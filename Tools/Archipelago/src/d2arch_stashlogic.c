/* ================================================================
 * D2Archipelago - Stash Logic (Phase 3: stacking + auto-routing)
 *
 * This module adds behavior on top of the storage layer defined in
 * d2arch_stash.{c,h}:
 *
 *   - A runtime whitelist of stackable item-class ids. The actual
 *     mapping from D2 item-type codes (misc.txt / itemtypes.txt)
 *     to class ids is not wired up yet — external code (or the
 *     eventual item-integration pass) will call
 *     StashLogicRegisterStackable() to populate the whitelist.
 *   - A "stacking tab" (global tab index 9, the last AP tab) that
 *     receives consolidated virtual stacks of stackable items.
 *   - A polling insertion scanner that compares a snapshot of slot
 *     state against the live tabs and routes newly inserted items.
 *
 * D2-side item-insertion hooks are NOT yet implemented — the code
 * below is scaffolding that compiles and is safe to call every
 * frame. Every function that relies on reading the actual item
 * blob is guarded with a TODO and short-circuits cleanly.
 *
 * BUILD NOTE — unity-build integration:
 * This file is #included by d2arch.c AFTER d2arch_stash.c, so it
 * can reference the Phase 1 globals and helpers directly (see the
 * header comment in d2arch_stash.c). No `extern`s.
 * ================================================================ */

/* ----------------------------------------------------------------
 * TUNABLES
 * ---------------------------------------------------------------- */
#define STASHLOGIC_STACK_TAB_GLOBAL     9    /* last AP tab (pre-goal accessible) */
#define STASHLOGIC_STACKABLE_MAX        64   /* registered whitelist capacity */

/* ----------------------------------------------------------------
 * LOCAL STATE
 * ---------------------------------------------------------------- */
static DWORD s_stackableIds[STASHLOGIC_STACKABLE_MAX];
static int   s_stackableCount = 0;

/* Snapshot of previous stash state for the insertion scanner. We
 * stage the entire AP + shared pool in this buffer so the diff can
 * be computed against `g_stashAP` and `g_stashShared` without
 * touching the live tabs. */
static StashTab s_stashSnapshotAP[STASH_NUM_AP_TABS];
static StashTab s_stashSnapshotShared[STASH_NUM_SHARED_TABS];
static DWORD    s_stashSnapshotHash = 0;
static BOOL     s_stashSnapshotValid = FALSE;

/* ----------------------------------------------------------------
 * INTERNAL HELPERS
 * ---------------------------------------------------------------- */

/* Fast rolling XOR hash over all slot occupied+stack fields. Used as
 * a cheap change detector — if the hash is unchanged, nothing has
 * changed in slot occupancy/counts since the last scan. */
static DWORD ComputeLiveStashHash(void) {
    DWORD h = 0;
    int t, s;
    for (t = 0; t < STASH_NUM_AP_TABS; t++) {
        for (s = 0; s < STASH_SLOTS_PER_TAB; s++) {
            StashSlot* sl = &g_stashAP[t].slots[s];
            h ^= (DWORD)sl->occupied;
            h  = (h << 1) | (h >> 31);
            h ^= sl->stack.itemClassId;
            h  = (h << 1) | (h >> 31);
            h ^= (DWORD)sl->stack.count;
            h  = (h << 1) | (h >> 31);
        }
    }
    for (t = 0; t < STASH_NUM_SHARED_TABS; t++) {
        for (s = 0; s < STASH_SLOTS_PER_TAB; s++) {
            StashSlot* sl = &g_stashShared[t].slots[s];
            h ^= (DWORD)sl->occupied;
            h  = (h << 1) | (h >> 31);
            h ^= sl->stack.itemClassId;
            h  = (h << 1) | (h >> 31);
            h ^= (DWORD)sl->stack.count;
            h  = (h << 1) | (h >> 31);
        }
    }
    return h;
}

/* Copy the live stash into the snapshot buffer and refresh the hash.
 * Called whenever the scanner is about to declare "no pending
 * changes" — the current state becomes the new baseline. */
static void TakeSnapshot(void) {
    memcpy(s_stashSnapshotAP,     g_stashAP,     sizeof(s_stashSnapshotAP));
    memcpy(s_stashSnapshotShared, g_stashShared, sizeof(s_stashSnapshotShared));
    s_stashSnapshotHash  = ComputeLiveStashHash();
    s_stashSnapshotValid = TRUE;
}

/* Placeholder for the real D2 item-class lookup. Returns FALSE until
 * the item-blob integration lands. The external classId is still
 * passed through so callers can build registration logic today. */
static BOOL IsStackableHeuristic(DWORD itemClassId) {
    /* TODO: when the real D2 item-type enum is wired through the
     *       item-blob integration, map the following groups to
     *       stackable:
     *         - Runes        (itype: rune)
     *         - Gems         (all quality: cgem/ogem/fgem/gem/pgem)
     *         - Jewels       (jewl)
     *         - Town Portal  (tsc)
     *         - Identify     (isc)
     *         - Arrows       (aqv)
     *         - Bolts        (cqv)
     *       Charms (cm1/cm2/cm3) MUST remain non-stackable.
     */
    (void)itemClassId;
    return FALSE;
}

/* ----------------------------------------------------------------
 * PUBLIC API
 * ---------------------------------------------------------------- */

void StashLogicInit(void) {
    memset(s_stackableIds, 0, sizeof(s_stackableIds));
    s_stackableCount = 0;
    memset(s_stashSnapshotAP,     0, sizeof(s_stashSnapshotAP));
    memset(s_stashSnapshotShared, 0, sizeof(s_stashSnapshotShared));
    s_stashSnapshotHash  = 0;
    s_stashSnapshotValid = FALSE;
    Log("StashLogic: initialized (stacking tab=global %d, cap=%d)\n",
        STASHLOGIC_STACK_TAB_GLOBAL, STASHLOGIC_STACKABLE_MAX);
}

BOOL StashLogicIsStackable(DWORD classId) {
    int i;
    for (i = 0; i < s_stackableCount; i++) {
        if (s_stackableIds[i] == classId) return TRUE;
    }
    /* Fall back to the heuristic (currently a stub returning FALSE). */
    return IsStackableHeuristic(classId);
}

void StashLogicRegisterStackable(DWORD classId) {
    int i;
    if (s_stackableCount >= STASHLOGIC_STACKABLE_MAX) {
        Log("StashLogic: whitelist full, cannot register classId=%lu\n",
            (unsigned long)classId);
        return;
    }
    for (i = 0; i < s_stackableCount; i++) {
        if (s_stackableIds[i] == classId) {
            /* Already registered — idempotent no-op. */
            return;
        }
    }
    s_stackableIds[s_stackableCount++] = classId;
    Log("StashLogic: registered stackable classId=%lu (total=%d)\n",
        (unsigned long)classId, s_stackableCount);
}

/* Try to merge one unit of classId into tab 9's virtual stacks.
 * Returns the new count on success, -1 if the class isn't stackable,
 * -2 if no free slot exists and every existing stack is full.
 *
 * If `tab` is valid and refers to a virtual stack already holding
 * this classId, we add there. Otherwise the first matching stack or
 * the first empty slot on the stacking tab receives the merge.
 * (The `tab` / `slot` args let future call sites request a specific
 * destination; current behavior is "auto-pick on tab 9".) */
int StashLogicTryStack(int tab, int slot, DWORD classId) {
    StashTab* stackTab;
    int s;
    int firstEmpty = -1;

    (void)tab; (void)slot;  /* reserved for future targeted merges */

    if (!StashLogicIsStackable(classId)) return -1;

    /* Stacking tab is AP tab 9 (global index 9 == AP pool index 9). */
    stackTab = &g_stashAP[9];

    for (s = 0; s < STASH_SLOTS_PER_TAB; s++) {
        StashSlot* sl = &stackTab->slots[s];
        if (sl->occupied == 2 &&
            sl->stack.itemClassId == classId &&
            sl->stack.count < STASH_MAX_STACK_SIZE) {
            sl->stack.count += 1;
            stackTab->lastModifiedTick = GetTickCount();
            return sl->stack.count;
        }
        if (sl->occupied == 0 && firstEmpty < 0) {
            firstEmpty = s;
        }
    }

    if (firstEmpty < 0) return -2;
    {
        StashSlot* sl = &stackTab->slots[firstEmpty];
        sl->occupied = 2;
        sl->stack.itemClassId = classId;
        sl->stack.count = 1;
        memset(sl->reserved, 0, sizeof(sl->reserved));
        stackTab->lastModifiedTick = GetTickCount();
        return 1;
    }
}

/* Withdraw `qty` units from the virtual stack at (tab, slot). Returns
 * the remaining count after withdrawal, or -1 on invalid args / non-
 * stack slot. Callers that need to spawn concrete item instances in
 * the player inventory will hook into this once item-blob creation
 * is implemented — today it just decrements the counter.
 *
 * TODO: on decrement, emit N real-item insertions into the player's
 *       open stash window (requires D2-side ITEMS_CreateItem + friends).
 */
int StashLogicWithdraw(int tab, int slot, int qty) {
    StashTab* pool;
    StashSlot* sl;
    int localTab;

    if (qty <= 0) return -1;
    if (slot < 0 || slot >= STASH_SLOTS_PER_TAB) return -1;

    if (tab >= 0 && tab < STASH_NUM_AP_TABS) {
        pool = g_stashAP;
        localTab = tab;
    } else if (tab >= STASH_NUM_AP_TABS && tab < STASH_MAX_TABS) {
        pool = g_stashShared;
        localTab = tab - STASH_NUM_AP_TABS;
    } else {
        return -1;
    }

    sl = &pool[localTab].slots[slot];
    if (sl->occupied != 2) return -1;

    if (qty >= sl->stack.count) {
        sl->occupied = 0;
        sl->stack.itemClassId = 0;
        sl->stack.count = 0;
        pool[localTab].lastModifiedTick = GetTickCount();
        return 0;
    }

    sl->stack.count -= qty;
    pool[localTab].lastModifiedTick = GetTickCount();
    return sl->stack.count;
}

/* Polling entry point — called from DrawAll every ~10 frames. When a
 * diff is detected against the snapshot, each newly-occupied slot is
 * inspected and, if stackable, rerouted to the stacking tab.
 *
 * TODO: the "is stackable" decision needs the real item-class
 *       resolver on the inserted slot. For now, we just refresh the
 *       snapshot on every change so the scanner stays quiet once
 *       the hash settles. When the item bridge exists, replace the
 *       inner loop body with a class-lookup + StashLogicTryStack. */
void StashLogicScanForInsertions(void) {
    DWORD liveHash;
    int t, s;

    if (!s_stashSnapshotValid) {
        TakeSnapshot();
        return;
    }

    liveHash = ComputeLiveStashHash();
    if (liveHash == s_stashSnapshotHash) {
        return;  /* nothing changed since last scan */
    }

    /* --- Diff pass (AP pool) --- */
    for (t = 0; t < STASH_NUM_AP_TABS; t++) {
        for (s = 0; s < STASH_SLOTS_PER_TAB; s++) {
            StashSlot* prev = &s_stashSnapshotAP[t].slots[s];
            StashSlot* cur  = &g_stashAP[t].slots[s];
            if (prev->occupied == 0 && cur->occupied == 1) {
                /* TODO: classify cur->reserved[] (contains the real
                 *       item blob once serialization lands) and, if
                 *       stackable, call
                 *         StashLogicTryStack(t, s, classId);
                 *         cur->occupied = 0;  // vacate physical slot
                 */
            }
        }
    }

    /* --- Diff pass (shared pool) --- */
    for (t = 0; t < STASH_NUM_SHARED_TABS; t++) {
        for (s = 0; s < STASH_SLOTS_PER_TAB; s++) {
            StashSlot* prev = &s_stashSnapshotShared[t].slots[s];
            StashSlot* cur  = &g_stashShared[t].slots[s];
            if (prev->occupied == 0 && cur->occupied == 1) {
                /* TODO: same rerouting logic as above, but for shared
                 *       tabs (only reachable on non-AP / post-goal). */
            }
        }
    }

    TakeSnapshot();
}

/* ================================================================
 * STASH PAGE SWAPPER (Phase 2b) — unlink/relink D2 stash items
 *
 * Strategy: when the user clicks a different tab, walk pInventory,
 * find items with page == INVPAGE_STASH, unlink them (D2 keeps the
 * UnitAny allocated in its memory pool), and store the pointers +
 * grid positions in a shadow table. Then relink items from the
 * target tab's shadow into the inventory at their stored positions.
 *
 * LIMITATION (Phase 2c TODO): cross-session persistence not wired.
 * Items on non-active tabs are lost when the character unloads (D2
 * only writes the currently-linked pInventory contents to .d2s).
 * Fix requires ITEMS_SerializeItemToBitstream (D2Common ord 10881)
 * + sidecar save path + item spawn on reload.
 * ================================================================ */

/* Minimal struct views into the D2 1.10f unit/inventory layout.
 * Offsets taken from Reference/D2MOO/source/D2Common/include. We do
 * NOT include the D2MOO headers (C++ + pragmas); just read bytes at
 * the documented offsets. */
#define D2U_OFF_UNIT_TYPE           0x00    /* DWORD; 4 = item         */
#define D2U_OFF_PITEMDATA           0x14    /* D2ItemDataStrc* (union) */
#define D2U_OFF_PSTATICPATH         0x2C    /* D2StaticPathStrc*       */
#define D2U_OFF_PINVENTORY          0x60    /* D2InventoryStrc*        */

/* Item grid X/Y live in pStaticPath->tGameCoords (int32s at 0x0C and
 * 0x10 from the start of the static-path struct). nNodePos is NOT a
 * grid coord — it's the grid-type id + 1. See D2Common::UNITS_Set-
 * X/YForStaticUnit and D2StaticPathStrc in Path.h. */
#define D2SP_OFF_GAMECOORD_X        0x0C            /* int32 nX        */
#define D2SP_OFF_GAMECOORD_Y        0x10            /* int32 nY        */

#define D2ID_OFF_NINVPAGE           0x45            /* uint8 nInvPage (4=stash) */

#define INVPAGE_STASH               4
/* Inventory record for stash grid. From D2MOO UNITS_GetInventoryRecordId
 * (ord 10409): LoD big bank = 12 (INVENTORYRECORD_BIG_BANK_PAGE_1), classic
 * bank = 8. D2 1.10f is LoD. Previously used 28 which was the 800x600 *menu*
 * variant — wrong target grid, explains why PlaceItemAtInventoryPage failed
 * with return FALSE in the first test. */
#define INVENTORYRECORD_BIG_BANK_1  12

/* D2Common inventory function ordinals (see D2MOO/D2Common/include/D2Inventory.h) */
typedef void*  (__stdcall *INV_GetFirstItem_t)(void* pInv);
typedef void*  (__stdcall *INV_GetNextItem_t)(void* pItem);
typedef void*  (__stdcall *INV_RemoveItem_t)(void* pInv, void* pItem);
typedef BOOL   (__stdcall *INV_PlaceItemAtPage_t)(void* pInv, void* pItem,
                                                   int x, int y, int nInvRecord,
                                                   BOOL bUnused, BYTE nPage);
typedef void   (__stdcall *ITEMS_GetDims_t)(void* pItem, BYTE* pW, BYTE* pH,
                                              const char* szFile, int nLine);
/* Item serialization ordinals (D2Common 10881, 10883). */
typedef size_t (__stdcall *ITEMS_Serialize_t)(void* pItem, BYTE* pBs, size_t nSize,
                                                BOOL bServer, BOOL bSaveItemInv, BOOL bGamble);
typedef BOOL   (__stdcall *ITEMS_GetCompact_t)(BYTE* pBs, size_t nSize, BOOL bCheckHdr,
                                                 BYTE* pItemSaveStruct);
/* D2Game item spawner from bitstream (no ordinal — resolved by offset).
 * __fastcall: pGame in ECX, pRoom in EDX, rest on stack. */
typedef void*  (__fastcall *ITEMS_SpawnFromBS_t)(void* pGame, void* pRoom,
                                                   BYTE* pBs, DWORD nSize, int bCheckHdr,
                                                   BYTE* pItemSave, DWORD* pOutSize, DWORD dwVersion);
/* D2Game "place item into player" function (sub_6FC898F0, RVA 0x598F0
 * from D2Game base). Handles proper placement based on item's anim mode
 * AND triggers the server→client update that populates client-side
 * pGfxData, avoiding the "ptGfxInfo" assertion we hit when using bare
 * INVENTORY_PlaceItemAtInventoryPage. Returns 0 on success, 12 on fail. */
typedef int    (__fastcall *PLACE_ItemInPlayer_t)(void* pGame, void* pPlayer,
                                                    void* pItem, void* pParentItem);
/* D2Common INVENTORY_PlaceItemAtFreePosition (ord 10246). Finds first
 * free slot on the given page and places. */
typedef BOOL   (__stdcall  *INV_PlaceFreePos_t)(void* pInv, void* pItem,
                                                  int nInvRecord, BOOL bUnused,
                                                  BYTE nPage, const char* szFile, int nLine);
/* D2Common INVENTORY_GetItemFromInventoryPage (ord 10252). Returns
 * the item at (gridX, gridY) on the given page, or NULL. */
typedef void*  (__stdcall  *INV_GetItemAtXY_t)(void* pInv, int gridX, int gridY,
                                                 int* pX, int* pY,
                                                 int nInvRecord, BYTE nPage);
/* D2Common UNITS_GetInventoryRecordId (ord 10409). Returns the
 * inventoryrecord ID for a given page on this unit (handles class
 * variants automatically). */
typedef int    (__stdcall  *UNITS_GetInvRec_t)(void* pUnit, int nPage, BOOL bLoD);
/* D2Game SUNIT_GetInteractUnit (RVA +0x8D4D0). Returns the unit the
 * given server-side player is currently interacting with (NPC for
 * shop, chest, portal, etc.), or NULL if not interacting. __fastcall:
 * pGame=ecx, pUnit=edx. */
typedef void*  (__fastcall *SUNIT_GetInteract_t)(void* pGame, void* pUnit);
/* D2Game D2GAME_STORES_SellItem (RVA +0x97680). Sells an item from the
 * player's inventory to the NPC they're interacting with. __fastcall:
 * pGame=ecx, pPlayer=edx, rest on stack. Returns 0 on success. */
typedef int    (__fastcall *STORES_Sell_t)(void* pGame, void* pPlayer,
                                             int nNpcGUID, int nItemGUID,
                                             short nItemMode, int aUnused);
/* D2Game D2GAME_ITEMSOCKET_PlaceItem (RVA +0x497E0). Places a socket
 * filler (rune/jewel/gem) into a parent item's socket. Used by D2's
 * internal ITEMS_Duplicate. __fastcall: pGame=ecx, pUnit=edx, rest on
 * stack. Returns non-zero on success. */
typedef int    (__fastcall *ITEMSOCKET_PlaceItem_t)(void* pGame, void* pUnit,
                                                      int nFillerGUID, int nItemGUID,
                                                      int* bPlaced, int bRefresh,
                                                      int bResetCursor, int bCheckMode);
/* D2Common UNITS_ChangeAnimMode (ord 10348). Changes a unit's anim
 * mode. Required to set socketed children to IMODE_ONCURSOR (=4)
 * before calling D2GAME_ITEMSOCKET_PlaceItem (which only attaches if
 * the filler is on the cursor). */
typedef int    (__stdcall *UNITS_ChangeAnimMode_t)(void* pUnit, int nMode);

static INV_GetFirstItem_t    fnInvFirst      = NULL;
static INV_GetNextItem_t     fnInvNext       = NULL;
static INV_RemoveItem_t      fnInvRemove     = NULL;
static INV_PlaceItemAtPage_t fnInvPlace      = NULL;
static ITEMS_GetDims_t       fnItemDims      = NULL;
static ITEMS_Serialize_t     fnItemSer       = NULL;
static ITEMS_GetCompact_t    fnItemCompact   = NULL;
static ITEMS_SpawnFromBS_t   fnItemSpawn     = NULL;
static PLACE_ItemInPlayer_t  fnPlaceInPlayer = NULL;
static INV_PlaceFreePos_t    fnPlaceFreePos  = NULL;
static INV_GetItemAtXY_t     fnGetItemAtXY   = NULL;
static UNITS_GetInvRec_t     fnGetInvRec     = NULL;
static SUNIT_GetInteract_t   fnGetInteract   = NULL;
static STORES_Sell_t         fnStoresSell    = NULL;
static ITEMSOCKET_PlaceItem_t fnSocketPlace  = NULL;
static UNITS_ChangeAnimMode_t fnChangeAnimMode= NULL;
static BOOL                  s_swapResolved = FALSE;

/* Shadow storage — SERIALIZED. Items on non-active tabs live only as
 * byte streams (produced by ITEMS_SerializeItemToBitstream). This lets
 * us persist them to disk on exit and re-spawn on load via
 * ITEMS_SpawnFromBitstream, which is the only architecture that
 * supports both cross-session persistence AND shared-across-character
 * stashes (the 10 shared tabs are saved to a global file that every
 * character reads on load).
 *
 * Size budget: 100 items/tab × 20 tabs × ~520 bytes ≈ 1 MB RAM.
 * A single item serializes to ~60-250 bytes typically; 512 is generous. */
#define SER_MAX_BYTES       512     /* bytes per item (oversized for safety) */
#define SER_MAX_PER_TAB     100     /* 10x10 stash grid full */

typedef struct {
    BYTE  x, y;                     /* grid position for placement             */
    BYTE  w, h;                     /* dimensions; 0 = not set                 */
    WORD  byteLen;                  /* actual serialized length (0..SER_MAX)   */
    BYTE  bytes[SER_MAX_BYTES];     /* D2 bitstream                            */
} SerEntry;

typedef struct {
    DWORD    count;                 /* active entries (0..SER_MAX_PER_TAB)     */
    SerEntry items[SER_MAX_PER_TAB];
} SerTab;

static SerTab s_serTab[STASH_MAX_TABS];
static BOOL   s_serInitialPopulated = FALSE;

/* Legacy pointer-based storage — kept temporarily while we migrate
 * callers. Will be removed once SerTab path is fully wired. */
#define SWAP_MAX_PER_TAB    128
typedef struct {
    void* pClientItem;
    void* pServerItem;
    DWORD initSeed;
    int   x, y;
} SwapEntry;
typedef struct {
    SwapEntry items[SWAP_MAX_PER_TAB];
    int       count;
} SwapTab;

static SwapTab s_swapTab[STASH_MAX_TABS];
static BOOL    s_swapInitialPopulated = FALSE;

static void StashSwap_ResolveOnce(void) {
    HMODULE h;
    if (s_swapResolved) return;
    h = GetModuleHandleA("D2Common.dll");
    if (!h) return;
    fnInvFirst    = (INV_GetFirstItem_t)   GetProcAddress(h, (LPCSTR)10277);
    fnInvNext     = (INV_GetNextItem_t)    GetProcAddress(h, (LPCSTR)10304);
    fnInvRemove   = (INV_RemoveItem_t)     GetProcAddress(h, (LPCSTR)10243);
    fnInvPlace    = (INV_PlaceItemAtPage_t)GetProcAddress(h, (LPCSTR)10249);
    fnItemDims    = (ITEMS_GetDims_t)      GetProcAddress(h, (LPCSTR)10749);
    fnItemSer     = (ITEMS_Serialize_t)    GetProcAddress(h, (LPCSTR)10881);
    fnItemCompact = (ITEMS_GetCompact_t)   GetProcAddress(h, (LPCSTR)10883);
    fnPlaceFreePos= (INV_PlaceFreePos_t)   GetProcAddress(h, (LPCSTR)10246);
    fnGetItemAtXY = (INV_GetItemAtXY_t)    GetProcAddress(h, (LPCSTR)10252);
    fnGetInvRec   = (UNITS_GetInvRec_t)    GetProcAddress(h, (LPCSTR)10409);
    fnChangeAnimMode = (UNITS_ChangeAnimMode_t)GetProcAddress(h, (LPCSTR)10348);
    /* D2Game functions at known RVAs (no ordinals):
     *   sub_6FC4EC10 @ +0x1EC10 — spawn UnitAny from bitstream
     *   sub_6FC898F0 @ +0x598F0 — place freshly-spawned item into player
     *   SUNIT_GetInteractUnit @ +0x8D4D0 — get NPC player is talking to
     *   D2GAME_STORES_SellItem @ +0x97680 — sell item to interacting NPC */
    if (hD2Game) {
        fnItemSpawn     = (ITEMS_SpawnFromBS_t) ((DWORD)hD2Game + 0x1EC10);
        fnPlaceInPlayer = (PLACE_ItemInPlayer_t)((DWORD)hD2Game + 0x598F0);
        fnGetInteract   = (SUNIT_GetInteract_t) ((DWORD)hD2Game + 0x8D4D0);
        fnStoresSell    = (STORES_Sell_t)       ((DWORD)hD2Game + 0x97680);
        fnSocketPlace   = (ITEMSOCKET_PlaceItem_t)((DWORD)hD2Game + 0x497E0);
    }
    s_swapResolved = TRUE;
    Log("StashSwap: ord resolve First=%p Next=%p Rm=%p Pl=%p Dims=%p Ser=%p Cmp=%p Spw=%p PlInPl=%p\n",
        fnInvFirst, fnInvNext, fnInvRemove, fnInvPlace, fnItemDims,
        fnItemSer, fnItemCompact, fnItemSpawn, fnPlaceInPlayer);
}

/* D2InventoryStrc offsets for grid access. See D2Inventory.h. */
#define D2INV_OFF_PGRIDS            0x14    /* D2InventoryGridStrc* pGrids */
#define D2INV_OFF_NGRIDCOUNT        0x18    /* int32 nGridCount            */

/* D2InventoryGridStrc (16 bytes each) */
#define D2GRID_SIZE                 16
#define D2GRID_OFF_PITEM            0x00    /* D2UnitStrc*   pItem         */
#define D2GRID_OFF_PLASTITEM        0x04    /* D2UnitStrc*   pLastItem     */
#define D2GRID_OFF_NGRIDWIDTH       0x08    /* uint8                       */
#define D2GRID_OFF_NGRIDHEIGHT      0x09    /* uint8                       */
#define D2GRID_OFF_PPITEMS          0x0C    /* D2UnitStrc**  ppItems       */

/* Read nNodePos (grid index + 1) from the item's pItemData->pExtraData.
 * D2 sets this when placing the item, so it tells us which grid the
 * item is actually stored in — regardless of our page-to-grid math. */
static int StashSwap_GetItemNodePos(void* pItem) {
    void* pItemData;
    if (!pItem) return 0;
    __try { pItemData = *(void**)((BYTE*)pItem + D2U_OFF_PITEMDATA); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (!pItemData) return 0;
    /* pExtraData at pItemData+0x5C, nNodePos at +0x0C inside it */
    __try { return (int)*(unsigned char*)((BYTE*)pItemData + 0x5C + 0x0C); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* Safety net: null out ppItems[y*w + x] for the item's footprint in
 * the grid D2 actually stored the item in. D2's INVENTORY_RemoveItem
 * *should* do this automatically, but in practice the stash cells stay
 * populated and block placement. This runs BEFORE RemoveItem while the
 * item's nNodePos is still valid. */
static void StashSwap_ClearGridCells(void* pInv, void* pItem, int itemX, int itemY) {
    BYTE *pGrids;
    BYTE *pGrid;
    BYTE **ppItems;
    int gridW, gridH;
    int nGrids;
    int gridIdx;
    int nodePos;
    BYTE w = 1, h = 1;
    int x, y;

    if (!pInv || !pItem) return;

    nodePos = StashSwap_GetItemNodePos(pItem);
    if (nodePos <= 0) {
        Log("ClearGridCells: item %p has nNodePos=%d, cannot locate grid\n",
            pItem, nodePos);
        return;
    }
    gridIdx = nodePos - 1;

    __try {
        pGrids = *(BYTE**)((BYTE*)pInv + D2INV_OFF_PGRIDS);
        nGrids = *(int*) ((BYTE*)pInv + D2INV_OFF_NGRIDCOUNT);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!pGrids || nGrids <= gridIdx) {
        Log("ClearGridCells: pGrids=%p nGrids=%d gridIdx=%d — out of range\n",
            pGrids, nGrids, gridIdx);
        return;
    }

    pGrid = pGrids + gridIdx * D2GRID_SIZE;
    __try {
        gridW  = *(BYTE*)  (pGrid + D2GRID_OFF_NGRIDWIDTH);
        gridH  = *(BYTE*)  (pGrid + D2GRID_OFF_NGRIDHEIGHT);
        ppItems= *(BYTE***)(pGrid + D2GRID_OFF_PPITEMS);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!ppItems || gridW <= 0 || gridH <= 0) {
        Log("ClearGridCells: gridIdx=%d ppItems=%p W=%d H=%d — invalid grid\n",
            gridIdx, ppItems, gridW, gridH);
        return;
    }

    if (fnItemDims) {
        __try { fnItemDims(pItem, &w, &h, __FILE__, __LINE__); }
        __except(EXCEPTION_EXECUTE_HANDLER) { w = 1; h = 1; }
    }
    if (w < 1) w = 1; if (h < 1) h = 1;

    Log("ClearGridCells: grid=%d (%dx%d) clearing item %p footprint (%d,%d)+%dx%d\n",
        gridIdx, gridW, gridH, pItem, itemX, itemY, w, h);
    for (y = itemY; y < itemY + h; y++) {
        for (x = itemX; x < itemX + w; x++) {
            if (x < 0 || x >= gridW || y < 0 || y >= gridH) continue;
            __try { ppItems[y * gridW + x] = NULL; }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
}

static void* StashSwap_GetPlayerInv(void) {
    void* pPlayer;
    void* pInv;
    if (!fnGetPlayer) return NULL;
    __try { pPlayer = fnGetPlayer(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return NULL; }
    if (!pPlayer) return NULL;
    __try { pInv = *(void**)((BYTE*)pPlayer + D2U_OFF_PINVENTORY); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return NULL; }
    return pInv;
}

/* Get the SERVER-side player's inventory. D2 single-player runs the
 * server in-process; placement checks happen server-side so the server
 * inventory must also be updated for swaps to take effect. Returns
 * NULL if pGame hasn't been captured yet (pre-game) or the server
 * player can't be located.
 *
 * IMPORTANT: g_cachedPGame is persisted across character changes (for
 * reinvest logic), which means it may point to a freed game struct
 * after a char switch. Always re-resolve from D2Game+0x1157FC global
 * each call so we pick up the new game pointer immediately. */
static void* StashSwap_GetServerInv(void) {
    DWORD pGame = 0;
    void* pSrvPlayer;
    void* pInv;

    if (hD2Game) {
        __try {
            DWORD* ppGame = (DWORD*)((DWORD)hD2Game + 0x1157FC);
            pGame = *ppGame;
        } __except(EXCEPTION_EXECUTE_HANDLER) { pGame = 0; }
    }
    if (!pGame) return NULL;

    /* Refresh the cache so other modules benefit from the fresh ptr. */
    if (pGame != g_cachedPGame) {
        Log("StashSwap: pGame refreshed %08X -> %08X\n",
            (DWORD)g_cachedPGame, pGame);
        g_cachedPGame = pGame;
    }

    __try { pSrvPlayer = GetServerPlayer(pGame); }
    __except(EXCEPTION_EXECUTE_HANDLER) { pSrvPlayer = NULL; }
    if (!pSrvPlayer) return NULL;
    __try { pInv = *(void**)((BYTE*)pSrvPlayer + D2U_OFF_PINVENTORY); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return NULL; }
    return pInv;
}

/* Extract grid XY from an item via pStaticPath->tGameCoords.nX/nY.
 * Items (UNIT_ITEM) use the static-path union member at UnitAny+0x2C.
 * The grid X/Y is stored there by UNITS_SetXForStaticUnit when D2
 * places the item. */
static void StashSwap_GetItemXY(void* pItem, int* outX, int* outY) {
    void* pStatic;
    *outX = 0; *outY = 0;
    if (!pItem) return;
    __try { pStatic = *(void**)((BYTE*)pItem + D2U_OFF_PSTATICPATH); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!pStatic) return;
    __try {
        *outX = *(int*)((BYTE*)pStatic + D2SP_OFF_GAMECOORD_X);
        *outY = *(int*)((BYTE*)pStatic + D2SP_OFF_GAMECOORD_Y);
    } __except(EXCEPTION_EXECUTE_HANDLER) { *outX = 0; *outY = 0; }
}

/* Read nInvPage (0=backpack, 3=cube, 4=stash) off the item data. */
static int StashSwap_GetItemPage(void* pItem) {
    void* pItemData;
    if (!pItem) return -1;
    __try { pItemData = *(void**)((BYTE*)pItem + D2U_OFF_PITEMDATA); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
    if (!pItemData) return -1;
    __try { return (int)*(BYTE*)((BYTE*)pItemData + D2ID_OFF_NINVPAGE); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* Read dwInitSeed — persistent identity for an item that matches
 * between client-side and server-side UnitAny copies. Offset 0x10
 * within D2ItemDataStrc. */
static DWORD StashSwap_GetItemSeed(void* pItem) {
    void* pItemData;
    if (!pItem) return 0;
    __try { pItemData = *(void**)((BYTE*)pItem + D2U_OFF_PITEMDATA); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (!pItemData) return 0;
    __try { return *(DWORD*)((BYTE*)pItemData + 0x10); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* Walk inventory and find a stash item at grid (matchX, matchY). Client
 * and server copies of the same item share the same grid position, so
 * this is a reliable pairing key — unlike dwInitSeed, which appears to
 * stay zero for many items under D2MOO. */
static void* StashSwap_FindStashAtXY(void* pInv, int matchX, int matchY) {
    void *pItem, *pNext;
    if (!pInv || !fnInvFirst || !fnInvNext) return NULL;
    __try { pItem = fnInvFirst(pInv); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return NULL; }
    while (pItem) {
        __try { pNext = fnInvNext(pItem); }
        __except(EXCEPTION_EXECUTE_HANDLER) { pNext = NULL; }
        if (StashSwap_GetItemPage(pItem) == INVPAGE_STASH) {
            int x, y;
            StashSwap_GetItemXY(pItem, &x, &y);
            if (x == matchX && y == matchY) return pItem;
        }
        pItem = pNext;
    }
    return NULL;
}

/* Walk BOTH client + server pInventory, move every INVPAGE_STASH item
 * out of the linked list into s_swapTab[tabIdx]. Server-side modification
 * is what makes placement checks on other tabs succeed; client-side makes
 * the UI reflect the change. Returns client items moved. */
static int StashSwap_Drain(void* pClientInv, void* pServerInv, int tabIdx) {
    void *pItem, *pNext;
    SwapTab* tab;
    int moved = 0;
    int totalWalked = 0;
    int i;

    if (!pClientInv || tabIdx < 0 || tabIdx >= STASH_MAX_TABS) return 0;
    if (!fnInvFirst || !fnInvNext || !fnInvRemove) {
        Log("StashSwap_Drain: missing fn (first=%p next=%p remove=%p)\n",
            fnInvFirst, fnInvNext, fnInvRemove);
        return 0;
    }

    tab = &s_swapTab[tabIdx];
    tab->count = 0;

    /* Pass 1: walk client inventory, collect stash items into shadow. */
    __try { pItem = fnInvFirst(pClientInv); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }

    while (pItem && tab->count < SWAP_MAX_PER_TAB) {
        int page;
        int wx, wy;
        __try { pNext = fnInvNext(pItem); }
        __except(EXCEPTION_EXECUTE_HANDLER) { pNext = NULL; }

        totalWalked++;
        page = StashSwap_GetItemPage(pItem);
        StashSwap_GetItemXY(pItem, &wx, &wy);
        Log("  walk[%d] item=%p page=%d xy=(%d,%d)\n",
            totalWalked - 1, pItem, page, wx, wy);
        if (page == INVPAGE_STASH) {
            int x, y;
            void* rc;
            DWORD seed;
            StashSwap_GetItemXY(pItem, &x, &y);
            seed = StashSwap_GetItemSeed(pItem);
            tab->items[tab->count].pClientItem = pItem;
            tab->items[tab->count].pServerItem = NULL;
            tab->items[tab->count].initSeed    = seed;
            tab->items[tab->count].x = x;
            tab->items[tab->count].y = y;
            tab->count++;
            StashSwap_ClearGridCells(pClientInv, pItem, x, y);
            __try { rc = fnInvRemove(pClientInv, pItem); }
            __except(EXCEPTION_EXECUTE_HANDLER) { rc = NULL; }
            Log("StashSwap_Drain: CLIENT item %p seed=%08X at (%d,%d) -> shadow tab %d\n",
                pItem, seed, x, y, tabIdx);
            moved++;
        }
        pItem = pNext;
    }

    /* Pass 2: walk server inventory, match each by grid position, remove. */
    if (pServerInv) {
        for (i = 0; i < tab->count; i++) {
            void* pSrv = StashSwap_FindStashAtXY(pServerInv,
                                                 tab->items[i].x,
                                                 tab->items[i].y);
            if (pSrv) {
                void* rc;
                tab->items[i].pServerItem = pSrv;
                StashSwap_ClearGridCells(pServerInv, pSrv,
                                         tab->items[i].x, tab->items[i].y);
                __try { rc = fnInvRemove(pServerInv, pSrv); }
                __except(EXCEPTION_EXECUTE_HANDLER) { rc = NULL; }
                Log("StashSwap_Drain: SERVER item %p at (%d,%d) removed\n",
                    pSrv, tab->items[i].x, tab->items[i].y);
            } else {
                Log("StashSwap_Drain: SERVER no match for (%d,%d) (client %p)\n",
                    tab->items[i].x, tab->items[i].y, tab->items[i].pClientItem);
            }
        }
    } else {
        Log("StashSwap_Drain: no server inventory available (skipped pass 2)\n");
    }

    Log("StashSwap_Drain: walked %d client items, %d stored in tab %d\n",
        totalWalked, moved, tabIdx);
    return moved;
}

/* Inverse of Drain: place shadow items back into both client and server
 * inventories at their stored grid positions. */
static int StashSwap_Fill(void* pClientInv, void* pServerInv, int tabIdx) {
    SwapTab* tab;
    int i, placed = 0, failed = 0;
    int writeIdx = 0;

    if (!pClientInv || tabIdx < 0 || tabIdx >= STASH_MAX_TABS) return 0;
    if (!fnInvPlace) {
        Log("StashSwap_Fill: fnInvPlace is NULL\n");
        return 0;
    }

    tab = &s_swapTab[tabIdx];
    Log("StashSwap_Fill: trying to place %d items into tab %d (client+server)\n",
        tab->count, tabIdx);
    for (i = 0; i < tab->count; i++) {
        void* pCli = tab->items[i].pClientItem;
        void* pSrv = tab->items[i].pServerItem;
        BOOL okCli = FALSE, okSrv = TRUE;
        if (!pCli) continue;
        __try {
            okCli = fnInvPlace(pClientInv, pCli, tab->items[i].x, tab->items[i].y,
                               INVENTORYRECORD_BIG_BANK_1, FALSE, INVPAGE_STASH);
        } __except(EXCEPTION_EXECUTE_HANDLER) { okCli = FALSE; }

        if (pServerInv && pSrv) {
            okSrv = FALSE;
            __try {
                okSrv = fnInvPlace(pServerInv, pSrv, tab->items[i].x, tab->items[i].y,
                                   INVENTORYRECORD_BIG_BANK_1, FALSE, INVPAGE_STASH);
            } __except(EXCEPTION_EXECUTE_HANDLER) { okSrv = FALSE; }
        }

        if (okCli && okSrv) {
            placed++;
        } else {
            failed++;
            Log("StashSwap_Fill: PLACE FAILED seed=%08X at (%d,%d) cli=%d srv=%d\n",
                tab->items[i].initSeed, tab->items[i].x, tab->items[i].y,
                (int)okCli, (int)okSrv);
            /* Keep for retry — compact in place by copying to writeIdx. */
            if (writeIdx != i) tab->items[writeIdx] = tab->items[i];
            writeIdx++;
        }
    }
    tab->count = writeIdx;
    if (failed > 0) {
        Log("StashSwap_Fill: %d items could not be placed, retained in shadow tab %d\n",
            failed, tabIdx);
    }
    return placed;
}

/* ================================================================
 * SERIALIZED SHADOW (Phase 2c) — bytes-based storage for persistence
 * ================================================================ */

/* Drain: serialize every stash item on BOTH sides (client + server)
 * into SerTab[tabIdx], then remove from both inventories. Returns the
 * count successfully serialized. */
static int StashSwap_SerDrain(void* pClientInv, void* pServerInv, int tabIdx) {
    void *pItem, *pNext;
    SerTab* tab;
    int moved = 0;
    int walked = 0;
    BYTE tmpStashItemsSrv[SER_MAX_PER_TAB][2]; /* (x,y) of found server items */
    int srvCount = 0;

    if (!pClientInv || tabIdx < 0 || tabIdx >= STASH_MAX_TABS) return 0;
    if (!fnInvFirst || !fnInvNext || !fnInvRemove || !fnItemSer) {
        Log("SerDrain: missing fn (first=%p ser=%p rm=%p)\n",
            fnInvFirst, fnItemSer, fnInvRemove);
        return 0;
    }

    tab = &s_serTab[tabIdx];
    tab->count = 0;

    /* Pass 1: walk CLIENT inv, serialize + remove each stash item. */
    __try { pItem = fnInvFirst(pClientInv); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }

    while (pItem && tab->count < SER_MAX_PER_TAB) {
        int page;
        __try { pNext = fnInvNext(pItem); }
        __except(EXCEPTION_EXECUTE_HANDLER) { pNext = NULL; }

        walked++;
        page = StashSwap_GetItemPage(pItem);
        if (page == INVPAGE_STASH) {
            SerEntry* e = &tab->items[tab->count];
            int x, y;
            size_t nBytes = 0;
            BYTE w = 1, h = 1;

            StashSwap_GetItemXY(pItem, &x, &y);
            if (fnItemDims) { __try { fnItemDims(pItem, &w, &h, __FILE__, __LINE__); }
                              __except(EXCEPTION_EXECUTE_HANDLER) { w = 1; h = 1; } }

            /* Serialize via D2Common ord 10881. bServer=1, bSaveItemInv=1,
             * bGamble=0 — matches the "full save" flavor used by .d2s load
             * (PlrSave.cpp line ~1420). */
            __try {
                nBytes = fnItemSer(pItem, e->bytes, SER_MAX_BYTES, 1, 1, 0);
            } __except(EXCEPTION_EXECUTE_HANDLER) { nBytes = 0; }

            if (nBytes == 0 || nBytes > SER_MAX_BYTES) {
                Log("SerDrain: serialize FAILED for item %p at (%d,%d) len=%zu\n",
                    pItem, x, y, nBytes);
            } else {
                /* 1.9.0: detect runeword + sockets to diagnose lost-rune bug.
                 * Read item flags from UnitAny+0x18 dwFlags. Bit 26
                 * = IFLAG_RUNEWORD. Read socket count from item state.
                 * For diagnostic purposes only. */
                __try {
                    DWORD itemFlags = *(DWORD*)((BYTE*)pItem + 0x18);
                    BOOL  isRW      = (itemFlags & (1 << 26)) != 0;
                    BOOL  isSocketed= (itemFlags & (1 << 11)) != 0;
                    Log("SerDrain DIAG: item %p flags=0x%08X runeword=%d socketed=%d "
                        "size=%zu (parent-only ~80b vs parent+3runes ~170b)\n",
                        pItem, itemFlags, isRW, isSocketed, nBytes);
                    /* Hex dump the first 80 bytes so we can see the payload */
                    if (isRW || isSocketed) {
                        char hex[256] = {0};
                        int dump = (int)nBytes; if (dump > 64) dump = 64;
                        for (int j = 0; j < dump; j++) {
                            char b[4];
                            sprintf(b, "%02X", e->bytes[j]);
                            strcat(hex, b);
                            if ((j & 3) == 3) strcat(hex, " ");
                        }
                        Log("SerDrain DIAG: first 64 bytes: %s\n", hex);
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {}

                e->x = (BYTE)x; e->y = (BYTE)y;
                e->w = w; e->h = h;
                e->byteLen = (WORD)nBytes;
                tab->count++;
                moved++;
                Log("SerDrain: CLIENT item %p at (%d,%d) %dx%d -> %zu bytes, shadow[%d].count=%u\n",
                    pItem, x, y, w, h, nBytes, tabIdx, tab->count);
                /* Remember for server-side pairing */
                if (srvCount < SER_MAX_PER_TAB) {
                    tmpStashItemsSrv[srvCount][0] = (BYTE)x;
                    tmpStashItemsSrv[srvCount][1] = (BYTE)y;
                    srvCount++;
                }
            }

            /* Remove from client (clears ppItems too). */
            StashSwap_ClearGridCells(pClientInv, pItem, x, y);
            __try { fnInvRemove(pClientInv, pItem); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        pItem = pNext;
    }

    /* Pass 2: walk SERVER inv, remove matching stash items by (x,y). */
    if (pServerInv) {
        int i;
        for (i = 0; i < srvCount; i++) {
            void* pSrv = StashSwap_FindStashAtXY(pServerInv,
                                                  tmpStashItemsSrv[i][0],
                                                  tmpStashItemsSrv[i][1]);
            if (pSrv) {
                StashSwap_ClearGridCells(pServerInv, pSrv,
                                         tmpStashItemsSrv[i][0],
                                         tmpStashItemsSrv[i][1]);
                __try { fnInvRemove(pServerInv, pSrv); }
                __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    }

    Log("SerDrain: walked %d client items, %d serialized into tab %d\n",
        walked, moved, tabIdx);
    return moved;
}

/* Fill: for each SerEntry in tabIdx's shadow, spawn a fresh UnitAny
 * from its bitstream (via D2Game sub_6FC4EC10), then hand it to D2's
 * own "place item into player" function (sub_6FC898F0). That function
 * not only places it in the player's inventory but also triggers the
 * server→client state-sync that populates pGfxData on the client side
 * — without that, D2Client's render code asserts on ptGfxInfo. */
static int StashSwap_SerFill(void* pClientInv, void* pServerInv, int tabIdx) {
    SerTab* tab;
    int i, placed = 0, failed = 0;
    int writeIdx = 0;
    BYTE itemSaveStruct[64];   /* D2ItemSaveStrc is 0x18 bytes, pad to 64 */
    DWORD pGame = (DWORD)g_cachedPGame;
    void* pServerPlayer = NULL;
    (void)pClientInv; /* not used — sub_6FC898F0 handles both sides */

    if (tabIdx < 0 || tabIdx >= STASH_MAX_TABS) return 0;
    if (!fnItemCompact || !fnItemSpawn || !fnPlaceInPlayer) {
        Log("SerFill: missing fn compact=%p spawn=%p place=%p\n",
            fnItemCompact, fnItemSpawn, fnPlaceInPlayer);
        return 0;
    }
    if (!pGame) {
        Log("SerFill: g_cachedPGame is 0, cannot spawn\n");
        return 0;
    }
    __try { pServerPlayer = GetServerPlayer(pGame); }
    __except(EXCEPTION_EXECUTE_HANDLER) { pServerPlayer = NULL; }
    if (!pServerPlayer) {
        Log("SerFill: no server player, cannot place\n");
        return 0;
    }

    tab = &s_serTab[tabIdx];
    Log("SerFill: tab %d has %u entries, server-player=%p\n",
        tabIdx, tab->count, pServerPlayer);

    for (i = 0; i < (int)tab->count; i++) {
        SerEntry* e = &tab->items[i];
        void* pNew = NULL;
        DWORD parsedSize = 0;
        BOOL parseOk = FALSE;
        int placeRc = 12;

        /* Step 1: parse bitstream header into ItemSaveStrc. */
        memset(itemSaveStruct, 0, sizeof(itemSaveStruct));
        __try {
            parseOk = fnItemCompact(e->bytes, e->byteLen, 1, itemSaveStruct);
        } __except(EXCEPTION_EXECUTE_HANDLER) { parseOk = FALSE; }
        if (!parseOk) {
            Log("SerFill: GetCompact failed for entry %d\n", i);
            failed++;
            if (writeIdx != i) tab->items[writeIdx] = *e;
            writeIdx++;
            continue;
        }

        /* Step 2: spawn via D2Game sub_6FC4EC10. pRoom=NULL for inv items. */
        __try {
            pNew = fnItemSpawn((void*)pGame, NULL, e->bytes, e->byteLen,
                                1, itemSaveStruct, &parsedSize, 96);
        } __except(EXCEPTION_EXECUTE_HANDLER) { pNew = NULL; }
        if (!pNew) {
            Log("SerFill: Spawn failed for entry %d\n", i);
            failed++;
            if (writeIdx != i) tab->items[writeIdx] = *e;
            writeIdx++;
            continue;
        }

        /* Step 3: place parent via D2Game sub_6FC898F0. Pass pParentItem=NULL
         * for the top-level item. This routes on pItem->dwAnimMode —
         * stash items come out with IMODE_STORED and get placed via
         * D2GAME_PlaceItem_6FC44410 (server-side inventory grid +
         * server→client update). */
        __try {
            placeRc = fnPlaceInPlayer((void*)pGame, pServerPlayer, pNew, NULL);
        } __except(EXCEPTION_EXECUTE_HANDLER) { placeRc = 12; }

        /* Step 4: spawn + place each socketed child. Mirrors D2's own
         * .d2s loader at PlrSave.cpp:1426-1492 — the SAME function
         * sub_6FC898F0 handles both top-level placement (4th arg NULL)
         * and socket attachment (4th arg = parent item). When the 4th
         * arg is non-NULL, sub_6FC898F0 routes the child into the
         * parent's socket and triggers the proper client sync.
         *
         * Without this loop, sub_6FC4EC10 only decodes the parent — the
         * socketed children's bytes sit unprocessed in the bitstream,
         * the parent gets the runeword bit but no children, and rune
         * intrinsic stats vanish on every tab swap. */
        if (placeRc == 0) {
            int nSocketCount = *(int*)(itemSaveStruct + 0x14);  /* nItemFileIndex */
            BYTE itemSaveSocketStruct[64];
            DWORD remainingSize = e->byteLen - parsedSize;
            BYTE* pBs = e->bytes + parsedSize;
            int sock;

            if (nSocketCount > 0) {
                Log("SerFill: entry %d parent has %d socketed children "
                    "(parsedSize=%u, remaining=%u)\n",
                    i, nSocketCount, parsedSize, remainingSize);
            }

            for (sock = 0; sock < nSocketCount; sock++) {
                BOOL childParseOk = FALSE;
                void* pChild = NULL;
                DWORD childParsedSize = 0;
                int placeChildRc = 12;

                memset(itemSaveSocketStruct, 0, sizeof(itemSaveSocketStruct));
                __try {
                    childParseOk = fnItemCompact(pBs, remainingSize, 1,
                                                 itemSaveSocketStruct);
                } __except(EXCEPTION_EXECUTE_HANDLER) { childParseOk = FALSE; }
                if (!childParseOk) {
                    Log("SerFill: child %d/%d compact parse FAILED at offset=%u "
                        "remaining=%u\n",
                        sock, nSocketCount, parsedSize, remainingSize);
                    break;
                }

                __try {
                    pChild = fnItemSpawn((void*)pGame, NULL, pBs, remainingSize,
                                          1, itemSaveSocketStruct,
                                          &childParsedSize, 96);
                } __except(EXCEPTION_EXECUTE_HANDLER) { pChild = NULL; }
                if (!pChild || childParsedSize == 0) {
                    Log("SerFill: child %d/%d Spawn FAILED parsedSize=%u\n",
                        sock, nSocketCount, childParsedSize);
                    break;
                }

                /* Place child as socketed sub-item by passing parent as
                 * 4th arg (pParentItem). sub_6FC898F0 detects the parent
                 * argument and routes via the socket-place path
                 * internally. This is exactly how PlrSave.cpp:1481 loads
                 * socketed items from .d2s — the gold standard. */
                __try {
                    placeChildRc = fnPlaceInPlayer((void*)pGame, pServerPlayer,
                                                    pChild, pNew);
                } __except(EXCEPTION_EXECUTE_HANDLER) { placeChildRc = 12; }
                Log("SerFill: child %d/%d placed in socket of parent=%p "
                    "(rc=%d, childParsedSize=%u)\n",
                    sock, nSocketCount, pNew, placeChildRc, childParsedSize);

                pBs += childParsedSize;
                remainingSize -= childParsedSize;
                parsedSize += childParsedSize;
            }
        }

        if (placeRc == 0) {
            placed++;
            Log("SerFill: entry %d placed at (%d,%d) item=%p (rc=0)\n",
                i, e->x, e->y, pNew);
            /* 1.9.0 Phase 6 — grandfather this item past the strict
             * two-flag check. The item came from a trusted stash
             * sidecar (player's own deposit) but was never observed
             * on the ground with IFLAG_NEWITEM by us. Without this,
             * the next inv-scan tick would classify it as TAINTED
             * and refuse to auto-mark even though it's a legitimate
             * collected item. Calling Coll_GrandfatherInvItem runs
             * the catalog lookup with requireLegit=FALSE, marking
             * any matching slot. Non-catalog items silently no-op. */
            extern void Coll_GrandfatherInvItem(void* pItem);
            Coll_GrandfatherInvItem(pNew);
        } else {
            failed++;
            Log("SerFill: PLACE FAILED entry %d at (%d,%d) item=%p rc=%d\n",
                i, e->x, e->y, pNew, placeRc);
            if (writeIdx != i) tab->items[writeIdx] = *e;
            writeIdx++;
        }
    }

    tab->count = writeIdx;
    if (failed > 0) {
        Log("SerFill: %d of %d entries placed into tab %d, %d failed (retained)\n",
            placed, placed + failed, tabIdx, failed);
    }
    return placed;
}

/* ================================================================
 * PERSISTENCE — save/load SerTab to sidecar files
 * ================================================================ */

#define SER_FILE_MAGIC      0x41504153    /* "APSA" (AP Stash) */
#define SER_FILE_VERSION    1

typedef struct {
    DWORD magic;
    DWORD version;
    DWORD numTabs;
    DWORD reserved;
} SerFileHeader;

/* Write a range of shadow tabs to a sidecar file. Atomic-ish (writes
 * all tabs' counts + their items, then a xor-rotate checksum trailer). */
static BOOL StashSer_WriteFile(const char* path, int tabLo, int tabHi) {
    FILE* f;
    SerFileHeader hdr;
    DWORD cksum = 0;
    int tab;
    BYTE* p;
    size_t i;

    if (!path) return FALSE;
    if (tabLo < 0 || tabHi > STASH_MAX_TABS || tabLo >= tabHi) return FALSE;

    f = fopen(path, "wb");
    if (!f) {
        Log("StashSer_WriteFile: fopen FAILED err=%lu path='%s'\n",
            GetLastError(), path);
        return FALSE;
    }

    hdr.magic    = SER_FILE_MAGIC;
    hdr.version  = SER_FILE_VERSION;
    hdr.numTabs  = (DWORD)(tabHi - tabLo);
    hdr.reserved = 0;
    /* 1.8.0 cleanup: validate fwrite return values so that a full disk or
     * bad file descriptor doesn't silently truncate stash persistence. */
    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1) {
        Log("StashSer_WriteFile: fwrite(hdr) FAILED err=%lu\n", GetLastError());
        fclose(f); return FALSE;
    }

    /* Update checksum as bytes are written. Include header. */
    p = (BYTE*)&hdr;
    for (i = 0; i < sizeof(hdr); i++) { cksum ^= p[i]; cksum = (cksum << 1) | (cksum >> 31); }

    for (tab = tabLo; tab < tabHi; tab++) {
        SerTab* t = &s_serTab[tab];
        DWORD count = t->count;
        if (fwrite(&count, sizeof(count), 1, f) != 1) {
            Log("StashSer_WriteFile: fwrite(count) FAILED tab=%d err=%lu\n", tab, GetLastError());
            fclose(f); return FALSE;
        }
        p = (BYTE*)&count;
        for (i = 0; i < 4; i++) { cksum ^= p[i]; cksum = (cksum << 1) | (cksum >> 31); }

        if (count > SER_MAX_PER_TAB) count = SER_MAX_PER_TAB;
        if (count > 0) {
            if (fwrite(t->items, sizeof(SerEntry), count, f) != count) {
                Log("StashSer_WriteFile: fwrite(items) FAILED tab=%d count=%u err=%lu\n",
                    tab, count, GetLastError());
                fclose(f); return FALSE;
            }
            p = (BYTE*)t->items;
            for (i = 0; i < count * sizeof(SerEntry); i++) {
                cksum ^= p[i]; cksum = (cksum << 1) | (cksum >> 31);
            }
        }
    }

    if (fwrite(&cksum, sizeof(cksum), 1, f) != 1) {
        Log("StashSer_WriteFile: fwrite(cksum) FAILED err=%lu\n", GetLastError());
        fclose(f); return FALSE;
    }
    fclose(f);
    Log("StashSer_WriteFile: wrote tabs [%d..%d) to '%s' cksum=%08X\n",
        tabLo, tabHi, path, cksum);
    return TRUE;
}

/* Read shadow tabs from a sidecar file. Returns total items loaded.
 * On any error (missing/corrupt/wrong-version), target tabs are left
 * at their pre-call state (usually empty from StashSwapReset). */
static int StashSer_ReadFile(const char* path, int tabLo, int tabHi) {
    FILE* f;
    SerFileHeader hdr;
    DWORD cksum = 0, fileCksum = 0;
    int tab;
    int loaded = 0;
    BYTE* p;
    size_t i;
    size_t rd;

    if (!path) return 0;
    if (tabLo < 0 || tabHi > STASH_MAX_TABS || tabLo >= tabHi) return 0;

    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        Log("StashSer_ReadFile: no file at '%s' — leaving tabs empty\n", path);
        return 0;
    }

    f = fopen(path, "rb");
    if (!f) {
        Log("StashSer_ReadFile: fopen FAILED err=%lu path='%s'\n",
            GetLastError(), path);
        return 0;
    }

    rd = fread(&hdr, 1, sizeof(hdr), f);
    if (rd != sizeof(hdr)) { fclose(f); return 0; }
    if (hdr.magic != SER_FILE_MAGIC) {
        Log("StashSer_ReadFile: bad magic 0x%08X\n", hdr.magic);
        fclose(f); return 0;
    }
    if (hdr.version != SER_FILE_VERSION) {
        Log("StashSer_ReadFile: bad version %u\n", hdr.version);
        fclose(f); return 0;
    }
    if ((int)hdr.numTabs != tabHi - tabLo) {
        Log("StashSer_ReadFile: numTabs %u != expected %d\n",
            hdr.numTabs, tabHi - tabLo);
        fclose(f); return 0;
    }

    p = (BYTE*)&hdr;
    for (i = 0; i < sizeof(hdr); i++) { cksum ^= p[i]; cksum = (cksum << 1) | (cksum >> 31); }

    for (tab = tabLo; tab < tabHi; tab++) {
        SerTab* t = &s_serTab[tab];
        DWORD count = 0;
        rd = fread(&count, 1, 4, f);
        if (rd != 4) { fclose(f); return loaded; }
        p = (BYTE*)&count;
        for (i = 0; i < 4; i++) { cksum ^= p[i]; cksum = (cksum << 1) | (cksum >> 31); }

        if (count > SER_MAX_PER_TAB) {
            Log("StashSer_ReadFile: tab %d count=%u > max, truncating\n", tab, count);
            count = SER_MAX_PER_TAB;
        }

        t->count = count;
        memset(t->items, 0, sizeof(t->items));
        if (count > 0) {
            rd = fread(t->items, sizeof(SerEntry), count, f);
            if (rd != count) { fclose(f); t->count = 0; return loaded; }
            p = (BYTE*)t->items;
            for (i = 0; i < count * sizeof(SerEntry); i++) {
                cksum ^= p[i]; cksum = (cksum << 1) | (cksum >> 31);
            }
            loaded += count;
        }
    }

    if (fread(&fileCksum, 1, 4, f) == 4) {
        if (fileCksum != cksum) {
            Log("StashSer_ReadFile: cksum mismatch file=%08X calc=%08X — using anyway\n",
                fileCksum, cksum);
        }
    }
    fclose(f);
    Log("StashSer_ReadFile: loaded %d items across tabs [%d..%d) from '%s'\n",
        loaded, tabLo, tabHi, path);
    return loaded;
}

/* Public: persist AP tabs (0..9) to ap_stash_ser_<char>.dat and
 * shared tabs (10..19) to shared_stash_ser.dat. Called on char exit
 * AND on every tab swap so crash recovery loses at most one swap. */
void StashSerSaveAll(const char* charName) {
    char path[MAX_PATH];
    char leaf[128];
    char archDir[MAX_PATH], saveDir[MAX_PATH];

    if (!charName || !charName[0]) return;
    GetArchDir(archDir, sizeof(archDir));
    GetCharFileDir(saveDir, sizeof(saveDir));

    /* AP tabs — per character (Game/Save/) */
    _snprintf(leaf, sizeof(leaf) - 1, "ap_stash_ser_%s.dat", charName);
    _snprintf(path, sizeof(path) - 1, "%s%s", saveDir, leaf);
    path[sizeof(path) - 1] = 0;
    StashSer_WriteFile(path, 0, STASH_NUM_AP_TABS);

    /* Shared tabs — account-wide (Game/Archipelago/) */
    _snprintf(path, sizeof(path) - 1, "%sshared_stash_ser.dat", archDir);
    path[sizeof(path) - 1] = 0;
    StashSer_WriteFile(path, STASH_NUM_AP_TABS, STASH_MAX_TABS);
}

/* Public: load AP+shared shadow from disk. Called on char load. */
void StashSerLoadAll(const char* charName) {
    char path[MAX_PATH];
    char archDir[MAX_PATH], saveDir[MAX_PATH];
    if (!charName || !charName[0]) return;
    GetArchDir(archDir, sizeof(archDir));
    GetCharFileDir(saveDir, sizeof(saveDir));

    _snprintf(path, sizeof(path) - 1, "%sap_stash_ser_%s.dat", saveDir, charName);
    path[sizeof(path) - 1] = 0;
    StashSer_ReadFile(path, 0, STASH_NUM_AP_TABS);

    _snprintf(path, sizeof(path) - 1, "%sshared_stash_ser.dat", archDir);
    path[sizeof(path) - 1] = 0;
    StashSer_ReadFile(path, STASH_NUM_AP_TABS, STASH_MAX_TABS);

    s_serInitialPopulated = TRUE;
}

/* ================================================================
 * DISPATCH — the main entry point called by the UI
 * ================================================================ */

/* Public: called from d2arch_stashui.c when the user clicks a new tab.
 * Serializes the current native stash into shadow[oldTab], then spawns
 * items from shadow[newTab] into the native stash.
 *
 * Pipeline: drain (serialize + remove) → fill (spawn + place). Shadow
 * tabs are byte-backed so they survive across sessions when persisted
 * via StashSerSaveAll/LoadAll on exit/load. */
void StashSwapPage(int oldTabGlobal, int newTabGlobal) {
    void *pCliInv, *pSrvInv;
    int drained = 0, filled = 0;

    StashSwap_ResolveOnce();
    if (!s_swapResolved || !fnInvFirst) return;
    if (oldTabGlobal == newTabGlobal) return;
    if (oldTabGlobal < 0 || oldTabGlobal >= STASH_MAX_TABS) return;
    if (newTabGlobal < 0 || newTabGlobal >= STASH_MAX_TABS) return;

    pCliInv = StashSwap_GetPlayerInv();
    pSrvInv = StashSwap_GetServerInv();
    if (!pCliInv) {
        Log("StashSwapPage: no client pInventory (player not loaded)\n");
        return;
    }
    Log("StashSwapPage: cliInv=%p srvInv=%p tab %d -> %d\n",
        pCliInv, pSrvInv, oldTabGlobal, newTabGlobal);

    /* 1.9.0 — STK tabs (global indices >= STASH_STK_AP_BASE) don't use the
     * AP/SH shadow system. Their items live in g_stashStkAp / g_stashStkSh
     * (template bytes + count, never instantiated in pInventory). So:
     *   - oldTab is STK: pInventory is already empty for stash slots —
     *     skip drain (nothing to serialize out).
     *   - newTab is STK: don't fill pInventory — vanilla draws empty
     *     cells, our overlay (StashUIRender) draws the ghost-images and
     *     stack counters on top.
     * For AP/SH tabs nothing changes from existing behavior. */
    BOOL oldIsStk = (oldTabGlobal >= STASH_STK_AP_BASE);
    BOOL newIsStk = (newTabGlobal >= STASH_STK_AP_BASE);

    if (!oldIsStk) {
        drained = StashSwap_SerDrain(pCliInv, pSrvInv, oldTabGlobal);
    } else {
        Log("StashSwapPage: oldTab %d is STK — skipping drain\n", oldTabGlobal);
    }
    if (!newIsStk) {
        filled = StashSwap_SerFill(pCliInv, pSrvInv, newTabGlobal);
    } else {
        Log("StashSwapPage: newTab %d is STK — skipping fill (overlay handles render)\n",
            newTabGlobal);
    }

    /* Persist on every swap so a crash doesn't lose more than the
     * in-flight swap. Cheap — writes ~1 MB to disk. */
    if (g_charName[0]) StashSerSaveAll(g_charName);

    Log("StashSwapPage: tab %d -> %d done (drained %d, filled %d)\n",
        oldTabGlobal, newTabGlobal, drained, filled);
}

/* Reset shadow on char load/unload. Keep legacy pointer-based tables
 * cleared so nothing stale leaks across chars. The serialized shadow
 * (s_serTab) is NOT cleared here — it gets loaded from disk in
 * StashSerLoadAll after char load, and explicit Save calls persist
 * across sessions. */
void StashSwapReset(void) {
    memset(s_swapTab, 0, sizeof(s_swapTab));
    s_swapInitialPopulated = FALSE;
    Log("StashSwap: legacy pointer shadow tables reset\n");
}

/* Clear the serialized shadow (in-memory only). Called when a new
 * character loads so we don't carry over the previous char's AP tabs
 * before StashSerLoadAll populates them from disk. */
void StashSerResetMemory(void) {
    memset(s_serTab, 0, sizeof(s_serTab));
    s_serInitialPopulated = FALSE;
    Log("StashSer: memory shadow cleared (disk data still intact)\n");
}

/* ================================================================
 * QUICK-STASH: shift+right-click item in inventory → auto-send to
 * first free slot on the active stash tab. The reverse of this
 * (stash → inventory) is Phase 2d and not yet wired.
 * ================================================================ */

/* Find the backpack (INVPAGE_INVENTORY) item whose grid footprint
 * contains (gridX, gridY). Uses D2Common::INVENTORY_GetItemFromInventoryPage
 * (ord 10252). Returns NULL if cell is empty. */
static void* FindBackpackItemAtGrid(void* pInv, void* pUnit, int gridX, int gridY) {
    int outX = 0, outY = 0;
    int record;
    if (!fnGetItemAtXY || !fnGetInvRec || !pInv || !pUnit) return NULL;
    __try { record = fnGetInvRec(pUnit, 0 /*INVPAGE_INVENTORY*/, 1 /*LoD*/); }
    __except(EXCEPTION_EXECUTE_HANDLER) { record = -1; }
    if (record < 0) return NULL;
    __try {
        return fnGetItemAtXY(pInv, gridX, gridY, &outX, &outY, record, 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}

/* Convert logical-800x600 mouse coords to backpack grid (col, row).
 * Works for the standard D2 inventory panel; Amazon/Sorc/... have
 * different vertical anchors but all share the same grid origin for
 * the 10x4 backpack cells. Returns FALSE if mouse is outside the grid. */
static BOOL MouseToBackpackGrid(int mx, int my, int* outCol, int* outRow) {
    /* 800x600 classic D2 inventory grid: 10 wide × 4 tall, anchored at
     * approximately (417, 321) with 29 px cells. These values match
     * inventory.txt for records 0-6 (pre-LoD class records). Refined
     * empirically — see D2's inventory.txt column "invLeft/invTop". */
    const int GRID_X0 = 417;
    const int GRID_Y0 = 321;
    const int CELL    = 29;
    const int COLS    = 10;
    const int ROWS    = 4;
    int col, row;

    if (mx < GRID_X0 || my < GRID_Y0) return FALSE;
    col = (mx - GRID_X0) / CELL;
    row = (my - GRID_Y0) / CELL;
    if (col < 0 || col >= COLS || row < 0 || row >= ROWS) return FALSE;

    *outCol = col;
    *outRow = row;
    return TRUE;
}

/* 1.9.0 — STK click-pickup. Called from StashUIHandleClick when the
 * user clicks the chest grid while an STK tab is active. Spawns a fresh
 * item from the cell's template bytes and places it on the player's
 * cursor; decrements the cell's stack count by 1. Returns TRUE if the
 * click hit a populated STK cell and an item was successfully placed
 * (caller should swallow the click). FALSE if click missed the grid,
 * cell was empty, or spawn/place failed (caller can let click fall
 * through). */
/* Find the player's cursor item.
 *
 * In D2 1.10f, the cursor item is stored in a dedicated field on the
 * Inventory struct (pInventory->pCursorItem at offset +0x20), NOT in
 * the pFirstItem linked list. When INVENTORY_SetCursorItem is called,
 * the item is REMOVED from the linked list and only the pCursorItem
 * pointer references it. Walking the inventory chain therefore can't
 * find it (which is why my earlier IMODE_ONCURSOR walks always
 * returned NULL even when an item was clearly on cursor).
 *
 * Two retrieval methods, both equivalent — use whichever resolves
 * first. D2Common.10262 is the canonical INVENTORY_GetCursorItem
 * function; the direct struct read at +0x20 is the fallback. */
typedef void* (__stdcall *D2C_GetCursorItem_t)(void* pInventory);

static void* StkFindCursorItem(void* pCliInv) {
    if (!pCliInv) return NULL;

    /* Method 1: D2Common ordinal 10262 (preferred) */
    static D2C_GetCursorItem_t fnGetCursor = NULL;
    static BOOL fnResolved = FALSE;
    if (!fnResolved) {
        HMODULE hCommon = GetModuleHandleA("D2Common.dll");
        if (hCommon) {
            fnGetCursor = (D2C_GetCursorItem_t)GetProcAddress(hCommon, (LPCSTR)10262);
        }
        fnResolved = TRUE;
    }
    if (fnGetCursor) {
        void* p = NULL;
        __try { p = fnGetCursor(pCliInv); }
        __except(EXCEPTION_EXECUTE_HANDLER) { p = NULL; }
        if (p) return p;
    }

    /* Method 2: direct struct read at +0x20 */
    void* p = NULL;
    __try { p = *(void**)((BYTE*)pCliInv + 0x20); }
    __except(EXCEPTION_EXECUTE_HANDLER) { p = NULL; }
    return p;
}

BOOL StkClickPickup(int mx, int my) {
    /* Only act when an STK tab is the active one. */
    if (g_activeStashTab < STASH_STK_AP_BASE) return FALSE;

    BOOL useApScope    = (g_activeStashTab < STASH_STK_SH_BASE);
    int  categoryIdx   = useApScope ? (g_activeStashTab - STASH_STK_AP_BASE)
                                    : (g_activeStashTab - STASH_STK_SH_BASE);

    /* Read the same INI-tunable grid coords that StashUIRenderStkOverlay
     * uses, so click hit-testing and visual cells stay in sync. */
    char ini[MAX_PATH];
    GetArchDir(ini, MAX_PATH);
    strcat(ini, "d2arch.ini");
    int gridX = GetPrivateProfileIntA("Stash", "StkGridX", 232, ini);
    int gridY = GetPrivateProfileIntA("Stash", "StkGridY", 143, ini);
    int cellW = GetPrivateProfileIntA("Stash", "StkCellW",  29, ini);
    int cellH = GetPrivateProfileIntA("Stash", "StkCellH",  29, ini);

    if (mx < gridX || my < gridY) return FALSE;
    int col = (mx - gridX) / cellW;
    int row = (my - gridY) / cellH;
    if (col < 0 || col >= STASH_GRID_COLS || row < 0 || row >= STASH_GRID_ROWS)
        return FALSE;

    /* For multi-cell items, the click might land on a sub-cell. Walk
     * origins to find the one whose footprint contains (col,row). */
    int cellIdx = -1;
    for (int searchIdx = 0; searchIdx < STASH_SLOTS_PER_TAB; searchIdx++) {
        StkLayoutEntry* e = &g_stkLayout[categoryIdx][searchIdx];
        if (!e->isOriginCell || e->dwCode == 0) continue;
        int origCol = searchIdx % STASH_GRID_COLS;
        int origRow = searchIdx / STASH_GRID_COLS;
        if (col >= origCol && col < origCol + e->cellsW &&
            row >= origRow && row < origRow + e->cellsH) {
            cellIdx = searchIdx;
            break;
        }
    }
    if (cellIdx < 0) {
        Log("StkPickup: click (col=%d row=%d) hit no slot — swallowed\n", col, row);
        return TRUE;
    }
    DWORD layoutCode = g_stkLayout[categoryIdx][cellIdx].dwCode;

    /* 1.9.0 — DRAG-AND-DROP DEPOSIT path. If the player has an item on
     * the cursor when they click an STK cell, this is a deposit
     * attempt (NOT a pickup). Validate the cursor item matches the
     * cell's dwCode; if it does, increment the stack and consume the
     * cursor item. If mismatch (wrong type for this cell, or click
     * landed on a black cell — already filtered above), swallow the
     * click without doing anything so the cursor item stays on cursor.
     *
     * Without this check, my old code would FIRE the pickup logic
     * (spawning a NEW item on the already-busy cursor) AND vanilla D2
     * would also process the click (placing the cursor item into
     * pInventory at the click position) — chaos, items duplicating or
     * vanishing. */
    StashSwap_ResolveOnce();
    void* pCliInv = StashSwap_GetPlayerInv();
    void* pSrvInv = StashSwap_GetServerInv();
    void* pCursorItem = StkFindCursorItem(pCliInv);
    if (pCursorItem) {
        DWORD itemClassId = 0;
        __try { itemClassId = *(DWORD*)((BYTE*)pCursorItem + 0x04); }
        __except(EXCEPTION_EXECUTE_HANDLER) { itemClassId = 0; }

        /* Resolve the cell's dwCode -> classId for comparison. */
        HMODULE hCommon = GetModuleHandleA("D2Common.dll");
        typedef void* (__stdcall *GetItemFromCode_t)(DWORD, int*);
        GetItemFromCode_t fnGet = hCommon
            ? (GetItemFromCode_t)GetProcAddress(hCommon, (LPCSTR)10601)
            : NULL;
        int cellClassId = -1;
        if (fnGet) {
            __try { fnGet(layoutCode, &cellClassId); }
            __except(EXCEPTION_EXECUTE_HANDLER) { cellClassId = -1; }
        }

        if (cellClassId != (int)itemClassId) {
            Log("StkPickup: cursor item clsId=%u does NOT match cell %d (clsId=%d) — "
                "rejected, cursor item kept\n",
                (unsigned)itemClassId, cellIdx, cellClassId);
            /* 1.9.0 Phase 4 — flash the cell red for visual feedback. */
            extern void Stk_FlashBadDrop(int tabCategory, int cellIdx);
            Stk_FlashBadDrop(categoryIdx, cellIdx);
            return TRUE;  /* swallow click; cursor item stays */
        }

        /* Match! Deposit the cursor item. */
        StkTab* tabD = useApScope ? &g_stashStkAp[categoryIdx]
                                   : &g_stashStkSh[categoryIdx];
        StkSlot* slotD = &tabD->slots[cellIdx];
        if (slotD->count >= STK_MAX_STACK_COUNT) {
            Log("StkPickup: stack full at cell %d — deposit rejected\n", cellIdx);
            /* 1.9.0 Phase 4 — flash red on stack-full rejection. */
            extern void Stk_FlashBadDrop(int tabCategory, int cellIdx);
            Stk_FlashBadDrop(categoryIdx, cellIdx);
            return TRUE;
        }
        slotD->dwCode = layoutCode;
        slotD->count++;
        tabD->lastModifiedTick = GetTickCount();

        /* Properly dispose of the cursor item. Just clearing the
         * pInventory->pCursorItem field via INVENTORY_SetCursorItem
         * is NOT enough: the item unit remains registered in the
         * D2Client global player-units hash table, so the client
         * keeps drawing the icon at cursor position ("ghost item").
         *
         * D2's own potion-consume code uses
         *   D2GAME_RemoveItemIfOnCursor   @ D2Game.dll +0x19760
         * which does the full sequence: send packet 0x42 (notify
         * client to clear cursor), call INVENTORY_SetCursorItem(NULL)
         * on the server inv, and ITEMS_RemoveFromAllPlayers (drops
         * the unit from the global hash). Signature is __fastcall
         * (pGame, pPlayer, pItem); returns 1 on success, 0 if the
         * item argument doesn't match the actual cursor item. */
        {
            HMODULE hD2GameDispose = GetModuleHandleA("D2Game.dll");
            DWORD   pGameDispose   = (DWORD)g_cachedPGame;
            void*   pSrvPlayerDispose = NULL;
            void*   pSrvCursorItem    = NULL;
            int     disposeRc = 0;

            if (pGameDispose) {
                __try { pSrvPlayerDispose = GetServerPlayer(pGameDispose); }
                __except(EXCEPTION_EXECUTE_HANDLER) { pSrvPlayerDispose = NULL; }
            }
            if (pSrvInv) {
                pSrvCursorItem = StkFindCursorItem(pSrvInv);
            }

            if (hD2GameDispose && pGameDispose && pSrvPlayerDispose && pSrvCursorItem) {
                typedef int (__fastcall *RemoveItemIfOnCursor_t)(
                    void* pGame, void* pPlayer, void* pItem);
                RemoveItemIfOnCursor_t fnRemoveCursor =
                    (RemoveItemIfOnCursor_t)((DWORD)hD2GameDispose + 0x19760);
                __try {
                    disposeRc = fnRemoveCursor((void*)pGameDispose,
                                               pSrvPlayerDispose,
                                               pSrvCursorItem);
                } __except(EXCEPTION_EXECUTE_HANDLER) { disposeRc = -1; }
                Log("StkPickup: RemoveItemIfOnCursor rc=%d (pGame=%08X pPl=%p pItem=%p)\n",
                    disposeRc, pGameDispose, pSrvPlayerDispose, pSrvCursorItem);
            } else {
                Log("StkPickup: dispose preconds missing (hD2G=%p pGame=%08X pSrvPl=%p pSrvCur=%p)\n",
                    hD2GameDispose, pGameDispose, pSrvPlayerDispose, pSrvCursorItem);
            }

            /* Belt-and-suspenders: if the dispose call didn't run or
             * returned 0 (item mismatch), still clear the cursor field
             * on both invs. The cursor will then show empty even if
             * the item unit lingers; user-visible result is correct. */
            if (disposeRc <= 0) {
                typedef void (__stdcall *D2C_SetCursor_t)(void* pInv, void* pItem);
                static D2C_SetCursor_t fnSetCursor = NULL;
                static BOOL setResolved = FALSE;
                if (!setResolved) {
                    HMODULE hCommon = GetModuleHandleA("D2Common.dll");
                    if (hCommon) {
                        fnSetCursor = (D2C_SetCursor_t)GetProcAddress(hCommon, (LPCSTR)10261);
                    }
                    setResolved = TRUE;
                }
                if (fnSetCursor) {
                    __try { fnSetCursor(pCliInv, NULL); } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    if (pSrvInv) {
                        __try { fnSetCursor(pSrvInv, NULL); } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    }
                } else {
                    __try { *(void**)((BYTE*)pCliInv + 0x20) = NULL; }
                    __except(EXCEPTION_EXECUTE_HANDLER) {}
                    if (pSrvInv) {
                        __try { *(void**)((BYTE*)pSrvInv + 0x20) = NULL; }
                        __except(EXCEPTION_EXECUTE_HANDLER) {}
                    }
                }
            }
        }

        if (g_charName[0]) {
            if (useApScope) StkSaveAP(g_charName);
            else            StkSaveShared();
        }
        Log("StkPickup: drag-drop DEPOSIT cat=%d cell=%d (count now %u) — cursor cleared\n",
            categoryIdx, cellIdx, (unsigned)slotD->count);
        return TRUE;
    }
    /* No cursor item — fall through to pickup logic below. */

    StkTab* tab = useApScope ? &g_stashStkAp[categoryIdx]
                              : &g_stashStkSh[categoryIdx];
    StkSlot* slot = &tab->slots[cellIdx];
    if (slot->count == 0) {
        Log("StkPickup: click on empty stack (cat=%d cell=%d) — no-op\n",
            categoryIdx, cellIdx);
        return TRUE;
    }

    /* 1.9.0 REDESIGN — use QUESTS_CreateItem to spawn a FRESH item
     * from its dwCode. This places the item at the FIRST FREE BACKPACK
     * SLOT (or drops it at the player's feet if backpack is full),
     * NOT at the original position the item was at when deposited.
     *
     * The previous bitstream-template approach (fnItemSpawn + fnPlaceInPlayer
     * with NULL parent) preserved the original position from the
     * serialized item, so withdrawn items always tried to land back at
     * the exact (x,y) they were taken from — failing silently if that
     * cell was now occupied by another item. The user reported this as
     * "items don't come out of the chest after I've moved things in
     * inventory".
     *
     * QUESTS_CreateItem (D2Game RVA +0x65DF0) is what the cheat menu
     * already uses to drop items; D2 handles placement automatically. */
    HMODULE hD2Game = GetModuleHandleA("D2Game.dll");
    if (!hD2Game) {
        Log("StkPickup: D2Game.dll not loaded\n");
        return TRUE;
    }
    typedef void* (__fastcall *QUESTS_CreateItem_t)(
        void* pGame, void* pPlayer,
        DWORD dwCode, int nLevel, BYTE nQuality, int bDroppable);
    QUESTS_CreateItem_t fnCI =
        (QUESTS_CreateItem_t)((DWORD)hD2Game + 0x65DF0);

    DWORD pGame = (DWORD)g_cachedPGame;
    if (!pGame) { Log("StkPickup: g_cachedPGame is 0\n"); return TRUE; }

    StashSwap_ResolveOnce();
    void* pServerPlayer = NULL;
    __try { pServerPlayer = GetServerPlayer(pGame); }
    __except(EXCEPTION_EXECUTE_HANDLER) { pServerPlayer = NULL; }
    if (!pServerPlayer) { Log("StkPickup: no server player\n"); return TRUE; }

    /* Spawn the item. nLevel=1 / nQuality=2 (NORMAL) match the cheat
     * menu's defaults for stackable consumables. The 4-byte dwCode is
     * space-padded ('hp1 ' not 'hp1\\0') because that's what D2's
     * item-code hash function expects. */
    void* pItem = NULL;
    __try {
        pItem = fnCI((void*)pGame, pServerPlayer, layoutCode, 1, 2, 1);
    } __except(EXCEPTION_EXECUTE_HANDLER) { pItem = NULL; }

    if (!pItem) {
        /* Inventory full + dropping disabled, or item-creation failure.
         * Per user spec: don't decrement — item stays in chest. */
        Log("StkPickup: QUESTS_CreateItem returned NULL (inv full?) — count NOT decremented (cat=%d cell=%d)\n",
            categoryIdx, cellIdx);
        return TRUE;
    }

    /* Spawn succeeded, decrement the stack. */
    slot->count--;
    if (slot->count == 0) {
        slot->dwCode = 0;
        slot->templateBytes = 0;
        memset(slot->tplBytes, 0, sizeof(slot->tplBytes));
    }
    tab->lastModifiedTick = GetTickCount();

    /* Persist the change immediately so a crash doesn't lose the count. */
    if (g_charName[0]) {
        if (useApScope) StkSaveAP(g_charName);
        else            StkSaveShared();
    }
    Log("StkPickup: cat=%d cell=%d code=%08X -> spawned via QUESTS_CreateItem (count now %u)\n",
        categoryIdx, cellIdx, layoutCode, (unsigned)slot->count);
    return TRUE;
}

/* Public: handle shift+right-click — move the backpack item under the
 * mouse to the first free slot on the current stash page. Returns TRUE
 * when the click was consumed (item moved OR miss — either way don't
 * let D2 process the click in its usual right-click-use handler).
 * Returns FALSE when stash UI isn't open or no item under cursor so
 * the normal right-click action (use potion, etc) can proceed. */
BOOL StashQuickMoveToStash(void) {
    void* pPlayer;
    void* pCliInv;
    void* pSrvInv;
    void* pServerPlayer;
    void* pCliItem;
    int mx, my, col, row;
    int srvX = 0, srvY = 0;
    BOOL ok;
    DWORD pGame;

    Log("StashQuick: entered\n");
    StashSwap_ResolveOnce();
    if (!s_swapResolved) {
        Log("StashQuick: swap not resolved yet\n");
        return FALSE;
    }

    /* Inline screen_shift check */
    {
        HMODULE hDc = GetModuleHandleA("D2Client.dll");
        DWORD v = 0;
        if (!hDc) { Log("StashQuick: no D2Client\n"); return FALSE; }
        __try { v = *(DWORD*)((DWORD)hDc + 0x10B9C4); }
        __except(EXCEPTION_EXECUTE_HANDLER) { v = 0; }
        if (v != 2 && v != 3) {
            Log("StashQuick: screen_shift=%u (need 2 or 3)\n", v);
            return FALSE;
        }

        /* 1.9.4 fix — defer to D2 if a vendor/trade UI is open.
         * koivuklapi bug: shift+rclick on potion in merchant window
         * was being captured by stash quick-move, drawing items from
         * stash instead of filling the belt from the merchant.
         * gpUIState array offsets:
         *   [0x0C] = NPC shop window
         *   [0x17] = multiplayer trade window
         * If either is non-zero, the player is interacting with a
         * vendor/trade — let D2's normal shift+rclick handler do its
         * thing (fill belt from merchant / move item to trade window). */
        DWORD* pUIState = (DWORD*)((BYTE*)hDc + 0x11A6A8);
        DWORD npcShop = 0, mpTrade = 0;
        __try {
            npcShop = pUIState[0x0C];
            mpTrade = pUIState[0x17];
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if (npcShop != 0 || mpTrade != 0) {
            Log("StashQuick: vendor UI open (npcShop=%u mpTrade=%u) — deferring to D2\n",
                npcShop, mpTrade);
            return FALSE;
        }
    }

    /* 1.9.0 — STK withdraw via shift+rclick. If the active stash tab is
     * an STK tab and the click is inside the STK grid area, route to
     * the same StkClickPickup path that left-click uses. Spawns ONE
     * fresh item from the cell's template and places it on the player
     * cursor; decrements the stack count by 1. */
    if (g_activeStashTab >= STASH_STK_AP_BASE) {
        int qmx = MouseX();
        int qmy = MouseY();
        if (StkClickPickup(qmx, qmy)) {
            Log("StashQuick: STK withdraw via shift+rclick OK at (%d,%d)\n", qmx, qmy);
            return TRUE;
        }
        /* If the click was in the STK grid area but didn't hit a
         * populated cell, StkClickPickup already swallowed the click
         * (returned TRUE) — falling here means the click was OUTSIDE
         * the STK grid. Could still be a backpack item the user wants
         * to deposit, so let the standard logic continue. */
    }
    if (!fnGetItemAtXY || !fnPlaceFreePos) {
        Log("StashQuick: missing fn getItemAtXY=%p placeFree=%p\n",
            fnGetItemAtXY, fnPlaceFreePos);
        return FALSE;
    }

    pGame = (DWORD)g_cachedPGame;
    pCliInv = StashSwap_GetPlayerInv();
    pSrvInv = StashSwap_GetServerInv();
    if (!pCliInv || !pSrvInv || !pGame) {
        Log("StashQuick: invs cli=%p srv=%p pGame=%08X\n",
            pCliInv, pSrvInv, pGame);
        return FALSE;
    }

    __try { pServerPlayer = GetServerPlayer(pGame); }
    __except(EXCEPTION_EXECUTE_HANDLER) { pServerPlayer = NULL; }
    if (!pServerPlayer) {
        Log("StashQuick: no server player\n");
        return FALSE;
    }

    if (!fnGetPlayer) { Log("StashQuick: no fnGetPlayer\n"); return FALSE; }
    __try { pPlayer = fnGetPlayer(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { pPlayer = NULL; }
    if (!pPlayer) { Log("StashQuick: fnGetPlayer returned NULL\n"); return FALSE; }

    mx = MouseX(); my = MouseY();

    /* Dump every available mouse coordinate source so we can see which
     * one reflects the user's actual click position. In widescreen modes
     * these can diverge. */
    {
        POINT ptRaw = {0, 0};
        int pmx = pMouseX ? *pMouseX : -1;
        int pmy = pMouseY ? *pMouseY : -1;
        if (GetCursorPos(&ptRaw)) {
            if (g_gameHwnd) ScreenToClient(g_gameHwnd, &ptRaw);
        }
        Log("StashQuick: MOUSE DUMP | MouseX/Y=(%d,%d) | pMouseX/Y=(%d,%d) "
            "| Window=(%ld,%ld) | screen=%dx%d\n",
            mx, my, pmx, pmy, ptRaw.x, ptRaw.y, g_screenW, g_screenH);
    }
    (void)col; (void)row;

    /* Empirically-calibrated grid origins for 1068x600 with Ultimate
     * Inventory mod. Both grids calibrated via 4-corner tests by user:
     *   Backpack  TL=(570,232)  TR=(829,232)  BL=(569,434)  BR=(829,431)
     *   Stash     TL=(246,157)  TR=(504,158)  BL=(242,420)  BR=(509,419)
     * Cell size is 29 px in both. Grid origins (center of cell (0,0)
     * minus half-cell) derived to pixel-accurate values. */
    {
        const int CELL          = 29;
        const int BP_X0         = 556;  /* backpack grid origin X */
        const int BP_Y0         = 218;
        const int ST_X0         = 232;  /* stash grid origin X (10×10) */
        const int ST_Y0         = 143;
        void *pIt, *pN;
        int foundPage = -1;   /* 0=backpack, 4=stash */

        pCliItem = NULL;

        __try { pIt = fnInvFirst(pCliInv); }
        __except(EXCEPTION_EXECUTE_HANDLER) { pIt = NULL; }

        while (pIt) {
            int page;
            __try { pN = fnInvNext(pIt); }
            __except(EXCEPTION_EXECUTE_HANDLER) { pN = NULL; }
            page = StashSwap_GetItemPage(pIt);

            if (page == 0 /*BACKPACK*/ || page == INVPAGE_STASH) {
                int ix, iy;
                BYTE w = 1, h = 1;
                int x0, y0, x1, y1;
                int gridOX = (page == INVPAGE_STASH) ? ST_X0 : BP_X0;
                int gridOY = (page == INVPAGE_STASH) ? ST_Y0 : BP_Y0;
                StashSwap_GetItemXY(pIt, &ix, &iy);
                if (fnItemDims) {
                    __try { fnItemDims(pIt, &w, &h, __FILE__, __LINE__); }
                    __except(EXCEPTION_EXECUTE_HANDLER) { w = 1; h = 1; }
                }
                x0 = gridOX + ix * CELL;
                y0 = gridOY + iy * CELL;
                x1 = x0 + w * CELL;
                y1 = y0 + h * CELL;
                if (mx >= x0 && mx < x1 && my >= y0 && my < y1) {
                    pCliItem = pIt;
                    foundPage = page;
                    Log("StashQuick: HIT item=%p page=%d grid=(%d,%d) %dx%d "
                        "screen=(%d,%d)-(%d,%d)\n",
                        pIt, page, ix, iy, w, h, x0, y0, x1, y1);
                    break;
                }
            }
            pIt = pN;
        }

        if (!pCliItem) {
            Log("StashQuick: mouse (%d,%d) not over any item\n", mx, my);
            return FALSE;
        }

        /* Remember which direction we're moving for later placement. */
        col = foundPage;   /* reuse col var as direction marker */
    }

    /* 1.9.0 — STK auto-deposit. If the active stash tab is one of our
     * STK tabs and the user shift+rclicked an item in their backpack,
     * route the item into the matching layout cell instead of trying to
     * move it through the regular pInventory stash slots (which aren't
     * connected to the STK virtual stack).
     *
     * Item resolution: pItem+0x04 is the dwClassId (Misc.txt/Weapons.txt
     * row index). We compare that against pre-resolved class IDs for the
     * 107 stackable types in the active layout. */
    if (g_activeStashTab >= STASH_STK_AP_BASE && col == 0 /*backpack*/) {
        BOOL useApScope    = (g_activeStashTab < STASH_STK_SH_BASE);
        int  categoryIdx   = useApScope ? (g_activeStashTab - STASH_STK_AP_BASE)
                                        : (g_activeStashTab - STASH_STK_SH_BASE);
        DWORD itemClassId = 0;
        __try { itemClassId = *(DWORD*)((DWORD)pCliItem + 0x04); }
        __except(EXCEPTION_EXECUTE_HANDLER) { itemClassId = 0; }

        Log("StashQuick: STK auto-deposit attempt — tab=%d cat=%d clsId=%u\n",
            g_activeStashTab, categoryIdx, (unsigned)itemClassId);

        /* Resolve the cell's dwCode -> classId on the fly. The layout
         * stores 4-byte space-padded codes; D2Common ord 10601 maps a
         * code to a class id. We do this every deposit (cheap) rather
         * than caching at startup because D2Common may not be loaded
         * when StashInit runs. */
        HMODULE hCommon = GetModuleHandleA("D2Common.dll");
        typedef void* (__stdcall *GetItemFromCode_t)(DWORD dwCode, int* pItemId);
        GetItemFromCode_t fnGetItemFromCode =
            hCommon ? (GetItemFromCode_t)GetProcAddress(hCommon, (LPCSTR)10601) : NULL;

        int matchCellIdx = -1;
        if (fnGetItemFromCode && itemClassId != 0) {
            /* Only origin cells own the slot data — sub-cells of
             * multi-cell items have the same dwCode but no storage.
             * Iterate origins only. */
            for (int ci = 0; ci < STASH_SLOTS_PER_TAB; ci++) {
                StkLayoutEntry* e = &g_stkLayout[categoryIdx][ci];
                if (!e->isOriginCell || e->dwCode == 0) continue;
                int layoutClsId = -1;
                __try { fnGetItemFromCode(e->dwCode, &layoutClsId); }
                __except(EXCEPTION_EXECUTE_HANDLER) { layoutClsId = -1; }
                if (layoutClsId == (int)itemClassId) {
                    matchCellIdx = ci;
                    break;
                }
            }
        }

        if (matchCellIdx < 0) {
            Log("StashQuick: STK no matching cell for clsId=%u in cat=%d — bouncing\n",
                (unsigned)itemClassId, categoryIdx);
            return FALSE;  /* let vanilla / regular stash path handle */
        }

        /* Capture template bytes via D2Common ITEMS_SerializeItemToBitstream
         * (ord 10881). On first deposit the template is stored; subsequent
         * deposits just bump the count. */
        DWORD layoutCode = g_stkLayout[categoryIdx][matchCellIdx].dwCode;
        BYTE  tplBuf[STK_TEMPLATE_BYTES];
        WORD  tplLen = 0;
        if (fnItemSer) {
            BYTE  outBuf[512];
            size_t serBytes = 0;
            __try {
                /* (pItem, pBs, nSize, bServer, bSaveItemInv, bGamble)
                 * - bServer=1: serialize using server-side fields (consistent with stash drain)
                 * - bSaveItemInv=1: include parent-inv-page so deserialize knows where to put
                 * - bGamble=0: not a gambling item */
                serBytes = fnItemSer(pCliItem, outBuf, sizeof(outBuf), 1, 1, 0);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Log("StashQuick: STK ItemSer EXCEPTION\n");
                serBytes = 0;
            }
            if (serBytes > 0 && serBytes <= STK_TEMPLATE_BYTES) {
                memcpy(tplBuf, outBuf, serBytes);
                tplLen = (WORD)serBytes;
            } else if (serBytes > STK_TEMPLATE_BYTES) {
                Log("StashQuick: STK template overflow (got %zu bytes, max %d)\n",
                    serBytes, STK_TEMPLATE_BYTES);
            }
        }

        BOOL ok = StkDeposit(useApScope, categoryIdx, matchCellIdx,
                             layoutCode, tplBuf, tplLen);
        if (!ok) {
            Log("StashQuick: STK deposit REJECTED (cell %d full or invalid)\n",
                matchCellIdx);
            return FALSE;
        }

        /* Item accepted — remove from BOTH client and server inventories.
         * The .d2s save uses server-side state, so removing only from the
         * client side made deposited items reappear in the player's
         * inventory after a logout/login round-trip. We need both sides
         * to be in sync.
         *
         * Find matching pSrvItem at same (x,y) on backpack, same dwCode
         * — server inv is mirror of client inv normally. */
        int cliX, cliY;
        StashSwap_GetItemXY(pCliItem, &cliX, &cliY);

        void* pSrvItem = NULL;
        if (pSrvInv && fnInvFirst && fnInvNext) {
            void* pIt = NULL;
            __try { pIt = fnInvFirst(pSrvInv); }
            __except(EXCEPTION_EXECUTE_HANDLER) { pIt = NULL; }
            while (pIt) {
                int sx, sy;
                int spage = StashSwap_GetItemPage(pIt);
                StashSwap_GetItemXY(pIt, &sx, &sy);
                if (spage == 0 /*BACKPACK*/ && sx == cliX && sy == cliY) {
                    pSrvItem = pIt;
                    break;
                }
                __try { pIt = fnInvNext(pIt); }
                __except(EXCEPTION_EXECUTE_HANDLER) { pIt = NULL; }
            }
        }

        StashSwap_ClearGridCells(pCliInv, pCliItem, cliX, cliY);
        __try { fnInvRemove(pCliInv, pCliItem); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}

        if (pSrvItem) {
            int srvX_d, srvY_d;
            StashSwap_GetItemXY(pSrvItem, &srvX_d, &srvY_d);
            StashSwap_ClearGridCells(pSrvInv, pSrvItem, srvX_d, srvY_d);
            __try { fnInvRemove(pSrvInv, pSrvItem); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
            Log("StashQuick: STK removed server item at (%d,%d)\n", srvX_d, srvY_d);
        } else {
            Log("StashQuick: STK no matching server item — client-only removal "
                "(may persist stale on .d2s save)\n");
        }

        /* Persist the STK file so a crash doesn't lose this deposit. */
        if (g_charName[0]) {
            if (useApScope) StkSaveAP(g_charName);
            else            StkSaveShared();
        }
        Log("StashQuick: STK deposit OK (cat=%d cell=%d code=%08X tplLen=%u)\n",
            categoryIdx, matchCellIdx, layoutCode, (unsigned)tplLen);
        return TRUE;
    }

    /* Determine direction based on clicked item's source page. */
    {
        int srcPage = col;                  /* we stashed direction in col */
        int dstPage;
        int dstRecord;
        int itemX, itemY;
        void* pSrvItem = NULL;
        void *pIt, *pN;
        BOOL okCli = FALSE, okSrv = FALSE;
        int dstClassRecord;

        /* VENDOR CHECK: only trigger if player is interacting with a
         * MONSTER (NPC), not a stash/chest/object. SUNIT_GetInteractUnit
         * returns whatever unit the player is interacting with, which
         * includes the stash chest (unit type 2 = UNIT_OBJECT). We only
         * want vendor sell when the interact target is a shopkeeper
         * NPC (unit type 1 = UNIT_MONSTER). */
        if (srcPage == 0 && fnGetInteract && fnStoresSell) {
            void* pNpc;
            DWORD npcType = 0;
            __try { pNpc = fnGetInteract((void*)g_cachedPGame, pServerPlayer); }
            __except(EXCEPTION_EXECUTE_HANDLER) { pNpc = NULL; }
            if (pNpc) {
                __try { npcType = *(DWORD*)pNpc; }
                __except(EXCEPTION_EXECUTE_HANDLER) { npcType = 0; }
                if (npcType != 1 /*UNIT_MONSTER*/) {
                    Log("StashQuick: interact unit type=%u (not NPC) — skipping vendor path\n",
                        npcType);
                    pNpc = NULL;   /* fall through to stash move */
                }
            }
            if (pNpc) {
                /* Find server-side item matching client item position, then
                 * call D2GAME_STORES_SellItem. Sell needs:
                 *   - npc GUID       (pNpc->dwUnitId at +0x0C)
                 *   - item GUID      (pSrvItem->dwUnitId)
                 *   - item mode      (pSrvItem->dwAnimMode at +0x10)
                 * Server-side item is what the sell function validates. */
                int itx, ity;
                int npcGUID = 0, itemGUID = 0, itemMode = 0;
                int sellRc = -1;
                void *pIt, *pN;
                void* pSrv = NULL;

                StashSwap_GetItemXY(pCliItem, &itx, &ity);
                __try { pIt = fnInvFirst(pSrvInv); }
                __except(EXCEPTION_EXECUTE_HANDLER) { pIt = NULL; }
                while (pIt) {
                    __try { pN = fnInvNext(pIt); }
                    __except(EXCEPTION_EXECUTE_HANDLER) { pN = NULL; }
                    if (StashSwap_GetItemPage(pIt) == 0) {
                        int ix, iy;
                        StashSwap_GetItemXY(pIt, &ix, &iy);
                        if (ix == itx && iy == ity) { pSrv = pIt; break; }
                    }
                    pIt = pN;
                }
                if (pSrv) {
                    __try {
                        npcGUID  = *(int*)((BYTE*)pNpc  + 0x0C);
                        itemGUID = *(int*)((BYTE*)pSrv  + 0x0C);
                        itemMode = *(int*)((BYTE*)pSrv  + 0x10);
                    } __except(EXCEPTION_EXECUTE_HANDLER) {
                        npcGUID = itemGUID = itemMode = 0;
                    }
                    Log("StashQuick: VENDOR detected pNpc=%p npcGUID=%d itemGUID=%d mode=%d\n",
                        pNpc, npcGUID, itemGUID, itemMode);
                    __try {
                        sellRc = fnStoresSell((void*)g_cachedPGame, pServerPlayer,
                                                npcGUID, itemGUID,
                                                (short)itemMode, 0);
                    } __except(EXCEPTION_EXECUTE_HANDLER) { sellRc = -99; }
                    Log("StashQuick: SELL result=%d\n", sellRc);
                    if (sellRc == 0) return TRUE;   /* sold — D2 handles client sync */
                    Log("StashQuick: sell failed (rc=%d) — NOT falling through (would vanish item in shop mode)\n", sellRc);
                    return FALSE;  /* don't risk moving to stash when vendor is active */
                } else {
                    Log("StashQuick: vendor open but no matching server item — abort\n");
                    return FALSE;
                }
            }
        }

        if (srcPage == 0) {
            /* Backpack → stash */
            dstPage = INVPAGE_STASH;
            dstRecord = INVENTORYRECORD_BIG_BANK_1;
        } else {
            /* Stash → backpack */
            dstPage = 0 /*INVPAGE_INVENTORY*/;
            /* Need the player's character class record for backpack.
             * UNITS_GetInventoryRecordId handles the class lookup. */
            if (fnGetInvRec) {
                __try { dstClassRecord = fnGetInvRec(pPlayer, 0, 1); }
                __except(EXCEPTION_EXECUTE_HANDLER) { dstClassRecord = 0; }
            } else {
                dstClassRecord = 0; /* Amazon default */
            }
            dstRecord = dstClassRecord;
        }

        StashSwap_GetItemXY(pCliItem, &itemX, &itemY);

        /* Mirror server item by position (matching source page). */
        __try { pIt = fnInvFirst(pSrvInv); }
        __except(EXCEPTION_EXECUTE_HANDLER) { pIt = NULL; }
        while (pIt) {
            __try { pN = fnInvNext(pIt); }
            __except(EXCEPTION_EXECUTE_HANDLER) { pN = NULL; }
            if (StashSwap_GetItemPage(pIt) == srcPage) {
                int ix, iy;
                StashSwap_GetItemXY(pIt, &ix, &iy);
                if (ix == itemX && iy == itemY) { pSrvItem = pIt; break; }
            }
            pIt = pN;
        }

        /* Remove from both sides at source location. */
        StashSwap_ClearGridCells(pCliInv, pCliItem, itemX, itemY);
        __try { fnInvRemove(pCliInv, pCliItem); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
        if (pSrvItem) {
            StashSwap_GetItemXY(pSrvItem, &srvX, &srvY);
            StashSwap_ClearGridCells(pSrvInv, pSrvItem, srvX, srvY);
            __try { fnInvRemove(pSrvInv, pSrvItem); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
        }

        /* Place into destination at first free slot, server first. */
        if (pSrvItem) {
            __try {
                okSrv = fnPlaceFreePos(pSrvInv, pSrvItem, dstRecord, FALSE,
                                        (BYTE)dstPage, __FILE__, __LINE__);
            } __except(EXCEPTION_EXECUTE_HANDLER) { okSrv = FALSE; }
        } else {
            okSrv = TRUE;  /* no server item to place; treat as OK */
        }

        if (okSrv) {
            __try {
                okCli = fnPlaceFreePos(pCliInv, pCliItem, dstRecord, FALSE,
                                        (BYTE)dstPage, __FILE__, __LINE__);
            } __except(EXCEPTION_EXECUTE_HANDLER) { okCli = FALSE; }
        }

        if (okSrv && okCli) {
            Log("StashQuick: moved page %d → %d (record=%d) OK\n",
                srcPage, dstPage, dstRecord);
            (void)ok;  /* silence unused-var warning */
            return TRUE;
        }

        Log("StashQuick: PlaceFreePos FAILED srv=%d cli=%d — destination full\n",
            (int)okSrv, (int)okCli);
        (void)ok;
        return FALSE;
    }
}

/* Call BEFORE D2 saves the .d2s on character exit. Walks every shadow
 * tab, tries to place its items back into the live stash at ANY free
 * position (falling back from original xy when that cell is taken by
 * something on the currently-active tab). Prevents items on non-active
 * tabs from being permanently lost at session end because D2 only
 * serializes whatever is currently linked in pInventory.
 *
 * Result on next load: all items appear on the active tab compacted
 * together (user has to redistribute across tabs manually). Imperfect,
 * but no data loss. Proper per-item tab persistence is Phase 2c. */
void StashSwapMigrateAllToLiveBeforeSave(void) {
    void *pCliInv, *pSrvInv;
    int tab, i;
    int totalMigrated = 0;
    int totalFailed   = 0;

    StashSwap_ResolveOnce();
    if (!fnInvPlace) return;

    /* At this point in the exit flow, fnGetPlayer() already returns NULL
     * because D2 has nulled the player getter. Use g_lastPlayerPtr which
     * still holds the player we captured a tick ago — the memory is
     * still valid (D2 frees it on game-close, not char-unload). */
    pCliInv = NULL;
    if (g_lastPlayerPtr) {
        __try { pCliInv = *(void**)((BYTE*)g_lastPlayerPtr + D2U_OFF_PINVENTORY); }
        __except(EXCEPTION_EXECUTE_HANDLER) { pCliInv = NULL; }
    }
    pSrvInv = StashSwap_GetServerInv();
    if (!pCliInv) {
        Log("StashSwapMigrate: no client inventory (g_lastPlayerPtr=%p), skipping\n",
            g_lastPlayerPtr);
        return;
    }
    Log("StashSwapMigrate: using cached cliInv=%p srvInv=%p\n", pCliInv, pSrvInv);

    for (tab = 0; tab < STASH_MAX_TABS; tab++) {
        SwapTab* st = &s_swapTab[tab];
        if (st->count == 0) continue;
        for (i = 0; i < st->count; i++) {
            void* pCli = st->items[i].pClientItem;
            void* pSrv = st->items[i].pServerItem;
            int px, py;
            BOOL okCli = FALSE, okSrv = TRUE;

            if (!pCli) continue;

            /* Try original position first. If cell occupied, scan for any
             * free spot in the 10x10 stash grid. */
            for (py = st->items[i].y; py < 10; py++) {
                for (px = (py == st->items[i].y ? st->items[i].x : 0); px < 10; px++) {
                    __try {
                        okCli = fnInvPlace(pCliInv, pCli, px, py,
                                           INVENTORYRECORD_BIG_BANK_1, FALSE, INVPAGE_STASH);
                    } __except(EXCEPTION_EXECUTE_HANDLER) { okCli = FALSE; }
                    if (okCli) goto client_placed;
                }
            }
            client_placed:
            if (!okCli) {
                /* Desperate: scan from (0,0) onward */
                for (py = 0; py < 10 && !okCli; py++) {
                    for (px = 0; px < 10 && !okCli; px++) {
                        __try {
                            okCli = fnInvPlace(pCliInv, pCli, px, py,
                                               INVENTORYRECORD_BIG_BANK_1, FALSE, INVPAGE_STASH);
                        } __except(EXCEPTION_EXECUTE_HANDLER) { okCli = FALSE; }
                    }
                }
            }

            /* Mirror on server at the same cell (px,py chosen above). */
            if (pSrvInv && pSrv && okCli) {
                okSrv = FALSE;
                __try {
                    okSrv = fnInvPlace(pSrvInv, pSrv, px, py,
                                       INVENTORYRECORD_BIG_BANK_1, FALSE, INVPAGE_STASH);
                } __except(EXCEPTION_EXECUTE_HANDLER) { okSrv = FALSE; }
            }

            if (okCli && okSrv) {
                totalMigrated++;
            } else {
                totalFailed++;
                Log("StashSwapMigrate: FAILED tab=%d item=%p cli=%d srv=%d (stash likely full)\n",
                    tab, pCli, (int)okCli, (int)okSrv);
            }
        }
        st->count = 0;
    }

    Log("StashSwapMigrate: %d items restored to live stash, %d failed\n",
        totalMigrated, totalFailed);
}
