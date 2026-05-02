/* ================================================================
 * D2Archipelago - F1 Collection Page (1.9.0 NEW)
 *
 * Read-only "Pokédex"-style tracker. F1 page 3 (after Skill Editor
 * and Quest Book) shows every collectible item in the game as
 * either a translucent ghost (un-collected) or a solid icon with a
 * "Collected" badge.
 *
 * Spec: Research/SYSTEM_2_F1_COLLECTION_PAGE_2026-04-30.md
 * Locked decisions: feedback_collection_design.md
 *
 * Categories:
 *   - Sets         (32 vanilla sets, ~127 set-pieces)
 *   - Runes        (33 runes r01..r33)
 *   - Gems         (35 gems = 7 colors × 5 grades)
 *   - Special      (Pandemonium keys/organs/essences, Hellfire Torch,
 *                   Token of Absolution)
 *   - Gold         (lifetime-earned counter — pickup + quest reward
 *                   only, NOT vendor sale revenue)
 *
 * Persistence is per-character: ap_collections_<charname>.dat.
 * Goal=3 (Collection-as-Goal) mode is layered on top — optional.
 * ================================================================ */
#ifndef D2ARCH_COLLECTIONS_H
#define D2ARCH_COLLECTIONS_H

#include <windows.h>
#include <stdint.h>

/* ----------------------------------------------------------------
 * SLOT INDEX MAP
 * ----------------------------------------------------------------
 *   0   .. 126  set-pieces (variable per set; total ~127)
 *   127 .. 159  runes (33 entries r01..r33)
 *   160 .. 194  gems  (35 entries: 7 colors × 5 grades)
 *   195 .. 204  special items (10 slots reserved)
 *
 * Total = COLL_NUM_SLOTS. Bumping a category means re-numbering the
 * slot indices, so we use compile-time constants and let the
 * compiler check.
 * ---------------------------------------------------------------- */
#define COLL_NUM_SETS            32
#define COLL_NUM_SET_PIECES      127  /* sum of pieces across all 32 sets */
#define COLL_NUM_RUNES           33
#define COLL_NUM_GEMS            35
#define COLL_NUM_SPECIALS        10

#define COLL_SLOT_SETS_BASE      0
#define COLL_SLOT_RUNES_BASE     (COLL_SLOT_SETS_BASE  + COLL_NUM_SET_PIECES)  /* 127 */
#define COLL_SLOT_GEMS_BASE      (COLL_SLOT_RUNES_BASE + COLL_NUM_RUNES)       /* 160 */
#define COLL_SLOT_SPECIALS_BASE  (COLL_SLOT_GEMS_BASE  + COLL_NUM_GEMS)        /* 195 */
#define COLL_NUM_SLOTS           (COLL_SLOT_SPECIALS_BASE + COLL_NUM_SPECIALS) /* 205 */

/* Sidecar magic + version */
#define COLL_FILE_MAGIC          0xC0117EC7  /* 'COLLECT'-ish */
/* v1: original — auto-mark anything found in inventory.
 * v2 (1.9.0): two-flag detection — item must have been observed on
 *             the ground with IFLAG_NEWITEM set (Flag A: legitimate
 *             monster/object drop) AND in the player's inventory
 *             (Flag B: actually picked up). Items shared via stash
 *             or dropped by other players no longer auto-mark.
 *             Sidecars loaded with version=1 trigger a one-time
 *             grace scan (legacy behavior) so existing collections
 *             aren't silently broken; sidecar is then upgraded to v2. */
#define COLL_FILE_VERSION        2

/* Tab/category index used by the F1 page renderer. */
typedef enum {
    COLL_CAT_SETS = 0,
    COLL_CAT_RUNES,
    COLL_CAT_GEMS,
    COLL_CAT_SPECIAL,
    COLL_CAT_GOLD,
    COLL_CAT_COUNT
} CollectionCategory;

/* ----------------------------------------------------------------
 * STATIC ITEM DEFINITIONS
 * ----------------------------------------------------------------
 * Authored once in d2arch_collections.c. The runtime never modifies
 * these; only the CollectionState (per-character) is mutable.
 * ---------------------------------------------------------------- */
typedef struct {
    char        code[5];        /* item code, e.g. "r29" "Ams" "amu". 4 chars + NUL */
    const char* displayName;    /* "Vex Rune", "Sigon's Visor of Power" */
    const char* dropSource;     /* "Hell-difficulty bosses + Hellforge" */
    int         reqLevel;       /* required character level to use */
    const char* invFile;        /* DC6 file basename for ghost render, e.g. "invrun26" */
} CollectionItem;

/* A vanilla set is a list of pieces. Sets vary in size (3..6 pieces). */
typedef struct {
    const char*           name;          /* "Sigon's Complete Steel" */
    const char*           classLock;     /* NULL = any class; else "Paladin" etc. */
    int                   firstSlot;     /* COLL_SLOT_SETS_BASE-relative index of first piece */
    int                   pieceCount;    /* number of pieces in this set */
    const CollectionItem* pieces;        /* pointer into a flat item table */
} CollectionSet;

/* Static read-only catalog tables — defined in d2arch_collections.c */
extern const CollectionSet  g_collSets[COLL_NUM_SETS];
extern const CollectionItem g_collSetPieces[COLL_NUM_SET_PIECES];
extern const CollectionItem g_collRunes[COLL_NUM_RUNES];
extern const CollectionItem g_collGems[COLL_NUM_GEMS];
extern const CollectionItem g_collSpecials[COLL_NUM_SPECIALS];

/* ----------------------------------------------------------------
 * PER-CHARACTER STATE (mutable, persisted)
 * ----------------------------------------------------------------
 * Sticky bitset: once collected, a slot stays collected forever.
 * Selling or dropping the item does not unflag.
 *
 * Timestamps store first-found Unix time (uint32_t). 0 = never found.
 *
 * Gold counter is monotonic — only increases. Counts gold pickup
 * from the ground and quest-reward gold; ignores vendor-sale revenue.
 * ---------------------------------------------------------------- */
typedef struct {
    uint32_t magic;                          /* COLL_FILE_MAGIC */
    uint32_t version;                        /* COLL_FILE_VERSION */
    uint64_t goldEarned;                     /* lifetime pickup + quest reward, monotonic */
    uint8_t  flags[(COLL_NUM_SLOTS + 7)/8];  /* bit-packed collected flags */
    uint32_t timestamps[COLL_NUM_SLOTS];     /* first-found Unix time per slot */
    uint8_t  reserved[64];                   /* future-proofing */
} CollectionState;

/* Single global state, owned by d2arch_collections.c. */
extern CollectionState g_collState;

/* ----------------------------------------------------------------
 * GOAL=3 MODE STATE (optional)
 * ----------------------------------------------------------------
 * If g_apGoal == GOAL_COLLECTION (3), the player picks a subset of
 * sets/runes/gems/specials/gold-target as their win condition.
 * Choice freezes at character creation per feedback_settings_isolation.
 * ---------------------------------------------------------------- */
#define GOAL_COLLECTION          3   /* extends the existing 0=Normal/1=NM/2=Hell */

typedef struct {
    uint8_t  setsTargeted[COLL_NUM_SETS];          /* 1 = needed for goal */
    uint8_t  allRunesTargeted;                     /* legacy: 1 = need all 33 runes
                                                    * (kept for v1 sidecar compat;
                                                    * 1.9.0 reads runesTargeted[] instead) */
    uint8_t  allGemsTargeted;                      /* 1 = need all 35 gems */
    uint8_t  specialsTargeted[COLL_NUM_SPECIALS];  /* 1 per special needed */
    uint64_t goldTarget;                           /* 0 = gold not part of goal */
    uint8_t  runesTargeted[COLL_NUM_RUNES];        /* 1.9.0 — granular rune targeting */
    uint8_t  reserved[16];
} CollectionGoalConfig;

extern CollectionGoalConfig g_collGoal;

/* ----------------------------------------------------------------
 * PUBLIC API
 * ---------------------------------------------------------------- */

/* One-time init at DLL load. Validates the static catalog tables.
 * Returns FALSE if the catalog is malformed (ship-blocker). */
BOOL Coll_Init(void);

/* Per-character lifecycle. Mirrors the StkLoadAP / StkSaveAP pattern. */
void Coll_LoadForCharacter(const char* charName);  /* call from OnCharacterLoad */
void Coll_SaveForCharacter(const char* charName);  /* call on unload + on flag flip */
void Coll_ResetOnPlayerGone(void);                 /* clear g_collState in memory */

/* Slot flag access. Returns/sets bit at index (0..COLL_NUM_SLOTS-1). */
BOOL Coll_IsSlotCollected(int slotIdx);
void Coll_MarkSlotCollected(int slotIdx);  /* stamps timestamp + sets flag + saves */

/* Lookup helpers — find slotIdx by item code, or return -1. */
int  Coll_FindSlotByCode(const char* code4);

/* Detection: scans inventory, equipped, belt, all stash sidecars,
 * and merc inventory for collected codes. Sets flags + timestamps.
 * Called from OnCharacterLoad after Coll_LoadForCharacter, and
 * polled on item-pickup events. */
void Coll_ScanPlayerHoldings(void* pPlayerUnit);

/* 1.9.0 Phase 6 — grandfather helper. Used by the stash-load
 * pipeline (StashSwap_SerFill in d2arch_stashlogic.c) to mark items
 * passing through stash sidecars as legitimately collected, bypassing
 * the strict two-flag GUID check. Items in the player's own stash
 * sidecars are trusted because they were placed there by the player
 * themselves at deposit time, even if the deposit happened pre-1.9.0
 * and isn't in our GUID classification table. */
void Coll_GrandfatherInvItem(void* pItem);

/* Gold counter — called from the gold delta-watcher.
 * `delta` is positive (only increase events forwarded).
 * The watcher already filters out vendor-UI cases. */
void Coll_AddGoldEarned(uint64_t delta);

/* Goal mode — returns TRUE iff Goal=3 is set AND every targeted
 * collection in g_collGoal is now flagged complete. */
BOOL Coll_IsGoalComplete(void);

/* F1 page rendering entry point — called from the F1 panel handler
 * when the active page index is 3. Implementation in
 * d2arch_collections.c (or a dedicated _ui.c if it grows). */
void Coll_RenderF1Page(int mouseX, int mouseY, BOOL clicked);

/* Per-tick entry point — called from ProcessPendingGameTick.
 * Throttles its own inventory scans (~2s interval) and polls gold
 * delta every tick. Caller passes the CLIENT player unit. */
void Coll_OnGameTick(void* pPlayerUnit);

/* Reset tick-state when the character unloads, so gold delta and
 * scan throttling start clean for the next character. */
void Coll_ResetTickState(void);

/* Drop cached DC6 cel pointers so a different character's F1 page
 * loads fresh icons. Called from Coll_ResetOnPlayerGone in v1.
 * Cels themselves are owned by D2's cel manager and freed by D2. */
void Coll_ResetCelCache(void);

#endif /* D2ARCH_COLLECTIONS_H */
