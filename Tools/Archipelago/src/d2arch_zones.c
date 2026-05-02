/* ================================================================
 * ZONE GATING SYSTEM (Game Mode B: Zone Explorer)
 * ================================================================ */
#define ZONE_KEY_COUNT 38
#define AP_ZONE_KEY_BASE 46001

typedef struct {
    int         apItemId;       /* AP item ID (46001-46038) */
    const char* name;           /* Display name */
    int         act;            /* Which act (1-5) */
    int         areas[10];      /* Area IDs this key unlocks (0-terminated) */
} ZoneKeyDef;

static const ZoneKeyDef g_zoneKeyDefs[ZONE_KEY_COUNT] = {
    /* Act 1 (10 keys) */
    { 46001, "Cold Plains",        1, {3, 9, 13, 0} },
    { 46002, "Burial Grounds",     1, {17, 18, 19, 0} },
    { 46003, "Stony Field",        1, {4, 10, 14, 0} },
    { 46004, "Dark Wood",          1, {5, 0} },
    { 46005, "Black Marsh",        1, {6, 11, 15, 20, 21, 22, 23, 24, 25, 0} },
    { 46006, "Tristram",           1, {38, 0} },
    { 46007, "Monastery",          1, {7, 12, 16, 26, 27, 28, 0} },
    { 46008, "Jail & Cathedral",   1, {29, 30, 31, 32, 33, 0} },
    { 46009, "Catacombs",          1, {34, 35, 36, 0} },
    { 46010, "Andariel's Lair",    1, {37, 0} },
    /* Act 2 (8 keys) */
    { 46011, "Rocky Waste",        2, {41, 47, 48, 49, 55, 59, 0} },
    { 46012, "Dry Hills",          2, {42, 56, 57, 60, 0} },
    { 46013, "Far Oasis",          2, {43, 62, 63, 64, 0} },
    { 46014, "Lost City",          2, {44, 45, 58, 61, 65, 0} },
    { 46015, "Palace",             2, {50, 51, 52, 53, 54, 0} },
    { 46016, "Arcane Sanctuary",   2, {74, 0} },
    { 46017, "Canyon of the Magi", 2, {46, 66, 67, 68, 69, 70, 71, 72, 0} },
    { 46018, "Duriel's Lair",      2, {73, 0} },
    /* Act 3 (6 keys) */
    { 46019, "Spider Forest",      3, {76, 84, 85, 0} },
    { 46020, "Jungle",             3, {77, 78, 86, 87, 88, 89, 90, 91, 0} },
    { 46021, "Kurast",             3, {79, 80, 92, 93, 94, 95, 0} },
    { 46022, "Upper Kurast",       3, {81, 82, 96, 97, 98, 99, 0} },
    { 46023, "Travincal",          3, {83, 0} },
    { 46024, "Durance of Hate",    3, {100, 101, 102, 0} },
    /* Act 4 (4 keys) */
    { 46025, "Outer Steppes",      4, {104, 105, 0} },
    { 46026, "City of the Damned", 4, {106, 0} },
    { 46027, "River of Flame",     4, {107, 0} },
    { 46028, "Chaos Sanctuary",    4, {108, 0} },
    /* Act 5 (7 keys) */
    { 46029, "Bloody Foothills",   5, {110, 0} },
    { 46030, "Highlands",          5, {111, 112, 0} },
    { 46031, "Caverns",            5, {113, 114, 115, 116, 117, 0} },
    { 46032, "Summit",             5, {118, 119, 120, 0} },
    { 46033, "Nihlathak",          5, {121, 122, 123, 124, 0} },
    { 46034, "Worldstone Keep",    5, {128, 129, 130, 0} },
    { 46035, "Throne of Destruction", 5, {131, 132, 0} },
    /* Reserved slots (36-38) */
    { 46036, "Reserved 1",         0, {0} },
    { 46037, "Reserved 2",         0, {0} },
    { 46038, "Reserved 3",         0, {0} },
};

static BOOL g_zoneLocked[MAX_AREA_ID];          /* TRUE = area is locked */
static BOOL g_zoneKeyReceived[ZONE_KEY_COUNT];  /* TRUE = key has been received */
static volatile int g_pendingZoneTeleport = 0;  /* Queued teleport to town area */

static BOOL g_zoneTrackerOpen = FALSE;           /* F5 Zone Tracker panel */
static int g_zoneTrackerScroll = 0;              /* Scroll offset for zone tracker */
static int g_zoneTrackerDiff = 0;               /* 0=Normal, 1=Nightmare, 2=Hell */

/* Per-difficulty zone lock state (zone keys unlock per-difficulty independently) */
static BOOL g_zoneLockedByDiff[3][MAX_AREA_ID];
static BOOL g_zoneKeyReceivedByDiff[3][ZONE_KEY_COUNT];
static DWORD g_lastZoneBlockTime = 0;           /* Cooldown for block notifications */
static int g_lastSafeArea = 1;                  /* Last unlocked area the player was in */

/* Get the town area ID for a given area */
static int GetActTown(int areaId) {
    if (areaId >= 110) return 109; /* Act 5 */
    if (areaId >= 103) return 103; /* Act 4 */
    if (areaId >= 75)  return 75;  /* Act 3 */
    if (areaId >= 40)  return 40;  /* Act 2 */
    return 1;                       /* Act 1 */
}

/* Get which act an area belongs to (1-5) */
static int GetActForArea(int areaId) {
    if (areaId >= 110) return 5;
    if (areaId >= 103) return 4;
    if (areaId >= 75)  return 3;
    if (areaId >= 40)  return 2;
    return 1;
}

/* Get the max act included in goal_scope */
static int GetMaxActForGoal(void) {
    switch (g_apGoalScope) {
        case 0: return 1; /* Andariel */
        case 1: return 2; /* Duriel */
        case 2: return 3; /* Mephisto */
        case 3: return 4; /* Diablo */
        default: return 5; /* Baal */
    }
}

/* ================================================================
 * 1.8.0 NEW — Gated zone-locking using preload regions
 * ================================================================
 * The old ZoneKeyDef[36] system is retained as dead code for save-file
 * backward compat (LoadChecks still reads `zonekey_N=1` lines), but the
 * active path in 1.8.0 is preload-driven: 18 gates per difficulty, 4
 * regions per act (R2-R5), locked/unlocked by gate-keys.
 *
 * Gate-key receipt state tracked per (difficulty × act × gate_idx).
 * AP item IDs:
 *   Normal:    46101-46118 (18 keys: act1g1..act5g4, skipping act4g3/4 slots)
 *   Nightmare: 46121-46138
 *   Hell:      46141-46158
 *
 * Slot encoding within each difficulty range:
 *   0-3   = Act 1 Gates 1-4
 *   4-7   = Act 2 Gates 1-4
 *   8-11  = Act 3 Gates 1-4
 *   12-13 = Act 4 Gates 1-2 (Act 4 has only 2 gates)
 *   14-17 = Act 5 Gates 1-4
 */
#define GATEKEY_PER_DIFF       18
#define GATEKEY_AP_BASE_NORMAL    46101
#define GATEKEY_AP_BASE_NM        46121
#define GATEKEY_AP_BASE_HELL      46141

static BOOL g_gateKeyReceived[3][GATEKEY_PER_DIFF];  /* [diff][slot] */

/* 1.8.2 NEW — Persistent gate-boss kill flag.
 * Set TRUE in ScanMonsters() when a gate boss dies. Used by:
 *   1. CustomBoss_AppendGateBosses() to SKIP already-killed bosses on
 *      character load → boss does not respawn after quit+reload.
 *   2. F4 tracker UI → show KILLED status independent of key receipt
 *      (key may still be in transit from AP server).
 * Saved to d2arch_state_<char>.dat via SaveStateFile / LoadChecks. */
static BOOL g_gateBossKilled[3][GATEKEY_PER_DIFF];   /* [diff][slot] */

/* Flatten (act, gate_idx) to slot 0..17. Returns -1 if invalid. */
static int GateKey_SlotFromActGate(int act, int gate_idx) {
    if (act < 1 || act > 5) return -1;
    if (gate_idx < 0) return -1;
    switch (act) {
        case 1: if (gate_idx < 4) return 0 + gate_idx;  else return -1;
        case 2: if (gate_idx < 4) return 4 + gate_idx;  else return -1;
        case 3: if (gate_idx < 4) return 8 + gate_idx;  else return -1;
        case 4: if (gate_idx < 2) return 12 + gate_idx; else return -1;
        case 5: if (gate_idx < 4) return 14 + gate_idx; else return -1;
    }
    return -1;
}

/* Inverse: slot 0..17 to (act, gate_idx). Returns FALSE if invalid. */
static BOOL GateKey_ActGateFromSlot(int slot, int* outAct, int* outGate) {
    if (slot < 0 || slot >= GATEKEY_PER_DIFF) return FALSE;
    if (slot <= 3)        { *outAct = 1; *outGate = slot; return TRUE; }
    else if (slot <= 7)   { *outAct = 2; *outGate = slot - 4; return TRUE; }
    else if (slot <= 11)  { *outAct = 3; *outGate = slot - 8; return TRUE; }
    else if (slot <= 13)  { *outAct = 4; *outGate = slot - 12; return TRUE; }
    else                  { *outAct = 5; *outGate = slot - 14; return TRUE; }
}

/* AP item ID → (diff, slot). Returns FALSE if not a gate-key item. */
static BOOL GateKey_FromAPId(int apId, int* outDiff, int* outSlot) {
    if (apId >= GATEKEY_AP_BASE_NORMAL && apId < GATEKEY_AP_BASE_NORMAL + GATEKEY_PER_DIFF) {
        *outDiff = 0;
        *outSlot = apId - GATEKEY_AP_BASE_NORMAL;
        return TRUE;
    }
    if (apId >= GATEKEY_AP_BASE_NM && apId < GATEKEY_AP_BASE_NM + GATEKEY_PER_DIFF) {
        *outDiff = 1;
        *outSlot = apId - GATEKEY_AP_BASE_NM;
        return TRUE;
    }
    if (apId >= GATEKEY_AP_BASE_HELL && apId < GATEKEY_AP_BASE_HELL + GATEKEY_PER_DIFF) {
        *outDiff = 2;
        *outSlot = apId - GATEKEY_AP_BASE_HELL;
        return TRUE;
    }
    return FALSE;
}

/* Get AP item ID for (diff, act, gate_idx). Returns 0 if invalid. */
static int GateKey_ToAPId(int diff, int act, int gate_idx) {
    int slot = GateKey_SlotFromActGate(act, gate_idx);
    if (slot < 0) return 0;
    int base = (diff == 0) ? GATEKEY_AP_BASE_NORMAL :
               (diff == 1) ? GATEKEY_AP_BASE_NM :
               (diff == 2) ? GATEKEY_AP_BASE_HELL : 0;
    if (!base) return 0;
    return base + slot;
}

/* Lock zones R2..R5 for all 5 acts based on current difficulty's preloads.
 * R1 zones stay unlocked (starting sphere). Always-open zones stay unlocked.
 * Called from InitZoneLocks() when zone_locking=1. */
static void InitZoneLocks_FromPreloads(void) {
    extern int g_currentDifficulty;
    int diff = g_currentDifficulty;
    if (diff < 0 || diff > 2) diff = 0;

    for (int act = 1; act <= 5; act++) {
        const ActRegionDef* ar = &g_actRegions[act - 1];

        /* Lock R2 zones */
        for (int i = 0; i < ar->r2n; i++) {
            int a = ar->r2[i];
            if (a > 0 && a < MAX_AREA_ID) g_zoneLocked[a] = TRUE;
        }
        /* Lock R3 */
        for (int i = 0; i < ar->r3n; i++) {
            int a = ar->r3[i];
            if (a > 0 && a < MAX_AREA_ID) g_zoneLocked[a] = TRUE;
        }
        /* Lock R4 */
        for (int i = 0; i < ar->r4n; i++) {
            int a = ar->r4[i];
            if (a > 0 && a < MAX_AREA_ID) g_zoneLocked[a] = TRUE;
        }
        /* Lock R5 */
        for (int i = 0; i < ar->r5n; i++) {
            int a = ar->r5[i];
            if (a > 0 && a < MAX_AREA_ID) g_zoneLocked[a] = TRUE;
        }

        /* Apply any gate keys already received for this difficulty */
        for (int g = 0; g < ar->num_gates; g++) {
            int slot = GateKey_SlotFromActGate(act, g);
            if (slot < 0) continue;
            if (!g_gateKeyReceived[diff][slot]) continue;
            /* Gate N Key unlocks Region N+1 */
            const int* zones; int count;
            switch (g) {
                case 0: zones = ar->r2; count = ar->r2n; break;
                case 1: zones = ar->r3; count = ar->r3n; break;
                case 2: zones = ar->r4; count = ar->r4n; break;
                case 3: zones = ar->r5; count = ar->r5n; break;
                default: continue;
            }
            for (int i = 0; i < count; i++) {
                int a = zones[i];
                if (a > 0 && a < MAX_AREA_ID) g_zoneLocked[a] = FALSE;
            }
        }
    }

    /* Always-open zones (Cow Level, Tristram-portal, Nihlathak-branch, Pandemonium) */
    for (int i = 0; i < g_alwaysOpenCount; i++) {
        int a = g_alwaysOpenZones[i];
        if (a > 0 && a < MAX_AREA_ID) g_zoneLocked[a] = FALSE;
    }

    Log("InitZoneLocks_FromPreloads: diff=%d — preload-gated zones applied\n", diff);
}

/* Initialize zone locks based on game mode */
static void InitZoneLocks(void) {
    memset(g_zoneLocked, 0, sizeof(g_zoneLocked));
    /* Keep legacy g_zoneKeyReceived zeroed so old LoadChecks path doesn't
     * accidentally unlock anything in the new system. */
    memset(g_zoneKeyReceived, 0, sizeof(g_zoneKeyReceived));
    g_pendingZoneTeleport = 0;
    g_lastSafeArea = 1;

    if (!g_zoneLockingOn) return; /* Zone Locking toggle is OFF */

    /* 1.8.0 preload-gated path */
    InitZoneLocks_FromPreloads();
}

/* Unlock zones for a gate-key receipt. Called from AP item handler. */
static void UnlockGateKey(int diff, int slot) {
    if (diff < 0 || diff > 2) return;
    if (slot < 0 || slot >= GATEKEY_PER_DIFF) return;
    if (g_gateKeyReceived[diff][slot]) return; /* already received */
    g_gateKeyReceived[diff][slot] = TRUE;

    int act, gate_idx;
    if (!GateKey_ActGateFromSlot(slot, &act, &gate_idx)) return;

    /* Only actually open zones if the player is currently on this difficulty */
    extern int g_currentDifficulty;
    if (g_currentDifficulty == diff) {
        const ActRegionDef* ar = &g_actRegions[act - 1];
        const int* zones; int count;
        switch (gate_idx) {
            case 0: zones = ar->r2; count = ar->r2n; break;
            case 1: zones = ar->r3; count = ar->r3n; break;
            case 2: zones = ar->r4; count = ar->r4n; break;
            case 3: zones = ar->r5; count = ar->r5n; break;
            default: return;
        }
        for (int i = 0; i < count; i++) {
            int a = zones[i];
            if (a > 0 && a < MAX_AREA_ID) g_zoneLocked[a] = FALSE;
        }
    }

    char msg[96];
    const char* diffName = (diff == 0) ? "Normal" : (diff == 1) ? "NM" : "Hell";
    _snprintf(msg, sizeof(msg), "Act %d Gate %d Key received (%s)", act, gate_idx + 1, diffName);
    ShowNotify(msg);
    Log("GateKey: diff=%d slot=%d (act=%d gate=%d) received\n", diff, slot, act, gate_idx + 1);
}

/* Check if an area is locked */
static BOOL IsAreaLocked(int areaId) {
    if (!g_zoneLockingOn) return FALSE;
    if (areaId <= 0 || areaId >= MAX_AREA_ID) return FALSE;
    if (IsTown((DWORD)areaId)) return FALSE;
    return g_zoneLocked[areaId];
}

/* Unlock all areas for a zone key */
static void UnlockZoneKey(int keyIdx) {
    if (keyIdx < 0 || keyIdx >= ZONE_KEY_COUNT) return;
    if (g_zoneKeyReceived[keyIdx]) return; /* Already received */

    g_zoneKeyReceived[keyIdx] = TRUE;
    const ZoneKeyDef* key = &g_zoneKeyDefs[keyIdx];
    for (int i = 0; i < 10 && key->areas[i] != 0; i++) {
        int a = key->areas[i];
        if (a > 0 && a < MAX_AREA_ID) {
            g_zoneLocked[a] = FALSE;
        }
    }
    char msg[128];
    sprintf(msg, "ZONE UNLOCKED: %s", key->name);
    ShowNotify(msg);
    Log("Zone key %d (%s) received — areas unlocked\n", keyIdx, key->name);
}

/* Convert AP item ID to zone key index, returns -1 if not a zone key */
static int APItemToZoneKeyIdx(int apItemId) {
    if (apItemId < AP_ZONE_KEY_BASE || apItemId >= AP_ZONE_KEY_BASE + ZONE_KEY_COUNT)
        return -1;
    return apItemId - AP_ZONE_KEY_BASE;
}

/* Count how many zone keys have been received */
static int CountZoneKeysReceived(void) {
    int count = 0;
    for (int i = 0; i < ZONE_KEY_COUNT; i++)
        if (g_zoneKeyReceived[i]) count++;
    return count;
}

/* Zone key prerequisites: which key must be unlocked before this one is eligible.
 * -1 = no prerequisite (reachable from town or starting area). */
static const int g_zoneKeyPrereqs[ZONE_KEY_COUNT] = {
    /* Act 1 (keys 0-9) */
    -1,  /*  0: Cold Plains — reachable from Blood Moor */
     0,  /*  1: Burial Grounds — needs Cold Plains */
     0,  /*  2: Stony Field — needs Cold Plains */
     2,  /*  3: Dark Wood — needs Stony Field (via Underground Passage) */
     3,  /*  4: Black Marsh — needs Dark Wood */
     2,  /*  5: Tristram — needs Stony Field (Cairn Stones portal) */
     4,  /*  6: Monastery — needs Black Marsh (via Tamoe Highland) */
     6,  /*  7: Jail & Cathedral — needs Monastery */
     7,  /*  8: Catacombs — needs Jail & Cathedral */
     8,  /*  9: Andariel's Lair — needs Catacombs */
    /* Act 2 (keys 10-17) */
    -1,  /* 10: Rocky Waste — exit town */
    10,  /* 11: Dry Hills — needs Rocky Waste */
    11,  /* 12: Far Oasis — needs Dry Hills */
    12,  /* 13: Lost City — needs Far Oasis */
    -1,  /* 14: Palace — accessible from town directly */
    14,  /* 15: Arcane Sanctuary — needs Palace */
    15,  /* 16: Canyon of Magi — needs Arcane Sanctuary */
    16,  /* 17: Duriel's Lair — needs Canyon of Magi */
    /* Act 3 (keys 18-23) */
    -1,  /* 18: Spider Forest — exit town */
    18,  /* 19: Jungle — needs Spider Forest */
    19,  /* 20: Kurast — needs Jungle */
    20,  /* 21: Upper Kurast — needs Kurast */
    21,  /* 22: Travincal — needs Upper Kurast */
    22,  /* 23: Durance of Hate — needs Travincal */
    /* Act 4 (keys 24-27) */
    -1,  /* 24: Outer Steppes — exit town */
    24,  /* 25: City of Damned — needs Outer Steppes */
    25,  /* 26: River of Flame — needs City of Damned */
    26,  /* 27: Chaos Sanctuary — needs River of Flame */
    /* Act 5 (keys 28-34) */
    -1,  /* 28: Bloody Foothills — exit town */
    28,  /* 29: Highlands — needs Bloody Foothills */
    29,  /* 30: Caverns — needs Highlands */
    30,  /* 31: Summit — needs Caverns */
    29,  /* 32: Nihlathak — needs Highlands (portal from town after quest) */
    31,  /* 33: Worldstone Keep — needs Summit */
    33,  /* 34: Throne of Destruction — needs Worldstone Keep */
    /* Reserved (keys 35-37) */
    -1, -1, -1,
};

/* Get next zone key using seed-based randomization.
 * Only picks from keys whose prerequisites are already unlocked.
 * Creates a unique labyrinth path through each act per seed. */
static int GetNextLockedZoneKey(void) {
    int maxAct = GetMaxActForGoal();

    /* Build eligible pool: keys that are unreceived AND have prereqs met */
    int eligible[ZONE_KEY_COUNT];
    int eligibleCount = 0;

    for (int i = 0; i < ZONE_KEY_COUNT; i++) {
        if (g_zoneKeyReceived[i]) continue;
        if (g_zoneKeyDefs[i].act < 1 || g_zoneKeyDefs[i].act > maxAct) continue;

        int prereq = g_zoneKeyPrereqs[i];
        if (prereq >= 0 && !g_zoneKeyReceived[prereq]) continue; /* prereq not met */

        eligible[eligibleCount++] = i;
    }

    if (eligibleCount == 0) return -1;
    if (eligibleCount == 1) return eligible[0];

    /* Deterministic random pick based on character seed + keys received */
    int received = CountZoneKeysReceived();
    DWORD hash = g_seed ^ (received * 2654435761u); /* Knuth multiplicative hash */
    int pick = (int)(hash % (unsigned)eligibleCount);

    Log("Zone key selection: %d eligible, seed=%u, pick=%d -> key %d (%s)\n",
        eligibleCount, g_seed, pick, eligible[pick], g_zoneKeyDefs[eligible[pick]].name);

    return eligible[pick];
}

/* Track dead monster unit IDs to avoid double-counting */
#define MAX_DEAD_TRACKED 512
static DWORD g_deadIds[MAX_DEAD_TRACKED];
static int g_deadCount = 0;

#define MODE_DEATH 0
#define MODE_DEAD  12

static BOOL IsTrackedBoss(DWORD txtId) {
    switch (txtId) {
        case 156: return TRUE;  /* Andariel */
        case 211: return TRUE;  /* Duriel */
        case 229: return TRUE;  /* Radament */
        case 242: return TRUE;  /* Mephisto */
        case 243: return TRUE;  /* Diablo */
        case 250: return TRUE;  /* The Summoner */
        case 256: return TRUE;  /* Izual */
        case 267: return TRUE;  /* Blood Raven */
        case 514: return TRUE;  /* Nihlathak */
        case 544: return TRUE;  /* Baal */
        default:  return FALSE;
    }
}

static DWORD SafeRead(DWORD addr) {
    DWORD val = 0;
    __try { val = *(DWORD*)addr; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return val;
}

static BOOL IsTown(DWORD area) {
    return (area == 0 || area == 1 || area == 40 || area == 75 || area == 103 || area == 109);
}

/* Forward declarations */
static void* Player(void);
static void GetArchDir(char* buf, int bufSize);

/* ================================================================
 * SKILL TREE PATCHING - ApplyAllSlots system
 * ================================================================ */

/* SkillsTxt struct offsets (record size 0x23C) */
#define SKT_CHARCLASS   0x0C
#define SKT_REQLEVEL    0x174
#define SKT_REQSKILL0   0x17E
#define SKT_REQSKILL1   0x180
#define SKT_REQSKILL2   0x182
#define SKT_SKILLDESC   0x194
#define SKT_SIZE        0x23C

/* SkillDescTxt struct offsets (record size 0x120) */
#define SDT_PAGE        0x02
#define SDT_ROW         0x03
#define SDT_COL         0x04
#define SDT_ICONCEL     0x07
#define SDT_SIZE        0x120

/* sgptDataTables offsets */
#define DT_OFFSET       0x96A20
#define DT_SKILLDESC    0xB8C
#define DT_SKILLDESC_N  0xB94
#define DT_SKILLS       0xB98
#define DT_SKILLS_N     0xBA0

/* Per-class tree positions: [class][tab][slot] -> {row, col} */
static const int TREE_POS[7][3][10][2] = {
    /* AMAZON */
    {
        {{1,2},{1,3},{2,1},{2,2},{3,3},{4,1},{4,2},{5,2},{5,3},{6,1}},
        {{1,1},{1,3},{2,2},{3,1},{3,2},{4,3},{5,1},{5,2},{6,1},{6,3}},
        {{1,1},{2,2},{2,3},{3,1},{3,3},{4,2},{4,3},{5,1},{6,2},{6,3}},
    },
    /* SORCERESS */
    {
        {{1,2},{1,3},{2,1},{3,1},{3,2},{4,1},{4,3},{5,2},{6,2},{6,3}},
        {{1,2},{2,1},{2,3},{3,1},{3,2},{4,2},{4,3},{5,1},{5,3},{6,2}},
        {{1,2},{1,3},{2,1},{2,2},{3,3},{4,2},{5,1},{5,3},{6,1},{6,2}},
    },
    /* NECROMANCER */
    {
        {{1,2},{2,1},{2,3},{3,2},{3,3},{4,1},{4,2},{5,1},{5,3},{6,2}},
        {{1,2},{1,3},{2,1},{2,2},{3,3},{4,1},{4,2},{5,3},{6,1},{6,2}},
        {{1,1},{1,3},{2,2},{3,1},{3,3},{4,2},{5,1},{5,2},{6,2},{6,3}},
    },
    /* PALADIN */
    {
        {{1,1},{1,3},{2,2},{3,1},{3,3},{4,1},{4,2},{5,1},{5,3},{6,2}},
        {{1,1},{2,2},{2,3},{3,1},{4,1},{4,2},{5,2},{5,3},{6,1},{6,3}},
        {{1,1},{1,3},{2,2},{2,3},{3,1},{3,3},{4,2},{5,1},{6,2},{6,3}},
    },
    /* BARBARIAN */
    {
        {{1,2},{2,1},{2,3},{3,2},{3,3},{4,1},{4,2},{5,3},{6,1},{6,2}},
        {{1,1},{1,2},{1,3},{2,1},{2,2},{2,3},{3,1},{4,3},{5,1},{6,3}},
        {{1,1},{1,3},{2,1},{2,2},{3,3},{4,1},{5,2},{5,3},{6,1},{6,2}},
    },
    /* DRUID */
    {
        {{1,2},{1,3},{2,1},{2,2},{3,3},{4,1},{4,2},{5,3},{6,1},{6,2}},
        {{1,1},{1,2},{2,3},{3,1},{3,3},{4,1},{4,2},{5,2},{5,3},{6,1}},
        {{1,1},{2,1},{2,3},{3,1},{3,3},{4,2},{5,1},{5,2},{6,1},{6,2}},
    },
    /* ASSASSIN */
    {
        {{1,2},{2,1},{2,3},{3,1},{3,2},{4,3},{5,1},{5,2},{6,1},{6,3}},
        {{1,2},{1,3},{2,1},{3,2},{3,3},{4,1},{4,2},{5,3},{6,1},{6,2}},
        {{1,2},{1,3},{2,1},{2,3},{3,2},{4,1},{4,3},{5,1},{5,3},{6,2}},
    },
};

/* Cache for original charclass values */
static short g_origCharClass[400];
static BOOL  g_origCacheInit = FALSE;
static int   g_savedClass = -1;

/* Get player class from unit */
static int GetPlayerClass(void) {
    void* p = NULL;
    if (!fnGetPlayer) return -1;
    __try {
        p = fnGetPlayer();
