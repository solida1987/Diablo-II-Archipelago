/* d2arch_shops.c — Shop Shuffle "full pool" runtime.
 *
 * While ShopShuffle is on, the launcher gives every vendor a seeded hand from
 * the whole spawnable item list (weapons/armor/misc vendor columns). D2's own
 * shelf filler, SUNITNPC_FillInventory (D2Game+0x97100, 1.10f), would then
 * walk those rows in FILE ORDER, skip everything with qlvl above clvl+5 (and
 * above the act cap 12/20/28/36/45 in Normal), roll normal/magic only, and
 * stop after 32 failed placements — so every shop would show the same first
 * thirty rows of weapons.txt, never a rune, never a rare.
 *
 * This module detours +0x97100 and fills the shelf itself:
 *   - rows visited in a fresh random order per refresh (the "reol")
 *   - quality rolled per item: normal/superior, magic, rare, set, unique
 *   - set/unique rows picked to match the base code and stamped through the
 *     existing CreateItemEx hook (Hooks_ArmForcedItemRow, d2arch_hooks.c)
 *   - items created with the ENGINE's own STORES_CreateStoreItem (+0x96A60),
 *     so store page, placement, identify flag and pricing stay vanilla
 *
 * Safety: both detour targets are prologue-verified, and the module only arms
 * itself after watching the engine fill one shelf and finding the argument
 * layout it expects (the V1 self-test). Until then the engine fills as usual.
 *
 * Verified 2026-09-04 against the shipped D2Game.dll (ImageBase 0x6FC30000):
 *   +0x97100 SUNITNPC_FillInventory  __fastcall(pGame, pPlayer, pNpc, pState) ret 8
 *   +0x96A60 STORES_CreateStoreItem  __fastcall(pNpc, dwCode, pGame, bIdentify,
 *                                               nQuality, nItemLvl, nClvl)    ret 0x14
 *   +0x9B910 STORES_GetVendorEntry   __fastcall(pGame, pNpc, int* pIdx)      ret 4
 *   +0x9CC00 STORES_ClearStore       __fastcall(pGame, pEntry, pNpc)          ret 4
 *   vendor entry (0x44 bytes): +0x2C ShopRow* rows, +0x30 count,
 *                              +0x34 DWORD* permCodes, +0x38 permCount,
 *                              +0x20 fill state (what the engine passes as pState)
 */
#ifndef D2ARCH_SHOPS_C
#define D2ARCH_SHOPS_C

#define SHOPS_RVA_FILL        0x97100
#define SHOPS_RVA_CREATE      0x96A60
#define SHOPS_RVA_GETVENDOR   0x9B910
#define SHOPS_RVA_CLEAR       0x9CC00

#define SHOPS_VE_ROWS         0x2C
#define SHOPS_VE_COUNT        0x30
#define SHOPS_VE_PERM         0x34
#define SHOPS_VE_PERMCNT      0x38
#define SHOPS_VE_STATE        0x20

#define SHOPS_MAX_ROWS        512
#define SHOPS_MAX_PER_REFRESH 40
#define SHOPS_MAX_FAILS       32
#define SHOPS_ITEM_LEVEL      99     /* Q3: every affix, base and unique from clvl 1 */

/* D2ItemsTxt (0x1A8 bytes) */
#define SHOPS_ITX_MAXSTACK    0xE8
#define SHOPS_ITX_TYPE        0x11E
/* D2ItemTypesTxt (0xE4 bytes) */
#define SHOPS_ITT_STRIDE      0xE4
#define SHOPS_ITT_MAGIC       0x14
#define SHOPS_ITT_RARE        0x15
#define SHOPS_ITT_NORMAL      0x16
/* sgptDataTables */
#define SHOPS_DT_PITEMTYPES   0xBF4
#define SHOPS_DT_NITEMTYPES   0xBF8

enum { SQ_INFERIOR = 1, SQ_NORMAL = 2, SQ_SUPERIOR = 3, SQ_MAGIC = 4, SQ_SET = 5, SQ_RARE = 6, SQ_UNIQUE = 7 };

typedef struct { BYTE min, max, magicMin, magicMax; DWORD code; BYTE magicLvl; BYTE pad[3]; } ShopRow;

typedef void  (__fastcall *ShopsFill_t)(void* pGame, void* pPlayer, void* pNpc, void* pState);
typedef void* (__fastcall *ShopsCreate_t)(void* pNpc, DWORD dwCode, void* pGame, int bIdentify,
                                          int nQuality, int nItemLvl, int nClvl);
typedef BYTE* (__fastcall *ShopsGetVendor_t)(void* pGame, void* pNpc, int* pIdx);
typedef void  (__fastcall *ShopsClear_t)(void* pGame, BYTE* pEntry, void* pNpc);
typedef BYTE* (__stdcall  *ShopsGetItemRec_t)(DWORD dwCode, int* pItemId);   /* D2Common #10601 */

static Detour            s_shopsFillDetour;
static Detour            s_shopsCreateDetour;
static ShopsCreate_t     s_shopsCreateRaw   = NULL;
static ShopsGetVendor_t  s_shopsGetVendor   = NULL;
static ShopsClear_t      s_shopsClear       = NULL;
static ShopsGetItemRec_t s_shopsGetItemRec  = NULL;
static BOOL              s_shopsInstalled   = FALSE;
static BOOL              s_shopsArmed       = FALSE;
static BOOL              s_shopsProbing     = FALSE;   /* engine fill under observation */
static BOOL              s_shopsInOurFill   = FALSE;
static int               s_shopsProbeSane   = 0;
static int               s_shopsProbeInsane = 0;
static int               s_shopsLogBudget   = 80;      /* per-item lines, then summaries only */
static DWORD             s_shopsRng         = 0;

/* Everything this module calls is already in scope through the unity build:
   Quests_CodeIsQuestItem / Quests_LoadUniqueCatalog / g_uniqueCatalog (quests.c),
   Coll_GetUniqueFileIndex / Coll_GetSetPieceFileIndex / Coll_GetSgptDT /
   g_collSetPieces (collections.c), Detour / Detour_Install /
   Hooks_ArmForcedItemRow / Hooks_DisarmForcedItemRow (hooks.c),
   fnGetStat / fnSetStat (api.c), g_shopShuffleEnabled (shuffle.c), g_cachedPGame (gameloop.c). */

/* ---- small helpers -------------------------------------------------------- */

static DWORD Shops_Rand(void) {
    /* xorshift32; reseeded per fill from the tick count so every refresh is a new shelf. */
    DWORD x = s_shopsRng ? s_shopsRng : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s_shopsRng = x;
    return x;
}
static int Shops_RandN(int n) { return n > 0 ? (int)(Shops_Rand() % (DWORD)n) : 0; }

/* 'hax' -> 'hax ' as the engine stores item codes (little-endian, space padded). */
static DWORD Shops_PackCode(const char* c) {
    BYTE b[4] = { ' ', ' ', ' ', ' ' };
    for (int i = 0; i < 4 && c && c[i]; i++) b[i] = (BYTE)c[i];
    return (DWORD)b[0] | ((DWORD)b[1] << 8) | ((DWORD)b[2] << 16) | ((DWORD)b[3] << 24);
}

static void Shops_CodeStr(DWORD code, char out[5]) {
    out[0] = (char)(code & 0xFF); out[1] = (char)((code >> 8) & 0xFF);
    out[2] = (char)((code >> 16) & 0xFF); out[3] = (char)((code >> 24) & 0xFF); out[4] = 0;
    for (int i = 0; i < 4; i++) if (out[i] < 0x20 || out[i] > 0x7E) out[i] = '?';
}

typedef struct {
    BYTE* pRec;        /* D2ItemsTxt* or NULL */
    BOOL  normalOnly;  /* ItemTypes Normal=1 (gems, runes, potions) */
    BOOL  magicOnly;   /* ItemTypes Magic=1 (ring, amulet, jewel, charm) */
    BOOL  canRare;     /* ItemTypes Rare=1 */
    int   maxStack;
} ShopItemInfo;

static BOOL Shops_Lookup(DWORD code, ShopItemInfo* out) {
    memset(out, 0, sizeof(*out));
    if (!s_shopsGetItemRec) return FALSE;
    int itemId = -1;
    BYTE* rec = NULL;
    __try { rec = s_shopsGetItemRec(code, &itemId); }
    __except (EXCEPTION_EXECUTE_HANDLER) { rec = NULL; }
    if (!rec) return FALSE;
    out->pRec = rec;
    __try {
        out->maxStack = *(int*)(rec + SHOPS_ITX_MAXSTACK);
        int type = *(short*)(rec + SHOPS_ITX_TYPE);
        DWORD dt = Coll_GetSgptDT();
        if (dt && type >= 0) {
            BYTE* pTypes = *(BYTE**)(dt + SHOPS_DT_PITEMTYPES);
            int   nTypes = *(int*)(dt + SHOPS_DT_NITEMTYPES);
            if (pTypes && type < nTypes) {
                BYTE* t = pTypes + type * SHOPS_ITT_STRIDE;
                BOOL magic  = t[SHOPS_ITT_MAGIC]  != 0;
                BOOL rare   = t[SHOPS_ITT_RARE]   != 0;
                BOOL normal = t[SHOPS_ITT_NORMAL] != 0;
                out->normalOnly = normal && !magic;
                out->magicOnly  = magic && !normal;
                out->canRare    = rare;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return TRUE;
}

/* Q2 distribution. Gear: 30 normal/superior, 35 magic, 15 rare, 10 set, 10 unique.
   Magic-only types: 55 magic, 25 rare, 10 set, 10 unique. Normal-only: always normal. */
static int Shops_RollQuality(const ShopItemInfo* info) {
    if (info->normalOnly) return SQ_NORMAL;
    int r = Shops_RandN(100);
    if (info->magicOnly) {
        if (r < 55) return SQ_MAGIC;
        if (r < 80) return info->canRare ? SQ_RARE : SQ_MAGIC;
        if (r < 90) return SQ_SET;
        return SQ_UNIQUE;
    }
    if (r < 30) return (Shops_RandN(100) < 25) ? SQ_SUPERIOR : SQ_NORMAL;
    if (r < 65) return SQ_MAGIC;
    if (r < 80) return info->canRare ? SQ_RARE : SQ_MAGIC;
    if (r < 90) return SQ_SET;
    return SQ_UNIQUE;
}

/* Runtime UniqueItems/SetItems row for a base code, or -1. Scans the catalogs
   every call — at most 40 items per refresh, so cheap enough not to cache. */
static int Shops_PickRow(DWORD code, int quality) {
    int cand[64]; int n = 0;
    if (quality == SQ_UNIQUE) {
        Quests_LoadUniqueCatalog();
        for (int i = 0; i < g_uniqueCatalogCount && n < 64; i++) {
            char c[5]; memcpy(c, g_uniqueCatalog[i].baseCode, 4); c[4] = 0;
            if (Shops_PackCode(c) != code) continue;
            if (Quests_CodeIsQuestItem(c)) continue;
            int row = Coll_GetUniqueFileIndex(i);
            if (row >= 0) cand[n++] = row;
        }
    } else if (quality == SQ_SET) {
        for (int i = 0; i < COLL_NUM_SET_PIECES && n < 64; i++) {
            if (Shops_PackCode(g_collSetPieces[i].code) != code) continue;
            int row = Coll_GetSetPieceFileIndex(i);
            if (row >= 0) cand[n++] = row;
        }
    }
    if (n == 0) return -1;
    return cand[Shops_RandN(n)];
}

static void* Shops_Create(void* pNpc, DWORD code, void* pGame, int quality, int ilvl, int clvl) {
    ShopsCreate_t fn = s_shopsCreateDetour.installed
                     ? (ShopsCreate_t)s_shopsCreateDetour.trampoline
                     : s_shopsCreateRaw;
    if (!fn) return NULL;
    return fn(pNpc, code, pGame, 1, quality, ilvl, clvl);
}

/* ---- the fill ------------------------------------------------------------- */

static void Shops_FillFullPool(void* pGame, void* pPlayer, void* pNpc) {
    int idx = 0;
    BYTE* ve = s_shopsGetVendor(pGame, pNpc, &idx);
    if (!ve) { Log("SHOPS: no vendor entry for npc=%p\n", pNpc); return; }

    ShopRow* rows = *(ShopRow**)(ve + SHOPS_VE_ROWS);
    int      n    = *(int*)(ve + SHOPS_VE_COUNT);
    DWORD*   perm = *(DWORD**)(ve + SHOPS_VE_PERM);
    int      pc   = *(int*)(ve + SHOPS_VE_PERMCNT);
    if (n < 0 || n > SHOPS_MAX_ROWS || pc < 0 || pc > SHOPS_MAX_ROWS) {
        Log("SHOPS: vendor entry looks wrong (rows=%d perm=%d) — skipping fill\n", n, pc);
        return;
    }

    int clvl = fnGetStat ? fnGetStat(pPlayer, 12 /* STAT_LEVEL */, 0) : 1;
    if (clvl < 1) clvl = 1;
    int ilvl = SHOPS_ITEM_LEVEL;

    s_shopsRng = GetTickCount() ^ (DWORD)pNpc ^ ((DWORD)idx << 24) ^ 0xA5A5A5A5u;
    WORD order[SHOPS_MAX_ROWS];
    for (int i = 0; i < n; i++) order[i] = (WORD)i;
    for (int i = n - 1; i > 0; i--) {                /* Fisher-Yates: a new shelf every refresh */
        int j = Shops_RandN(i + 1);
        WORD t = order[i]; order[i] = order[j]; order[j] = t;
    }

    int made = 0, fails = 0, cnt[8] = {0};
    s_shopsInOurFill = TRUE;
    for (int k = 0; k < n && fails <= SHOPS_MAX_FAILS && made < SHOPS_MAX_PER_REFRESH; k++) {
        ShopRow* r = rows ? &rows[order[k]] : NULL;
        if (!r || !r->code) continue;
        ShopItemInfo info;
        if (!Shops_Lookup(r->code, &info)) continue;

        int q = Shops_RollQuality(&info);
        int forcedRow = -1;
        if (q == SQ_UNIQUE || q == SQ_SET) {
            forcedRow = Shops_PickRow(r->code, q);
            if (forcedRow < 0) q = info.canRare ? SQ_RARE : SQ_MAGIC;   /* no named item on this base */
            else Hooks_ArmForcedItemRow(forcedRow, pGame, q == SQ_UNIQUE);
        }
        void* item = NULL;
        __try { item = Shops_Create(pNpc, r->code, pGame, q, ilvl, clvl); }
        __except (EXCEPTION_EXECUTE_HANDLER) { item = NULL; }
        Hooks_DisarmForcedItemRow();

        if (item) { made++; if (q >= 0 && q < 8) cnt[q]++; } else fails++;
        if (s_shopsLogBudget > 0) {
            s_shopsLogBudget--;
            char cs[5]; Shops_CodeStr(r->code, cs);
            Log("SHOPS:   %s q=%d row=%d -> %s\n", cs, q, forcedRow, item ? "ok" : "FAIL");
        }
    }

    /* Permanent stock exactly as the engine does it: potions, scrolls, keys, ammo. */
    int permMade = 0;
    for (int i = 0; i < pc && fails <= SHOPS_MAX_FAILS; i++) {
        DWORD code = perm ? perm[i] : 0;
        if (!code) continue;
        void* it = NULL;
        __try { it = Shops_Create(pNpc, code, pGame, SQ_NORMAL, ilvl, clvl); }
        __except (EXCEPTION_EXECUTE_HANDLER) { it = NULL; }
        if (!it) { fails++; continue; }
        permMade++;
        if (code == Shops_PackCode("aqv") || code == Shops_PackCode("cqv")) {
            ShopItemInfo info;
            if (Shops_Lookup(code, &info) && info.maxStack > 0 && fnSetStat) {
                __try { fnSetStat(it, 70 /* STAT_ITEM_QUANTITY */, info.maxStack, 0); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    }
    s_shopsInOurFill = FALSE;

    Log("SHOPS: fill npc=%p vendor=%d rows=%d clvl=%d ilvl=%d -> made=%d perm=%d fails=%d "
        "[normal=%d superior=%d magic=%d set=%d rare=%d unique=%d]\n",
        pNpc, idx, n, clvl, ilvl, made, permMade, fails,
        cnt[SQ_NORMAL], cnt[SQ_SUPERIOR], cnt[SQ_MAGIC], cnt[SQ_SET], cnt[SQ_RARE], cnt[SQ_UNIQUE]);
}

/* ---- hooks ---------------------------------------------------------------- */

/* Probe on STORES_CreateStoreItem: while the engine fills a shelf we log what it
   passes and check it against the layout this module relies on (V1). */
static void* __fastcall Shops_CreateHook(void* pNpc, DWORD code, void* pGame, int bIdentify,
                                         int nQuality, int nItemLvl, int nClvl) {
    if (s_shopsProbing && !s_shopsInOurFill) {
        BOOL sane = (pGame == (void*)g_cachedPGame || g_cachedPGame == 0) &&
                    bIdentify == 1 && nQuality >= 1 && nQuality <= 4 &&
                    nItemLvl >= 1 && nItemLvl <= 104 && nClvl >= 1 && nClvl <= 99;
        if (sane) s_shopsProbeSane++; else s_shopsProbeInsane++;
        if (s_shopsProbeSane + s_shopsProbeInsane <= 12) {
            char cs[5]; Shops_CodeStr(code, cs);
            Log("SHOPS: engine create npc=%p code=%s pGame=%p ident=%d q=%d ilvl=%d clvl=%d -> %s\n",
                pNpc, cs, pGame, bIdentify, nQuality, nItemLvl, nClvl, sane ? "sane" : "UNEXPECTED");
        }
    }
    return ((ShopsCreate_t)s_shopsCreateDetour.trampoline)(pNpc, code, pGame, bIdentify,
                                                           nQuality, nItemLvl, nClvl);
}

static void __fastcall Shops_FillHook(void* pGame, void* pPlayer, void* pNpc, void* pState) {
    ShopsFill_t orig = (ShopsFill_t)s_shopsFillDetour.trampoline;
    if (!g_shopShuffleEnabled || !s_shopsGetVendor) { orig(pGame, pPlayer, pNpc, pState); return; }

    if (!s_shopsArmed) {
        /* V1 self-test: let the engine fill this shelf once under observation. */
        s_shopsProbeSane = s_shopsProbeInsane = 0;
        s_shopsProbing = TRUE;
        orig(pGame, pPlayer, pNpc, pState);
        s_shopsProbing = FALSE;

        BYTE* ve = NULL; int idx = 0;
        __try { ve = s_shopsGetVendor(pGame, pNpc, &idx); } __except (EXCEPTION_EXECUTE_HANDLER) { ve = NULL; }
        BOOL layoutOk = ve && ((BYTE*)pState == ve + SHOPS_VE_STATE);
        int rows = 0;
        if (ve) { __try { rows = *(int*)(ve + SHOPS_VE_COUNT); } __except (EXCEPTION_EXECUTE_HANDLER) { rows = -1; } }

        if (layoutOk && s_shopsProbeInsane == 0 && s_shopsProbeSane >= 1 && rows >= 0 && rows <= SHOPS_MAX_ROWS) {
            s_shopsArmed = TRUE;
            Log("SHOPS: self-test passed (sane=%d, entry=%p rows=%d) — full-pool fill ARMED\n",
                s_shopsProbeSane, ve, rows);
            if (s_shopsClear) {
                __try {
                    s_shopsClear(pGame, ve, pNpc);          /* drop the engine's shelf ... */
                    Shops_FillFullPool(pGame, pPlayer, pNpc); /* ... and build ours */
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    Log("SHOPS: refill after self-test faulted — engine shelf stays\n");
                }
            }
        } else {
            Log("SHOPS: self-test NOT passed (layoutOk=%d sane=%d unexpected=%d rows=%d) — engine fill kept\n",
                layoutOk, s_shopsProbeSane, s_shopsProbeInsane, rows);
        }
        return;
    }

    __try { Shops_FillFullPool(pGame, pPlayer, pNpc); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("SHOPS: fill faulted — shelf left as is\n"); }
}

/* Called from Hooks_Install once hD2Game is known. */
void Shops_Install(HMODULE hGame) {
    if (s_shopsInstalled || !hGame) return;
    s_shopsInstalled = TRUE;

    static const BYTE kFillPro[]   = { 0x83, 0xEC, 0x48, 0x53, 0x55 };   /* sub esp,48h; push ebx; push ebp */
    static const BYTE kCreatePro[] = { 0x83, 0xEC, 0x0C, 0x53, 0x55 };   /* sub esp,0Ch; push ebx; push ebp */
    static const BYTE kVendorPro[] = { 0x56, 0x85, 0xD2, 0x57 };         /* push esi; test edx,edx; push edi */
    static const BYTE kClearPro[]  = { 0x56, 0x57, 0x8B, 0xF2, 0x8B, 0xF9 }; /* push esi; push edi; mov esi,edx; mov edi,ecx */

    BYTE* pf = (BYTE*)((DWORD)hGame + SHOPS_RVA_FILL);
    BYTE* pcr = (BYTE*)((DWORD)hGame + SHOPS_RVA_CREATE);
    BYTE* pv = (BYTE*)((DWORD)hGame + SHOPS_RVA_GETVENDOR);
    BYTE* pcl = (BYTE*)((DWORD)hGame + SHOPS_RVA_CLEAR);
    BOOL fOk = FALSE, cOk = FALSE, vOk = FALSE, clOk = FALSE;
    __try {
        fOk  = memcmp(pf,  kFillPro,   sizeof(kFillPro))   == 0;
        cOk  = memcmp(pcr, kCreatePro, sizeof(kCreatePro)) == 0;
        vOk  = memcmp(pv,  kVendorPro, sizeof(kVendorPro)) == 0;
        clOk = memcmp(pcl, kClearPro,  sizeof(kClearPro))  == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { fOk = cOk = vOk = clOk = FALSE; }

    HMODULE hCommon = GetModuleHandleA("D2Common.dll");
    if (hCommon) s_shopsGetItemRec = (ShopsGetItemRec_t)GetProcAddress(hCommon, (LPCSTR)10601);

    if (!fOk || !cOk || !vOk || !s_shopsGetItemRec) {
        Log("SHOPS: prologue/import mismatch (fill=%d create=%d vendor=%d clear=%d itemrec=%p) — "
            "full-pool fill DISABLED, engine fill only\n", fOk, cOk, vOk, clOk, s_shopsGetItemRec);
        return;
    }
    s_shopsCreateRaw = (ShopsCreate_t)pcr;
    s_shopsGetVendor = (ShopsGetVendor_t)pv;
    s_shopsClear     = clOk ? (ShopsClear_t)pcl : NULL;

    BOOL okC = Detour_Install(&s_shopsCreateDetour, (void*)pcr, (void*)Shops_CreateHook, 5);
    BOOL okF = Detour_Install(&s_shopsFillDetour,   (void*)pf,  (void*)Shops_FillHook,   5);
    Log("SHOPS: hooks fill=%d create=%d clear=%d (shopShuffle=%d) — arms after first shelf self-test\n",
        okF, okC, clOk, g_shopShuffleEnabled);
}

#endif /* D2ARCH_SHOPS_C */
