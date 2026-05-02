/* ================================================================
 * D2Archipelago - System 1: Dead-End Cave Entrance Shuffle
 *                          (TELEPORT-BASED IMPLEMENTATION)
 *
 * Replaces the prior Vis-patching approach (which fails on cross-act
 * because D2 1.10f's DRLG suppresses warp-tile placement when the
 * destination level is in a different act). Instead we:
 *
 *   1. Leave Vis/Warp tables ALONE — D2 places the click-able warp
 *      tile normally on the surface (Den of Evil's mouth in Blood
 *      Moor stays clickable).
 *   2. When player ENTERS a shuffled cave, intercept the area-change
 *      event and queue LEVEL_WarpUnit to the shuffled destination.
 *   3. Track the dungeon set the player is now inside (multi-floor
 *      dungeons like Tower Cellar L1..L5 are one set — internal
 *      navigation is left vanilla).
 *   4. When the player EXITS the dungeon set back to its natural
 *      surface zone, queue another warp back to the surface where
 *      they originally clicked.
 *
 * Mechanism: g_pendingZoneTeleport (already in d2arch_zones.c, used
 * by zone-locking). Same consumer in d2arch_gameloop.c calls
 * LEVEL_WarpUnit at D2Game+0xC410 which works for ANY level-id with
 * no act/proximity checks (per Research/teleport_warp_findings_2026-04-27.md).
 *
 * Cross-act shuffles WORK with this approach. Pool A = Acts 1+2 mixed
 * (per user's original spec). Pool B = Acts 3+4+5 mixed.
 *
 * sgptDataTables offsets still used for the maze-room enlargement
 * (Hell-size dungeons in all difficulties — that part of the prior
 * implementation worked fine and stays).
 * ================================================================ */

#define LDB_SIZE             0x9C
#define LDB_VIS_OFF          0x48
#define LDB_DRLGTYPE_OFF     0x30

#define DT_LEVELDEFBIN_PTR   0xC60
#define DT_LEVELS_COUNT      0xC5C
#define DT_LVLMAZE_PTR       0x1030
#define DT_LVLMAZE_COUNT     0x1034

#define LMAZE_SIZE           0x1C
#define LMAZE_LEVELID_OFF    0x00
#define LMAZE_ROOMS_OFF      0x04

/* === Toggle === */
static BOOL g_entranceShuffleEnabled = FALSE;
static BOOL g_entranceShuffleApplied = FALSE;

/* ----------------------------------------------------------------
 * Dungeon set table.
 *
 * A "set" = one logical cave unit with:
 *   surfaceParent: the zone the player comes FROM in vanilla
 *   members[]:     all level-IDs in this dungeon (L1, L2, ...). The
 *                  L1 (members[0]) is the entry level — that's what
 *                  triggers our "warp into shuffled destination" hook.
 *                  Internal Vis (L1<->L2) is left vanilla.
 *   act:           1..5 — used for pool grouping
 *   name:          for logging
 *
 * For v1: caves with outdoor parents only. Special portals (Tristram,
 * Cow Level, Pandemonium) are added — they work the same way because
 * D2 spawns the player at the level's default entry tile regardless
 * of how they got there (clicked Vis-tile vs. walked through portal).
 * ---------------------------------------------------------------- */
/* DungeonSet flags */
#define DS_FLAG_IS_COW        0x01  /* This set IS Cow Level — pre-placed onto
                                     * a cow-eligible partner so the swap
                                     * can never replace a quest cave. */
#define DS_FLAG_COW_ELIGIBLE  0x02  /* This set is allowed to be Cow Level's
                                     * partner: a non-quest, non-WP, side-only
                                     * Act 5 cave whose loss is harmless. */

typedef struct {
    int         surfaceParent;
    int         act;            /* 1-5, for pool grouping */
    int         members[8];     /* member level-IDs; member[0] = entry */
    int         numMembers;
    int         flags;          /* DS_FLAG_* bitmask */
    const char* name;
} DungeonSet;

static const DungeonSet g_sets[] = {
    /* ============================================================
     * Pool A — Act 1+2 mixed (21 sets total)
     * Acts 1+2 dead-end caves + Tristram + 7 Tal Rasha tombs.
     * Quest-impact accepted: e.g. Den of Evil quest fires on level
     * 8, but if the player's "Den of Evil door" actually leads to
     * Crypt, level 8 is never visited and quest never fires. The
     * player can still progress through act bosses — they just lose
     * those side-quest XP rewards. User accepts this trade.
     * ============================================================ */
    /* Act 1 */
    {  2, 1, {  8 },                  1, 0, "Den of Evil"           },
    {  3, 1, {  9, 13 },              2, 0, "Cave"                  },
    { 17, 1, { 18 },                  1, 0, "Crypt"                 },
    { 17, 1, { 19 },                  1, 0, "Mausoleum"             },
    {  6, 1, { 11, 15 },              2, 0, "Hole"                  },
    {  7, 1, { 12, 16 },              2, 0, "Pit"                   },
    { 20, 1, { 21, 22, 23, 24, 25 }, 5, 0, "Tower Cellar"           },
    {  4, 1, { 38 },                  1, 0, "Tristram (portal)"     },
    /* Act 2 */
    { 40, 2, { 47, 48, 49 },          3, 0, "A2 Sewers"             },
    { 41, 2, { 55, 59 },              2, 0, "Stony Tomb"            },
    { 42, 2, { 56, 57, 60 },          3, 0, "Halls of the Dead"     },
    { 43, 2, { 62, 63, 64 },          3, 0, "Maggot Lair"           },
    { 44, 2, { 65 },                  1, 0, "Ancient Tunnels"       },
    { 45, 2, { 58, 61 },              2, 0, "Claw Viper Temple"     },
    /* 7 Tal Rasha tombs — all entered from Canyon of the Magi (46).
     * One of them is the "true tomb" (random per game) containing the
     * Orifice for Duriel's Lair. Shuffle just changes which physical
     * tomb is behind which Canyon-portal. Player iterates as in vanilla. */
    { 46, 2, { 66 },                  1, 0, "Tal Rasha Tomb #1"     },
    { 46, 2, { 67 },                  1, 0, "Tal Rasha Tomb #2"     },
    { 46, 2, { 68 },                  1, 0, "Tal Rasha Tomb #3"     },
    { 46, 2, { 69 },                  1, 0, "Tal Rasha Tomb #4"     },
    { 46, 2, { 70 },                  1, 0, "Tal Rasha Tomb #5"     },
    { 46, 2, { 71 },                  1, 0, "Tal Rasha Tomb #6"     },
    { 46, 2, { 72 },                  1, 0, "Tal Rasha Tomb #7"     },

    /* ============================================================
     * Pool B — Act 3+4+5 mixed + Cow Level (17 sets)
     * Cow Level pre-placed onto a cow-eligible partner (Cellar of
     * Pity OR Echo Chamber — picked by seed). The remaining 15 sets
     * are Sattolo'd into a single cycle. Total cycle structure:
     * 2-cycle (cow swap) + 15-cycle (everything else).
     * Act 4 has zero dead-end caves — pure progression spine.
     * ============================================================ */
    /* Act 3 — main dungeons */
    { 76, 3, { 84 },                  1, 0, "Spider Cave"           },
    { 76, 3, { 85 },                  1, 0, "Spider Cavern"         },
    { 78, 3, { 86, 87, 90 },          3, 0, "Swampy Pit"            },
    { 78, 3, { 88, 89, 91 },          3, 0, "Flayer Dungeon"        },
    /* A3 Sewers — entered from BOTH Kurast Bazaar AND Upper Kurast.
     * We model parent as Bazaar (80); the other entrance still works
     * because both lead to level 92 and our hook fires on entry to 92. */
    { 80, 3, { 92, 93 },              2, 0, "A3 Sewers"             },
    /* 6 tome temples — one contains Lam Esen's Tome (random per game).
     * Same iteration model as Tal Rasha tombs. */
    { 80, 3, { 94 },                  1, 0, "Ruined Temple"         },
    { 80, 3, { 95 },                  1, 0, "Disused Fane"          },
    { 81, 3, { 96 },                  1, 0, "Forgotten Reliquary"   },
    { 81, 3, { 97 },                  1, 0, "Forgotten Temple"      },
    { 82, 3, { 98 },                  1, 0, "Ruined Fane"           },
    { 82, 3, { 99 },                  1, 0, "Disused Reliquary"     },
    /* Act 5 — main dungeons (off the act spine but contain WPs/quests) */
    { 112, 5, { 113, 115 },           2, 0, "Crystallized Cavern"   },
    { 117, 5, { 118, 119 },           2, 0, "Glacial Caves"         },
    /* Nihlathak chain — entered via Anya-portal in Harrogath after
     * Anya-rescue quest. Single set covering the whole 4-level chain. */
    { 109, 5, { 121, 122, 123, 124 }, 4, 0, "Nihlathak chain"       },
    /* Cow Level — portal-only from Rogue Encampment via cube recipe.
     * Pre-placed onto a cow-eligible partner so it never replaces a
     * quest-required cave. */
    {   1, 5, { 39 },                 1, DS_FLAG_IS_COW, "Cow Level (portal)" },
    /* Cow-eligible partners: pure side dungeons in Act 5, no quest,
     * no waypoint, off Crystallized Cavern. Safe for Cow Level to
     * replace because losing access to them costs nothing. */
    { 113, 5, { 114 },                1, DS_FLAG_COW_ELIGIBLE, "Cellar of Pity" },
    { 115, 5, { 116 },                1, DS_FLAG_COW_ELIGIBLE, "Echo Chamber"   },
};
#define NUM_SETS  ((int)(sizeof(g_sets) / sizeof(g_sets[0])))

/* === Shuffle map: index of set -> destination set index ===
 * Built per-pool with Sattolo (no fixed points within pool). */
static int g_shuffleMap[NUM_SETS];

/* === Per-frame runtime state === */
static int g_currentSetIdx     = -1;  /* which set player is in (-1 = none) */
static int g_returnSurface     = 0;   /* warp-back target on exit (0 = no pending) */
static int g_pendingWarpTarget = 0;   /* swallow next area-change matching this */

/* === Maze enlargement backup (kept from prior version) === */
typedef struct {
    int   levelId;
    DWORD origRooms[3];
} MazePatch;
#define MAX_MAZE_PATCHES  96
static MazePatch g_mazePatches[MAX_MAZE_PATCHES];
static int       g_mazePatchCount = 0;

/* ----------------------------------------------------------------
 * Permutation + hash helpers
 * ---------------------------------------------------------------- */

/* Sattolo's algorithm — single-cycle permutation, 0 fixed points */
static void BuildPermutation(int* perm, int size, DWORD seed) {
    for (int i = 0; i < size; i++) perm[i] = i;
    if (size < 2) return;
    DWORD s = seed * 2654435761u;
    for (int i = size - 1; i > 0; i--) {
        s = s * 1103515245u + 12345u;
        int j = (int)((s >> 16) % (DWORD)i);
        int tmp = perm[i]; perm[i] = perm[j]; perm[j] = tmp;
    }
}

/* DJB2 string hash — seed fallback for non-AP characters */
static DWORD HashCharName(const char* name) {
    DWORD h = 5381;
    if (!name) return 1;
    while (*name) {
        h = ((h << 5) + h) ^ (DWORD)(BYTE)*name++;
    }
    return h ? h : 1;
}

/* ----------------------------------------------------------------
 * Set-membership helpers
 * ---------------------------------------------------------------- */

/* Returns the index in g_sets[] whose members[0] == level, or -1.
 * "Entry level" = the L1 of a dungeon. Used to detect "player just
 * entered a cave from outside". */
static int FindSetByEntryLevel(int level) {
    for (int i = 0; i < NUM_SETS; i++) {
        if (g_sets[i].numMembers > 0 && g_sets[i].members[0] == level) return i;
    }
    return -1;
}

/* Returns TRUE if level is anywhere in the set's members[]. */
static BOOL IsLevelInSet(int level, int setIdx) {
    if (setIdx < 0 || setIdx >= NUM_SETS) return FALSE;
    const DungeonSet* s = &g_sets[setIdx];
    for (int i = 0; i < s->numMembers; i++) {
        if (s->members[i] == level) return TRUE;
    }
    return FALSE;
}

/* ----------------------------------------------------------------
 * Act-town locking — prevent player from being stuck in an act-town
 * they haven't unlocked yet.
 *
 * Triggered by: dying inside a shuffled cave that physically lives
 * in a higher act than the player has progressed to. D2 respawns
 * the player in the town of THAT act (e.g., shuffled into Maggot
 * Lair, die, respawn in Lut Gholein). Without this check, the
 * player could explore Act 2 without having killed Andariel.
 *
 * Rule: if the player ends up in town for act N but hasn't killed
 * boss N-1, warp them back to their highest-unlocked act's town.
 * ---------------------------------------------------------------- */

/* D2 quest IDs for act-end bosses (param values in d2arch_quests.c) */
#define ACT_BOSS_QUEST_ANDARIEL  6
#define ACT_BOSS_QUEST_DURIEL    14
#define ACT_BOSS_QUEST_MEPHISTO  22
#define ACT_BOSS_QUEST_DIABLO    26

/* D2 quest state flags (subset — same set used by CheckQuestFlags) */
#define QSTATE_REWARD_GRANTED    0
#define QSTATE_PRIMARY_DONE      13
#define QSTATE_COMPLETED_NOW     14
#define QSTATE_COMPLETED_BEFORE  15

static BOOL IsActBossDead(void* pQuestFlags, int d2QuestId) {
    if (!pQuestFlags || !fnGetQuestState) return FALSE;
    BOOL done = FALSE;
    __try {
        done = fnGetQuestState(pQuestFlags, d2QuestId, QSTATE_REWARD_GRANTED) ||
               fnGetQuestState(pQuestFlags, d2QuestId, QSTATE_PRIMARY_DONE)   ||
               fnGetQuestState(pQuestFlags, d2QuestId, QSTATE_COMPLETED_NOW)  ||
               fnGetQuestState(pQuestFlags, d2QuestId, QSTATE_COMPLETED_BEFORE);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return done;
}

/* Read pQuestFlags via pGame->pQuestControl->pQuestFlags chain.
 * Same offsets as CheckQuestFlags() in d2arch_gameloop.c. */
static int GetHighestUnlockedAct(void) {
    if (!g_cachedPGame || !fnGetQuestState) return 5;  /* fail open */
    void* pQuestFlags = NULL;
    __try {
        DWORD pQuestControl = *(DWORD*)(g_cachedPGame + 0x10F4);
        if (!pQuestControl) return 5;
        pQuestFlags = *(void**)(pQuestControl + 0x0C);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 5; }
    if (!pQuestFlags) return 5;

    int act = 1;
    if (IsActBossDead(pQuestFlags, ACT_BOSS_QUEST_ANDARIEL)) act = 2;
    if (IsActBossDead(pQuestFlags, ACT_BOSS_QUEST_DURIEL))   act = 3;
    if (IsActBossDead(pQuestFlags, ACT_BOSS_QUEST_MEPHISTO)) act = 4;
    if (IsActBossDead(pQuestFlags, ACT_BOSS_QUEST_DIABLO))   act = 5;
    return act;
}

/* Map act 1..5 -> town level-id */
static int TownForAct(int act) {
    switch (act) {
        case 1: return 1;     /* Rogue Encampment */
        case 2: return 40;    /* Lut Gholein */
        case 3: return 75;    /* Kurast Docktown */
        case 4: return 103;   /* Pandemonium Fortress */
        case 5: return 109;   /* Harrogath */
        default: return 1;
    }
}

/* Returns town level-id to warp to if currentArea is a forbidden town,
 * or 0 if no warp needed. */
static int CheckActTownLock(int currentArea) {
    if (!IsTown((DWORD)currentArea)) return 0;
    int townAct = GetActForArea(currentArea);
    if (townAct <= 1) return 0;  /* Act 1 town is always allowed */
    int maxUnlocked = GetHighestUnlockedAct();
    if (townAct <= maxUnlocked) return 0;
    return TownForAct(maxUnlocked);
}

/* ----------------------------------------------------------------
 * Maze room enlargement (Hell-size everywhere)
 * Same as prior version — works fine, gives bigger dungeons.
 * ---------------------------------------------------------------- */
static void EnlargeMazeRooms(DWORD pLvlMazeTxt, int nMazeCount) {
    if (!pLvlMazeTxt || nMazeCount <= 0) return;
    if (nMazeCount > MAX_MAZE_PATCHES) {
        Log("ENTRANCE SHUFFLE: maze count %d > backup %d, capping\n",
            nMazeCount, MAX_MAZE_PATCHES);
        nMazeCount = MAX_MAZE_PATCHES;
    }
    int enlarged = 0;
    for (int i = 0; i < nMazeCount; i++) {
        BYTE* rec = (BYTE*)(pLvlMazeTxt + (DWORD)i * LMAZE_SIZE);
        DWORD* dwRooms = (DWORD*)(rec + LMAZE_ROOMS_OFF);
        DWORD hell = dwRooms[2];
        DWORD normal = dwRooms[0];
        DWORD nm = dwRooms[1];
        if (hell > normal || hell > nm) {
            int levelId = (int)*(DWORD*)(rec + LMAZE_LEVELID_OFF);
            g_mazePatches[g_mazePatchCount].levelId = levelId;
            g_mazePatches[g_mazePatchCount].origRooms[0] = normal;
            g_mazePatches[g_mazePatchCount].origRooms[1] = nm;
            g_mazePatches[g_mazePatchCount].origRooms[2] = hell;
            g_mazePatchCount++;
            if (hell > normal) dwRooms[0] = hell;
            if (hell > nm)     dwRooms[1] = hell;
            enlarged++;
        }
    }
    Log("ENTRANCE SHUFFLE: enlarged %d maze records to Hell-size rooms\n", enlarged);
}

/* ----------------------------------------------------------------
 * Apply — build the shuffle map + enlarge mazes.
 *
 * NO Vis-patching. The shuffle map is consumed by EntranceShuffle_Tick
 * which runs per-frame from d2arch_drawall.c.
 * ---------------------------------------------------------------- */
static void ApplyEntranceShuffle(DWORD seed) {
    if (g_entranceShuffleApplied) return;

    /* Derive effective seed (char-name fallback for non-AP chars) */
    extern char g_charName[];
    DWORD effectiveSeed = seed;
    if (effectiveSeed == 0 && g_charName[0]) {
        effectiveSeed = HashCharName(g_charName);
        Log("ENTRANCE SHUFFLE: g_seed=0, derived from charName '%s' -> %u\n",
            g_charName, effectiveSeed);
    }

    /* Build pool index lists. Pool A = Act 1+2. Pool B = Act 3-5
     * INCLUDING Cow Level and cow-eligible partners (the cow constraint
     * is enforced via pre-placement, not pool exclusion). */
    int poolA[NUM_SETS], poolB[NUM_SETS];
    int cowIdx = -1;
    int cowEligibleIdxs[NUM_SETS];
    int numCowEligible = 0;
    int countA = 0, countB = 0;
    for (int i = 0; i < NUM_SETS; i++) {
        if (g_sets[i].flags & DS_FLAG_IS_COW)        cowIdx = i;
        if (g_sets[i].flags & DS_FLAG_COW_ELIGIBLE)  cowEligibleIdxs[numCowEligible++] = i;
        if (g_sets[i].act >= 1 && g_sets[i].act <= 2) {
            poolA[countA++] = i;
        } else if (g_sets[i].act >= 3 && g_sets[i].act <= 5) {
            poolB[countB++] = i;
        }
    }

    /* Default: identity (no shuffle) */
    for (int i = 0; i < NUM_SETS; i++) g_shuffleMap[i] = i;

    /* Pool A: full Sattolo */
    if (countA >= 2) {
        int permA[NUM_SETS];
        BuildPermutation(permA, countA, effectiveSeed);
        for (int i = 0; i < countA; i++) {
            g_shuffleMap[poolA[i]] = poolA[permA[i]];
        }
    }

    /* Pool B with Cow Level constraint:
     *   1. Pre-place Cow Level <-> a cow-eligible partner (deterministic
     *      pick based on seed). This guarantees the cave that "leads
     *      to Cow Level" is non-quest, and walking out of Cow Level
     *      returns the player to the partner cave's natural surface.
     *   2. Sattolo the remaining Pool B sets (excluding cow + partner).
     *   3. Result: 2-cycle (cow swap) + (countB-2)-cycle (everything else),
     *      no fixed points anywhere. */
    int cowPartnerIdx = -1;
    if (cowIdx >= 0 && numCowEligible > 0) {
        DWORD pickSeed = (effectiveSeed * 1234567u) ^ 0xCAFEF00Du;
        int pick = (int)(pickSeed % (DWORD)numCowEligible);
        cowPartnerIdx = cowEligibleIdxs[pick];
        g_shuffleMap[cowIdx] = cowPartnerIdx;
        g_shuffleMap[cowPartnerIdx] = cowIdx;
        Log("ENTRANCE SHUFFLE: Cow Level pre-placed: %s <-> %s\n",
            g_sets[cowIdx].name, g_sets[cowPartnerIdx].name);
    }

    /* Sattolo the rest of Pool B */
    int poolBRest[NUM_SETS];
    int countBRest = 0;
    for (int i = 0; i < countB; i++) {
        int idx = poolB[i];
        if (idx == cowIdx || idx == cowPartnerIdx) continue;
        poolBRest[countBRest++] = idx;
    }
    if (countBRest >= 2) {
        int permB[NUM_SETS];
        BuildPermutation(permB, countBRest, effectiveSeed ^ 0xA5A5A5A5u);
        for (int i = 0; i < countBRest; i++) {
            g_shuffleMap[poolBRest[i]] = poolBRest[permB[i]];
        }
    }

    /* Log the shuffle for visibility */
    Log("ENTRANCE SHUFFLE [Pool A, Act 1+2, %d sets]:\n", countA);
    for (int i = 0; i < countA; i++) {
        int src = poolA[i];
        int dst = g_shuffleMap[src];
        Log("  %s (entry L%d) -> %s (entry L%d)\n",
            g_sets[src].name, g_sets[src].members[0],
            g_sets[dst].name, g_sets[dst].members[0]);
    }
    Log("ENTRANCE SHUFFLE [Pool B, Act 3+4+5 + Cow, %d sets]:\n", countB);
    for (int i = 0; i < countB; i++) {
        int src = poolB[i];
        int dst = g_shuffleMap[src];
        Log("  %s (entry L%d) -> %s (entry L%d)\n",
            g_sets[src].name, g_sets[src].members[0],
            g_sets[dst].name, g_sets[dst].members[0]);
    }

    /* Reset runtime state */
    g_currentSetIdx = -1;
    g_returnSurface = 0;
    g_pendingWarpTarget = 0;

    /* Enlarge maze rooms to Hell-size in all difficulties */
    g_mazePatchCount = 0;
    DWORD dt = GetSgptDT();
    if (dt) {
        DWORD pLvlMazeTxt = 0;
        int nMazeCount = 0;
        __try {
            pLvlMazeTxt = *(DWORD*)(dt + DT_LVLMAZE_PTR);
            nMazeCount = *(int*)(dt + DT_LVLMAZE_COUNT);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if (pLvlMazeTxt && nMazeCount > 0) {
            DWORD bytes = (DWORD)nMazeCount * LMAZE_SIZE;
            DWORD oldProt;
            VirtualProtect((void*)pLvlMazeTxt, bytes, PAGE_READWRITE, &oldProt);
            EnlargeMazeRooms(pLvlMazeTxt, nMazeCount);
            VirtualProtect((void*)pLvlMazeTxt, bytes, oldProt, &oldProt);
        }
    }

    g_entranceShuffleApplied = TRUE;
    Log("ENTRANCE SHUFFLE: applied (effective seed=%u, %d sets, %d maze patches)\n",
        effectiveSeed, NUM_SETS, g_mazePatchCount);
}

/* ----------------------------------------------------------------
 * Undo — clear runtime state + revert maze enlargement.
 * ---------------------------------------------------------------- */
static void UndoEntranceShuffle(void) {
    if (!g_entranceShuffleApplied) return;
    g_currentSetIdx = -1;
    g_returnSurface = 0;
    g_pendingWarpTarget = 0;

    DWORD dt = GetSgptDT();
    if (dt && g_mazePatchCount > 0) {
        DWORD pLvlMazeTxt = 0;
        int nMazeCount = 0;
        __try {
            pLvlMazeTxt = *(DWORD*)(dt + DT_LVLMAZE_PTR);
            nMazeCount = *(int*)(dt + DT_LVLMAZE_COUNT);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if (pLvlMazeTxt && nMazeCount > 0) {
            DWORD bytes = (DWORD)nMazeCount * LMAZE_SIZE;
            DWORD oldProt;
            VirtualProtect((void*)pLvlMazeTxt, bytes, PAGE_READWRITE, &oldProt);
            for (int p = 0; p < g_mazePatchCount; p++) {
                for (int i = 0; i < nMazeCount; i++) {
                    BYTE* rec = (BYTE*)(pLvlMazeTxt + (DWORD)i * LMAZE_SIZE);
                    int recLevelId = (int)*(DWORD*)(rec + LMAZE_LEVELID_OFF);
                    if (recLevelId == g_mazePatches[p].levelId) {
                        DWORD* dwRooms = (DWORD*)(rec + LMAZE_ROOMS_OFF);
                        dwRooms[0] = g_mazePatches[p].origRooms[0];
                        dwRooms[1] = g_mazePatches[p].origRooms[1];
                        dwRooms[2] = g_mazePatches[p].origRooms[2];
                        break;
                    }
                }
            }
            VirtualProtect((void*)pLvlMazeTxt, bytes, oldProt, &oldProt);
        }
    }

    Log("ENTRANCE SHUFFLE: undone (%d maze patches reverted)\n", g_mazePatchCount);
    g_mazePatchCount = 0;
    g_entranceShuffleApplied = FALSE;
}

/* ----------------------------------------------------------------
 * Tick — called per-frame from d2arch_drawall.c after the
 * zone-locking enforcement block. Detects area changes and queues
 * teleports via g_pendingZoneTeleport (consumed by gameloop.c).
 * ---------------------------------------------------------------- */
static void EntranceShuffle_Tick(void) {
    if (!g_entranceShuffleEnabled || !g_entranceShuffleApplied) return;

    static int s_lastArea = -1;
    int newArea = GetCurrentArea();
    if (newArea <= 0) return;
    if (newArea == s_lastArea) return;

    int prevArea = s_lastArea;
    s_lastArea = newArea;

    /* Suppress the area-change that's the result of OUR own warp */
    if (newArea == g_pendingWarpTarget) {
        g_pendingWarpTarget = 0;
        Log("ENTRANCE SHUFFLE TICK: ignoring area-change to %d (our own warp landing)\n", newArea);
        return;
    }

    /* Act-town lock: catches death-respawn into a higher-act town than
     * the player has unlocked. Most common after dying in a shuffled
     * cave that physically lives in a higher act. */
    {
        int kickTo = CheckActTownLock(newArea);
        if (kickTo > 0 && g_pendingZoneTeleport == 0) {
            g_pendingZoneTeleport = kickTo;
            g_pendingWarpTarget = kickTo;
            Log("ENTRANCE SHUFFLE TICK: town %d (act %d) not yet unlocked (max unlocked=%d) "
                "-> kicking to town %d\n",
                newArea, GetActForArea(newArea), GetHighestUnlockedAct(), kickTo);
            ShowNotify("You haven't unlocked this act yet — returning home");
            /* Clear cave-context state since the trip is aborted */
            g_currentSetIdx = -1;
            g_returnSurface = 0;
            return;
        }
    }


    /* CASE A: player is currently inside a shuffled dungeon set.
     * Either internal navigation (L1<->L2) — ignore — or they exited
     * the set entirely. */
    if (g_currentSetIdx >= 0) {
        if (IsLevelInSet(newArea, g_currentSetIdx)) {
            /* Internal navigation, no action */
            return;
        }
        /* Player left the set */
        int parentOfCurrentSet = g_sets[g_currentSetIdx].surfaceParent;
        if (newArea == parentOfCurrentSet && g_returnSurface > 0
            && g_pendingZoneTeleport == 0)
        {
            /* Exited via the natural surface — redirect back to where
             * the player originally clicked from. */
            int returnTo = g_returnSurface;
            g_pendingZoneTeleport = returnTo;
            g_pendingWarpTarget = returnTo;
            Log("ENTRANCE SHUFFLE TICK: exit detected (set='%s' parentNatural=%d, "
                "originalReturn=%d) -> warping to %d\n",
                g_sets[g_currentSetIdx].name, parentOfCurrentSet, returnTo, returnTo);
        } else {
            Log("ENTRANCE SHUFFLE TICK: left set '%s' to area %d (not natural parent "
                "or no pending return) -> clearing state\n",
                g_sets[g_currentSetIdx].name, newArea);
        }
        g_returnSurface = 0;
        g_currentSetIdx = -1;
        return;
    }

    /* CASE B: player is NOT in any shuffled set. Check if they just
     * entered a cave entry level. */
    int sourceSetIdx = FindSetByEntryLevel(newArea);
    if (sourceSetIdx < 0) return;  /* not a cave entry — ignore */

    int destSetIdx = g_shuffleMap[sourceSetIdx];
    if (destSetIdx == sourceSetIdx) {
        /* Identity — no warp needed, but still track */
        g_currentSetIdx = sourceSetIdx;
        g_returnSurface = g_sets[sourceSetIdx].surfaceParent;
        Log("ENTRANCE SHUFFLE TICK: entry to '%s' (identity, no warp)\n",
            g_sets[sourceSetIdx].name);
        return;
    }

    /* Different destination — queue warp */
    int destL1 = g_sets[destSetIdx].members[0];
    if (g_pendingZoneTeleport != 0) {
        /* Zone-locking already queued a warp this tick — defer */
        Log("ENTRANCE SHUFFLE TICK: g_pendingZoneTeleport=%d already set, "
            "deferring entry-warp\n", g_pendingZoneTeleport);
        return;
    }
    g_pendingZoneTeleport = destL1;
    g_pendingWarpTarget = destL1;
    g_returnSurface = g_sets[sourceSetIdx].surfaceParent;
    g_currentSetIdx = destSetIdx;
    Log("ENTRANCE SHUFFLE TICK: entry to L%d ('%s' from area %d) -> warp to "
        "L%d ('%s'), return to surface %d\n",
        newArea, g_sets[sourceSetIdx].name, prevArea,
        destL1, g_sets[destSetIdx].name, g_returnSurface);
}
