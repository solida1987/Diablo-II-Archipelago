/* Bonus check categories Adds up to 1494 extra AP locations driven by object interactions (shrines, urns, barrels, chests) plus first-time set-piece pickups and lifetime gold milestones. */

/* AP location ID base offsets (must match locations.py BONUS_BASE_*) */
#define BONUS_BASE_SHRINE   60000
#define BONUS_BASE_URN      60200
#define BONUS_BASE_BARREL   60500
#define BONUS_BASE_CHEST    60800
#define BONUS_BASE_GOLDMS   65000
#define BONUS_BASE_SETPICK  65100

#define BONUS_QUOTA_SHRINE  50
#define BONUS_QUOTA_URN     100
#define BONUS_QUOTA_BARREL  100
#define BONUS_QUOTA_CHEST   200

#define BONUS_NUM_GOLDMS_NORMAL    7
#define BONUS_NUM_GOLDMS_NIGHTMARE 5
#define BONUS_NUM_GOLDMS_HELL      5
#define BONUS_NUM_GOLDMS_TOTAL     (BONUS_NUM_GOLDMS_NORMAL + BONUS_NUM_GOLDMS_NIGHTMARE + BONUS_NUM_GOLDMS_HELL)

#define BONUS_NUM_SETPIECES        127

/* Gold milestone thresholds — must mirror locations.py */
static const uint64_t g_goldMilestoneThresholds[BONUS_NUM_GOLDMS_TOTAL] = {
    /* Normal */
    10000ull, 100000ull, 200000ull, 400000ull, 800000ull, 1800000ull, 3000000ull,
    /* Nightmare */
    3500000ull, 4000000ull, 4500000ull, 5000000ull, 6000000ull,
    /* Hell */
    7000000ull, 8000000ull, 9000000ull, 10000000ull, 12000000ull,
};

/* 1.9.0 reward types (must match RewardType enum in d2arch_quests.c for delivery code reuse). */
#define BR_GOLD          0
#define BR_SKILL         1   /* skill point (filler) */
#define BR_STAT          2
#define BR_TRAP          3
#define BR_RESETPT       4
#define BR_LOOT          5   /* boss loot drop */
#define BR_XP            6
#define BR_DROP_CHARM    7
#define BR_DROP_SET      8
#define BR_DROP_UNIQUE   9

/* Per-char state — slot indices and attempt counters per category/diff. */
typedef struct {
    /* Object check progress: which slot is active, how many attempts so far */
    uint16_t shrineSlot[3];   uint8_t shrineAttempt[3];
    uint16_t urnSlot[3];      uint8_t urnAttempt[3];
    uint16_t barrelSlot[3];   uint8_t barrelAttempt[3];
    uint16_t chestSlot[3];    uint8_t chestAttempt[3];

    /* Fired bitmap — one bit per location across the whole bonus range. */
    uint8_t fired[5232 / 8];  /* 5232 bits = 654 bytes */

    /* pre-rolled rewards per slot. */
    uint8_t  rewardType[5232];
    int32_t  rewardValue[5232];
    BOOL     rewardsRolled;  /* TRUE once Bonus_PreRollAllRewards has run */
} BonusCheckState;

static BonusCheckState g_bonusState;

/* Toggles received from slot_data (or d2arch.ini for standalone) */
static BOOL g_bonusEnabled[6] = { FALSE, FALSE, FALSE, FALSE, FALSE, FALSE };
#define BX_SHRINE       0
#define BX_URN          1
#define BX_BARREL       2
#define BX_CHEST        3
#define BX_SET_PICKUP   4
#define BX_GOLD_MS      5

/* Bitmap helpers (offset = AP location ID minus relevant base range; we use a single contiguous offset for compact storage). */
static int Bonus_OffsetFromApId(int apId) {
    /* Map the spread-out AP id ranges into a contiguous 0..5232 bitmap */
    if (apId >= BONUS_BASE_SHRINE  && apId < BONUS_BASE_SHRINE  + 150) return apId - BONUS_BASE_SHRINE;            /* 0..149 */
    if (apId >= BONUS_BASE_URN     && apId < BONUS_BASE_URN     + 300) return 200 + (apId - BONUS_BASE_URN);       /* 200..499 */
    if (apId >= BONUS_BASE_BARREL  && apId < BONUS_BASE_BARREL  + 300) return 500 + (apId - BONUS_BASE_BARREL);    /* 500..799 */
    if (apId >= BONUS_BASE_CHEST   && apId < BONUS_BASE_CHEST   + 600) return 800 + (apId - BONUS_BASE_CHEST);     /* 800..1399 */
    if (apId >= BONUS_BASE_GOLDMS  && apId < BONUS_BASE_GOLDMS  + BONUS_NUM_GOLDMS_TOTAL)
        return 5000 + (apId - BONUS_BASE_GOLDMS);
    if (apId >= BONUS_BASE_SETPICK && apId < BONUS_BASE_SETPICK + BONUS_NUM_SETPIECES)
        return 5100 + (apId - BONUS_BASE_SETPICK);
    return -1;
}

static BOOL Bonus_IsFired(int apId) {
    int off = Bonus_OffsetFromApId(apId);
    if (off < 0 || off >= (int)(sizeof(g_bonusState.fired) * 8)) return FALSE;
    return (g_bonusState.fired[off >> 3] & (1u << (off & 7))) != 0;
}

static void Bonus_MarkFired(int apId) {
    int off = Bonus_OffsetFromApId(apId);
    if (off < 0 || off >= (int)(sizeof(g_bonusState.fired) * 8)) return;
    g_bonusState.fired[off >> 3] |= (1u << (off & 7));
}

/* Public entry called from PollAPUnlocks when an AP unlock arrives whose location ID falls in a bonus-check range (60000-65999). */
BOOL Bonus_OnAPItemReceived(int apId) {
    int off = Bonus_OffsetFromApId(apId);
    if (off < 0) return FALSE;
    int byteIdx = off >> 3;
    int bitMask = 1u << (off & 7);
    BOOL wasAlreadyFired = (g_bonusState.fired[byteIdx] & bitMask) != 0;
    if (wasAlreadyFired) return TRUE;   /* already counted */
    g_bonusState.fired[byteIdx] |= bitMask;

    /* For shrine/urn/barrel/chest, also bump the per-diff slot counter. */
    if (apId >= BONUS_BASE_SHRINE && apId < BONUS_BASE_SHRINE + 150) {
        int local = apId - BONUS_BASE_SHRINE;
        int diff  = local / BONUS_QUOTA_SHRINE;
        if (diff >= 0 && diff < 3) g_bonusState.shrineSlot[diff]++;
    } else if (apId >= BONUS_BASE_URN && apId < BONUS_BASE_URN + 300) {
        int local = apId - BONUS_BASE_URN;
        int diff  = local / BONUS_QUOTA_URN;
        if (diff >= 0 && diff < 3) g_bonusState.urnSlot[diff]++;
    } else if (apId >= BONUS_BASE_BARREL && apId < BONUS_BASE_BARREL + 300) {
        int local = apId - BONUS_BASE_BARREL;
        int diff  = local / BONUS_QUOTA_BARREL;
        if (diff >= 0 && diff < 3) g_bonusState.barrelSlot[diff]++;
    } else if (apId >= BONUS_BASE_CHEST && apId < BONUS_BASE_CHEST + 600) {
        int local = apId - BONUS_BASE_CHEST;
        int diff  = local / BONUS_QUOTA_CHEST;
        if (diff >= 0 && diff < 3) g_bonusState.chestSlot[diff]++;
    }
    /* Set pickups + gold milestones: bitmap alone is the counter. */
    return TRUE;
}

/* Forward-decl: fires the AP location and pulses the in-game UI. */
static void Bonus_FireApLocation(int apId, const char* tag);
static void Bonus_DeliverStandalone(int apId, const char* tag);

/* Escalating-chance helper. */
static BOOL Bonus_RollEscalating(uint8_t attempt) {
    int pct = attempt * 10;
    if (pct < 10)   pct = 10;
    if (pct >= 100) return TRUE;
    return (rand() % 100) < pct;
}

/* Generic object-category roll handler. */
static BOOL Bonus_TryFireObject(int categoryIdx, int diff, int quota,
                                int apIdBase, uint16_t* slotPtr,
                                uint8_t* attemptPtr, const char* label)
{
    if (!g_bonusEnabled[categoryIdx]) return FALSE;
    if (diff < 0 || diff > 2) return FALSE;
    /* goal-scope gate. The apworld only generates bonus locations for the difficulties inside the seed's goal scope, so firing one above that scope (e.g. an NM shrine in a Normal-goal seed) sends a location id the server has never heard of — the bridge logs it as invalid on every trigger. Only fire within scope; the player can still play the higher difficulty. */
    if (g_apConnected && g_apDiffScope >= 0 && g_apDiffScope < 3 && diff > g_apDiffScope)
        return FALSE;
    if (*slotPtr >= quota) return FALSE;  /* category for this diff full */

    /* Pre-increment then roll. */
    if (*attemptPtr < 255) (*attemptPtr)++;
    BOOL hit = Bonus_RollEscalating(*attemptPtr);
    if (!hit) return FALSE;

    int apId = apIdBase + diff * quota + (*slotPtr);
    Bonus_FireApLocation(apId, label);
    (*slotPtr)++;
    *attemptPtr = 0;
    return TRUE;
}

/* Public per-category entry points called from d2arch_hooks.c */
BOOL Bonus_OnShrineActivated(int diff) {
    return Bonus_TryFireObject(BX_SHRINE, diff, BONUS_QUOTA_SHRINE,
                               BONUS_BASE_SHRINE,
                               &g_bonusState.shrineSlot[diff],
                               &g_bonusState.shrineAttempt[diff],
                               "Shrine");
}
BOOL Bonus_OnUrnBroken(int diff) {
    return Bonus_TryFireObject(BX_URN, diff, BONUS_QUOTA_URN,
                               BONUS_BASE_URN,
                               &g_bonusState.urnSlot[diff],
                               &g_bonusState.urnAttempt[diff],
                               "Urn");
}
BOOL Bonus_OnBarrelBroken(int diff) {
    return Bonus_TryFireObject(BX_BARREL, diff, BONUS_QUOTA_BARREL,
                               BONUS_BASE_BARREL,
                               &g_bonusState.barrelSlot[diff],
                               &g_bonusState.barrelAttempt[diff],
                               "Barrel");
}
BOOL Bonus_OnChestOpened(int diff) {
    return Bonus_TryFireObject(BX_CHEST, diff, BONUS_QUOTA_CHEST,
                               BONUS_BASE_CHEST,
                               &g_bonusState.chestSlot[diff],
                               &g_bonusState.chestAttempt[diff],
                               "Chest");
}

/* Set-piece pickup check. */
/* g_collGoal.setsTargeted is a uint8_t[32] gate. */
extern uint8_t Coll_IsSetTargeted(int setIdx);

/* Set piece -> set index mapping (mirrors d2arch_collections.c g_collSets[] firstSlot+pieceCount layout). */
static int Bonus_SetIdxFromPiece(int pieceIdx) {
    /* setIdx -> firstSlot+pieceCount cumulative table */
    static const int s_setStart[33] = {
        0,   3,   6,   9,  13,  17,  21,  25,  30,  35,
        41,  44,  47,  50,  54,  58,  62,  66,  70,  76,
        81,  85,  90,  95, 100, 104, 108, 112, 115, 117,
        120, 123, 127  /* sentinel */
    };
    if (pieceIdx < 0 || pieceIdx >= 127) return -1;
    for (int s = 0; s < 32; s++) {
        if (pieceIdx >= s_setStart[s] && pieceIdx < s_setStart[s + 1])
            return s;
    }
    return -1;
}

BOOL Bonus_OnSetPiecePickup(int pieceIdx) {
    if (!g_bonusEnabled[BX_SET_PICKUP]) return FALSE;
    if (pieceIdx < 0 || pieceIdx >= BONUS_NUM_SETPIECES) return FALSE;

    /* Per-set gating: respect collect_set_* YAML toggles via the collections.c Coll_IsSetTargeted accessor. */
    int setIdx = Bonus_SetIdxFromPiece(pieceIdx);
    if (setIdx >= 0) {
        if (!Coll_IsSetTargeted(setIdx)) return FALSE;
    }

    int apId = BONUS_BASE_SETPICK + pieceIdx;
    if (Bonus_IsFired(apId)) return FALSE;
    Bonus_FireApLocation(apId, "Set Pickup");
    return TRUE;
}

/* Gold milestone watcher. */
void Bonus_CheckGoldMilestones(uint64_t lifetimeGold) {
    if (!g_bonusEnabled[BX_GOLD_MS]) return;
    for (int i = 0; i < BONUS_NUM_GOLDMS_TOTAL; i++) {
        if (lifetimeGold < g_goldMilestoneThresholds[i]) break;  /* sorted */
        int apId = BONUS_BASE_GOLDMS + i;
        if (Bonus_IsFired(apId)) continue;
        char tag[48];
        _snprintf(tag, sizeof(tag), "Gold Milestone %llu",
                  (unsigned long long)g_goldMilestoneThresholds[i]);
        Bonus_FireApLocation(apId, tag);
    }
}

/* Reward pre-roll per slot. */
extern int g_uniqueCatalogCount;  /* in d2arch_quests.c — populated lazily */
extern void Quests_LoadUniqueCatalog(void);

static void Bonus_RollOneReward(uint8_t* outType, int32_t* outValue) {
    /* Same flat weighted picker as quests' AssignAllRewards. */
    extern BOOL g_skillHuntingOn;
    struct WRow { int weight; int rewardType; int extraIdx; };
    struct WRow wtable[] = {
        { 10,  BR_GOLD,        0 },
        { 15,  BR_XP,          0 },
        { 10,  BR_STAT,        0 },
        { 10,  BR_SKILL,       0 },
        { (g_skillHuntingOn ? 5 : 0), BR_RESETPT, 0 },
        {  2,  BR_TRAP,        0 /* TRAP_MONSTERS */ },
        {  1,  BR_TRAP,        1 /* TRAP_SLOW */     },
        {  1,  BR_TRAP,        2 /* TRAP_WEAKEN */   },
        {  1,  BR_TRAP,        3 /* TRAP_POISON */   },
        {  1,  BR_LOOT,        0 },  /* Andariel */
        {  2,  BR_LOOT,        1 },  /* Duriel */
        {  2,  BR_LOOT,        2 },  /* Mephisto */
        {  1,  BR_LOOT,        3 },  /* Diablo */
        {  1,  BR_LOOT,        4 },  /* Baal */
        {  9,  BR_DROP_CHARM,  0 },
        {  9,  BR_DROP_SET,    0 },
        {  6,  BR_DROP_UNIQUE, 0 },
    };
    int wcount = (int)(sizeof(wtable) / sizeof(wtable[0]));
    int totalW = 0;
    for (int i = 0; i < wcount; i++) totalW += wtable[i].weight;
    if (totalW <= 0) totalW = 1;
    int roll = rand() % totalW;
    int cum = 0;
    int rewardType = BR_GOLD;
    int extraIdx = 0;
    for (int i = 0; i < wcount; i++) {
        if (wtable[i].weight <= 0) continue;
        cum += wtable[i].weight;
        if (roll < cum) {
            rewardType = wtable[i].rewardType;
            extraIdx   = wtable[i].extraIdx;
            break;
        }
    }
    *outType = (uint8_t)rewardType;
    /* Roll specific value */
    switch (rewardType) {
        /* value is no longer an amount; the drain scales by level. */
        case BR_GOLD:        *outValue = 0;                      break;
        case BR_XP:          *outValue = 0;                      break;
        case BR_TRAP:        *outValue = extraIdx;               break;
        case BR_LOOT:        *outValue = extraIdx;               break;
        case BR_DROP_CHARM:  *outValue = rand() % 3;             break;
        case BR_DROP_SET:    *outValue = rand() % 127;           break;
        case BR_DROP_UNIQUE:
            if (!g_uniqueCatalogCount) Quests_LoadUniqueCatalog();
            /* quest bases excluded — see Quests_PickAnyUnique */
            *outValue = (g_uniqueCatalogCount > 0) ? Quests_PickAnyUnique() : 0;
            break;
        default:             *outValue = 0;                      break;
    }
}

void Bonus_PreRollAllRewards(unsigned seed) {
    /* Seed-based deterministic roll matching the rest of the per-char pre-roll system. */
    srand(seed + 999);
    int total = (int)(sizeof(g_bonusState.rewardType));
    for (int i = 0; i < total; i++) {
        Bonus_RollOneReward(&g_bonusState.rewardType[i], &g_bonusState.rewardValue[i]);
    }
    g_bonusState.rewardsRolled = TRUE;
    Log("Bonus_PreRollAllRewards: rolled %d slot rewards (seed=%u)\n", total, seed);
}

/* Apply received slot_data toggles. */
void Bonus_ApplyToggles(BOOL shrines, BOOL urns, BOOL barrels,
                        BOOL chests, BOOL setPickups, BOOL goldMs) {
    g_bonusEnabled[BX_SHRINE]     = shrines;
    g_bonusEnabled[BX_URN]        = urns;
    g_bonusEnabled[BX_BARREL]     = barrels;
    g_bonusEnabled[BX_CHEST]      = chests;
    g_bonusEnabled[BX_SET_PICKUP] = setPickups;
    g_bonusEnabled[BX_GOLD_MS]    = goldMs;
    Log("Bonus: toggles applied — shrines=%d urns=%d barrels=%d chests=%d "
        "setPickups=%d goldMs=%d\n",
        (int)shrines, (int)urns, (int)barrels, (int)chests,
        (int)setPickups, (int)goldMs);
}

/* State reset (called on character switch). */
void Bonus_ResetState(void) {
    memset(&g_bonusState, 0, sizeof(g_bonusState));
}

/* Persist + restore via per-char state file. */
void Bonus_SaveToFile(FILE* f) {
    if (!f) return;
    /* persist the toggle bitmap so existing characters created before the user enabled bonus toggles in d2arch.ini still get the right enabled state on next load. */
    fprintf(f, "bonus_enabled=%d,%d,%d,%d,%d,%d\n",
            g_bonusEnabled[BX_SHRINE]     ? 1 : 0,
            g_bonusEnabled[BX_URN]        ? 1 : 0,
            g_bonusEnabled[BX_BARREL]     ? 1 : 0,
            g_bonusEnabled[BX_CHEST]      ? 1 : 0,
            g_bonusEnabled[BX_SET_PICKUP] ? 1 : 0,
            g_bonusEnabled[BX_GOLD_MS]    ? 1 : 0);
    for (int d = 0; d < 3; d++) {
        if (g_bonusState.shrineSlot[d] || g_bonusState.shrineAttempt[d])
            fprintf(f, "bonus_shrine_%d=%u,%u\n", d,
                    g_bonusState.shrineSlot[d], g_bonusState.shrineAttempt[d]);
        if (g_bonusState.urnSlot[d] || g_bonusState.urnAttempt[d])
            fprintf(f, "bonus_urn_%d=%u,%u\n", d,
                    g_bonusState.urnSlot[d], g_bonusState.urnAttempt[d]);
        if (g_bonusState.barrelSlot[d] || g_bonusState.barrelAttempt[d])
            fprintf(f, "bonus_barrel_%d=%u,%u\n", d,
                    g_bonusState.barrelSlot[d], g_bonusState.barrelAttempt[d]);
        if (g_bonusState.chestSlot[d] || g_bonusState.chestAttempt[d])
            fprintf(f, "bonus_chest_%d=%u,%u\n", d,
                    g_bonusState.chestSlot[d], g_bonusState.chestAttempt[d]);
    }
    /* Fired bitmap: hex per byte, one line per 64-byte chunk. */
    fputs("bonus_fired=", f);
    for (size_t i = 0; i < sizeof(g_bonusState.fired); i++) {
        fprintf(f, "%02x", g_bonusState.fired[i]);
    }
    fputc('\n', f);
}

void Bonus_LoadLine(const char* line) {
    int d, sl, at;
    {
        /* restore frozen-at-creation toggle bitmap. */
        int es, eu, eb, ec, esp, eg;
        if (sscanf(line, "bonus_enabled=%d,%d,%d,%d,%d,%d",
                   &es, &eu, &eb, &ec, &esp, &eg) == 6) {
            Bonus_ApplyToggles(es != 0, eu != 0, eb != 0,
                               ec != 0, esp != 0, eg != 0);
            return;
        }
    }
    if (sscanf(line, "bonus_shrine_%d=%d,%d", &d, &sl, &at) == 3) {
        if (d >= 0 && d < 3) { g_bonusState.shrineSlot[d] = (uint16_t)sl; g_bonusState.shrineAttempt[d] = (uint8_t)at; }
        return;
    }
    if (sscanf(line, "bonus_urn_%d=%d,%d", &d, &sl, &at) == 3) {
        if (d >= 0 && d < 3) { g_bonusState.urnSlot[d] = (uint16_t)sl; g_bonusState.urnAttempt[d] = (uint8_t)at; }
        return;
    }
    if (sscanf(line, "bonus_barrel_%d=%d,%d", &d, &sl, &at) == 3) {
        if (d >= 0 && d < 3) { g_bonusState.barrelSlot[d] = (uint16_t)sl; g_bonusState.barrelAttempt[d] = (uint8_t)at; }
        return;
    }
    if (sscanf(line, "bonus_chest_%d=%d,%d", &d, &sl, &at) == 3) {
        if (d >= 0 && d < 3) { g_bonusState.chestSlot[d] = (uint16_t)sl; g_bonusState.chestAttempt[d] = (uint8_t)at; }
        return;
    }
    if (strncmp(line, "bonus_fired=", 12) == 0) {
        const char* hex = line + 12;
        size_t i = 0;
        while (hex[0] && hex[1] && i < sizeof(g_bonusState.fired)) {
            unsigned v = 0;
            sscanf(hex, "%2x", &v);
            g_bonusState.fired[i++] = (uint8_t)v;
            hex += 2;
        }
        return;
    }
}

/* derive the per-difficulty slot counters from the fired bitmap. */
void Bonus_RebuildCountersFromBitmap(void) {
    struct { int base, quota, span; uint16_t* slots; const char* name; } cats[4] = {
        {   0, BONUS_QUOTA_SHRINE, 150, g_bonusState.shrineSlot, "shrine" },
        { 200, BONUS_QUOTA_URN,    300, g_bonusState.urnSlot,    "urn"    },
        { 500, BONUS_QUOTA_BARREL, 300, g_bonusState.barrelSlot, "barrel" },
        { 800, BONUS_QUOTA_CHEST,  600, g_bonusState.chestSlot,  "chest"  },
    };
    for (int c = 0; c < 4; c++) {
        for (int d = 0; d < 3; d++) {
            int highest = -1;
            for (int s = 0; s < cats[c].quota; s++) {
                int off = cats[c].base + d * cats[c].quota + s;
                if (off < 0 || off >= cats[c].base + cats[c].span) break;
                if (off >= (int)(sizeof(g_bonusState.fired) * 8)) break;
                if (g_bonusState.fired[off >> 3] & (1u << (off & 7))) highest = s;
            }
            uint16_t derived = (uint16_t)(highest + 1);
            if (cats[c].slots[d] != derived) {
                Log("BONUS C8: %s diff=%d counter %u -> %u (rebuilt from bitmap)\n",
                    cats[c].name, d, cats[c].slots[d], derived);
                cats[c].slots[d] = derived;
            }
        }
    }
}

/* Iterate fired locations for WriteChecksFile to emit. */
int Bonus_NextFiredApId(int* iterState) {
    int total = (int)(sizeof(g_bonusState.fired) * 8);
    while (*iterState < total) {
        int off = *iterState;
        (*iterState)++;
        if (!(g_bonusState.fired[off >> 3] & (1u << (off & 7)))) continue;
        /* Map offset back to apId */
        if (off >= 0    && off < 150)        return BONUS_BASE_SHRINE  + off;
        if (off >= 200  && off < 500)        return BONUS_BASE_URN     + (off - 200);
        if (off >= 500  && off < 800)        return BONUS_BASE_BARREL  + (off - 500);
        if (off >= 800  && off < 1400)       return BONUS_BASE_CHEST   + (off - 800);
        if (off >= 5000 && off < 5000 + BONUS_NUM_GOLDMS_TOTAL)
            return BONUS_BASE_GOLDMS + (off - 5000);
        if (off >= 5100 && off < 5100 + BONUS_NUM_SETPIECES)
            return BONUS_BASE_SETPICK + (off - 5100);
        /* gap region -> skip */
    }
    return -1;
}

/* Public accessors for UI rendering (Page 8 progress bars). */
int Bonus_GetSlotCount(int category, int diff) {
    if (diff < 0 || diff > 2) return 0;
    switch (category) {
        case BX_SHRINE: return g_bonusState.shrineSlot[diff];
        case BX_URN:    return g_bonusState.urnSlot[diff];
        case BX_BARREL: return g_bonusState.barrelSlot[diff];
        case BX_CHEST:  return g_bonusState.chestSlot[diff];
    }
    return 0;
}
int Bonus_GetQuota(int category) {
    switch (category) {
        case BX_SHRINE: return BONUS_QUOTA_SHRINE;
        case BX_URN:    return BONUS_QUOTA_URN;
        case BX_BARREL: return BONUS_QUOTA_BARREL;
        case BX_CHEST:  return BONUS_QUOTA_CHEST;
    }
    return 0;
}
BOOL Bonus_IsCategoryEnabled(int category) {
    if (category < 0 || category >= 6) return FALSE;
    return g_bonusEnabled[category];
}
BOOL Bonus_IsGoldMilestoneFired(int milestoneIdx) {
    if (milestoneIdx < 0 || milestoneIdx >= BONUS_NUM_GOLDMS_TOTAL) return FALSE;
    return Bonus_IsFired(BONUS_BASE_GOLDMS + milestoneIdx);
}
int Bonus_CountFiredSetPickups(void) {
    int n = 0;
    for (int i = 0; i < BONUS_NUM_SETPIECES; i++) {
        if (Bonus_IsFired(BONUS_BASE_SETPICK + i)) n++;
    }
    return n;
}
uint64_t Bonus_GetGoldMilestoneThreshold(int idx) {
    if (idx < 0 || idx >= BONUS_NUM_GOLDMS_TOTAL) return 0;
    return g_goldMilestoneThresholds[idx];
}

/* Spoiler emission — called by Quests_WriteSpoilerFile after the per-quest section. */
extern const char* Quests_TrapTypeName(int t);
extern const char* Quests_BossLootName(int b);
extern const char* Quests_CharmName(int c);
extern const char* Quests_SetPieceName(int idx);
extern const char* Quests_UniqueName(int idx);

static void Bonus_FormatReward(int off, char* out, int outSize) {
    uint8_t rt = g_bonusState.rewardType[off];
    int32_t v  = g_bonusState.rewardValue[off];
    switch (rt) {
        case BR_GOLD:        _snprintf(out, outSize, "%d Gold",  v); break;
        case BR_XP:          _snprintf(out, outSize, "%d XP",    v); break;
        case BR_STAT:        _snprintf(out, outSize, "+5 Stat Points");  break;
        case BR_SKILL:       _snprintf(out, outSize, "+1 Skill Point");  break;
        case BR_RESETPT:     _snprintf(out, outSize, "+1 Reset Point");  break;
        case BR_TRAP:        _snprintf(out, outSize, "%s", Quests_TrapTypeName(v)); break;
        case BR_LOOT:        _snprintf(out, outSize, "Drop: %s Loot", Quests_BossLootName(v)); break;
        case BR_DROP_CHARM:  _snprintf(out, outSize, "Drop: %s", Quests_CharmName(v)); break;
        case BR_DROP_SET:    _snprintf(out, outSize, "Drop: %s (Set)", Quests_SetPieceName(v)); break;
        case BR_DROP_UNIQUE: _snprintf(out, outSize, "Drop: %s (Unique)", Quests_UniqueName(v)); break;
        default:             _snprintf(out, outSize, "?"); break;
    }
    out[outSize - 1] = 0;
}

/* Total enabled bonus-check slot count for spoiler footer grand total. */
int Bonus_GetTotalEnabledSlots(void) {
    int total = 0;
    if (g_bonusEnabled[BX_SHRINE]) total += BONUS_QUOTA_SHRINE * 3;
    if (g_bonusEnabled[BX_URN])    total += BONUS_QUOTA_URN    * 3;
    if (g_bonusEnabled[BX_BARREL]) total += BONUS_QUOTA_BARREL * 3;
    if (g_bonusEnabled[BX_CHEST])  total += BONUS_QUOTA_CHEST  * 3;
    if (g_bonusEnabled[BX_SET_PICKUP]) total += BONUS_NUM_SETPIECES;
    if (g_bonusEnabled[BX_GOLD_MS])    total += BONUS_NUM_GOLDMS_TOTAL;
    return total;
}

/* Count pre-rolled rewards across all bonus slots that belong to ENABLED categories. */
void Bonus_CountRewardsInto(int totals[10]) {
    if (!totals) return;
    if (!g_bonusState.rewardsRolled) return;
    /* Map each apId range to its category enable flag */
    static const struct { int catIdx; int base; int count; } ranges[] = {
        { BX_SHRINE, BONUS_BASE_SHRINE, BONUS_QUOTA_SHRINE * 3 },
        { BX_URN,    BONUS_BASE_URN,    BONUS_QUOTA_URN    * 3 },
        { BX_BARREL, BONUS_BASE_BARREL, BONUS_QUOTA_BARREL * 3 },
        { BX_CHEST,  BONUS_BASE_CHEST,  BONUS_QUOTA_CHEST  * 3 },
        { BX_GOLD_MS, BONUS_BASE_GOLDMS, BONUS_NUM_GOLDMS_TOTAL },
        { BX_SET_PICKUP, BONUS_BASE_SETPICK, BONUS_NUM_SETPIECES },
    };
    for (int r = 0; r < (int)(sizeof(ranges)/sizeof(ranges[0])); r++) {
        if (!g_bonusEnabled[ranges[r].catIdx]) continue;
        for (int i = 0; i < ranges[r].count; i++) {
            int apId = ranges[r].base + i;
            int off = Bonus_OffsetFromApId(apId);
            if (off < 0) continue;
            uint8_t rt = g_bonusState.rewardType[off];
            if (rt < 10) totals[rt]++;
        }
    }
}

void Bonus_AppendSpoilerToFile(FILE* f) {
    if (!f) return;
    if (!g_bonusState.rewardsRolled) return;

    int activeCount = 0;
    for (int c = 0; c < 6; c++) if (g_bonusEnabled[c]) activeCount++;
    if (activeCount == 0) return;

    fprintf(f, "\n================ Bonus Check Rewards ================\n\n");
    fprintf(f, "Each bonus check below uses the escalating-chance roll\n");
    fprintf(f, "(10%% -> 100%% per attempt, reset on hit) so you may not\n");
    fprintf(f, "hit every slot in a single playthrough. Slots fire in order;\n");
    fprintf(f, "the reward listed is what that slot will deliver when it fires.\n\n");

    static const char* diffNames[3] = {"Normal", "Nightmare", "Hell"};
    static const struct { int catIdx; int base; int quota; const char* name; } cats[] = {
        { BX_SHRINE, BONUS_BASE_SHRINE, BONUS_QUOTA_SHRINE, "Shrines" },
        { BX_URN,    BONUS_BASE_URN,    BONUS_QUOTA_URN,    "Urns" },
        { BX_BARREL, BONUS_BASE_BARREL, BONUS_QUOTA_BARREL, "Barrels" },
        { BX_CHEST,  BONUS_BASE_CHEST,  BONUS_QUOTA_CHEST,  "Chests" },
    };
    char rewBuf[96];
    for (int ci = 0; ci < 4; ci++) {
        if (!g_bonusEnabled[cats[ci].catIdx]) continue;
        fprintf(f, "  -- %s (%d / difficulty) --\n", cats[ci].name, cats[ci].quota);
        for (int diff = 0; diff < 3; diff++) {
            fprintf(f, "    [%s]\n", diffNames[diff]);
            for (int slot = 0; slot < cats[ci].quota; slot++) {
                int apId = cats[ci].base + diff * cats[ci].quota + slot;
                int off = Bonus_OffsetFromApId(apId);
                if (off < 0) continue;
                Bonus_FormatReward(off, rewBuf, sizeof(rewBuf));
                fprintf(f, "      %s #%-3d -> %s\n", cats[ci].name, slot + 1, rewBuf);
            }
        }
        fprintf(f, "\n");
    }

    if (g_bonusEnabled[BX_SET_PICKUP]) {
        fprintf(f, "  -- Set Piece Pickups (127 unique pieces) --\n");
        for (int i = 0; i < BONUS_NUM_SETPIECES; i++) {
            int apId = BONUS_BASE_SETPICK + i;
            int off = Bonus_OffsetFromApId(apId);
            if (off < 0) continue;
            Bonus_FormatReward(off, rewBuf, sizeof(rewBuf));
            const char* pieceName = Quests_SetPieceName(i);
            fprintf(f, "      %-32s -> %s\n", pieceName ? pieceName : "?", rewBuf);
        }
        fprintf(f, "\n");
    }

    if (g_bonusEnabled[BX_GOLD_MS]) {
        fprintf(f, "  -- Gold Milestones (17 lifetime gold thresholds) --\n");
        static const char* gmDiff[BONUS_NUM_GOLDMS_TOTAL] = {
            "Normal","Normal","Normal","Normal","Normal","Normal","Normal",
            "Nightmare","Nightmare","Nightmare","Nightmare","Nightmare",
            "Hell","Hell","Hell","Hell","Hell"
        };
        for (int i = 0; i < BONUS_NUM_GOLDMS_TOTAL; i++) {
            int apId = BONUS_BASE_GOLDMS + i;
            int off = Bonus_OffsetFromApId(apId);
            if (off < 0) continue;
            Bonus_FormatReward(off, rewBuf, sizeof(rewBuf));
            fprintf(f, "      %12llu g (%s) -> %s\n",
                    (unsigned long long)g_goldMilestoneThresholds[i],
                    gmDiff[i], rewBuf);
        }
        fprintf(f, "\n");
    }
}

/* Standalone reward delivery — uses the slot's pre-rolled reward (set at character creation by Bonus_PreRollAllRewards). */
static void Bonus_DeliverStandalone(int apId, const char* tag) {
    int off = Bonus_OffsetFromApId(apId);
    if (off < 0 || off >= (int)sizeof(g_bonusState.rewardType)) {
        extern int g_pendingRewardGold;
        g_pendingRewardGold += 250;  /* fallback */
        return;
    }
    uint8_t rt = g_bonusState.rewardType[off];
    int32_t v  = g_bonusState.rewardValue[off];

    /* Reuse the same pending-state globals the quest delivery path touches — they're declared `static` in earlier-included files (d2arch_skilltree.c, d2arch_quests.c) so we can read them directly in this unity-build TU without redeclaring. */
    char msg[96];
    switch (rt) {
        case BR_GOLD: {
            PendAdd(&g_serverPendingGoldLevels, 1);
            _snprintf(msg, sizeof(msg), "%s: gold", tag ? tag : "Check");
            break;
        }
        case BR_XP: {
            if (v <= 0) v = 1000;
            /* route flat XP through the pending queue: the consumer runs on the GAME thread and adds g_rewardXpExclude there, before the XP multiplier watcher samples the delta. */
            PendAdd(&g_serverPendingXpLevels, 1);
            _snprintf(msg, sizeof(msg), "%s: one level of XP", tag ? tag : "Check");
            break;
        }
        case BR_STAT:
            PendAdd(&g_serverPendingStatPts, 5);
            _snprintf(msg, sizeof(msg), "%s: +5 Stat Points", tag ? tag : "Check");
            break;
        case BR_SKILL:
            PendAdd(&g_serverPendingSkillPts, 1);
            _snprintf(msg, sizeof(msg), "%s: +1 Skill Point", tag ? tag : "Check");
            break;
        case BR_RESETPT:
            g_resetPoints++;
            _snprintf(msg, sizeof(msg), "%s: +1 Reset Point", tag ? tag : "Check");
            break;
        case BR_TRAP:
            switch (v) {
                case 0: g_pendingTrapSpawn++;  _snprintf(msg, sizeof(msg), "%s: TRAP! Monsters", tag ? tag : "Check"); break;
                case 1: g_pendingTrapSlow++;   _snprintf(msg, sizeof(msg), "%s: TRAP! Slow",     tag ? tag : "Check"); break;
                case 2: g_pendingTrapWeaken++; _snprintf(msg, sizeof(msg), "%s: TRAP! Weaken",   tag ? tag : "Check"); break;
                case 3: g_pendingTrapPoison++; _snprintf(msg, sizeof(msg), "%s: TRAP! Poison",   tag ? tag : "Check"); break;
                default: g_pendingTrapSpawn++; strcpy(msg, "TRAP!"); break;
            }
            break;
        case BR_LOOT: {
            int bossIdx = v;
            if (bossIdx < 0 || bossIdx > 4) bossIdx = 2;
            g_pendingLootDrop++;
            g_pendingLootBossId = bossIdx;
            static const char* bn[] = {"Andariel","Duriel","Mephisto","Diablo","Baal"};
            _snprintf(msg, sizeof(msg), "%s: %s loot incoming!", tag ? tag : "Check", bn[bossIdx]);
            break;
        }
        case BR_DROP_CHARM:
            Quests_QueueSpecificDrop(7 /* REWARD_DROP_CHARM */,  v, tag);
            return;  /* QueueSpecificDrop already shows its own notify */
        case BR_DROP_SET:
            Quests_QueueSpecificDrop(8 /* REWARD_DROP_SET */,    v, tag);
            return;
        case BR_DROP_UNIQUE:
            Quests_QueueSpecificDrop(9 /* REWARD_DROP_UNIQUE */, v, tag);
            return;
        default:
            g_pendingRewardGold += 250;
            _snprintf(msg, sizeof(msg), "%s: +250 gold", tag ? tag : "Check");
            break;
    }
    ShowNotify(msg);
}

/* Fire implementation — marks bitmap, emits notification, and (for AP mode) the WriteChecksFile pipeline picks it up next flush. */
static void Bonus_FireApLocation(int apId, const char* tag) {
    if (Bonus_IsFired(apId)) return;
    Bonus_MarkFired(apId);

    extern BOOL g_apConnected;
    if (g_apConnected) {
        Log("BONUS_CHECK: AP fire — %s -> apId=%d\n", tag ? tag : "?", apId);
        char notify[96];
        _snprintf(notify, sizeof(notify), "Check: %s", tag ? tag : "Bonus");
        ShowNotify(notify);
    } else {
        /* Standalone — deliver the slot's pre-rolled reward. */
        Log("BONUS_CHECK: standalone fire — %s -> apId=%d (delivering pre-rolled)\n",
            tag ? tag : "?", apId);
        Bonus_DeliverStandalone(apId, tag);
    }
    /* persist fired bit + (standalone) any pending reward counters within 250ms via PeriodicSave throttle. */
    extern void MarkStateDirty(void);
    MarkStateDirty();
}
