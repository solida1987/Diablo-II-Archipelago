/* Multi-tab Stash (foundation layer) Data structures, globals, sidecar I/O scaffolding and access control for the 10 AP + 10 shared tab stash system. */

#include "d2arch_stash.h"
#include <stdio.h>       /* fopen, fwrite, fread, fclose */
#include <stdlib.h>      /* malloc/free (unused today, reserved) */
#include <string.h>      /* memset, memcpy, strlen */
#include <wchar.h>       /* _snwprintf */

/* GLOBAL STATE (definitions) */
StashTab g_stashShared[STASH_NUM_SHARED_TABS];
StashTab g_stashAP[STASH_NUM_AP_TABS];
DWORD    g_stashSharedGold = 0;
DWORD    g_stashAPGold     = 0;

int      g_activeStashTab  = 0;
BOOL     g_stashOpen       = FALSE;

/* INTERNAL HELPERS */

/* Assign default tab names "Tab 1" .. */
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

/* Simple XOR + 1-bit left-rotate checksum over a byte buffer. */
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

/* Build "<archDir>/<fileName>" into out. */
static int EnsureArchDir(char* out, size_t outSize) {
    if (!out || outSize == 0) return 0;
    /* GetArchDir already creates the directory if it does not exist and terminates with a trailing backslash. */
    GetArchDir(out, (int)outSize);
    return (out[0] != 0) ? 1 : 0;
}

/* Join <archDir>/<leaf> into outPath. */
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

/* Join <saveDir>/<leaf> into outPath. */
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

/* Build "ap_stash_<charName>.dat" into outLeaf. */
static int BuildAPStashLeaf(char* outLeaf, size_t outSize, const char* charName) {
    int written;
    if (!outLeaf || outSize == 0 || !charName || !charName[0]) return 0;
    written = _snprintf(outLeaf, outSize, "ap_stash_%s.dat", charName);
    if (written < 0 || (size_t)written >= outSize) return 0;
    outLeaf[outSize - 1] = 0;
    return 1;
}

/* LIFECYCLE */
/* Forward decls so StashInit can call STK init helpers defined later in this file. */
static void StkInitLayouts(void);
static void StkResetAll(void);

void StashInit(void) {
    StashInitPool(g_stashShared, STASH_NUM_SHARED_TABS);
    StashInitPool(g_stashAP,     STASH_NUM_AP_TABS);
    g_stashSharedGold = 0;
    g_stashAPGold     = 0;
    g_activeStashTab  = 0;
    g_stashOpen       = FALSE;
    /* initialize the STK system. */
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

/* SIDECAR FILE FORMAT Header (20 bytes): DWORD magic = STASH_FILE_MAGIC DWORD version = STASH_FILE_VERSION DWORD gold = pool gold DWORD reserved0 = 0 (future: flags) DWORD numTabs = STASH_NUM_*_TABS (sanity vs version bump) Body: StashTab[numTabs] (raw struct dump) Trailer: DWORD checksum = XOR-rotate of every byte above */
typedef struct {
    DWORD magic;
    DWORD version;
    DWORD gold;
    DWORD reserved0;
    DWORD numTabs;
} StashFileHeader;

/* Shared by both load paths. */
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
        /* File doesn't exist -> caller should keep defaults. */
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
    char tmpPath[MAX_PATH];

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

    /* atomic write — .tmp then MoveFileExA over the real file, keeping a .bak of the previous version. */
    _snprintf(tmpPath, sizeof(tmpPath) - 1, "%s.tmp", path);
    tmpPath[sizeof(tmpPath) - 1] = 0;

    f = fopen(tmpPath, "wb");
    if (!f) {
        Log("Stash: WriteStashFile open failed err=%lu path='%s'\n",
            GetLastError(), tmpPath);
        free(buf);
        return 0;
    }

    wrote = fwrite(buf, 1, bufLen, f);
    if (wrote != bufLen) {
        Log("Stash: short write (got %zu want %zu) path='%s'\n",
            wrote, bufLen, tmpPath);
        fclose(f);
        DeleteFileA(tmpPath);
        free(buf);
        return 0;
    }
    if (fwrite(&checksum, 1, sizeof(checksum), f) != sizeof(checksum)) {
        Log("Stash: checksum write failed path='%s'\n", tmpPath);
        fclose(f);
        DeleteFileA(tmpPath);
        free(buf);
        return 0;
    }

    fclose(f);
    free(buf);

    {
        char bakPath[MAX_PATH];
        _snprintf(bakPath, sizeof(bakPath) - 1, "%s.bak", path);
        bakPath[sizeof(bakPath) - 1] = 0;
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
            CopyFileA(path, bakPath, FALSE);
    }
    if (!MoveFileExA(tmpPath, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Log("Stash: WriteStashFile rename failed err=%lu — leaving tmp at '%s'\n",
            GetLastError(), tmpPath);
        return 0;
    }
    Log("Stash: wrote %zu bytes (+cksum) path='%s'\n", bufLen, path);
    return 1;
}

/* 2.x CHEST KEYING (Phase 2 — isolation toggle) The one chest's data file is chosen by the StashIsolated toggle the launcher writes to d2arch.ini [settings] (default ISOLATED): isolated -> stash_seed_<key>.dat (only this AP seed's characters share it) global -> stash_global.dat (every "global" character shares it) <key> is the launcher-provided per-seed key ([settings] SeedKey, the same fnv8(seedName) the per-seed item index uses); standalone falls back to the ShuffleSeed, and if neither is present we degrade to the global file so the chest is never keyless. */
static void StashChestLeaf(char* outLeaf, size_t outSize, BOOL stk) {
    char ini[MAX_PATH], iso[8], key[64];
    BuildArchPath(ini, sizeof(ini), "d2arch.ini");
    GetPrivateProfileStringA("settings", "StashIsolated", "1", iso, sizeof(iso), ini);
    /* shared resolver adds the [ap] SlotName fallback so isolation actually works in multi-AP (where the launcher may not write SeedKey). */
    ResolveSeedKey(ini, key, sizeof(key));
    if (atoi(iso) != 0 && key[0]) {
        _snprintf(outLeaf, outSize, stk ? "stash_seed_%s_stk.dat" : "stash_seed_%s.dat", key);
    } else {
        _snprintf(outLeaf, outSize, stk ? "stash_global_stk.dat" : "stash_global.dat");
    }
    outLeaf[outSize - 1] = 0;
}

/* SHARED STASH LOAD/SAVE */
BOOL StashLoadShared(void) {
    char path[MAX_PATH], leaf[MAX_PATH];
    int rc;

    StashChestLeaf(leaf, sizeof(leaf), FALSE);
    if (!BuildArchPath(path, sizeof(path), leaf)) {
        Log("Stash: BuildArchPath failed for %s\n", leaf);
        return FALSE;
    }
    /* Lossless migration: keyed file absent -> read the legacy shared_stash.dat so existing items appear; the next save writes the keyed file. */
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        char legacy[MAX_PATH];
        if (BuildArchPath(legacy, sizeof(legacy), "shared_stash.dat") &&
            GetFileAttributesA(legacy) != INVALID_FILE_ATTRIBUTES) {
            Log("Stash: chest '%s' absent — migrating from legacy shared_stash.dat\n", leaf);
            strcpy(path, legacy);
        }
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
        /* Missing file or corrupt -> defaults. */
        StashInitPool(g_stashShared, STASH_NUM_SHARED_TABS);
        g_stashSharedGold = 0;
        Log("Stash: shared using defaults\n");
        return TRUE;
    }
    /* rc < 0 -> fatal I/O */
    return FALSE;
}

BOOL StashSaveShared(void) {
    char path[MAX_PATH], leaf[MAX_PATH];
    StashChestLeaf(leaf, sizeof(leaf), FALSE);
    if (!BuildArchPath(path, sizeof(path), leaf)) {
        Log("Stash: BuildArchPath failed for %s (save)\n", leaf);
        return FALSE;
    }
    return WriteStashFile(path,
                          g_stashSharedGold,
                          g_stashShared,
                          STASH_NUM_SHARED_TABS) ? TRUE : FALSE;
}

/* AP STASH LOAD/SAVE (per character) */
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

/* ACCESS CONTROL tabGlobal 0..9 -> AP tab index tabGlobal 10..19 -> shared tab index (subtract 10) tabGlobal 20..22 -> STK_AP tab (Consumables/Runes/Gems) — 1.9.0 tabGlobal 23..25 -> STK_SH tab (Consumables/Runes/Gems) — 1.9.0 Matrix: non-AP char: AP = NONE shared = READWRITE STK_AP = NONE STK_SH = RW AP pre-goal: AP = READWRITE shared = NONE STK_AP = RW STK_SH = NONE AP post-goal: AP = READWRITE shared = READWRITE STK_AP = RW STK_SH = RW The STK rules mirror the existing AP/SH ones exactly, per user spec: "Standalone skal kun vises, når man er på standalone, og AP skal kun vises, når man er på AP". */
StashAccess StashGetAccess(int tabGlobalIndex) {
    /* 2.x ONE-CHEST REDESIGN — the old AP-vs-Shared access matrix (which HID/LOCKED one pool depending on AP-mode + goal) is gone; that hide/lock swap was the root of the "stash shows whichever set it feels like / swaps to Shared on save&quit" confusion. */
    if (tabGlobalIndex < 0 || tabGlobalIndex >= STASH_MAX_TABS)
        return STASH_ACCESS_NONE;
    /* Hide the retired AP pool (0..9) and AP-STK pool (20..22). */
    if (tabGlobalIndex < STASH_NUM_AP_TABS) return STASH_ACCESS_NONE;
    if (tabGlobalIndex >= STASH_STK_AP_BASE &&
        tabGlobalIndex <  STASH_STK_AP_BASE + STASH_NUM_STK_TABS)
        return STASH_ACCESS_NONE;
    /* The Shared pool (10..19) + SH-STK (23..25) = the one chest. */
    return STASH_ACCESS_READWRITE;
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

/* ITEM CLASSIFICATION (stackable whitelist) TODO: needs real D2 item-type classification. */
BOOL StashIsStackableType(DWORD itemClassId) {
    (void)itemClassId;   /* suppress unused-parameter warning */
    /* TODO: needs real D2 item-type classification — currently all return FALSE */
    return FALSE;
}

/* STK (Stackable) Tab System Three category tabs (Consumables / Runes / Gems) × two scopes (per-character AP and account-wide shared) = 6 underlying tabs. */

StkLayoutEntry g_stkLayout[STASH_NUM_STK_TABS][STASH_SLOTS_PER_TAB];
StkTab         g_stashStkAp[STASH_NUM_STK_TABS];
StkTab         g_stashStkSh[STASH_NUM_STK_TABS];

/* 1.9.0 Phase 4 — bad-drop flash state. */
int   g_stkBadDropTab  = -1;   /* tab category 0..2 */
int   g_stkBadDropCell = -1;   /* cell idx 0..99 */
DWORD g_stkBadDropTick = 0;    /* GetTickCount() at rejection */
#define STK_BAD_DROP_FLASH_MS  300  /* visible duration */

void Stk_FlashBadDrop(int tabCategory, int cellIdx) {
    g_stkBadDropTab  = tabCategory;
    g_stkBadDropCell = cellIdx;
    g_stkBadDropTick = GetTickCount();
}

/* Pack a 3-char code as a space-padded 4-byte little-endian DWORD, matching D2's DATATBLS_GetItemRecordFromItemCode hashing convention ('hp1' -> 0x20313068, NOT null-padded). */
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

/* Helper: place a multi-cell layout entry. */
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

/* Author all three layouts. */
static void StkInitLayouts(void) {
    memset(g_stkLayout, 0, sizeof(g_stkLayout));

    /* Tab 0 — STK_TAB_CONSUMABLES — clean grouped layout col 0 1 2 3 4 5 6 7 8 9 R0 hp1 hp2 hp3 hp4 hp5 . */
    int t = STK_TAB_CONSUMABLES;

    /* HP potions */
    StkSetCell(t, 0, 0, STK_CODE3('h','p','1'), "invhp1", "Lesser Healing Potion");
    StkSetCell(t, 1, 0, STK_CODE3('h','p','2'), "invhp2", "Light Healing Potion");
    StkSetCell(t, 2, 0, STK_CODE3('h','p','3'), "invhp3", "Healing Potion");
    StkSetCell(t, 3, 0, STK_CODE3('h','p','4'), "invhp4", "Strong Healing Potion");
    StkSetCell(t, 4, 0, STK_CODE3('h','p','5'), "invhp5", "Greater Healing Potion");

    /* MP potions */
    StkSetCell(t, 0, 1, STK_CODE3('m','p','1'), "invmp1", "Lesser Mana Potion");
    StkSetCell(t, 1, 1, STK_CODE3('m','p','2'), "invmp2", "Light Mana Potion");
    StkSetCell(t, 2, 1, STK_CODE3('m','p','3'), "invmp3", "Mana Potion");
    StkSetCell(t, 3, 1, STK_CODE3('m','p','4'), "invmp4", "Strong Mana Potion");
    StkSetCell(t, 4, 1, STK_CODE3('m','p','5'), "invmp5", "Greater Mana Potion");

    /* misc potions (rejuv + utility) */
    StkSetCell(t, 0, 2, STK_CODE3('r','v','s'), "invvps", "Rejuvenation Potion");
    StkSetCell(t, 1, 2, STK_CODE3('r','v','l'), "invvpl", "Full Rejuvenation Potion");
    StkSetCell(t, 2, 2, STK_CODE3('v','p','s'), "invwps", "Stamina Potion");
    StkSetCell(t, 3, 2, STK_CODE3('y','p','s'), "invnps", "Antidote Potion");
    StkSetCell(t, 4, 2, STK_CODE3('w','m','s'), "invyps", "Thawing Potion");

    /* scrolls + tomes (tomes 1x2) */
    StkSetCell     (t, 0, 3, STK_CODE3('t','s','c'), "invbsc", "Town Portal Scroll");
    StkSetCell     (t, 1, 3, STK_CODE3('i','s','c'), "invrsc", "Identify Scroll");
    StkSetCellSized(t, 3, 3, 1, 2, STK_CODE3('t','b','k'), "invbbk", "Tome of Town Portal");
    StkSetCellSized(t, 4, 3, 1, 2, STK_CODE3('i','b','k'), "invrbk", "Tome of Identify");

    /* throwing potions (3 gas + 3 oil) */
    StkSetCell(t, 0, 5, STK_CODE3('g','p','s'), "invgpl", "Rancid Gas Potion");
    StkSetCell(t, 1, 5, STK_CODE3('g','p','m'), "invgpm", "Choking Gas Potion");
    StkSetCell(t, 2, 5, STK_CODE3('g','p','l'), "invgps", "Strangling Gas Potion");
    StkSetCell(t, 3, 5, STK_CODE3('o','p','s'), "invopl", "Oil Potion");
    StkSetCell(t, 4, 5, STK_CODE3('o','p','m'), "invopm", "Exploding Potion");
    StkSetCell(t, 5, 5, STK_CODE3('o','p','l'), "invops", "Fulminating Potion");

    /* organs + key + 4 essences */
    StkSetCell(t, 0, 6, STK_CODE3('d','h','n'), "invfang", "Diablo's Horn");
    StkSetCell(t, 1, 6, STK_CODE3('m','b','r'), "invbrnz", "Mephisto's Brain");
    StkSetCell(t, 2, 6, STK_CODE3('b','e','y'), "inveye",  "Baal's Eye");
    StkSetCell(t, 4, 6, STK_CODE3('k','e','y'), "invkey",  "Skeleton Key");
    StkSetCell(t, 6, 6, STK_CODE3('t','e','s'), "invhrt",  "Twisted Essence of Suffering");
    StkSetCell(t, 7, 6, STK_CODE3('c','e','h'), "invhrt",  "Charged Essence of Hatred");
    StkSetCell(t, 8, 6, STK_CODE3('b','e','t'), "invhrt",  "Burning Essence of Terror");
    StkSetCell(t, 9, 6, STK_CODE3('f','e','d'), "invhrt",  "Festering Essence of Destruction");

    /* large multi-cell items. */
    StkSetCellSized(t, 0, 7, 1, 3, STK_CODE3('a','q','v'), "invqvr", "Arrows");          /* 1x3 */
    StkSetCellSized(t, 1, 7, 1, 3, STK_CODE3('c','q','v'), "invcqv", "Bolts");           /* 1x3 */
    StkSetCellSized(t, 3, 7, 1, 2, STK_CODE3('p','k','1'), "invmph", "Key of Terror");   /* 1x2 */
    StkSetCellSized(t, 4, 7, 1, 2, STK_CODE3('p','k','2'), "invmph", "Key of Hate");     /* 1x2 */
    StkSetCellSized(t, 5, 7, 1, 2, STK_CODE3('p','k','3'), "invmph", "Key of Destruction"); /* 1x2 */
    StkSetCellSized(t, 6, 7, 2, 3, STK_CODE3('h','f','t'), "invhrt", "Hellfire Torch");  /* 2x3 */
    StkSetCellSized(t, 8, 8, 2, 2, STK_CODE3('t','o','a'), "invsbk", "Token of Absolution"); /* 2x2 */

    /* Tab 1 — STK_TAB_RUNES (33 runes, tier-grouped with row-spacing) */
    t = STK_TAB_RUNES;

    /* Hardcoded names + invFiles per Misc.txt's invFile column. */
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

    /* Tab 2 — STK_TAB_GEMS (35 gems = 7 colors × 5 grades) Rows 1-7, columns 0-4 (chipped, flawed, normal, flawless, perfect) */
    t = STK_TAB_GEMS;

    /* Color base codes (third char) per Misc.txt — note Amethyst's flawless is 'gzv' (z) but other gems use 'gl?' (l), and Skull uses an entirely different prefix 'sk*'. */
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

/* Reset all STK in-memory state. */
static void StkResetAll(void) {
    memset(g_stashStkAp, 0, sizeof(g_stashStkAp));
    memset(g_stashStkSh, 0, sizeof(g_stashStkSh));
    /* g_stkLayout is data, not state — preserved across resets. */
}

/* Public: clear per-character in-memory state + invalidate the cel pointer cache. */
void StkResetOnPlayerGone(void) {
    /* Clear per-character STK_AP (will be re-loaded on next char). */
    memset(g_stashStkAp, 0, sizeof(g_stashStkAp));
    /* SH stays — it's account-wide. */

    /* 2.x (roadmap #7) — DO NOT wipe the cel cache here. */
    Log("STK: in-memory state reset (player-gone) — STK_AP cleared (cels kept, load-once)\n");
}

/* Find the cell index in a category tab whose layout dwCode matches the given code. */
int StkFindCellForCode(int tabCategoryIdx, DWORD dwCode) {
    if (tabCategoryIdx < 0 || tabCategoryIdx >= STASH_NUM_STK_TABS) return -1;
    if (dwCode == 0) return -1;
    for (int i = 0; i < STASH_SLOTS_PER_TAB; i++) {
        if (g_stkLayout[tabCategoryIdx][i].dwCode == dwCode) return i;
    }
    return -1;
}

/* Deposit `qty` units of an item into the cell in ONE shot. */
BOOL StkDeposit(BOOL useApScope, int tabCategoryIdx, int cellIdx,
                DWORD dwCode, const BYTE* templateBytes, WORD templateLen,
                int qty) {
    if (tabCategoryIdx < 0 || tabCategoryIdx >= STASH_NUM_STK_TABS) return FALSE;
    if (cellIdx < 0 || cellIdx >= STASH_SLOTS_PER_TAB) return FALSE;
    if (templateLen > STK_TEMPLATE_BYTES) templateLen = STK_TEMPLATE_BYTES;
    if (qty < 1) qty = 1;

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

    if (slot->count + (DWORD)qty > STK_MAX_STACK_COUNT) {
        Stk_FlashBadDrop(tabCategoryIdx, cellIdx);  /* 1.9.0 Phase 4 */
        return FALSE;  /* would overflow the 999 cap — reject whole deposit */
    }

    if (slot->count == 0) {
        slot->dwCode = dwCode;
        if (templateBytes && templateLen > 0)
            memcpy(slot->tplBytes, templateBytes, templateLen);
        slot->templateBytes = templateLen;
    }
    slot->count += (DWORD)qty;

    tab->lastModifiedTick = GetTickCount();
    return TRUE;
}

/* Pickup ONE item from a cell. */
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

/* Sidecar I/O — Phase F implementations. */
BOOL StkLoadShared(void) {
    char path[MAX_PATH], dir[MAX_PATH], leaf[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    StashChestLeaf(leaf, sizeof(leaf), TRUE);
    sprintf(path, "%s%s", dir, leaf);
    /* Lossless migration: keyed STK file absent -> read legacy shared_stash_stk.dat. */
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        char legacy[MAX_PATH];
        sprintf(legacy, "%sshared_stash_stk.dat", dir);
        if (GetFileAttributesA(legacy) != INVALID_FILE_ATTRIBUTES) strcpy(path, legacy);
    }
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
    char path[MAX_PATH], dir[MAX_PATH], leaf[MAX_PATH], tmpPath[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    StashChestLeaf(leaf, sizeof(leaf), TRUE);
    sprintf(path, "%s%s", dir, leaf);
    /* atomic .tmp+rename with .bak so a crash can't truncate the STK file. */
    _snprintf(tmpPath, sizeof(tmpPath) - 1, "%s.tmp", path);
    tmpPath[sizeof(tmpPath) - 1] = 0;
    FILE* f = fopen(tmpPath, "wb");
    if (!f) {
        Log("STK: shared save FAILED to open %s (err=%lu)\n", tmpPath, GetLastError());
        return FALSE;
    }
    DWORD magic = 0x534B5453u /* "STKS" */, version = 1, numTabs = STASH_NUM_STK_TABS;
    fwrite(&magic,   sizeof(magic),   1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&numTabs, sizeof(numTabs), 1, f);
    fwrite(g_stashStkSh, sizeof(g_stashStkSh), 1, f);
    fclose(f);
    {
        char bakPath[MAX_PATH];
        _snprintf(bakPath, sizeof(bakPath) - 1, "%s.bak", path);
        bakPath[sizeof(bakPath) - 1] = 0;
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
            CopyFileA(path, bakPath, FALSE);
    }
    if (!MoveFileExA(tmpPath, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Log("STK: shared save rename failed err=%lu — leaving tmp at %s\n", GetLastError(), tmpPath);
        return FALSE;
    }
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
    char path[MAX_PATH], dir[MAX_PATH], tmpPath[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sap_stash_stk_%s.dat", dir, charName);
    /* atomic .tmp+rename with .bak so a crash can't truncate the STK file. */
    _snprintf(tmpPath, sizeof(tmpPath) - 1, "%s.tmp", path);
    tmpPath[sizeof(tmpPath) - 1] = 0;
    FILE* f = fopen(tmpPath, "wb");
    if (!f) {
        Log("STK: AP-stk save FAILED to open %s (err=%lu)\n", tmpPath, GetLastError());
        return FALSE;
    }
    DWORD magic = 0x414B5453u /* "STKA" */, version = 1, numTabs = STASH_NUM_STK_TABS;
    fwrite(&magic,   sizeof(magic),   1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&numTabs, sizeof(numTabs), 1, f);
    fwrite(g_stashStkAp, sizeof(g_stashStkAp), 1, f);
    fclose(f);
    {
        char bakPath[MAX_PATH];
        _snprintf(bakPath, sizeof(bakPath) - 1, "%s.bak", path);
        bakPath[sizeof(bakPath) - 1] = 0;
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
            CopyFileA(path, bakPath, FALSE);
    }
    if (!MoveFileExA(tmpPath, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Log("STK: AP-stk save rename failed err=%lu — leaving tmp at %s\n", GetLastError(), tmpPath);
        return FALSE;
    }
    return TRUE;
}
