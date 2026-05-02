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
} ExtraCheckState;

static ExtraCheckState g_extraState;

/* Toggles — set from slot_data (or d2arch.ini for standalone). */
static BOOL g_extraEnabled[EX_TOGGLE_COUNT] = { FALSE, FALSE, FALSE, FALSE, FALSE, FALSE };

/* External difficulty accessor (provided by d2arch_quests.c globals).
 * 0 = Normal, 1 = NM, 2 = Hell. */
extern int g_currentDifficulty;
extern BOOL g_apConnected;

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
    fputs("extra_fired=", f);
    for (int i = 0; i < EXTRA_FIRED_BYTES; i++) {
        fprintf(f, "%02x", g_extraState.fired[i]);
    }
    fputc('\n', f);
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
 * Standalone reward delivery — for AP-disconnected play. Each fire
 * grants a flat 1000 gold for now. (Per-slot pre-rolled rewards
 * mirroring the bonus_check pipeline are a 1.9.3 follow-up; the
 * spec said "reuse Bonus_DeliverStandalone" but doing that requires
 * pre-rolling 500 reward slots, which adds 8 KB to the per-char
 * state file — skipped for the 1.9.2 ship.) */
static void Extra_DeliverStandalone(int apId, const char* tag) {
    /* In the unity build d2arch_skilltree.c declares g_pendingRewardGold
     * as `static int` at file-scope BEFORE this TU sees us; an `extern
     * int` redeclaration here resolves to the same internal-linkage
     * variable. Same trick d2arch_bonuschecks.c uses (line 723). The
     * `volatile` qualifier on the previous version mismatched the
     * underlying declaration and tripped C2373 — keep it plain. */
    extern int g_pendingRewardGold;
    g_pendingRewardGold += 1000;
    Log("EXTRA: standalone reward — %s -> +1000 gold (apId=%d)\n",
        tag ? tag : "?", apId);
    (void)apId;
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
    Log("EXTRA: AP BONUS-TRACKED apId=%d (received from server)\n", apId);
    return TRUE;
}

/* ==================================================================
 * CATEGORY 1 — COW LEVEL EXPANSION
 * ================================================================== */

/* First entry per difficulty. Called from Stats_OnAreaEnter when
 * newAreaId == 39 (Moo Moo Farm). */
void Extra_OnCowLevelEnter(int diff) {
    if (!g_extraEnabled[EX_COW]) return;
    if (diff < 0 || diff > 2) return;
    char tag[48];
    static const char* DIFF_NAMES[3] = { "Normal", "Nightmare", "Hell" };
    _snprintf(tag, sizeof(tag), "Cow Level Entry (%s)", DIFF_NAMES[diff]);
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
    static const char* DIFF_NAMES[3] = { "Normal", "Nightmare", "Hell" };
    _snprintf(tag, sizeof(tag), "Cow King Killed (%s)", DIFF_NAMES[diff]);
    Extra_FireApLocation(EXTRA_COW_KING_BASE + diff, tag);
}

/* Hell Bovine kill — bumps lifetime counter and fires milestone
 * checks at 100 / 500 / 1000. Called from the unit-death scan
 * after the SU check. */
void Extra_OnCowKilled(void) {
    if (!g_extraEnabled[EX_COW]) return;
    g_extraState.cowKillsLifetime++;
    uint64_t n = g_extraState.cowKillsLifetime;
    if (n == 100)  Extra_FireApLocation(EXTRA_COW_LIFETIME_BASE + 0, "100 Cows Killed");
    if (n == 500)  Extra_FireApLocation(EXTRA_COW_LIFETIME_BASE + 1, "500 Cows Killed");
    if (n == 1000) Extra_FireApLocation(EXTRA_COW_LIFETIME_BASE + 2, "1000 Cows Killed");
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

    int unitId = 0; int mode = 0; int level = 0;
    __try {
        unitId = *(int*)((BYTE*)pMerc + 0x0C);    /* dwUnitId */
        mode   = *(int*)((BYTE*)pMerc + 0x5C);    /* current animation mode */
        /* 1.9.2 fix: previously read pStatList from offset 0x5C which
         * is the animation mode (already loaded above), not the stat
         * list. The bogus pointer was masked by __except so it just
         * silently returned 0 every tick — the level-30 detection
         * never tripped, but reading garbage as a pointer is ugly.
         * StatList is at offset 0x5C+8 = 0x64 in 1.10f, but the
         * level-stat-12 lookup needs D2Common ord 10527 anyway and
         * is deferred to 1.9.3. Skip the read entirely until then. */
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    /* First hire detection */
    if (g_extraState.mercHires == 0) {
        g_extraState.mercHires = 1;
        Extra_FireApLocation(EXTRA_MERC_HIRE, "First Mercenary Hired");
    }

    /* Resurrect detection — unit id changes when mercenary dies and is
     * re-resurrected at the act NPC. Also catches hire-of-different-merc
     * but that's also a meaningful milestone. */
    if (g_extraState.mercLastUnitId > 0 &&
            unitId != g_extraState.mercLastUnitId) {
        g_extraState.mercResurrects++;
        uint32_t r = g_extraState.mercResurrects;
        if (r ==  5) Extra_FireApLocation(EXTRA_MERC_RESURRECT_BASE + 0,  "5 Merc Resurrects");
        if (r == 10) Extra_FireApLocation(EXTRA_MERC_RESURRECT_BASE + 1, "10 Merc Resurrects");
        if (r == 25) Extra_FireApLocation(EXTRA_MERC_RESURRECT_BASE + 2, "25 Merc Resurrects");
        if (r == 50) Extra_FireApLocation(EXTRA_MERC_RESURRECT_BASE + 3, "50 Merc Resurrects");
    }
    g_extraState.mercLastUnitId = unitId;

    /* Level-30 detection — gated off until 1.9.3 wires D2Common
     * ord 10527 (STATLIST_GetStatValue) for stat 12 (level). The
     * direct-field-read approach is unreliable across builds, and
     * reading a wrong offset as a stat-list pointer is what created
     * the harmless-but-ugly bug fixed above. Keep the slot reachable
     * via AP /release until then. */
    (void)level;

    (void)mode;
}

/* ==================================================================
 * CATEGORY 3 — HIGH RUNES + HELLFORGE
 * ================================================================== */

/* Hellforge use per difficulty. Called from the existing
 * OperateHandlerHook in d2arch_hooks.c on case 49 (Hellforge anvil
 * use). The diff is read from g_currentDifficulty at fire time. */
void Extra_OnHellforgeUsed(int diff) {
    if (!g_extraEnabled[EX_HFRUNES]) return;
    if (diff < 0 || diff > 2) return;
    char tag[48];
    static const char* DIFF_NAMES[3] = { "Normal", "Nightmare", "Hell" };
    _snprintf(tag, sizeof(tag), "Hellforge Used (%s)", DIFF_NAMES[diff]);
    Extra_FireApLocation(EXTRA_HF_HELLFORGE_BASE + diff, tag);
}

/* High rune pickup. runeIdx is 1..33 (matches r01..r33 codes).
 * We fire once per (tier, diff) pair. Tier mapping:
 *   r21 (Pul) .. r25 (Gul)   -> tier 0 (entry "high" runes)
 *   r26 (Vex) .. r30 (Ber)   -> tier 1 (mid "high" runes)
 *   r31 (Jah) .. r33 (Zod)   -> tier 2 (top runes)
 * Called from Coll_ProcessItem on first observation of a rune item
 * by code. */
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
    static const char* DIFF_NAMES[3] = { "Normal", "Nightmare", "Hell" };
    char tag[64];
    _snprintf(tag, sizeof(tag), "High Rune %s (%s)",
              TIER_NAMES[tier], DIFF_NAMES[diff]);
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

void Extra_OnNpcDialogue(int npcIdx, int diff) {
    if (!g_extraEnabled[EX_NPC]) return;
    if (npcIdx < 0 || npcIdx >= 27) return;
    if (diff < 0 || diff > 2) return;
    int slot = npcIdx * 3 + diff;
    if (slot >= EXTRA_CT_NPC) return;
    char tag[48];
    static const char* DIFF_NAMES[3] = { "Normal", "Nightmare", "Hell" };
    _snprintf(tag, sizeof(tag), "NPC Dialogue #%d (%s)", npcIdx, DIFF_NAMES[diff]);
    Extra_FireApLocation(EXTRA_BASE_NPC + slot, tag);
}

void Extra_OnRunewordCreated(int rwIdx) {
    if (!g_extraEnabled[EX_RUNEWORD]) return;
    if (rwIdx < 0 || rwIdx >= EXTRA_CT_RUNEWORD) return;
    char tag[48];
    _snprintf(tag, sizeof(tag), "Runeword Crafted #%d", rwIdx);
    Extra_FireApLocation(EXTRA_BASE_RUNEWORD + rwIdx, tag);
}

void Extra_OnCubeRecipe(int recipeIdx) {
    if (!g_extraEnabled[EX_CUBE]) return;
    if (recipeIdx < 0 || recipeIdx >= EXTRA_CT_CUBE) return;
    char tag[48];
    _snprintf(tag, sizeof(tag), "Cube Recipe #%d", recipeIdx);
    Extra_FireApLocation(EXTRA_BASE_CUBE + recipeIdx, tag);
}

/* ==================================================================
 * STANDALONE SPOILER FILE — appended to the per-character spoiler
 * .txt that Quests_WriteSpoilerFile produces. Lists every extra
 * check slot with its standalone reward (flat 1000 gold for 1.9.2).
 *
 * Mirrors Bonus_AppendSpoilerToFile but simpler since the 1.9.2
 * extras don't pre-roll per-slot rewards (every fire grants a flat
 * 1000 gold). Per-slot pre-rolling is a 1.9.3 follow-up — when that
 * lands this function will switch to formatting the rolled reward
 * the same way Bonus_FormatReward does.
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
    fprintf(f, "Standalone reward is a flat 1000 gold per fire — per-slot\n");
    fprintf(f, "pre-rolled rewards (matching the bonus-check pipeline) are\n");
    fprintf(f, "a 1.9.3 follow-up.\n\n");

    static const char* DIFF_NAMES[3] = { "Normal", "Nightmare", "Hell" };

    /* Cat 1 — Cow Level expansion (9 slots) */
    if (g_extraEnabled[EX_COW]) {
        fprintf(f, "  -- Cow Level (9 slots) --\n");
        for (int d = 0; d < 3; d++) {
            fprintf(f, "    Cow Level Entry (%s)        -> 1000 Gold\n",
                    DIFF_NAMES[d]);
        }
        for (int d = 0; d < 3; d++) {
            fprintf(f, "    Cow King Killed (%s)        -> 1000 Gold\n",
                    DIFF_NAMES[d]);
        }
        static const int milestones[3] = { 100, 500, 1000 };
        for (int i = 0; i < 3; i++) {
            fprintf(f, "    %d Cows Killed                -> 1000 Gold\n",
                    milestones[i]);
        }
        fprintf(f, "\n");
    }

    /* Cat 2 — Mercenary milestones (6 slots) */
    if (g_extraEnabled[EX_MERC]) {
        fprintf(f, "  -- Mercenary Milestones (6 slots) --\n");
        fprintf(f, "    First Mercenary Hired         -> 1000 Gold\n");
        static const int rezCounts[4] = { 5, 10, 25, 50 };
        for (int i = 0; i < 4; i++) {
            fprintf(f, "    %d Merc Resurrects             -> 1000 Gold\n",
                    rezCounts[i]);
        }
        fprintf(f, "    Merc Reaches Level 30         -> 1000 Gold\n");
        fprintf(f, "\n");
    }

    /* Cat 3 — Hellforge + High Runes (12 slots) */
    if (g_extraEnabled[EX_HFRUNES]) {
        fprintf(f, "  -- Hellforge + High Runes (12 slots) --\n");
        for (int d = 0; d < 3; d++) {
            fprintf(f, "    Hellforge Used (%s)         -> 1000 Gold\n",
                    DIFF_NAMES[d]);
        }
        static const char* TIER_NAMES[3] = { "Pul-Gul", "Vex-Ber", "Jah-Zod" };
        for (int t = 0; t < 3; t++) {
            for (int d = 0; d < 3; d++) {
                fprintf(f, "    High Rune %s (%s)       -> 1000 Gold\n",
                        TIER_NAMES[t], DIFF_NAMES[d]);
            }
        }
        fprintf(f, "\n");
    }

    /* Cat 4 — NPC Dialogue (81 slots: 27 NPCs × 3 diff) */
    if (g_extraEnabled[EX_NPC]) {
        fprintf(f, "  -- NPC Dialogue (81 slots, 27 NPCs x 3 diff) --\n");
        fprintf(f, "    1.9.2: framework ships, in-game detection lands in 1.9.3.\n");
        fprintf(f, "    Until then, slots can be unlocked via AP /release.\n");
        for (int n = 0; n < 27; n++) {
            for (int d = 0; d < 3; d++) {
                fprintf(f, "    NPC #%-2d (%s)              -> 1000 Gold\n",
                        n + 1, DIFF_NAMES[d]);
            }
        }
        fprintf(f, "\n");
    }

    /* Cat 5 — Runeword crafting (50 slots) */
    if (g_extraEnabled[EX_RUNEWORD]) {
        fprintf(f, "  -- Runeword Crafting (50 slots) --\n");
        fprintf(f, "    1.9.2: framework ships, in-game detection lands in 1.9.3.\n");
        for (int i = 0; i < EXTRA_CT_RUNEWORD; i++) {
            fprintf(f, "    Runeword #%-3d                -> 1000 Gold\n", i + 1);
        }
        fprintf(f, "\n");
    }

    /* Cat 6 — Cube recipes (135 slots) */
    if (g_extraEnabled[EX_CUBE]) {
        fprintf(f, "  -- Cube Recipes (135 slots) --\n");
        fprintf(f, "    1.9.2: framework ships, in-game detection lands in 1.9.3.\n");
        for (int i = 0; i < EXTRA_CT_CUBE; i++) {
            fprintf(f, "    Cube Recipe #%-3d             -> 1000 Gold\n", i + 1);
        }
        fprintf(f, "\n");
    }
}
