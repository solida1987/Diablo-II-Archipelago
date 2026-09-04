/* d2arch_ubers.c - Pandemonium uber boss cube recipes (1.10f) Two recipes already exist in cubemain.txt with output="Cow Portal": - "Pandemonium key": pk1 + pk2 + pk3 (3 keys) - "Pandemonium Finale key": dhn + mbr + bey (3 organs) - vanilla "1 Wirt's leg + 1 town portal book" still works as Cow Portal We hook PLRTRADE_CreateCowPortal (D2Game+0x60000), inspect the player's cube contents, and dispatch: - 3 keys outside town -> spawn Uber Andariel/Duriel/Izual (mons 707/708/706) - 3 organs outside town -> spawn Uber Mephisto/Diablo/Baal (mons 704/705/709) - In town -> notify "Cannot use Pandemonium recipe in town", reject - Anything else -> call trampoline (vanilla Cow Portal logic) Why this approach instead of the proper portal+zone path: 1.10f's Pandemonium Run/Finale level IDs (138-141) are inside #ifdef D2_VERSION_HAS_UBERS and don't exist. */

#define MK_CODE4(a,b,c,d) ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b)<<8) | ((DWORD)(BYTE)(c)<<16) | ((DWORD)(BYTE)(d)<<24))

/* Item codes packed as little-endian DWORDs. */
#define UBER_CODE_PK1 MK_CODE4('p','k','1',' ')
#define UBER_CODE_PK2 MK_CODE4('p','k','2',' ')
#define UBER_CODE_PK3 MK_CODE4('p','k','3',' ')
#define UBER_CODE_DHN MK_CODE4('d','h','n',' ')
#define UBER_CODE_MBR MK_CODE4('m','b','r',' ')
#define UBER_CODE_BEY MK_CODE4('b','e','y',' ')
#define UBER_CODE_HFT MK_CODE4('h','f','t',' ')

/* Custom MonStats rows (Game/data/global/excel/MonStats.txt 707-712). */
#define UBER_MONID_MEPHISTO 704
#define UBER_MONID_DIABLO   705
#define UBER_MONID_IZUAL    706
#define UBER_MONID_ANDARIEL 707  /* listed as "Lilith" in MonStats.txt */
#define UBER_MONID_DURIEL   708
#define UBER_MONID_BAAL     709

/* Resolved at first-call init */
static DWORD u_classId_pk1 = (DWORD)-1;
static DWORD u_classId_pk2 = (DWORD)-1;
static DWORD u_classId_pk3 = (DWORD)-1;
static DWORD u_classId_dhn = (DWORD)-1;
static DWORD u_classId_mbr = (DWORD)-1;
static DWORD u_classId_bey = (DWORD)-1;
static BOOL  u_classIds_resolved = FALSE;

/* PLRTRADE_CreateCowPortal hook state. */
typedef int (__fastcall *CreateCowPortal_t)(void* pGame, void* pUnit);
static CreateCowPortal_t u_origCreateCowPortal = NULL;
static DWORD* u_cubeDispatchTable = NULL;
static BOOL  u_cowHookInstalled = FALSE;

/* Uber tracking: when we spawn an uber, we record its GUID + which "set" it belongs to (mini=1, finale=2). */
#define UBER_TRACK_MAX 16
typedef struct {
    DWORD guid;       /* dwUnitId */
    int   set;        /* 1 = mini trio, 2 = finale trio */
    int   monId;      /* original spawn monId for organ-drop mapping */
    BOOL  killed;     /* set when death hook fires */
} UberTrackEntry;
static UberTrackEntry u_uberList[UBER_TRACK_MAX];
static int u_uberCount = 0;
static int u_finaleSpawnedCount = 0;  /* across all spawns; resets when all 3 dead */
static int u_finaleKillCount    = 0;

/* Resolve our 6 item codes to ItemsTxt class IDs via D2Common's exported DATATBLS_GetItemRecordFromItemCode (ord 10601). */
typedef void* (__stdcall *GetItemFromCode_t)(DWORD dwCode, int* pItemId);
static GetItemFromCode_t u_fnGetItemFromCode = NULL;

static void Ubers_ResolveItemCodes(void) {
    if (u_classIds_resolved) return;
    HMODULE hCommon = GetModuleHandleA("D2Common.dll");
    if (!hCommon) {
        Log("UBERS: D2Common.dll not loaded, can't resolve item codes\n");
        return;
    }
    if (!u_fnGetItemFromCode) {
        u_fnGetItemFromCode = (GetItemFromCode_t)GetProcAddress(hCommon, (LPCSTR)10601);
    }
    if (!u_fnGetItemFromCode) {
        Log("UBERS: D2Common ord 10601 not resolvable\n");
        return;
    }
    int id;
    #define RESOLVE(name, code) do { \
        id = -1; \
        u_fnGetItemFromCode((DWORD)(code), &id); \
        u_classId_##name = (DWORD)id; \
        Log("UBERS: code '%c%c%c' -> classId=%d\n", \
            (char)((code)&0xFF), (char)(((code)>>8)&0xFF), \
            (char)(((code)>>16)&0xFF), id); \
    } while(0)
    RESOLVE(pk1, UBER_CODE_PK1);
    RESOLVE(pk2, UBER_CODE_PK2);
    RESOLVE(pk3, UBER_CODE_PK3);
    RESOLVE(dhn, UBER_CODE_DHN);
    RESOLVE(mbr, UBER_CODE_MBR);
    RESOLVE(bey, UBER_CODE_BEY);
    #undef RESOLVE
    u_classIds_resolved = TRUE;
}

/* Walk pUnit's inventory and collect dwClassId values for items in the cube (nInvPage == INVPAGE_CUBE = 3). */
static int Ubers_CollectCubeItems(void* pUnit, DWORD* outIds, int maxIds) {
    int count = 0;
    if (!pUnit || maxIds <= 0) return 0;
    __try {
        DWORD pInv = *(DWORD*)((DWORD)pUnit + 0x60);
        if (!pInv || pInv < 0x10000) return 0;
        DWORD pItem = *(DWORD*)(pInv + 0x0C);  /* pFirstItem */
        int safety = 0;
        while (pItem && pItem > 0x10000 && safety++ < 256) {
            DWORD pItemData = *(DWORD*)(pItem + 0x14);
            if (pItemData && pItemData > 0x10000) {
                BYTE invPage = *(BYTE*)(pItemData + 0x45);
                if (invPage == 3 /* INVPAGE_CUBE */) {
                    DWORD classId = *(DWORD*)(pItem + 0x04);
                    if (count < maxIds) outIds[count++] = classId;
                }
                /* pNextItem in extra data at pItemData + 0x5C + 0x08 = +0x64 */
                DWORD pNext = *(DWORD*)(pItemData + 0x64);
                if (pNext == pItem) break;  /* paranoia: self-loop */
                pItem = pNext;
            } else {
                break;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("UBERS: exception walking inventory\n");
        return 0;
    }
    return count;
}

/* Returns TRUE if `cubeIds[]` (count items) contains all 3 of {a,b,c}. */
static BOOL Ubers_CubeHasTriple(const DWORD* cubeIds, int count,
                                DWORD a, DWORD b, DWORD c) {
    if (count < 3) return FALSE;
    if (a == (DWORD)-1 || b == (DWORD)-1 || c == (DWORD)-1) return FALSE;
    BOOL ha = FALSE, hb = FALSE, hc = FALSE;
    for (int i = 0; i < count; i++) {
        if (cubeIds[i] == a) ha = TRUE;
        else if (cubeIds[i] == b) hb = TRUE;
        else if (cubeIds[i] == c) hc = TRUE;
    }
    return ha && hb && hc;
}

/* Spawn an uber boss at (centerX + dx, centerY + dy) using fnSpawnMonster resolved in d2arch_main.c. */
static BOOL Ubers_SpawnAt(void* pGame, void* pRoom, int x, int y,
                          int monId, int set) {
    if (!fnSpawnMonster) {
        Log("UBERS: fnSpawnMonster not resolved\n");
        return FALSE;
    }
    void* pMon = NULL;
    __try {
        pMon = fnSpawnMonster(pGame, pRoom, x, y, monId, 1, -1, 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) { pMon = NULL; }
    if (!pMon) {
        __try {
            pMon = fnSpawnMonster(pGame, pRoom, x, y, monId, 0, 4, 0);
        } __except(EXCEPTION_EXECUTE_HANDLER) { pMon = NULL; }
    }
    Log("UBERS: spawned monId=%d at (%d,%d) set=%d -> pMon=%p\n",
        monId, x, y, set, pMon);
    if (!pMon) return FALSE;

    /* Tracker full? Compact killed entries out first. Mini-uber corpses used to accumulate here forever (only finale completion compacted), so after ~5 rounds every new uber spawned UNTRACKED — a finale trio could then never satisfy Ubers_FinaleAllDead = organs consumed, all three killed, no Hellfire Torch until save&quit. */
    if (u_uberCount >= UBER_TRACK_MAX) {
        int w = 0;
        for (int r = 0; r < u_uberCount; r++) {
            if (!u_uberList[r].killed) u_uberList[w++] = u_uberList[r];
        }
        Log("UBERS: tracker compacted %d -> %d live entries\n", u_uberCount, w);
        u_uberCount = w;
    }
    if (u_uberCount < UBER_TRACK_MAX) {
        DWORD guid = 0;
        __try { guid = *(DWORD*)((DWORD)pMon + 0x0C); }  /* dwUnitId */
        __except(EXCEPTION_EXECUTE_HANDLER) { guid = 0; }
        u_uberList[u_uberCount].guid   = guid;
        u_uberList[u_uberCount].set    = set;
        u_uberList[u_uberCount].monId  = monId;
        u_uberList[u_uberCount].killed = FALSE;
        u_uberCount++;
        if (set == 2) u_finaleSpawnedCount++;
    } else {
        Log("UBERS: WARNING tracker full of LIVE ubers — monId=%d spawned untracked\n",
            monId);
    }
    return TRUE;
}

/* STAGGERED TRIO SPAWNS — crash mitigation The trio used to spawn in a SINGLE server tick, inside the cube transmute dispatch, in an arbitrary level. */
#define UBER_PEND_MAX 8
static struct {
    int   monId;
    int   set;        /* 1 = mini, 2 = finale */
    DWORD dueTick;
    int   tries;
    BOOL  active;
} u_pendQ[UBER_PEND_MAX];

static void Ubers_QueueSpawn(int monId, int set, DWORD dueTick) {
    for (int i = 0; i < UBER_PEND_MAX; i++) {
        if (!u_pendQ[i].active) {
            u_pendQ[i].monId   = monId;
            u_pendQ[i].set     = set;
            u_pendQ[i].dueTick = dueTick;
            u_pendQ[i].tries   = 0;
            u_pendQ[i].active  = TRUE;
            return;
        }
    }
    Log("UBERS: pending queue full — monId=%d dropped\n", monId);
}

/* Called every game tick from ProcessPendingGameTick. */
void Ubers_TickPending(void* pGame, void* pPlayer) {
    DWORD now;
    int i;
    if (!pGame || !pPlayer) return;
    now = GetTickCount();
    for (i = 0; i < UBER_PEND_MAX; i++) {
        DWORD pPath = 0, pRoom = 0;
        int px = 0, py = 0;
        BOOL ok = FALSE;
        static const int offs[3][2] = { { 6, 0 }, { -3, 4 }, { -3, -4 } };
        if (!u_pendQ[i].active || now < u_pendQ[i].dueTick) continue;

        __try {
            pPath = *(DWORD*)((DWORD)pPlayer + 0x2C);
            if (pPath) {
                pRoom = *(DWORD*)(pPath + 0x1C);
                px = (int)*(unsigned short*)(pPath + 0x02);
                py = (int)*(unsigned short*)(pPath + 0x06);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { pRoom = 0; }

        if (pRoom && px && py) {
            int o;
            for (o = 0; o < 3 && !ok; o++)
                ok = Ubers_SpawnAt(pGame, (void*)pRoom,
                                   px + offs[o][0], py + offs[o][1],
                                   u_pendQ[i].monId, u_pendQ[i].set);
        }

        if (ok) {
            u_pendQ[i].active = FALSE;
        } else if (++u_pendQ[i].tries >= 12) {   /* ~8 s of retries */
            Log("UBERS: pending spawn GAVE UP monId=%d after %d tries\n",
                u_pendQ[i].monId, u_pendQ[i].tries);
            ShowNotify("An uber could not find open ground - move and it may appear");
            u_pendQ[i].active = FALSE;
        } else {
            u_pendQ[i].dueTick = now + 700;
        }
    }
}

/* Public: check if a killed unit was a tracked uber. */
static int Ubers_OnMonsterKilled(DWORD guid) {
    for (int i = 0; i < u_uberCount; i++) {
        if (u_uberList[i].guid == guid && !u_uberList[i].killed) {
            u_uberList[i].killed = TRUE;
            int set = u_uberList[i].set;
            if (set == 2) u_finaleKillCount++;
            Log("UBERS: kill tracked guid=%08X monId=%d set=%d (finale: %d/%d)\n",
                guid, u_uberList[i].monId, set,
                u_finaleKillCount, u_finaleSpawnedCount);
            return set;
        }
    }
    return 0;
}

/* Called on a finale boss kill: if all 3 finale ubers are dead, the Hellfire Torch should drop. */
static BOOL Ubers_FinaleAllDead(void) {
    if (u_finaleSpawnedCount > 0 && u_finaleKillCount >= u_finaleSpawnedCount) {
        /* Reset so the next finale recipe round starts fresh */
        int n = u_finaleSpawnedCount;
        u_finaleSpawnedCount = 0;
        u_finaleKillCount = 0;
        /* Also clear killed entries from tracker */
        int w = 0;
        for (int r = 0; r < u_uberCount; r++) {
            if (!u_uberList[r].killed) {
                u_uberList[w++] = u_uberList[r];
            }
        }
        u_uberCount = w;
        Log("UBERS: finale all %d dead - Hellfire Torch should drop\n", n);
        return TRUE;
    }
    return FALSE;
}

/* 1.9.0 Phase 2 — Drop the Hellfire Torch. */
static void Ubers_DropHellfireTorch(void* pGame, void* pSourceUnit) {
    HMODULE hD2Game = GetModuleHandleA("D2Game.dll");
    if (!hD2Game || !pGame || !pSourceUnit) {
        Log("UBERS: torch drop skipped (hD2Game=%p pGame=%p pSrc=%p)\n",
            hD2Game, pGame, pSourceUnit);
        return;
    }

    typedef void* (__fastcall *QUESTS_CreateItem_t)(
        void* pGame, void* pPlayer,
        DWORD dwCode, int nLevel, BYTE nQuality, int bDroppable);
    QUESTS_CreateItem_t fnCI =
        (QUESTS_CreateItem_t)((DWORD)hD2Game + 0x65DF0);

    /* `cm2` = Charm Large. Space-padded ('cm2 ') because DATATBLS_GetItemRecordFromItemCode hashes 4-byte codes. */
    DWORD code_cm2 = ((DWORD)'c') | ((DWORD)'m' << 8) | ((DWORD)'2' << 16) | ((DWORD)0x20 << 24);

    void* pItem = NULL;
    __try {
        /* nLevel=99, nQuality=7 (UNIQUE), bDroppable=0 = straight to the bags. */
        pItem = fnCI(pGame, pSourceUnit, code_cm2, 99, 7, 1);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("UBERS: torch drop EXCEPTION\n");
        return;
    }
    Log("UBERS: Hellfire Torch drop attempted -> pItem=%p (cm2 lvl99 quality=unique)\n",
        pItem);
}

/* 1.9.0 Phase 2 — Called from the unit-death walk in d2arch_gameloop.c for every dead `type==1` unit it sees. */
void Ubers_OnUnitDeathScan(void* pGame, void* pUnit, DWORD txtId, DWORD unitId) {
    /* Cheap pre-filter: only ubers (MonStats rows 704-712) should reach the tracker. */
    if (txtId < UBER_MONID_MEPHISTO || txtId > UBER_MONID_BAAL) return;

    int set = Ubers_OnMonsterKilled(unitId);
    if (set == 0) {
        /* unit was an uber-range monId but not in our track list — could be a vanilla spawn (unlikely at txtId 704-712 but possible if future content adds non-recipe ubers). */
        return;
    }

    Log("UBERS: death-scan recorded kill (set=%d txtId=%u unitId=%08X)\n",
        set, txtId, unitId);

    /* Wire Custom Goal (goal=4) uber-kill detection. */
    if (set == 2) {
        int uberId = -1;
        if (txtId == UBER_MONID_MEPHISTO)      uberId = 0;
        else if (txtId == UBER_MONID_DIABLO)   uberId = 1;
        else if (txtId == UBER_MONID_BAAL)     uberId = 2;
        if (uberId >= 0) {
            extern void CustomGoal_OnUberKilled(int);
            CustomGoal_OnUberKilled(uberId);
        }
    }

    if (set == 2 && Ubers_FinaleAllDead()) {
        ShowNotify("Hellfire Torch has appeared!");
        Ubers_DropHellfireTorch(pGame, pUnit);
        /* F1 Logbook: count the completed finale run. */
        {
            extern void Stats_OnPandemoniumRunComplete(void);
            Stats_OnPandemoniumRunComplete();
        }
        /* Wire Custom Goal finale-complete event. */
        {
            extern void CustomGoal_OnHellfireTorchComplete(void);
            CustomGoal_OnHellfireTorchComplete();
        }
    }
}

/* Public reset hook called from d2arch_main.c WndProc EXIT. */
void Ubers_ResetTrackingOnPlayerGone(void) {
    int hadEntries = u_uberCount;
    int hadFinale = u_finaleSpawnedCount;
    u_uberCount = 0;
    u_finaleSpawnedCount = 0;
    u_finaleKillCount = 0;
    for (int i = 0; i < UBER_TRACK_MAX; i++) {
        u_uberList[i].guid = 0;
        u_uberList[i].set = 0;
        u_uberList[i].monId = 0;
        u_uberList[i].killed = FALSE;
    }
    if (hadEntries > 0 || hadFinale > 0) {
        Log("UBERS: reset tracking on player-gone (had %d entries, %d finale spawned)\n",
            hadEntries, hadFinale);
    }
}

/* The hook wrapper. Called when D2 reaches output type=1 (Cow Portal) during cube transmute. We get pGame in ECX, pUnit in EDX (player). Return 1 = success (cube consumes inputs), 0 = fail (cube refuses). */
static int __fastcall UberCowPortalHook(void* pGame, void* pUnit) {
    /* (Cube-transmute counter moved to a Pre-dispatch hook on sub_6FC91250 (D2Game+0x61250) which catches the transmute-button click for ALL recipes — gem upgrades, runeword sockets, rerolls etc. */

    /* Inspect cube contents BEFORE the engine consumes them */
    Ubers_ResolveItemCodes();
    DWORD cubeIds[16];
    int n = Ubers_CollectCubeItems(pUnit, cubeIds, 16);

    /* Debug log: what's in the cube vs. */
    char idsBuf[128] = {0};
    int off = 0;
    for (int i = 0; i < n && off < 100; i++) {
        off += _snprintf(idsBuf + off, 100 - off, "%d ", (int)cubeIds[i]);
    }
    Log("UBERS: cube hook fired. items=%d ids=[%s] keys=[%d,%d,%d] organs=[%d,%d,%d]\n",
        n, idsBuf,
        (int)u_classId_pk1, (int)u_classId_pk2, (int)u_classId_pk3,
        (int)u_classId_dhn, (int)u_classId_mbr, (int)u_classId_bey);

    BOOL is3keys   = Ubers_CubeHasTriple(cubeIds, n,
                                         u_classId_pk1, u_classId_pk2, u_classId_pk3);
    BOOL is3organs = Ubers_CubeHasTriple(cubeIds, n,
                                         u_classId_dhn, u_classId_mbr, u_classId_bey);

    if (!is3keys && !is3organs) {
        /* Vanilla "Wirt's leg + Tome of TP" -> normal Cow Portal logic */
        if (u_origCreateCowPortal) {
            return u_origCreateCowPortal(pGame, pUnit);
        }
        /* Trampoline failed; fail safely. */
        return 0;
    }

    /* Read player's path/room/coords */
    DWORD pPath = 0, pRoom = 0;
    int pX = 0, pY = 0, curLevel = 0;
    __try {
        pPath = *(DWORD*)((DWORD)pUnit + 0x2C);
        if (pPath) {
            pRoom = *(DWORD*)(pPath + 0x1C);
            pX = (int)*(unsigned short*)(pPath + 0x02);
            pY = (int)*(unsigned short*)(pPath + 0x06);
            if (pRoom) {
                DWORD pDrlgRoom = *(DWORD*)(pRoom + 0x38);
                if (pDrlgRoom) {
                    DWORD pLevel = *(DWORD*)(pDrlgRoom + 0x00);
                    if (pLevel) curLevel = *(int*)(pLevel + 0x04);
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    if (!pRoom) {
        Log("UBERS: pRoom NULL, cannot spawn\n");
        return 0;
    }

    if (IsTown((DWORD)curLevel)) {
        Log("UBERS: in town (level %d) - rejecting Pandemonium recipe\n", curLevel);
        ShowNotify("Cannot use Pandemonium recipe in town - leave town first");
        return 0;
    }

    /* Spawn pattern: 3 ubers in a triangle ~5 tiles ahead of player */
    static const int SPAWN_OFFSETS[3][2] = {
        { 6,  0},
        {-3,  4},
        {-3, -4},
    };

    if (is3keys) {
        int monIds[3] = { UBER_MONID_ANDARIEL, UBER_MONID_DURIEL, UBER_MONID_IZUAL };
        Log("UBERS: 3-keys recipe in level %d - spawning mini ubers (staggered)\n", curLevel);
        /* STAGGERED (see the pending-queue block above): first boss spawns NOW — proves placement works before the cube eats the keys — the other two follow ~700/1400 ms later from the game tick. */
        {
            BOOL first = FALSE;
            for (int o = 0; o < 3 && !first; o++)
                first = Ubers_SpawnAt(pGame, (void*)pRoom,
                                      pX + SPAWN_OFFSETS[o][0],
                                      pY + SPAWN_OFFSETS[o][1],
                                      monIds[0], 1);
            if (!first) {
                /* Nothing placeable. Returning 1 anyway would let the cube EAT the 3 keys with nothing to show for it. Refuse. */
                Log("UBERS: 3-keys recipe REFUSED — no placement (blocked ground?)\n");
                ShowNotify("Uber spawn blocked - move to open ground and retry");
                return 0;
            }
            {
                DWORD nowT = GetTickCount();
                Ubers_QueueSpawn(monIds[1], 1, nowT + 700);
                Ubers_QueueSpawn(monIds[2], 1, nowT + 1400);
            }
        }
        ShowNotify("Pandemonium Mini Ubers spawned!");
        return 1;
    }

    if (is3organs) {
        int monIds[3] = { UBER_MONID_MEPHISTO, UBER_MONID_DIABLO, UBER_MONID_BAAL };
        /* Queued-but-not-yet-spawned finale bosses count as "in progress" too — u_finaleSpawnedCount only increments on actual spawn, so without this a second organ set could slip in during the stagger window. */
        BOOL pendingFinale = FALSE;
        for (int pq = 0; pq < UBER_PEND_MAX; pq++)
            if (u_pendQ[pq].active && u_pendQ[pq].set == 2) pendingFinale = TRUE;
        /* A finale round already in progress? */
        if (pendingFinale ||
            (u_finaleSpawnedCount > 0 && u_finaleKillCount < u_finaleSpawnedCount)) {
            Log("UBERS: 3-organs recipe REFUSED — finale round in progress (%d/%d dead)\n",
                u_finaleKillCount, u_finaleSpawnedCount);
            ShowNotify("Finish the current Uber trio first");
            return 0;
        }
        Log("UBERS: 3-organs recipe in level %d - spawning prime evil ubers (staggered)\n", curLevel);
        /* Same staggered pattern as the 3-keys recipe above. */
        {
            BOOL first = FALSE;
            for (int o = 0; o < 3 && !first; o++)
                first = Ubers_SpawnAt(pGame, (void*)pRoom,
                                      pX + SPAWN_OFFSETS[o][0],
                                      pY + SPAWN_OFFSETS[o][1],
                                      monIds[0], 2);
            if (!first) {
                Log("UBERS: 3-organs recipe REFUSED — no placement (blocked ground?)\n");
                ShowNotify("Uber spawn blocked - move to open ground and retry");
                return 0;
            }
            {
                DWORD nowT = GetTickCount();
                Ubers_QueueSpawn(monIds[1], 2, nowT + 700);
                Ubers_QueueSpawn(monIds[2], 2, nowT + 1400);
            }
        }
        ShowNotify("Uber Tristram trio spawned! Hellfire Torch awaits.");
        return 1;
    }

    return 0;
}

/* Patch the cube-output dispatch table entry [1] (Cow Portal). */
static void Ubers_InstallHook(void) {
    if (u_cowHookInstalled) return;
    HMODULE hG = GetModuleHandleA("D2Game.dll");
    if (!hG) {
        Log("UBERS: D2Game.dll not loaded yet, skipping hook install\n");
        return;
    }

    DWORD expectedCowPortal = (DWORD)hG + 0x60000;
    DWORD* table = (DWORD*)((DWORD)hG + 0xF9340);  /* dword_6FD2933C[1] */

    DWORD currentEntry = 0;
    __try { currentEntry = *table; }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("UBERS: cannot read cube dispatch table at %p\n", table);
        return;
    }

    if (currentEntry != expectedCowPortal) {
        Log("UBERS: dispatch table mismatch at %p: got %08X, expected %08X. "
            "Hook NOT installed - cube recipes will use vanilla logic.\n",
            table, currentEntry, expectedCowPortal);
        return;
    }

    u_origCreateCowPortal = (CreateCowPortal_t)currentEntry;
    u_cubeDispatchTable = table;

    DWORD oldProt;
    if (!VirtualProtect(table, sizeof(DWORD), PAGE_READWRITE, &oldProt)) {
        Log("UBERS: VirtualProtect on dispatch table failed (err=%d)\n",
            (int)GetLastError());
        return;
    }
    *table = (DWORD)UberCowPortalHook;
    DWORD tmp;
    VirtualProtect(table, sizeof(DWORD), oldProt, &tmp);

    u_cowHookInstalled = TRUE;
    Log("UBERS: dispatch table patched at %p: %08X -> UberCowPortalHook=%p "
        "(orig CreateCowPortal=%08X saved for fall-through)\n",
        table, currentEntry, UberCowPortalHook, currentEntry);
}
