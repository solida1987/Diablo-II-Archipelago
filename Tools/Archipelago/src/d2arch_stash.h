/* ================================================================
 * D2Archipelago - Multi-tab Stash (foundation layer)
 *
 * This header declares the data model, globals, sidecar I/O entry
 * points and access-control API for the 10+10 tab stash system.
 *
 * UI rendering, D2 inventory hooks and item-blob serialization are
 * NOT in this layer; they will be wired in later modules.
 *
 * See d2arch_stash.c for the access-control matrix and file format.
 * ================================================================ */
#ifndef D2ARCH_STASH_H
#define D2ARCH_STASH_H

#include <windows.h>

/* ----------------------------------------------------------------
 * TAB LAYOUT
 * ----------------------------------------------------------------
 *   tabGlobalIndex 0..9   -> per-character AP tabs (column 1)
 *   tabGlobalIndex 10..19 -> account-wide shared tabs (column 1)
 *   tabGlobalIndex 20..22 -> per-character STK tabs (column 2, top 3) — 1.9.0 NEW
 *   tabGlobalIndex 23..25 -> account-wide shared STK tabs (column 2, top 3) — 1.9.0 NEW
 *
 * The "global index" is what the UI hands to access-control and
 * visibility queries. Storage pools use the local 0..2 index for STK
 * (Consumables/Runes/Gems) and the local 0..9 for AP/SH.
 *
 * STK tabs are slot-locked: each cell has a designated dwCode and
 * accepts only items whose pItemData->dwCode matches. Black inactive
 * cells (dwCode == 0) reject all drops. See StkLayoutEntry below.
 * ---------------------------------------------------------------- */
#define STASH_NUM_SHARED_TABS    10
#define STASH_NUM_AP_TABS        10
#define STASH_NUM_STK_TABS       3   /* per scope (Consumables / Runes / Gems) */
#define STASH_MAX_TABS           (STASH_NUM_SHARED_TABS + STASH_NUM_AP_TABS + 2 * STASH_NUM_STK_TABS)  /* 26 */

#define STASH_STK_AP_BASE        20  /* g_stashStkAp[i] -> tabGlobalIndex 20+i */
#define STASH_STK_SH_BASE        23  /* g_stashStkSh[i] -> tabGlobalIndex 23+i */

/* STK tab category indices (0..STASH_NUM_STK_TABS-1) */
#define STK_TAB_CONSUMABLES      0
#define STK_TAB_RUNES            1
#define STK_TAB_GEMS             2

/* Max stack count for an STK cell. User spec: 999. */
#define STK_MAX_STACK_COUNT      999

/* Max bitstream length we keep as the per-cell template. D2 stackable
 * items serialize to well under 64 bytes, but we leave headroom for
 * runewords / quivers with quantity. */
#define STK_TEMPLATE_BYTES       128

/* Ultimate Inventory Stash & Cube mod by Shaky is installed. See
 * data/global/excel/inventory.txt "Big Bank Page 1": gridX=10, gridY=10.
 * Panel position: gridLeft=16, gridTop=82 (within the stash window). */
#define STASH_GRID_COLS          10
#define STASH_GRID_ROWS          10
#define STASH_SLOTS_PER_TAB      (STASH_GRID_COLS * STASH_GRID_ROWS)  /* 100 */

/* Max stack size for virtualized (non-physical) stacks of runes/gems/etc. */
#define STASH_MAX_STACK_SIZE     9999

/* Sidecar file signature + version */
#define STASH_FILE_MAGIC         0xAA55AA55
#define STASH_FILE_VERSION       1

/* ----------------------------------------------------------------
 * SLOT / TAB DATA MODEL
 * ----------------------------------------------------------------
 * A slot is one of three states:
 *   occupied == 0  -> empty
 *   occupied == 1  -> real item (underlying item bytes stored in the
 *                     reserved[] scratch for now; will be replaced
 *                     with a proper item-blob pointer later)
 *   occupied == 2  -> virtualized stack (stack.count > 1 of some
 *                     stackable class — runes, gems, scrolls, ...)
 *
 * The StashStackMeta.itemClassId is a TBD classification key. For
 * now it is treated opaquely; StashIsStackableType() will decide
 * what classifies as stackable once the D2 item-type mapping is
 * known.
 * ---------------------------------------------------------------- */
typedef struct {
    DWORD   itemClassId;     /* opaque classification key (TBD)                */
    int     count;           /* 1..STASH_MAX_STACK_SIZE                        */
    /* Future: array of underlying item blobs that make up the stack. */
} StashStackMeta;

typedef struct {
    int             occupied;       /* 0=empty, 1=real item, 2=virtual stack   */
    StashStackMeta  stack;           /* valid when occupied == 2                */
    /* Future: item blob pointer OR inline item bytes when occupied == 1.
     * reserved[] is padding that keeps the sidecar file layout stable
     * across early revisions — real item serialization will re-use these
     * bytes. Do NOT shrink without bumping STASH_FILE_VERSION. */
    BYTE            reserved[64];
} StashSlot;

typedef struct {
    int         tabIndex;            /* 0..9 within its pool                   */
    wchar_t     tabName[32];         /* user-facing name, default "Tab N"      */
    StashSlot   slots[STASH_SLOTS_PER_TAB];
    DWORD       lastModifiedTick;    /* GetTickCount() at last mutation        */
} StashTab;

/* ----------------------------------------------------------------
 * GLOBAL STATE
 * ----------------------------------------------------------------
 * NOTE: Although these are declared `extern` here to make the module
 * interface explicit, the project is a unity build (see d2arch.c)
 * and the definitions in d2arch_stash.c have internal linkage like
 * every other module. Callers inside the same translation unit see
 * them by direct reference; no cross-TU linkage is performed.
 * ---------------------------------------------------------------- */
extern StashTab g_stashShared[STASH_NUM_SHARED_TABS];
extern StashTab g_stashAP[STASH_NUM_AP_TABS];
extern DWORD    g_stashSharedGold;
extern DWORD    g_stashAPGold;

/* ----------------------------------------------------------------
 * 1.9.0 NEW: STK (Stackable) tab system
 * ----------------------------------------------------------------
 * Three categories × two scopes = 6 underlying tabs. Each tab uses
 * the same 10x10 grid as the regular tabs but each cell is bound to
 * a specific dwCode (the layout in g_stkLayout). Items can ONLY be
 * deposited in their designated cell; any other cell is "black"
 * (dwCode==0) and rejects all drops.
 *
 * Pickup is one-at-a-time: each click on an occupied cell spawns
 * ONE fresh item from the saved template bitstream and decrements
 * the count by 1. Drops auto-consolidate (existing stack of same
 * code grows by 1; otherwise the cell's count goes 0->1).
 * ---------------------------------------------------------------- */
typedef struct {
    DWORD       dwCode;          /* space-padded 4-char code, 0 = black cell */
    const char* invFile;         /* DC6 path under data\global\items\stk\, NULL for black */
    const char* displayName;     /* human-readable, used in tooltip / notify */
    void*       pCachedCel;      /* fnCelFileLoad result, lazy-loaded on first render */
    BYTE        cellsW;          /* item width in cells (1 normally; 2 for hft) */
    BYTE        cellsH;          /* item height in cells (1 for most; 2 for tomes/pk1-3, 3 for quivers/hft) */
    BYTE        isOriginCell;    /* TRUE for the top-left of a multi-cell item; FALSE for "covered" sub-cells */
    BYTE        _pad;
} StkLayoutEntry;

typedef struct {
    DWORD       dwCode;          /* matches the layout cell's dwCode when occupied; 0 = empty */
    DWORD       count;           /* 1..STK_MAX_STACK_COUNT, 0 = empty cell */
    WORD        templateBytes;   /* bytes used in template[] */
    BYTE        tplBytes[STK_TEMPLATE_BYTES];   /* serialized item used to spawn fresh copies */
} StkSlot;

typedef struct {
    StkSlot     slots[STASH_SLOTS_PER_TAB];     /* 100 cells, indexed y*10+x */
    DWORD       lastModifiedTick;
} StkTab;

extern StkLayoutEntry g_stkLayout[STASH_NUM_STK_TABS][STASH_SLOTS_PER_TAB];
extern StkTab         g_stashStkAp[STASH_NUM_STK_TABS];   /* per-character */
extern StkTab         g_stashStkSh[STASH_NUM_STK_TABS];   /* account-wide */

/* Current view state (driven by UI) */
extern int  g_activeStashTab;        /* global index 0..19                     */
extern BOOL g_stashOpen;

/* ----------------------------------------------------------------
 * LIFECYCLE
 * ---------------------------------------------------------------- */
void StashInit(void);                /* zero state + default tab names         */
void StashResetAll(void);            /* clear all slots + gold                 */

/* ----------------------------------------------------------------
 * SAVE / LOAD (sidecar files in GetArchDir())
 * ----------------------------------------------------------------
 *   shared_stash.dat             — account-wide, survives char delete
 *   ap_stash_<charName>.dat      — per-AP-character, deleted on delete
 *
 * All four functions return TRUE on "state is now well-defined"
 * (including "no file, using defaults"). They only return FALSE on
 * I/O errors we cannot recover from.
 * ---------------------------------------------------------------- */
BOOL StashLoadShared(void);
BOOL StashSaveShared(void);
BOOL StashLoadAP(const char* charName);
BOOL StashSaveAP(const char* charName);
void StashDeleteAPFile(const char* charName);

/* 1.9.0 NEW — STK tab sidecar I/O. ap_stash_stk_<char>.dat holds all 3
 * STK_AP tabs; shared_stash_stk.dat holds all 3 STK_SH tabs. */
BOOL StkLoadShared(void);
BOOL StkSaveShared(void);
BOOL StkLoadAP(const char* charName);
BOOL StkSaveAP(const char* charName);

/* 1.9.0 NEW — STK helpers used by deposit / pickup / render paths. */
int  StkFindCellForCode(int tabCategoryIdx, DWORD dwCode);  /* returns cellIdx 0..99, or -1 */
BOOL StkDeposit(BOOL useApScope, int tabCategoryIdx, int cellIdx,
                DWORD dwCode, const BYTE* templateBytes, WORD templateLen);
BOOL StkPickupOne(BOOL useApScope, int tabCategoryIdx, int cellIdx,
                  BYTE* outTemplate, WORD* outLen);

/* 1.9.0 NEW — reset in-memory STK state on character change. Clears the
 * AP-side per-character tables (the SH-side stays — it's account-wide
 * and will be re-loaded from disk on next OnCharacterLoad anyway) AND
 * invalidates all cached fnCelLoad pointers in g_stkLayout so next
 * render reloads cleanly. Without this, stale cel pointers from the
 * previous character's MPQ context corrupt the rendered icons. */
void StkResetOnPlayerGone(void);

/* ----------------------------------------------------------------
 * ACCESS CONTROL
 * ----------------------------------------------------------------
 * Matrix (current character mode -> tab type -> access):
 *
 *   non-AP char:                AP tabs = NONE     shared = READWRITE
 *   AP char, pre-goal:          AP tabs = RW       shared = LOCKED
 *   AP char, post-goal:         AP tabs = RW       shared = READWRITE
 *
 * LOCKED tabs are visible but greyed out (no interactions). NONE
 * tabs are entirely hidden from the tab bar.
 * ---------------------------------------------------------------- */
typedef enum {
    STASH_ACCESS_NONE,               /* hide tab entirely                      */
    STASH_ACCESS_READONLY,           /* draw items, disallow mutations         */
    STASH_ACCESS_READWRITE,          /* full access                            */
    STASH_ACCESS_LOCKED              /* visible but greyed out (no interact)   */
} StashAccess;

StashAccess StashGetAccess(int tabGlobalIndex);
BOOL        StashIsTabVisible(int tabGlobalIndex);
int         StashTabCount(void);     /* 10 or 20 depending on AP+goal state   */

/* ----------------------------------------------------------------
 * ITEM CLASSIFICATION (stackable whitelist)
 * ----------------------------------------------------------------
 * Runes, gems, jewels, scrolls (TP/ID), arrows, bolts stack.
 * Charms do NOT stack. Other items do NOT stack.
 *
 * This is currently a stub — the real implementation needs a
 * concrete mapping from D2 item-type codes to classification. See
 * d2arch_stash.c for the TODO.
 * ---------------------------------------------------------------- */
BOOL StashIsStackableType(DWORD itemClassId);

#endif /* D2ARCH_STASH_H */
