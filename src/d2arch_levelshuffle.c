/* System 1: Dead-End Cave Entrance Shuffle (TELEPORT-BASED IMPLEMENTATION) Replaces the prior Vis-patching approach (which fails on cross-act because D2 1.10f's DRLG suppresses warp-tile placement when the destination level is in a different act). */

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

/* Dungeon set table. A "set" = one logical cave unit with: surfaceParent: the zone the player comes FROM in vanilla members[]: all level-IDs in this dungeon (L1, L2, ...). The L1 (members[0]) is the entry level — that's what triggers our "warp into shuffled destination" hook. Internal Vis (L1<->L2) is left vanilla. act: 1..5 — used for pool grouping name: for logging For v1: caves with outdoor parents only. Special portals (Tristram, Cow Level, Pandemonium) are added — they work the same way because D2 spawns the player at the level's default entry tile regardless of how they got there (clicked Vis-tile vs. walked through portal). */
/* DungeonSet flags */
#define DS_FLAG_IS_COW        0x01  /* This set IS Cow Level — pre-placed onto a cow-eligible partner so the swap can never replace a quest cave. */
#define DS_FLAG_NO_SHUFFLE    0x04  /* Quest-critical set PINNED to its vanilla entrance (identity mapping). */
#define DS_FLAG_COW_ELIGIBLE  0x02  /* This set is allowed to be Cow Level's partner: a non-quest, non-WP, side-only Act 5 cave whose loss is harmless. */

typedef struct {
    int         surfaceParent;
    int         act;            /* 1-5, for pool grouping */
    int         members[8];     /* member level-IDs; member[0] = entry */
    int         numMembers;
    int         flags;          /* DS_FLAG_* bitmask */
    const char* name;
} DungeonSet;

static const DungeonSet g_sets[] = {
    /* Pool A — Act 1+2 mixed (21 sets total) Acts 1+2 dead-end caves + Tristram + 7 Tal Rasha tombs. */
    /* Act 1 */
    {  2, 1, {  8 },                  1, 0, "Den of Evil"           },
    {  3, 1, {  9, 13 },              2, 0, "Cave"                  },
    { 17, 1, { 18 },                  1, 0, "Crypt"                 },
    { 17, 1, { 19 },                  1, 0, "Mausoleum"             },
    {  6, 1, { 11, 15 },              2, 0, "Hole"                  },
    {  7, 1, { 12, 16 },              2, 0, "Pit"                   },
    /* 1.9.5 bug O3 fix: Forgotten Tower L1 is level 20 (entry from Black Marsh = level 6 via Vis2). */
    {  6, 1, { 20, 21, 22, 23, 24, 25 }, 6, 0, "Forgotten Tower"     },
    /* Tristram (level 38, Stony Field portal) REMOVED from shuffle pool. */
    /* Act 2 */
    { 40, 2, { 47, 48, 49 },          3, 0, "A2 Sewers"             },
    { 41, 2, { 55, 59 },              2, 0, "Stony Tomb"            },
    /* the three staff-chain dungeons are PINNED (DS_FLAG_NO_SHUFFLE): if any of them shuffles behind a Tal Rasha tomb entrance the seed hard-deadlocks (tomb access needs Palace, Palace needs Tainted Sun, Tainted Sun needs CVT). */
    { 42, 2, { 56, 57, 60 },          3, DS_FLAG_NO_SHUFFLE, "Halls of the Dead"     },
    { 43, 2, { 62, 63, 64 },          3, DS_FLAG_NO_SHUFFLE, "Maggot Lair"           },
    { 44, 2, { 65 },                  1, 0, "Ancient Tunnels"       },
    { 45, 2, { 58, 61 },              2, DS_FLAG_NO_SHUFFLE, "Claw Viper Temple"     },
    /* 7 Tal Rasha tombs — all entered from Canyon of the Magi (46). */
    { 46, 2, { 66 },                  1, 0, "Tal Rasha Tomb #1"     },
    { 46, 2, { 67 },                  1, 0, "Tal Rasha Tomb #2"     },
    { 46, 2, { 68 },                  1, 0, "Tal Rasha Tomb #3"     },
    { 46, 2, { 69 },                  1, 0, "Tal Rasha Tomb #4"     },
    { 46, 2, { 70 },                  1, 0, "Tal Rasha Tomb #5"     },
    { 46, 2, { 71 },                  1, 0, "Tal Rasha Tomb #6"     },
    { 46, 2, { 72 },                  1, 0, "Tal Rasha Tomb #7"     },

    /* Pool B — Act 3+4+5 mixed + Cow Level (17 sets) Cow Level pre-placed onto a cow-eligible partner (Cellar of Pity OR Echo Chamber — picked by seed). */
    /* Act 3 — main dungeons */
    { 76, 3, { 84 },                  1, 0, "Spider Cave"           },
    /* Khalim organ chain — the three organ dungeons are PINNED (DS_FLAG_NO_SHUFFLE), same class of deadlock as the A2 staff chain above: Pool B mixes Acts 3+4+5, so an organ dungeon can land behind an Act 5 entrance — but Act 5 needs Diablo, Diablo needs Mephisto, and Mephisto needs Khalim's Will assembled from the organ INSIDE that dungeon. */
    { 76, 3, { 85 },                  1, DS_FLAG_NO_SHUFFLE, "Spider Cavern"         },
    { 78, 3, { 86, 87, 90 },          3, 0, "Swampy Pit"            },
    { 78, 3, { 88, 89, 91 },          3, DS_FLAG_NO_SHUFFLE, "Flayer Dungeon"        },
    /* entered from BOTH Kurast Bazaar AND Upper Kurast. */
    { 80, 3, { 92, 93 },              2, DS_FLAG_NO_SHUFFLE, "A3 Sewers"             },
    /* 6 tome temples — one contains Lam Esen's Tome (random per game). */
    { 80, 3, { 94 },                  1, 0, "Ruined Temple"         },
    { 80, 3, { 95 },                  1, 0, "Disused Fane"          },
    { 81, 3, { 96 },                  1, 0, "Forgotten Reliquary"   },
    { 81, 3, { 97 },                  1, 0, "Forgotten Temple"      },
    { 82, 3, { 98 },                  1, 0, "Ruined Fane"           },
    { 82, 3, { 99 },                  1, 0, "Disused Reliquary"     },
    /* Act 5 - THE ACT SPINE. Both of these are PINNED (DS_FLAG_NO_SHUFFLE),
     * and the comment that used to sit here calling them "off the act spine"
     * was simply wrong. Read the Vis columns: 115 -> 117 (Tundra, outdoors)
     * and 118 -> 120 (Rocky Summit). A set is only safe to shuffle if every
     * member's Vis targets are inside the set or are the surfaceParent,
     * because the exit handler below knows exactly one way out -- back to the
     * parent -- and releases the player anywhere else. These two are
     * corridors, not caves, so shuffling them let a tester walk from an Act 3
     * entrance to Arreat Summit and finish the game from there. */
    { 112, 5, { 113, 115 },           2, DS_FLAG_NO_SHUFFLE, "Crystallized Cavern" },
    { 117, 5, { 118, 119 },           2, DS_FLAG_NO_SHUFFLE, "Glacial Caves"       },
    /* Nihlathak chain — entered via Anya-portal in Harrogath after Anya-rescue quest. */
    { 109, 5, { 121, 122, 123, 124 }, 4, 0, "Nihlathak chain"       },
    /* Cow Level — portal-only from Rogue Encampment via cube recipe. */
    {   1, 5, { 39 },                 1, DS_FLAG_IS_COW, "Cow Level (portal)" },
    /* Cow-eligible partners: pure side dungeons in Act 5, no quest, no
     * waypoint. The cow SWAPS with its partner, so the partner ends up behind
     * the cow portal and its cube recipe.
     * Cellar of Pity is NOT eligible: level 114 is the Frozen River, where
     * Anya is held, and Prison of Ice is progression in the apworld. It was
     * flagged eligible under the description "whose loss is harmless", which
     * put Anya behind Wirt's leg in every other seed. */
    { 113, 5, { 114 },                1, 0, "Cellar of Pity" },
    { 115, 5, { 116 },                1, DS_FLAG_COW_ELIGIBLE, "Echo Chamber"   },
};
#define NUM_SETS  ((int)(sizeof(g_sets) / sizeof(g_sets[0])))

/* === Shuffle map: index of set -> destination set index === Built per-pool with Sattolo (no fixed points within pool). */
static int g_shuffleMap[NUM_SETS];

/* === Inverse map: destination set -> the set whose entrance leads INTO it ===
 * The shuffle is a bijection, so this answers "which door did the player come
 * in through" with NO session state. Exits are driven from this (2026-08-29):
 * the old exit path depended on g_returnSurface, which every save&quit wipes
 * (UndoEntranceShuffle) -- so a relog inside a shuffled dungeon released the
 * player onto the destination's own surface, acts away from where they
 * entered ("entered Den of Evil, exited into Act 2"). */
static int g_inverseMap[NUM_SETS];

/* === Per-frame runtime state === */
static int g_currentSetIdx     = -1;  /* which set player is in (-1 = none) */
static int g_returnSurface     = 0;   /* warp-back target on exit (0 = no pending) */
static int g_pendingWarpTarget = 0;   /* swallow next area-change matching this */

#include "d2arch_vis.g.h"

/* Landing correction (P11/M9): after our warp lands at the level's default
 * spawn spot, move the player to the warp tile that leads back to the level
 * they conceptually came from. Set alongside g_pendingWarpTarget; consumed
 * server-side in d2arch_gameloop.c. Any failure leaves the default landing. */
static int          g_pendingLandingNextTo = 0;
volatile int        g_relocateNextTo = 0;
volatile int        g_relocateLevel  = 0;
static int          g_returnEntryL1  = 0;

extern DWORD g_lastDeathTick;
static BOOL RecentlyDied(void) {
    return g_lastDeathTick != 0 && (GetTickCount() - g_lastDeathTick) < 15000;
}
static int g_esLastArea        = -1;  /* 2.x (bug D) — tick's previous area. */

/* === Maze enlargement backup (kept from prior version) === */
typedef struct {
    int   levelId;
    DWORD origRooms[3];
} MazePatch;
#define MAX_MAZE_PATCHES  96
static MazePatch g_mazePatches[MAX_MAZE_PATCHES];
static int       g_mazePatchCount = 0;

/* Permutation + hash helpers */

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

/* Set-membership helpers */

/* Returns the index in g_sets[] whose members[0] == level, or -1. */
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

/* Returns the index of the set containing level (any member), or -1. */
static int FindSetContaining(int level) {
    for (int i = 0; i < NUM_SETS; i++) {
        if (IsLevelInSet(level, i)) return i;
    }
    return -1;
}

/* Pool participant = not pinned. Pinned sets keep the identity mapping and
 * normal zone-lock rules; pooled interiors are only physically reachable
 * through whatever entrance the shuffle assigned them. */
static BOOL SetIsPooled(int setIdx) {
    if (setIdx < 0 || setIdx >= NUM_SETS) return FALSE;
    return !(g_sets[setIdx].flags & DS_FLAG_NO_SHUFFLE);
}

/* Zone-lock combined-mode exemption (used by d2arch_drawall.c). The old test
 * was `g_currentSetIdx >= 0` -- session state, wiped by every save&quit, so a
 * relog inside a shuffled zone-locked dungeon got the player kicked to town.
 * Membership in a pooled set is statically derivable and needs no state. */
static BOOL AreaInPooledShuffledSet(int area) {
    return SetIsPooled(FindSetContaining(area));
}

/* Act-town locking — prevent player from being stuck in an act-town they haven't unlocked yet. */

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
 * Returns -1 when the chain is not readable (load window): the caller must
 * SKIP the town-lock check that tick. The old "fail closed to act 1" could
 * warp a legitimate act-3+ character toward Act 1 town while the quest
 * pointers were still settling. Failing open for one tick is harmless -- the
 * next tick re-checks. */
static int GetHighestUnlockedAct(void) {
    if (!g_cachedPGame || !fnGetQuestState) return -1;  /* unknown -> skip */
    void* pQuestFlags = NULL;
    __try {
        DWORD pQuestControl = *(DWORD*)(g_cachedPGame + 0x10F4);
        if (!pQuestControl) return -1;
        pQuestFlags = *(void**)(pQuestControl + 0x0C);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
    if (!pQuestFlags) return -1;

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

/* Returns town level-id to warp to if currentArea is a forbidden town, or 0 if no warp needed. */
static int CheckActTownLock(int currentArea) {
    if (!IsTown((DWORD)currentArea)) return 0;
    int townAct = GetActForArea(currentArea);
    if (townAct <= 1) return 0;  /* Act 1 town is always allowed */
    int maxUnlocked = GetHighestUnlockedAct();
    if (maxUnlocked < 0) return 0;  /* quest flags unreadable -> don't warp on a guess */
    if (townAct <= maxUnlocked) return 0;
    return TownForAct(maxUnlocked);
}

/* Maze room enlargement (Hell-size everywhere) Same as prior version — works fine, gives bigger dungeons. */
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

/* Apply — build the shuffle map + enlarge mazes. */
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

    /* Build pool index lists. */
    int poolA[NUM_SETS], poolB[NUM_SETS];
    int cowIdx = -1;
    int cowEligibleIdxs[NUM_SETS];
    int numCowEligible = 0;
    int countA = 0, countB = 0;
    for (int i = 0; i < NUM_SETS; i++) {
        if (g_sets[i].flags & DS_FLAG_IS_COW)        cowIdx = i;
        if (g_sets[i].flags & DS_FLAG_COW_ELIGIBLE)  cowEligibleIdxs[numCowEligible++] = i;
        if (g_sets[i].flags & DS_FLAG_NO_SHUFFLE) {
            /* pinned quest-critical set: keeps the identity mapping from the default init below, participates in no pool. */
            Log("ENTRANCE SHUFFLE: '%s' pinned to vanilla entrance (quest-critical)\n",
                g_sets[i].name);
            continue;
        }
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

    /* Pool B with Cow Level constraint: 1. */
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

    /* Build the inverse map (destination -> source). A permutation gives every
     * set exactly one inbound entrance; exits are derived from this. */
    for (int i = 0; i < NUM_SETS; i++) g_inverseMap[i] = i;
    for (int i = 0; i < NUM_SETS; i++) g_inverseMap[g_shuffleMap[i]] = i;

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

    /* maze-room enlargement is DISABLED (was: force Normal/NM room counts up to the Hell values whenever entrance shuffle was on). */
    g_mazePatchCount = 0;
    if (0) EnlargeMazeRooms(0, 0);   /* kept for a future MonDen-aware version */

    g_entranceShuffleApplied = TRUE;
    Log("ENTRANCE SHUFFLE: applied (effective seed=%u, %d sets, %d maze patches)\n",
        effectiveSeed, NUM_SETS, g_mazePatchCount);
}

/* Undo — clear runtime state + revert maze enlargement. */
static void UndoEntranceShuffle(void) {
    if (!g_entranceShuffleApplied) return;
    g_currentSetIdx = -1;
    g_returnSurface = 0;
    g_pendingWarpTarget = 0;
    g_esLastArea = -1;   /* 2.x (bug C/D) — forget the previous character's area so the new character's first tick detects its load area (runs the act-town-lock check + doesn't swallow the first shuffled entrance). */

    DWORD dt = GetSgptDT();
    if (dt && g_mazePatchCount > 0) {
        /* SEH net around the WHOLE revert (was: only the two header reads). */
        __try {
            DWORD pLvlMazeTxt = *(DWORD*)(dt + DT_LVLMAZE_PTR);
            int   nMazeCount  = *(int*)(dt + DT_LVLMAZE_COUNT);
            if (pLvlMazeTxt && nMazeCount > 0) {
                DWORD bytes = (DWORD)nMazeCount * LMAZE_SIZE;
                DWORD oldProt;
                if (VirtualProtect((void*)pLvlMazeTxt, bytes, PAGE_READWRITE, &oldProt)) {
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
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Log("ENTRANCE SHUFFLE: undo skipped — maze data already torn down (save&quit race)\n");
        }
    }

    Log("ENTRANCE SHUFFLE: undone (%d maze patches reverted)\n", g_mazePatchCount);
    g_mazePatchCount = 0;
    g_entranceShuffleApplied = FALSE;
}

/* Tick — called per-frame from d2arch_drawall.c after the zone-locking enforcement block. */
static void EntranceShuffle_Tick(void) {
    if (!g_entranceShuffleEnabled || !g_entranceShuffleApplied) return;

    int newArea = GetCurrentArea();
    if (newArea <= 0) return;
    if (newArea == g_esLastArea) return;

    int prevArea = g_esLastArea;
    g_esLastArea = newArea;

    /* Suppress the area-change that's the result of OUR own warp */
    if (newArea == g_pendingWarpTarget) {
        g_pendingWarpTarget = 0;
        if (g_pendingLandingNextTo > 0) {
            g_relocateLevel  = newArea;
            g_relocateNextTo = g_pendingLandingNextTo;
            g_pendingLandingNextTo = 0;
        }
        Log("ENTRANCE SHUFFLE TICK: ignoring area-change to %d (our own warp landing)\n", newArea);
        return;
    }

    /* Act-town lock: catches death-respawn into a higher-act town than the player has unlocked. */
    {
        int kickTo = CheckActTownLock(newArea);
        if (kickTo > 0 && g_pendingZoneTeleport == 0) {
            /* P4 (Maegis, Cain-quest kill): this lock used to run BEFORE the
             * CASE A town-portal branch below and CLEAR the return context —
             * so a player who town-portalled out of a cross-act shuffled
             * dungeon (D2 portals to the town of the act you physically stand
             * in) got Act2-town -> kicked to Act1-town, and the recorded way
             * back to the original entrance was wiped. If we still hold a
             * pending return surface, THAT is where the player belongs: it is
             * by definition an area they walked to themselves, in an unlocked
             * act. The lock stays for the plain death-respawn case. */
            if (g_currentSetIdx >= 0 && g_returnSurface > 0) {
                kickTo = g_returnSurface;
                g_pendingLandingNextTo = g_returnEntryL1;
                Log("ENTRANCE SHUFFLE TICK: locked town %d but a shuffled-set "
                    "return is pending -> sending player to the ORIGINAL "
                    "entrance surface %d instead of town\n", newArea, kickTo);
                ShowNotify("Returning to where you entered");
            } else if (prevArea > 0 && prevArea != newArea && !IsTown((DWORD)prevArea)
                       && !RecentlyDied()) {
                /* P25: a town-portal into a locked act's town used to strand
                 * the player in another town with the portal pair out of
                 * reach. Send them back where they stood instead. Death
                 * respawns keep the old town kick. */
                kickTo = prevArea;
                g_pendingLandingNextTo = 0;
                Log("ENTRANCE SHUFFLE TICK: locked town %d, no set context -> "
                    "bouncing back to previous area %d\n", newArea, kickTo);
                ShowNotify("That town's act is still locked — sending you back");
            } else {
                g_pendingLandingNextTo = 0;
                Log("ENTRANCE SHUFFLE TICK: town %d (act %d) not yet unlocked (max unlocked=%d) "
                    "-> kicking to town %d\n",
                    newArea, GetActForArea(newArea), GetHighestUnlockedAct(), kickTo);
                ShowNotify("You haven't unlocked this act yet — returning home");
            }
            g_pendingZoneTeleport = kickTo;
            g_pendingWarpTarget = kickTo;
            /* Clear cave-context state since the trip is aborted */
            g_currentSetIdx = -1;
            g_returnSurface = 0;
            return;
        }
    }


    /* STATELESS EXIT (2026-08-29, replaces the g_returnSurface exit). The
     * shuffle map is a bijection, so the set the player just left plus the
     * INVERSE map name the one entrance that leads in -- no session state
     * needed. The old path depended on g_returnSurface, which every
     * save&quit wipes, so a relog inside a shuffled dungeon released the
     * player onto the destination's own surface ("entered Den of Evil,
     * exited into Act 2"). Guards:
     *  - the transition must come FROM inside the set (the old check had no
     *    prevArea test: TP to town + a stroll onto the destination's own
     *    surface teleported the player cross-act mid-walk);
     *  - a fresh death respawn into a parent-TOWN (A2 Sewers/Cow/Nihlathak
     *    exits) is not an exit -- the player wants their corpse run. */
    {
        int physSet = FindSetContaining(prevArea);
        if (physSet >= 0 && newArea == g_sets[physSet].surfaceParent &&
            g_inverseMap[physSet] != physSet &&
            !(IsTown((DWORD)newArea) && RecentlyDied())) {
            int src      = g_inverseMap[physSet];
            int returnTo = g_sets[src].surfaceParent;
            if (returnTo == newArea) {
                /* Source entrance sits on the SAME surface we just landed on
                 * (the Tal Rasha tombs all share Canyon of the Magi). No warp
                 * -- just move the player to the door they actually entered
                 * through. */
                g_relocateLevel  = newArea;
                g_relocateNextTo = g_sets[src].members[0];
                Log("ENTRANCE SHUFFLE TICK: exit of '%s' onto shared surface %d "
                    "-> repositioning at '%s' entrance (L%d)\n",
                    g_sets[physSet].name, newArea,
                    g_sets[src].name, g_sets[src].members[0]);
            } else {
                /* This OVERRIDES any zone-lock teleport queued this same tick
                 * (P4): the return target is by construction the surface whose
                 * entrance the player used -- self-visited, unlocked. */
                if (g_pendingZoneTeleport != 0 && g_pendingZoneTeleport != returnTo)
                    Log("ENTRANCE SHUFFLE TICK: overriding queued zone-lock teleport "
                        "%d with shuffled-set return %d\n", g_pendingZoneTeleport, returnTo);
                g_pendingZoneTeleport  = returnTo;
                g_pendingWarpTarget    = returnTo;
                g_pendingLandingNextTo = g_sets[src].members[0];
                Log("ENTRANCE SHUFFLE TICK: exit of '%s' (surface %d) -> its "
                    "entrance is '%s''s, returning to surface %d\n",
                    g_sets[physSet].name, g_sets[physSet].surfaceParent,
                    g_sets[src].name, returnTo);
            }
            g_returnSurface = 0;
            g_currentSetIdx = -1;
            return;
        }
    }

    /* CASE A: player is currently inside a shuffled dungeon set (context is
     * kept for town-portal round-trips and logging; exits no longer need it). */
    if (g_currentSetIdx >= 0) {
        if (IsLevelInSet(newArea, g_currentSetIdx)) {
            /* Internal navigation, no action */
            return;
        }
        /* 2.x (bug B) — going to TOWN from a dungeon is only possible via a town portal, i.e. */
        /* V10 (#12) — the town guard must NOT swallow a set whose natural surface parent IS a town. */
        if (IsTown((DWORD)newArea) && newArea != g_sets[g_currentSetIdx].surfaceParent) {
            Log("ENTRANCE SHUFFLE TICK: '%s' -> town %d via portal; keeping set context\n",
                g_sets[g_currentSetIdx].name, newArea);
            return;
        }
        /* Left the set some other way (death respawn into the parent town,
         * identity-mapped exit, aborted trip) -- just drop the context. */
        Log("ENTRANCE SHUFFLE TICK: left set '%s' to area %d -> clearing state\n",
            g_sets[g_currentSetIdx].name, newArea);
        g_returnSurface = 0;
        g_currentSetIdx = -1;
        return;
    }

    /* CASE B: player is NOT in any shuffled set. */
    int sourceSetIdx = FindSetByEntryLevel(newArea);
    if (sourceSetIdx < 0) return;  /* not a cave entry — ignore */

    /* 2.x (bug B, belt-and-suspenders) — arriving at a cave entry FROM TOWN is
     * a town-portal return, not a fresh entrance... UNLESS that town IS the
     * set's own natural parent (2026-08-29): A2 Sewers (trapdoor in Lut
     * Gholein), the Cow portal (Rogue Encampment) and the Nihlathak chain
     * (Anya portal in Harrogath) are entered FROM a town by design. The old
     * unconditional guard swallowed their fresh entries, so those three never
     * shuffled -- and their mapped partners lost their only inbound entrance:
     * Echo Chamber's interior (q432 + a Hunt superunique) was unreachable in
     * EVERY entrance-shuffle seed. Real TP returns are safe: while a portal
     * exists the set context is still alive (CASE A keeps it on TP-to-town),
     * so this branch only runs after a context loss -- and no town portal
     * survives a relog. */
    if (IsTown((DWORD)prevArea) && prevArea != g_sets[sourceSetIdx].surfaceParent) {
        g_currentSetIdx = sourceSetIdx;
        g_returnSurface = g_sets[sourceSetIdx].surfaceParent;
        Log("ENTRANCE SHUFFLE TICK: entry to '%s' from town %d (portal return) -> no redirect\n",
            g_sets[sourceSetIdx].name, prevArea);
        return;
    }

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
    g_pendingLandingNextTo = g_sets[destSetIdx].surfaceParent;
    g_returnSurface = g_sets[sourceSetIdx].surfaceParent;
    g_returnEntryL1 = g_sets[sourceSetIdx].members[0];
    g_currentSetIdx = destSetIdx;
    Log("ENTRANCE SHUFFLE TICK: entry to L%d ('%s' from area %d) -> warp to "
        "L%d ('%s'), return to surface %d\n",
        newArea, g_sets[sourceSetIdx].name, prevArea,
        destL1, g_sets[destSetIdx].name, g_returnSurface);
}
