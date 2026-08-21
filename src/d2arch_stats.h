/* d2arch_stats.h — F1 Statistics / Logbook page (1.9.0) Per-character + lifetime stat tracking. */
#ifndef D2ARCH_STATS_H
#define D2ARCH_STATS_H

#include <stdint.h>
#include <windows.h>

/* Sidecar magic + version (per-char and lifetime use the same magic but distinguishable file names). */
#define STATS_FILE_MAGIC          0x57415453  /* 'STAW' */
#define STATS_FILE_VERSION        1

/* Difficulty count (Normal / Nightmare / Hell). */
#define STATS_DIFF_COUNT          3
/* Act count (1..5). The arrays are 0-based so [0]=Act1 .. [4]=Act5. */
#define STATS_ACT_COUNT           5

/* Per-character stat block. */
typedef struct {
    uint32_t magic;                           /* STATS_FILE_MAGIC */
    uint32_t version;                         /* STATS_FILE_VERSION */

    /* Time */
    uint64_t playtimeSec;                     /* total seconds spent in-game on this char */
    uint64_t firstSeenUnix;                   /* Unix timestamp of char creation (first scan) */
    uint64_t lastSeenUnix;                    /* Unix timestamp of last save */

    /* Combat */
    uint64_t monstersKilled;
    uint64_t championsKilled;
    uint64_t uniquesKilled;                   /* unique-type monster category */
    uint64_t bossesKilled;                    /* the "tracked" big bosses */
    uint64_t superuniquesKilled;              /* SuperUniques (Bishibosh, etc.) */
    uint64_t playerDeaths;
    uint64_t monstersByDifficulty[STATS_DIFF_COUNT];

    /* Loot */
    uint64_t itemsPickedUp;                   /* count of inv-items first-seen */
    uint64_t setItemsPickedUp;                /* subset: set-quality items */
    uint64_t uniqueItemsPickedUp;             /* subset: unique-quality items */
    uint64_t runesPickedUp;                   /* r01..r33 */
    uint64_t gemsPickedUp;                    /* gcv/gcy/.../skz */
    uint64_t goldCollected;                   /* lifetime gold pickup (mirrors Coll's counter) */
    uint64_t goldByAct[STATS_ACT_COUNT + 1];  /* index 1..5; [0] = town/menu/unknown */

    /* Vendor / economy (TODO — needs vendor hook) */
    uint64_t goldSpent;                       /* TODO */
    uint64_t itemsSoldToVendor;               /* TODO */
    uint64_t itemsBoughtFromVendor;           /* TODO */
    uint64_t gambledItems;                    /* TODO */

    /* Cube / crafting */
    uint64_t cubeTransmutes;                  /* every successful transmute */
    uint64_t runewordsCreated;                /* TODO — detect by item flag transition */
    uint64_t itemsIdentified;                 /* TODO — Cain talk / scroll consume */

    /* Movement / exploration */
    uint64_t waypointsUsed;                   /* operateFn 23 */
    uint64_t portalsTaken;                    /* operateFn 15/27/34/43/44/46/70/72/73 */
    uint64_t doorsOpened;                     /* operateFn 8/18/29 */
    uint64_t chestsOpened;                    /* operateFn 1/4/19/20/26/30/39/40/41/51/57/58/59 */
    uint64_t barrelsBroken;                   /* operateFn 5/7 */
    uint64_t jarsBroken;                      /* operateFn 3 (urns/baskets/jars) */
    uint64_t shrinesActivated;                /* operateFn 2 */
    uint64_t wellsDrunk;                      /* operateFn 22 */
    uint64_t corpsesLooted;                   /* operateFn 14 (looting Fallen-style corpses) */
    uint64_t monolithsActivated;              /* operateFn 9 (the "stone-flipping" objects) */
    uint64_t questObjectsInteracted;          /* operateFn 10/12/17/21/24/25/28/31/33/42/45/48/49/52-56/61/65/67/68/71 */

    /* Quests / progression */
    uint64_t questsCompleted;
    /* Time-to-complete each act (seconds since char creation). */
    uint32_t actCompleteSec[STATS_DIFF_COUNT][STATS_ACT_COUNT];

    /* Consumables (TODO — needs item-use hook) */
    uint64_t potionsConsumed;
    uint64_t scrollsUsed;
    uint64_t tomesUsed;

    /* Misc */
    uint64_t cowLevelEntries;                 /* TODO — area-id transition */
    uint64_t tristramVisits;                  /* TODO — area-id transition */
    uint64_t pandemoniumRunsCompleted;        /* TODO — finale uber 3-set drops */

    uint8_t  reserved[256];                   /* future-proofing */
} CharStats;

/* Lifetime aggregate block — same shape as CharStats but the values sum every char's stats. */
typedef struct {
    uint32_t magic;
    uint32_t version;

    uint64_t charactersPlayed;                /* number of unique chars seen */
    uint64_t totalPlaytimeSec;
    uint64_t totalMonstersKilled;
    uint64_t totalChampionsKilled;
    uint64_t totalUniquesKilled;
    uint64_t totalBossesKilled;
    uint64_t totalSuperuniquesKilled;
    uint64_t totalPlayerDeaths;

    uint64_t totalItemsPickedUp;
    uint64_t totalSetItemsPickedUp;
    uint64_t totalUniqueItemsPickedUp;
    uint64_t totalRunesPickedUp;
    uint64_t totalGemsPickedUp;
    uint64_t totalGoldCollected;
    uint64_t totalGoldByAct[STATS_ACT_COUNT + 1];

    uint64_t totalGoldSpent;
    uint64_t totalItemsSoldToVendor;
    uint64_t totalItemsBoughtFromVendor;
    uint64_t totalGambledItems;

    uint64_t totalCubeTransmutes;
    uint64_t totalRunewordsCreated;
    uint64_t totalItemsIdentified;

    uint64_t totalWaypointsUsed;
    uint64_t totalPortalsTaken;
    uint64_t totalDoorsOpened;
    uint64_t totalChestsOpened;
    uint64_t totalBarrelsBroken;
    uint64_t totalJarsBroken;
    uint64_t totalShrinesActivated;
    uint64_t totalWellsDrunk;
    uint64_t totalCorpsesLooted;
    uint64_t totalMonolithsActivated;
    uint64_t totalQuestObjectsInteracted;

    uint64_t totalQuestsCompleted;
    /* Best (fastest) act completion times across every character. */
    uint32_t bestActSec[STATS_DIFF_COUNT][STATS_ACT_COUNT];

    uint64_t totalPotionsConsumed;
    uint64_t totalScrollsUsed;
    uint64_t totalTomesUsed;

    uint64_t totalCowLevelEntries;
    uint64_t totalTristramVisits;
    uint64_t totalPandemoniumRunsCompleted;

    uint8_t  reserved[512];
} LifetimeStats;

/* Globals defined in d2arch_stats.c. */
extern CharStats     g_charStats;
extern LifetimeStats g_lifetimeStats;

/* API */

/* Persistence. Per-char file: ap_stats_<charname>.dat in Game/Archipelago/. Lifetime file: ap_stats_lifetime.dat (account-wide). */
void Stats_LoadForCharacter(const char* charName);
void Stats_SaveForCharacter(const char* charName);
void Stats_LoadLifetime(void);
void Stats_SaveLifetime(void);

/* Aggregate the current char's stats into the lifetime totals. */
void Stats_AggregateLifetime(const char* charName);

/* Per-tick driver. Increments playtime, polls for events the tick itself can detect (player death, area changes for goldByAct, act-completion timestamps). Called from Coll_OnGameTick. */
void Stats_OnGameTick(void* pPlayerUnit);

/* Event hooks — called from elsewhere in the codebase. */
void Stats_OnMonsterKill(int txtId, int typeFlag, int currentArea, int currentDifficulty);
void Stats_OnPlayerDeath(void);
void Stats_OnQuestComplete(void);
void Stats_OnCubeTransmute(void);
void Stats_OnGoldDelta(int64_t delta, int currentArea);
void Stats_OnItemPickedUp(int quality, int slotIdx);  /* slotIdx is the Coll slot if any */
void Stats_OnActComplete(int difficulty, int actIdx);
void Stats_OnPandemoniumRunComplete(void);
void Stats_OnAreaEnter(int newAreaId);  /* called from Stats_OnGameTick on area change */
void Stats_OnRunewordCreated(void);
void Stats_OnItemIdentified(void);

/* F1 page render (g_editorPage == 8). */
void Stats_RenderF1Page(int mouseX, int mouseY, BOOL clicked);

/* Reset state on character change so a fresh char starts cleanly. */
void Stats_OnCharacterChange(void);

/* Mouse-wheel / arrow-button scroll input. */
void Stats_HandleScroll(int side, int delta);

#endif /* D2ARCH_STATS_H */
