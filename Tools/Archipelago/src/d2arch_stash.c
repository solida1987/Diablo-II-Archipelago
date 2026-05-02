/* ================================================================
 * D2Archipelago - Multi-tab Stash (foundation layer)
 *
 * Data structures, globals, sidecar I/O scaffolding and access
 * control for the 10 AP + 10 shared tab stash system.
 *
 * UI + item-serialization live in later modules. This file has no
 * D2-side hooks of its own — it only provides the storage + policy.
 *
 * ---------------------------------------------------------------
 * BUILD NOTE - unity-build integration
 * ---------------------------------------------------------------
 * The project is a unity build: d2arch.c #includes every .c file
 * in a fixed order. As a result, Log / GetArchDir / g_charName /
 * g_apMode / g_apGoalComplete all have INTERNAL linkage (they are
 * declared `static` in d2arch_config.c, d2arch_helpers.c,
 * d2arch_input.c respectively), yet they are visible to every
 * include that comes LATER in d2arch.c because it is the same
 * translation unit.
 *
 * This file MUST therefore be #included AFTER:
 *   d2arch_config.c   (for Log)
 *   d2arch_input.c    (for g_charName, g_apMode, g_apGoalComplete)
 *   d2arch_helpers.c  (for GetArchDir)
 *
 * The task spec asked for `extern` declarations of those symbols.
 * Because they are static in their defining modules, an `extern`
 * here would either fail to resolve (separate TU) or clash with
 * the internal-linkage definition (same TU). Matching the existing
 * convention set by d2arch_itemlog.c, d2arch_render.c, etc., we
 * reference the statics directly and rely on include order. The
 * header d2arch_stash.h still exposes `extern` signatures for the
 * stash globals as an interface contract.
 *
 * If this module is ever split to a separate TU, the listed
 * dependencies must be promoted to external linkage first (see the
 * mismatch notes at the bottom of the agent report).
 * ================================================================ */

#include "d2arch_stash.h"
#include <stdio.h>       /* fopen, fwrite, fread, fclose              */
#include <stdlib.h>      /* malloc/free (unused today, reserved)      */
#include <string.h>      /* memset, memcpy, strlen                    */
#include <wchar.h>       /* _snwprintf                                */

/* ----------------------------------------------------------------
 * GLOBAL STATE (definitions)
 * ---------------------------------------------------------------- */
StashTab g_stashShared[STASH_NUM_SHARED_TABS];
StashTab g_stashAP[STASH_NUM_AP_TABS];
DWORD    g_stashSharedGold = 0;
DWORD    g_stashAPGold     = 0;

int      g_activeStashTab  = 0;
BOOL     g_stashOpen       = FALSE;

/* ----------------------------------------------------------------
 * INTERNAL HELPERS
 * ---------------------------------------------------------------- */

/* Assign default tab names "Tab 1" .. "Tab N" (wide char) into the
 * tabs of a pool and reset their tabIndex fields. */
static void StashInitPool(StashTab* pool, int poolSize) {
    int t;
    for (t = 0; t < poolSize; t++) {
        StashTab* tab = &pool[t];
        int s;

        memset(tab, 0, sizeof(StashTab));
        tab->tabIndex = t;
        tab->lastModifiedTick = 0;

        /* Default display name "Tab N" where N is 1-based. */
        _snwprintf(tab->tabName,
                   (sizeof(tab->tabName) / sizeof(tab->tabName[0])) - 1,
                   L"Tab %d", t + 1);
        tab->tabName[(sizeof(tab->tabName) / sizeof(tab->tabName[0])) - 1] = 0;

        for (s = 0; s < STASH_SLOTS_PER_TAB; s++) {
            tab->slots[s].occupied = 0;
            tab->slots[s].stack.itemClassId = 0;
            tab->slots[s].stack.count = 0;
            memset(tab->slots[s].reserved, 0, sizeof(tab->slots[s].reserved));
        }
    }
}

/* Simple XOR + 1-bit left-rotate checksum over a byte buffer. Matches
 * the style used by d2s_CalcChecksum() in d2arch_save.c (rotate-add)
 * but uses XOR-then-rotate so a trailing zero checksum field does
 * not behave as a no-op across rewrites. */
static DWORD ComputeStashChecksum(const void* data, size_t len) {
    const unsigned char* p = (const unsigned char*)data;
    DWORD sum = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        sum ^= (DWORD)p[i];
        sum = (sum << 1) | (sum >> 31);
    }
    return sum;
}

/* Build "<archDir>/<fileName>" into out. Returns 0 on failure, 1 ok. */
static int EnsureArchDir(char* out, size_t outSize) {
    if (!out || outSize == 0) return 0;
    /* GetArchDir already creates the directory if it does not exist
     * and terminates with a trailing backslash. */
    GetArchDir(out, (int)outSize);
    return (out[0] != 0) ? 1 : 0;
}

/* Join <archDir>/<leaf> into outPath. Returns 0 if the path would
 * overflow outSize, 1 otherwise. Used for SHARED files (no char suffix). */
static int BuildArchPath(char* outPath, size_t outSize, const char* leaf) {
    char dir[MAX_PATH];
    size_t dirLen, leafLen;

    if (!outPath || outSize == 0 || !leaf) return 0;
    if (!EnsureArchDir(dir, sizeof(dir))) return 0;

    dirLen = strlen(dir);
    leafLen = strlen(leaf);
    if (dirLen + leafLen + 1 > outSize) return 0;

    memcpy(outPath, dir, dirLen);
    memcpy(outPath + dirLen, leaf, leafLen + 1);
    return 1;
}

/* 1.9.0: Join <saveDir>/<leaf> into outPath. Used for PER-CHARACTER
 * files (state, slots, stash, collections, etc.) which now live next
 * to the .d2s files in Game/Save/ instead of being mixed into the
 * shared Game/Archipelago/ folder. Returns 0 on overflow. */
static int BuildCharSavePath(char* outPath, size_t outSize, const char* leaf) {
    char dir[MAX_PATH];
    size_t dirLen, leafLen;

    if (!outPath || outSize == 0 || !leaf) return 0;
    GetCharFileDir(dir, sizeof(dir));
    if (!dir[0]) return 0;

    dirLen = strlen(dir);
    leafLen = strlen(leaf);
    if (dirLen + leafLen + 1 > outSize) return 0;

    memcpy(outPath, dir, dirLen);
    memcpy(outPath + dirLen, leaf, leafLen + 1);
    return 1;
}

/* Build "ap_stash_<charName>.dat" into outLeaf. Returns 0 on invalid
 * input / overflow. charName is the ANSI char name from g_charName. */
static int BuildAPStashLeaf(char* outLeaf, size_t outSize, const char* charName) {
    int written;
    if (!outLeaf || outSize == 0 || !charName || !charName[0]) return 0;
    written = _snprintf(outLeaf, outSize, "ap_stash_%s.dat", charName);
    if (written < 0 || (size_t)written >= outSize) return 0;
    outLeaf[outSize - 1] = 0;
    return 1;
}

/* ----------------------------------------------------------------
 * LIFECYCLE
 * ---------------------------------------------------------------- */
/* Forward decls so StashInit can call STK init helpers defined later
 * in this file. Both are static — visible only within this TU. */
static void StkInitLayouts(void);
static void StkResetAll(void);

void StashInit(void) {
    StashInitPool(g_stashShared, STASH_NUM_SHARED_TABS);
    StashInitPool(g_stashAP,     STASH_NUM_AP_TABS);
    g_stashSharedGold = 0;
    g_stashAPGold     = 0;
    g_activeStashTab  = 0;
    g_stashOpen       = FALSE;
    /* 1.9.0 — initialize the STK system. Layouts are static data; in-memory
     * tabs reset to empty here and load from sidecars later in
     * OnCharacterLoad / DLL startup. */
    StkInitLayouts();
    StkResetAll();
    Log("Stash: initialized (shared=%d ap=%d stk=%d slots/tab=%d)\n",
        STASH_NUM_SHARED_TABS, STASH_NUM_AP_TABS, STASH_NUM_STK_TABS,
        STASH_SLOTS_PER_TAB);
}

void StashResetAll(void) {
    StashInitPool(g_stashShared, STASH_NUM_SHARED_TABS);
    StashInitPool(g_stashAP,     STASH_NUM_AP_TABS);
    g_stashSharedGold = 0;
    g_stashAPGold     = 0;
    StkResetAll();
    Log("Stash: reset all (gold cleared, all slots emptied)\n");
}

/* ----------------------------------------------------------------
 * SIDECAR FILE FORMAT
 * ----------------------------------------------------------------
 * Header (20 bytes):
 *   DWORD magic      = STASH_FILE_MAGIC
 *   DWORD version    = STASH_FILE_VERSION
 *   DWORD gold       = pool gold
 *   DWORD reserved0  = 0  (future: flags)
 *   DWORD numTabs    = STASH_NUM_*_TABS (sanity vs version bump)
 * Body:
 *   StashTab[numTabs]                      (raw struct dump)
 * Trailer:
 *   DWORD checksum   = XOR-rotate of every byte above
 * ---------------------------------------------------------------- */
typedef struct {
    DWORD magic;
    DWORD version;
    DWORD gold;
    DWORD reserved0;
    DWORD numTabs;
} StashFileHeader;

/* Shared by both load paths. Reads file at `path`, verifies magic/
 * version/checksum, and on success copies the gold + tab data into
 * outGold/outTabs (sized for `expectedTabs`). Returns 1 on success,
 * 0 on "file missing / corrupt — defaults should be used", -1 on
 * fatal I/O error. */
static int ReadStashFile(const char* path,
                         DWORD* outGold,
                         StashTab* outTabs,
                         int expectedTabs) {
    FILE* f;
    StashFileHeader hdr;
    size_t bodyBytes = (size_t)expectedTabs * sizeof(StashTab);
    DWORD storedChecksum = 0;
    DWORD calcChecksum;
    unsigned char* buf = NULL;
    size_t bufLen;
    size_t rd;

    if (!path || !outGold || !outTabs || expectedTabs <= 0) return -1;

    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        /* File doesn't exist -> caller should keep defaults. Not an error. */
        return 0;
    }

    f = fopen(path, "rb");
    if (!f) {
        Log("Stash: ReadStashFile open failed err=%lu path='%s'\n",
            GetLastError(), path);
        return -1;
    }

    /* Allocate a buffer that holds header + body (for checksum recompute) */
    bufLen = sizeof(StashFileHeader) + bodyBytes;
    buf = (unsigned char*)malloc(bufLen);
    if (!buf) {
        fclose(f);
        Log("Stash: ReadStashFile malloc failed (%zu bytes)\n", bufLen);
        return -1;
    }

    rd = fread(buf, 1, bufLen, f);
    if (rd != bufLen) {
        Log("Stash: file too short (got %zu want %zu) path='%s' -> defaults\n",
            rd, bufLen, path);
        free(buf);
        fclose(f);
        return 0;
    }

    if (fread(&storedChecksum, 1, sizeof(storedChecksum), f)
            != sizeof(storedChecksum)) {
        Log("Stash: no checksum trailer path='%s' -> defaults\n", path);
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);

    memcpy(&hdr, buf, sizeof(hdr));

    if (hdr.magic != STASH_FILE_MAGIC) {
        Log("Stash: bad magic 0x%08lX (expected 0x%08lX) path='%s' -> defaults\n",
            (unsigned long)hdr.magic, (unsigned long)STASH_FILE_MAGIC, path);
        free(buf);
        return 0;
    }
    if (hdr.version != STASH_FILE_VERSION) {
        Log("Stash: version mismatch got=%lu want=%lu path='%s' -> defaults\n",
            (unsigned long)hdr.version, (unsigned long)STASH_FILE_VERSION, path);
        free(buf);
        return 0;
    }
    if ((int)hdr.numTabs != expectedTabs) {
        Log("Stash: tab count mismatch got=%lu want=%d path='%s' -> defaults\n",
            (unsigned long)hdr.numTabs, expectedTabs, path);
        free(buf);
        return 0;
    }

    calcChecksum = ComputeStashChecksum(buf, bufLen);
    if (calcChecksum != storedChecksum) {
        Log("Stash: checksum mismatch got=0x%08lX want=0x%08lX path='%s' -> defaults\n",
            (unsigned long)calcChecksum, (unsigned long)storedChecksum, path);
        free(buf);
        return 0;
    }

    *outGold = hdr.gold;
    memcpy(outTabs, buf + sizeof(hdr), bodyBytes);
    free(buf);
    return 1;
}

/* Shared writer. Returns 1 on success, 0 on failure. */
static int WriteStashFile(const char* path,
                          DWORD gold,
                          const StashTab* tabs,
                          int numTabs) {
    FILE* f;
    StashFileHeader hdr;
    size_t bodyBytes = (size_t)numTabs * sizeof(StashTab);
    size_t bufLen;
    unsigned char* buf;
    DWORD checksum;
    size_t wrote;

    if (!path || !tabs || numTabs <= 0) return 0;

    hdr.magic     = STASH_FILE_MAGIC;
    hdr.version   = STASH_FILE_VERSION;
    hdr.gold      = gold;
    hdr.reserved0 = 0;
    hdr.numTabs   = (DWORD)numTabs;

    bufLen = sizeof(hdr) + bodyBytes;
    buf = (unsigned char*)malloc(bufLen);
    if (!buf) {
        Log("Stash: WriteStashFile malloc failed (%zu bytes)\n", bufLen);
        return 0;
    }
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), tabs, bodyBytes);

    checksum = ComputeStashChecksum(buf, bufLen);

    f = fopen(path, "wb");
    if (!f) {
        Log("Stash: WriteStashFile open failed err=%lu path='%s'\n",
            GetLastError(), path);
        free(buf);
        return 0;
    }

    wrote = fwrite(buf, 1, bufLen, f);
    if (wrote != bufLen) {
        Log("Stash: short write (got %zu want %zu) path='%s'\n",
            wrote, bufLen, path);
        fclose(f);
        free(buf);
        return 0;
    }
    if (fwrite(&checksum, 1, sizeof(checksum), f) != sizeof(checksum)) {
        Log("Stash: checksum write failed path='%s'\n", path);
        fclose(f);
        free(buf);
        return 0;
    }

    fclose(f);
    free(buf);
    Log("Stash: wrote %zu bytes (+cksum) path='%s'\n", bufLen, path);
    return 1;
}

/* ----------------------------------------------------------------
 * SHARED STASH LOAD/SAVE
 * ---------------------------------------------------------------- */
BOOL StashLoadShared(void) {
    char path[MAX_PATH];
    int rc;

    if (!BuildArchPath(path, sizeof(path), "shared_stash.dat")) {
        Log("Stash: BuildArchPath failed for shared_stash.dat\n");
        return FALSE;
    }

    rc = ReadStashFile(path,
                       &g_stashSharedGold,
                       g_stashShared,
                       STASH_NUM_SHARED_TABS);
    if (rc == 1) {
        Log("Stash: shared loaded gold=%lu tabs=%d\n",
            (unsigned long)g_stashSharedGold, STASH_NUM_SHARED_TABS);
        return TRUE;
    }
    if (rc == 0) {
        /* Missing file or corrupt -> defaults. Caller expects TRUE. */
        StashInitPool(g_stashShared, STASH_NUM_SHARED_TABS);
        g_stashSharedGold = 0;
        Log("Stash: shared using defaults\n");
        return TRUE;
    }
    /* rc < 0 -> fatal I/O */
    return FALSE;
}

BOOL StashSaveShared(void) {
    char path[MAX_PATH];
    if (!BuildArchPath(path, sizeof(path), "shared_stash.dat")) {
        Log("Stash: BuildArchPath failed for shared_stash.dat (save)\n");
        return FALSE;
    }
    return WriteStashFile(path,
                          g_stashSharedGold,
                          g_stashShared,
                          STASH_NUM_SHARED_TABS) ? TRUE : FALSE;
}

/* ----------------------------------------------------------------
 * AP STASH LOAD/SAVE (per character)
 * ---------------------------------------------------------------- */
BOOL StashLoadAP(const char* charName) {
    char leaf[MAX_PATH];
    char path[MAX_PATH];
    int rc;

    if (!charName || !charName[0]) {
        Log("Stash: StashLoadAP called with empty charName\n");
        return FALSE;
    }
    if (!BuildAPStashLeaf(leaf, sizeof(leaf), charName)) return FALSE;
    if (!BuildCharSavePath(path, sizeof(path), leaf))    return FALSE;

    rc = ReadStashFile(path,
                       &g_stashAPGold,
                       g_stashAP,
                       STASH_NUM_AP_TABS);
    if (rc == 1) {
        Log("Stash: AP loaded '%s' gold=%lu tabs=%d\n",
            charName, (unsigned long)g_stashAPGold, STASH_NUM_AP_TABS);
        return TRUE;
    }
    if (rc == 0) {
        StashInitPool(g_stashAP, STASH_NUM_AP_TABS);
        g_stashAPGold = 0;
        Log("Stash: AP '%s' using defaults\n", charName);
        return TRUE;
    }
    return FALSE;
}

BOOL StashSaveAP(const char* charName) {
    char leaf[MAX_PATH];
    char path[MAX_PATH];

    if (!charName || !charName[0]) {
        Log("Stash: StashSaveAP called with empty charName\n");
        return FALSE;
    }
    if (!BuildAPStashLeaf(leaf, sizeof(leaf), charName)) return FALSE;
    if (!BuildCharSavePath(path, sizeof(path), leaf))    return FALSE;

    return WriteStashFile(path,
                          g_stashAPGold,
                          g_stashAP,
                          STASH_NUM_AP_TABS) ? TRUE : FALSE;
}

void StashDeleteAPFile(const char* charName) {
    char leaf[MAX_PATH];
    char path[MAX_PATH];

    if (!charName || !charName[0]) return;
    if (!BuildAPStashLeaf(leaf, sizeof(leaf), charName)) return;
    if (!BuildCharSavePath(path, sizeof(path), leaf))    return;

    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        Log("Stash: StashDeleteAPFile: no file for '%s' (nothing to do)\n", charName);
        return;
    }
    if (DeleteFileA(path)) {
        Log("Stash: AP sidecar deleted for '%s' (path='%s')\n", charName, path);
    } else {
        Log("Stash: StashDeleteAPFile failed err=%lu path='%s'\n",
            GetLastError(), path);
    }
}

/* ----------------------------------------------------------------
 * ACCESS CONTROL
 * ----------------------------------------------------------------
 *   tabGlobal 0..9   -> AP tab index
 *   tabGlobal 10..19 -> shared tab index (subtract 10)
 *   tabGlobal 20..22 -> STK_AP tab (Consumables/Runes/Gems) — 1.9.0
 *   tabGlobal 23..25 -> STK_SH tab (Consumables/Runes/Gems) — 1.9.0
 *
 * Matrix:
 *   non-AP char:          AP = NONE        shared = READWRITE   STK_AP = NONE   STK_SH = RW
 *   AP pre-goal:          AP = READWRITE   shared = NONE        STK_AP = RW     STK_SH = NONE
 *   AP post-goal:         AP = READWRITE   shared = READWRITE   STK_AP = RW     STK_SH = RW
 *
 * The STK rules mirror the existing AP/SH ones exactly, per user spec:
 * "Standalone skal kun vises, når man er på standalone, og AP skal
 * kun vises, når man er på AP".
 * ---------------------------------------------------------------- */
StashAccess StashGetAccess(int tabGlobalIndex) {
    if (tabGlobalIndex < 0 || tabGlobalIndex >= STASH_MAX_TABS)
        return STASH_ACCESS_NONE;

    if (tabGlobalIndex < STASH_NUM_AP_TABS) {
        /* AP pool (0..9) — red buttons */
        if (!g_apMode) return STASH_ACCESS_NONE;   /* hidden entirely for non-AP chars */
        return STASH_ACCESS_READWRITE;             /* always available for AP chars */
    }

    if (tabGlobalIndex < STASH_NUM_AP_TABS + STASH_NUM_SHARED_TABS) {
        /* Shared pool (10..19) — orange buttons */
        if (!g_apMode) return STASH_ACCESS_READWRITE;  /* non-AP char: only source of stash */
        /* AP char: hidden until goal complete, then unlocked. */
        return g_apGoalComplete ? STASH_ACCESS_READWRITE : STASH_ACCESS_NONE;
    }

    if (tabGlobalIndex < STASH_STK_AP_BASE + STASH_NUM_STK_TABS) {
        /* 1.9.0 — STK_AP pool (20..22) — yellow buttons, mirrors AP rule */
        if (!g_apMode) return STASH_ACCESS_NONE;
        return STASH_ACCESS_READWRITE;
    }

    if (tabGlobalIndex < STASH_STK_SH_BASE + STASH_NUM_STK_TABS) {
        /* 1.9.0 — STK_SH pool (23..25) — yellow buttons, mirrors SH rule */
        if (!g_apMode) return STASH_ACCESS_READWRITE;
        return g_apGoalComplete ? STASH_ACCESS_READWRITE : STASH_ACCESS_NONE;
    }

    return STASH_ACCESS_NONE;
}

BOOL StashIsTabVisible(int tabGlobalIndex) {
    return (StashGetAccess(tabGlobalIndex) != STASH_ACCESS_NONE) ? TRUE : FALSE;
}

int StashTabCount(void) {
    int i, count = 0;
    for (i = 0; i < STASH_MAX_TABS; i++) {
        if (StashIsTabVisible(i)) count++;
    }
    return count;
}

/* ----------------------------------------------------------------
 * ITEM CLASSIFICATION (stackable whitelist)
 * ----------------------------------------------------------------
 * TODO: needs real D2 item-type classification.
 *
 * The ruleset we want is:
 *   Stackable : runes, gems (skull+chipped+flawed+...), jewels,
 *               town-portal / identify scrolls, arrows, bolts.
 *   NOT stack : charms (small/large/grand), everything else.
 *
 * D2 classifies items by a 3- or 4-char `code` in weapons.txt /
 * armor.txt / misc.txt, and groups them via `type`/`type2` columns
 * pointing into itemtypes.txt. The runtime usually compares
 * UnitAny.pItemData->dwType against those ids. No module currently
 * touches this, so the key format for StashStackMeta.itemClassId
 * is undecided.
 *
 * Until the item-blob integration lands, we return FALSE and treat
 * every slot as non-stackable; the UI layer can safely assume one
 * item per slot. Revisit when the D2 item-type enum is wired up.
 * ---------------------------------------------------------------- */
BOOL StashIsStackableType(DWORD itemClassId) {
    (void)itemClassId;   /* suppress unused-parameter warning                 */
    /* TODO: needs real D2 item-type classification — currently all return FALSE */
    return FALSE;
}

/* ================================================================
 * 1.9.0 NEW — STK (Stackable) Tab System
 *
 * Three category tabs (Consumables / Runes / Gems) × two scopes
 * (per-character AP and account-wide shared) = 6 underlying tabs.
 * Each tab uses the same 10x10 grid as the regular AP/SH tabs but
 * each cell is bound to a specific dwCode via g_stkLayout. Items
 * can ONLY be deposited in their designated cell; any other cell
 * is "black" (dwCode==0) and rejects all drops.
 *
 * Access mirrors the AP/SH matrix: STK_AP visible only in AP mode,
 * STK_SH visible in standalone or post-goal AP. See StashGetAccess
 * for the rules.
 * ================================================================ */

StkLayoutEntry g_stkLayout[STASH_NUM_STK_TABS][STASH_SLOTS_PER_TAB];
StkTab         g_stashStkAp[STASH_NUM_STK_TABS];
StkTab         g_stashStkSh[STASH_NUM_STK_TABS];

/* 1.9.0 Phase 4 — bad-drop flash state. Set when a deposit attempt
 * is rejected (wrong-type cell or stack full). The render path checks
 * `(GetTickCount() - g_stkBadDropTick) < STK_BAD_DROP_FLASH_MS` and
 * tints the matching cell red while the timer is active. -1 in the
 * tab/cell fields means "no flash pending." */
int   g_stkBadDropTab  = -1;   /* tab category 0..2 */
int   g_stkBadDropCell = -1;   /* cell idx 0..99 */
DWORD g_stkBadDropTick = 0;    /* GetTickCount() at rejection */
#define STK_BAD_DROP_FLASH_MS  300  /* visible duration */

void Stk_FlashBadDrop(int tabCategory, int cellIdx) {
    g_stkBadDropTab  = tabCategory;
    g_stkBadDropCell = cellIdx;
    g_stkBadDropTick = GetTickCount();
}

/* Pack a 3-char code as a space-padded 4-byte little-endian DWORD,
 * matching D2's DATATBLS_GetItemRecordFromItemCode hashing convention
 * ('hp1' -> 0x20313068, NOT null-padded). */
#define STK_CODE3(a,b,c) \
    ((DWORD)(BYTE)(a) | \
     ((DWORD)(BYTE)(b) << 8) | \
     ((DWORD)(BYTE)(c) << 16) | \
     ((DWORD)0x20 << 24))

/* Helper: place a single 1x1 layout entry at (col,row). */
static void StkSetCell(int tab, int col, int row,
                       DWORD code, const char* invFile, const char* name) {
    if (tab < 0 || tab >= STASH_NUM_STK_TABS) return;
    if (col < 0 || col >= STASH_GRID_COLS) return;
    if (row < 0 || row >= STASH_GRID_ROWS) return;
    int idx = row * STASH_GRID_COLS + col;
    g_stkLayout[tab][idx].dwCode       = code;
    g_stkLayout[tab][idx].invFile      = invFile;
    g_stkLayout[tab][idx].displayName  = name;
    g_stkLayout[tab][idx].pCachedCel   = NULL;
    g_stkLayout[tab][idx].cellsW       = 1;
    g_stkLayout[tab][idx].cellsH       = 1;
    g_stkLayout[tab][idx].isOriginCell = 1;
}

/* Helper: place a multi-cell layout entry. The top-left cell is the
 * "origin" (renders the cel + accepts deposits/clicks). The covered
 * sub-cells reference the same dwCode but isOriginCell=0 so the
 * render/click code knows to skip them. Width/height in CELLS. */
static void StkSetCellSized(int tab, int col, int row, int w, int h,
                            DWORD code, const char* invFile, const char* name) {
    if (tab < 0 || tab >= STASH_NUM_STK_TABS) return;
    if (col < 0 || col + w > STASH_GRID_COLS) return;
    if (row < 0 || row + h > STASH_GRID_ROWS) return;
    for (int dr = 0; dr < h; dr++) {
        for (int dc = 0; dc < w; dc++) {
            int idx = (row + dr) * STASH_GRID_COLS + (col + dc);
            g_stkLayout[tab][idx].dwCode       = code;
            g_stkLayout[tab][idx].invFile      = invFile;
            g_stkLayout[tab][idx].displayName  = name;
            g_stkLayout[tab][idx].pCachedCel   = NULL;
            g_stkLayout[tab][idx].cellsW       = (BYTE)w;
            g_stkLayout[tab][idx].cellsH       = (BYTE)h;
            g_stkLayout[tab][idx].isOriginCell = (dr == 0 && dc == 0) ? 1 : 0;
        }
    }
}

/* Author all three layouts. Called from StashInit at DLL load.
 * Cells not explicitly set remain dwCode == 0 (black/inactive). */
static void StkInitLayouts(void) {
    memset(g_stkLayout, 0, sizeof(g_stkLayout));

    /* ------------------------------------------------------------
     * Tab 0 — STK_TAB_CONSUMABLES — clean grouped layout
     *
     *      col  0    1    2    3    4    5    6    7    8    9
     *  R0  hp1  hp2  hp3  hp4  hp5   .    .    .    .    .
     *  R1  mp1  mp2  mp3  mp4  mp5   .    .    .    .    .
     *  R2  rvs  rvl  vps  yps  wms   .    .    .    .    .
     *  R3  tsc  isc   .   TBK  IBK   .    .    .    .    .   <- tomes 1x2
     *  R4   .    .    .   *T   *I    .    .    .    .    .
     *  R5  gps  gpm  gpl  ops  opm  opl   .    .    .    .   <- 6 throwing
     *  R6  dhn  mbr  bey   .  key    .  tes  ceh  bet  fed   <- organs+key+ess
     *  R7  AQV  CQV   .   PK1  PK2  PK3  HFT  *h    .    .   <- multi-cell
     *  R8  *aq  *cq   .   *p1  *p2  *p3  *h   *h   TOA *toa
     *  R9  *aq  *cq   .    .    .    .   *h   *h  *toa *toa
     *
     * AQV/CQV 1x3 (rows 7-9), PK1/2/3 1x2 (rows 7-8), HFT 2x3 (rows 7-9
     * cols 6-7), TOA 2x2 (rows 8-9 cols 8-9).
     * ------------------------------------------------------------ */
    int t = STK_TAB_CONSUMABLES;

    /* R0: HP potions */
    StkSetCell(t, 0, 0, STK_CODE3('h','p','1'), "invhp1", "Lesser Healing Potion");
    StkSetCell(t, 1, 0, STK_CODE3('h','p','2'), "invhp2", "Light Healing Potion");
    StkSetCell(t, 2, 0, STK_CODE3('h','p','3'), "invhp3", "Healing Potion");
    StkSetCell(t, 3, 0, STK_CODE3('h','p','4'), "invhp4", "Strong Healing Potion");
    StkSetCell(t, 4, 0, STK_CODE3('h','p','5'), "invhp5", "Greater Healing Potion");

    /* R1: MP potions */
    StkSetCell(t, 0, 1, STK_CODE3('m','p','1'), "invmp1", "Lesser Mana Potion");
    StkSetCell(t, 1, 1, STK_CODE3('m','p','2'), "invmp2", "Light Mana Potion");
    StkSetCell(t, 2, 1, STK_CODE3('m','p','3'), "invmp3", "Mana Potion");
    StkSetCell(t, 3, 1, STK_CODE3('m','p','4'), "invmp4", "Strong Mana Potion");
    StkSetCell(t, 4, 1, STK_CODE3('m','p','5'), "invmp5", "Greater Mana Potion");

    /* R2: misc potions (rejuv + utility) */
    StkSetCell(t, 0, 2, STK_CODE3('r','v','s'), "invvps", "Rejuvenation Potion");
    StkSetCell(t, 1, 2, STK_CODE3('r','v','l'), "invvpl", "Full Rejuvenation Potion");
    StkSetCell(t, 2, 2, STK_CODE3('v','p','s'), "invwps", "Stamina Potion");
    StkSetCell(t, 3, 2, STK_CODE3('y','p','s'), "invnps", "Antidote Potion");
    StkSetCell(t, 4, 2, STK_CODE3('w','m','s'), "invyps", "Thawing Potion");

    /* R3-R4: scrolls + tomes (tomes 1x2) */
    StkSetCell     (t, 0, 3, STK_CODE3('t','s','c'), "invbsc", "Town Portal Scroll");
    StkSetCell     (t, 1, 3, STK_CODE3('i','s','c'), "invrsc", "Identify Scroll");
    StkSetCellSized(t, 3, 3, 1, 2, STK_CODE3('t','b','k'), "invbbk", "Tome of Town Portal");
    StkSetCellSized(t, 4, 3, 1, 2, STK_CODE3('i','b','k'), "invrbk", "Tome of Identify");

    /* R5: throwing potions (3 gas + 3 oil) */
    StkSetCell(t, 0, 5, STK_CODE3('g','p','s'), "invgpl", "Rancid Gas Potion");
    StkSetCell(t, 1, 5, STK_CODE3('g','p','m'), "invgpm", "Choking Gas Potion");
    StkSetCell(t, 2, 5, STK_CODE3('g','p','l'), "invgps", "Strangling Gas Potion");
    StkSetCell(t, 3, 5, STK_CODE3('o','p','s'), "invopl", "Oil Potion");
    StkSetCell(t, 4, 5, STK_CODE3('o','p','m'), "invopm", "Exploding Potion");
    StkSetCell(t, 5, 5, STK_CODE3('o','p','l'), "invops", "Fulminating Potion");

    /* R6: organs + key + 4 essences */
    StkSetCell(t, 0, 6, STK_CODE3('d','h','n'), "invfang", "Diablo's Horn");
    StkSetCell(t, 1, 6, STK_CODE3('m','b','r'), "invbrnz", "Mephisto's Brain");
    StkSetCell(t, 2, 6, STK_CODE3('b','e','y'), "inveye",  "Baal's Eye");
    StkSetCell(t, 4, 6, STK_CODE3('k','e','y'), "invkey",  "Skeleton Key");
    StkSetCell(t, 6, 6, STK_CODE3('t','e','s'), "invhrt",  "Twisted Essence of Suffering");
    StkSetCell(t, 7, 6, STK_CODE3('c','e','h'), "invhrt",  "Charged Essence of Hatred");
    StkSetCell(t, 8, 6, STK_CODE3('b','e','t'), "invhrt",  "Burning Essence of Terror");
    StkSetCell(t, 9, 6, STK_CODE3('f','e','d'), "invhrt",  "Festering Essence of Destruction");

    /* R7-R9: large multi-cell items. Origin = top-left. */
    StkSetCellSized(t, 0, 7, 1, 3, STK_CODE3('a','q','v'), "invqvr", "Arrows");          /* 1x3 */
    StkSetCellSized(t, 1, 7, 1, 3, STK_CODE3('c','q','v'), "invcqv", "Bolts");           /* 1x3 */
    StkSetCellSized(t, 3, 7, 1, 2, STK_CODE3('p','k','1'), "invmph", "Key of Terror");   /* 1x2 */
    StkSetCellSized(t, 4, 7, 1, 2, STK_CODE3('p','k','2'), "invmph", "Key of Hate");     /* 1x2 */
    StkSetCellSized(t, 5, 7, 1, 2, STK_CODE3('p','k','3'), "invmph", "Key of Destruction"); /* 1x2 */
    StkSetCellSized(t, 6, 7, 2, 3, STK_CODE3('h','f','t'), "invhrt", "Hellfire Torch");  /* 2x3 */
    StkSetCellSized(t, 8, 8, 2, 2, STK_CODE3('t','o','a'), "invsbk", "Token of Absolution"); /* 2x2 */

    /* ------------------------------------------------------------
     * Tab 1 — STK_TAB_RUNES (33 runes, tier-grouped with row-spacing)
     * ------------------------------------------------------------ */
    t = STK_TAB_RUNES;

    /* Hardcoded names + invFiles per Misc.txt's invFile column.
     * Note 1.10f Misc.txt has invFile=invrJo for r31 (Jah) due to a
     * historical typo, and invrShae (without trailing l) for r13. */
    static const struct { DWORD code; const char* invFile; const char* name; } RUNES_DATA[33] = {
        { STK_CODE3('r','0','1'), "invrEl",   "El Rune" },
        { STK_CODE3('r','0','2'), "invrEld",  "Eld Rune" },
        { STK_CODE3('r','0','3'), "invrTir",  "Tir Rune" },
        { STK_CODE3('r','0','4'), "invrNef",  "Nef Rune" },
        { STK_CODE3('r','0','5'), "invrEth",  "Eth Rune" },
        { STK_CODE3('r','0','6'), "invrIth",  "Ith Rune" },
        { STK_CODE3('r','0','7'), "invrTal",  "Tal Rune" },
        { STK_CODE3('r','0','8'), "invrRal",  "Ral Rune" },
        { STK_CODE3('r','0','9'), "invrOrt",  "Ort Rune" },
        { STK_CODE3('r','1','0'), "invrThul", "Thul Rune" },
        { STK_CODE3('r','1','1'), "invrAmn",  "Amn Rune" },
        { STK_CODE3('r','1','2'), "invrSol",  "Sol Rune" },
        { STK_CODE3('r','1','3'), "invrShae", "Shael Rune" },
        { STK_CODE3('r','1','4'), "invrDol",  "Dol Rune" },
        { STK_CODE3('r','1','5'), "invrHel",  "Hel Rune" },
        { STK_CODE3('r','1','6'), "invrIo",   "Io Rune" },
        { STK_CODE3('r','1','7'), "invrLum",  "Lum Rune" },
        { STK_CODE3('r','1','8'), "invrKo",   "Ko Rune" },
        { STK_CODE3('r','1','9'), "invrFal",  "Fal Rune" },
        { STK_CODE3('r','2','0'), "invrLem",  "Lem Rune" },
        { STK_CODE3('r','2','1'), "invrPul",  "Pul Rune" },
        { STK_CODE3('r','2','2'), "invrUm",   "Um Rune" },
        { STK_CODE3('r','2','3'), "invrMal",  "Mal Rune" },
        { STK_CODE3('r','2','4'), "invrIst",  "Ist Rune" },
        { STK_CODE3('r','2','5'), "invrGul",  "Gul Rune" },
        { STK_CODE3('r','2','6'), "invrVex",  "Vex Rune" },
        { STK_CODE3('r','2','7'), "invrOhm",  "Ohm Rune" },
        { STK_CODE3('r','2','8'), "invrLo",   "Lo Rune" },
        { STK_CODE3('r','2','9'), "invrSur",  "Sur Rune" },
        { STK_CODE3('r','3','0'), "invrBer",  "Ber Rune" },
        { STK_CODE3('r','3','1'), "invrJo",   "Jah Rune" },
        { STK_CODE3('r','3','2'), "invrCham", "Cham Rune" },
        { STK_CODE3('r','3','3'), "invrZod",  "Zod Rune" },
    };
    /* Row 1: r01-r10 (Low Runes) */
    for (int i = 0; i < 10; i++) {
        StkSetCell(t, i, 1, RUNES_DATA[i].code, RUNES_DATA[i].invFile, RUNES_DATA[i].name);
    }
    /* Row 3: r11-r20 (Mid Runes) */
    for (int i = 0; i < 10; i++) {
        StkSetCell(t, i, 3, RUNES_DATA[10+i].code, RUNES_DATA[10+i].invFile, RUNES_DATA[10+i].name);
    }
    /* Row 5: r21-r30 (High Runes) */
    for (int i = 0; i < 10; i++) {
        StkSetCell(t, i, 5, RUNES_DATA[20+i].code, RUNES_DATA[20+i].invFile, RUNES_DATA[20+i].name);
    }
    /* Row 7: r31-r33 (Top Runes), centered at cols 4-6 */
    for (int i = 0; i < 3; i++) {
        StkSetCell(t, 4+i, 7, RUNES_DATA[30+i].code, RUNES_DATA[30+i].invFile, RUNES_DATA[30+i].name);
    }

    /* ------------------------------------------------------------
     * Tab 2 — STK_TAB_GEMS (35 gems = 7 colors × 5 grades)
     * Rows 1-7, columns 0-4 (chipped, flawed, normal, flawless, perfect)
     * ------------------------------------------------------------ */
    t = STK_TAB_GEMS;

    /* Color base codes (third char) per Misc.txt — note Amethyst's
     * flawless is 'gzv' (z) but other gems use 'gl?' (l), and Skull
     * uses an entirely different prefix 'sk*'. */
    static const struct {
        const char* codes[5];     /* chipped, flawed, normal, flawless, perfect */
        const char* invFiles[5];
        const char* colorName;
    } GEMS_DATA[7] = {
        { {"gcv","gfv","gsv","gzv","gpv"}, {"invgsva","invgsvb","invgsvc","invgsvd","invgsve"}, "Amethyst" },
        { {"gcy","gfy","gsy","gly","gpy"}, {"invgsya","invgsyb","invgsyc","invgsyd","invgsye"}, "Topaz"    },
        { {"gcb","gfb","gsb","glb","gpb"}, {"invgsba","invgsbb","invgsbc","invgsbd","invgsbe"}, "Sapphire" },
        { {"gcg","gfg","gsg","glg","gpg"}, {"invgsga","invgsgb","invgsgc","invgsgd","invgsge"}, "Emerald"  },
        { {"gcr","gfr","gsr","glr","gpr"}, {"invgsra","invgsrb","invgsrc","invgsrd","invgsre"}, "Ruby"     },
        { {"gcw","gfw","gsw","glw","gpw"}, {"invgswa","invgswb","invgswc","invgswd","invgswe"}, "Diamond"  },
        { {"skc","skf","sku","skl","skz"}, {"invskc", "invskf", "invsku", "invskl", "invskz" }, "Skull"    },
    };
    static const char* GRADE_NAMES[5] = { "Chipped", "Flawed", "", "Flawless", "Perfect" };

    for (int color = 0; color < 7; color++) {
        for (int grade = 0; grade < 5; grade++) {
            const char* code3 = GEMS_DATA[color].codes[grade];
            DWORD code = STK_CODE3(code3[0], code3[1], code3[2]);
            /* Build display name "Chipped Amethyst" / "Amethyst" / "Perfect Amethyst" */
            static char nameBuf[7][5][32];
            if (GRADE_NAMES[grade][0])
                _snprintf(nameBuf[color][grade], 31, "%s %s",
                          GRADE_NAMES[grade], GEMS_DATA[color].colorName);
            else
                _snprintf(nameBuf[color][grade], 31, "%s", GEMS_DATA[color].colorName);
            nameBuf[color][grade][31] = 0;
            /* Row = color+1 (rows 1..7); Col = grade (0..4) */
            StkSetCell(t, grade, color + 1, code, GEMS_DATA[color].invFiles[grade],
                       nameBuf[color][grade]);
        }
    }

    Log("STK: 3 tab layouts authored — Consumables (39 cells), "
        "Runes (33 cells), Gems (35 cells)\n");
}

/* Reset all STK in-memory state. Called from StashInit and on character
 * change so a stale per-character STK_AP doesn't leak. */
static void StkResetAll(void) {
    memset(g_stashStkAp, 0, sizeof(g_stashStkAp));
    memset(g_stashStkSh, 0, sizeof(g_stashStkSh));
    /* g_stkLayout is data, not state — preserved across resets. */
}

/* Public: clear per-character in-memory state + invalidate the cel
 * pointer cache. Called from WndProc EXIT (player-gone) so:
 *  1. The next character's load doesn't see the previous character's
 *     STK_AP stacks bleed through before StkLoadAP runs.
 *  2. The cel cache is wiped — without this, fnCelLoad pointers from
 *     the previous session can become stale across character changes
 *     (D2's CelMgr context is per-character in some configurations)
 *     and the next render uses garbage data, corrupting icons. */
void StkResetOnPlayerGone(void) {
    /* Clear per-character STK_AP (will be re-loaded on next char). */
    memset(g_stashStkAp, 0, sizeof(g_stashStkAp));
    /* SH stays — it's account-wide. But we still invalidate cels.
     * Note: SH in-memory is just a copy of disk; if logout wrote a
     * different state, OnCharacterLoad's StkLoadShared re-reads it. */

    /* Wipe cel cache for ALL 3 layouts × 100 cells. */
    int t, i;
    for (t = 0; t < STASH_NUM_STK_TABS; t++) {
        for (i = 0; i < STASH_SLOTS_PER_TAB; i++) {
            g_stkLayout[t][i].pCachedCel = NULL;
        }
    }
    Log("STK: in-memory state reset (player-gone) — STK_AP cleared, cel cache wiped\n");
}

/* Find the cell index in a category tab whose layout dwCode matches
 * the given code. Used by deposit auto-route. Returns -1 on miss. */
int StkFindCellForCode(int tabCategoryIdx, DWORD dwCode) {
    if (tabCategoryIdx < 0 || tabCategoryIdx >= STASH_NUM_STK_TABS) return -1;
    if (dwCode == 0) return -1;
    for (int i = 0; i < STASH_SLOTS_PER_TAB; i++) {
        if (g_stkLayout[tabCategoryIdx][i].dwCode == dwCode) return i;
    }
    return -1;
}

/* Deposit one item into the cell. If the cell already holds a stack of
 * the same code, increment count. If empty, store the template bytes
 * and start at count=1. Returns FALSE if the deposit is rejected
 * (mismatched code, black cell, or stack at max). */
BOOL StkDeposit(BOOL useApScope, int tabCategoryIdx, int cellIdx,
                DWORD dwCode, const BYTE* templateBytes, WORD templateLen) {
    if (tabCategoryIdx < 0 || tabCategoryIdx >= STASH_NUM_STK_TABS) return FALSE;
    if (cellIdx < 0 || cellIdx >= STASH_SLOTS_PER_TAB) return FALSE;
    if (templateLen > STK_TEMPLATE_BYTES) templateLen = STK_TEMPLATE_BYTES;

    StkLayoutEntry* layout = &g_stkLayout[tabCategoryIdx][cellIdx];
    if (layout->dwCode == 0) {
        Stk_FlashBadDrop(tabCategoryIdx, cellIdx);  /* 1.9.0 Phase 4 */
        return FALSE;  /* black cell */
    }
    if (layout->dwCode != dwCode) {
        Stk_FlashBadDrop(tabCategoryIdx, cellIdx);  /* 1.9.0 Phase 4 */
        return FALSE;  /* wrong type */
    }

    StkTab* tab = useApScope ? &g_stashStkAp[tabCategoryIdx] : &g_stashStkSh[tabCategoryIdx];
    StkSlot* slot = &tab->slots[cellIdx];

    if (slot->count == 0) {
        slot->dwCode = dwCode;
        if (templateBytes && templateLen > 0)
            memcpy(slot->tplBytes, templateBytes, templateLen);
        slot->templateBytes = templateLen;
        slot->count = 1;
    } else if (slot->count < STK_MAX_STACK_COUNT) {
        slot->count++;
    } else {
        Stk_FlashBadDrop(tabCategoryIdx, cellIdx);  /* 1.9.0 Phase 4 */
        return FALSE;  /* stack at 999 max */
    }
    tab->lastModifiedTick = GetTickCount();
    return TRUE;
}

/* Pickup ONE item from a cell. Decrements count by 1 and emits the
 * template bytes via outTemplate. Returns FALSE if the cell is empty.
 * On count reaching 0 the slot is cleared. */
BOOL StkPickupOne(BOOL useApScope, int tabCategoryIdx, int cellIdx,
                  BYTE* outTemplate, WORD* outLen) {
    if (tabCategoryIdx < 0 || tabCategoryIdx >= STASH_NUM_STK_TABS) return FALSE;
    if (cellIdx < 0 || cellIdx >= STASH_SLOTS_PER_TAB) return FALSE;
    if (!outTemplate || !outLen) return FALSE;

    StkTab* tab = useApScope ? &g_stashStkAp[tabCategoryIdx] : &g_stashStkSh[tabCategoryIdx];
    StkSlot* slot = &tab->slots[cellIdx];
    if (slot->count == 0) return FALSE;

    /* Copy template bytes out so the caller can spawn a fresh item. */
    *outLen = slot->templateBytes;
    if (slot->templateBytes > 0)
        memcpy(outTemplate, slot->tplBytes, slot->templateBytes);

    slot->count--;
    if (slot->count == 0) {
        slot->dwCode = 0;
        slot->templateBytes = 0;
        memset(slot->tplBytes, 0, sizeof(slot->tplBytes));
    }
    tab->lastModifiedTick = GetTickCount();
    return TRUE;
}

/* Sidecar I/O — Phase F implementations. Stubs return TRUE so the
 * load/save call sites can already be wired in. The actual file-format
 * reads/writes will land in Phase F. */
BOOL StkLoadShared(void) {
    char path[MAX_PATH], dir[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(path, "%sshared_stash_stk.dat", dir);
    FILE* f = fopen(path, "rb");
    if (!f) {
        memset(g_stashStkSh, 0, sizeof(g_stashStkSh));
        return TRUE;  /* no file = empty defaults */
    }
    DWORD magic = 0, version = 0, numTabs = 0;
    fread(&magic,   sizeof(magic),   1, f);
    fread(&version, sizeof(version), 1, f);
    fread(&numTabs, sizeof(numTabs), 1, f);
    if (magic != 0x534B5453u /* "STKS" */ ||
        version != 1 ||
        numTabs != STASH_NUM_STK_TABS) {
        fclose(f);
        memset(g_stashStkSh, 0, sizeof(g_stashStkSh));
        Log("STK: shared_stash_stk.dat header mismatch — using defaults\n");
        return TRUE;
    }
    fread(g_stashStkSh, sizeof(g_stashStkSh), 1, f);
    fclose(f);
    Log("STK: shared loaded from %s\n", path);
    return TRUE;
}

BOOL StkSaveShared(void) {
    char path[MAX_PATH], dir[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(path, "%sshared_stash_stk.dat", dir);
    FILE* f = fopen(path, "wb");
    if (!f) {
        Log("STK: shared save FAILED to open %s (err=%lu)\n", path, GetLastError());
        return FALSE;
    }
    DWORD magic = 0x534B5453u /* "STKS" */, version = 1, numTabs = STASH_NUM_STK_TABS;
    fwrite(&magic,   sizeof(magic),   1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&numTabs, sizeof(numTabs), 1, f);
    fwrite(g_stashStkSh, sizeof(g_stashStkSh), 1, f);
    fclose(f);
    return TRUE;
}

BOOL StkLoadAP(const char* charName) {
    if (!charName || !charName[0]) return FALSE;
    char path[MAX_PATH], dir[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sap_stash_stk_%s.dat", dir, charName);
    FILE* f = fopen(path, "rb");
    if (!f) {
        memset(g_stashStkAp, 0, sizeof(g_stashStkAp));
        return TRUE;
    }
    DWORD magic = 0, version = 0, numTabs = 0;
    fread(&magic,   sizeof(magic),   1, f);
    fread(&version, sizeof(version), 1, f);
    fread(&numTabs, sizeof(numTabs), 1, f);
    if (magic != 0x414B5453u /* "STKA" */ ||
        version != 1 ||
        numTabs != STASH_NUM_STK_TABS) {
        fclose(f);
        memset(g_stashStkAp, 0, sizeof(g_stashStkAp));
        Log("STK: ap_stash_stk_%s.dat header mismatch — using defaults\n", charName);
        return TRUE;
    }
    fread(g_stashStkAp, sizeof(g_stashStkAp), 1, f);
    fclose(f);
    Log("STK: AP-stk loaded for '%s' from %s\n", charName, path);
    return TRUE;
}

BOOL StkSaveAP(const char* charName) {
    if (!charName || !charName[0]) return FALSE;
    char path[MAX_PATH], dir[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sap_stash_stk_%s.dat", dir, charName);
    FILE* f = fopen(path, "wb");
    if (!f) {
        Log("STK: AP-stk save FAILED to open %s (err=%lu)\n", path, GetLastError());
        return FALSE;
    }
    DWORD magic = 0x414B5453u /* "STKA" */, version = 1, numTabs = STASH_NUM_STK_TABS;
    fwrite(&magic,   sizeof(magic),   1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&numTabs, sizeof(numTabs), 1, f);
    fwrite(g_stashStkAp, sizeof(g_stashStkAp), 1, f);
    fclose(f);
    return TRUE;
}
