/* ================================================================
 * 1.9.2 — Custom Goal (AP-side only)
 *
 * When the apworld YAML sets goal=4 (custom), the AP slot_data ships
 * two extra keys:
 *   custom_goal_gold_target = uint64 (lifetime gold required, 0=skip)
 *   custom_goal_targets_csv = comma-separated list of target tokens
 *
 * Each token maps to one entry in the CGT_* enum below. We track
 * which targets are REQUIRED (parsed from CSV) and which are
 * FIRED (set at runtime by detection hooks scattered across the
 * codebase). When required == fired AND lifetime gold >= target,
 * CustomGoal_IsComplete() returns TRUE — the existing AP victory
 * path picks that up via g_apGoalComplete in d2arch_save.c /
 * d2arch_gameloop.c.
 *
 * Standalone players never set goal=4 (no UI for it on title screen);
 * if they somehow do, the DLL falls back to Full Normal because the
 * required bitmap stays empty.
 * ================================================================ */

/* Target enum — must stay in sync with the apworld OptionSet's
 * valid_keys list. Index = bit position in the required/fired
 * bitmaps. The CSV parser uses the string<->index table below. */
typedef enum {
    /* Act bosses × difficulty */
    CGT_KILL_ANDARIEL_NORMAL = 0,
    CGT_KILL_ANDARIEL_NM,
    CGT_KILL_ANDARIEL_HELL,
    CGT_KILL_DURIEL_NORMAL,
    CGT_KILL_DURIEL_NM,
    CGT_KILL_DURIEL_HELL,
    CGT_KILL_MEPHISTO_NORMAL,
    CGT_KILL_MEPHISTO_NM,
    CGT_KILL_MEPHISTO_HELL,
    CGT_KILL_DIABLO_NORMAL,
    CGT_KILL_DIABLO_NM,
    CGT_KILL_DIABLO_HELL,
    CGT_KILL_BAAL_NORMAL,
    CGT_KILL_BAAL_NM,
    CGT_KILL_BAAL_HELL,
    /* Cow King */
    CGT_KILL_COW_KING_NORMAL,
    CGT_KILL_COW_KING_NM,
    CGT_KILL_COW_KING_HELL,
    /* Pandemonium ubers */
    CGT_KILL_UBER_MEPHISTO,
    CGT_KILL_UBER_DIABLO,
    CGT_KILL_UBER_BAAL,
    CGT_HELLFIRE_TORCH_COMPLETE,
    /* Famous super-uniques */
    CGT_KILL_BISHIBOSH,
    CGT_KILL_CORPSEFIRE,
    CGT_KILL_RAKANISHU,
    CGT_KILL_GRISWOLD,
    CGT_KILL_PINDLESKIN,
    CGT_KILL_NIHLATHAK_SU,
    CGT_KILL_SUMMONER,
    CGT_KILL_RADAMENT,
    CGT_KILL_IZUAL,
    CGT_KILL_COUNCIL,
    /* Quest bulk */
    CGT_ALL_QUESTS_NORMAL,
    CGT_ALL_QUESTS_NM,
    CGT_ALL_QUESTS_HELL,
    CGT_ALL_HUNTING_QUESTS,
    CGT_ALL_KILL_ZONE_QUESTS,
    CGT_ALL_EXPLORATION_QUESTS,
    CGT_ALL_WAYPOINTS,
    CGT_ALL_LEVEL_MILESTONES,
    /* Bonus check bulk */
    CGT_ALL_SHRINES,
    CGT_ALL_URNS,
    CGT_ALL_BARRELS,
    CGT_ALL_CHESTS,
    CGT_ALL_SET_PICKUPS,
    CGT_ALL_GOLD_MILESTONES,
    /* Extra check bulk (1.9.2) */
    CGT_ALL_COW_LEVEL_CHECKS,
    CGT_ALL_MERC_MILESTONES,
    CGT_ALL_HELLFORGE_RUNES,
    CGT_ALL_NPC_DIALOGUE,
    CGT_ALL_RUNEWORD_CRAFTING,
    CGT_ALL_CUBE_RECIPES,
    /* Collection bulk */
    CGT_ALL_SET_PIECES,
    CGT_ALL_RUNES,
    CGT_ALL_GEMS,
    CGT_ALL_SPECIALS,
    CGT_COMPLETE_COLLECTION,
    /* End sentinel */
    CGT_COUNT
} CustomGoalTarget;

/* String<->index map. Names MUST match valid_keys in the apworld
 * options.py CustomGoalTargets OptionSet. Sorted by enum order. */
static const struct { const char* name; int idx; } CGT_NAME_TABLE[] = {
    { "kill_andariel_normal",    CGT_KILL_ANDARIEL_NORMAL    },
    { "kill_andariel_nightmare", CGT_KILL_ANDARIEL_NM        },
    { "kill_andariel_hell",      CGT_KILL_ANDARIEL_HELL      },
    { "kill_duriel_normal",      CGT_KILL_DURIEL_NORMAL      },
    { "kill_duriel_nightmare",   CGT_KILL_DURIEL_NM          },
    { "kill_duriel_hell",        CGT_KILL_DURIEL_HELL        },
    { "kill_mephisto_normal",    CGT_KILL_MEPHISTO_NORMAL    },
    { "kill_mephisto_nightmare", CGT_KILL_MEPHISTO_NM        },
    { "kill_mephisto_hell",      CGT_KILL_MEPHISTO_HELL      },
    { "kill_diablo_normal",      CGT_KILL_DIABLO_NORMAL      },
    { "kill_diablo_nightmare",   CGT_KILL_DIABLO_NM          },
    { "kill_diablo_hell",        CGT_KILL_DIABLO_HELL        },
    { "kill_baal_normal",        CGT_KILL_BAAL_NORMAL        },
    { "kill_baal_nightmare",     CGT_KILL_BAAL_NM            },
    { "kill_baal_hell",          CGT_KILL_BAAL_HELL          },
    { "kill_cow_king_normal",    CGT_KILL_COW_KING_NORMAL    },
    { "kill_cow_king_nightmare", CGT_KILL_COW_KING_NM        },
    { "kill_cow_king_hell",      CGT_KILL_COW_KING_HELL      },
    { "kill_uber_mephisto",      CGT_KILL_UBER_MEPHISTO      },
    { "kill_uber_diablo",        CGT_KILL_UBER_DIABLO        },
    { "kill_uber_baal",          CGT_KILL_UBER_BAAL          },
    { "hellfire_torch_complete", CGT_HELLFIRE_TORCH_COMPLETE },
    { "kill_bishibosh",          CGT_KILL_BISHIBOSH          },
    { "kill_corpsefire",         CGT_KILL_CORPSEFIRE         },
    { "kill_rakanishu",          CGT_KILL_RAKANISHU          },
    { "kill_griswold",           CGT_KILL_GRISWOLD           },
    { "kill_pindleskin",         CGT_KILL_PINDLESKIN         },
    { "kill_nihlathak_su",       CGT_KILL_NIHLATHAK_SU       },
    { "kill_summoner",           CGT_KILL_SUMMONER           },
    { "kill_radament",           CGT_KILL_RADAMENT           },
    { "kill_izual",              CGT_KILL_IZUAL              },
    { "kill_council",            CGT_KILL_COUNCIL            },
    { "all_quests_normal",       CGT_ALL_QUESTS_NORMAL       },
    { "all_quests_nightmare",    CGT_ALL_QUESTS_NM           },
    { "all_quests_hell",         CGT_ALL_QUESTS_HELL         },
    { "all_hunting_quests",      CGT_ALL_HUNTING_QUESTS      },
    { "all_kill_zone_quests",    CGT_ALL_KILL_ZONE_QUESTS    },
    { "all_exploration_quests",  CGT_ALL_EXPLORATION_QUESTS  },
    { "all_waypoints",           CGT_ALL_WAYPOINTS           },
    { "all_level_milestones",    CGT_ALL_LEVEL_MILESTONES    },
    { "all_shrines",             CGT_ALL_SHRINES             },
    { "all_urns",                CGT_ALL_URNS                },
    { "all_barrels",             CGT_ALL_BARRELS             },
    { "all_chests",              CGT_ALL_CHESTS              },
    { "all_set_pickups",         CGT_ALL_SET_PICKUPS         },
    { "all_gold_milestones",     CGT_ALL_GOLD_MILESTONES     },
    { "all_cow_level_checks",    CGT_ALL_COW_LEVEL_CHECKS    },
    { "all_merc_milestones",     CGT_ALL_MERC_MILESTONES     },
    { "all_hellforge_runes",     CGT_ALL_HELLFORGE_RUNES     },
    { "all_npc_dialogue",        CGT_ALL_NPC_DIALOGUE        },
    { "all_runeword_crafting",   CGT_ALL_RUNEWORD_CRAFTING   },
    { "all_cube_recipes",        CGT_ALL_CUBE_RECIPES        },
    { "all_set_pieces",          CGT_ALL_SET_PIECES          },
    { "all_runes",               CGT_ALL_RUNES               },
    { "all_gems",                CGT_ALL_GEMS                },
    { "all_specials",            CGT_ALL_SPECIALS            },
    { "complete_collection",     CGT_COMPLETE_COLLECTION     },
};
#define CGT_NAME_COUNT  ((int)(sizeof(CGT_NAME_TABLE) / sizeof(CGT_NAME_TABLE[0])))

/* Per-character custom-goal state. Persisted to d2arch_state_<char>.dat
 * via CustomGoal_SaveToFile / CustomGoal_LoadLine. */
#define CGT_BITMAP_BYTES   ((CGT_COUNT + 7) / 8)
typedef struct {
    uint8_t  required[CGT_BITMAP_BYTES];   /* set by slot_data parser */
    uint8_t  fired[CGT_BITMAP_BYTES];      /* set by detection hooks */
    uint64_t goldTarget;                   /* 0 = no gold req */
    BOOL     active;                       /* TRUE when goal=custom + slot_data parsed */
} CustomGoalState;
static CustomGoalState g_cgState;

/* ------------------------------------------------------------------
 * Bitmap helpers
 * ------------------------------------------------------------------ */
static BOOL CGT_IsRequired(int idx) {
    if (idx < 0 || idx >= CGT_COUNT) return FALSE;
    return (g_cgState.required[idx / 8] & (1 << (idx % 8))) != 0;
}
static BOOL CGT_IsFired(int idx) {
    if (idx < 0 || idx >= CGT_COUNT) return FALSE;
    return (g_cgState.fired[idx / 8] & (1 << (idx % 8))) != 0;
}
static void CGT_SetFired(int idx) {
    if (idx < 0 || idx >= CGT_COUNT) return;
    g_cgState.fired[idx / 8] |= (1 << (idx % 8));
}
static int CGT_LookupName(const char* name) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < CGT_NAME_COUNT; i++) {
        if (strcmp(name, CGT_NAME_TABLE[i].name) == 0)
            return CGT_NAME_TABLE[i].idx;
    }
    return -1;
}

/* ------------------------------------------------------------------
 * State management — called from OnCharacterLoad + AP slot_data parse
 * ------------------------------------------------------------------ */
void CustomGoal_ResetState(void) {
    memset(&g_cgState, 0, sizeof(g_cgState));
}

/* Parse the comma-separated targets list from slot_data. Whitespace
 * tolerant; unknown names are silently dropped (with a log line so
 * the user can see if their YAML had a typo). */
void CustomGoal_ParseTargetsCSV(const char* csv, uint64_t goldTarget) {
    /* Reset required bitmap first; preserve fired bitmap (the player
     * may have already triggered some targets in a previous session). */
    memset(g_cgState.required, 0, sizeof(g_cgState.required));
    g_cgState.goldTarget = goldTarget;
    g_cgState.active = TRUE;

    if (!csv || !csv[0]) {
        Log("CGT: empty targets CSV — goal will trivially complete on gold target alone (or instantly if gold=0)\n");
        return;
    }

    char buf[1024];
    strncpy(buf, csv, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    int parsed = 0, unknown = 0;
    char* tok = strtok(buf, ", \t\r\n");
    while (tok) {
        int idx = CGT_LookupName(tok);
        if (idx >= 0) {
            g_cgState.required[idx / 8] |= (1 << (idx % 8));
            parsed++;
        } else {
            Log("CGT: unknown target token '%s' — ignored\n", tok);
            unknown++;
        }
        tok = strtok(NULL, ", \t\r\n");
    }
    Log("CGT: parsed %d targets (%d unknown) + goldTarget=%llu\n",
        parsed, unknown, (unsigned long long)goldTarget);
}

/* ------------------------------------------------------------------
 * Persistence — line format: cgt_required=<hex>, cgt_fired=<hex>,
 * cgt_gold=<uint64>, cgt_active=<0/1>
 * ------------------------------------------------------------------ */
void CustomGoal_SaveToFile(FILE* f) {
    if (!f) return;
    if (!g_cgState.active) return;  /* don't write if goal isn't custom */
    fprintf(f, "cgt_active=1\n");
    fprintf(f, "cgt_gold=%llu\n", (unsigned long long)g_cgState.goldTarget);
    fputs("cgt_required=", f);
    for (int i = 0; i < CGT_BITMAP_BYTES; i++)
        fprintf(f, "%02x", g_cgState.required[i]);
    fputc('\n', f);
    fputs("cgt_fired=", f);
    for (int i = 0; i < CGT_BITMAP_BYTES; i++)
        fprintf(f, "%02x", g_cgState.fired[i]);
    fputc('\n', f);
}

void CustomGoal_LoadLine(const char* line) {
    int v;
    if (sscanf(line, "cgt_active=%d", &v) == 1) {
        g_cgState.active = (v != 0);
        return;
    }
    {
        unsigned long long u;
        if (sscanf(line, "cgt_gold=%llu", &u) == 1) {
            g_cgState.goldTarget = (uint64_t)u;
            return;
        }
    }
    if (strncmp(line, "cgt_required=", 13) == 0 ||
        strncmp(line, "cgt_fired=", 10) == 0) {
        BOOL isReq = (line[4] == 'r');
        const char* hex = strchr(line, '=') + 1;
        uint8_t* dst = isReq ? g_cgState.required : g_cgState.fired;
        int i = 0;
        while (hex[0] && hex[1] && i < CGT_BITMAP_BYTES) {
            unsigned x = 0;
            sscanf(hex, "%2x", &x);
            dst[i++] = (uint8_t)x;
            hex += 2;
        }
        return;
    }
}

/* ------------------------------------------------------------------
 * Detection entry points — called from existing event hooks elsewhere
 * in the codebase. Each is a no-op if g_cgState.active is FALSE OR
 * the matching target wasn't requested in the CSV.
 *
 * The gateway pattern: only mark fired if (1) goal is active and
 * (2) the target was actually required. This keeps the bitmap honest
 * — fired & required gives the precise "complete count".
 * ------------------------------------------------------------------ */
static void CustomGoal_TryFire(int idx) {
    if (!g_cgState.active) return;
    if (!CGT_IsRequired(idx)) return;
    if (CGT_IsFired(idx)) return;
    CGT_SetFired(idx);
    Log("CGT: target #%d fired (%s)\n", idx,
        idx < CGT_NAME_COUNT ? CGT_NAME_TABLE[idx].name : "?");
}

/* Boss kills — called from gameloop quest-complete on act-boss qids.
 * Mapping: qid 6/106/206/303/406 = Andariel/Duriel/Mephisto/Diablo/Baal.
 * diff: 0/1/2 = Normal/NM/Hell. */
void CustomGoal_OnActBossKilled(int bossIdx, int diff) {
    static const int TARGET_MAP[5][3] = {
        { CGT_KILL_ANDARIEL_NORMAL, CGT_KILL_ANDARIEL_NM, CGT_KILL_ANDARIEL_HELL },
        { CGT_KILL_DURIEL_NORMAL,   CGT_KILL_DURIEL_NM,   CGT_KILL_DURIEL_HELL   },
        { CGT_KILL_MEPHISTO_NORMAL, CGT_KILL_MEPHISTO_NM, CGT_KILL_MEPHISTO_HELL },
        { CGT_KILL_DIABLO_NORMAL,   CGT_KILL_DIABLO_NM,   CGT_KILL_DIABLO_HELL   },
        { CGT_KILL_BAAL_NORMAL,     CGT_KILL_BAAL_NM,     CGT_KILL_BAAL_HELL     },
    };
    if (bossIdx < 0 || bossIdx > 4) return;
    if (diff < 0 || diff > 2) return;
    CustomGoal_TryFire(TARGET_MAP[bossIdx][diff]);
}

void CustomGoal_OnCowKingKilled(int diff) {
    static const int MAP[3] = {
        CGT_KILL_COW_KING_NORMAL, CGT_KILL_COW_KING_NM, CGT_KILL_COW_KING_HELL
    };
    if (diff < 0 || diff > 2) return;
    CustomGoal_TryFire(MAP[diff]);
}

/* Pandemonium ubers — uberId: 0=Mephisto, 1=Diablo, 2=Baal */
void CustomGoal_OnUberKilled(int uberId) {
    switch (uberId) {
        case 0: CustomGoal_TryFire(CGT_KILL_UBER_MEPHISTO); break;
        case 1: CustomGoal_TryFire(CGT_KILL_UBER_DIABLO);   break;
        case 2: CustomGoal_TryFire(CGT_KILL_UBER_BAAL);     break;
    }
}

void CustomGoal_OnHellfireTorchComplete(void) {
    CustomGoal_TryFire(CGT_HELLFIRE_TORCH_COMPLETE);
}

/* Super-Unique kills — hcIdx mapping. Called from gameloop unit-death
 * scan on type==SuperUnique kills (already fires for quest-tracking). */
void CustomGoal_OnSuperUniqueKilled(int hcIdx) {
    static const struct { int hcIdx; int target; } MAP[] = {
        { 0,  CGT_KILL_BISHIBOSH    },  /* Bishibosh hcIdx=0 in vanilla */
        { 40, CGT_KILL_CORPSEFIRE   },
        { 3,  CGT_KILL_RAKANISHU    },
        { 5,  CGT_KILL_GRISWOLD     },
        { 53, CGT_KILL_PINDLESKIN   },
        { 61, CGT_KILL_NIHLATHAK_SU },
        { 35, CGT_KILL_SUMMONER     },
        { 31, CGT_KILL_RADAMENT     },
        { 41, CGT_KILL_IZUAL        },
        { 36, CGT_KILL_COUNCIL      },  /* Geleb Flamefinger (one of council) */
    };
    int n = (int)(sizeof(MAP) / sizeof(MAP[0]));
    for (int i = 0; i < n; i++) {
        if (MAP[i].hcIdx == hcIdx) {
            CustomGoal_TryFire(MAP[i].target);
            break;
        }
    }
}

/* ------------------------------------------------------------------
 * Per-tick poller — checks bulk completion targets that don't have
 * dedicated event hooks (e.g. "all_runeword_crafting" = check counter
 * vs threshold every tick). Cheap because each branch is a single
 * compare against a counter + bit check.
 *
 * Externs from other modules — only valid when those modules are
 * loaded later in the unity build, but quests.c (where this would be
 * called) is included BEFORE collections.c. So this poller lives in
 * extrachecks.c side and is invoked from gameloop tick directly.
 * ------------------------------------------------------------------ */
extern int Bonus_GetSlotCount(int category, int diff);
extern int Bonus_GetQuota(int category);
extern int Bonus_CountFiredSetPickups(void);
extern BOOL Bonus_IsGoldMilestoneFired(int idx);
extern int Extra_GetSlotCount(int cat);
extern int Extra_CountFiredCategory(int cat);
/* Forward decls for static-int globals we sanity-poll */
extern int  g_apDiffScope;
/* g_questCompleted is BOOL[3][800] in d2arch_quests.c (MAX_QUEST_ID=800).
 * We extern with the same dimension so the compiler doesn't redefine. */
extern int  g_questCompleted[3][800];

void CustomGoal_PollBulkTargets(void) {
    if (!g_cgState.active) return;

    /* All shrines: BX_SHRINE category fired count == quota * 3 */
    if (CGT_IsRequired(CGT_ALL_SHRINES) && !CGT_IsFired(CGT_ALL_SHRINES)) {
        int got = Bonus_GetSlotCount(0, 0) + Bonus_GetSlotCount(0, 1)
                + Bonus_GetSlotCount(0, 2);
        if (got >= Bonus_GetQuota(0) * 3) CGT_SetFired(CGT_ALL_SHRINES);
    }
    if (CGT_IsRequired(CGT_ALL_URNS) && !CGT_IsFired(CGT_ALL_URNS)) {
        int got = Bonus_GetSlotCount(1, 0) + Bonus_GetSlotCount(1, 1)
                + Bonus_GetSlotCount(1, 2);
        if (got >= Bonus_GetQuota(1) * 3) CGT_SetFired(CGT_ALL_URNS);
    }
    if (CGT_IsRequired(CGT_ALL_BARRELS) && !CGT_IsFired(CGT_ALL_BARRELS)) {
        int got = Bonus_GetSlotCount(2, 0) + Bonus_GetSlotCount(2, 1)
                + Bonus_GetSlotCount(2, 2);
        if (got >= Bonus_GetQuota(2) * 3) CGT_SetFired(CGT_ALL_BARRELS);
    }
    if (CGT_IsRequired(CGT_ALL_CHESTS) && !CGT_IsFired(CGT_ALL_CHESTS)) {
        int got = Bonus_GetSlotCount(3, 0) + Bonus_GetSlotCount(3, 1)
                + Bonus_GetSlotCount(3, 2);
        if (got >= Bonus_GetQuota(3) * 3) CGT_SetFired(CGT_ALL_CHESTS);
    }
    if (CGT_IsRequired(CGT_ALL_SET_PICKUPS) && !CGT_IsFired(CGT_ALL_SET_PICKUPS)) {
        if (Bonus_CountFiredSetPickups() >= 127) CGT_SetFired(CGT_ALL_SET_PICKUPS);
    }
    if (CGT_IsRequired(CGT_ALL_GOLD_MILESTONES) && !CGT_IsFired(CGT_ALL_GOLD_MILESTONES)) {
        int got = 0;
        for (int i = 0; i < 17; i++)
            if (Bonus_IsGoldMilestoneFired(i)) got++;
        if (got >= 17) CGT_SetFired(CGT_ALL_GOLD_MILESTONES);
    }

    /* Extra check bulk — category counters */
    if (CGT_IsRequired(CGT_ALL_COW_LEVEL_CHECKS) && !CGT_IsFired(CGT_ALL_COW_LEVEL_CHECKS)) {
        if (Extra_CountFiredCategory(0) >= Extra_GetSlotCount(0))
            CGT_SetFired(CGT_ALL_COW_LEVEL_CHECKS);
    }
    if (CGT_IsRequired(CGT_ALL_MERC_MILESTONES) && !CGT_IsFired(CGT_ALL_MERC_MILESTONES)) {
        if (Extra_CountFiredCategory(1) >= Extra_GetSlotCount(1))
            CGT_SetFired(CGT_ALL_MERC_MILESTONES);
    }
    if (CGT_IsRequired(CGT_ALL_HELLFORGE_RUNES) && !CGT_IsFired(CGT_ALL_HELLFORGE_RUNES)) {
        if (Extra_CountFiredCategory(2) >= Extra_GetSlotCount(2))
            CGT_SetFired(CGT_ALL_HELLFORGE_RUNES);
    }
    if (CGT_IsRequired(CGT_ALL_NPC_DIALOGUE) && !CGT_IsFired(CGT_ALL_NPC_DIALOGUE)) {
        if (Extra_CountFiredCategory(3) >= Extra_GetSlotCount(3))
            CGT_SetFired(CGT_ALL_NPC_DIALOGUE);
    }
    if (CGT_IsRequired(CGT_ALL_RUNEWORD_CRAFTING) && !CGT_IsFired(CGT_ALL_RUNEWORD_CRAFTING)) {
        if (Extra_CountFiredCategory(4) >= Extra_GetSlotCount(4))
            CGT_SetFired(CGT_ALL_RUNEWORD_CRAFTING);
    }
    if (CGT_IsRequired(CGT_ALL_CUBE_RECIPES) && !CGT_IsFired(CGT_ALL_CUBE_RECIPES)) {
        if (Extra_CountFiredCategory(5) >= Extra_GetSlotCount(5))
            CGT_SetFired(CGT_ALL_CUBE_RECIPES);
    }

    /* Quest bulk — count completed quests per difficulty.
     * MAX_QUEST_ID = 800 in quests.c; iterate 1..799. */
    static const int QUEST_TARGETS_PER_DIFF[3] = {
        CGT_ALL_QUESTS_NORMAL, CGT_ALL_QUESTS_NM, CGT_ALL_QUESTS_HELL
    };
    for (int d = 0; d < 3; d++) {
        if (!CGT_IsRequired(QUEST_TARGETS_PER_DIFF[d])) continue;
        if (CGT_IsFired(QUEST_TARGETS_PER_DIFF[d])) continue;
        int done = 0;
        for (int qid = 1; qid < 800; qid++) {
            if (g_questCompleted[d][qid]) done++;
        }
        /* Conservatively declare bulk done when >= 50 quests
         * completed for the diff — exact total varies with class /
         * skill_hunting filters. */
        if (done >= 50) CGT_SetFired(QUEST_TARGETS_PER_DIFF[d]);
    }
}

/* Public completion check — called from existing g_apGoalComplete
 * evaluator in d2arch_save.c / wherever goal-mode is checked.
 * Returns TRUE iff all required targets have fired AND lifetime gold
 * meets/exceeds the gold target. */
extern uint64_t g_charStats_lifetimeGold(void);  /* helper from stats.c */

BOOL CustomGoal_IsComplete(void) {
    if (!g_cgState.active) return FALSE;
    /* All required bits must be set in fired bits */
    for (int b = 0; b < CGT_BITMAP_BYTES; b++) {
        uint8_t need = g_cgState.required[b];
        if ((g_cgState.fired[b] & need) != need) return FALSE;
    }
    /* Gold target check */
    if (g_cgState.goldTarget > 0) {
        uint64_t lifetime = 0;
        __try { lifetime = g_charStats_lifetimeGold(); }
        __except(EXCEPTION_EXECUTE_HANDLER) { lifetime = 0; }
        if (lifetime < g_cgState.goldTarget) return FALSE;
    }
    return TRUE;
}

/* Reporting helpers for F1 Overview rendering */
int CustomGoal_GetRequiredCount(void) {
    int n = 0;
    for (int b = 0; b < CGT_BITMAP_BYTES; b++) {
        uint8_t v = g_cgState.required[b];
        while (v) { n += (v & 1); v >>= 1; }
    }
    return n;
}
int CustomGoal_GetFiredCount(void) {
    int n = 0;
    for (int b = 0; b < CGT_BITMAP_BYTES; b++) {
        uint8_t v = g_cgState.required[b] & g_cgState.fired[b];
        while (v) { n += (v & 1); v >>= 1; }
    }
    return n;
}
BOOL CustomGoal_IsActive(void) { return g_cgState.active; }
uint64_t CustomGoal_GetGoldTarget(void) { return g_cgState.goldTarget; }
