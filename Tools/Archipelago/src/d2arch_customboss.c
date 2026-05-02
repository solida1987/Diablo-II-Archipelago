/* d2arch_customboss.c — Custom Boss Framework (Phase 4: multi-mode spawn)
 *
 * Spawns designated bosses at specific areas using one of three strategies:
 *   player  — offset from player at area entry (simple, approximate)
 *   abs     — absolute world coords (for fixed areas like towns, Chaos Sanct)
 *   random  — random walkable tile in area, min-distance from player
 *
 * Each boss spawns ONCE per character-load session. Three safeguards prevent
 * duplicate spawns:
 *   1. g_customBosses[i].spawned flag — set TRUE after first spawn attempt
 *   2. D2's own 1-per-game SU rule — fnSpawnSuperUnique returns NULL if the
 *      same base SU slot already exists in the current game session
 *   3. The spawned unit persists in D2's memory until either killed or the
 *      player quits the game. Re-entering the area does NOT re-populate.
 *
 * When the boss is killed, it stays dead until OnCharacterLoad fires
 * (player quits to main menu, loads character again). At that point:
 *   - CustomBoss_Reset() clears all spawned flags
 *   - D2 re-populates the area on next entry
 *   - Boss spawns again (possibly at a different random position)
 *
 * Scrolling out/in of the same area WITHOUT quitting → no respawn.
 *
 * Config file: Game/Archipelago/custom_bosses.dat
 *
 *   NEW 6-field format:
 *     AREA_ID  BASE_SU  MODE  PARAM1  PARAM2  LABEL
 *
 *   LEGACY 5-field format (auto-detected, assumed player mode):
 *     AREA_ID  BASE_SU  OFFSET_X  OFFSET_Y  LABEL
 *
 * Integration (d2arch.c include chain):
 *   AFTER d2arch_skilltree.c  (needs fnSpawnSuperUnique, GetCurrentArea, Log)
 *   BEFORE d2arch_gameloop.c  (calls CustomBoss_Tick from ProcessPendingGameTick)
 */

#include <stdlib.h>   /* rand, srand */
#include <time.h>     /* time — seed rand once per session */

#define CUSTOMBOSS_MAX         256
#define CUSTOMBOSS_LABEL_MAX   48

/* Spawn modes */
#define CBMODE_PLAYER   0    /* param1/2 = offset from player at area-enter */
#define CBMODE_ABS      1    /* param1/2 = absolute world (X, Y)             */
#define CBMODE_RANDOM   2    /* param1 = min tiles from player, param2 unused */

typedef struct CustomBoss_s {
    int         areaId;
    int         baseSuId;
    int         mode;                           /* CBMODE_* */
    int         param1;                         /* interpretation depends on mode */
    int         param2;
    BOOL        spawned;                        /* session-lifetime spawn flag */
    char        label[CUSTOMBOSS_LABEL_MAX];
} CustomBoss;

static CustomBoss g_customBosses[CUSTOMBOSS_MAX];
static int        g_customBossCount = 0;
static int        s_customBoss_lastArea = 0;
static BOOL       s_customBoss_randSeeded = FALSE;

/* Local helper: get server-side player unit from pGame's unit list. */
static void* CustomBoss_GetServerPlayer(DWORD pGame) {
    if (!pGame) return NULL;
    __try {
        DWORD* pPlayerBuckets = (DWORD*)(pGame + 0x1120);
        for (int i = 0; i < 128; i++) {
            DWORD pUnit = pPlayerBuckets[i];
            if (pUnit) return (void*)pUnit;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return NULL;
}

/* Parse mode string. Returns CBMODE_PLAYER as default for unknown strings. */
static int CustomBoss_ParseMode(const char* s) {
    if (!s || !s[0]) return CBMODE_PLAYER;
    if (_stricmp(s, "player") == 0) return CBMODE_PLAYER;
    if (_stricmp(s, "abs")    == 0) return CBMODE_ABS;
    if (_stricmp(s, "random") == 0) return CBMODE_RANDOM;
    return -1; /* unknown — caller decides fallback */
}

static const char* CustomBoss_ModeName(int m) {
    switch (m) {
        case CBMODE_PLAYER: return "player";
        case CBMODE_ABS:    return "abs";
        case CBMODE_RANDOM: return "random";
        default:            return "???";
    }
}

/* Parse one config line. Handles both new 6-field and legacy 5-field format.
 * Returns TRUE if a boss was successfully parsed into 'out'. */
static BOOL CustomBoss_ParseLine(const char* line, CustomBoss* out, int lineNum) {
    /* Skip leading whitespace */
    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    /* Blank lines / comments */
    if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) return FALSE;

    /* Try new format: AREA BASE_SU MODE PARAM1 PARAM2 LABEL */
    int  areaId = 0, baseSu = 0, p1 = 0, p2 = 0;
    char modeStr[16] = {0};
    char label[CUSTOMBOSS_LABEL_MAX] = {0};
    int  n = sscanf(p, "%d %d %15s %d %d %47s",
                    &areaId, &baseSu, modeStr, &p1, &p2, label);

    int mode;
    if (n >= 5 && CustomBoss_ParseMode(modeStr) >= 0) {
        /* New format — mode string parsed OK */
        mode = CustomBoss_ParseMode(modeStr);
    } else {
        /* Try legacy: AREA BASE_SU OFFSET_X OFFSET_Y LABEL (mode=player) */
        areaId = baseSu = p1 = p2 = 0;
        label[0] = 0;
        n = sscanf(p, "%d %d %d %d %47s", &areaId, &baseSu, &p1, &p2, label);
        if (n < 4) {
            Log("CustomBoss: config line %d malformed: '%s'\n", lineNum, p);
            return FALSE;
        }
        mode = CBMODE_PLAYER;
    }

    /* Validation */
    if (areaId <= 0) {
        Log("CustomBoss: line %d rejected — areaId=%d invalid\n", lineNum, areaId);
        return FALSE;
    }
    if (baseSu < 0 || baseSu > 66) {
        Log("CustomBoss: line %d rejected — baseSu=%d out of [0..66]\n", lineNum, baseSu);
        return FALSE;
    }

    out->areaId   = areaId;
    out->baseSuId = baseSu;
    out->mode     = mode;
    out->param1   = p1;
    out->param2   = p2;
    out->spawned  = FALSE;
    if (label[0]) {
        strncpy(out->label, label, CUSTOMBOSS_LABEL_MAX - 1);
        out->label[CUSTOMBOSS_LABEL_MAX - 1] = 0;
    } else {
        _snprintf(out->label, CUSTOMBOSS_LABEL_MAX, "Boss_%d_%d", areaId, baseSu);
        out->label[CUSTOMBOSS_LABEL_MAX - 1] = 0;
    }
    return TRUE;
}

/* Read custom_bosses.dat into g_customBosses[]. Falls back to a single
 * default boss (Blood Moor random mode) if file is missing. */
static void CustomBoss_LoadConfig(void) {
    g_customBossCount = 0;

    char path[MAX_PATH];
    GetArchDir(path, MAX_PATH);
    strcat(path, "custom_bosses.dat");

    FILE* f = fopen(path, "r");
    if (!f) {
        /* No config — install a default Blood Moor random boss. */
        if (CUSTOMBOSS_MAX > 0) {
            CustomBoss* b = &g_customBosses[0];
            b->areaId   = 2;              /* Blood Moor */
            b->baseSuId = 61;             /* Colenzo / Baal Subject 1 */
            b->mode     = CBMODE_RANDOM;
            b->param1   = 40;             /* min 40 tiles from player */
            b->param2   = 0;
            b->spawned  = FALSE;
            strncpy(b->label, "DefaultBloodMoorBoss", CUSTOMBOSS_LABEL_MAX - 1);
            b->label[CUSTOMBOSS_LABEL_MAX - 1] = 0;
            g_customBossCount = 1;
        }
        Log("CustomBoss: '%s' not found — using default (Blood Moor random boss)\n", path);
        return;
    }

    char line[256];
    int  lineNum = 0, accepted = 0, rejected = 0;
    while (fgets(line, sizeof(line), f) && g_customBossCount < CUSTOMBOSS_MAX) {
        lineNum++;
        CustomBoss tmp = {0};
        if (CustomBoss_ParseLine(line, &tmp, lineNum)) {
            g_customBosses[g_customBossCount++] = tmp;
            accepted++;
        } else {
            /* Only count as rejected if it wasn't a blank/comment line.
             * ParseLine returns FALSE for those too, but silently. We don't
             * distinguish here — the log inside ParseLine covers real errors. */
        }
    }
    fclose(f);

    Log("CustomBoss: loaded %d bosses from '%s' (accepted=%d across %d lines)\n",
        g_customBossCount, path, accepted, lineNum);
    for (int i = 0; i < g_customBossCount; i++) {
        CustomBoss* b = &g_customBosses[i];
        Log("  [%d] area=%d baseSU=%d mode=%s p1=%d p2=%d label='%s'\n",
            i, b->areaId, b->baseSuId, CustomBoss_ModeName(b->mode),
            b->param1, b->param2, b->label);
    }
}

/* 1.8.0 NEW — Append gate-bosses from the active preload set.
 *
 * Reads g_actPreload[5][3] (baked at character creation from slot_data)
 * and appends up to 18 gate-bosses to g_customBosses[] for the CURRENT
 * difficulty only. Called from OnCharacterLoad after LoadChecks restored
 * g_actPreload values.
 *
 * Silently skips zero-spawn entries (Act 4 only has 2 gates, and the
 * 4th preload slot for Act 4 is unused). */
static void CustomBoss_AppendGateBosses(void) {
    extern int g_currentDifficulty;
    extern BOOL g_zoneLockingOn;

    if (!g_zoneLockingOn) {
        Log("CustomBoss: zone locking OFF — skipping gate-boss append\n");
        return;
    }

    int diff = g_currentDifficulty;
    if (diff < 0 || diff > 2) diff = 0;

    int appended = 0, skippedKilled = 0;
    for (int act = 1; act <= 5; act++) {
        int preload_id = g_actPreload[act - 1][diff];
        int num_gates = g_actRegions[act - 1].num_gates;

        for (int g = 0; g < num_gates; g++) {
            const GateSpawnDef* gd = Preload_GetGate(act, preload_id, g);
            if (!gd) continue;

            /* 1.8.2 — Skip gates whose boss has already been killed on this
             * difficulty. Prevents respawn after quit+reload. */
            int slot = GateKey_SlotFromActGate(act, g);
            if (slot >= 0 && g_gateBossKilled[diff][slot]) {
                skippedKilled++;
                continue;
            }

            if (g_customBossCount >= CUSTOMBOSS_MAX) {
                Log("CustomBoss: g_customBosses full, stopping append at (act=%d gate=%d)\n",
                    act, g + 1);
                return;
            }
            CustomBoss* b = &g_customBosses[g_customBossCount++];
            b->areaId   = gd->spawn_zone;
            b->baseSuId = gd->base_su;
            b->mode     = CBMODE_RANDOM;
            b->param1   = gd->min_dist;
            b->param2   = 0;
            b->spawned  = FALSE;
            strncpy(b->label, gd->label, CUSTOMBOSS_LABEL_MAX - 1);
            b->label[CUSTOMBOSS_LABEL_MAX - 1] = 0;
            appended++;
        }
    }

    Log("CustomBoss: appended %d gate-bosses for diff=%d (skipped %d already-killed, count=%d)\n",
        appended, diff, skippedKilled, g_customBossCount);
}

/* Attempt to spawn boss based on its mode. Returns TRUE if successful. */
static BOOL CustomBoss_DoSpawn(void* pGame, void* pSrvPlayer, CustomBoss* b) {
    __try {
        DWORD pPath = *(DWORD*)((DWORD)pSrvPlayer + 0x2C);
        if (!pPath) { Log("CustomBoss[%s]: pPath NULL\n", b->label); return FALSE; }
        DWORD pRoom = *(DWORD*)(pPath + 0x1C);
        if (!pRoom) { Log("CustomBoss[%s]: pRoom NULL\n", b->label); return FALSE; }
        int playerX = (int)*(unsigned short*)(pPath + 0x02);
        int playerY = (int)*(unsigned short*)(pPath + 0x06);

        int spawnX = 0, spawnY = 0;
        void* spawned = NULL;

        switch (b->mode) {
            case CBMODE_PLAYER: {
                spawnX = playerX + b->param1;
                spawnY = playerY + b->param2;
                Log("CustomBoss[%s]: PLAYER-mode spawn baseSU=%d at (%d,%d) player=(%d,%d)\n",
                    b->label, b->baseSuId, spawnX, spawnY, playerX, playerY);
                spawned = fnSpawnSuperUnique(pGame, (void*)pRoom, spawnX, spawnY, b->baseSuId);
                break;
            }

            case CBMODE_ABS: {
                /* Use world coords directly. Works best in fixed areas where
                 * coordinates are stable across game seeds. */
                spawnX = b->param1;
                spawnY = b->param2;
                Log("CustomBoss[%s]: ABS-mode spawn baseSU=%d at world=(%d,%d)\n",
                    b->label, b->baseSuId, spawnX, spawnY);
                spawned = fnSpawnSuperUnique(pGame, (void*)pRoom, spawnX, spawnY, b->baseSuId);
                break;
            }

            case CBMODE_RANDOM: {
                /* Pick random direction + distance. Min distance from player
                 * = param1 (default 40). Max radius = 300. Retry up to 30
                 * attempts; fnSpawnSuperUnique returns NULL on invalid/blocked
                 * positions so successful return = boss is actually walkable. */
                int minDist = (b->param1 > 0) ? b->param1 : 40;
                int maxDist = 300;
                if (maxDist <= minDist) maxDist = minDist + 100;

                /* 8 compass directions give enough spread without full trig */
                static const int dx8[8] = { 1, 1, 0,-1,-1,-1, 0, 1};
                static const int dy8[8] = { 0, 1, 1, 1, 0,-1,-1,-1};

                for (int attempt = 0; attempt < 30; attempt++) {
                    int angleIdx = rand() & 7;
                    int dist = minDist + (rand() % (maxDist - minDist));
                    spawnX = playerX + dx8[angleIdx] * dist;
                    spawnY = playerY + dy8[angleIdx] * dist;

                    /* Can't spawn at negative coords */
                    if (spawnX < 0 || spawnY < 0) continue;

                    spawned = fnSpawnSuperUnique(pGame, (void*)pRoom, spawnX, spawnY, b->baseSuId);
                    if (spawned) {
                        Log("CustomBoss[%s]: RANDOM-mode spawn baseSU=%d at (%d,%d) "
                            "dist=%d dir=%d attempt=%d\n",
                            b->label, b->baseSuId, spawnX, spawnY, dist, angleIdx, attempt);
                        break;
                    }
                }
                if (!spawned) {
                    Log("CustomBoss[%s]: RANDOM-mode failed after 30 attempts (area may be blocked)\n",
                        b->label);
                }
                break;
            }

            default:
                Log("CustomBoss[%s]: unknown mode=%d\n", b->label, b->mode);
                return FALSE;
        }

        if (spawned) {
            Log("CustomBoss[%s]: SPAWN OK unit=%p\n", b->label, (DWORD)spawned);
            return TRUE;
        } else {
            Log("CustomBoss[%s]: spawn returned NULL — baseSU=%d may already exist this game\n",
                b->label, b->baseSuId);
            return FALSE;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("CustomBoss[%s]: EXCEPTION during DoSpawn\n", b->label);
        return FALSE;
    }
}

/* Called from ProcessPendingGameTick each tick. Area transition triggers
 * spawn attempt for all bosses registered to the new area (that haven't
 * spawned yet this session). */
static void CustomBoss_Tick(void* pGame) {
    if (!pGame) return;
    if (!fnSpawnSuperUnique) return;
    if (!Player()) return;
    if (g_customBossCount <= 0) return;

    int curArea = GetCurrentArea();
    if (curArea <= 0) return;
    if (curArea == s_customBoss_lastArea) return;
    s_customBoss_lastArea = curArea;

    /* Seed RNG once per session for random mode. */
    if (!s_customBoss_randSeeded) {
        srand((unsigned)time(NULL) ^ GetTickCount());
        s_customBoss_randSeeded = TRUE;
    }

    Log("CustomBoss: area transition detected, now in area=%d\n", curArea);

    void* pSrvPlayer = CustomBoss_GetServerPlayer((DWORD)pGame);
    if (!pSrvPlayer) {
        Log("CustomBoss: no server player yet (area=%d)\n", curArea);
        return;
    }

    for (int i = 0; i < g_customBossCount; i++) {
        CustomBoss* b = &g_customBosses[i];
        if (b->areaId != curArea) continue;

        if (b->spawned) {
            /* Already spawned this session. Re-entering the area is a no-op.
             * D2 keeps the boss alive in its own unit list across area
             * transitions, so the boss persists until killed or game quit. */
            continue;
        }

        /* Attempt spawn. On success OR on "SU already exists" we mark spawned
         * so we don't retry. The "already exists" case shouldn't normally fire
         * since we're the only ones spawning this SU, but it guards against
         * double-fire from transition edge cases. */
        CustomBoss_DoSpawn(pGame, pSrvPlayer, b);
        b->spawned = TRUE;
    }
}

/* Called from OnCharacterLoad. Reloads config from disk (so edits take
 * effect) and clears per-session spawn flags. */
static void CustomBoss_Reset(void) {
    s_customBoss_lastArea = 0;
    CustomBoss_LoadConfig();
    Log("CustomBoss: state reset (%d bosses in registry)\n", g_customBossCount);
}
