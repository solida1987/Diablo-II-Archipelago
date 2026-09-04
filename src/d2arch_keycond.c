/* d2arch_keycond.c — Combined-mode Key Conditions (zone-locking + entrance-shuffle together) WHY: zone-locking hands you a gate key for killing a super-unique in a specific zone; entrance-shuffle moves which zone an entrance leads to, so the boss is shuffled away and the shuffle can drop you into a still-locked zone -> kicked. */

#define KC_NUM_GATES 18   /* 4+4+4+2+4 across acts 1..5 */

typedef enum {
    KC_GOLD = 0,      /* lifetime gold >= threshold — always available */
    KC_LEVEL,         /* character level >= threshold — always available */
    KC_SHRINES,       /* shrines activated (this diff) — needs CheckShrines */
    KC_BARRELS,       /* barrels broken (this diff) — act>=3, needs CheckBarrels */
    KC_URNS,          /* urns broken (this diff) — act>=3, needs CheckUrns */
    KC_CHESTS,        /* chests opened (this diff) — act>=3, needs CheckChests */
    KC_MONSTERS,      /* monsters killed THIS difficulty — always (g_charStats), big numbers */
    KC_SUPERUNIQUES,  /* super-uniques killed (lifetime) — always (g_charStats) */
    KC_TYPE_COUNT
} KeyCondType;

typedef struct { uint8_t type; uint32_t threshold; } KeyCondEntry;
static KeyCondEntry g_keyCond[3][KC_NUM_GATES];
static DWORD        g_keyCondSeed = 0;     /* seed the current table was built from (0 = none) */

/* Bonus-check category ids (match d2arch_bonuschecks.c BX_*). */
#define KC_BX_SHRINE 0
#define KC_BX_URN    1
#define KC_BX_BARREL 2
#define KC_BX_CHEST  3

/* Whether a given bonus check is enabled — so we never assign a condition the player can't make progress on. */
extern int g_bonusEnabled[];   /* BOOL[]; indexed by BX_* */

BOOL KeyCond_Active(void) {
    return g_zoneLockingOn && g_entranceShuffleEnabled;
}

/* slot 0..17 -> 1-based act + 0-based gate within act. */
static void KC_SlotToActGate(int slot, int* act, int* gate) {
    if      (slot < 4)  { *act = 1; *gate = slot;       }
    else if (slot < 8)  { *act = 2; *gate = slot - 4;   }
    else if (slot < 12) { *act = 3; *gate = slot - 8;   }
    else if (slot < 14) { *act = 4; *gate = slot - 12;  }
    else                { *act = 5; *gate = slot - 14;  }
}

/* Small deterministic mixer (so the table is identical for a given seed). */
static DWORD KC_Hash(DWORD seed, int diff, int slot) {
    DWORD h = seed ^ 0x9E3779B9u;
    h = (h ^ (DWORD)(diff * 2654435761u)) * 2246822519u;
    h = (h ^ (DWORD)(slot * 40503u))      * 3266489917u;
    h ^= h >> 15;
    return h;
}

/* Threshold for a given (type, act 1..5, diff 0..2). */
static uint32_t KC_ThresholdFor(uint8_t type, int act, int diff) {
    int a = act - 1;                 /* 0..4 */
    if (a < 0) a = 0; if (a > 4) a = 4;
    if (diff < 0) diff = 0; if (diff > 2) diff = 2;
    switch (type) {
        case KC_LEVEL: {              /* cumulative char level (clamp 98) */
            /* Marco's expected character level at the END of each act on standard XP — the whole calibration anchor. */
            static const int L[3][5] = {
                { 10, 20, 25, 30, 40 },   /* Normal */
                { 45, 50, 55, 60, 65 },   /* Nightmare */
                { 68, 70, 74, 78, 80 },   /* Hell */
            };
            int v = L[diff][a]; if (v > 98) v = 98; return (uint32_t)v;
        }
        case KC_GOLD: {               /* lifetime gold, ~accumulated by act-end */
            static const uint32_t G[3][5] = {
                {   12000u,   35000u,   60000u,   95000u,  160000u },
                {  400000u,  600000u,  850000u, 1150000u, 1500000u },
                { 2000000u, 2600000u, 3400000u, 4400000u, 5600000u },
            };
            return G[diff][a];
        }
        case KC_MONSTERS: {           /* LIFETIME kills (cumulative across all difficulties) — Marco's "kill quest" ladder = the milestone at the END of each act, counting straight up 500 -> 100000. */
            static const uint32_t M[3][5] = {
                {   500u,  1500u,  3000u,  5000u,  10000u },  /* Normal */
                { 14000u, 19000u, 25000u, 32000u,  40000u },  /* Nightmare */
                { 50000u, 62000u, 75000u, 88000u, 100000u },  /* Hell */
            };
            return M[diff][a];
        }
        case KC_SUPERUNIQUES: {       /* lifetime super-uniques (1-per-game). */
            static const int S[3][5] = {
                {  2,  4,  6,  9, 12 },   /* Normal */
                { 16, 20, 24, 28, 34 },   /* Nightmare */
                { 40, 46, 52, 58, 64 },   /* Hell */
            };
            return (uint32_t)S[diff][a];
        }
        case KC_SHRINES: {            /* LIFETIME shrines (sum of all diffs, ~50/diff cap) */
            static const int SH[3][5] = {
                {  5, 11, 18, 25, 32 },   /* Normal */
                { 40, 47, 54, 61, 68 },   /* Nightmare */
                { 74, 80, 86, 92, 98 },   /* Hell */
            };
            return (uint32_t)SH[diff][a];
        }
        case KC_BARRELS: case KC_URNS: { /* LIFETIME (~100/diff cap). */
            static const int B[3][5] = {
                {   0,   0,  18,  35,  55 },   /* Normal (act 3-5) */
                {  70,  88, 106, 124, 142 },   /* Nightmare (all acts) */
                { 162, 182, 202, 222, 242 },   /* Hell (all acts) */
            };
            return (uint32_t)B[diff][a];
        }
        case KC_CHESTS: {             /* LIFETIME (~200/diff cap). */
            static const int C[3][5] = {
                {   8,  16,  25,  48,  75 },   /* Normal (all acts) */
                { 100, 130, 160, 190, 220 },   /* Nightmare (all acts) */
                { 255, 295, 335, 375, 415 },   /* Hell (all acts) */
            };
            return (uint32_t)C[diff][a];
        }
    }
    return 10;
}

/* Build the 54-entry table from the seed. */
static void KeyCond_Generate(DWORD seed) {
    BOOL shrineOn = (g_bonusEnabled[KC_BX_SHRINE] != 0);
    BOOL barrelOn = (g_bonusEnabled[KC_BX_BARREL] != 0);
    BOOL urnOn    = (g_bonusEnabled[KC_BX_URN]    != 0);
    BOOL chestOn  = (g_bonusEnabled[KC_BX_CHEST]  != 0);

    /* slot of gate 0 for each act (act4 has only 2 gates). */
    static const int kActBase[6] = { 0, 0, 4, 8, 12, 14 };

    for (int d = 0; d < 3; d++) {
        for (int act = 1; act <= 5; act++) {
            int gatesInAct = (act == 4) ? 2 : 4;
            int lastG      = gatesInAct - 1;
            int base       = kActBase[act];

            /* The LAST gate of EVERY act is the monster-kill capstone (Marco): the kill ladder (KC_MONSTERS, lifetime) is the progression spine and is always the act's final key, so it lands on the highest gate slot. */
            g_keyCond[d][base + lastG].type = KC_MONSTERS;
            g_keyCond[d][base + lastG].threshold = KC_ThresholdFor(KC_MONSTERS, act, d);

            /* The OTHER gates get DISTINCT random types — never monsters (reserved for last) — so the side-conditions are well mixed and not always the same per act (Marco). */
            BOOL tier2 = (d >= 1 || act >= 3);
            uint8_t avail[8]; int na = 0;
            avail[na++] = KC_SUPERUNIQUES;
            avail[na++] = KC_LEVEL;
            avail[na++] = KC_GOLD;
            if (shrineOn)          avail[na++] = KC_SHRINES;
            if (chestOn)           avail[na++] = KC_CHESTS;
            if (tier2 && barrelOn) avail[na++] = KC_BARRELS;
            if (tier2 && urnOn)    avail[na++] = KC_URNS;

            /* Seeded Fisher-Yates shuffle — deterministic per seed (AP + standalone agree), but varied so the same type isn't always in the same slot. */
            for (int i = na - 1; i > 0; i--) {
                DWORD h = KC_Hash(seed, d, act * 16 + i);
                int j = (int)(h % (DWORD)(i + 1));
                uint8_t t = avail[i]; avail[i] = avail[j]; avail[j] = t;
            }

            for (int g = 0; g < lastG; g++) {
                int slot = base + g;
                uint8_t type = avail[g % na];           /* distinct: lastG <= na */
                g_keyCond[d][slot].type = type;
                g_keyCond[d][slot].threshold = KC_ThresholdFor(type, act, d);
            }
        }
    }
    g_keyCondSeed = seed;
    Log("KEYCOND: generated 54 key conditions from seed=%u (shrine=%d barrel=%d urn=%d chest=%d)\n",
        seed, (int)shrineOn, (int)barrelOn, (int)urnOn, (int)chestOn);
}

/* Lifetime bonus-object count = sum across ALL three difficulties (Marco: shrines/barrels/urns/chests count straight through every difficulty, like monsters — easier for players to read one climbing number). */
static uint32_t KC_BonusLifetime(int category) {
    long t = 0;
    for (int d = 0; d < 3; d++) t += Bonus_GetSlotCount(category, d);
    return (t < 0) ? 0 : (uint32_t)t;
}

/* Current live progress toward a gate's condition. */
static uint32_t KeyCond_Progress(int diff, int slot) {
    KeyCondEntry* e = &g_keyCond[diff][slot];
    switch (e->type) {
        case KC_GOLD: {
            uint64_t g = 0;
            __try { g = g_charStats_lifetimeGold(); } __except(EXCEPTION_EXECUTE_HANDLER) { g = 0; }
            return (g > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)g;
        }
        case KC_LEVEL: {
            int lvl = 0;
            if (g_cachedPGame && fnGetStat) {
                void* pSrv = GetServerPlayer(g_cachedPGame);
                if (pSrv) { __try { lvl = fnGetStat(pSrv, 12, 0); } __except(EXCEPTION_EXECUTE_HANDLER) { lvl = 0; } }
            }
            return (lvl < 0) ? 0 : (uint32_t)lvl;
        }
        case KC_SHRINES: return KC_BonusLifetime(KC_BX_SHRINE);  /* lifetime, all diffs */
        case KC_BARRELS: return KC_BonusLifetime(KC_BX_BARREL);
        case KC_URNS:    return KC_BonusLifetime(KC_BX_URN);
        case KC_CHESTS:  return KC_BonusLifetime(KC_BX_CHEST);
        case KC_MONSTERS: {
            /* monsters killed LIFETIME — cumulative across ALL difficulties (Marco: the kill counter counts straight through Normal->NM->Hell). */
            uint64_t k = 0;
            __try { k = g_charStats.monstersKilled; } __except(EXCEPTION_EXECUTE_HANDLER) { k = 0; }
            return (k > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)k;
        }
        case KC_SUPERUNIQUES: {
            /* lifetime super-unique kills (1-per-game -> must be cumulative/saved, not per-session, or a reload would soft-lock it). */
            uint64_t k = 0;
            __try { k = g_charStats.superuniquesKilled; } __except(EXCEPTION_EXECUTE_HANDLER) { k = 0; }
            return (k > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)k;
        }
        default: return 0;
    }
}

/* Clear a gate via the condition — reuse the EXACT boss-kill plumbing. */
static void KeyCond_FireGate(int diff, int slot) {
    if (g_gateBossKilled[diff][slot]) return;
    g_gateBossKilled[diff][slot] = TRUE;
    int act, gate; KC_SlotToActGate(slot, &act, &gate);
    Log("KEYCOND: gate A%dG%d (diff=%d slot=%d) cleared by condition\n", act, gate + 1, diff, slot);
    if (!g_apConnected) {
        /* standalone — open the region now */
        UnlockGateKey(diff, slot);
    }
    /* AP — WriteChecksFile emits the gate check from g_gateBossKilled, exactly as a boss kill would; the server echoes the gate-key item back. */
    SaveStateFile();
    WriteChecksFile();

    /* Refresh the launcher Map (lounge) immediately. */
    {
        char zd[MAX_PATH], zm[MAX_PATH];
        GetArchDir(zd, MAX_PATH);
        _snprintf(zm, sizeof(zm), "%smap", zd);
        zm[sizeof(zm) - 1] = 0;
        D2Zones_WriteLockFile(zm);
    }
    {
        /* say what actually happened. */
        char buf[80];
        if (g_apConnected)
            _snprintf(buf, sizeof(buf), "Gate A%dG%d check sent — awaiting the key", act, gate + 1);
        else
            _snprintf(buf, sizeof(buf), "Gate A%dG%d key earned", act, gate + 1);
        buf[sizeof(buf) - 1] = 0;
        ShowNotify(buf);
    }
}

/* Per-tick driver — generate lazily / on seed change, then evaluate. */
void KeyCond_Tick(void) {
    if (!KeyCond_Active()) return;
    if (!g_cachedPGame) return;            /* not in a live game — no player to read */
    DWORD seed = (g_seed != 0) ? g_seed : HashCharName(g_charName);
    if (seed == 0) return;                 /* no usable seed yet */
    if (seed != g_keyCondSeed) KeyCond_Generate(seed);

    int diff = g_currentDifficulty;
    if (diff < 0 || diff > 2) return;
    for (int slot = 0; slot < KC_NUM_GATES; slot++) {
        if (g_gateBossKilled[diff][slot]) continue;            /* already cleared */
        if (KeyCond_Progress(diff, slot) >= g_keyCond[diff][slot].threshold)
            KeyCond_FireGate(diff, slot);
    }
}

/* For the F4 tracker: describe a gate's condition + live progress. */
const char* KeyCond_TypeName(int diff, int slot) {
    if (diff < 0 || diff > 2 || slot < 0 || slot >= KC_NUM_GATES) return "?";
    switch (g_keyCond[diff][slot].type) {
        case KC_GOLD:         return "Gold";
        case KC_LEVEL:        return "Level";
        case KC_SHRINES:      return "Shrines";
        case KC_BARRELS:      return "Barrels";
        case KC_URNS:         return "Urns";
        case KC_CHESTS:       return "Chests";
        case KC_MONSTERS:     return "Monsters";
        case KC_SUPERUNIQUES: return "Super-uniques";
        default:              return "?";
    }
}
uint32_t KeyCond_GetThreshold(int diff, int slot) {
    if (diff < 0 || diff > 2 || slot < 0 || slot >= KC_NUM_GATES) return 0;
    return g_keyCond[diff][slot].threshold;
}
uint32_t KeyCond_GetProgress(int diff, int slot) {
    if (diff < 0 || diff > 2 || slot < 0 || slot >= KC_NUM_GATES) return 0;
    return KeyCond_Progress(diff, slot);
}
