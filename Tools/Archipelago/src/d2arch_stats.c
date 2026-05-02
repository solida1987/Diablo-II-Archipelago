/* ================================================================
 * d2arch_stats.c — F1 Statistics / Logbook page (1.9.0)
 * ================================================================ */
#ifndef D2ARCH_STATS_C
#define D2ARCH_STATS_C

#include "d2arch_stats.h"
#include <time.h>

/* ----------------------------------------------------------------
 * Global state
 * ---------------------------------------------------------------- */
CharStats     g_charStats;
LifetimeStats g_lifetimeStats;

/* Tracks the last char name we aggregated into lifetime, so calling
 * Stats_AggregateLifetime repeatedly per tick doesn't double-count.
 * The actual de-dup is delta-based (we record the per-char baseline
 * at last aggregation and only add the delta since then). */
static char     s_lifetimeAggCharName[64] = {0};
static CharStats s_lifetimeAggBaseline = {0};
static BOOL     s_lifetimeBaselineValid = FALSE;

/* Tick clock for playtime accumulation. Every full second we
 * increment playtimeSec by 1 — fewer writes than per-frame.
 * lastTickMs is reset on character load so a paused alt-tab
 * doesn't pump bogus seconds in. */
static DWORD    s_playtimeLastTickMs = 0;

/* Player death edge detector — we increment deaths when the player
 * mode transitions from "alive" to "dead/death", not while it's
 * staying dead. */
static DWORD    s_lastPlayerMode = 0xFFFFFFFFu;

/* Last-seen area for gold-by-act bucketing. */
static int      s_lastSeenArea = -1;

/* F1 page scroll state (independent for each side). */
static int      s_scrollLeft  = 0;
static int      s_scrollRight = 0;

/* Reload trigger so user can edit INI keys for the stats page
 * layout without a restart. Each render loads once. */
static int      s_statsLayoutLoaded = 0;

/* ----------------------------------------------------------------
 * Layout (INI-driven, [Stats] section)
 *
 * Defaults are aligned with the locked book parchment bounds from
 * the [Collection] section (LeftPageX=58/W=260, RightPageX=384/W=254)
 * so labels + values stay inside the parchment area. Override any
 * value via the [Stats] section in d2arch.ini.
 * ---------------------------------------------------------------- */
static int g_statsLpX        = 58;      /* matches Collection LeftPageX */
static int g_statsLpW        = 260;     /* matches Collection LeftPageW */
static int g_statsRpX        = 384;     /* matches Collection RightPageX */
static int g_statsRpW        = 254;     /* matches Collection RightPageW */
static int g_statsTitleX     = 410;     /* "Logbook ..." X (centered, mode 1) */
static int g_statsTitleY     = 72;
static int g_statsHdrLeftX   = 130;     /* "THIS CHARACTER" X (centered) */
static int g_statsHdrLeftY   = 100;
static int g_statsHdrRightX  = 450;     /* "LIFETIME" X (centered) */
static int g_statsHdrRightY  = 100;
static int g_statsListTopY   = 124;     /* first stat row Y */
static int g_statsListBotY   = 386;     /* last visible row bottom Y */
static int g_statsRowH       = 14;      /* row height in px */
static int g_statsLabelOffX  = 4;       /* label X offset from page-X */
static int g_statsValueOffX  = 240;     /* right-aligned value X offset from page-X
                                         * (must be < page-W to keep numbers inside) */
static int g_statsSectionPadY= 4;

/* Read the [Stats] section from d2arch.ini. Most users won't touch
 * this — defaults are tuned for the locked book bounds. */
extern void GetArchDir(char* outDir, int outSize);

static const char* k_iniName(void) {
    static char path[MAX_PATH] = {0};
    if (!path[0]) {
        char dir[MAX_PATH] = {0};
        GetArchDir(dir, MAX_PATH);
        _snprintf(path, sizeof(path) - 1, "%sd2arch.ini", dir);
        path[sizeof(path) - 1] = 0;
    }
    return path;
}

#define STATS_INI(key, var, defVal) \
    var = GetPrivateProfileIntA("Stats", key, (defVal), iniPath)

static void Stats_LoadIniLayout(void) {
    const char* iniPath = k_iniName();
    STATS_INI("LpX",          g_statsLpX,        58);
    STATS_INI("LpW",          g_statsLpW,        260);
    STATS_INI("RpX",          g_statsRpX,        384);
    STATS_INI("RpW",          g_statsRpW,        254);
    STATS_INI("TitleX",       g_statsTitleX,     410);
    STATS_INI("TitleY",       g_statsTitleY,     72);
    STATS_INI("HdrLeftX",     g_statsHdrLeftX,   130);
    STATS_INI("HdrLeftY",     g_statsHdrLeftY,   100);
    STATS_INI("HdrRightX",    g_statsHdrRightX,  450);
    STATS_INI("HdrRightY",    g_statsHdrRightY,  100);
    STATS_INI("ListTopY",     g_statsListTopY,   124);
    STATS_INI("ListBotY",     g_statsListBotY,   386);
    STATS_INI("RowH",         g_statsRowH,       14);
    STATS_INI("LabelOffX",    g_statsLabelOffX,  4);
    STATS_INI("ValueOffX",    g_statsValueOffX,  240);
    STATS_INI("SectionPadY",  g_statsSectionPadY,4);
    s_statsLayoutLoaded = 1;
}

/* ----------------------------------------------------------------
 * Path helpers
 * ---------------------------------------------------------------- */
static void Stats_BuildSidecarPath(const char* charName,
                                   char* outPath, size_t outSize) {
    char dir[MAX_PATH] = {0};
    /* 1.9.0: per-character stats live in Game/Save/ next to the .d2s
     * (same move as state/slots/collections). */
    GetCharFileDir(dir, MAX_PATH);
    _snprintf(outPath, outSize - 1, "%sap_stats_%s.dat", dir, charName);
    outPath[outSize - 1] = '\0';
}

static void Stats_BuildLifetimePath(char* outPath, size_t outSize) {
    char dir[MAX_PATH] = {0};
    GetArchDir(dir, MAX_PATH);
    _snprintf(outPath, outSize - 1, "%sap_stats_lifetime.dat", dir);
    outPath[outSize - 1] = '\0';
}

/* ----------------------------------------------------------------
 * Persistence — character
 * ---------------------------------------------------------------- */
void Stats_LoadForCharacter(const char* charName) {
    char path[MAX_PATH];
    Stats_BuildSidecarPath(charName, path, sizeof(path));

    /* Reset memory first — fresh state is the safe default. */
    memset(&g_charStats, 0, sizeof(g_charStats));
    g_charStats.magic   = STATS_FILE_MAGIC;
    g_charStats.version = STATS_FILE_VERSION;

    s_lifetimeBaselineValid = FALSE;
    memset(s_lifetimeAggCharName, 0, sizeof(s_lifetimeAggCharName));

    FILE* f = fopen(path, "rb");
    if (!f) {
        g_charStats.firstSeenUnix = (uint64_t)time(NULL);
        Log("Stats_Load: no sidecar for '%s' — starting fresh\n", charName);
        return;
    }
    CharStats onDisk;
    size_t got = fread(&onDisk, 1, sizeof(onDisk), f);
    fclose(f);

    if (got == sizeof(onDisk) && onDisk.magic == STATS_FILE_MAGIC &&
        onDisk.version <= STATS_FILE_VERSION) {
        g_charStats = onDisk;
        /* Treat the loaded values as the lifetime-baseline so we don't
         * double-count anything already aggregated previously. */
        s_lifetimeAggBaseline = g_charStats;
        s_lifetimeBaselineValid = TRUE;
        _snprintf(s_lifetimeAggCharName, sizeof(s_lifetimeAggCharName) - 1,
                  "%s", charName);
        Log("Stats_Load: '%s' OK (playtime=%llu sec, mons=%llu)\n",
            charName, (unsigned long long)g_charStats.playtimeSec,
            (unsigned long long)g_charStats.monstersKilled);
    } else {
        Log("Stats_Load: corrupt sidecar for '%s' — resetting\n", charName);
        memset(&g_charStats, 0, sizeof(g_charStats));
        g_charStats.magic   = STATS_FILE_MAGIC;
        g_charStats.version = STATS_FILE_VERSION;
        g_charStats.firstSeenUnix = (uint64_t)time(NULL);
    }
}

void Stats_SaveForCharacter(const char* charName) {
    char path[MAX_PATH];
    Stats_BuildSidecarPath(charName, path, sizeof(path));
    g_charStats.lastSeenUnix = (uint64_t)time(NULL);
    FILE* f = fopen(path, "wb");
    if (!f) {
        Log("Stats_Save: cannot open '%s'\n", path);
        return;
    }
    fwrite(&g_charStats, 1, sizeof(g_charStats), f);
    fclose(f);
}

/* ----------------------------------------------------------------
 * Persistence — lifetime
 * ---------------------------------------------------------------- */
void Stats_LoadLifetime(void) {
    char path[MAX_PATH];
    Stats_BuildLifetimePath(path, sizeof(path));
    memset(&g_lifetimeStats, 0, sizeof(g_lifetimeStats));
    g_lifetimeStats.magic   = STATS_FILE_MAGIC;
    g_lifetimeStats.version = STATS_FILE_VERSION;
    FILE* f = fopen(path, "rb");
    if (!f) {
        Log("Stats_LoadLifetime: no file — starting fresh\n");
        return;
    }
    LifetimeStats onDisk;
    size_t got = fread(&onDisk, 1, sizeof(onDisk), f);
    fclose(f);
    if (got == sizeof(onDisk) && onDisk.magic == STATS_FILE_MAGIC &&
        onDisk.version <= STATS_FILE_VERSION) {
        g_lifetimeStats = onDisk;
        Log("Stats_LoadLifetime: OK (chars=%llu, playtime=%llu)\n",
            (unsigned long long)g_lifetimeStats.charactersPlayed,
            (unsigned long long)g_lifetimeStats.totalPlaytimeSec);
    } else {
        Log("Stats_LoadLifetime: corrupt — resetting\n");
        memset(&g_lifetimeStats, 0, sizeof(g_lifetimeStats));
        g_lifetimeStats.magic   = STATS_FILE_MAGIC;
        g_lifetimeStats.version = STATS_FILE_VERSION;
    }
}

void Stats_SaveLifetime(void) {
    char path[MAX_PATH];
    Stats_BuildLifetimePath(path, sizeof(path));
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(&g_lifetimeStats, 1, sizeof(g_lifetimeStats), f);
    fclose(f);
}

/* ----------------------------------------------------------------
 * Lifetime aggregation
 *
 * Approach: we keep a "baseline" snapshot of the char's stats from
 * the last time we aggregated. The delta between current and baseline
 * is added to the lifetime totals; the baseline becomes current.
 *
 * This handles repeated calls (every save tick) without double-counting
 * AND it handles the "first time we see this char" case where the
 * full current value is the delta.
 *
 * For "best ever" fields (act completion times) we just min() into
 * the lifetime slot. For "characters played" counter, we increment
 * once per unique-char-name when we first see them.
 * ---------------------------------------------------------------- */
void Stats_AggregateLifetime(const char* charName) {
    if (!charName || !charName[0]) return;

    /* If this is a new char (different name from last aggregation)
     * count it once and reset the baseline to zeros. The dedup uses
     * a stable flag in g_charStats.reserved[0] so reloading the same
     * char doesn't double-count it as another "characters played". */
    if (strcmp(s_lifetimeAggCharName, charName) != 0) {
        if (!s_lifetimeBaselineValid) {
            if (g_charStats.reserved[0] == 0) {
                g_lifetimeStats.charactersPlayed++;
                g_charStats.reserved[0] = 1;
            }
            memset(&s_lifetimeAggBaseline, 0, sizeof(s_lifetimeAggBaseline));
        }
        _snprintf(s_lifetimeAggCharName,
                  sizeof(s_lifetimeAggCharName) - 1, "%s", charName);
        s_lifetimeBaselineValid = TRUE;
    }

    /* Macro adds the (current - baseline) delta into a lifetime field. */
    #define DELTA_ADD(charField, lifeField)                              \
        do {                                                             \
            uint64_t cur = g_charStats.charField;                        \
            uint64_t bas = s_lifetimeAggBaseline.charField;              \
            if (cur >= bas) g_lifetimeStats.lifeField += (cur - bas);    \
            s_lifetimeAggBaseline.charField = cur;                       \
        } while (0)

    DELTA_ADD(playtimeSec,           totalPlaytimeSec);
    DELTA_ADD(monstersKilled,        totalMonstersKilled);
    DELTA_ADD(championsKilled,       totalChampionsKilled);
    DELTA_ADD(uniquesKilled,         totalUniquesKilled);
    DELTA_ADD(bossesKilled,          totalBossesKilled);
    DELTA_ADD(superuniquesKilled,    totalSuperuniquesKilled);
    DELTA_ADD(playerDeaths,          totalPlayerDeaths);
    DELTA_ADD(itemsPickedUp,         totalItemsPickedUp);
    DELTA_ADD(setItemsPickedUp,      totalSetItemsPickedUp);
    DELTA_ADD(uniqueItemsPickedUp,   totalUniqueItemsPickedUp);
    DELTA_ADD(runesPickedUp,         totalRunesPickedUp);
    DELTA_ADD(gemsPickedUp,          totalGemsPickedUp);
    DELTA_ADD(goldCollected,         totalGoldCollected);
    DELTA_ADD(goldSpent,             totalGoldSpent);
    DELTA_ADD(itemsSoldToVendor,     totalItemsSoldToVendor);
    DELTA_ADD(itemsBoughtFromVendor, totalItemsBoughtFromVendor);
    DELTA_ADD(gambledItems,          totalGambledItems);
    DELTA_ADD(cubeTransmutes,        totalCubeTransmutes);
    DELTA_ADD(runewordsCreated,      totalRunewordsCreated);
    DELTA_ADD(itemsIdentified,       totalItemsIdentified);
    DELTA_ADD(waypointsUsed,         totalWaypointsUsed);
    DELTA_ADD(portalsTaken,          totalPortalsTaken);
    DELTA_ADD(doorsOpened,           totalDoorsOpened);
    DELTA_ADD(chestsOpened,          totalChestsOpened);
    DELTA_ADD(barrelsBroken,         totalBarrelsBroken);
    DELTA_ADD(jarsBroken,            totalJarsBroken);
    DELTA_ADD(shrinesActivated,      totalShrinesActivated);
    DELTA_ADD(wellsDrunk,            totalWellsDrunk);
    DELTA_ADD(corpsesLooted,         totalCorpsesLooted);
    DELTA_ADD(monolithsActivated,    totalMonolithsActivated);
    DELTA_ADD(questObjectsInteracted,totalQuestObjectsInteracted);
    DELTA_ADD(questsCompleted,       totalQuestsCompleted);
    DELTA_ADD(potionsConsumed,       totalPotionsConsumed);
    DELTA_ADD(scrollsUsed,           totalScrollsUsed);
    DELTA_ADD(tomesUsed,             totalTomesUsed);
    DELTA_ADD(cowLevelEntries,       totalCowLevelEntries);
    DELTA_ADD(tristramVisits,        totalTristramVisits);
    DELTA_ADD(pandemoniumRunsCompleted, totalPandemoniumRunsCompleted);

    for (int a = 0; a <= STATS_ACT_COUNT; a++) {
        DELTA_ADD(goldByAct[a], totalGoldByAct[a]);
    }

    #undef DELTA_ADD

    /* Best (fastest) act-complete times — min() into lifetime. */
    for (int d = 0; d < STATS_DIFF_COUNT; d++) {
        for (int a = 0; a < STATS_ACT_COUNT; a++) {
            uint32_t cur = g_charStats.actCompleteSec[d][a];
            if (cur == 0) continue;
            uint32_t* best = &g_lifetimeStats.bestActSec[d][a];
            if (*best == 0 || cur < *best) *best = cur;
        }
    }
}

/* ----------------------------------------------------------------
 * Tick driver — called from Coll_OnGameTick every frame
 * ---------------------------------------------------------------- */
void Stats_OnGameTick(void* pPlayerUnit) {
    if (!pPlayerUnit) return;

    DWORD nowMs = GetTickCount();

    /* Playtime: accumulate every full second of wall-clock between ticks.
     * If nowMs-lastTickMs is huge (>10 sec) treat it as a pause/alt-tab
     * and skip — we don't want to credit time the user wasn't playing. */
    if (s_playtimeLastTickMs == 0) {
        s_playtimeLastTickMs = nowMs;
    } else {
        DWORD delta = nowMs - s_playtimeLastTickMs;
        if (delta < 10000) {
            /* accumulate fractional seconds via leftover state */
            static DWORD s_leftoverMs = 0;
            s_leftoverMs += delta;
            while (s_leftoverMs >= 1000) {
                g_charStats.playtimeSec++;
                s_leftoverMs -= 1000;
            }
        }
        s_playtimeLastTickMs = nowMs;
    }

    /* Player death edge detect. The player unit's dwMode transitions
     * to MODE_DEATH (0) or MODE_DEAD (12) when they die. We've used
     * the same offsets in d2arch_gameloop.c (mode @ unit+0x10). */
    DWORD mode = 0xFFFFFFFFu;
    __try { mode = *(DWORD*)((BYTE*)pPlayerUnit + 0x10); }
    __except(EXCEPTION_EXECUTE_HANDLER) { mode = 0xFFFFFFFFu; }
    /* Edge: alive (was 1/2/4/5/6) → dead (0 or 12). Skip the 0xFFFFFFFFu
     * sentinel which means "haven't read a sane value yet". */
    if (mode != 0xFFFFFFFFu &&
        s_lastPlayerMode != 0xFFFFFFFFu &&
        s_lastPlayerMode != 0 && s_lastPlayerMode != 12 &&
        (mode == 0 || mode == 12)) {
        g_charStats.playerDeaths++;
    }
    s_lastPlayerMode = mode;

    /* Area-transition edge detect. Polls GetCurrentArea() and fires
     * Stats_OnAreaEnter when it changes. The first observation per
     * character (s_lastSeenArea == -1) doesn't count as a transition
     * — that's the initial load, not an "entry" event. */
    {
        extern int GetCurrentArea(void);
        int curArea = GetCurrentArea();
        if (curArea > 0 && curArea != s_lastSeenArea) {
            if (s_lastSeenArea != -1) Stats_OnAreaEnter(curArea);
            s_lastSeenArea = curArea;
        }
    }
}

/* ----------------------------------------------------------------
 * Event hooks
 * ---------------------------------------------------------------- */
void Stats_OnMonsterKill(int txtId, int typeFlag, int currentArea, int currentDifficulty) {
    (void)currentArea;
    g_charStats.monstersKilled++;
    if (currentDifficulty >= 0 && currentDifficulty < STATS_DIFF_COUNT)
        g_charStats.monstersByDifficulty[currentDifficulty]++;
    if (typeFlag & 0x01) g_charStats.uniquesKilled++;
    if (typeFlag & 0x02) g_charStats.superuniquesKilled++;
    if (typeFlag & 0x04) g_charStats.championsKilled++;
    /* "Tracked" big bosses tagged in d2arch_quests.c via IsTrackedBoss
     * (Andariel/Duriel/Mephisto/Diablo/Baal/Izual/Radament/Summoner/
     * Smith/Council). The caller can pass the typeFlag with bit 8 set
     * to indicate a boss kill — we just check bit 8 here. */
    if (typeFlag & 0x80) g_charStats.bossesKilled++;
    (void)txtId;
}

void Stats_OnPlayerDeath(void) {
    g_charStats.playerDeaths++;
}

void Stats_OnQuestComplete(void) {
    g_charStats.questsCompleted++;
}

void Stats_OnCubeTransmute(void) {
    g_charStats.cubeTransmutes++;
}

/* Stamp the act-completion time on the named act/difficulty if not
 * already set. The current playtime is the timestamp. Called from the
 * quest-completion handler when an act-boss quest completes. */
void Stats_OnActComplete(int difficulty, int actIdx) {
    if (difficulty < 0 || difficulty >= STATS_DIFF_COUNT) return;
    if (actIdx    < 0 || actIdx    >= STATS_ACT_COUNT)   return;
    if (g_charStats.actCompleteSec[difficulty][actIdx] != 0) return;
    g_charStats.actCompleteSec[difficulty][actIdx] =
        (uint32_t)g_charStats.playtimeSec;
}

/* Called from Ubers_OnUnitDeathScan after Ubers_FinaleAllDead returns
 * TRUE — i.e. the player has cleared a full Pandemonium finale set
 * (Uber Mephisto + Uber Diablo + Uber Baal all dead). */
void Stats_OnPandemoniumRunComplete(void) {
    g_charStats.pandemoniumRunsCompleted++;
}

/* Called from Stats_OnGameTick whenever the player's current area-id
 * changes. Counts visits to special "memorable" zones — Cow Level
 * (39, Moo Moo Farm) and Tristram (38, the Cain rescue zone). */
void Stats_OnAreaEnter(int newAreaId) {
    if (newAreaId == 39) g_charStats.cowLevelEntries++;
    else if (newAreaId == 38) g_charStats.tristramVisits++;
    /* 1.9.2: Extra check Cat 1 — first cow-level entry per difficulty.
     * Forward-declared because d2arch_extrachecks.c is included AFTER
     * d2arch_stats.c in the unity build. Idempotent inside the callee
     * (bitmap dedup) so safe to call on every entry. */
    if (newAreaId == 39) {
        extern void Extra_OnCowLevelEnter(int diff);
        extern int  g_currentDifficulty;
        Extra_OnCowLevelEnter(g_currentDifficulty);
    }
}

/* Fired when an item's IFLAG_RUNEWORD bit transitions 0→1 — i.e. the
 * player just socketed the final rune that completes a runeword. */
void Stats_OnRunewordCreated(void) {
    g_charStats.runewordsCreated++;
}

/* Fired when an item's IFLAG_IDENTIFIED bit transitions 0→1 — i.e.
 * the player identified an item via Cain talk, identify scroll, or
 * identify tome. */
void Stats_OnItemIdentified(void) {
    g_charStats.itemsIdentified++;
}

/* Map a level/area ID to an act number (1..5), 0 = town/menu/unknown.
 * D2 1.10f area boundaries: Act 1 = 1..39, Act 2 = 40..74, Act 3 =
 * 75..102, Act 4 = 103..108, Act 5 = 109..135. */
static int Stats_AreaToAct(int areaId) {
    if (areaId <= 0)   return 0;
    if (areaId < 40)   return 1;
    if (areaId < 75)   return 2;
    if (areaId < 103)  return 3;
    if (areaId < 109)  return 4;
    if (areaId < 136)  return 5;
    return 0;
}

void Stats_OnGoldDelta(int64_t delta, int currentArea) {
    if (delta == 0) return;
    if (delta < 0) {
        g_charStats.goldSpent += (uint64_t)(-delta);
        return;
    }
    int act = Stats_AreaToAct(currentArea);
    g_charStats.goldCollected += (uint64_t)delta;
    if (act >= 0 && act <= STATS_ACT_COUNT) {
        g_charStats.goldByAct[act] += (uint64_t)delta;
    } else {
        g_charStats.goldByAct[0] += (uint64_t)delta;
    }
}

void Stats_OnItemPickedUp(int quality, int slotIdx) {
    g_charStats.itemsPickedUp++;
    /* quality: 5=set, 7=unique (D2 1.10f) */
    if (quality == 5) g_charStats.setItemsPickedUp++;
    if (quality == 7) g_charStats.uniqueItemsPickedUp++;
    /* Coll catalog ranges — slot 128..160 = runes, 160..195 = gems */
    if (slotIdx >= 128 && slotIdx < 160) g_charStats.runesPickedUp++;
    if (slotIdx >= 160 && slotIdx < 195) g_charStats.gemsPickedUp++;
}

/* 1.9.2 — accessor for d2arch_customgoal.c which is included BEFORE
 * stats.c in the unity build (so it can't reach g_charStats directly
 * via struct member access). Returns the same lifetime gold counter
 * the F1 Collection page uses for the gold-target check. */
uint64_t g_charStats_lifetimeGold(void) {
    return g_charStats.goldCollected;
}

void Stats_OnCharacterChange(void) {
    s_playtimeLastTickMs = 0;       /* reset wall-clock anchor */
    s_lastPlayerMode     = 0xFFFFFFFFu;
    s_lastSeenArea       = -1;
    s_scrollLeft         = 0;
    s_scrollRight        = 0;
}

void Stats_HandleScroll(int side, int delta) {
    int* target = (side == 0) ? &s_scrollLeft : &s_scrollRight;
    *target -= delta;  /* wheel-up (delta>0) scrolls UP, decreases offset */
    if (*target < 0) *target = 0;
    /* Upper bound clamped at render time when row count is known. */
}

/* ----------------------------------------------------------------
 * F1 PAGE RENDER
 * ---------------------------------------------------------------- */

/* Format helpers ---------------------------------------------------*/

static void Stats_FormatHMS(uint64_t totalSec, char* out, size_t outSz) {
    uint64_t hr  = totalSec / 3600;
    uint64_t mn  = (totalSec / 60) % 60;
    uint64_t sc  = totalSec % 60;
    if (hr > 0)
        _snprintf(out, outSz - 1, "%lluh %02llum %02llus", hr, mn, sc);
    else if (mn > 0)
        _snprintf(out, outSz - 1, "%llum %02llus", mn, sc);
    else
        _snprintf(out, outSz - 1, "%llus", sc);
    out[outSz - 1] = 0;
}

static void Stats_FormatActTime(uint32_t sec, char* out, size_t outSz) {
    if (sec == 0) {
        /* Plain ASCII only — the wide-char convert in the row renderer
         * casts each byte individually, so multi-byte UTF-8 like an
         * em-dash would render as 3 garbage glyphs. "-" stays a single
         * dash everywhere. */
        _snprintf(out, outSz - 1, "-");
        out[outSz - 1] = 0;
        return;
    }
    Stats_FormatHMS((uint64_t)sec, out, outSz);
}

static void Stats_FormatU64(uint64_t v, char* out, size_t outSz) {
    /* Group thousands with comma for readability. */
    char tmp[32];
    _snprintf(tmp, sizeof(tmp) - 1, "%llu", (unsigned long long)v);
    tmp[sizeof(tmp) - 1] = 0;
    int len = (int)strlen(tmp);
    int outIdx = 0;
    int firstGroup = len % 3;
    if (firstGroup == 0) firstGroup = 3;
    for (int i = 0; i < len && outIdx < (int)outSz - 2; i++) {
        if (i > 0 && (i - firstGroup) % 3 == 0 && (i - firstGroup) >= 0) {
            out[outIdx++] = ',';
        }
        out[outIdx++] = tmp[i];
    }
    out[outIdx] = 0;
}

/* Each row of the list is described by this struct so we can build
 * a flat array, then render only the rows visible after scroll. */
typedef enum {
    STATS_ROW_HEADER = 0,    /* section header — bold, gold color */
    STATS_ROW_VALUE          /* normal label-value pair */
} StatsRowKind;

typedef struct {
    StatsRowKind kind;
    const char*  label;       /* for VALUE: stat name. for HEADER: section text */
    char         valueBuf[64]; /* formatted value string (VALUE rows only) */
} StatsRow;

#define STATS_MAX_ROWS  128

static int Stats_BuildCharRows(StatsRow* rows, int maxRows) {
    int n = 0;
    char buf[64];

    #define HDR(text)  do { if (n<maxRows) { rows[n].kind=STATS_ROW_HEADER; rows[n].label=(text); rows[n].valueBuf[0]=0; n++; } } while(0)
    #define ROW(name, value)  do {                                       \
        if (n<maxRows) {                                                  \
            rows[n].kind=STATS_ROW_VALUE; rows[n].label=(name);           \
            _snprintf(rows[n].valueBuf, sizeof(rows[n].valueBuf)-1,       \
                      "%s", (value));                                     \
            rows[n].valueBuf[sizeof(rows[n].valueBuf)-1]=0;               \
            n++;                                                          \
        }                                                                 \
    } while(0)

    HDR("TIME");
    Stats_FormatHMS(g_charStats.playtimeSec, buf, sizeof(buf));
    ROW("Playtime", buf);

    HDR("COMBAT");
    Stats_FormatU64(g_charStats.monstersKilled, buf, sizeof(buf));     ROW("Monsters Killed", buf);
    Stats_FormatU64(g_charStats.championsKilled, buf, sizeof(buf));    ROW("Champions Killed", buf);
    Stats_FormatU64(g_charStats.uniquesKilled, buf, sizeof(buf));      ROW("Unique Monsters", buf);
    Stats_FormatU64(g_charStats.superuniquesKilled, buf, sizeof(buf)); ROW("SuperUniques", buf);
    Stats_FormatU64(g_charStats.bossesKilled, buf, sizeof(buf));       ROW("Bosses Killed", buf);
    Stats_FormatU64(g_charStats.playerDeaths, buf, sizeof(buf));       ROW("Player Deaths", buf);
    Stats_FormatU64(g_charStats.monstersByDifficulty[0], buf, sizeof(buf)); ROW("  on Normal", buf);
    Stats_FormatU64(g_charStats.monstersByDifficulty[1], buf, sizeof(buf)); ROW("  on Nightmare", buf);
    Stats_FormatU64(g_charStats.monstersByDifficulty[2], buf, sizeof(buf)); ROW("  on Hell", buf);

    HDR("LOOT");
    Stats_FormatU64(g_charStats.itemsPickedUp, buf, sizeof(buf));      ROW("Items Picked Up", buf);
    Stats_FormatU64(g_charStats.setItemsPickedUp, buf, sizeof(buf));   ROW("  Set Items", buf);
    Stats_FormatU64(g_charStats.uniqueItemsPickedUp, buf, sizeof(buf)); ROW("  Unique Items", buf);
    Stats_FormatU64(g_charStats.runesPickedUp, buf, sizeof(buf));      ROW("  Runes", buf);
    Stats_FormatU64(g_charStats.gemsPickedUp, buf, sizeof(buf));       ROW("  Gems", buf);
    Stats_FormatU64(g_charStats.goldCollected, buf, sizeof(buf));      ROW("Gold Collected", buf);
    Stats_FormatU64(g_charStats.goldByAct[1], buf, sizeof(buf));       ROW("  Act 1 Gold", buf);
    Stats_FormatU64(g_charStats.goldByAct[2], buf, sizeof(buf));       ROW("  Act 2 Gold", buf);
    Stats_FormatU64(g_charStats.goldByAct[3], buf, sizeof(buf));       ROW("  Act 3 Gold", buf);
    Stats_FormatU64(g_charStats.goldByAct[4], buf, sizeof(buf));       ROW("  Act 4 Gold", buf);
    Stats_FormatU64(g_charStats.goldByAct[5], buf, sizeof(buf));       ROW("  Act 5 Gold", buf);

    HDR("ECONOMY");
    Stats_FormatU64(g_charStats.goldSpent, buf, sizeof(buf));          ROW("Gold Spent", buf);
    Stats_FormatU64(g_charStats.itemsSoldToVendor, buf, sizeof(buf));  ROW("Items Sold", buf);
    Stats_FormatU64(g_charStats.itemsBoughtFromVendor, buf, sizeof(buf)); ROW("Items Bought", buf);
    Stats_FormatU64(g_charStats.gambledItems, buf, sizeof(buf));       ROW("Items Gambled", buf);

    HDR("CRAFTING");
    Stats_FormatU64(g_charStats.cubeTransmutes, buf, sizeof(buf));     ROW("Cube Transmutes", buf);
    Stats_FormatU64(g_charStats.runewordsCreated, buf, sizeof(buf));   ROW("Runewords Created", buf);
    Stats_FormatU64(g_charStats.itemsIdentified, buf, sizeof(buf));    ROW("Items Identified", buf);

    HDR("PROGRESSION");
    Stats_FormatU64(g_charStats.questsCompleted, buf, sizeof(buf));    ROW("Quests Completed", buf);
    static const char* k_diff[3] = {"Normal", "Nightmare", "Hell"};
    for (int d = 0; d < STATS_DIFF_COUNT; d++) {
        char hdrText[40];
        _snprintf(hdrText, sizeof(hdrText) - 1, "  %s clear times", k_diff[d]);
        hdrText[sizeof(hdrText) - 1] = 0;
        /* Sub-header (use VALUE row with empty value to act like a sub-label) */
        if (n < maxRows) {
            rows[n].kind = STATS_ROW_VALUE;
            rows[n].label = "";
            _snprintf(rows[n].valueBuf, sizeof(rows[n].valueBuf) - 1, "%s", "");
            n++;
        }
        for (int a = 0; a < STATS_ACT_COUNT; a++) {
            char rowName[24];
            _snprintf(rowName, sizeof(rowName) - 1, "  %s Act %d", k_diff[d], a + 1);
            rowName[sizeof(rowName) - 1] = 0;
            Stats_FormatActTime(g_charStats.actCompleteSec[d][a], buf, sizeof(buf));
            /* allocate a stable label string per row — the rows array uses
             * pointers, so we need a per-row string. Stash it in valueBuf? No,
             * we put the label in label and value after. Use a static ring
             * of label strings. */
            static char s_actLabels[STATS_DIFF_COUNT * STATS_ACT_COUNT][24];
            int idx = d * STATS_ACT_COUNT + a;
            _snprintf(s_actLabels[idx], 23, "%s", rowName);
            s_actLabels[idx][23] = 0;
            ROW(s_actLabels[idx], buf);
        }
    }

    HDR("EXPLORATION");
    Stats_FormatU64(g_charStats.waypointsUsed, buf, sizeof(buf));         ROW("Waypoints Used", buf);
    Stats_FormatU64(g_charStats.portalsTaken, buf, sizeof(buf));          ROW("Portals Taken", buf);
    Stats_FormatU64(g_charStats.doorsOpened, buf, sizeof(buf));           ROW("Doors Opened", buf);
    Stats_FormatU64(g_charStats.chestsOpened, buf, sizeof(buf));          ROW("Chests Opened", buf);
    Stats_FormatU64(g_charStats.barrelsBroken, buf, sizeof(buf));         ROW("Barrels Broken", buf);
    Stats_FormatU64(g_charStats.jarsBroken, buf, sizeof(buf));            ROW("Jars/Urns Broken", buf);
    Stats_FormatU64(g_charStats.shrinesActivated, buf, sizeof(buf));      ROW("Shrines Activated", buf);
    Stats_FormatU64(g_charStats.wellsDrunk, buf, sizeof(buf));            ROW("Wells Used", buf);
    Stats_FormatU64(g_charStats.corpsesLooted, buf, sizeof(buf));         ROW("Corpses Looted", buf);
    Stats_FormatU64(g_charStats.monolithsActivated, buf, sizeof(buf));    ROW("Stones Flipped", buf);
    Stats_FormatU64(g_charStats.questObjectsInteracted, buf, sizeof(buf));ROW("Quest Objects Used", buf);
    Stats_FormatU64(g_charStats.cowLevelEntries, buf, sizeof(buf));       ROW("Cow Level Trips", buf);
    Stats_FormatU64(g_charStats.tristramVisits, buf, sizeof(buf));        ROW("Tristram Visits", buf);

    HDR("CONSUMABLES");
    Stats_FormatU64(g_charStats.potionsConsumed, buf, sizeof(buf));  ROW("Potions Used", buf);
    Stats_FormatU64(g_charStats.scrollsUsed, buf, sizeof(buf));      ROW("Scrolls Used", buf);
    Stats_FormatU64(g_charStats.tomesUsed, buf, sizeof(buf));        ROW("Tomes Used", buf);

    HDR("EVENTS");
    Stats_FormatU64(g_charStats.pandemoniumRunsCompleted, buf, sizeof(buf));
    ROW("Pandemonium Runs", buf);

    /* 1.9.0 — Bonus AP check progress. Only shown when at least one
     * bonus category is enabled in the current slot_data / d2arch.ini. */
    {
        extern int  Bonus_GetSlotCount(int category, int diff);
        extern int  Bonus_GetQuota(int category);
        extern BOOL Bonus_IsCategoryEnabled(int category);
        extern int  Bonus_CountFiredSetPickups(void);
        extern BOOL Bonus_IsGoldMilestoneFired(int idx);
        extern uint64_t Bonus_GetGoldMilestoneThreshold(int idx);

        BOOL anyOn = FALSE;
        for (int c = 0; c < 6; c++) if (Bonus_IsCategoryEnabled(c)) { anyOn = TRUE; break; }
        if (anyOn) {
            HDR("AP CHECK PROGRESS");
            const char* catNames[4] = { "Shrines", "Urns", "Barrels", "Chests" };
            for (int c = 0; c < 4; c++) {
                if (!Bonus_IsCategoryEnabled(c)) continue;
                int q = Bonus_GetQuota(c);
                /* Per-difficulty line: "<Cat> N: A/Q  NM: A/Q  H: A/Q" */
                _snprintf(buf, sizeof(buf), "N %d/%d  NM %d/%d  H %d/%d",
                    Bonus_GetSlotCount(c, 0), q,
                    Bonus_GetSlotCount(c, 1), q,
                    Bonus_GetSlotCount(c, 2), q);
                ROW(catNames[c], buf);
            }
            if (Bonus_IsCategoryEnabled(4)) { /* set pickups */
                _snprintf(buf, sizeof(buf), "%d / 127", Bonus_CountFiredSetPickups());
                ROW("Set Piece Pickups", buf);
            }
            if (Bonus_IsCategoryEnabled(5)) { /* gold milestones */
                int hit = 0, total = 17;
                for (int i = 0; i < total; i++) if (Bonus_IsGoldMilestoneFired(i)) hit++;
                _snprintf(buf, sizeof(buf), "%d / %d (next: %llu g)",
                    hit, total,
                    hit < total ? (unsigned long long)Bonus_GetGoldMilestoneThreshold(hit) : 0ull);
                ROW("Gold Milestones", buf);
            }
        }
    }

    #undef HDR
    #undef ROW
    return n;
}

static int Stats_BuildLifetimeRows(StatsRow* rows, int maxRows) {
    int n = 0;
    char buf[64];

    #define HDR(text)  do { if (n<maxRows) { rows[n].kind=STATS_ROW_HEADER; rows[n].label=(text); rows[n].valueBuf[0]=0; n++; } } while(0)
    #define ROW(name, value)  do {                                       \
        if (n<maxRows) {                                                  \
            rows[n].kind=STATS_ROW_VALUE; rows[n].label=(name);           \
            _snprintf(rows[n].valueBuf, sizeof(rows[n].valueBuf)-1,       \
                      "%s", (value));                                     \
            rows[n].valueBuf[sizeof(rows[n].valueBuf)-1]=0;               \
            n++;                                                          \
        }                                                                 \
    } while(0)

    HDR("ACCOUNT");
    Stats_FormatU64(g_lifetimeStats.charactersPlayed, buf, sizeof(buf));   ROW("Characters Played", buf);
    Stats_FormatHMS(g_lifetimeStats.totalPlaytimeSec, buf, sizeof(buf));   ROW("Total Playtime", buf);

    HDR("COMBAT (TOTAL)");
    Stats_FormatU64(g_lifetimeStats.totalMonstersKilled, buf, sizeof(buf));    ROW("Monsters Killed", buf);
    Stats_FormatU64(g_lifetimeStats.totalChampionsKilled, buf, sizeof(buf));   ROW("Champions Killed", buf);
    Stats_FormatU64(g_lifetimeStats.totalUniquesKilled, buf, sizeof(buf));     ROW("Unique Monsters", buf);
    Stats_FormatU64(g_lifetimeStats.totalSuperuniquesKilled, buf, sizeof(buf)); ROW("SuperUniques", buf);
    Stats_FormatU64(g_lifetimeStats.totalBossesKilled, buf, sizeof(buf));      ROW("Bosses Killed", buf);
    Stats_FormatU64(g_lifetimeStats.totalPlayerDeaths, buf, sizeof(buf));      ROW("Total Deaths", buf);

    HDR("LOOT (TOTAL)");
    Stats_FormatU64(g_lifetimeStats.totalItemsPickedUp, buf, sizeof(buf));     ROW("Items Picked Up", buf);
    Stats_FormatU64(g_lifetimeStats.totalSetItemsPickedUp, buf, sizeof(buf));  ROW("  Set Items", buf);
    Stats_FormatU64(g_lifetimeStats.totalUniqueItemsPickedUp, buf, sizeof(buf)); ROW("  Unique Items", buf);
    Stats_FormatU64(g_lifetimeStats.totalRunesPickedUp, buf, sizeof(buf));     ROW("  Runes", buf);
    Stats_FormatU64(g_lifetimeStats.totalGemsPickedUp, buf, sizeof(buf));      ROW("  Gems", buf);
    Stats_FormatU64(g_lifetimeStats.totalGoldCollected, buf, sizeof(buf));     ROW("Gold Collected", buf);
    Stats_FormatU64(g_lifetimeStats.totalGoldByAct[1], buf, sizeof(buf));      ROW("  Act 1 Gold", buf);
    Stats_FormatU64(g_lifetimeStats.totalGoldByAct[2], buf, sizeof(buf));      ROW("  Act 2 Gold", buf);
    Stats_FormatU64(g_lifetimeStats.totalGoldByAct[3], buf, sizeof(buf));      ROW("  Act 3 Gold", buf);
    Stats_FormatU64(g_lifetimeStats.totalGoldByAct[4], buf, sizeof(buf));      ROW("  Act 4 Gold", buf);
    Stats_FormatU64(g_lifetimeStats.totalGoldByAct[5], buf, sizeof(buf));      ROW("  Act 5 Gold", buf);

    HDR("ECONOMY (TOTAL)");
    Stats_FormatU64(g_lifetimeStats.totalGoldSpent, buf, sizeof(buf));         ROW("Gold Spent", buf);
    Stats_FormatU64(g_lifetimeStats.totalItemsSoldToVendor, buf, sizeof(buf)); ROW("Items Sold", buf);
    Stats_FormatU64(g_lifetimeStats.totalItemsBoughtFromVendor, buf, sizeof(buf)); ROW("Items Bought", buf);
    Stats_FormatU64(g_lifetimeStats.totalGambledItems, buf, sizeof(buf));      ROW("Items Gambled", buf);

    HDR("CRAFTING (TOTAL)");
    Stats_FormatU64(g_lifetimeStats.totalCubeTransmutes, buf, sizeof(buf));    ROW("Cube Transmutes", buf);
    Stats_FormatU64(g_lifetimeStats.totalRunewordsCreated, buf, sizeof(buf));  ROW("Runewords Created", buf);
    Stats_FormatU64(g_lifetimeStats.totalItemsIdentified, buf, sizeof(buf));   ROW("Items Identified", buf);

    HDR("PROGRESSION (TOTAL)");
    Stats_FormatU64(g_lifetimeStats.totalQuestsCompleted, buf, sizeof(buf));   ROW("Quests Completed", buf);
    static const char* k_diff2[3] = {"Normal", "Nightmare", "Hell"};
    for (int d = 0; d < STATS_DIFF_COUNT; d++) {
        for (int a = 0; a < STATS_ACT_COUNT; a++) {
            static char s_bestActLabels[STATS_DIFF_COUNT * STATS_ACT_COUNT][32];
            int idx = d * STATS_ACT_COUNT + a;
            _snprintf(s_bestActLabels[idx], 31, "Best %s Act %d", k_diff2[d], a + 1);
            s_bestActLabels[idx][31] = 0;
            Stats_FormatActTime(g_lifetimeStats.bestActSec[d][a], buf, sizeof(buf));
            ROW(s_bestActLabels[idx], buf);
        }
    }

    HDR("EXPLORATION (TOTAL)");
    Stats_FormatU64(g_lifetimeStats.totalWaypointsUsed, buf, sizeof(buf));         ROW("Waypoints Used", buf);
    Stats_FormatU64(g_lifetimeStats.totalPortalsTaken, buf, sizeof(buf));          ROW("Portals Taken", buf);
    Stats_FormatU64(g_lifetimeStats.totalDoorsOpened, buf, sizeof(buf));           ROW("Doors Opened", buf);
    Stats_FormatU64(g_lifetimeStats.totalChestsOpened, buf, sizeof(buf));          ROW("Chests Opened", buf);
    Stats_FormatU64(g_lifetimeStats.totalBarrelsBroken, buf, sizeof(buf));         ROW("Barrels Broken", buf);
    Stats_FormatU64(g_lifetimeStats.totalJarsBroken, buf, sizeof(buf));            ROW("Jars/Urns Broken", buf);
    Stats_FormatU64(g_lifetimeStats.totalShrinesActivated, buf, sizeof(buf));      ROW("Shrines Activated", buf);
    Stats_FormatU64(g_lifetimeStats.totalWellsDrunk, buf, sizeof(buf));            ROW("Wells Used", buf);
    Stats_FormatU64(g_lifetimeStats.totalCorpsesLooted, buf, sizeof(buf));         ROW("Corpses Looted", buf);
    Stats_FormatU64(g_lifetimeStats.totalMonolithsActivated, buf, sizeof(buf));    ROW("Stones Flipped", buf);
    Stats_FormatU64(g_lifetimeStats.totalQuestObjectsInteracted, buf, sizeof(buf));ROW("Quest Objects Used", buf);
    Stats_FormatU64(g_lifetimeStats.totalCowLevelEntries, buf, sizeof(buf));       ROW("Cow Level Trips", buf);
    Stats_FormatU64(g_lifetimeStats.totalTristramVisits, buf, sizeof(buf));        ROW("Tristram Visits", buf);

    HDR("CONSUMABLES (TOTAL)");
    Stats_FormatU64(g_lifetimeStats.totalPotionsConsumed, buf, sizeof(buf));   ROW("Potions Used", buf);
    Stats_FormatU64(g_lifetimeStats.totalScrollsUsed, buf, sizeof(buf));       ROW("Scrolls Used", buf);
    Stats_FormatU64(g_lifetimeStats.totalTomesUsed, buf, sizeof(buf));         ROW("Tomes Used", buf);

    HDR("EVENTS (TOTAL)");
    Stats_FormatU64(g_lifetimeStats.totalPandemoniumRunsCompleted, buf, sizeof(buf));
    ROW("Pandemonium Runs", buf);

    #undef HDR
    #undef ROW
    return n;
}

/* Render one column of stat rows with scroll. Returns the number of
 * rows that fit (used to clamp the scroll offset). */
static int Stats_RenderColumn(int pageX, int pageW,
                              StatsRow* rows, int rowCount,
                              int* scrollOffset) {
    /* fnText / fnFont are file-scope function pointers from
     * d2arch_collections.c via the unity build; no extern needed here.
     * Just guard against NULL during early init. */
    if (!fnText || !fnFont) return 0;

    int rowH = g_statsRowH;
    int firstY = g_statsListTopY;
    int lastY  = g_statsListBotY;
    int maxRowsVisible = (lastY - firstY) / rowH;
    if (maxRowsVisible < 1) maxRowsVisible = 1;

    /* Clamp scroll. */
    int maxScroll = rowCount - maxRowsVisible;
    if (maxScroll < 0) maxScroll = 0;
    if (*scrollOffset > maxScroll) *scrollOffset = maxScroll;
    if (*scrollOffset < 0) *scrollOffset = 0;

    int labelX = pageX + g_statsLabelOffX;
    int valueX = pageX + g_statsValueOffX;

    for (int i = 0; i < maxRowsVisible; i++) {
        int rowIdx = i + *scrollOffset;
        if (rowIdx >= rowCount) break;
        StatsRow* r = &rows[rowIdx];
        int y = firstY + i * rowH;

        wchar_t wlabel[80];
        int j = 0;
        for (; j < 79 && r->label[j]; j++) wlabel[j] = (wchar_t)(unsigned char)r->label[j];
        wlabel[j] = 0;

        if (r->kind == STATS_ROW_HEADER) {
            fnFont(0);
            fnText(wlabel, labelX, y + rowH - 2, 4 /* gold-ish */, 0);
        } else {
            fnFont(0);
            fnText(wlabel, labelX, y + rowH - 2, 0 /* white */, 0);
            wchar_t wval[64];
            int k = 0;
            for (; k < 63 && r->valueBuf[k]; k++) wval[k] = (wchar_t)(unsigned char)r->valueBuf[k];
            wval[k] = 0;
            /* Right-align the value at valueX (mode 2 in fnText). */
            fnText(wval, valueX, y + rowH - 2, 0, 2);
        }
    }

    /* Scroll hint — short, left-aligned so it never overruns the
     * parchment regardless of the row count. Anchored at the same
     * X as the labels so it visually belongs to this column. */
    if (*scrollOffset + maxRowsVisible < rowCount) {
        char buf[40];
        _snprintf(buf, sizeof(buf) - 1, "Scroll for %d more",
                  rowCount - (*scrollOffset + maxRowsVisible));
        wchar_t w[40];
        int k = 0;
        for (; k < 39 && buf[k]; k++) w[k] = (wchar_t)(unsigned char)buf[k];
        w[k] = 0;
        fnFont(0);
        fnText(w, labelX, lastY + 2, 7, 0); /* mode 0 = left-aligned */
    }
    if (*scrollOffset > 0) {
        char buf[40];
        _snprintf(buf, sizeof(buf) - 1, "%d above", *scrollOffset);
        wchar_t w[40];
        int k = 0;
        for (; k < 39 && buf[k]; k++) w[k] = (wchar_t)(unsigned char)buf[k];
        w[k] = 0;
        fnFont(0);
        fnText(w, labelX, firstY - 4, 7, 0);
    }

    return maxRowsVisible;
}

void Stats_RenderF1Page(int mouseX, int mouseY, BOOL clicked) {
    (void)mouseX; (void)mouseY; (void)clicked;

    if (!s_statsLayoutLoaded) Stats_LoadIniLayout();
    /* Lightweight: re-read INI every render so user can tune live. */
    Stats_LoadIniLayout();

    /* Title + page headers — explicit X positions from the INI so the
     * user can drag them around without recompiling. Centered (mode 1)
     * around each X anchor. */
    if (fnText && fnFont) {
        fnFont(0);
        const wchar_t* title = L"Logbook - This Character / Lifetime";
        fnText(title, g_statsTitleX, g_statsTitleY, 4, 1);

        const wchar_t* hdrL = L"THIS CHARACTER";
        fnText(hdrL, g_statsHdrLeftX, g_statsHdrLeftY, 4, 1);
        const wchar_t* hdrR = L"LIFETIME (ALL CHARS)";
        fnText(hdrR, g_statsHdrRightX, g_statsHdrRightY, 4, 1);
    }

    /* Build + render both columns. */
    static StatsRow s_charRows[STATS_MAX_ROWS];
    static StatsRow s_lifeRows[STATS_MAX_ROWS];
    int charCount = Stats_BuildCharRows(s_charRows, STATS_MAX_ROWS);
    int lifeCount = Stats_BuildLifetimeRows(s_lifeRows, STATS_MAX_ROWS);

    Stats_RenderColumn(g_statsLpX, g_statsLpW, s_charRows, charCount, &s_scrollLeft);
    Stats_RenderColumn(g_statsRpX, g_statsRpW, s_lifeRows, lifeCount, &s_scrollRight);
}

#endif /* D2ARCH_STATS_C */
