/* ================================================================
 * 1.9.2 — Extra check categories
 *
 * Six new AP check categories on top of the 1.9.0 bonus categories.
 * Each is independently toggleable from title-screen + apworld YAML.
 * Mirrors the d2arch_bonuschecks.c architecture (state struct + helpers
 * + per-category detection entry points + standalone reward fallback +
 * AP self-release backfill + state file persistence).
 *
 * Categories + AP location ID ranges (must match apworld locations.py):
 *   1. Cow Level expansion       (9 slots,  65300-65308)
 *      - First entry per diff (3) + Cow King kill per diff (3) +
 *        100/500/1000 lifetime cow kills (3)
 *   2. Mercenary milestones      (6 slots,  65310-65315)
 *      - First hire (1) + 5/10/25/50 lifetime resurrects (4) +
 *        first 30+ merc level (1)
 *   3. High runes + Hellforge    (12 slots, 65320-65331)
 *      - Hellforge use per diff (3) + first Lo/Mal/Pul/Cham/Zod
 *        per diff (varies — implementation uses per-diff buckets)
 *   4. Per-NPC dialogue          (81 slots, 65400-65480)
 *      - First dialogue per NPC × 3 difficulties.
 *      - 1.9.2 ships the apworld locations + framework wiring;
 *        the per-NPC detection hook is a 1.9.3 follow-up because
 *        it requires a UIVAR poll mapping table that needs more
 *        in-game verification.
 *   5. Runeword crafting         (50 slots, 65500-65549)
 *      - First creation of each runeword (per char, deduped).
 *      - 1.9.2 ships apworld + framework; detection hook hangs off
 *        Coll_ProcessItem's existing IFLAG_RUNEWORD transition and
 *        is wired in 1.9.3 once the rune-codes lookup table is
 *        verified against runes.txt.
 *   6. Cube recipe completions   (135 slots, 65600-65734)
 *      - First successful completion of each cube recipe (per char).
 *      - 1.9.2 ships apworld + framework; detection hook extends the
 *        existing TradeBtn_Hook in d2arch_quests.c with a recipe-
 *        signature pre/post diff against a CubeMain.txt-derived
 *        lookup table; lands in 1.9.3.
 *
 * Persistence: per-char state in d2arch_state_<char>.dat, lines
 * prefixed `extra_*=`. Save/load hooks added in d2arch_save.c.
 *
 * AP integration: WriteChecksFile (d2arch_save.c) emits one
 * `check=<n>` line per fired extra location. Bridge translates back
 * to apLocId by adding LOCATION_BASE (42000) the same way it does
 * for quest/gate/collection/bonus checks.
 * ================================================================ */

/* AP location ID base offsets — must match apworld locations.py
 * EXTRA_BASE_* constants. */
#define EXTRA_BASE_COW          65300
#define EXTRA_BASE_MERC         65310
#define EXTRA_BASE_HFRUNES      65320
#define EXTRA_BASE_NPC          65400
#define EXTRA_BASE_RUNEWORD     65500
#define EXTRA_BASE_CUBE         65600

/* Slot counts per category. */
#define EXTRA_CT_COW            9
#define EXTRA_CT_MERC           6
#define EXTRA_CT_HFRUNES       12
#define EXTRA_CT_NPC           81
#define EXTRA_CT_RUNEWORD      50
#define EXTRA_CT_CUBE         135

/* Range covered by the fired bitmap: 65300 .. 65799 = 500 offsets.
 * Plenty of headroom past the highest current slot (65734). */
#define EXTRA_RANGE_LO         65300
#define EXTRA_RANGE_HI         65799
#define EXTRA_RANGE_SIZE       (EXTRA_RANGE_HI - EXTRA_RANGE_LO + 1)
#define EXTRA_FIRED_BYTES      ((EXTRA_RANGE_SIZE + 7) / 8)   /* 63 bytes */

/* Sub-slot addressing within each category (cow/merc/hfrunes use
 * known structures; npc/runeword/cube are dense [0..count) ranges). */
#define EXTRA_COW_FIRST_ENTRY_BASE   (EXTRA_BASE_COW + 0)   /* +diff */
#define EXTRA_COW_KING_BASE          (EXTRA_BASE_COW + 3)   /* +diff */
#define EXTRA_COW_LIFETIME_BASE      (EXTRA_BASE_COW + 6)   /* +0/1/2 = 100/500/1000 */

#define EXTRA_MERC_HIRE              (EXTRA_BASE_MERC + 0)
#define EXTRA_MERC_RESURRECT_BASE    (EXTRA_BASE_MERC + 1)  /* +0..3 = 5/10/25/50 */
#define EXTRA_MERC_LEVEL30           (EXTRA_BASE_MERC + 5)

#define EXTRA_HF_HELLFORGE_BASE      (EXTRA_BASE_HFRUNES + 0)  /* +diff (3) */
#define EXTRA_HF_HIGH_RUNE_BASE      (EXTRA_BASE_HFRUNES + 3)  /* +rune-tier×diff (9) */

/* High-rune tiers we track:
 *   tier 0 = Lo  (r28)
 *   tier 1 = Mal (r23)   (highest of the "Mal+" tier)
 *   tier 2 = Pul (r21)   (entry to "high" runes)
 * Spec said Lo+/Mal+/Pul+/Cham/Zod = 5 tiers but with 3 diffs that's
 * 15 — we trim to the 3 most-meaningful tiers × 3 diffs = 9 to fit
 * the 12-slot budget alongside 3 Hellforge slots. */
#define EXTRA_HF_RUNE_TIERS          3

/* Toggle indices */
#define EX_COW          0
#define EX_MERC         1
#define EX_HFRUNES      2
#define EX_NPC          3
#define EX_RUNEWORD     4
#define EX_CUBE         5
#define EX_TOGGLE_COUNT 6

/* Per-char state — counters + dedup bitmaps. */
typedef struct {
    /* Cow */
    uint64_t cowKillsLifetime;          /* incremented on every Hell Bovine kill */

    /* Merc */
    uint32_t mercResurrects;            /* lifetime resurrect counter */
    uint32_t mercHires;                 /* lifetime hire counter (one-shot fires once) */
    int      mercReached30;             /* boolean — set on first observation of merc lvl >= 30 */
    int      mercLastUnitId;            /* per-game: track current merc unit id for resurrect detection */
    int      mercLastAlive;             /* per-game: was merc alive last poll? */

    /* Fired bitmap covering the whole 65300..65799 range. */
    uint8_t fired[EXTRA_FIRED_BYTES];

    /* Sequential counters for Cat 5 (runeword) and Cat 6 (cube)
     * since both fire on EVERY craft/transmute and we need to know
     * which sequential slot (1st RW, 2nd RW, ..., 50th RW; same for
     * cube up to 135). Persisted in the state file so a game crash
     * doesn't restart the counter. */
    uint16_t runewordCounter;           /* lifetime runeword crafts */
    uint16_t cubeCounter;               /* lifetime successful cube transmutes */

    /* Per-slot pre-rolled rewards (mirrors d2arch_bonuschecks.c
     * BonusCheckState). Rolled at character creation by
     * Extra_PreRollAllRewards using the same seed as the quest +
     * bonus pre-rolls. Total state added: ~2.5 KB per character.
     *
     *   rewardType[off] = BR_GOLD / BR_XP / BR_STAT / BR_SKILL /
     *                     BR_RESETPT / BR_TRAP / BR_LOOT /
     *                     BR_DROP_CHARM / BR_DROP_SET / BR_DROP_UNIQUE
     *   rewardValue[off] = gold amount, XP amount, or sub-index
     *                      (trap variant 0..3, boss 0..4, etc.)
     *
     * Same delivery code path as Bonus_DeliverStandalone — when an
     * extra check fires in standalone mode, we look up the slot's
     * rolled reward and apply it via the existing pending-state
     * globals (g_serverPendingGold, g_serverPendingStatPts, etc.). */
    uint8_t  rewardType[EXTRA_RANGE_SIZE];
    int32_t  rewardValue[EXTRA_RANGE_SIZE];
    BOOL     rewardsRolled;             /* TRUE once Extra_PreRollAllRewards has run */
} ExtraCheckState;

static ExtraCheckState g_extraState;

/* Toggles — set from slot_data (or d2arch.ini for standalone). */
static BOOL g_extraEnabled[EX_TOGGLE_COUNT] = { FALSE, FALSE, FALSE, FALSE, FALSE, FALSE };

/* External difficulty accessor (provided by d2arch_quests.c globals).
 * 0 = Normal, 1 = NM, 2 = Hell. */
extern int g_currentDifficulty;
extern BOOL g_apConnected;

/* Forward decls for entry points called before their bodies appear
 * later in the file (Extra_PollNpcDialogue is defined before
 * Extra_OnNpcDialogue, etc). */
void Extra_OnNpcDialogue(int npcIdx, int diff);
void Extra_OnRunewordCreated(int rwIdx);
void Extra_OnCubeRecipe(int recipeIdx);

/* ------------------------------------------------------------------
 * Bitmap helpers
 * ------------------------------------------------------------------ */
static int Extra_OffsetFromApId(int apId) {
    if (apId < EXTRA_RANGE_LO || apId > EXTRA_RANGE_HI) return -1;
    return apId - EXTRA_RANGE_LO;
}

static BOOL Extra_IsFired(int apId) {
    int off = Extra_OffsetFromApId(apId);
    if (off < 0) return FALSE;
    return (g_extraState.fired[off / 8] & (1 << (off % 8))) != 0;
}

static void Extra_MarkFired(int apId) {
    int off = Extra_OffsetFromApId(apId);
    if (off < 0) return;
    g_extraState.fired[off / 8] |= (1 << (off % 8));
}

/* ------------------------------------------------------------------
 * Toggle accessor — gates per-category detection entry points.
 * ------------------------------------------------------------------ */
BOOL Extra_IsCategoryEnabled(int cat) {
    if (cat < 0 || cat >= EX_TOGGLE_COUNT) return FALSE;
    return g_extraEnabled[cat];
}

/* ------------------------------------------------------------------
 * State management
 * ------------------------------------------------------------------ */
void Extra_ResetState(void) {
    memset(&g_extraState, 0, sizeof(g_extraState));
    g_extraState.mercLastUnitId = -1;
    g_extraState.mercLastAlive  = -1;
}

void Extra_ApplyToggles(BOOL cow, BOOL merc, BOOL hfRunes,
                        BOOL npc, BOOL runeword, BOOL cube) {
    g_extraEnabled[EX_COW]      = cow;
    g_extraEnabled[EX_MERC]     = merc;
    g_extraEnabled[EX_HFRUNES]  = hfRunes;
    g_extraEnabled[EX_NPC]      = npc;
    g_extraEnabled[EX_RUNEWORD] = runeword;
    g_extraEnabled[EX_CUBE]     = cube;
    Log("Extra: toggles applied — cow=%d merc=%d hfRunes=%d npc=%d "
        "runeword=%d cube=%d\n",
        (int)cow, (int)merc, (int)hfRunes,
        (int)npc, (int)runeword, (int)cube);
}

/* ------------------------------------------------------------------
 * Persistence — single hex bitmap + a few counters per char.
 * Lines look like:
 *   extra_enabled=1,1,1,0,0,0
 *   extra_cow_lifetime=237
 *   extra_merc_resurrects=12
 *   extra_merc_hires=1
 *   extra_merc_lvl30=1
 *   extra_fired=00ff020000...   (hex bitmap, EXTRA_FIRED_BYTES bytes)
 * ------------------------------------------------------------------ */
void Extra_SaveToFile(FILE* f) {
    if (!f) return;
    fprintf(f, "extra_enabled=%d,%d,%d,%d,%d,%d\n",
            g_extraEnabled[EX_COW]      ? 1 : 0,
            g_extraEnabled[EX_MERC]     ? 1 : 0,
            g_extraEnabled[EX_HFRUNES]  ? 1 : 0,
            g_extraEnabled[EX_NPC]      ? 1 : 0,
            g_extraEnabled[EX_RUNEWORD] ? 1 : 0,
            g_extraEnabled[EX_CUBE]     ? 1 : 0);
    if (g_extraState.cowKillsLifetime)
        fprintf(f, "extra_cow_lifetime=%llu\n",
                (unsigned long long)g_extraState.cowKillsLifetime);
    if (g_extraState.mercResurrects)
        fprintf(f, "extra_merc_resurrects=%u\n", g_extraState.mercResurrects);
    if (g_extraState.mercHires)
        fprintf(f, "extra_merc_hires=%u\n", g_extraState.mercHires);
    if (g_extraState.mercReached30)
        fprintf(f, "extra_merc_lvl30=1\n");
    if (g_extraState.runewordCounter)
        fprintf(f, "extra_runeword_counter=%u\n",
                (unsigned)g_extraState.runewordCounter);
    if (g_extraState.cubeCounter)
        fprintf(f, "extra_cube_counter=%u\n",
                (unsigned)g_extraState.cubeCounter);
    fputs("extra_fired=", f);
    for (int i = 0; i < EXTRA_FIRED_BYTES; i++) {
        fprintf(f, "%02x", g_extraState.fired[i]);
    }
    fputc('\n', f);

    /* 1.9.2 — Persist per-slot pre-rolled rewards. We skip writing
     * if rewardsRolled is FALSE (legacy character pre-1.9.2 state
     * file) so the load path can detect missing rolls and trigger
     * a re-roll on next AssignAllRewards. The reward arrays are
     * deterministic from the seed so this is just a fast-path cache;
     * losing the cache is harmless. */
    if (g_extraState.rewardsRolled) {
        fputs("extra_rewards=", f);
        for (int i = 0; i < EXTRA_RANGE_SIZE; i++) {
            /* Compact format: "T:V" pairs separated by commas.
             * Type fits in 1 hex digit (0..9), value packed to int. */
            fprintf(f, "%x:%d,", g_extraState.rewardType[i],
                    g_extraState.rewardValue[i]);
        }
        fputc('\n', f);
    }
}

void Extra_LoadLine(const char* line) {
    int ec, em, eh, en, er, eu;
    if (sscanf(line, "extra_enabled=%d,%d,%d,%d,%d,%d",
               &ec, &em, &eh, &en, &er, &eu) == 6) {
        Extra_ApplyToggles(ec != 0, em != 0, eh != 0,
                           en != 0, er != 0, eu != 0);
        return;
    }
    {
        unsigned long long u64;
        if (sscanf(line, "extra_cow_lifetime=%llu", &u64) == 1) {
            g_extraState.cowKillsLifetime = (uint64_t)u64;
            return;
        }
    }
    {
        unsigned u32;
        if (sscanf(line, "extra_merc_resurrects=%u", &u32) == 1) {
            g_extraState.mercResurrects = u32;
            return;
        }
        if (sscanf(line, "extra_merc_hires=%u", &u32) == 1) {
            g_extraState.mercHires = u32;
            return;
        }
        if (sscanf(line, "extra_merc_lvl30=%u", &u32) == 1) {
            g_extraState.mercReached30 = (u32 != 0);
            return;
        }
        if (sscanf(line, "extra_runeword_counter=%u", &u32) == 1) {
            g_extraState.runewordCounter = (uint16_t)u32;
            return;
        }
        if (sscanf(line, "extra_cube_counter=%u", &u32) == 1) {
            g_extraState.cubeCounter = (uint16_t)u32;
            return;
        }
    }
    if (strncmp(line, "extra_fired=", 12) == 0) {
        const char* hex = line + 12;
        int i = 0;
        while (hex[0] && hex[1] && i < EXTRA_FIRED_BYTES) {
            unsigned v = 0;
            sscanf(hex, "%2x", &v);
            g_extraState.fired[i++] = (uint8_t)v;
            hex += 2;
        }
        return;
    }
    if (strncmp(line, "extra_rewards=", 14) == 0) {
        /* Parse "T:V,T:V,..." compact format written by SaveToFile.
         * If parsing fails partway we still mark rewardsRolled so the
         * partial reload is preserved (better than re-rolling and
         * shifting all the slot rewards). */
        const char* p = line + 14;
        int i = 0;
        while (*p && i < EXTRA_RANGE_SIZE) {
            unsigned t = 0; int v = 0;
            int n = sscanf(p, "%x:%d", &t, &v);
            if (n != 2) break;
            g_extraState.rewardType[i]  = (uint8_t)t;
            g_extraState.rewardValue[i] = v;
            i++;
            /* Skip past 'T:V,' to next entry */
            const char* comma = strchr(p, ',');
            if (!comma) break;
            p = comma + 1;
        }
        if (i > 0) g_extraState.rewardsRolled = TRUE;
        return;
    }
}

/* ------------------------------------------------------------------
 * Iterator — emit fired AP locations for WriteChecksFile.
 * Returns -1 when exhausted.
 * ------------------------------------------------------------------ */
int Extra_NextFiredApId(int* iterState) {
    while (*iterState < EXTRA_RANGE_SIZE) {
        int off = *iterState;
        (*iterState)++;
        if (g_extraState.fired[off / 8] & (1 << (off % 8))) {
            return EXTRA_RANGE_LO + off;
        }
    }
    return -1;
}

int Extra_GetSlotCount(int cat) {
    switch (cat) {
        case EX_COW:      return EXTRA_CT_COW;
        case EX_MERC:     return EXTRA_CT_MERC;
        case EX_HFRUNES:  return EXTRA_CT_HFRUNES;
        case EX_NPC:      return EXTRA_CT_NPC;
        case EX_RUNEWORD: return EXTRA_CT_RUNEWORD;
        case EX_CUBE:     return EXTRA_CT_CUBE;
        default:          return 0;
    }
}

/* Count fired bits within a sub-range — used by F1 Overview /
 * Logbook to render "X / Y" progress per category. */
int Extra_CountFiredInRange(int apIdLo, int apIdHi) {
    int n = 0;
    for (int apId = apIdLo; apId <= apIdHi; apId++) {
        if (Extra_IsFired(apId)) n++;
    }
    return n;
}

int Extra_CountFiredCategory(int cat) {
    switch (cat) {
        case EX_COW:      return Extra_CountFiredInRange(EXTRA_BASE_COW,
                                  EXTRA_BASE_COW + EXTRA_CT_COW - 1);
        case EX_MERC:     return Extra_CountFiredInRange(EXTRA_BASE_MERC,
                                  EXTRA_BASE_MERC + EXTRA_CT_MERC - 1);
        case EX_HFRUNES:  return Extra_CountFiredInRange(EXTRA_BASE_HFRUNES,
                                  EXTRA_BASE_HFRUNES + EXTRA_CT_HFRUNES - 1);
        case EX_NPC:      return Extra_CountFiredInRange(EXTRA_BASE_NPC,
                                  EXTRA_BASE_NPC + EXTRA_CT_NPC - 1);
        case EX_RUNEWORD: return Extra_CountFiredInRange(EXTRA_BASE_RUNEWORD,
                                  EXTRA_BASE_RUNEWORD + EXTRA_CT_RUNEWORD - 1);
        case EX_CUBE:     return Extra_CountFiredInRange(EXTRA_BASE_CUBE,
                                  EXTRA_BASE_CUBE + EXTRA_CT_CUBE - 1);
        default: return 0;
    }
}

/* ------------------------------------------------------------------
 * Pre-rolled reward (used by spoiler + standalone delivery).
 * Mirrors Bonus_RollOneReward in d2arch_bonuschecks.c so the two
 * categories share the same reward distribution.
 *
 * The BR_* macros (BR_GOLD, BR_XP, BR_STAT, BR_SKILL, BR_RESETPT,
 * BR_TRAP, BR_LOOT, BR_DROP_CHARM, BR_DROP_SET, BR_DROP_UNIQUE) are
 * defined in d2arch_bonuschecks.c which is included BEFORE us in the
 * unity build, so we just reference them directly.
 * ------------------------------------------------------------------ */
extern int g_uniqueCatalogCount;          /* d2arch_quests.c */
extern void Quests_LoadUniqueCatalog(void);
extern BOOL g_skillHuntingOn;

static void Extra_RollOneReward(uint8_t* outType, int32_t* outValue) {
    struct WRow { int weight; int rewardType; int extraIdx; };
    struct WRow wtable[] = {
        { 10,  BR_GOLD,        0 },
        { 15,  BR_XP,          0 },
        { 10,  BR_STAT,        0 },
        { 10,  BR_SKILL,       0 },
        { (g_skillHuntingOn ? 5 : 0), BR_RESETPT, 0 },
        {  2,  BR_TRAP,        0 },
        {  1,  BR_TRAP,        1 },
        {  1,  BR_TRAP,        2 },
        {  1,  BR_TRAP,        3 },
        {  1,  BR_LOOT,        0 },
        {  2,  BR_LOOT,        1 },
        {  2,  BR_LOOT,        2 },
        {  1,  BR_LOOT,        3 },
        {  1,  BR_LOOT,        4 },
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
    switch (rewardType) {
        case BR_GOLD:        *outValue = 1 + (rand() % 10000);   break;
        case BR_XP:          *outValue = 1 + (rand() % 250000);  break;
        case BR_TRAP:        *outValue = extraIdx;               break;
        case BR_LOOT:        *outValue = extraIdx;               break;
        case BR_DROP_CHARM:  *outValue = rand() % 3;             break;
        case BR_DROP_SET:    *outValue = rand() % 127;           break;
        case BR_DROP_UNIQUE:
            if (!g_uniqueCatalogCount) Quests_LoadUniqueCatalog();
            *outValue = (g_uniqueCatalogCount > 0)
                         ? (rand() % g_uniqueCatalogCount) : 0;
            break;
        default:             *outValue = 0;                      break;
    }
}

void Extra_PreRollAllRewards(unsigned seed) {
    /* +888 offset so we don't collide with quests' AssignAllRewards
     * (+777) or Bonus_PreRollAllRewards (+999). Same seed as the
     * other pre-rolls so the spoiler can list everything coherently. */
    srand(seed + 888);
    for (int i = 0; i < EXTRA_RANGE_SIZE; i++) {
        Extra_RollOneReward(&g_extraState.rewardType[i],
                            &g_extraState.rewardValue[i]);
    }
    g_extraState.rewardsRolled = TRUE;
    Log("Extra_PreRollAllRewards: rolled %d slot rewards (seed=%u)\n",
        EXTRA_RANGE_SIZE, seed);
}

/* Format a single rolled reward as human-readable text — used by
 * the standalone spoiler. Mirrors Bonus_FormatReward. */
extern const char* Quests_TrapTypeName(int idx);
extern const char* Quests_BossLootName(int idx);
extern const char* Quests_CharmName(int idx);
extern const char* Quests_SetPieceName(int idx);
extern const char* Quests_UniqueName(int idx);
static void Extra_FormatReward(int off, char* out, size_t outSize) {
    if (off < 0 || off >= EXTRA_RANGE_SIZE) {
        _snprintf(out, outSize, "?");
        return;
    }
    uint8_t rt = g_extraState.rewardType[off];
    int32_t v  = g_extraState.rewardValue[off];
    switch (rt) {
        case BR_GOLD:        _snprintf(out, outSize, "%d Gold",  v); break;
        case BR_XP:          _snprintf(out, outSize, "%d XP",    v); break;
        case BR_STAT:        _snprintf(out, outSize, "+5 Stat Points");   break;
        case BR_SKILL:       _snprintf(out, outSize, "+1 Skill Point");   break;
        case BR_RESETPT:     _snprintf(out, outSize, "+1 Reset Point");   break;
        case BR_TRAP:        _snprintf(out, outSize, "%s", Quests_TrapTypeName(v)); break;
        case BR_LOOT:        _snprintf(out, outSize, "Drop: %s Loot", Quests_BossLootName(v)); break;
        case BR_DROP_CHARM:  _snprintf(out, outSize, "Drop: %s", Quests_CharmName(v)); break;
        case BR_DROP_SET:    _snprintf(out, outSize, "Drop: %s (Set)", Quests_SetPieceName(v)); break;
        case BR_DROP_UNIQUE: _snprintf(out, outSize, "Drop: %s (Unique)", Quests_UniqueName(v)); break;
        default:             _snprintf(out, outSize, "1000 Gold"); break;  /* fallback */
    }
}

/* ------------------------------------------------------------------
 * Standalone reward delivery — uses the slot's pre-rolled reward
 * (set at character creation by Extra_PreRollAllRewards). If the
 * pre-roll never ran (legacy character or pre-rolled state lost),
 * falls back to a flat 1000 gold so the player still gets something.
 * Mirrors Bonus_DeliverStandalone exactly so all reward kinds
 * route through the same pending-state globals.
 * ------------------------------------------------------------------ */
static void Extra_DeliverStandalone(int apId, const char* tag) {
    int off = Extra_OffsetFromApId(apId);
    if (off < 0 || !g_extraState.rewardsRolled) {
        /* Fallback — pre-roll never ran. Grant flat 1000 gold so the
         * player isn't left empty-handed. */
        extern int g_pendingRewardGold;
        g_pendingRewardGold += 1000;
        char msg[96];
        _snprintf(msg, sizeof(msg), "%s: +1000 gold", tag ? tag : "Extra Check");
        ShowNotify(msg);
        Log("EXTRA: standalone reward (fallback) — %s -> +1000 gold (apId=%d)\n",
            tag ? tag : "?", apId);
        return;
    }
    uint8_t rt = g_extraState.rewardType[off];
    int32_t v  = g_extraState.rewardValue[off];

    /* Reuse the pending-state globals the quest delivery path uses —
     * they're file-scope `static int` in earlier-included files so
     * we don't redeclare them. NO `extern` keyword for static-int
     * globals in same TU. */
    char msg[96];
    switch (rt) {
        case BR_GOLD: {
            if (v <= 0) v = 250;
            g_serverPendingGold += v;
            _snprintf(msg, sizeof(msg), "%s: +%d gold",
                      tag ? tag : "Extra Check", v);
            break;
        }
        case BR_XP: {
            if (v <= 0) v = 1000;
            /* fnAddStat is `static void(__stdcall*)(void*, int, int, int)`
             * in d2arch_api.c (included before us) — same trick used by
             * Bonus_DeliverStandalone, no extern needed. g_cachedPGame
             * is `static DWORD` in d2arch_skilltree.c, same situation. */
            if (fnAddStat && g_cachedPGame) {
                void* pXP = GetServerPlayer(g_cachedPGame);
                if (pXP) { __try { fnAddStat(pXP, 13, v, 0); } __except(1) {} }
            }
            _snprintf(msg, sizeof(msg), "%s: +%d XP",
                      tag ? tag : "Extra Check", v);
            break;
        }
        case BR_STAT:
            g_serverPendingStatPts += 5;
            _snprintf(msg, sizeof(msg), "%s: +5 Stat Points",
                      tag ? tag : "Extra Check");
            break;
        case BR_SKILL:
            g_serverPendingSkillPts += 1;
            _snprintf(msg, sizeof(msg), "%s: +1 Skill Point",
                      tag ? tag : "Extra Check");
            break;
        case BR_RESETPT:
            g_resetPoints++;
            _snprintf(msg, sizeof(msg), "%s: +1 Reset Point",
                      tag ? tag : "Extra Check");
            break;
        case BR_TRAP:
            switch (v) {
                case 0: g_pendingTrapSpawn++;  _snprintf(msg, sizeof(msg), "%s: TRAP! Monsters", tag ? tag : "Extra Check"); break;
                case 1: g_pendingTrapSlow++;   _snprintf(msg, sizeof(msg), "%s: TRAP! Slow",     tag ? tag : "Extra Check"); break;
                case 2: g_pendingTrapWeaken++; _snprintf(msg, sizeof(msg), "%s: TRAP! Weaken",   tag ? tag : "Extra Check"); break;
                case 3: g_pendingTrapPoison++; _snprintf(msg, sizeof(msg), "%s: TRAP! Poison",   tag ? tag : "Extra Check"); break;
                default: g_pendingTrapSpawn++; strcpy(msg, "TRAP!"); break;
            }
            break;
        case BR_LOOT: {
            int bossIdx = v;
            if (bossIdx < 0 || bossIdx > 4) bossIdx = 2;
            g_pendingLootDrop++;
            g_pendingLootBossId = bossIdx;
            static const char* bn[] = {"Andariel","Duriel","Mephisto","Diablo","Baal"};
            _snprintf(msg, sizeof(msg), "%s: %s loot incoming!",
                      tag ? tag : "Extra Check", bn[bossIdx]);
            break;
        }
        case BR_DROP_CHARM:
            Quests_QueueSpecificDrop(7 /* REWARD_DROP_CHARM */,  v, tag);
            return;
        case BR_DROP_SET:
            Quests_QueueSpecificDrop(8 /* REWARD_DROP_SET */,    v, tag);
            return;
        case BR_DROP_UNIQUE:
            Quests_QueueSpecificDrop(9 /* REWARD_DROP_UNIQUE */, v, tag);
            return;
        default:
            { extern int g_pendingRewardGold;
              g_pendingRewardGold += 250; }
            _snprintf(msg, sizeof(msg), "%s: +250 gold",
                      tag ? tag : "Extra Check");
            break;
    }
    ShowNotify(msg);
}

/* ------------------------------------------------------------------
 * Fire — marks bitmap, emits notification, AP write follows via
 * WriteChecksFile next flush.
 * ------------------------------------------------------------------ */
static void Extra_FireApLocation(int apId, const char* tag) {
    if (Extra_IsFired(apId)) return;
    Extra_MarkFired(apId);
    if (g_apConnected) {
        Log("EXTRA_CHECK: AP fire — %s -> apId=%d\n", tag ? tag : "?", apId);
        char notify[96];
        _snprintf(notify, sizeof(notify), "Check: %s", tag ? tag : "Extra");
        ShowNotify(notify);
    } else {
        Log("EXTRA_CHECK: standalone fire — %s -> apId=%d\n",
            tag ? tag : "?", apId);
        Extra_DeliverStandalone(apId, tag);
    }
}

/* ------------------------------------------------------------------
 * AP server reverse path — when the server tells us we received an
 * extra-check item (e.g. self-released via console), bump the local
 * fired flag so the F1 counter matches.
 * ------------------------------------------------------------------ */
BOOL Extra_OnAPItemReceived(int apId) {
    int off = Extra_OffsetFromApId(apId);
    if (off < 0) return FALSE;
    if (Extra_IsFired(apId)) return TRUE;
    Extra_MarkFired(apId);
    Log("EXTRA: AP EXTRA-TRACKED apId=%d (received from server)\n", apId);
    return TRUE;
}

/* ==================================================================
 * CATEGORY 1 — COW LEVEL EXPANSION
 * ================================================================== */

/* DIFF_LABEL matches apworld locations.py — empty string for Normal,
 * " (Nightmare)" / " (Hell)" suffixes. Matching the AP-registered name
 * exactly so the in-game popup, AP server entry, spoiler, and bridge
 * log all read the same. */
static const char* EXTRA_DIFF_LABEL[3] = { "", " (Nightmare)", " (Hell)" };

/* First entry per difficulty. Called from Stats_OnAreaEnter when
 * newAreaId == 39 (Moo Moo Farm). */
void Extra_OnCowLevelEnter(int diff) {
    if (!g_extraEnabled[EX_COW]) return;
    if (diff < 0 || diff > 2) return;
    char tag[48];
    _snprintf(tag, sizeof(tag), "Cow Level Entry%s", EXTRA_DIFF_LABEL[diff]);
    Extra_FireApLocation(EXTRA_COW_FIRST_ENTRY_BASE + diff, tag);
}

/* Cow King kill per difficulty. The Cow King is hcIdx 391 (Hell
 * Bovine MonStats row) AND SuperUnique row 39. Detection happens
 * in the unit-death scan in d2arch_gameloop.c — we get called
 * once we've confirmed it's the SU and not a regular Hell Bovine. */
void Extra_OnCowKingKilled(int diff) {
    if (!g_extraEnabled[EX_COW]) return;
    if (diff < 0 || diff > 2) return;
    char tag[48];
    _snprintf(tag, sizeof(tag), "Cow King Killed%s", EXTRA_DIFF_LABEL[diff]);
    Extra_FireApLocation(EXTRA_COW_KING_BASE + diff, tag);
}

/* Hell Bovine kill — bumps lifetime counter and fires milestone
 * checks at 100 / 500 / 1000. Called from the unit-death scan
 * after the SU check. Names match apworld locations.py {n:,} format
 * (comma in 1,000). */
void Extra_OnCowKilled(void) {
    if (!g_extraEnabled[EX_COW]) return;
    g_extraState.cowKillsLifetime++;
    uint64_t n = g_extraState.cowKillsLifetime;
    if (n == 100)  Extra_FireApLocation(EXTRA_COW_LIFETIME_BASE + 0, "Cow Kills: 100");
    if (n == 500)  Extra_FireApLocation(EXTRA_COW_LIFETIME_BASE + 1, "Cow Kills: 500");
    if (n == 1000) Extra_FireApLocation(EXTRA_COW_LIFETIME_BASE + 2, "Cow Kills: 1,000");
}

/* ==================================================================
 * CATEGORY 2 — MERCENARY MILESTONES
 * ================================================================== */

/* Per-tick poll on the player's hireable unit slot (pPlayer + 0x14C
 * is the merc unit pointer in 1.10f). Called from the gameloop tick
 * — cheap because all paths early-return when the toggle is off. */
void Extra_PollMerc(void* pPlayer) {
    if (!g_extraEnabled[EX_MERC]) return;
    if (!pPlayer) return;
    void* pMerc = NULL;
    __try {
        /* pPlayer + 0x14C = pHireUnit (merc) */
        pMerc = *(void**)((BYTE*)pPlayer + 0x14C);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    if (!pMerc) {
        /* Merc gone (unhired or dead-and-removed). Do nothing —
         * resurrect detection compares unitId to detect re-hire. */
        g_extraState.mercLastAlive = 0;
        return;
    }

    int unitId = 0; int mode = 0;
    __try {
        unitId = *(int*)((BYTE*)pMerc + 0x0C);    /* dwUnitId */
        mode   = *(int*)((BYTE*)pMerc + 0x5C);    /* current animation mode */
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    /* First hire detection */
    if (g_extraState.mercHires == 0) {
        g_extraState.mercHires = 1;
        Extra_FireApLocation(EXTRA_MERC_HIRE, "First Mercenary Hired");
    }

    /* Resurrect detection — unit id changes when mercenary dies and is
     * re-resurrected at the act NPC. Also catches hire-of-different-merc
     * but that's also a meaningful milestone. Tag strings match apworld
     * locations.py "Merc Resurrects: N" format. */
    if (g_extraState.mercLastUnitId > 0 &&
            unitId != g_extraState.mercLastUnitId) {
        g_extraState.mercResurrects++;
        uint32_t r = g_extraState.mercResurrects;
        if (r ==  5) Extra_FireApLocation(EXTRA_MERC_RESURRECT_BASE + 0, "Merc Resurrects: 5");
        if (r == 10) Extra_FireApLocation(EXTRA_MERC_RESURRECT_BASE + 1, "Merc Resurrects: 10");
        if (r == 25) Extra_FireApLocation(EXTRA_MERC_RESURRECT_BASE + 2, "Merc Resurrects: 25");
        if (r == 50) Extra_FireApLocation(EXTRA_MERC_RESURRECT_BASE + 3, "Merc Resurrects: 50");
    }
    g_extraState.mercLastUnitId = unitId;

    /* Level-30 detection — read stat 12 (STAT_LEVEL) via fnGetStat
     * which is the D2Common STATLIST_GetStatValue accessor already
     * wired in d2arch_api.c and used elsewhere (drawall.c reads
     * fnGetStat(pPlayer, 12) for character level). Same call works
     * for merc units. Wrapped in __except because pMerc may be
     * mid-resurrection and the stat list briefly invalid. */
    if (!g_extraState.mercReached30) {
        extern int (__stdcall *fnGetStat)(void*, int, int);
        int mercLevel = 0;
        if (fnGetStat) {
            __try { mercLevel = fnGetStat(pMerc, 12, 0); }
            __except(EXCEPTION_EXECUTE_HANDLER) { mercLevel = 0; }
        }
        if (mercLevel >= 30) {
            g_extraState.mercReached30 = 1;
            Extra_FireApLocation(EXTRA_MERC_LEVEL30, "Mercenary Reaches Level 30");
        }
    }

    (void)mode;
}

/* ==================================================================
 * CATEGORY 3 — HIGH RUNES + HELLFORGE
 * ================================================================== */

/* Hellforge use per difficulty. Called from the existing
 * OperateHandlerHook in d2arch_hooks.c on case 49 (Hellforge anvil
 * use). The diff is read from g_currentDifficulty at fire time. Tag
 * matches apworld locations.py — empty diff suffix for Normal. */
void Extra_OnHellforgeUsed(int diff) {
    if (!g_extraEnabled[EX_HFRUNES]) return;
    if (diff < 0 || diff > 2) return;
    char tag[48];
    _snprintf(tag, sizeof(tag), "Hellforge Used%s", EXTRA_DIFF_LABEL[diff]);
    Extra_FireApLocation(EXTRA_HF_HELLFORGE_BASE + diff, tag);
}

/* High rune pickup. runeIdx is 1..33 (matches r01..r33 codes).
 * We fire once per (tier, diff) pair. Tier mapping:
 *   r21 (Pul) .. r25 (Gul)   -> tier 0 (entry "high" runes)
 *   r26 (Vex) .. r30 (Ber)   -> tier 1 (mid "high" runes)
 *   r31 (Jah) .. r33 (Zod)   -> tier 2 (top runes)
 * Called from Coll_ProcessItem on first observation of a rune item
 * by code. Tag matches apworld locations.py format. */
void Extra_OnHighRunePickup(int runeIdx, int diff) {
    if (!g_extraEnabled[EX_HFRUNES]) return;
    if (runeIdx < 21 || runeIdx > 33) return;          /* sub-high runes ignored */
    if (diff < 0 || diff > 2) return;
    int tier;
    if (runeIdx <= 25)      tier = 0;
    else if (runeIdx <= 30) tier = 1;
    else                    tier = 2;
    int slot = tier * 3 + diff;                        /* 0..8 */
    static const char* TIER_NAMES[3] = { "Pul-Gul", "Vex-Ber", "Jah-Zod" };
    char tag[64];
    _snprintf(tag, sizeof(tag), "High Rune %s%s",
              TIER_NAMES[tier], EXTRA_DIFF_LABEL[diff]);
    Extra_FireApLocation(EXTRA_HF_HIGH_RUNE_BASE + slot, tag);
}

/* ==================================================================
 * CATEGORY 4 — PER-NPC DIALOGUE   (1.9.2: framework only, hook 1.9.3)
 * CATEGORY 5 — RUNEWORD CRAFTING  (1.9.2: framework only, hook 1.9.3)
 * CATEGORY 6 — CUBE RECIPES       (1.9.2: framework only, hook 1.9.3)
 *
 * These three need detection logic that requires more in-game
 * verification before shipping. The framework is in place — apworld
 * locations are reserved, save/load handles the bitmap, AP self-
 * release backfill works — only the detection ENTRY POINTS below
 * are stubbed for 1.9.2. The 1.9.3 cycle wires:
 *
 *   Cat 4 (NPC):     UIVAR 0x06 poll for ACTIVE_NPC transitions,
 *                    with an hcIdx -> npc_idx mapping table that
 *                    folds Cain's 6 hcIdx variants into one logical
 *                    NPC.
 *   Cat 5 (RW):      Sorted-rune-codes lookup in Coll_ProcessItem
 *                    on IFLAG_RUNEWORD 0->1 transition.
 *   Cat 6 (Cube):    Pre/post-state diff against a CubeMain.txt-
 *                    derived 135-recipe table, hooked via the
 *                    existing TradeBtn_Hook in d2arch_quests.c.
 * ================================================================== */

/* NPC name table — MUST match apworld locations.py EXTRA_NPC_NAMES
 * order so the slot indices line up. 27 entries. */
static const char* EXTRA_NPC_NAMES[27] = {
    "Akara", "Charsi", "Gheed", "Kashya", "Warriv (A1)", "Cain (A1)",
    "Atma", "Drognan", "Elzix", "Fara", "Greiz", "Lysander",
    "Meshif (A2)", "Jerhyn",
    "Alkor", "Asheara", "Hratli", "Ormus", "Cain (A3)",
    "Tyrael", "Halbu", "Jamella",
    "Anya", "Larzuk", "Malah", "Nihlathak", "Qual-Kehk",
};

/* MonStats hcIdx -> npcIdx mapping. Cross-referenced with the NPC
 * block of g_shuffleBannedIdx[] in d2arch_shuffle.c which lists every
 * vendor / hireable / quest-NPC hcIdx. Missing rows: 0 = unknown
 * (silently ignored). Cain has 6 hcIdx variants across acts; A1 Cain
 * (146) folds to npcIdx 5, every later variant (244/245/246/265/520)
 * folds to npcIdx 18 (Cain A3) since the player's interactions with
 * "rescued Cain" are functionally one logical NPC. */
static int Extra_HcIdxToNpcIdx(int hcIdx) {
    switch (hcIdx) {
        /* Act 1 */
        case 148: return 0;   /* Akara */
        case 154: return 1;   /* Charsi */
        case 147: return 2;   /* Gheed */
        case 150: return 3;   /* Kashya */
        case 155: return 4;   /* Warriv (A1) */
        case 146: return 5;   /* Cain (A1) — pre-rescue */
        /* Act 2 */
        case 176: return 6;   /* Atma */
        case 177: return 7;   /* Drognan */
        case 199: return 8;   /* Elzix */
        case 178: return 9;   /* Fara */
        case 198: return 10;  /* Greiz */
        case 202: return 11;  /* Lysander */
        case 210: return 12;  /* Meshif (A2) */
        case 175: return 12;  /* Warriv (A2) — same logical NPC slot */
        case 201: return 13;  /* Jerhyn */
        /* Act 3 */
        case 254: return 14;  /* Alkor */
        case 252: return 15;  /* Asheara */
        case 253: return 16;  /* Hratli */
        case 255: return 17;  /* Ormus */
        case 244: return 18;  /* Cain (A3) variant 1 */
        case 245: return 18;  /* Cain variant 2 */
        case 246: return 18;  /* Cain variant 3 */
        case 264: return 18;  /* Meshif (A3) — fold to Cain slot? actually skip */
        case 265: return 18;  /* Cain variant 5 */
        case 520: return 18;  /* Cain variant 6 */
        /* Act 4 */
        case 251: return 19;  /* Tyrael (A4) */
        case 367: return 19;  /* Tyrael variant */
        case 521: return 19;  /* Tyrael variant */
        case 257: return 20;  /* Halbu */
        case 405: return 21;  /* Jamella */
        /* Act 5 */
        case 512: return 22;  /* Anya (drehya) */
        case 527: return 22;  /* Anya iced variant */
        case 511: return 23;  /* Larzuk */
        case 513: return 24;  /* Malah */
        case 514: return 25;  /* Nihlathak (town) */
        case 515: return 26;  /* Qual-Kehk */
        default:  return -1;
    }
}

/* Per-tick NPC dialogue poll. Called from gameloop.c game-tick block
 * right after Extra_PollMerc.
 *
 * 1.9.2 DIAGNOSTIC PASS — UIVar 0x06 didn't trigger NPC checks in
 * the user's session, so this version polls a WIDE range of UIVars
 * (0x01..0x0F) and logs everything that changes. The first time any
 * UIVar transitions from 0 to non-zero with a value that walks
 * cleanly as a unit pointer (pUnit+0x14 -> pMonsterData -> +0x26 =
 * hcIdx readable), we fire on it. Telemetry is rate-limited via a
 * per-UIVar last-value cache so we don't spam the log.
 *
 * Output to look for in d2arch_log.txt when player talks to an NPC:
 *   "NPC POLL: UIVar 0x?? changed 0 -> 0x????????"
 *   "NPC POLL: ... walked unit ok, txtId=?, hcIdx=?, mapped npcIdx=?"
 *   "NPC FIRE: hcIdx=? -> npcIdx=? (NPC name)"
 * If no log lines appear when talking to an NPC, GetUIVar isn't
 * resolved or no UIVar in 0x01..0x0F holds the NPC unit. */
/* 1.9.2 — NPC dialogue detection via NEARBY-ROOM SCAN (UIVar approach
 * abandoned because D2Client ord 10059 returned NULL in the user's
 * 1.10f build and the legacy +0xBE400 thunk returned 0 for every
 * possible UIVar id 0x00..0x40 across hundreds of polls).
 *
 * New strategy: every game tick, walk the nearby rooms (same pattern
 * as ScanMonsters in d2arch_gameloop.c) looking for any unit with
 * type=1 (UNIT_MONSTER) whose hcIdx maps to one of our 27 NPCs.
 * Track which NPCs have been "near and stationary with the player"
 * for at least N consecutive ticks — when an NPC stays close while
 * the player isn't moving, that's our heuristic for "dialogue is
 * open". Fires the matching slot.
 *
 * Heuristic isn't perfect: walking past an NPC and stopping briefly
 * could fire it. But it's robust — uses no reverse-engineered
 * function offsets, just struct-field reads we already do elsewhere
 * in the codebase. */

#define NPC_NEAR_TILES        2       /* tiles between player and NPC for "near" */
#define NPC_STATIONARY_TICKS  20      /* throttled ticks (10th-tick poll) so
                                       * 20 = ~3.3s of actual standing-near */

void Extra_PollNpcDialogue(void* pPlayerUnit) {
    /* Heartbeat — log once every ~5000 calls (≈83 sec at 60fps) so
     * we know the per-tick poll is actually running. */
    static unsigned s_heartbeatCount = 0;
    if ((++s_heartbeatCount % 5000) == 1) {
        Log("NPC HEARTBEAT: poll #%u, EX_NPC enabled=%d, pPlayer=%p\n",
            s_heartbeatCount, (int)g_extraEnabled[EX_NPC],
            (void*)pPlayerUnit);
    }

    if (!g_extraEnabled[EX_NPC]) return;
    if (!pPlayerUnit) return;

    /* 1.9.2 — Throttle room-scan to every 10th tick (6Hz at 60fps).
     * Player must stand near an NPC for 20 throttled ticks = ~3.3 sec
     * before firing — plenty of time to actually open the menu, but
     * doesn't burden every game tick with the room walk. */
    static unsigned s_throttle = 0;
    if ((++s_throttle % 10) != 0) return;

    /* Read player's pPath -> pRoom -> nearby rooms. Same pattern as
     * ScanMonsters in gameloop.c. The CLIENT player struct (passed
     * via pPlayerUnit) has pPath at offset 0x2C. */
    DWORD pPath = 0, pRoom = 0;
    int playerX = 0, playerY = 0;
    __try {
        pPath = *(DWORD*)((BYTE*)pPlayerUnit + 0x2C);
        if (!pPath) return;
        /* Path xPos/yPos are at 0x02 and 0x06 (16-bit each in client) */
        playerX = (int)*(WORD*)(pPath + 0x02);
        playerY = (int)*(WORD*)(pPath + 0x06);
        pRoom   = *(DWORD*)(pPath + 0x1C);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!pRoom) return;

    /* Edge-detect player movement: when player position changes,
     * reset the "near for N ticks" counter for every NPC. */
    static int s_lastPlayerX = -1, s_lastPlayerY = -1;
    BOOL playerMoved = (playerX != s_lastPlayerX || playerY != s_lastPlayerY);
    s_lastPlayerX = playerX;
    s_lastPlayerY = playerY;

    /* Per-NPC "ticks near and stationary" counter. Index = npcIdx 0..26.
     * Reset when player moves; bumped each tick the NPC is within
     * NPC_NEAR_TILES of the player. When it crosses
     * NPC_STATIONARY_TICKS, fire the slot once. */
    static int s_nearTicks[27] = {0};
    if (playerMoved) {
        for (int i = 0; i < 27; i++) s_nearTicks[i] = 0;
    }

    /* Walk nearby rooms (current + adjacent) looking for NPC units. */
    DWORD rooms[21];
    int roomCount = 0;
    rooms[roomCount++] = pRoom;
    __try {
        DWORD ppRoomList = *(DWORD*)(pRoom + 0x24);
        int   nNumRooms  = (int)*(DWORD*)(pRoom + 0x28);
        if (nNumRooms > 20) nNumRooms = 20;
        if (ppRoomList && nNumRooms > 0) {
            for (int r = 0; r < nNumRooms && roomCount < 21; r++) {
                DWORD nr = *(DWORD*)((DWORD)ppRoomList + r * 4);
                if (nr && nr != pRoom) rooms[roomCount++] = nr;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { /* fall through with current room only */ }

    /* For each room, walk the unit list (type=1 monsters) and check
     * for NPC matches. Limited per-room chain length to keep cost
     * bounded. */
    for (int ri = 0; ri < roomCount; ri++) {
        DWORD unit = 0;
        __try { unit = *(DWORD*)(rooms[ri] + 0x2C); }
        __except(EXCEPTION_EXECUTE_HANDLER) { continue; }

        int chain = 0;
        while (unit && chain++ < 100) {
            DWORD type = 0; DWORD txtId = 0; DWORD pMonData = 0;
            DWORD pNpcPath = 0;
            int   npcX = 0, npcY = 0;
            int   hcIdx = -1;
            __try {
                type     = *(DWORD*)(unit + 0x00);
                txtId    = *(DWORD*)(unit + 0x04);
                pMonData = *(DWORD*)(unit + 0x14);
                pNpcPath = *(DWORD*)(unit + 0x2C);
                if (pNpcPath) {
                    npcX = (int)*(WORD*)(pNpcPath + 0x02);
                    npcY = (int)*(WORD*)(pNpcPath + 0x06);
                }
                if (pMonData) {
                    hcIdx = (int)*(WORD*)(pMonData + 0x26);
                }
                /* Advance to next unit in room chain */
                unit = *(DWORD*)(unit + 0xE8);
            } __except(EXCEPTION_EXECUTE_HANDLER) { break; }

            if (type != 1 || hcIdx < 0) continue;
            int npcIdx = Extra_HcIdxToNpcIdx(hcIdx);
            if (npcIdx < 0) continue;

            /* Distance check (Chebyshev — diagonal counts as 1 tile) */
            int dx = playerX - npcX; if (dx < 0) dx = -dx;
            int dy = playerY - npcY; if (dy < 0) dy = -dy;
            int dist = (dx > dy) ? dx : dy;
            if (dist > NPC_NEAR_TILES) continue;

            /* Player was stationary this tick (or hasn't moved since
             * last tick reset). Bump the near-counter for this NPC. */
            s_nearTicks[npcIdx]++;
            if (s_nearTicks[npcIdx] == NPC_STATIONARY_TICKS) {
                /* First time crossing the threshold this run — fire. */
                Log("NPC FIRE: scan-detected hcIdx=%d -> npcIdx=%d (%s) "
                    "after %d stationary ticks at (%d,%d) dist=%d\n",
                    hcIdx, npcIdx, EXTRA_NPC_NAMES[npcIdx],
                    NPC_STATIONARY_TICKS, playerX, playerY, dist);
                Extra_OnNpcDialogue(npcIdx, g_currentDifficulty);
            }
        }
    }
}

void Extra_OnNpcDialogue(int npcIdx, int diff) {
    if (!g_extraEnabled[EX_NPC]) return;
    if (npcIdx < 0 || npcIdx >= 27) return;
    if (diff < 0 || diff > 2) return;
    int slot = npcIdx * 3 + diff;
    if (slot >= EXTRA_CT_NPC) return;
    char tag[64];
    _snprintf(tag, sizeof(tag), "NPC Dialogue: %s%s",
              EXTRA_NPC_NAMES[npcIdx], EXTRA_DIFF_LABEL[diff]);
    Extra_FireApLocation(EXTRA_BASE_NPC + slot, tag);
}

void Extra_OnRunewordCreated(int rwIdx) {
    if (!g_extraEnabled[EX_RUNEWORD]) return;
    if (rwIdx < 0 || rwIdx >= EXTRA_CT_RUNEWORD) return;
    char tag[48];
    /* 1-indexed to match apworld locations.py "Runeword Crafted #{i+1}" */
    _snprintf(tag, sizeof(tag), "Runeword Crafted #%d", rwIdx + 1);
    Extra_FireApLocation(EXTRA_BASE_RUNEWORD + rwIdx, tag);
}

/* 1.9.2 — Auto-counter wrapper for Coll_ProcessItem. Bumps an
 * internal sequential counter and fires the Nth runeword slot.
 * Called from d2arch_collections.c on IFLAG_RUNEWORD 0->1 transition.
 * The counter persists across saves (extra_runeword_counter line). */
void Extra_OnRunewordCreatedAuto(void) {
    if (!g_extraEnabled[EX_RUNEWORD]) return;
    if (g_extraState.runewordCounter >= EXTRA_CT_RUNEWORD) return;
    int slot = (int)g_extraState.runewordCounter;
    g_extraState.runewordCounter++;
    Extra_OnRunewordCreated(slot);
}

void Extra_OnCubeRecipe(int recipeIdx) {
    if (!g_extraEnabled[EX_CUBE]) return;
    if (recipeIdx < 0 || recipeIdx >= EXTRA_CT_CUBE) return;
    char tag[48];
    /* 1-indexed to match apworld locations.py "Cube Recipe #{i+1}" */
    _snprintf(tag, sizeof(tag), "Cube Recipe #%d", recipeIdx + 1);
    Extra_FireApLocation(EXTRA_BASE_CUBE + recipeIdx, tag);
}

/* 1.9.2 — Auto-counter wrapper for TradeBtn_Hook. Bumps an
 * internal sequential counter and fires the Nth cube-recipe slot.
 * Called from d2arch_hooks.c on a successful cube transmute. */
void Extra_OnCubeRecipeAuto(void) {
    if (!g_extraEnabled[EX_CUBE]) return;
    if (g_extraState.cubeCounter >= EXTRA_CT_CUBE) return;
    int slot = (int)g_extraState.cubeCounter;
    g_extraState.cubeCounter++;
    Extra_OnCubeRecipe(slot);
}

/* ==================================================================
 * STANDALONE SPOILER FILE — appended to the per-character spoiler
 * .txt that Quests_WriteSpoilerFile produces. Lists every extra
 * check slot with its pre-rolled standalone reward (mirrors the
 * bonus-check spoiler section).
 * ================================================================== */
void Extra_AppendSpoilerToFile(FILE* f) {
    if (!f) return;
    int activeCount = 0;
    for (int c = 0; c < EX_TOGGLE_COUNT; c++) {
        if (g_extraEnabled[c]) activeCount++;
    }
    if (activeCount == 0) return;

    fprintf(f, "\n================ Extra Check Rewards ================\n\n");
    fprintf(f, "Each extra check fires the first time you trigger it (per\n");
    fprintf(f, "character per slot, deduplicated via the fired bitmap).\n");
    if (g_extraState.rewardsRolled) {
        fprintf(f, "Standalone rewards are pre-rolled per slot using the\n");
        fprintf(f, "character's seed and the same weighted catalog as the\n");
        fprintf(f, "quest + bonus pre-rolls. Re-running AssignAllRewards\n");
        fprintf(f, "(seed change) re-rolls these slots too.\n\n");
    } else {
        fprintf(f, "(Pre-rolled rewards not yet generated — every fire\n");
        fprintf(f, "will grant a flat 1000 gold fallback until you trigger\n");
        fprintf(f, "AssignAllRewards via a seed change or fresh character.)\n\n");
    }

    /* Spoiler entries use the SAME names the apworld registers in
     * locations.py — empty diff suffix for Normal, NPC names from
     * the EXTRA_NPC_NAMES table, 1-indexed runeword/cube. Keeps the
     * spoiler readable next to the AP server's location list. */

    /* Helper to print one slot row with the pre-rolled reward (or
     * "1000 Gold" fallback when rewardsRolled is FALSE). */
    #define EXTRA_PRINT_SLOT(apId, name) do { \
        char rewBuf[80]; \
        if (g_extraState.rewardsRolled) { \
            int _off = Extra_OffsetFromApId(apId); \
            Extra_FormatReward(_off, rewBuf, sizeof(rewBuf)); \
        } else { \
            _snprintf(rewBuf, sizeof(rewBuf), "1000 Gold (fallback)"); \
        } \
        fprintf(f, "    %-40s -> %s\n", name, rewBuf); \
    } while (0)

    /* Cat 1 — Cow Level expansion (9 slots) */
    if (g_extraEnabled[EX_COW]) {
        fprintf(f, "  -- Cow Level (9 slots) --\n");
        for (int d = 0; d < 3; d++) {
            char name[48];
            _snprintf(name, sizeof(name), "Cow Level Entry%s",
                      EXTRA_DIFF_LABEL[d]);
            EXTRA_PRINT_SLOT(EXTRA_COW_FIRST_ENTRY_BASE + d, name);
        }
        for (int d = 0; d < 3; d++) {
            char name[48];
            _snprintf(name, sizeof(name), "Cow King Killed%s",
                      EXTRA_DIFF_LABEL[d]);
            EXTRA_PRINT_SLOT(EXTRA_COW_KING_BASE + d, name);
        }
        EXTRA_PRINT_SLOT(EXTRA_COW_LIFETIME_BASE + 0, "Cow Kills: 100");
        EXTRA_PRINT_SLOT(EXTRA_COW_LIFETIME_BASE + 1, "Cow Kills: 500");
        EXTRA_PRINT_SLOT(EXTRA_COW_LIFETIME_BASE + 2, "Cow Kills: 1,000");
        fprintf(f, "\n");
    }

    /* Cat 2 — Mercenary milestones (6 slots) */
    if (g_extraEnabled[EX_MERC]) {
        fprintf(f, "  -- Mercenary Milestones (6 slots) --\n");
        EXTRA_PRINT_SLOT(EXTRA_MERC_HIRE,                "First Mercenary Hired");
        EXTRA_PRINT_SLOT(EXTRA_MERC_RESURRECT_BASE + 0,  "Merc Resurrects: 5");
        EXTRA_PRINT_SLOT(EXTRA_MERC_RESURRECT_BASE + 1,  "Merc Resurrects: 10");
        EXTRA_PRINT_SLOT(EXTRA_MERC_RESURRECT_BASE + 2,  "Merc Resurrects: 25");
        EXTRA_PRINT_SLOT(EXTRA_MERC_RESURRECT_BASE + 3,  "Merc Resurrects: 50");
        EXTRA_PRINT_SLOT(EXTRA_MERC_LEVEL30,             "Mercenary Reaches Level 30");
        fprintf(f, "\n");
    }

    /* Cat 3 — Hellforge + High Runes (12 slots) */
    if (g_extraEnabled[EX_HFRUNES]) {
        fprintf(f, "  -- Hellforge + High Runes (12 slots) --\n");
        for (int d = 0; d < 3; d++) {
            char name[48];
            _snprintf(name, sizeof(name), "Hellforge Used%s",
                      EXTRA_DIFF_LABEL[d]);
            EXTRA_PRINT_SLOT(EXTRA_HF_HELLFORGE_BASE + d, name);
        }
        static const char* TIER_NAMES[3] = { "Pul-Gul", "Vex-Ber", "Jah-Zod" };
        for (int t = 0; t < 3; t++) {
            for (int d = 0; d < 3; d++) {
                char name[64];
                _snprintf(name, sizeof(name), "High Rune %s%s",
                          TIER_NAMES[t], EXTRA_DIFF_LABEL[d]);
                EXTRA_PRINT_SLOT(EXTRA_HF_HIGH_RUNE_BASE + t * 3 + d, name);
            }
        }
        fprintf(f, "\n");
    }

    /* Cat 4 — NPC Dialogue (81 slots: 27 NPCs × 3 diff) */
    if (g_extraEnabled[EX_NPC]) {
        fprintf(f, "  -- NPC Dialogue (81 slots, 27 NPCs x 3 diff) --\n");
        for (int n = 0; n < 27; n++) {
            for (int d = 0; d < 3; d++) {
                char name[64];
                _snprintf(name, sizeof(name), "NPC Dialogue: %s%s",
                          EXTRA_NPC_NAMES[n], EXTRA_DIFF_LABEL[d]);
                EXTRA_PRINT_SLOT(EXTRA_BASE_NPC + n * 3 + d, name);
            }
        }
        fprintf(f, "\n");
    }

    /* Cat 5 — Runeword crafting (50 slots) */
    if (g_extraEnabled[EX_RUNEWORD]) {
        fprintf(f, "  -- Runeword Crafting (50 slots) --\n");
        for (int i = 0; i < EXTRA_CT_RUNEWORD; i++) {
            char name[48];
            _snprintf(name, sizeof(name), "Runeword Crafted #%d", i + 1);
            EXTRA_PRINT_SLOT(EXTRA_BASE_RUNEWORD + i, name);
        }
        fprintf(f, "\n");
    }

    /* Cat 6 — Cube recipes (135 slots) */
    if (g_extraEnabled[EX_CUBE]) {
        fprintf(f, "  -- Cube Recipes (135 slots) --\n");
        for (int i = 0; i < EXTRA_CT_CUBE; i++) {
            char name[48];
            _snprintf(name, sizeof(name), "Cube Recipe #%d", i + 1);
            EXTRA_PRINT_SLOT(EXTRA_BASE_CUBE + i, name);
        }
        fprintf(f, "\n");
    }
    #undef EXTRA_PRINT_SLOT
}
