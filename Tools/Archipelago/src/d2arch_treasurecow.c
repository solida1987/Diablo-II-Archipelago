/* d2arch_treasurecow.c — Archipelago Treasure Cow (Phases 5a-d)
 *
 * The complete Treasure Cow system:
 *   a) Spawn mechanism (fnSpawnMonster + Hell Bovine)
 *   b) Per-act counter + per-area tracking + chance roll
 *   c) Post-spawn HP boost so cow is boss-tier tanky
 *   d) Death detection → Diablo/boss-tier loot drop
 *   e) Purple name "Archipelago Treasure Cow" via TBL override
 *      (applied separately via d2tbl.exe — no runtime modification needed)
 *
 * DESIGN CHOICES
 *
 * - Uses vanilla Hell Bovine (MonStats hcIdx 391). Actual cow graphics,
 *   no minions, no DS1 / SuperUniques.txt / MonPreset.txt modifications.
 *   (The old data-file approach caused TBL hash collisions and map bleeding
 *   we just spent hours fixing. Runtime-only = zero bleed.)
 *
 * - fnSpawnMonster doesn't enforce D2's "1-per-game SuperUnique" rule, so
 *   we can spawn 25+ cows across a session.
 *
 * - Each cow's unitId is tracked. On death we trigger the existing
 *   g_pendingLootDrop mechanism → same Diablo/Baal/Mephisto boss TC drop
 *   that quest rewards use. This inherits the cooldown + town-guard +
 *   ilvl-floor logic already in gameloop.c REWARD_LOOT.
 *
 * - HP boost is configured per-act via treasure_cows.dat so you can tune
 *   difficulty per act.
 *
 * INCLUDE ORDER
 *
 * This module is included AFTER d2arch_gameloop.c in d2arch.c, because we
 * reference g_pendingLootDrop (defined in gameloop). gameloop.c forward-
 * declares TreasureCow_Tick + TreasureCow_Reset so its ProcessPendingGameTick
 * can call them.
 */

#include <stdlib.h>
#include <time.h>

#define TC_MAX_AREAS        256
#define TC_HELL_BOVINE_ID   391    /* vanilla MonStats hcIdx for Hell Bovine */
#define TC_MAX_TRACKED      64     /* enough for 5×5 acts + headroom */

typedef struct {
    int   maxSpawns;
    int   chancePct;
    int   hpBonus;                   /* flat HP boost (in 256ths — 256 = 1 HP) */
    int   resistPct;                 /* fire/cold/lightning/poison resist % */
    int   spawnedCount;
    BOOL  areaHasCow[TC_MAX_AREAS];
} TreasureCowAct;

typedef struct {
    int    unitId;                   /* D2 unit ID of the spawned cow */
    int    areaId;                   /* which area it's in */
    void*  pUnit;                    /* last-known unit pointer (may go stale) */
    BOOL   alive;                    /* last-known alive state */
    BOOL   lootDropped;              /* did we already trigger loot for its death? */
} TrackedCow;

static TreasureCowAct g_tcActs[5];
static TrackedCow     g_tcTracked[TC_MAX_TRACKED];
static int            g_tcTrackedCount = 0;
static int            s_tc_lastArea = 0;
static DWORD          s_tc_lastRollTick = 0;          /* 1.8.5 — for re-roll */
static BOOL           s_tc_randSeeded = FALSE;
#define TC_REROLL_INTERVAL_MS 60000                    /* re-roll every 60s if camped */

/* 1.8.5 — dedicated treasure-cow log file. Captures EVERY tick decision so
 * the user can audit "did the spawn code even run?" without grepping the
 * main d2arch.log for [TC] lines. Throttled tick-entry logs avoid spam.
 *
 * Format per line:
 *   [HH:MM:SS.mmm] reason | act=N area=M ...
 */
static void TCLog(const char* fmt, ...) {
    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(path, "%streasure_cow.log", dir);

    FILE* f = fopen(path, "a");
    if (!f) return;

    /* Timestamp prefix — wall-clock ms, mirrors what the user sees in the
     * status overlay, easier to correlate with screenshots. */
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);

    /* Add newline if format string didn't end with one */
    size_t flen = strlen(fmt);
    if (flen == 0 || fmt[flen - 1] != '\n')
        fputc('\n', f);

    fclose(f);

    /* Also pipe through the main Log so it shows up in d2arch.log too. */
    char buf[512];
    va_list ap2;
    va_start(ap2, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap2);
    va_end(ap2);
    buf[sizeof(buf) - 1] = 0;
    Log("[TC] %s", buf);
}

static int TreasureCow_AreaToAct(int areaId) {
    if (areaId >=   1 && areaId <=  39) return 1;
    if (areaId >=  40 && areaId <=  74) return 2;
    if (areaId >=  75 && areaId <= 102) return 3;
    if (areaId >= 103 && areaId <= 108) return 4;
    if (areaId >= 109 && areaId <= 132) return 5;
    return 0;
}

static BOOL TreasureCow_IsTown(int areaId) {
    return areaId ==   1 || areaId ==  40 || areaId ==  75 ||
           areaId == 103 || areaId == 109;
}

/* Config loader. Format: ACT MAX_SPAWNS CHANCE_PCT HP_BONUS RESIST_PCT */
static void TreasureCow_LoadConfig(void) {
    /* Defaults */
    int defaultHp[5]     = {  5000,  15000,  30000,   60000,  100000 };
    int defaultResist[5] = {    30,     40,     50,      60,      70 };
    for (int i = 0; i < 5; i++) {
        g_tcActs[i].maxSpawns    = 5;
        g_tcActs[i].chancePct    = 25;
        g_tcActs[i].hpBonus      = defaultHp[i];
        g_tcActs[i].resistPct    = defaultResist[i];
        g_tcActs[i].spawnedCount = 0;
        memset(g_tcActs[i].areaHasCow, 0, sizeof(g_tcActs[i].areaHasCow));
    }

    char path[MAX_PATH];
    GetArchDir(path, MAX_PATH);
    strcat(path, "treasure_cows.dat");

    FILE* f = fopen(path, "r");
    if (!f) {
        Log("TreasureCow: '%s' not found — using defaults\n", path);
        return;
    }

    char line[256];
    int  lineNum = 0, accepted = 0;
    while (fgets(line, sizeof(line), f)) {
        lineNum++;
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;

        int act = 0, maxSp = 0, chance = 0, hpBonus = 0, resist = 0;
        int n = sscanf(p, "%d %d %d %d %d", &act, &maxSp, &chance, &hpBonus, &resist);
        if (n < 3) {
            Log("TreasureCow: line %d malformed: '%s'\n", lineNum, p);
            continue;
        }
        if (act < 1 || act > 5) continue;
        if (maxSp < 0 || maxSp > 100) continue;
        if (chance < 0 || chance > 100) continue;
        g_tcActs[act - 1].maxSpawns = maxSp;
        g_tcActs[act - 1].chancePct = chance;
        if (n >= 4 && hpBonus >= 0)   g_tcActs[act - 1].hpBonus   = hpBonus;
        if (n >= 5 && resist  >= 0)   g_tcActs[act - 1].resistPct = resist;
        accepted++;
    }
    fclose(f);

    Log("TreasureCow: loaded %d act configs from '%s'\n", accepted, path);
    for (int i = 0; i < 5; i++) {
        Log("  Act %d: max=%d chance=%d%% hp=+%d resist=+%d%%\n",
            i + 1, g_tcActs[i].maxSpawns, g_tcActs[i].chancePct,
            g_tcActs[i].hpBonus, g_tcActs[i].resistPct);
    }
}

static void TreasureCow_Reset(void) {
    s_tc_lastArea = 0;
    s_tc_lastRollTick = 0;
    g_tcTrackedCount = 0;
    memset(g_tcTracked, 0, sizeof(g_tcTracked));
    TreasureCow_LoadConfig();
    TCLog("RESET state cleared, config reloaded — see d2arch.log for "
          "per-act chance/max numbers");
    Log("TreasureCow: state reset (tracked cows cleared, config reloaded)\n");
}

/* Post-spawn stat boost. fnAddStat uses 256ths for HP. Resistances are
 * percentages (flat). */
static void TreasureCow_BoostStats(void* pUnit, int actIdx) {
    if (!pUnit || !fnAddStat) return;
    TreasureCowAct* ta = &g_tcActs[actIdx];

    __try {
        /* STAT_HITPOINTS (current HP) = 6, STAT_MAXHP = 7 */
        int hp256 = ta->hpBonus * 256;
        fnAddStat(pUnit, 7, hp256, 0);   /* max HP */
        fnAddStat(pUnit, 6, hp256, 0);   /* fill current HP to match */

        /* Resistances: STAT_FIRERESIST=39, COLDRESIST=43, LIGHTRESIST=41, POISONRESIST=45 */
        fnAddStat(pUnit, 39, ta->resistPct, 0);
        fnAddStat(pUnit, 43, ta->resistPct, 0);
        fnAddStat(pUnit, 41, ta->resistPct, 0);
        fnAddStat(pUnit, 45, ta->resistPct, 0);

        Log("TreasureCow: boosted stats — +%d HP (act %d) resist +%d%%\n",
            ta->hpBonus, actIdx + 1, ta->resistPct);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("TreasureCow: EXCEPTION during stat boost\n");
    }
}

/* Spawn helper — reads player coords, picks random dir+dist, retries up
 * to 30 times. Returns pUnit on success (and passes back unitId). */
static void* TreasureCow_Spawn(void* pGame, void* pSrvPlayer, int* outUnitId) {
    *outUnitId = 0;
    __try {
        DWORD pPath = *(DWORD*)((DWORD)pSrvPlayer + 0x2C);
        if (!pPath) return NULL;
        DWORD pRoom = *(DWORD*)(pPath + 0x1C);
        if (!pRoom) return NULL;
        int playerX = (int)*(unsigned short*)(pPath + 0x02);
        int playerY = (int)*(unsigned short*)(pPath + 0x06);

        int minDist = 40, maxDist = 300;
        static const int dx8[8] = { 1, 1, 0,-1,-1,-1, 0, 1};
        static const int dy8[8] = { 0, 1, 1, 1, 0,-1,-1,-1};

        for (int attempt = 0; attempt < 30; attempt++) {
            int dir  = rand() & 7;
            int dist = minDist + (rand() % (maxDist - minDist));
            int x    = playerX + dx8[dir] * dist;
            int y    = playerY + dy8[dir] * dist;
            if (x < 0 || y < 0) continue;

            if (!fnSpawnMonster) return NULL;
            void* cow = fnSpawnMonster(pGame, (void*)pRoom, x, y,
                                         TC_HELL_BOVINE_ID, 1, -1, 0);
            if (cow) {
                /* Read unitId from pUnit+0x0C (standard D2 UnitAny offset) */
                __try {
                    *outUnitId = (int)*(DWORD*)((DWORD)cow + 0x0C);
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
                Log("TreasureCow: SPAWN OK at (%d,%d) unit=%p unitId=%d attempt=%d\n",
                    x, y, (DWORD)cow, *outUnitId, attempt);
                return cow;
            }
        }
        Log("TreasureCow: failed to spawn after 30 attempts\n");
        return NULL;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("TreasureCow: EXCEPTION during spawn\n");
        return NULL;
    }
}

/* Check tracked cows each tick: if any died, trigger loot drop. Uses
 * the same pattern as ScanMonsters — read pUnit's mode + flags with SEH. */
static void TreasureCow_CheckDeaths(void) {
    for (int i = 0; i < g_tcTrackedCount; i++) {
        TrackedCow* tc = &g_tcTracked[i];
        if (!tc->alive || tc->lootDropped) continue;
        if (!tc->pUnit) { tc->alive = FALSE; continue; }

        BOOL died = FALSE;
        __try {
            int mode     = *(int*)((DWORD)tc->pUnit + 0x10);
            DWORD flags  = *(DWORD*)((DWORD)tc->pUnit + 0xC4);
            /* MODE_DEATH(0) is unreliable due to SEH but MODE_DEAD(12) +
             * UNITFLAG_ISDEAD(0x10000) are solid (same logic as ScanMonsters). */
            if (mode == 12 || (flags & 0x10000)) died = TRUE;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            /* Unit freed by D2 — treat as dead */
            died = TRUE;
        }

        if (died) {
            tc->alive = FALSE;
            tc->lootDropped = TRUE;
            g_pendingLootDrop++;
            Log("TreasureCow: cow unitId=%d died — triggered loot drop "
                "(g_pendingLootDrop=%d)\n", tc->unitId, g_pendingLootDrop);
        }
    }
}

/* Per-tick entry point. Handles:
 *   - Area transition detection + new-cow spawn probability roll
 *   - Continuous death-check on tracked cows
 *
 * 1.8.5 — every decision is logged to treasure_cow.log so the user can
 * verify the function is being called and trace exactly why spawns did
 * or did not happen. Heartbeat lines are throttled to 10s; per-decision
 * lines are unthrottled so every roll/skip/spawn is visible. */
static void TreasureCow_Tick(void* pGame) {
    static DWORD s_tc_heartbeatTick = 0;
    static unsigned long s_tc_callCount = 0;
    s_tc_callCount++;

    /* Periodic heartbeat — proves the function is actually running.
     * 10-second throttle so the log doesn't drown in tick spam. */
    DWORD nowHB = GetTickCount();
    if (nowHB - s_tc_heartbeatTick > 10000) {
        s_tc_heartbeatTick = nowHB;
        TCLog("HEARTBEAT calls=%lu pGame=%p Player=%p fnSpawnMonster=%p tracked=%d",
              s_tc_callCount, pGame, (void*)(DWORD)(Player() ? 1 : 0),
              (void*)fnSpawnMonster, g_tcTrackedCount);
    }

    if (!pGame) {
        /* No log here — would spam every tick the menu is open. The heartbeat
         * above already shows pGame=NULL when relevant. */
        return;
    }
    if (!Player()) {
        return;
    }

    /* Death-check runs EVERY tick (not just on area transition) so we catch
     * kills promptly and drop loot where the cow died. */
    TreasureCow_CheckDeaths();

    if (!fnSpawnMonster) {
        /* This is a one-time symptom — log once per heartbeat window. */
        if (nowHB - s_tc_heartbeatTick < 100)
            TCLog("SKIP fnSpawnMonster=NULL — cow spawn logic disabled");
        return;
    }

    int curArea = GetCurrentArea();
    if (curArea <= 0 || curArea >= TC_MAX_AREAS) return;

    /* 1.8.5 FIX: rolled-once-per-area was bricking 75% of areas (1×25%=25%
     * coverage). Now we allow a re-roll every 60s while player stays in the
     * same area, until either a cow spawns or player leaves. Cumulative
     * chance over a typical 5-minute area visit ≈ 92% (with chance bumped
     * to 40 in treasure_cows.dat). */
    DWORD now = GetTickCount();
    BOOL  areaChanged = (curArea != s_tc_lastArea);
    BOOL  timerReady  = (s_tc_lastRollTick == 0) ||
                        (now - s_tc_lastRollTick >= TC_REROLL_INTERVAL_MS);
    if (!areaChanged && !timerReady) {
        /* No-op: same area + cool-down still active. Suppressed from log
         * because it fires every game-tick and would drown out useful info. */
        return;
    }
    s_tc_lastArea = curArea;

    if (!s_tc_randSeeded) {
        srand((unsigned)time(NULL) ^ GetTickCount() ^ 0xC0FFEE);
        s_tc_randSeeded = TRUE;
        TCLog("RNG seeded");
    }

    if (TreasureCow_IsTown(curArea)) {
        TCLog("SKIP area=%d is town — no cow spawn", curArea);
        s_tc_lastRollTick = now;       /* don't re-trigger every tick in town */
        return;
    }

    int act = TreasureCow_AreaToAct(curArea);
    if (act < 1 || act > 5) {
        TCLog("SKIP area=%d -> act=%d (out of range)", curArea, act);
        s_tc_lastRollTick = now;
        return;
    }

    TreasureCowAct* ta = &g_tcActs[act - 1];
    if (ta->spawnedCount >= ta->maxSpawns) {
        TCLog("SKIP act=%d full (%d/%d cows spawned this session)",
              act, ta->spawnedCount, ta->maxSpawns);
        s_tc_lastRollTick = now;
        return;
    }
    if (ta->areaHasCow[curArea]) {
        TCLog("SKIP act=%d area=%d already has a cow this session",
              act, curArea);
        s_tc_lastRollTick = now;
        return;
    }

    int roll = rand() % 100;
    BOOL hit = (roll < ta->chancePct);
    TCLog("ROLL act=%d area=%d count=%d/%d roll=%d vs chance=%d%% -> %s (%s)",
          act, curArea, ta->spawnedCount, ta->maxSpawns,
          roll, ta->chancePct,
          hit ? "HIT — spawning cow" : "miss",
          areaChanged ? "area entry" : "re-roll");
    /* Keep the original Log() too so existing log-readers keep working. */
    Log("TreasureCow: act=%d area=%d count=%d/%d roll=%d vs chance=%d%% (%s)\n",
        act, curArea, ta->spawnedCount, ta->maxSpawns, roll, ta->chancePct,
        areaChanged ? "area entry" : "re-roll");

    s_tc_lastRollTick = now;
    if (!hit) return;

    void* pSrvPlayer = CustomBoss_GetServerPlayer((DWORD)pGame);
    if (!pSrvPlayer) {
        TCLog("FAIL CustomBoss_GetServerPlayer returned NULL — skipping spawn");
        return;
    }

    int unitId = 0;
    void* cow = TreasureCow_Spawn(pGame, pSrvPlayer, &unitId);
    if (!cow) {
        TCLog("FAIL TreasureCow_Spawn returned NULL act=%d area=%d "
              "(see d2arch.log for spawn-attempt details)", act, curArea);
        return;
    }

    TCLog("SPAWN OK act=%d area=%d unitId=%d unit=%p", act, curArea, unitId, cow);

    /* Boost cow stats. */
    TreasureCow_BoostStats(cow, act - 1);

    /* Mark area + increment counter. */
    ta->areaHasCow[curArea] = TRUE;
    ta->spawnedCount++;

    /* 1.8.2 — Tip the player off that something unusual is in this area.
     * Intentionally generic so we can reuse it later for other special
     * spawns (custom-look bosses, alt-MonStats variants, etc.) without
     * having to change the message. The user has to find the creature
     * themselves; the warning only confirms it's worth searching. */
    ShowBigWarning("SOMETHING UNUSUAL HAS BEEN SPOTTED IN THIS AREA...");

    /* Add to tracking list for death-detection. */
    if (g_tcTrackedCount < TC_MAX_TRACKED) {
        TrackedCow* tc  = &g_tcTracked[g_tcTrackedCount++];
        tc->unitId      = unitId;
        tc->areaId      = curArea;
        tc->pUnit       = cow;
        tc->alive       = TRUE;
        tc->lootDropped = FALSE;
    } else {
        TCLog("WARN tracking table full (%d/%d) — death-detection may miss "
              "this cow's loot", g_tcTrackedCount, TC_MAX_TRACKED);
    }

    TCLog("DONE act=%d %d/%d (area=%d marked, tracked=%d)",
          act, ta->spawnedCount, ta->maxSpawns, curArea, g_tcTrackedCount);
    Log("TreasureCow: act %d %d/%d (area %d marked, tracked=%d)\n",
        act, ta->spawnedCount, ta->maxSpawns, curArea, g_tcTrackedCount);
}
