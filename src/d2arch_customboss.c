/* d2arch_customboss.c — Custom Boss Framework (Phase 4: multi-mode spawn) Spawns designated bosses at specific areas using one of three strategies: player — offset from player at area entry (simple, approximate) abs — absolute world coords (for fixed areas like towns, Chaos Sanct) random — random walkable tile in area, min-distance from player Each boss spawns ONCE per character-load session. */

#include <stdlib.h>   /* rand, srand */
#include <time.h>     /* time — seed rand once per session */

#define CUSTOMBOSS_MAX         256
#define CUSTOMBOSS_LABEL_MAX   48

/* Spawn modes */
#define CBMODE_PLAYER   0    /* param1/2 = offset from player at area-enter */
#define CBMODE_ABS      1    /* param1/2 = absolute world (X, Y) */
#define CBMODE_RANDOM   2    /* param1 = min tiles from player, param2 unused */

typedef struct CustomBoss_s {
    int         areaId;
    int         baseSuId;
    int         mode;                           /* CBMODE_* */
    int         param1;                         /* interpretation depends on mode */
    int         param2;
    BOOL        spawned;                        /* session-lifetime spawn flag */
    int         failedTries;                    /* failed spawn attempts this session (retry cap 3) */
    char        label[CUSTOMBOSS_LABEL_MAX];
} CustomBoss;

static CustomBoss g_customBosses[CUSTOMBOSS_MAX];
static int        g_customBossCount = 0;
static int        s_customBoss_lastArea = 0;
static BOOL       s_customBoss_randSeeded = FALSE;

/* Defined in d2arch_gameloop.c, which the unity build includes AFTER this
 * file -- so it needs announcing here. It re-reads the engine's live game
 * pointer, which is the only reliable answer to "which game is running". */
static DWORD SyncCachedPGame(void);

/* Local helper: get server-side player unit from pGame's unit list.
 *
 * The pGame callers hand in is usually g_cachedPGame, and during the window
 * between one game ending and the next tick re-syncing, that still points at
 * the FREED previous game. Dereferencing it faults. The twin of this function
 * in d2arch_gameloop.c already re-reads the engine's live pointer first; this
 * one did not, and it is the one the treasure-cow heartbeat and the object
 * traps call every tick. A single finished playthrough logged 356 access
 * violations, 338 of them here -- swallowed by the __except below, so nothing
 * ever surfaced, but every one of them was a fault per frame across the whole
 * load window. Re-sync first: the engine's own global is the only thing that
 * knows which game is live. */
static void* CustomBoss_GetServerPlayer(DWORD pGame) {
    DWORD live = SyncCachedPGame();
    if (!live) return NULL;
    pGame = live;
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

/* Parse one config line. */
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
    out->failedTries = 0;
    if (label[0]) {
        strncpy(out->label, label, CUSTOMBOSS_LABEL_MAX - 1);
        out->label[CUSTOMBOSS_LABEL_MAX - 1] = 0;
    } else {
        _snprintf(out->label, CUSTOMBOSS_LABEL_MAX, "Boss_%d_%d", areaId, baseSu);
        out->label[CUSTOMBOSS_LABEL_MAX - 1] = 0;
    }
    return TRUE;
}

/* Read custom_bosses.dat into g_customBosses[]. */
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
            b->failedTries = 0;
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
            /* Only count as rejected if it wasn't a blank/comment line. */
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

/* Append gate-bosses from the active preload set. */
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

            /* Skip gates whose boss has already been killed on this difficulty. */
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
            b->failedTries = 0;
            strncpy(b->label, gd->label, CUSTOMBOSS_LABEL_MAX - 1);
            b->label[CUSTOMBOSS_LABEL_MAX - 1] = 0;
            appended++;
        }
    }

    Log("CustomBoss: appended %d gate-bosses for diff=%d (skipped %d already-killed, count=%d)\n",
        appended, diff, skippedKilled, g_customBossCount);
}

/* HUNT-TARGET FALLBACK (BetaHub #10 — "Winged Death never spawns").
 *
 * The four custom superuniques (hcIdx 32-35) exist ONLY as zone-locking gate
 * bosses: each appears in some preloads and not in others, and with zone
 * locking off they are appended nowhere at all. But their hunt quests are AP
 * locations in every seed with hunting on — so any game whose preload roll
 * skipped them carried a check that could never be completed. The fix is the
 * boring one: whichever of the four the preloads did not place, place anyway,
 * in its own home zone, through the exact spawn path gate bosses already use.
 * The kill handler maps the gate-row hcIdx back to the vanilla one, so a
 * fallback kill completes the hunt the same way a gate kill does. */
static void CustomBoss_AppendHuntFallbacks(void) {
    /* g_apQuestHunting: file-scope static in d2arch_input.c, in scope here
     * because the unity build includes input.c first. */
    if (!g_apQuestHunting) {
        Log("CustomBoss: hunting off — no hunt fallbacks needed\n");
        return;
    }

    static const struct { int su; int areaId; const char* label; } k[4] = {
        { 32, 105, "HuntFB_WingedPlains" },   /* Winged Death -> Plains of Despair */
        { 33, 107, "HuntFB_TormentRiver" },   /* The Tormentor -> River of Flame */
        { 34,  90, "HuntFB_TaintSwamp" },     /* Taintbreeder -> Swampy Pit L3 */
        { 35,  84, "HuntFB_RiftCave" },       /* Riftwraith -> Spider Cave */
    };

    int added = 0;
    for (int i = 0; i < 4; i++) {
        BOOL present = FALSE;
        for (int b = 0; b < g_customBossCount; b++)
            if (g_customBosses[b].baseSuId == k[i].su) { present = TRUE; break; }
        if (present) continue;

        if (g_customBossCount >= CUSTOMBOSS_MAX) {
            Log("CustomBoss: table full — hunt fallback for su=%d dropped\n", k[i].su);
            break;
        }
        CustomBoss* b = &g_customBosses[g_customBossCount++];
        b->areaId   = k[i].areaId;
        b->baseSuId = k[i].su;
        b->mode     = CBMODE_RANDOM;
        b->param1   = 40;
        b->param2   = 0;
        b->spawned  = FALSE;
        b->failedTries = 0;
        strncpy(b->label, k[i].label, CUSTOMBOSS_LABEL_MAX - 1);
        b->label[CUSTOMBOSS_LABEL_MAX - 1] = 0;
        added++;
    }
    Log("CustomBoss: %d hunt fallback(s) appended (count=%d)\n",
        added, g_customBossCount);
}

/* GATE-BOSS IDENTITY REGISTRY PROBLEM. */
/* DEDICATED SuperUniques ROWS FOR GATE BOSSES The research round overturned the "66 is a hard ceiling" conclusion: the engine accepts up to 512 SuperUniques rows (MonsterTbls.cpp:1526), spawning goes by ROW index, and the fixed nSuperUniqueIds[66] is only a reverse map feeding HARDCODED quest/AI logic — rows with an hcIdx COLUMN above 65 are deliberately excluded from that map (a harmless "Invalid hcIdx" -txt trace), which is exactly what gate bosses want. */
/* Defined in d2arch_collections.c — included AFTER this file in the unity build, so forward-declare (static: same translation unit). */
static DWORD Coll_GetSgptDT(void);

#define GATE_HC_BASE   100
#define GATE_PAIRS_N   44
static const short s_gatePairs[GATE_PAIRS_N][2] = {
    /* {vanilla hcIdx, gate hcIdx} — order mirrors SuperUniques.txt append */
    {0,100},{1,101},{2,102},{3,103},{4,104},{5,105},{6,106},{7,107},
    {9,108},{10,109},{11,110},{12,111},{13,112},{14,113},{15,114},{16,115},
    {17,116},{19,117},{20,118},{21,119},{22,120},{23,121},{24,122},{25,123},
    {32,124},{33,125},{34,126},{35,127},{40,128},{42,129},{46,130},{47,131},
    {48,132},{49,133},{50,134},{51,135},{52,136},{53,137},{54,138},{55,139},
    {56,140},{57,141},{58,142},{59,143}
};

int CustomBoss_GateHcFromVanilla(int vanillaHc) {
    for (int i = 0; i < GATE_PAIRS_N; i++)
        if (s_gatePairs[i][0] == (short)vanillaHc) return s_gatePairs[i][1];
    return -1;
}

int CustomBoss_VanillaHcFromGate(int gateHc) {
    for (int i = 0; i < GATE_PAIRS_N; i++)
        if (s_gatePairs[i][1] == (short)gateHc) return s_gatePairs[i][0];
    return -1;
}

/* sgptDataTables offsets for the runtime SuperUniques table (D2MOO D2DataTbls.h / MonsterTbls.h). */
#define CB_DT_PSUPERUNIQUES   0xAD4
#define CB_DT_NSUPERUNIQUES   0xADC
#define CB_SU_STRIDE          0x34
#define CB_SU_OFF_CLASS       0x04
#define CB_SU_OFF_HCIDX       0x08

/* Runtime ROW whose hcIdx COLUMN equals hcCol, or -1. */
int CustomBoss_SuRowFromHcIdx(int hcCol) {
    extern DWORD Coll_GetSgptDT(void);
    DWORD dt = Coll_GetSgptDT();
    DWORD pSu = 0; int n = 0;
    if (!dt) return -1;
    __try {
        pSu = *(DWORD*)(dt + CB_DT_PSUPERUNIQUES);
        n   = *(int*)  (dt + CB_DT_NSUPERUNIQUES);
    } __except(EXCEPTION_EXECUTE_HANDLER) { pSu = 0; }
    if (!pSu || n <= 0 || n > 512) return -1;
    for (int r = 0; r < n; r++) {
        __try {
            char* row = (char*)(pSu + (DWORD)r * CB_SU_STRIDE);
            if (*(int*)(row + CB_SU_OFF_HCIDX) == hcCol &&
                *(int*)(row + CB_SU_OFF_CLASS) >= 0)
                return r;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return -1;
}

/* hcIdx COLUMN of a runtime row (what quests/gates are keyed on), or `row` itself when the table is unreachable — the pre-C7v2 behavior. */
int CustomBoss_SuHcIdxFromRow(int row) {
    extern DWORD Coll_GetSgptDT(void);
    DWORD dt = Coll_GetSgptDT();
    DWORD pSu = 0; int n = 0;
    if (!dt) return row;
    __try {
        pSu = *(DWORD*)(dt + CB_DT_PSUPERUNIQUES);
        n   = *(int*)  (dt + CB_DT_NSUPERUNIQUES);
        if (pSu && row >= 0 && row < n)
            return *(int*)((char*)(pSu + (DWORD)row * CB_SU_STRIDE) + CB_SU_OFF_HCIDX);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return row;
}

/* hcIdx of the VANILLA runtime row whose Class is this monster id, or -1 when there is no unique answer. */
int CustomBoss_SuHcIdxFromClass(int monClass) {
    extern DWORD Coll_GetSgptDT(void);
    DWORD dt = Coll_GetSgptDT();
    DWORD pSu = 0; int n = 0, found = -1;
    if (!dt || monClass < 0) return -1;
    __try {
        pSu = *(DWORD*)(dt + CB_DT_PSUPERUNIQUES);
        n   = *(int*)  (dt + CB_DT_NSUPERUNIQUES);
    } __except(EXCEPTION_EXECUTE_HANDLER) { pSu = 0; }
    if (!pSu || n <= 0 || n > 512) return -1;
    for (int r = 0; r < n; r++) {
        __try {
            char* row = (char*)(pSu + (DWORD)r * CB_SU_STRIDE);
            int cls = *(int*)(row + CB_SU_OFF_CLASS);
            int hc  = *(int*)(row + CB_SU_OFF_HCIDX);
            if (cls == monClass && hc >= 0 && hc < 66) {
                if (found >= 0 && found != hc) { found = -1; return -1; } /* ambiguous */
                found = hc;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return found;
}

#define GATEBOSS_ID_MAX   64
#define GATEBOSS_SU_MAX   160    /* covers vanilla 0-65 AND gate ids 100-143 */

static DWORD s_gateBossUnitIds[GATEBOSS_ID_MAX];
static int   s_gateBossUnitCount = 0;
static BYTE  s_gateBossSuSpawned[GATEBOSS_SU_MAX];

/* Cleared on every game start — unit ids are recycled across games, so a stale id would mis-credit a completely unrelated monster. */
void CustomBoss_ResetGateBossIdentity(void) {
    s_gateBossUnitCount = 0;
    memset(s_gateBossUnitIds,  0, sizeof(s_gateBossUnitIds));
    memset(s_gateBossSuSpawned, 0, sizeof(s_gateBossSuSpawned));
}

static void CustomBoss_RegisterGateBoss(int baseSuId, void* spawnedUnit) {
    DWORD unitId = 0;
    __try { unitId = *(DWORD*)((BYTE*)spawnedUnit + 0x0C); }
    __except(EXCEPTION_EXECUTE_HANDLER) { unitId = 0; }
    if (!unitId) return;

    if (baseSuId >= 0 && baseSuId < GATEBOSS_SU_MAX)
        s_gateBossSuSpawned[baseSuId] = 1;

    for (int i = 0; i < s_gateBossUnitCount; i++)
        if (s_gateBossUnitIds[i] == unitId) return;      /* already known */

    if (s_gateBossUnitCount < GATEBOSS_ID_MAX)
        s_gateBossUnitIds[s_gateBossUnitCount++] = unitId;
    else
        Log("CustomBoss: gate-boss id registry full — identity check degrades to hcIdx for later spawns\n");
}

/* TRUE if this killed unit is a gate boss WE spawned. */
BOOL CustomBoss_IsOurGateBoss(DWORD unitId) {
    if (!unitId) return FALSE;
    for (int i = 0; i < s_gateBossUnitCount; i++)
        if (s_gateBossUnitIds[i] == unitId) return TRUE;
    return FALSE;
}

/* TRUE if we successfully spawned a gate boss with this hcIdx this game. */
BOOL CustomBoss_GateSuWasSpawned(int hcIdx) {
    if (hcIdx < 0 || hcIdx >= GATEBOSS_SU_MAX) return FALSE;
    return s_gateBossSuSpawned[hcIdx] != 0;
}

/* Attempt to spawn boss based on its mode. */
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

        /* spawn the DEDICATED gate row when it exists. */
        int spawnRow = -1;
        {
            int gateHc = CustomBoss_GateHcFromVanilla(b->baseSuId);
            if (gateHc >= 0) spawnRow = CustomBoss_SuRowFromHcIdx(gateHc);
            if (spawnRow < 0) {
                spawnRow = CustomBoss_SuRowFromHcIdx(b->baseSuId);
                if (gateHc >= 0)
                    Log("CustomBoss[%s]: gate row hc=%d NOT in runtime table — "
                        "falling back to vanilla row (old game data?)\n",
                        b->label, gateHc);
            }
            if (spawnRow < 0) spawnRow = b->baseSuId;   /* last resort = old behavior */
        }

        switch (b->mode) {
            case CBMODE_PLAYER: {
                spawnX = playerX + b->param1;
                spawnY = playerY + b->param2;
                Log("CustomBoss[%s]: PLAYER-mode spawn baseSU=%d at (%d,%d) player=(%d,%d)\n",
                    b->label, b->baseSuId, spawnX, spawnY, playerX, playerY);
                spawned = fnSpawnSuperUnique(pGame, (void*)pRoom, spawnX, spawnY, spawnRow);
                break;
            }

            case CBMODE_ABS: {
                /* Use world coords directly. */
                spawnX = b->param1;
                spawnY = b->param2;
                Log("CustomBoss[%s]: ABS-mode spawn baseSU=%d at world=(%d,%d)\n",
                    b->label, b->baseSuId, spawnX, spawnY);
                spawned = fnSpawnSuperUnique(pGame, (void*)pRoom, spawnX, spawnY, spawnRow);
                break;
            }

            case CBMODE_RANDOM: {
                /* Pick random direction + distance. */
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

                    spawned = fnSpawnSuperUnique(pGame, (void*)pRoom, spawnX, spawnY, spawnRow);
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
            /* remember WHICH unit this is so the kill can be told apart from the identical vanilla superunique. */
            CustomBoss_RegisterGateBoss(b->baseSuId, spawned);
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

/* Called from ProcessPendingGameTick each tick. */
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
            /* Already spawned this session. */
            continue;
        }

        /* Attempt spawn. Mark spawned ONLY on success — a failed spawn (pPath/pRoom NULL right after a warp, or all placement attempts blocked) used to latch spawned=TRUE anyway, and with zone-locking the gate boss carries the zone KEY: the boss was then gone for the whole session (only save&quit re-rolled it). On failure we retry on the next area transition into this area; a bounded per-boss retry counter stops pathological loops (the "SU already exists" duplicate case returns FALSE too — after 3 failed tries we latch to avoid re-spawn spam against an already-live SU). */
        if (CustomBoss_DoSpawn(pGame, pSrvPlayer, b)) {
            b->spawned = TRUE;
        } else if (++b->failedTries >= 3) {
            b->spawned = TRUE;
            Log("CustomBoss[%s]: giving up after %d failed spawn attempts\n",
                b->label, b->failedTries);
        }
    }
}

/* Called from OnCharacterLoad. */
static void CustomBoss_Reset(void) {
    s_customBoss_lastArea = 0;
    CustomBoss_ResetGateBossIdentity();
    CustomBoss_LoadConfig();
    Log("CustomBoss: state reset (%d bosses in registry)\n", g_customBossCount);
}
