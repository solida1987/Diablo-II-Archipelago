static D2DebugGame_t g_origD2DebugGame = NULL;
static BYTE  g_debugGameTrampoline[32] = {0};  /* big enough for any prologue */

/* Forward declarations — Treasure Cow module is included AFTER this file so ProcessPendingGameTick can call it. */
static void TreasureCow_Tick(void* pGame);
static void TreasureCow_Reset(void);
static void ObjTraps_Tick(void);   /* d2arch_objtraps.c, included after this file */

/* quest-type toggle check lives in d2arch_questlog.c (included later in the TU). */
static BOOL IsQuestTypeActive(int questType);
/* Defined further down; the forced-check drain in the tick needs it earlier. */
static void OnQuestComplete(Quest* quest);

/* grant reward gold while honouring D2's carried-gold cap (level*10000). */
static void GrantGoldCapped(void* pPlayer, int amount) {
    int level = 1, cur = 0, cap, headroom, toCarry, overflow;
    if (!pPlayer || !fnAddStat || amount <= 0) return;
    if (fnGetStat) {
        __try { level = fnGetStat(pPlayer, 12 /* STAT_LEVEL */, 0); } __except(EXCEPTION_EXECUTE_HANDLER) { level = 1; }
        __try { cur   = fnGetStat(pPlayer, STAT_GOLD, 0); }        __except(EXCEPTION_EXECUTE_HANDLER) { cur   = 0; }
    }
    if (level < 1)  level = 1;
    if (level > 99) level = 99;
    if (cur   < 0)  cur   = 0;
    cap = level * 10000;
    headroom = cap - cur;
    if (headroom < 0) headroom = 0;
    toCarry  = (amount < headroom) ? amount : headroom;
    overflow = amount - toCarry;
    __try {
        if (toCarry  > 0) fnAddStat(pPlayer, STAT_GOLD,     toCarry,  0);
        if (overflow > 0) fnAddStat(pPlayer, STAT_GOLDBANK, overflow, 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (overflow > 0)
        Log("GrantGoldCapped: +%d gold (carried +%d, bank +%d; cap=%d cur=%d)\n",
            amount, toCarry, overflow, cap, cur);
}

/* grant ledger points to a live stat WITHOUT exceeding its .d2s field cap (NEWSKILLS 8-bit=255, STATPTS 10-bit=1023). */
static void GrantLedgerCapped(void* pSrv, int statId, int cap, int* pLedger) {
    int cur = 0, room, give;
    if (!pSrv || !fnAddStat || !pLedger || *pLedger <= 0) return;
    if (fnGetStat) {
        __try { cur = fnGetStat(pSrv, statId, 0); } __except(EXCEPTION_EXECUTE_HANDLER) { cur = 0; }
    }
    if (cur < 0) cur = 0;
    room = cap - cur;
    if (room <= 0) return;
    give = (*pLedger < room) ? *pLedger : room;
    __try { fnAddStat(pSrv, statId, give, 0); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    *pLedger -= give;
    Log("GrantLedgerCapped: stat %d +%d (cur=%d cap=%d, ledger left=%d)\n",
        statId, give, cur, cap, *pLedger);
    { extern void MarkStateDirty(void); MarkStateDirty(); }
}

/* Monster Revive Trap (2.x) — opt-in "monster ambush" trap. */
int g_monsterTrap        = 0;     /* feature on/off */
int g_monsterTrapChance  = 3;     /* % chance per qualifying kill */
int g_monsterTrapQuota   = 15;    /* max trapped kills per game */
int g_monsterTrapCount   = 8;     /* hostile copies spawned per trigger */
int g_monsterTrapDelayMs = 1000;  /* ambush delay (ms) */
int g_monTrapRemaining   = 0;     /* runtime quota left (reset on settings load) */

/* 2.9.9 dev tool — ITEM DELIVERY STRESS TEST (Ctrl+V -> "Item Delivery Test"). */
static int   g_dtActive    = 0;   /* test in progress */
static int   g_dtPhase     = 0;   /* 0 = single spec-drops, 1 = boss loot (multi-item) */
static int   g_dtRemaining = 0;   /* spec-drops left to queue (phase 0) */
static int   g_dtIndex     = 0;   /* cycles charm/set/unique */
static int   g_dtBossIdx   = 0;   /* which boss to trigger next (phase 1) */
static int   g_dtBossItems = 0;   /* total items dropped by boss loot (set by the drain) */
static DWORD g_dtNextMs    = 0;   /* throttle */
static int   g_dtQCharm = 0, g_dtQSet = 0, g_dtQUniq = 0;  /* queued counts */
static int   g_dtSpInv  = 0, g_dtSpGround = 0;             /* spawned counts (set by the drain) */

static void DeliveryTestLog(const char* msg) {
    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    _snprintf(path, MAX_PATH, "%sd2arch_delivery_test.txt", dir);
    FILE* f = fopen(path, "a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d] %s", st.wHour, st.wMinute, st.wSecond, msg);
    fclose(f);
}

#define MONTRAP_MAX_PENDING 16
/* src: 0 = Monster Revive Trap roll, 1 = a delivered "Trap: Monsters" item.
 * The drain used to announce EVERY ambush as "The slain rise against you!" —
 * so a reward-trap ambush read as the revive trap firing while its toggle was
 * OFF (Yllarius' report; the toggle plumbing itself was verified fine). */
typedef struct { DWORD pRoom; int x, y, monId, count, spawnedSoFar; DWORD dueTick; BOOL active; BYTE src; } MonTrapPending;
static MonTrapPending g_monTrapQueue[MONTRAP_MAX_PENDING];
static int g_monTrapLastArea = -1;
/* 2.x — last NORMAL monster killed + the area it died in, so trap spawns use a REAL client-loaded monster instead of a guessed area-formula id (the formula could pick a monster whose graphics aren't loaded -> D2Client ptGfxInfo crash). */
static int g_lastKillMonId = 0;
static int g_lastKillArea  = -1;

/* Trap-spawned unit ids — never allowed to re-trigger the trap (no chaining). */
#define MONTRAP_EXCLUDE_RING 256
static DWORD g_monTrapSpawnedIds[MONTRAP_EXCLUDE_RING];
static int   g_monTrapSpawnedHead = 0;
static BOOL MonTrap_IsSpawned(DWORD unitId) {
    if (!unitId) return FALSE;
    for (int i = 0; i < MONTRAP_EXCLUDE_RING; i++)
        if (g_monTrapSpawnedIds[i] == unitId) return TRUE;
    return FALSE;
}
static void MonTrap_RememberSpawned(DWORD unitId) {
    if (!unitId) return;
    g_monTrapSpawnedIds[g_monTrapSpawnedHead] = unitId;
    g_monTrapSpawnedHead = (g_monTrapSpawnedHead + 1) % MONTRAP_EXCLUDE_RING;
}

/* 1.9.0 Phase 2: forward decl for the uber death-scan callback. */
extern void Ubers_OnUnitDeathScan(void* pGame, void* pUnit, DWORD txtId, DWORD unitId);

/* SINGLE source of truth for "which game is live RIGHT NOW". */
static DWORD SyncCachedPGame(void) {
    if (hD2Game) {
        __try {
            DWORD live = *(DWORD*)((DWORD)hD2Game + 0x1157FC);
            g_cachedPGame = live;     /* track the live game; 0 on menu/transition */
            return live;
        } __except(1) { /* transient — fall through to the last known value */ }
    }
    return g_cachedPGame;
}

/* Which LOADED room actually contains (x,y)? Player's room + its neighbour
 * list, bounds read from the room's CollMap (pRoom1+0x34 -> pos@0x00/0x04,
 * size@0x08/0x0C — the same offsets the census code already proved live).
 *
 * This exists because EVERY spawn call in this mod used to pass the PLAYER's
 * room together with coordinates that could lie outside it — and D2Common does
 * not return NULL on that mismatch, it ASSERTS (UnitRoom.cpp:44,
 * DungeonTestRoomGame). The cow's own 30-attempt retry loop never retried
 * once: the first bad draw killed the game. So the check has to happen BEFORE
 * the spawn call, and a point no loaded room contains is skipped, never
 * "handled". Returns 0 when (x,y) is nowhere. */
static DWORD Room_ForPoint(int x, int y) {
    __try {
        DWORD pClient = *(DWORD*)(g_cachedPGame + 0x88);
        if (pClient <= 0x10000) return 0;
        DWORD pUnit = *(DWORD*)(pClient + 0x174);
        if (pUnit <= 0x10000) return 0;
        DWORD pPath = *(DWORD*)(pUnit + 0x2C);
        if (!pPath) return 0;
        DWORD anchor = *(DWORD*)(pPath + 0x1C);
        if (!anchor) return 0;

        DWORD rooms[21];
        int roomCount = 0;
        rooms[roomCount++] = anchor;
        DWORD* ppNear = *(DWORD**)(anchor + 0x24);
        int nNear = *(int*)(anchor + 0x28);
        if (nNear > 20) nNear = 20;
        if (ppNear) {
            for (int r = 0; r < nNear && roomCount < 21; r++) {
                DWORD nr = ppNear[r];
                if (nr && nr != anchor) rooms[roomCount++] = nr;
            }
        }
        for (int ri = 0; ri < roomCount; ri++) {
            DWORD pColl = *(DWORD*)(rooms[ri] + 0x34);
            if (!pColl) continue;
            int posX  = *(int*)(pColl + 0x00);
            int posY  = *(int*)(pColl + 0x04);
            int sizeX = *(int*)(pColl + 0x08);
            int sizeY = *(int*)(pColl + 0x0C);
            if (x >= posX && x < posX + sizeX &&
                y >= posY && y < posY + sizeY)
                return rooms[ri];
        }
    } __except(1) {}
    return 0;
}

/* Get server-side player + room from pGame's unit list. */
static void* GetServerPlayer(DWORD pGame) {
    /* callers almost always pass g_cachedPGame, which can be STALE (the previous, freed game) during the new-game load window before the first tick re-syncs. */
    DWORD live = SyncCachedPGame();
    if (!live) return NULL;
    pGame = live;
    /* reliable game-active gate (the save&quit crash). */
    if (fnGetPlayer) {
        void* cp = NULL;
        __try { cp = fnGetPlayer(); } __except(EXCEPTION_EXECUTE_HANDLER) { cp = NULL; }
        if (!cp) return NULL;
    }
    __try {
        /* D2GameStrc->pUnitList is at offset 0x1120 (from D2MOO Game.h) pUnitList[5][128] — index 0 = players (UNIT_PLAYER remaps to 0) Each entry in pUnitList[0][0..127] is a D2UnitStrc* hash bucket, and each bucket is a linked list through +0xE8 (pRoomNext). */
        DWORD* pPlayerBuckets = (DWORD*)(pGame + 0x1120);

        /* return OUR player, not "the first one in the table". */
        DWORD myId = 0;
        {
            void* cp = Player();
            if (cp) myId = *(DWORD*)((DWORD)cp + 0x0C);
        }

        DWORD firstAny = 0;
        for (int i = 0; i < 128; i++) {
            DWORD pUnit = pPlayerBuckets[i];
            int chain = 0;
            while (pUnit && chain++ < 64) {
                if (!firstAny) firstAny = pUnit;
                if (myId && *(DWORD*)(pUnit + 0x0C) == myId)
                    return (void*)pUnit;
                pUnit = *(DWORD*)(pUnit + 0xE8);
            }
        }
        /* No id match (no client player yet during load, or an unexpected unit layout) — fall back to the old behaviour rather than returning NULL, so single player keeps working exactly as before. */
        if (firstAny) return (void*)firstAny;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("GetServerPlayer: exception reading pGame+0x1120\n");
    }
    return NULL;
}

/* Our wrapper — called instead of D2DebugGame. */
static int __cdecl HookD2DebugGame(void* pGame) {
    static int hookCallCount = 0;
    g_cachedPGame = (DWORD)pGame;
    hookCallCount++;

    /* Debug: log every 500th call + whenever pending > 0 */
    if (hookCallCount <= 3 || (hookCallCount % 500 == 0)) {
        Log("D2DebugGame hook #%d: pGame=%08X pending=%d fnSpawn=%08X\n",
            hookCallCount, (DWORD)pGame, g_pendingTrapSpawn, (DWORD)fnSpawnSuperUnique);
    }
    if (g_pendingTrapSpawn > 0) {
        Log("D2DebugGame: TRAP PENDING=%d fnSpawn=%08X pGame=%08X\n",
            g_pendingTrapSpawn, (DWORD)fnSpawnSuperUnique, (DWORD)pGame);
    }

    /* 1.8.0 cleanup: Treasure Cow name override comment block extracted */

    /* Note: CustomBoss_Tick() was originally here but moved to ProcessPendingGameTick below — HookD2DebugGame is unreliable on many user setups, while GAME_UpdateClients always fires. */

    /* Trap-spawn SuperUnique consumer REMOVED 2026-06-08 (1.9.13 audit fix): this hook used to ALSO consume g_pendingTrapSpawn here — spawning a level-tiered SuperUnique boss via fnSpawnSuperUnique with NO town check whatsoever — racing unsynchronized against ProcessPendingGameTick’s own consumer below (which spawns an 8-12 monster pack WITH proper town-deferral + a 5-minute watchdog, the 1.8.5 fix for "traps silently dropped during shopping/stash/ identify"). */

    /* 1.8.0 cleanup: Treasure Cow Levels.txt approach comment extracted */

    /* Cheat commands processed in ProcessPendingGameTick */

    /* Reward-apply consumer RELOCATED 2026-06-08 (1.9.13 audit fix) to ProcessPendingGameTick below — this was the ONLY code anywhere that ever turned g_pendingRewardGold/StatPts/SkillPts into an actual fnAddStat() grant, and it lived entirely inside this unreliable hook. */

    /* Zone Gating: g_pendingZoneTeleport is consumed SOLELY by ProcessPendingGameTick below (the GAME_UpdateClients hook) — not here. */

    /* Call original D2DebugGame */
    return ((D2DebugGame_t)(void*)g_debugGameTrampoline)(pGame);
}

/* 2.x SEED-LOG (Phase 4) — shared progression across characters in the SAME seed. */
/* resolve the per-seed key that isolates BOTH the one chest and the seed-ledger. */
static void ResolveSeedKey(const char* ini, char* key, size_t n) {
    char sk[40];
    key[0] = 0;
    GetPrivateProfileStringA("settings", "SeedKey", "", key, (DWORD)n, ini);
    if (!key[0]) {
        GetPrivateProfileStringA("settings", "ShuffleSeed", "0", sk, sizeof(sk), ini);
        if (sk[0] && strcmp(sk, "0") != 0) { strncpy(key, sk, n - 1); key[n - 1] = 0; }
    }
    if (!key[0]) {
        char slot[64], *p;
        GetPrivateProfileStringA("ap", "SlotName", "", slot, sizeof(slot), ini);
        for (p = slot; *p; p++) {
            char c = *p;
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
                *p = '_';
        }
        if (slot[0]) { strncpy(key, slot, n - 1); key[n - 1] = 0; }
    }
}

static void SeedLedger_BuildPath(char* out, size_t n) {
    char ini[MAX_PATH], dir[MAX_PATH], key[64];
    out[0] = 0;
    GetArchDir(dir, sizeof(dir));
    _snprintf(ini, sizeof(ini), "%sd2arch.ini", dir);
    ResolveSeedKey(ini, key, sizeof(key));
    if (!key[0]) return;
    _snprintf(out, n, "%sseed_ledger_%s.dat", dir, key);
    out[n - 1] = 0;
}

static void SeedLedger_Tick(void) {
    static DWORD s_last = 0;
    DWORD now = GetTickCount();
    if (now - s_last < 3000) return;   /* throttle to ~3s */
    s_last = now;

    if (!g_cachedPGame || !fnGetStat || !g_charName[0]) return;

    {   /* kill switch */
        char ini[MAX_PATH], dir[MAX_PATH], buf[8];
        GetArchDir(dir, sizeof(dir));
        _snprintf(ini, sizeof(ini), "%sd2arch.ini", dir);
        GetPrivateProfileStringA("settings", "SeedSharedProgression", "1", buf, sizeof(buf), ini);
        if (atoi(buf) == 0) return;
    }

    char path[MAX_PATH];
    SeedLedger_BuildPath(path, sizeof(path));
    if (!path[0]) return;   /* no seed key -> no shared ledger */

    void* pSrv = NULL;
    __try { pSrv = GetServerPlayer(g_cachedPGame); } __except(1) { return; }
    if (!pSrv) return;

    int charXp = 0, charLvl = 0;
    __try { charXp  = fnGetStat(pSrv, 13, 0); } __except(1) { return; }
    __try { charLvl = fnGetStat(pSrv, 12, 0); } __except(1) { charLvl = 0; }
    if (charXp < 0) return;   /* int32 overflow at very high levels — skip */

    int baseline = 0;
    { FILE* f = fopen(path, "r"); if (f) { if (fscanf(f, "%d", &baseline) != 1) baseline = 0; fclose(f); } }
    if (baseline < 0) baseline = 0;

    /* One-shot catch-up per character session. */
    static char s_lastChar[32] = {0};
    static BOOL s_caughtUp = FALSE;
    if (strncmp(s_lastChar, g_charName, 31) != 0) {
        strncpy(s_lastChar, g_charName, 31); s_lastChar[31] = 0;
        s_caughtUp = FALSE;
    }
    if (!s_caughtUp) {
        s_caughtUp = TRUE;
        /* One diagnostic line per character session — tells us, from the tester's d2arch_log.txt, exactly what the seed-log saw: the ledger file, this char's level/xp, and the seed baseline. */
        Log("SEED-LOG: char '%s' lvl=%d xp=%d | ledger='%s' baseline=%d\n",
            g_charName, charLvl, charXp, path, baseline);
        if (baseline > charXp && g_serverPendingXP == 0) {
            PendAdd(&g_serverPendingXP, baseline - charXp);
            Log("SEED-LOG: catch-up due — banking +%d XP (D2 applies the levels"
                " + stat/skill points on this char's NEXT kill)\n", baseline - charXp);
        } else if (baseline <= charXp) {
            Log("SEED-LOG: no catch-up — char is at/above the seed baseline\n");
        }
    }

    /* Write-back: raise the seed baseline when this character is ahead. */
    if (charXp > baseline) {
        FILE* f = fopen(path, "w");
        if (f) { fprintf(f, "%d", charXp); fclose(f); }
        static int s_loggedLvl = -1;
        if (charLvl != s_loggedLvl) {
            s_loggedLvl = charLvl;
            Log("SEED-LOG: raised seed baseline to %d (char '%s' lvl=%d is ahead)\n",
                charXp, g_charName, charLvl);
        }
    }
}

/* PET AI KICK — summons/mercs freeze after our warps LEVEL_WarpUnit DOES bring pets along (PlayerPets.cpp:786 iterates every pet list and teleports them) — the freeze is what happens to their AI. */
static int g_petKickPending = 0;   /* countdown to the delayed second kick */
static void PetAiKick(const char* why);

/* P11/M9 — after an entrance-shuffle warp lands at the level's default spot,
 * move the player to the warp tile whose Vis slot leads back to the level they
 * conceptually came from. Vanilla-verified: rooms are walked per D2MOO 1.10f
 * layout, activation is D2Common+0x33CF0, the mover is D2Game+0x8DFE0 (the
 * same pair LEVEL_ChangeAct itself uses). Prologue-guarded; any miss keeps the
 * engine's default landing. */
static void LandingNudge_Tick(void) {
    static int s_tries = 0;
    BOOL finish = FALSE;

    if (g_relocateNextTo <= 0) { s_tries = 0; return; }
    if (!g_cachedPGame || !hD2Game || !hD2Common) return;
    if (++s_tries > 30) finish = TRUE;

    __try {
        if (!finish) {
            typedef void* (__stdcall *GetRoom_t)(void*);
            typedef void  (__stdcall *T2S_t)(int*, int*);
            typedef void  (__fastcall *InitEx_t)(void*);
            typedef int   (__fastcall *Move_t)(void*, void*, void*, int, int, int, int);
            static GetRoom_t fnGetRoom;
            static T2S_t     fnT2S;
            static const BYTE initSig[6] = { 0x56,0x8B,0xF1,0xF7,0x46,0x14 };
            static const BYTE moveSig[7] = { 0x83,0xEC,0x30,0x53,0x55,0x56,0x57 };

            if (!fnGetRoom) fnGetRoom = (GetRoom_t)GetProcAddress(hD2Common, (LPCSTR)10342);
            if (!fnT2S)     fnT2S     = (T2S_t)GetProcAddress(hD2Common, (LPCSTR)10113);

            void* pUnit = GetServerPlayer(g_cachedPGame);
            DWORD pRoom = (pUnit && fnGetRoom) ? (DWORD)fnGetRoom(pUnit) : 0;
            DWORD pRoomEx = pRoom ? *(DWORD*)(pRoom + 0x38) : 0;
            DWORD pLevel  = pRoomEx ? *(DWORD*)(pRoomEx + 0x00) : 0;
            if (!fnT2S || !fnGetRoom) finish = TRUE;
            else if (pLevel && *(int*)(pLevel + 0x04) == g_relocateLevel) {
                finish = TRUE;
                int levelId = g_relocateLevel;
                int slot = -1;
                if (levelId > 0 && levelId < VIS_MAX_LEVEL)
                    for (int s = 0; s < 8; s++)
                        if (g_levelVis[levelId][s] == g_relocateNextTo) { slot = s; break; }

                BYTE* init = (BYTE*)hD2Common + 0x33CF0;
                BYTE* move = (BYTE*)hD2Game   + 0x8DFE0;
                if (slot >= 0 && memcmp(init, initSig, 6) == 0
                              && memcmp(move, moveSig, 7) == 0) {
                    DWORD flagBit = 0x10u << slot;
                    for (DWORD ex = *(DWORD*)(pLevel + 0x30); ex; ex = *(DWORD*)(ex + 0xE8)) {
                        if (!(*(DWORD*)(ex + 0x14) & flagBit)) continue;
                        int tx = *(int*)(ex + 0x04) + *(int*)(ex + 0x0C) / 2;
                        int ty = *(int*)(ex + 0x08) + *(int*)(ex + 0x10) / 2;
                        fnT2S(&tx, &ty);
                        tx += 3; ty += 3;
                        ((InitEx_t)init)((void*)ex);
                        DWORD pActive = *(DWORD*)(ex + 0xE4);
                        if (pActive) {
                            int ok = ((Move_t)move)((void*)g_cachedPGame, pUnit,
                                                    (void*)pActive, tx, ty, 0, 0);
                            Log("LANDING NUDGE: level=%d nextTo=%d slot=%d -> (%d,%d) ok=%d\n",
                                levelId, g_relocateNextTo, slot, tx, ty, ok);
                            if (ok) { PetAiKick("nudge"); g_petKickPending = 10; }
                        }
                        break;
                    }
                } else {
                    Log("LANDING NUDGE: no slot/sig for level=%d nextTo=%d -> default landing\n",
                        levelId, g_relocateNextTo);
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("LANDING NUDGE: exception, keeping default landing\n");
        finish = TRUE;
    }

    if (finish) { g_relocateNextTo = 0; g_relocateLevel = 0; s_tries = 0; }
}

static void __fastcall PetAiKickCallback(void* pGame, void* pPlayer,
                                         void* pPet, void* arg) {
    (void)pPlayer; (void)arg;
    __try {
        DWORD mode = *(DWORD*)((BYTE*)pPet + 0x10);   /* dwAnimMode */
        if (mode == 0 || mode == 12) return;          /* DEATH / DEAD */
        {
            typedef void (__fastcall *EventsDelete_t)(void*, void*, int, int);
            typedef void (__fastcall *EventSet_t)(void*, void*, int, int, int, int);
            EventsDelete_t fnDel = (EventsDelete_t)((DWORD)hD2Game + 0x4840);
            EventSet_t     fnSet = (EventSet_t)((DWORD)hD2Game + 0x51B0);
            fnDel(pGame, pPet, 2 /* EVENTTYPE_AITHINK */, 0);
            fnSet(pGame, pPet, 2, 0 /* clamped to next frame */, 0, 0);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void PetAiKick(const char* why) {
    __try {
        typedef void (__fastcall *IteratePets_t)(void*, void*,
            void (__fastcall*)(void*, void*, void*, void*), void*);
        IteratePets_t fnIter;
        DWORD pClient, pUnit;
        if (!g_cachedPGame || !hD2Game) return;
        pClient = *(DWORD*)(g_cachedPGame + 0x88);
        if (pClient <= 0x10000) return;
        pUnit = *(DWORD*)(pClient + 0x174);
        if (pUnit <= 0x10000) return;
        fnIter = (IteratePets_t)((DWORD)hD2Game + 0x4E7C0);
        fnIter((void*)g_cachedPGame, (void*)pUnit, PetAiKickCallback, NULL);
        Log("PET KICK: re-armed pet AI (%s)\n", why);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("PET KICK: exception (%s)\n", why);
    }
}

/* Hand over `amount` experience without ever letting the stat run past what
 * level 99 costs.
 *
 * Two things make this less trivial than one AddStat call. Level 99 needs
 * 3,520,485,254 XP, which does NOT fit in the signed int fnAddStat takes,
 * so a large grant goes over in pieces. And a character already near the
 * top must not be pushed past the cap, because the wrap lands on a negative
 * total — the character would appear to lose every level it had.
 *
 * Returns what was actually granted, which is zero at 99. */
static unsigned int Cheat_GiveExperience(void* pSC, unsigned int amount) {
    if (!pSC || !fnAddStat || !fnGetStat || amount == 0) return 0;

    unsigned int cur = 0;
    __try { cur = (unsigned int)fnGetStat(pSC, 13, 0); }
    __except(1) { return 0; }

    LoadExpTable();
    unsigned int cap = g_expThreshold[EXP_MAX_LEVEL];
    if (cap) {
        if (cur >= cap) {
            Log("CHEAT XP: already at the level 99 total (%u) — nothing added\n", cur);
            return 0;
        }
        if (cap - cur < amount) amount = cap - cur;
    }

    unsigned int left = amount;
    while (left > 0) {
        int chunk = (left > 1000000000u) ? 1000000000 : (int)left;
        __try { fnAddStat(pSC, 13, chunk, 0); }
        __except(1) { Log("CHEAT XP: AddStat(%d) exception\n", chunk); break; }
        left -= (unsigned int)chunk;
    }

    unsigned int given = amount - left;
    /* Cheat XP is not multiplied by the XP-rate setting. */
    g_rewardXpExclude += (int)(given > 2000000000u ? 2000000000u : given);
    Log("CHEAT XP: %u granted (was %u, cap %u)\n", given, cur, cap);
    return given;
}

/* Add the experience that actually separates the character from level+n. */
static void Cheat_GiveLevels(void* pSC, int levels) {
    if (!pSC || !fnGetStat || levels <= 0) return;

    int lvl = 1;
    __try { lvl = fnGetStat(pSC, 12, 0); } __except(1) { lvl = 1; }
    if (lvl < 1) lvl = 1;

    LoadExpTable();
    int target = lvl + levels;
    if (target > EXP_MAX_LEVEL) target = EXP_MAX_LEVEL;

    unsigned int need = g_expThreshold[target];
    unsigned int cur = 0;
    __try { cur = (unsigned int)fnGetStat(pSC, 13, 0); } __except(1) { cur = 0; }

    if (!need || need <= cur) {
        Log("CHEAT LEVELS: already at or past level %d — nothing added\n", target);
        return;
    }
    Log("CHEAT LEVELS: level %d -> %d\n", lvl, target);
    Cheat_GiveExperience(pSC, need - cur);
}

static void ProcessPendingGameTick(void) {
    g_pendingGold = 0;

    /* 2.x seed-log: keep this character in step with its seed's progression baseline (self-throttled, one-shot catch-up + write-back). */
    SeedLedger_Tick();

    /* install the Logbook event hooks once D2Game.dll is loaded. */
    {
        HMODULE hG = GetModuleHandleA("D2Game.dll");
        if (hG) {
            extern void Hooks_InstallLogbookHooks(HMODULE hD2Game);
            Hooks_InstallLogbookHooks(hG);
        }
    }

    /* Get pGame from D2Game's global gpGame_6FD457FC (base + 0x1157FC). */
    /* RE-SYNC the cached game pointer from D2's LIVE gpGame EVERY tick (was: only when g_cachedPGame==0). */
    if (hD2Game) {
        __try {
            DWORD live = *(DWORD*)((DWORD)hD2Game + 0x1157FC);
            if (live != g_cachedPGame) {
                g_cachedPGame = live;
                if (live) Log("pGame re-synced from D2Game global: %08X\n", live);
            }
        } __except(1) {}
    }

    /* 1.8.0 NEW: Custom Boss framework — moved here from HookD2DebugGame because GAME_UpdateClients fires reliably on all user setups whereas D2DebugGame hook is environment-dependent. */
    if (g_cachedPGame) {
        CustomBoss_Tick((void*)g_cachedPGame);
        TreasureCow_Tick((void*)g_cachedPGame);
        /* Rift/Reset hooks removed 2026-05-05 — see Research/RIFT_AND_RESET_FAILURE_2026-05-05.md */

        /* pending DeathLink kill. */
        if (g_pendingDeathLinkKill > 0) {
            g_pendingDeathLinkKill = 0;
            extern BOOL DeathLink_KillLocalPlayer(void);
            if (!DeathLink_KillLocalPlayer()) {
                Log("DEATHLINK: kill unavailable on tick — spawning trap instead\n");
                g_pendingTrapSpawn++;
            }
        }

        /* forced checks from the launcher's "Cheat" button in standalone. */
        {
            int fq, fd;
            while (ApPipe_PopForceCheck(&fq, &fd)) {
                if (fd != g_currentDifficulty) {
                    Log("FORCECHECK: qid=%d is for diff %d, player is on %d — ignored\n",
                        fq, fd, g_currentDifficulty);
                    continue;
                }
                Quest* fqp = NULL;
                for (int fa = 0; fa < 5 && !fqp; fa++)
                    for (int fi = 0; fi < g_acts[fa].num; fi++)
                        if (g_acts[fa].quests[fi].id == fq) {
                            fqp = &g_acts[fa].quests[fi];
                            break;
                        }
                if (!fqp) {
                    Log("FORCECHECK: no quest with id=%d\n", fq);
                    continue;
                }
                Log("FORCECHECK: completing qid=%d (%s) on diff %d\n",
                    fq, fqp->name, fd);
                OnQuestComplete(fqp);
            }
        }
    }

    /* 1.9.13 audit fix (2026-06-08) — Reward-apply consumer relocated here from HookD2DebugGame: same migration reason as CustomBoss_Tick/ TreasureCow_Tick just above (D2DebugGame is environment-dependent; this hook always fires). */
    if (g_cachedPGame) {
        /* 2.x GOLD/POINTS DUPLICATION FIX — deliver each queued reward ONCE then zero it, exactly like g_serverPendingGold above. */
        void* pPlayer = GetServerPlayer(g_cachedPGame);
        if (pPlayer && fnAddStat &&
            (g_pendingRewardGold > 0 || g_pendingRewardStatPts > 0 || g_pendingRewardSkillPts > 0)) {
            __try {
                if (g_pendingRewardGold > 0) {
                    int g = g_pendingRewardGold; g_pendingRewardGold = 0;
                    GrantGoldCapped(pPlayer, g);   /* overflow -> gold bank */
                    Log("REWARD APPLY: +%d gold (pending zeroed)\n", g);
                }
                if (g_pendingRewardStatPts > 0) {
                    /* via capped ledger so the live stat never exceeds its .d2s field cap (see GrantLedgerCapped). */
                    int s = g_pendingRewardStatPts; g_pendingRewardStatPts = 0;
                    g_statPtsLedger += s;
                    GrantLedgerCapped(pPlayer, STAT_STATPTS, 1023, &g_statPtsLedger);
                    Log("REWARD APPLY: +%d stat points via ledger (pending zeroed)\n", s);
                }
                if (g_pendingRewardSkillPts > 0) {
                    int k = g_pendingRewardSkillPts; g_pendingRewardSkillPts = 0;
                    g_skillPtsLedger += k;
                    GrantLedgerCapped(pPlayer, STAT_NEWSKILLS, 255, &g_skillPtsLedger);
                    Log("REWARD APPLY: +%d skill points via ledger (pending zeroed)\n", k);
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                static int addStatErr = 0;
                if (addStatErr++ < 3)
                    Log("Live reward apply exception\n");
            }
        }
    }

    /* 2.x — combined-mode key conditions (zone-lock + entrance-shuffle). */
    { extern void KeyCond_Tick(void); KeyCond_Tick(); }

    /* periodic check re-send sweep. */
    {
        static DWORD s_lastCheckSweep = 0;
        DWORD nowSweep = GetTickCount();
        if (nowSweep - s_lastCheckSweep > 8000) {
            s_lastCheckSweep = nowSweep;
            extern void ResendChecksToLauncher(void);
            ResendChecksToLauncher();
        }
    }

    /* C10/U7 — periodic stats save. */
    if (g_charName[0]) {
        static DWORD s_lastStatsSave = 0;
        DWORD nowStats = GetTickCount();
        if (nowStats - s_lastStatsSave > 60000) {
            s_lastStatsSave = nowStats;
            extern void Stats_SaveForCharacter(const char* charName);
            extern void Stats_SaveLifetime(void);
            __try {
                Stats_SaveForCharacter(g_charName);
                Stats_SaveLifetime();
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    /* 1.9.0 NEW: F1 Collection page tick — scans player inventory and polls gold delta. */
    {
        extern void* (__stdcall *fnGetPlayer)(void);
        extern void Coll_OnGameTick(void* pPlayerUnit);
        extern void Stats_OnGameTick(void* pPlayerUnit);
        extern void Extra_PollMerc(void* pPlayer);  /* 1.9.2 Cat 2 */
        extern void Extra_PollNpcDialogue(void* pPlayer); /* 1.9.2 Cat 4 */
        extern void CustomGoal_PollBulkTargets(void); /* 1.9.2 custom goal */
        if (fnGetPlayer) {
            void* pCliPlayer = NULL;
            __try { pCliPlayer = fnGetPlayer(); } __except(1) {}
            if (pCliPlayer) {
                Coll_OnGameTick(pCliPlayer);
                Stats_OnGameTick(pCliPlayer);  /* playtime + death-edge */
                Extra_PollMerc(pCliPlayer);    /* merc hire/resurrect/level */
                Extra_PollNpcDialogue(pCliPlayer); /* NPC near + stationary */
                CustomGoal_PollBulkTargets();  /* custom goal bulk completions */
                /* Map tracker — stream the player's position to the launcher for the live "you are here" dot. */
                extern void SendPlayerPosToLauncher(void* pPlayerUnit);
                SendPlayerPosToLauncher(pCliPlayer);
            }
        }
    }

    /* REINVEST: re-apply saved skill points from game tick context. */
    if (g_reinvestPending && g_cachedPGame) {
        /* no 30s forced-respec timeout. */

        /* Re-read pGame fresh */
        __try {
            DWORD* ppG2 = (DWORD*)((DWORD)hD2Game + 0x1157FC);
            if (*ppG2) g_cachedPGame = *ppG2;
        } __except(1) {}
        void* pSrvR = GetServerPlayer(g_cachedPGame);
        void* pCliR = Player();

        /* gate on the player being fully resolved (both server and client units live) for a few consecutive ticks, instead of waiting for a key/mouse press. */
        static int s_reinvestReadyTicks = 0;
        if (!(pSrvR && pCliR)) { s_reinvestReadyTicks = 0; goto skipReinvest; }
        if (++s_reinvestReadyTicks < 5) goto skipReinvest;

        if (pSrvR && pCliR) {
            /* 1.7.1 ATOMIC CONSUME: rename reinvest file to `.consuming` so it's hidden from OnCharacterLoad's rehydrate path but survives a crash mid-apply. */
            char consumingPath[MAX_PATH];
            if (!BeginReinvestConsume(consumingPath, sizeof(consumingPath))) {
                /* File missing or rename failed — skip this tick and retry next. */
                Log("REINVEST: BeginReinvestConsume failed, skipping tick\n");
                goto skipReinvest;
            }
            typedef void* (__stdcall *AddSkill_t)(void* pUnit, int nSkillId);
            static AddSkill_t fnAddSkillR = NULL;
            if (!fnAddSkillR && hD2Common) {
                fnAddSkillR = (AddSkill_t)GetProcAddress(hD2Common, (LPCSTR)10952);
            }
            /* 1.8.0 CRITICAL: apply Skills.txt patches (maxlvl 20->99, cross-class animations, weapon restrictions) BEFORE reinvest fires. */
            PatchAllSkillAnimations();

            /* top the live stats up from the overflow ledgers BEFORE the reinvest loop drains NEWSKILLS, so the pool starts at its true value and the per-point decrements below never floor. */
            GrantLedgerCapped(pSrvR, 5, 255,  &g_skillPtsLedger);
            GrantLedgerCapped(pSrvR, 4, 1023, &g_statPtsLedger);

            Log("REINVEST (game tick): %d skills, srv=%08X cli=%08X\n",
                g_reinvestCount, (DWORD)pSrvR, (DWORD)pCliR);
            for (int ri = 0; ri < g_reinvestCount; ri++) {
                int skId = g_reinvestSkills[ri];
                int skPts = g_reinvestPoints[ri];
                __try { PatchSkillForPlayer(skId); }
                __except(1) { Log("REINVEST: PatchSkillForPlayer(%d) exception\n", skId); }
                __try { InsertSkillInClassList(skId); }
                __except(1) { Log("REINVEST: InsertSkillInClassList(%d) exception\n", skId); }
                for (int rp = 0; rp < skPts; rp++) {
                    if (fnAddSkillR) {
                        __try { fnAddSkillR(pSrvR, skId); }
                        __except(1) { Log("REINVEST: AddSkill(srv,%d) exception at rp=%d\n", skId, rp); }
                        __try { fnAddSkillR(pCliR, skId); }
                        __except(1) { Log("REINVEST: AddSkill(cli,%d) exception at rp=%d\n", skId, rp); }
                    }
                    if (fnAddStat) {
                        /* floor at 0. Decrementing NEWSKILLS below zero wraps the 8-bit field to a huge value (Merlyn's "negative skillpoints"). With the ledger restored above the pool should never run dry here, but guard anyway. */
                        int curNS = 1;
                        if (fnGetStat) { __try { curNS = fnGetStat(pSrvR, 5, 0); } __except(1) { curNS = 1; } }
                        if (curNS > 0) {
                            __try { fnAddStat(pSrvR, 5, -1, 0); }
                            __except(1) { Log("REINVEST: AddStat(NEWSKILLS,-1) exception\n"); }
                        }
                    }
                }
                Log("REINVEST: skill %d = %d pts (btnIdx=%d)\n",
                    skId, skPts, g_reinvestBtnIdx[ri]);
                /* 1.8.2 FIX: write to the per-button file matching the ORIGINAL btnIdx (= tab*10+slot) the user invested at — NOT the compact reinvest array index `ri`. */
                {
                    int btnIdx = g_reinvestBtnIdx[ri];
                    if (btnIdx < 0 || btnIdx >= 30) btnIdx = ri; /* defensive */
                    /* atomic write via tmp+rename. */
                    char rsp[MAX_PATH], rspTmp[MAX_PATH], rsfx[32];
                    GetCharFileDir(rsp, MAX_PATH);
                    if (btnIdx == 0) strcat(rsp, "d2arch_fireball_");
                    else { sprintf(rsfx, "d2arch_skill%d_", btnIdx + 1); strcat(rsp, rsfx); }
                    strcat(rsp, g_charName); strcat(rsp, ".dat");
                    snprintf(rspTmp, sizeof(rspTmp), "%s.tmp", rsp);
                    FILE* rsf = fopen(rspTmp, "w");
                    if (rsf) {
                        fprintf(rsf, "%d", skPts);
                        fclose(rsf);
                        MoveFileExA(rspTmp, rsp, MOVEFILE_REPLACE_EXISTING);
                    }
                }
            }
            g_reinvestPending = FALSE;
            g_reinvestCount = 0;
            g_reinvestTime = 0;
            g_reinvestDone = TRUE; /* trigger skill tree panel to reload levels */
            /* atomic cleanup — delete the .consuming file now that all points have been applied successfully. */
            EndReinvestConsume(consumingPath);
        }
    }
    skipReinvest:

    /* ledger grant INDEPENDENT of reinvest. */
    if (g_cachedPGame && (g_skillPtsLedger > 0 || g_statPtsLedger > 0)) {
        void* pSrvL = GetServerPlayer(g_cachedPGame);
        void* pCliL = Player();
        static int s_ledgerReadyTicks = 0;
        if (!(pSrvL && pCliL)) {
            s_ledgerReadyTicks = 0;
        } else if (++s_ledgerReadyTicks >= 5) {
            GrantLedgerCapped(pSrvL, 5, 255,  &g_skillPtsLedger);
            GrantLedgerCapped(pSrvL, 4, 1023, &g_statPtsLedger);
        }
    }

    /* Consume server-side pending rewards — give directly to server player */
    if (g_cachedPGame && fnAddStat &&
        (g_serverPendingGold > 0 || g_serverPendingStatPts > 0 || g_serverPendingSkillPts > 0 ||
         g_serverPendingXP > 0 || g_serverPendingXpLevels > 0 || g_serverPendingGoldLevels > 0)) {
        void* pSrvReward = GetServerPlayer(g_cachedPGame);
        if (pSrvReward) {
            BOOL anyConsumed = FALSE;
            __try {
                /* level-scaled rewards. */
                if (g_serverPendingXpLevels > 0) {
                    int grants = PendTake(&g_serverPendingXpLevels);
                    for (int gi = 0; gi < grants; gi++) {
                        int lvl = 0;
                        __try { lvl = fnGetStat(pSrvReward, 12, 0); } __except(1) { lvl = 0; }
                        int xp = ExpForNextLevel(lvl > 0 ? lvl : 1);
                        if (xp <= 0) {          /* already level 99 */
                            Log("XP REWARD: character at level cap, granting gold instead\n");
                            GrantGoldCapped(pSrvReward, GoldForLevel(EXP_MAX_LEVEL));
                            continue;
                        }
                        fnAddStat(pSrvReward, 13, xp, 0);   /* STAT_EXPERIENCE */
                        g_rewardXpExclude += xp;  /* flat reward — the XP multiplier must not inflate it */
                        Log("XP REWARD: level %d -> +%d XP (one level)\n", lvl, xp);
                    }
                    anyConsumed = TRUE;
                }
                if (g_serverPendingGoldLevels > 0) {
                    int grants = PendTake(&g_serverPendingGoldLevels);
                    int lvl = 0;
                    __try { lvl = fnGetStat(pSrvReward, 12, 0); } __except(1) { lvl = 0; }
                    int per = GoldForLevel(lvl > 0 ? lvl : 1);
                    for (int gi = 0; gi < grants; gi++)
                        GrantGoldCapped(pSrvReward, per);   /* overflow -> gold bank */
                    /* Announce HERE, not at receive time: this is the only place
                     * the amount exists (the receive side has no readable player
                     * level and used to print a hardcoded 0). */
                    {
                        char gmsg[64];
                        _snprintf(gmsg, sizeof(gmsg), "AP: %d Gold!", per * grants);
                        gmsg[sizeof(gmsg) - 1] = 0;
                        ShowNotify(gmsg);
                    }
                    Log("GOLD REWARD: level %d -> %d x %d gold\n", lvl, grants, per);
                    anyConsumed = TRUE;
                }
                if (g_serverPendingXP > 0) {
                    /* 2.x seed-log: bank catch-up experience onto the char's STAT_EXPERIENCE. */
                    int xp = PendTake(&g_serverPendingXP);
                    fnAddStat(pSrvReward, 13, xp, 0); /* STAT_EXPERIENCE */
                    g_rewardXpExclude += xp;  /* flat catch-up XP — XP multiplier must not inflate it */
                    Log("SEED-LOG: banked +%d catch-up XP (levels apply on next kill)\n", xp);
                    anyConsumed = TRUE;
                }
                if (g_serverPendingGold > 0) {
                    int g = PendTake(&g_serverPendingGold);
                    GrantGoldCapped(pSrvReward, g);   /* overflow -> gold bank */
                    Log("SERVER REWARD: +%d gold\n", g);
                    anyConsumed = TRUE;
                }
                if (g_serverPendingStatPts > 0) {
                    /* route through the ledger: a raw grant above the 10-bit STATPTS field cap would wrap in the vanilla save before our re-encode could clamp it. */
                    int s = PendTake(&g_serverPendingStatPts);
                    g_statPtsLedger += s;
                    GrantLedgerCapped(pSrvReward, 4, 1023, &g_statPtsLedger);
                    Log("SERVER REWARD: +%d stat points (via capped ledger)\n", s);
                    anyConsumed = TRUE;
                }
                if (g_serverPendingSkillPts > 0) {
                    /* same, NEWSKILLS 8-bit cap 255. */
                    int k = PendTake(&g_serverPendingSkillPts);
                    g_skillPtsLedger += k;
                    GrantLedgerCapped(pSrvReward, 5, 255, &g_skillPtsLedger);
                    Log("SERVER REWARD: +%d skill points (via capped ledger)\n", k);
                    anyConsumed = TRUE;
                }
            } __except(1) { Log("SERVER REWARD: exception\n"); }
            /* mark state dirty so the zero'd pending counters persist within 250 ms. */
            if (anyConsumed) {
                extern void MarkStateDirty(void);
                MarkStateDirty();
            }
        }
    }

    /* XP Multiplier: detect XP gain from kills and add bonus. */
    if (g_xpMultiplier > 0 && g_cachedPGame && fnAddStat && fnGetStat) {
        static int s_lastXP = 0;
        static BOOL s_justGaveBonus = FALSE;
        static void* s_lastXPUnit = NULL;   /* baseline owner */
        void* pXP = GetServerPlayer(g_cachedPGame);
        if (pXP) {
            /* the baseline belongs to ONE character. */
            /* Unit-pointer identity alone is not enough: D2 pools/reuses unit allocations, so a new char can land at the old address. */
            static char s_lastXPChar[32] = {0};
            if (pXP != s_lastXPUnit || strncmp(s_lastXPChar, g_charName, 31) != 0) {
                s_lastXPUnit = pXP;
                strncpy(s_lastXPChar, g_charName, 31); s_lastXPChar[31] = 0;
                s_lastXP = 0;              /* re-baselined below, no bonus this tick */
                s_justGaveBonus = FALSE;
                g_rewardXpExclude = 0;     /* stale exclusions belong to the old char */
            }
            int curXP = 0;
            __try { curXP = fnGetStat(pXP, 13, 0); } __except(1) {}
            if (s_lastXP > 0 && curXP > s_lastXP) {
                if (s_justGaveBonus) {
                    /* This delta is from our own bonus - skip it */
                    s_justGaveBonus = FALSE;
                } else {
                    int delta = curXP - s_lastXP;
                    /* 2.x — never multiply flat XP REWARDS (quest/bonus/AP XP reward). */
                    if (g_rewardXpExclude > 0 && delta > 0) {
                        int excl = (g_rewardXpExclude < delta) ? g_rewardXpExclude : delta;
                        delta -= excl;
                        g_rewardXpExclude -= excl;
                    }
                    /* 2.x — g_xpMultiplier is now 0-BASED: 0 = standard (no bonus), each +1 adds +100% (one extra copy of the kill's XP). */
                    __int64 bonus64 = (__int64)delta * (__int64)g_xpMultiplier;
                    if (bonus64 < 0) bonus64 = 0;
                    if (bonus64 > 0x7FFFFFFFLL) bonus64 = 0x7FFFFFFFLL;
                    int bonus = (int)bonus64;
                    if (bonus > 0) {
                        __try { fnAddStat(pXP, 13, bonus, 0); }
                        __except(1) { Log("XP_MULT: fnAddStat exception (bonus=%d, mult=%d)\n", bonus, g_xpMultiplier); }
                        s_justGaveBonus = TRUE;
                    }
                }
            }
            s_lastXP = curXP;
        }
    }

    /* TRAP SPAWN: spawn 8-12 area-matching monsters near player. */
    {
        static DWORD s_trapTownLastLog = 0;
        static DWORD s_trapTownFirstQueue = 0;

        if (g_pendingTrapSpawn > 0 && g_cachedPGame && fnSpawnMonster) {
            int curArea = GetCurrentArea();
            if (IsTown((DWORD)curArea)) {
                if (s_trapTownFirstQueue == 0)
                    s_trapTownFirstQueue = GetTickCount();
                DWORD now = GetTickCount();
                if (now - s_trapTownLastLog > 10000) {
                    Log("TRAP_MONSTERS: deferred — player in town area %d "
                        "(pending=%d, queued for %lus)\n",
                        curArea, g_pendingTrapSpawn,
                        (now - s_trapTownFirstQueue) / 1000);
                    s_trapTownLastLog = now;
                }
                if (now - s_trapTownFirstQueue > 300000) {
                    g_pendingTrapSpawn--;
                    s_trapTownFirstQueue = now;
                    Log("TRAP_MONSTERS: 5-min watchdog dropped one stale "
                        "trap (pending=%d)\n", g_pendingTrapSpawn);
                }
            } else {
                /* Player left town — reset watchdog so next town visit gets a fresh 5-min grace window for any new pending traps. */
                s_trapTownFirstQueue = 0;
                s_trapTownLastLog = 0;

                void* pTrap = GetServerPlayer(g_cachedPGame);
                if (pTrap) {
                    __try {
                        DWORD pPath = *(DWORD*)((DWORD)pTrap + 0x2C);
                        if (pPath) {
                            DWORD pRoom = *(DWORD*)(pPath + 0x1C);
                            int nX = (int)*(unsigned short*)(pPath + 0x02);
                            int nY = (int)*(unsigned short*)(pPath + 0x06);
                            if (pRoom && nX > 0 && nY > 0) {
                                /* 2.x — use the LAST normal monster killed in THIS area (a real, client-loaded type) instead of a guessed area-formula id, and route through the STAGGERED queue (1-2/tick). */
                                int area = GetCurrentArea();
                                if (g_lastKillArea == area && g_lastKillMonId > 0) {
                                    for (int qi = 0; qi < MONTRAP_MAX_PENDING; qi++) {
                                        if (!g_monTrapQueue[qi].active) {
                                            g_monTrapQueue[qi].active       = TRUE;
                                            g_monTrapQueue[qi].pRoom        = pRoom;
                                            g_monTrapQueue[qi].x            = nX;
                                            g_monTrapQueue[qi].y            = nY;
                                            g_monTrapQueue[qi].monId        = g_lastKillMonId;
                                            g_monTrapQueue[qi].count        = 8 + (rand() % 5); /* 8-12 */
                                            g_monTrapQueue[qi].spawnedSoFar = 0;
                                            g_monTrapQueue[qi].dueTick      = GetTickCount();
                                            g_monTrapQueue[qi].src          = 1; /* reward trap — announced right below */
                                            break;
                                        }
                                    }
                                    g_monTrapLastArea = area; /* don't let the drain loop clear our just-queued ambush */
                                    Log("TRAP: enqueued staggered ambush (id=%d) at (%d,%d) area=%d\n", g_lastKillMonId, nX, nY, area);
                                    ShowNotify("TRAP! Monsters incoming!");
                                } else {
                                    Log("TRAP: no recent same-area kill — skipping (avoid unloaded-monster crash)\n");
                                }
                            }
                        }
                    } __except(1) { Log("TRAP: exception\n"); }
                    g_pendingTrapSpawn--;
                }
            }
        }
    }

    /* 2.x — Monster Revive Trap: spawn any queued ambushes that are now due. */
    /* 2.x — process the staggered trap queue ALWAYS (not gated on g_monsterTrap) so a REWARD_TRAP ambush (enqueued below) also drains even when the Monster Revive Trap toggle is off. */
    if (g_cachedPGame && fnSpawnMonster) {
        int mtArea = GetCurrentArea();
        if (mtArea != g_monTrapLastArea) {
            /* Area changed — captured pRoom pointers are now stale; drop the queue so we never spawn against a freed room. */
            for (int qi = 0; qi < MONTRAP_MAX_PENDING; qi++) g_monTrapQueue[qi].active = FALSE;
            g_monTrapLastArea = mtArea;
        } else {
            DWORD mtNow = GetTickCount();
            /* USE-AFTER-FREE FIX — never spawn against the ENQUEUED pRoom. */
            DWORD liveRoom = 0;
            __try {
                DWORD pClient = *(DWORD*)(g_cachedPGame + 0x88);
                if (pClient > 0x10000) {
                    DWORD pUnit = *(DWORD*)(pClient + 0x174);
                    if (pUnit > 0x10000) {
                        DWORD pPath = *(DWORD*)(pUnit + 0x2C);
                        if (pPath) liveRoom = *(DWORD*)(pPath + 0x1C);
                    }
                }
            } __except(1) { liveRoom = 0; }
            for (int qi = 0; qi < MONTRAP_MAX_PENDING; qi++) {
                if (!g_monTrapQueue[qi].active) continue;
                if (mtNow < g_monTrapQueue[qi].dueTick) continue;
                if (!liveRoom) continue;                    /* no live room — retry next tick */
                if (mtNow - g_monTrapQueue[qi].dueTick > 8000) {
                    /* TTL: entry has been failing/waiting too long — drop it. */
                    Log("MONTRAP: TTL expired for monId=%d — dropping entry\n",
                        g_monTrapQueue[qi].monId);
                    g_monTrapQueue[qi].active = FALSE;
                    continue;
                }
                /* 2.x — STAGGER (Marco): spawn only 1-2 copies per tick, short delay between batches. */
                int remaining = g_monTrapQueue[qi].count - g_monTrapQueue[qi].spawnedSoFar;
                int batch = 1 + (int)(mtNow & 1);            /* 1 or 2 this tick */
                if (batch > remaining) batch = remaining;
                /* Rooms and coordinates travel as a PAIR. The death spot must be
                 * resolved to the room that actually CONTAINS it — passing the
                 * player's liveRoom with the death coordinates is exactly the
                 * DungeonTestRoomGame assert (UnitRoom.cpp:44, Marco's crash).
                 * The old code re-anchored AFTER a failed spawn, but D2 asserts
                 * instead of returning NULL, so that path never ran. Re-anchor
                 * BEFORE: if no loaded room contains the death spot, move the
                 * whole ambush to the player now and spawn there. */
                if (!Room_ForPoint(g_monTrapQueue[qi].x, g_monTrapQueue[qi].y)) {
                    __try {
                        DWORD pClient = *(DWORD*)(g_cachedPGame + 0x88);
                        DWORD pUnit   = (pClient > 0x10000) ? *(DWORD*)(pClient + 0x174) : 0;
                        DWORD pPath   = (pUnit  > 0x10000) ? *(DWORD*)(pUnit + 0x2C)    : 0;
                        if (pPath) {
                            int px = (int)*(unsigned short*)(pPath + 0x02);
                            int py = (int)*(unsigned short*)(pPath + 0x06);
                            if (px && py) {
                                Log("MONTRAP: re-anchoring ambush monId=%d (%d,%d)->(%d,%d) BEFORE spawn\n",
                                    g_monTrapQueue[qi].monId,
                                    g_monTrapQueue[qi].x, g_monTrapQueue[qi].y, px, py);
                                g_monTrapQueue[qi].x = px;
                                g_monTrapQueue[qi].y = py;
                            }
                        }
                    } __except(1) {}
                }
                int got = 0;
                for (int b = 0; b < batch; b++) {
                    int k = g_monTrapQueue[qi].spawnedSoFar + b;
                    int ox = g_monTrapQueue[qi].x + (k % 4) * 4 - 6;
                    int oy = g_monTrapQueue[qi].y + (k / 4) * 4 - 4;
                    /* Per-copy: the k-offsets can straddle a room border even
                     * when the anchor point resolves, so each copy gets its own
                     * containing room; none found -> skip the copy, never crash. */
                    DWORD copyRoom = Room_ForPoint(ox, oy);
                    if (!copyRoom) continue;
                    __try {
                        void* u = fnSpawnMonster((void*)g_cachedPGame,
                            (void*)copyRoom, ox, oy,
                            g_monTrapQueue[qi].monId, 1, -1, 0);
                        if (u) { got++; MonTrap_RememberSpawned(*(DWORD*)((DWORD)u + 0x0C)); }
                    } __except(1) {}
                }
                if (g_monTrapQueue[qi].spawnedSoFar == 0 && got > 0 &&
                    g_monTrapQueue[qi].src == 0)
                    ShowNotify("TRAP! The slain rise against you!");   /* once, first batch, REVIVE trap only — reward traps announced themselves at enqueue */
                g_monTrapQueue[qi].spawnedSoFar += batch;
                if (g_monTrapQueue[qi].spawnedSoFar >= g_monTrapQueue[qi].count) {
                    Log("MONTRAP: ambush done (%d copies) of monId=%d at (%d,%d)\n",
                        g_monTrapQueue[qi].spawnedSoFar, g_monTrapQueue[qi].monId,
                        g_monTrapQueue[qi].x, g_monTrapQueue[qi].y);
                    g_monTrapQueue[qi].active = FALSE;
                } else {
                    g_monTrapQueue[qi].dueTick = mtNow + 150;   /* next batch soon */
                }
            }
        }
    }

    /* object-spawn test removed 2026-04-27 ? */


    /* Cheat physical portal spawn. */
    if (g_pendingPortalLevel != 0 && g_cachedPGame) {
        int destLevel = g_pendingPortalLevel;
        int objId     = g_pendingPortalObjId;
        g_pendingPortalLevel = 0;

        typedef int (__fastcall *CreatePortalObject_t)(
            void* pGame, void* pUnit, void* pRoom,
            int nX, int nY,
            int nDestLevel,
            void** ppSourceUnit,
            int nObjectId,
            int bPerm);
        static CreatePortalObject_t fnCreatePortal = NULL;
        static BOOL portalResolved = FALSE;
        if (!portalResolved) {
            portalResolved = TRUE;
            HMODULE hG = GetModuleHandleA("D2Game.dll");
            if (hG) {
                fnCreatePortal = (CreatePortalObject_t)((DWORD)hG + 0xE3DF0);
                Log("PORTAL: resolved CreatePortalObject at %p (D2Game=%p)\n",
                    fnCreatePortal, hG);
            }
        }

        if (fnCreatePortal) {
            void* pSrvPlayer = GetServerPlayer(g_cachedPGame);
            if (pSrvPlayer) {
                __try {
                    DWORD pPath = *(DWORD*)((DWORD)pSrvPlayer + 0x2C);
                    if (pPath) {
                        DWORD pRoom = *(DWORD*)(pPath + 0x1C);
                        int playerX = (int)*(unsigned short*)(pPath + 0x02);
                        int playerY = (int)*(unsigned short*)(pPath + 0x06);
                        int curLevelId = 0;
                        if (pRoom) {
                            DWORD pDrlgRoom = *(DWORD*)(pRoom + 0x38);
                            if (pDrlgRoom) {
                                DWORD pLevel = *(DWORD*)(pDrlgRoom + 0x00);
                                if (pLevel) curLevelId = *(int*)(pLevel + 0x04);
                            }
                        }
                        /* Restriction: D2 1.10f's CreatePortalObject asserts on CROSS-ACT portals (Skills.cpp:3651). */
                        #define ACT_OF_LEVEL(id) \
                            ((id) < 40  ? 0 : \
                             (id) < 75  ? 1 : \
                             (id) < 103 ? 2 : \
                             (id) < 109 ? 3 : 4)
                        int srcAct  = ACT_OF_LEVEL(curLevelId);
                        int destAct = ACT_OF_LEVEL(destLevel);
                        BOOL inTown = IsTown((DWORD)curLevelId);
                        /* Cross-act portals removed 2026-05-05 (rift abandoned). */
                        BOOL allowed = pRoom && objId == 60 && (srcAct == destAct);
                        #undef ACT_OF_LEVEL
                        if (allowed) {
                            int spX = playerX + 3;
                            int spY = playerY;
                            void* pPortal = NULL;
                            int rc = fnCreatePortal((void*)g_cachedPGame, pSrvPlayer,
                                                    (void*)pRoom, spX, spY,
                                                    destLevel, &pPortal,
                                                    objId, 1);
                            Log("CHEAT PORTAL: src=%d(act%d) dest=%d(act%d) objId=%d "
                                "at (%d,%d) rc=%d pPortal=%p inTown=%d\n",
                                curLevelId, srcAct, destLevel, destAct, objId,
                                spX, spY, rc, pPortal, (int)inTown);
                        } else {
                            Log("CHEAT PORTAL: rejected src=%d(act%d) dest=%d(act%d) "
                                "objId=%d inTown=%d (cross-act portals would assert)\n",
                                curLevelId, srcAct, destLevel, destAct, objId,
                                (int)inTown);
                        }
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Log("CHEAT PORTAL: EXCEPTION while spawning\n");
                }
            } else {
                Log("CHEAT PORTAL: no server player\n");
            }
        }
    }

    /* ZONE TELEPORT: warp player to town if in locked zone. */
    /* Delayed second pet-AI kick (armed by the warp below). */
    if (g_petKickPending > 0 && --g_petKickPending == 0)
        PetAiKick("delayed");

    /* Staggered uber-trio drain — bosses 2 and 3 of a Pandemonium recipe spawn from here, ~700 ms apart, against the player's LIVE room. */
    if (g_cachedPGame) {
        extern void Ubers_TickPending(void* pGame, void* pPlayer);
        void* pSrvP = GetServerPlayer(g_cachedPGame);
        if (pSrvP) {
            __try { Ubers_TickPending((void*)g_cachedPGame, pSrvP); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    if (g_pendingZoneTeleport > 0) {
        int townArea = g_pendingZoneTeleport;
        if (!g_cachedPGame || !hD2Game) {
            /* Hold the queue until pGame becomes available — DON'T silently drop. */
            Log("ZONE WARP: deferring (pGame=%08X hD2Game=%08X area=%d)\n",
                g_cachedPGame, (DWORD)hD2Game, townArea);
        } else {
            BOOL warpFired = FALSE;
            __try {
                FARPROC warpFn = (FARPROC)((DWORD)hD2Game + 0xC410);
                DWORD pClient = *(DWORD*)(g_cachedPGame + 0x88);
                DWORD pUnit = 0;
                if (pClient > 0x10000) pUnit = *(DWORD*)(pClient + 0x174);
                if (pUnit > 0x10000 && warpFn) {
                    typedef void (__fastcall *WarpUnit_t)(void*, void*, int, int);
                    Log("ZONE WARP: calling LEVEL_WarpUnit(pGame=%08X pUnit=%08X area=%d)\n",
                        g_cachedPGame, pUnit, townArea);
                    ((WarpUnit_t)warpFn)((void*)g_cachedPGame, (void*)pUnit, townArea, 0);
                    Log("ZONE WARP: LEVEL_WarpUnit returned — area=%d\n", townArea);
                    warpFired = TRUE;
                } else {
                    Log("ZONE WARP: skip — pClient=%08X pUnit=%08X warpFn=%08X area=%d\n",
                        pClient, pUnit, (DWORD)warpFn, townArea);
                }
            } __except(1) { Log("ZONE WARP: exception (area=%d)\n", townArea); }
            /* V9 (#35) — RETRY instead of always clearing. */
            static int s_warpTries = 0;
            static int s_warpLastArea = 0;
            /* New/changed target -> fresh retry budget (a request abandoned at char exit must not shrink the next character's 40 attempts). */
            if (townArea != s_warpLastArea) { s_warpLastArea = townArea; s_warpTries = 0; }
            if (warpFired) {
                g_pendingZoneTeleport = 0;
                g_pendingWarpTarget   = 0;   /* don't leak the target into later logic */
                s_warpTries = 0;
                /* Pet AI kick: immediately, plus a delayed second pass ~10 ticks out (see the block above ProcessPendingGameTick). */
                PetAiKick("post-warp");
                g_petKickPending = 10;
            } else if (++s_warpTries >= 40) {
                Log("ZONE WARP: giving up on area=%d after %d attempts\n", townArea, s_warpTries);
                g_pendingZoneTeleport = 0;
                g_pendingWarpTarget   = 0;
                s_warpTries = 0;
                g_relocateNextTo = 0;
                g_relocateLevel  = 0;
            }
        }
    }

    LandingNudge_Tick();

    /* Process cheat menu commands (set from UI, consumed here in game tick) */
    if (g_cachedPGame && fnAddStat && (g_cheatGold || g_cheatStatPts ||
                                       g_cheatSkillPts || g_cheatLevel ||
                                       g_cheatXpBig)) {
        void* pSC = GetServerPlayer(g_cachedPGame);
        if (pSC) {
            if (g_cheatGold > 0) {
                __try { fnAddStat(pSC, 14, g_cheatGold, 0); }
                __except(1) { Log("CHEAT: AddStat GOLD(%d) exception\n", g_cheatGold); }
                g_cheatGold = 0;
            }
            if (g_cheatStatPts > 0) {
                __try { fnAddStat(pSC, 4, g_cheatStatPts, 0); }
                __except(1) { Log("CHEAT: AddStat STATPTS(%d) exception\n", g_cheatStatPts); }
                g_cheatStatPts = 0;
            }
            if (g_cheatSkillPts > 0) {
                __try { fnAddStat(pSC, 5, g_cheatSkillPts, 0); }
                __except(1) { Log("CHEAT: AddStat NEWSKILLS(%d) exception\n", g_cheatSkillPts); }
                g_cheatSkillPts = 0;
            }
            if (g_cheatLevel > 0) {
                /* "+10 Levels" used to hand over a flat 10 x 50,000 XP, which is
                 * about thirty levels at level 1 and almost nothing at level 80.
                 * Now it reads the character's level and adds the real gap to
                 * level+10, so the label is true wherever it is pressed. */
                Cheat_GiveLevels(pSC, g_cheatLevel);
                g_cheatLevel = 0;
            }
            if (g_cheatXpBig > 0) {
                Cheat_GiveExperience(pSC, CHEAT_BIG_XP);
                g_cheatXpBig = 0;
            }
        }
    }

    /* === TRAP DEBUG: Spawn SuperUnique via cheat menu === */
    if (g_cheatSpawnTrapSU > 0) {
        Log("CHEAT TRAP SU: triggered, pendingTrapSpawn was %d\n", g_pendingTrapSpawn);
        g_pendingTrapSpawn++;
        g_cheatSpawnTrapSU = 0;
    }

    /* === TRAP DEBUG: Spawn regular monsters via cheat menu === */
    if (g_cheatSpawnTrapMon > 0 && g_cachedPGame) {
        void* pMon = GetServerPlayer(g_cachedPGame);
        if (pMon) {
            __try {
                DWORD pPath = *(DWORD*)((DWORD)pMon + 0x2C);
                if (pPath) {
                    DWORD pRoom = *(DWORD*)(pPath + 0x1C);
                    int nX = (int)*(unsigned short*)(pPath + 0x02);
                    int nY = (int)*(unsigned short*)(pPath + 0x06);
                    Log("CHEAT SPAWN MON: player at (%d,%d) pRoom=%08X fnSpawnMonster=%08X\n",
                        nX, nY, pRoom, (DWORD)fnSpawnMonster);

                    if (pRoom && nX > 0 && nY > 0 && fnSpawnMonster) {
                        /* Test multiple monster IDs and param combos to find what works. */
                        static const int testMonIds[] = {3, 4, 5, 7, 9}; /* Fallen, zombie, skeleton variants */
                        int spawned = 0;
                        for (int ti = 0; ti < 5; ti++) {
                            int ox = nX + (ti % 3) * 4 - 4;
                            int oy = nY + (ti / 3) * 4 - 2;
                            int monId = testMonIds[ti];

                            /* Try 1: nAnimMode=1, a7=-1 (D2MOO recommended) */
                            __try {
                                void* unit = fnSpawnMonster((void*)g_cachedPGame, (void*)pRoom, ox, oy, monId, 1, -1, 0);
                                Log("CHEAT MON[%d] mode=1,a7=-1: monId=%d at (%d,%d) result=%08X\n",
                                    ti, monId, ox, oy, (DWORD)unit);
                                if (unit) { spawned++; continue; }
                            } __except(1) {
                                Log("CHEAT MON[%d] mode=1,a7=-1: CRASHED\n", ti);
                            }

                            /* Try 2: nAnimMode=0, a7=4 (alternate search radius) */
                            __try {
                                void* unit = fnSpawnMonster((void*)g_cachedPGame, (void*)pRoom, ox, oy, monId, 0, 4, 0);
                                Log("CHEAT MON[%d] mode=0,a7=4: monId=%d at (%d,%d) result=%08X\n",
                                    ti, monId, ox, oy, (DWORD)unit);
                                if (unit) { spawned++; continue; }
                            } __except(1) {
                                Log("CHEAT MON[%d] mode=0,a7=4: CRASHED\n", ti);
                            }

                            /* Try 3: Use SpawnNormalMonster at offset 0x38E30 — different func */
                            __try {
                                typedef void* (__fastcall *SpawnNormal_t)(void*, void*, int, int, int, int, int, short);
                                SpawnNormal_t fnSpawnNormal = (SpawnNormal_t)((DWORD)hD2Game + 0x38E30);
                                void* unit = fnSpawnNormal((void*)g_cachedPGame, (void*)pRoom, ox, oy, monId, 1, -1, 0);
                                Log("CHEAT MON[%d] SpawnNormal: monId=%d at (%d,%d) result=%08X\n",
                                    ti, monId, ox, oy, (DWORD)unit);
                                if (unit) spawned++;
                            } __except(1) {
                                Log("CHEAT MON[%d] SpawnNormal: CRASHED\n", ti);
                            }
                        }
                        Log("CHEAT SPAWN MON: %d/%d spawned OK\n", spawned, 5);
                    } else {
                        Log("CHEAT SPAWN MON: missing: pRoom=%08X fnSpawnMon=%08X\n", pRoom, (DWORD)fnSpawnMonster);
                    }
                }
            } __except(1) { Log("CHEAT SPAWN MON: outer exception\n"); }
        } else {
            Log("CHEAT SPAWN MON: no server player\n");
        }
        g_cheatSpawnTrapMon = 0;
    }

    /* === TRAP CURSE SYSTEM: Apply curses from trap triggers + auto-expire === */
    /* log entry to OUTER block so we can see if any of the three gating conditions (g_cachedPGame, fnAddStat, GetServerPlayer != NULL) is failing. */
    {
        static DWORD s_lastOuter = 0;
        DWORD nd = GetTickCount();
        if (nd - s_lastOuter > 2000) {
            s_lastOuter = nd;
            void* probe = (g_cachedPGame && fnAddStat) ? GetServerPlayer(g_cachedPGame) : NULL;
            if (g_verboseInput) Log("OUTER DRAIN diag: pGame=%p fnAddStat=%p pCurseTarget=%p pendLoot=%d\n",
                g_cachedPGame, (void*)fnAddStat, probe, g_pendingLootDrop);
        }
    }
    if (g_cachedPGame && fnAddStat) {
        void* pCurseTarget = GetServerPlayer(g_cachedPGame);
        if (pCurseTarget) {
            typedef void (__stdcall *ToggleState_t)(void* pUnit, int nState, int bSet);
            static ToggleState_t fnToggleState = NULL;
            if (!fnToggleState && hD2Common) {
                fnToggleState = (ToggleState_t)GetProcAddress(hD2Common, (LPCSTR)10486);
            }

            /* Track active curses for auto-removal */
            static DWORD s_slowExpire = 0;
            static DWORD s_weakenExpire = 0;
            static DWORD s_poisonExpire = 0;
            static int s_poisonTickNext = 0;

            /* Duration scales with level: 10 seconds per 10 levels */
            int playerLevel = 1;
            __try { playerLevel = fnGetStat(pCurseTarget, 12, 0); } __except(1) {}
            if (playerLevel < 1) playerLevel = 1;
            int curseDurationMs = ((playerLevel - 1) / 10 + 1) * 10000;

            /* === Auto-remove expired curses === */
            DWORD now = GetTickCount();
            if (s_slowExpire > 0 && now >= s_slowExpire) {
                __try {
                    fnAddStat(pCurseTarget, 67, 50, 0);
                    if (fnToggleState) fnToggleState(pCurseTarget, 60, 0);
                    Log("TRAP SLOW: expired, velocity restored\n");
                } __except(1) { Log("TRAP SLOW expire: exception\n"); }
                s_slowExpire = 0;
            }
            if (s_weakenExpire > 0 && now >= s_weakenExpire) {
                __try {
                    /* 1.7.1 FIX: stat 21 (mindamage) removal — toggle state alone provides the AMPLIFYDAMAGE effect (2x damage taken). */
                    if (fnToggleState) fnToggleState(pCurseTarget, 9, 0);
                    Log("TRAP WEAKEN: expired, AMPLIFYDAMAGE state cleared\n");
                } __except(1) { Log("TRAP WEAKEN expire: exception\n"); }
                s_weakenExpire = 0;
            }
            if (s_poisonExpire > 0 && now < s_poisonExpire && now >= (DWORD)s_poisonTickNext) {
                __try {
                    int curHP = fnGetStat(pCurseTarget, 6, 0);
                    if (curHP > 1280) {
                        fnAddStat(pCurseTarget, 6, -512, 0); /* -2 HP per tick */
                    }
                } __except(1) { Log("TRAP POISON tick: exception\n"); }
                s_poisonTickNext = now + 500;
            }
            if (s_poisonExpire > 0 && now >= s_poisonExpire) {
                __try {
                    if (fnToggleState) fnToggleState(pCurseTarget, 2, 0);
                    Log("TRAP POISON: expired\n");
                } __except(1) { Log("TRAP POISON expire: exception\n"); }
                s_poisonExpire = 0;
            }

            /* === Apply pending trap curses (from quest rewards) === 1.7.1: Curse stacking protection — if the same curse is already active, EXTEND the existing expiration instead of re-applying the stat changes. */
            if (g_pendingTrapSlow > 0) {
                if (s_slowExpire > now) {
                    s_slowExpire = now + curseDurationMs;
                    Log("TRAP SLOW: already active — extended duration to %u ms (lvl %d)\n",
                        curseDurationMs, playerLevel);
                } else {
                    Log("TRAP SLOW: applying Decrepify for %dms (lvl %d)\n", curseDurationMs, playerLevel);
                    __try {
                        fnAddStat(pCurseTarget, 67, -50, 0);
                        if (fnToggleState) fnToggleState(pCurseTarget, 60, 1);
                        s_slowExpire = now + curseDurationMs;
                    } __except(1) { Log("TRAP SLOW apply: exception\n"); }
                }
                g_pendingTrapSlow--;
            }

            if (g_pendingTrapWeaken > 0) {
                if (s_weakenExpire > now) {
                    s_weakenExpire = now + curseDurationMs;
                    Log("TRAP WEAKEN: already active — extended duration to %u ms (lvl %d)\n",
                        curseDurationMs, playerLevel);
                } else {
                    /* 1.7.1 FIX: stat 21 (mindamage) mutation removed — state 9 (STATE_AMPLIFYDAMAGE) alone provides 2x damage taken. */
                    __try {
                        if (fnToggleState) fnToggleState(pCurseTarget, 9, 1);
                        s_weakenExpire = now + curseDurationMs;
                    } __except(1) { Log("TRAP WEAKEN apply: exception\n"); }
                    Log("WEAKEN: state 9 AMPLIFYDAMAGE toggled, duration=%d ms\n", curseDurationMs);
                }
                g_pendingTrapWeaken--;
            }

            if (g_pendingTrapPoison > 0) {
                if (s_poisonExpire > now) {
                    s_poisonExpire = now + curseDurationMs;
                    Log("TRAP POISON: already active — extended duration to %u ms (lvl %d)\n",
                        curseDurationMs, playerLevel);
                } else {
                    Log("TRAP POISON: applying for %dms (lvl %d)\n", curseDurationMs, playerLevel);
                    __try {
                        if (fnToggleState) fnToggleState(pCurseTarget, 2, 1);
                        int curHP = fnGetStat(pCurseTarget, 6, 0);
                        if (curHP > 2560) {
                            fnAddStat(pCurseTarget, 6, -1280, 0); /* -5 HP initial hit */
                        }
                        s_poisonExpire = now + curseDurationMs;
                        s_poisonTickNext = now + 500;
                    } __except(1) { Log("TRAP POISON apply: exception\n"); }
                }
                g_pendingTrapPoison--;
            }

            /* Object traps drain here rather than next to the curse traps
             * above: they need a room and a subtile, which only holds while
             * pCurseTarget is a live server player. */
            ObjTraps_Tick();

            /* === Drop gold (debug menu) === */
            if (g_cheatDropGold > 0) {
                __try {
                    fnAddStat(pCurseTarget, 14, 1000, 0);
                    Log("CHEAT DROP GOLD: +1000 gold\n");
                } __except(1) { Log("CHEAT DROP GOLD: exception\n"); }
                g_cheatDropGold = 0;
            }

            /* === REWARD_LOOT: Drop random boss loot table from quest reward === 1.7.1: cap the queue at 5 so accumulated rewards don't carpet the floor at once; 3-second cooldown between drops to avoid item-count overflow; IsTown guard so towns stay clean; ilvl floor of 30 keeps low-level drops useful; notify only on successful drop (nDropped > 0). */
            {
                static DWORD s_lastLootDropMs = 0;
                /* was 5. The clamp exists so ordinary rewards that pile up while you are in a menu do not carpet the floor at once. It was raised to 16 for the Treasure Cow, which used to queue 12 tables on death and lost the tail to a clamp of 5. The cow now drops through its own treasure class instead, but the headroom still serves any other burst. */
                if (g_pendingLootDrop > 16) g_pendingLootDrop = 16;
                /* log entry to drain block so we can see which condition is blocking. */
                if (g_pendingLootDrop > 0) {
                    static DWORD s_lastDiag = 0;
                    DWORD nd = GetTickCount();
                    if (nd - s_lastDiag > 2000) {
                        s_lastDiag = nd;
                        Log("LOOT DRAIN diag: pending=%d pGame=%p hD2Game=%p area=%d\n",
                            g_pendingLootDrop, g_cachedPGame, hD2Game, GetCurrentArea());
                    }
                }
                if (g_pendingLootDrop > 0 && g_cachedPGame && hD2Game) {
                    DWORD nowMs = GetTickCount();
                    /* Town-skip removed. User explicitly wants loot delivered everywhere; only TRAPS (monster spawns) need a town guard. Boss-loot TC drops on the ground around the player; in town that's just cosmetic clutter the player accepted by enabling the bonus. */
                    if (nowMs - s_lastLootDropMs < 250) {   /* was 3000ms; near-instant loot (cap of 5 keeps the floor sane) */
                        /* Cooldown — try again next tick. */
                    } else {
                        s_lastLootDropMs = nowMs;

                        /* pre-rolled boss takes priority over the legacy random pick. */
                        int bossIdx;
                        if (g_pendingLootBossId >= 0 && g_pendingLootBossId < BOSS_LOOT_COUNT) {
                            bossIdx = g_pendingLootBossId;
                            g_pendingLootBossId = -1;
                        } else {
                            DWORD t = GetTickCount();
                            bossIdx = (int)((t ^ (t >> 11)) % BOSS_LOOT_COUNT);
                        }
                        int playerLvl = 1;
                        __try { playerLvl = fnGetStat(pCurseTarget, 12, 0); } __except(1) {}
                        if (playerLvl < 1) playerLvl = 1;
                        int itemLevel = playerLvl + 5;
                        if (itemLevel < 85) itemLevel = 85;
                        if (itemLevel > 99) itemLevel = 99;

                        /* === 2.10.0 BOSS-LOOT QUALITY REWRITE Marco: "drop GOOD loot, make Magic Find actually work, and at 0 MF still drop decent loot — not pisse-lorteloot." The OLD path called fnDropTC(pMonster=NULL, nQuality=0): (a) it rolled the raw boss TC, whose OWN weights are ~50% NoDrop/gold/junk-potions (Andariel(H): NoDrop 19 + gold 11 + junk 15 vs equip 19 + good 3 / 67) — bosses are balanced around a real KILL, not a reward; (b) with a NULL monster the engine reads Magic Find off the (absent) monster unit -> effective MF = 0 -> every item that did drop collapsed to the BOTTOM of the quality cascade (normal/white). */
                        /* each boss draws from its OWN treasure-class band (Andariel 39-69 … Baal 54-87) so it is REALLY that boss's loot; higher bosses reach higher-tier bases. */
                        typedef void* (__fastcall *QUESTS_CreateItem_t)(
                            void* pGame, void* pPlayer,
                            DWORD dwCode, int nLevel, BYTE nQuality, int bDroppable);
                        QUESTS_CreateItem_t fnCI =
                            (QUESTS_CreateItem_t)((DWORD)hD2Game + 0x65DF0);
                        BossLoot_LoadItemDB();
                        if (!g_uniqueCatalogLoaded) Quests_LoadUniqueCatalog();
                        int bandLo = g_bossBandLo[bossIdx];
                        int bandHi = g_bossBandHi[bossIdx];
                        (void)itemLevel;  /* superseded by the per-boss band */

                        /* Player's TOTAL magic find (item_magicbonus, stat 80). */
                        int mf = 0;
                        __try { mf = fnGetStat(pCurseTarget, 80, 0); } __except(1) { mf = 0; }
                        if (mf < 0) mf = 0;
                        /* D2 1.10 diminishing-returns caps (unique 250 / set 500 / rare 600). */
                        int emfU = (mf > 0) ? (250 * mf) / (mf + 250) : 0;
                        int emfS = (mf > 0) ? (500 * mf) / (mf + 500) : 0;
                        int emfR = (mf > 0) ? (600 * mf) / (mf + 600) : 0;
                        /* Per-mille tier chances at 0 MF, scaled by MF and capped. */
                        int pU = ( 40 * (100 + emfU)) / 100; if (pU > 250) pU = 250;  /* unique (gold) */
                        int pS = ( 70 * (100 + emfS)) / 100; if (pS > 350) pS = 350;  /* set (green) */
                        int pR = (350 * (100 + emfR)) / 100; if (pR > 620) pR = 620;  /* rare (yellow) */

                        /* xorshift PRNG advanced per roll (GetTickCount is constant within a tick — all rolls would otherwise be identical). */
                        static unsigned int s_lootRng = 0;
                        if (s_lootRng == 0) s_lootRng = GetTickCount() ^ 0x9E3779B9u;

                        /* GOLD — Marco: boss loot must include gold. */
                        if (fnAddStat) {
                            int gold = 6000 + bossIdx * 7000 + (int)(s_lootRng % 12000);
                            GrantGoldCapped(pCurseTarget, gold);   /* overflow -> gold bank */
                        }

                        /* 3-5 items per drop (Marco). */
                        int nItems = 3 + (int)(s_lootRng % 3);
                        int nInv = 0, nGround = 0, nUniq = 0, nSet = 0, nRare = 0, nMagic = 0, li;
                        for (li = 0; li < nItems; li++) {
                            /* advance RNG -> pick a base from THIS boss's band (or a charm) */
                            s_lootRng ^= s_lootRng << 13; s_lootRng ^= s_lootRng >> 17; s_lootRng ^= s_lootRng << 5;
                            int isCharm = ((int)(s_lootRng % 100) < 15);
                            DWORD code;
                            if (isCharm) {
                                static const char* ch[3] = { "cm1", "cm2", "cm3" };
                                code = Quests_PackCode(ch[(s_lootRng >> 9) % 3]);
                            } else {
                                code = BossLoot_PickBase(bandLo, bandHi, s_lootRng);
                            }
                            if (!code) continue;
                            /* advance RNG -> roll quality from MF (magic->rare->set->unique) */
                            s_lootRng ^= s_lootRng << 13; s_lootRng ^= s_lootRng >> 17; s_lootRng ^= s_lootRng << 5;
                            int roll = (int)(s_lootRng % 1000);
                            BYTE qual;
                            if      (isCharm)             qual = 4;  /* charms are magic */
                            else if (roll < pU)           qual = 7;  /* unique (gold) */
                            else if (roll < pU + pS)      qual = 5;  /* set (green) */
                            else if (roll < pU + pS + pR) qual = 6;  /* rare (yellow) */
                            else                          qual = 4;  /* magic (blue, floor) */

                            /* 2.9.9 RESEARCH FIX — the boss-loot test proved (LOOT DROP lines: R0 across ALL 5 bosses despite rare being ~35%% of rolls) that fnCI(quality=6/rare) returns NULL for weapon/armor bases EVERY time — exactly like set/unique. */
                            void* pItem = NULL; BYTE gotQual = qual;
                            {
                                struct { BYTE q; int lvl; } tr[6];
                                int nt = 0;
                                if (qual == 5 || qual == 7) {          /* set / unique */
                                    tr[nt].q = qual; tr[nt].lvl = bandHi; nt++;
                                    tr[nt].q = qual; tr[nt].lvl = 50;     nt++;
                                    tr[nt].q = qual; tr[nt].lvl = 30;     nt++;
                                    tr[nt].q = 6;    tr[nt].lvl = bandHi; nt++;
                                    tr[nt].q = 4;    tr[nt].lvl = bandHi; nt++;
                                    tr[nt].q = 2;    tr[nt].lvl = bandHi; nt++;
                                } else if (qual == 6) {                 /* rare */
                                    tr[nt].q = 6;    tr[nt].lvl = bandHi; nt++;
                                    tr[nt].q = 4;    tr[nt].lvl = bandHi; nt++;
                                    tr[nt].q = 2;    tr[nt].lvl = bandHi; nt++;
                                } else {                                /* magic / charm floor */
                                    tr[nt].q = qual; tr[nt].lvl = bandHi; nt++;
                                    tr[nt].q = 2;    tr[nt].lvl = bandHi; nt++;
                                }
                                for (int t = 0; t < nt && !pItem; t++) {
                                    __try { pItem = fnCI((void*)g_cachedPGame, pCurseTarget, code, tr[t].lvl, tr[t].q, 1); }
                                    __except(EXCEPTION_EXECUTE_HANDLER) { pItem = NULL; }
                                    if (pItem) gotQual = tr[t].q;
                                }
                                if (!pItem)   /* base is fundamentally unspawnable — rare; log it */
                                    Log("LOOT ITEM FAIL: boss=%s code=%08X reqQ=%d — every quality NULL\n",
                                        g_bossLootNames[bossIdx], code, qual);
                            }
                            if (pItem) {
                                if (fnSetStat) {
                                    __try { fnSetStat(pItem,91,0,0); fnSetStat(pItem,92,1,0); fnSetStat(pItem,93,0,0); }
                                    __except(EXCEPTION_EXECUTE_HANDLER) {}
                                }
                                { extern void Coll_MarkApItemReqFree(void* pItem);
                                  __try { Coll_MarkApItemReqFree(pItem); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
                                nInv++;
                                if      (gotQual == 7) nUniq++;
                                else if (gotQual == 5) nSet++;
                                else if (gotQual == 6) nRare++;
                                else                   nMagic++;
                            }
                        }

                        Log("LOOT DROP: boss=%s mf=%d band=%d-%d -> %d items (inv %d, ground %d | U%d S%d R%d M%d)\n",
                            g_bossLootNames[bossIdx], mf, bandLo, bandHi,
                            nInv + nGround, nInv, nGround, nUniq, nSet, nRare, nMagic);
                        /* Name the boss and the haul. The old "Awesome loot
                         * incoming!" carried neither a colon nor the TRAP
                         * prefix, which is what the standalone pipe filters on
                         * — so boss loot was the one reward type that produced
                         * no tracker row and, once solo runs gained sounds, no
                         * sound either. Every other drop type already announces
                         * itself as "Drop: <what>". */
                        if (nInv + nGround > 0) {
                            char lootNote[64];
                            _snprintf(lootNote, sizeof(lootNote), "Drop: %s Loot (%d items)",
                                      g_bossLootNames[bossIdx], nInv + nGround);
                            lootNote[sizeof(lootNote) - 1] = 0;
                            ShowNotify(lootNote);
                        }
                        if (g_dtActive && g_dtPhase == 0) {   /* boss loot is phase 0 in the test */
                            g_dtBossItems += (nInv + nGround);
                            char dm[176];
                            _snprintf(dm, sizeof(dm), "[BOSS LOOT %s] %d items (inv %d, ground %d | U%d S%d R%d M%d)\n",
                                g_bossLootNames[bossIdx], nInv + nGround, nInv, nGround, nUniq, nSet, nRare, nMagic);
                            DeliveryTestLog(dm);
                        }
                        g_pendingLootDrop--;
                    }
                }
            }

            /* === 1.9.0: Specific-item drop processor — pops one entry from g_pendingDropQueue per tick (with cooldown) and spawns the right charm/set/unique via QUESTS_CreateItem. */
            {
                static DWORD s_lastSpecDropMs = 0;
                int peekKind = 0, peekIdx = 0;
                /* log entry to drain block. */
                {
                    int dKind = 0, dIdx = 0;
                    if (Quests_PeekPendingDrop(&dKind, &dIdx)) {
                        static DWORD s_lastDiag2 = 0;
                        DWORD nd = GetTickCount();
                        if (nd - s_lastDiag2 > 2000) {
                            s_lastDiag2 = nd;
                            Log("SPEC DRAIN diag: peekKind=%d peekIdx=%d pGame=%p hD2Game=%p area=%d\n",
                                dKind, dIdx, g_cachedPGame, hD2Game, GetCurrentArea());
                        }
                    }
                }
                if (Quests_PeekPendingDrop(&peekKind, &peekIdx) && g_cachedPGame && hD2Game) {
                    DWORD nowMs = GetTickCount();
                    /* Town-skip removed. Items go directly to inventory via bDroppable=0, so they appear safely regardless of where the player is standing. Only trap-based monster spawns need a town guard. */
                    if (nowMs - s_lastSpecDropMs < 250) {   /* was 1500ms; near-instant item delivery */
                        /* Cooldown — try again next tick. */
                    } else {
                        s_lastSpecDropMs = nowMs;
                        DWORD code = Quests_GetDropCode(peekKind, peekIdx);
                        BYTE  qual = Quests_GetDropQuality(peekKind);
                        if (peekKind == REWARD_DROP_BATCH) {
                            /* Ordinary-item bundles: runes, gems, potions and
                             * the like. Their own helper knows the codes and
                             * the quantities, so this branch just runs it and
                             * moves the queue along. */
                            int lvl = 1;
                            __try { lvl = fnGetStat(pCurseTarget, 12, 0); } __except(1) {}
                            int made = Quests_SpawnFillerBatch(
                                (void*)g_cachedPGame, pCurseTarget, peekIdx, lvl);
                            if (made == 0)
                                Log("SPEC DROP: filler batch %d produced nothing\n", peekIdx);
                            Quests_ConsumePendingDrop();
                        } else if (code == 0) {
                            Log("SPEC DROP: invalid code for kind=%d idx=%d — skipping\n",
                                peekKind, peekIdx);
                            Quests_ConsumePendingDrop();
                        } else {
                            int playerLvl = 1;
                            __try { playerLvl = fnGetStat(pCurseTarget, 12, 0); } __except(1) {}
                            if (playerLvl < 1) playerLvl = 1;
                            int itemLevel = playerLvl + 5;
                            /* floor at 85 so set/unique items always meet their req-level and spawn at proper quality. */
                            if (itemLevel < 85) itemLevel = 85;
                            if (itemLevel > 99) itemLevel = 99;

                            typedef void* (__fastcall *QUESTS_CreateItem_t)(
                                void* pGame, void* pPlayer,
                                DWORD dwCode, int nLevel, BYTE nQuality, int bDroppable);
                            QUESTS_CreateItem_t fnCI =
                                (QUESTS_CreateItem_t)((DWORD)hD2Game + 0x65DF0);

                            /* deliver with the EXACT mechanism the Ctrl+V dev-menu item buttons use (the runes/gems/bases buttons the tester confirms work 100%). */
                            /* fnCI(quality=set/unique) returns NULL for many armor/weapon bases (charms=magic + amulet-sets work, armor/weapon set/unique don't). */
                            /* FORCE THE EXACT NAMED SET/UNIQUE. */
                            int forceRow = -1;
                            {
                                extern int Coll_GetSetPieceFileIndex(int catalogIdx);
                                extern int Coll_GetUniqueFileIndex(int catalogIdx);
                                __try {
                                    if (peekKind == REWARD_DROP_SET)
                                        forceRow = Coll_GetSetPieceFileIndex(peekIdx);
                                    else if (peekKind == REWARD_DROP_UNIQUE)
                                        forceRow = Coll_GetUniqueFileIndex(peekIdx);
                                } __except(EXCEPTION_EXECUTE_HANDLER) { forceRow = -1; }
                            }
                            {
                                extern void Hooks_ArmForcedItemRow(int rowIdx, void* pGame, BOOL isUnique);
                                __try { Hooks_ArmForcedItemRow(forceRow, (void*)g_cachedPGame,
                                                               peekKind == REWARD_DROP_UNIQUE); }
                                __except(EXCEPTION_EXECUTE_HANDLER) {}
                            }

                            void* pSpawned = NULL;
                            BYTE gotQual = 0; int gotLvl = 0;
                            if (qual == 5 || qual == 7) {
                                /* ladder fix. The old ladder retried the requested quality at item levels 50 and 30, i.e. LOWER than the original: pointless, because a set/unique roll that fails at ilvl N fails harder below it (the qlvl filter only gets stricter). Retry at 99 instead — the highest legal item level, which is what actually widens the eligible set/unique table — and only then degrade. */
                                struct { BYTE q; int lvl; } tries[6] = {
                                    { qual, itemLevel }, { qual, 99 },
                                    { 6, itemLevel }, { 4, itemLevel }, { 2, itemLevel },
                                    { 2, 99 }
                                };
                                for (int t = 0; t < 6 && !pSpawned; t++) {
                                    __try { pSpawned = fnCI((void*)g_cachedPGame, pCurseTarget,
                                                            code, tries[t].lvl, tries[t].q, 1); }
                                    __except(EXCEPTION_EXECUTE_HANDLER) { pSpawned = NULL; }
                                    if (pSpawned) { gotQual = tries[t].q; gotLvl = tries[t].lvl; }
                                }
                            } else {
                                __try { pSpawned = fnCI((void*)g_cachedPGame, pCurseTarget,
                                                        code, itemLevel, qual, 1); }
                                __except(EXCEPTION_EXECUTE_HANDLER) { pSpawned = NULL; }
                                if (pSpawned) { gotQual = qual; gotLvl = itemLevel; }
                            }

                            /* Disarm IMMEDIATELY — every other item the engine creates must see an untouched drop request. */
                            {
                                extern void Hooks_DisarmForcedItemRow(void);
                                __try { Hooks_DisarmForcedItemRow(); }
                                __except(EXCEPTION_EXECUTE_HANDLER) {}
                            }

                            /* V4c self-check — READ THE ITEM, don't trust the return value. */
                            int gotRow = -1, realQual = -1;
                            if (pSpawned) {
                                __try {
                                    DWORD pItemData = *(DWORD*)((BYTE*)pSpawned + 0x14);
                                    if (pItemData) {
                                        realQual = *(int*)(pItemData + 0x00);
                                        gotRow   = *(int*)(pItemData + 0x28);
                                    }
                                } __except(EXCEPTION_EXECUTE_HANDLER) { gotRow = -1; realQual = -1; }
                                if (realQual > 0) gotQual = (BYTE)realQual;
                            }

                            if (pSpawned) {
                                if (fnSetStat) {
                                    __try {
                                        fnSetStat(pSpawned, 91, 0, 0);
                                        fnSetStat(pSpawned, 92, 1, 0);
                                        fnSetStat(pSpawned, 93, 0, 0);
                                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                                }
                                {
                                    extern void Coll_ProcessItem(void* pItem, int requireLegit);
                                    extern void Coll_MarkApItemReqFree(void* pItem);
                                    __try {
                                        Coll_ProcessItem(pSpawned, 0);
                                        Coll_MarkApItemReqFree(pSpawned);
                                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                                }
                            }
                            /* re-announce what ACTUALLY spawned. */
                            if (pSpawned && gotQual != qual) {
                                const char* qName =
                                    (gotQual == 7) ? "unique" : (gotQual == 5) ? "set" :
                                    (gotQual == 6) ? "rare"   : (gotQual == 4) ? "magic" : "normal";
                                char rb[96];
                                _snprintf(rb, sizeof(rb), "AP item arrived as %s (roll failed)", qName);
                                rb[sizeof(rb) - 1] = 0;
                                ShowNotify(rb);
                            }
                            {
                                int stamps = 0;
                                extern int Hooks_ForcedItemStampCount(void);
                                __try { stamps = Hooks_ForcedItemStampCount(); }
                                __except(EXCEPTION_EXECUTE_HANDLER) { stamps = 0; }
                                /* Only call the row un-honoured when the quality actually held — on an engine degrade the file index is cleared to -1 and a row mismatch says nothing about whether the stamp was read. */
                                const char* verdict = "";
                                if (forceRow >= 0 && pSpawned) {
                                    if (realQual > 0 && (BYTE)realQual != qual)
                                        verdict = "  <-- QUALITY DEGRADED IN ENGINE";
                                    else if (gotRow != forceRow)
                                        verdict = "  <-- ROW NOT HONOURED";
                                }
                                Log("SPEC DROP: kind=%d idx=%d code=%08X reqQ=%d -> %s realQ=%d lvl=%d "
                                    "forceRow=%d gotRow=%d stamps=%d%s\n",
                                    peekKind, peekIdx, code, qual, pSpawned ? "OK" : "FAILED",
                                    realQual, gotLvl, forceRow, gotRow, stamps, verdict);
                            }
                            if (g_dtActive) {
                                if (pSpawned) g_dtSpInv++; else g_dtSpGround++;
                                char dm[176];
                                if (!pSpawned)
                                    _snprintf(dm, sizeof(dm), "[FAIL] reqQ=%d code=%08X — every quality returned NULL\n", qual, code);
                                else if (gotQual != qual)
                                    _snprintf(dm, sizeof(dm), "[DEGRADED] reqQ=%d -> gotQ=%d @lvl=%d code=%08X\n", qual, gotQual, gotLvl, code);
                                else
                                    _snprintf(dm, sizeof(dm), "[OK] Q=%d @lvl=%d code=%08X\n", gotQual, gotLvl, code);
                                DeliveryTestLog(dm);
                            }
                            Quests_ConsumePendingDrop();
                        }
                    }
                }
            }

            /* === 2.9.9 dev tool: ITEM DELIVERY STRESS TEST driver === Started by the Ctrl+V "Item Delivery Test" button. */
            if (g_cheatDeliveryTest) {
                g_cheatDeliveryTest = 0;
                g_dtActive = 1; g_dtPhase = 0; g_dtRemaining = 30; g_dtIndex = 0; g_dtNextMs = 0;
                g_dtBossIdx = 0; g_dtBossItems = 0;
                g_dtQCharm = g_dtQSet = g_dtQUniq = 0;
                g_dtSpInv = g_dtSpGround = 0;
                DeliveryTestLog("==== DELIVERY TEST START ====\n");
                DeliveryTestLog("-- Phase 0: BOSS LOOT — each of the 5 bosses drops its multi-item table --\n");
                ShowNotify("Delivery test: phase 0 (boss loot, multi-item)");
            }
            if (g_dtActive) {
                DWORD nowDt = GetTickCount();
                if (g_dtPhase == 0) {
                    /* PHASE 0 — BOSS LOOT (multi-item): trigger each boss's full loot table, ~3s apart, waiting for the previous one to fully drop. */
                    if (g_dtBossIdx < BOSS_LOOT_COUNT &&
                        g_pendingLootDrop == 0 &&
                        (g_dtNextMs == 0 || nowDt - g_dtNextMs >= 3000)) {
                        g_dtNextMs = nowDt;
                        g_pendingLootBossId = g_dtBossIdx;
                        g_pendingLootDrop   = 1;   /* one full loot table = 3-5 items at once */
                        { char m[96]; _snprintf(m, sizeof(m), "[BOSS LOOT] trigger boss #%d (%s)\n",
                            g_dtBossIdx, g_bossLootNames[g_dtBossIdx]); DeliveryTestLog(m); }
                        g_dtBossIdx++;
                    }
                    /* All 5 bosses triggered + loot fully dropped -> advance to spec-drops. */
                    if (g_dtBossIdx >= BOSS_LOOT_COUNT && g_pendingLootDrop == 0) {
                        char m[256];
                        _snprintf(m, sizeof(m),
                            "-- Phase 0 done: boss loot = %d items across %d bosses (multi-item OK). --\n"
                            "-- Phase 1: 30 single spec-drops (charm / set / unique) --\n",
                            g_dtBossItems, BOSS_LOOT_COUNT);
                        DeliveryTestLog(m);
                        ShowNotify("Delivery test: phase 1 (single item drops)");
                        g_dtPhase = 1; g_dtNextMs = 0;
                    }
                } else if (g_dtPhase == 1) {
                    /* PHASE 1 — single spec-drops (charm/set/unique), ~3s apart. */
                    if (g_dtRemaining > 0 && (g_dtNextMs == 0 || nowDt - g_dtNextMs >= 3000)) {
                        g_dtNextMs = nowDt;
                        int n = 30 - g_dtRemaining + 1;
                        int sub = g_dtIndex / 3;   /* within-type sequence so each type varies */
                        int kind, idx; const char* typ;
                        switch (g_dtIndex % 3) {
                            case 0:  kind = REWARD_DROP_CHARM;  idx = sub % 3;    typ = "CHARM";  g_dtQCharm++; break;
                            case 1:  kind = REWARD_DROP_SET;    idx = sub % 127;  typ = "SET";    g_dtQSet++;   break;
                            default: kind = REWARD_DROP_UNIQUE;
                                     idx = (g_uniqueCatalogCount > 0) ? (sub % g_uniqueCatalogCount) : 0;
                                     typ = "UNIQUE"; g_dtQUniq++; break;
                        }
                        Quests_QueueSpecificDrop(kind, idx, "DELIVERY TEST");
                        { char m[96]; _snprintf(m, sizeof(m), "[QUEUE #%d] %s idx=%d\n", n, typ, idx); DeliveryTestLog(m); }
                        g_dtIndex++; g_dtRemaining--;
                    }
                    /* Spec-drops done + queue drained -> final summary. */
                    if (g_dtRemaining == 0) {
                        int pk, pi;
                        if (!Quests_PeekPendingDrop(&pk, &pi)) {
                            char m[288];
                            _snprintf(m, sizeof(m),
                                "==== DELIVERY TEST DONE. Boss loot = %d items across %d bosses (multi-item). "
                                "Spec-drops: queued charm=%d set=%d unique=%d (=%d); spawned inv=%d ground=%d (=%d). "
                                "Compare to what appeared in-game. ====\n",
                                g_dtBossItems, BOSS_LOOT_COUNT,
                                g_dtQCharm, g_dtQSet, g_dtQUniq, g_dtQCharm + g_dtQSet + g_dtQUniq,
                                g_dtSpInv, g_dtSpGround, g_dtSpInv + g_dtSpGround);
                            DeliveryTestLog(m);
                            ShowNotify("Delivery test complete — see d2arch_delivery_test.txt");
                            g_dtActive = 0;
                        }
                    }
                }
            }

            /* === Drop specific boss loot table (debug menu) === 2.10.0 — route the cheat trigger through the SAME real boss-loot path (g_pendingLootDrop) so the dev menu exercises the new MF-scaled quality drop instead of a separate fnDropTC code path. */
            if (g_cheatDropLoot > 0 && g_cachedPGame && hD2Game) {
                int bossIdx = g_cheatDropBossId;
                if (bossIdx < 0 || bossIdx >= BOSS_LOOT_COUNT) bossIdx = 2; /* default Mephisto */
                g_pendingLootBossId = bossIdx;
                if (g_pendingLootDrop < 1) g_pendingLootDrop = 1;
                Log("CHEAT LOOT: queued boss=%s via real MF-scaled drop path\n",
                    g_bossLootNames[bossIdx]);
                g_cheatDropLoot = 0;
            }

            /* Drop runeword test items via QUESTS_CreateItem. */
            if ((g_cheatTestRunewords > 0 || g_cheatTestRunes > 0 || g_cheatTestBases > 0)
                && g_cachedPGame && hD2Game) {
                typedef void* (__fastcall *QUESTS_CreateItem_t)(
                    void* pGame, void* pPlayer,
                    DWORD dwCode, int nLevel, BYTE nQuality, int bDroppable);
                QUESTS_CreateItem_t fnQuestsCreateItem =
                    (QUESTS_CreateItem_t)((DWORD)hD2Game + 0x65DF0);

                /* Three item lists, picked based on which flag triggered. */
                struct ItemEntry { const char* code3; int qty; const char* name; };

                static const struct ItemEntry RUNES[] = {
                    { "box", 1, "Horadric Cube"   },
                    { "r01", 1, "El"              },
                    { "r02", 1, "Eld"             },
                    { "r03", 1, "Tir"             },
                    { "r04", 1, "Nef"             },
                    { "r05", 1, "Eth"             },
                    { "r06", 1, "Ith"             },
                    { "r07", 1, "Tal"             },
                    { "r08", 1, "Ral"             },
                    { "r09", 1, "Ort"             },
                    { "r10", 2, "Thul"            },
                    { "r11", 2, "Amn"             },
                    { "r12", 2, "Sol"             },
                    { "r13", 2, "Shael"           },
                    { "r14", 1, "Dol"             },
                    { "r15", 1, "Hel"             },
                    { "r16", 1, "Io"              },
                    { "r17", 1, "Lum"             },
                    { "r18", 1, "Ko"              },
                    { "r19", 1, "Fal"             },
                    { "r20", 1, "Lem"             },
                    { "r21", 1, "Pul"             },
                    { "r22", 2, "Um"              },
                    { "r23", 1, "Mal"             },
                    { "r24", 1, "Ist"             },
                    { "r25", 1, "Gul"             },
                    { "r26", 1, "Vex"             },
                    { "r27", 1, "Ohm"             },
                    { "r28", 1, "Lo"              },
                    { "r29", 1, "Sur"             },
                    { "r30", 1, "Ber"             },
                    { "r31", 1, "Jah"             },
                    { "r32", 1, "Cham"            },
                    { "r33", 1, "Zod"             },
                    { NULL, 0, NULL }
                };

                static const struct ItemEntry BASES[] = {
                    { "ltp", 5, "Light Plate"     },  /* body armor */
                    { "crn", 5, "Crown"           },  /* helm */
                    { "lsd", 3, "Long Sword"      },  /* sword */
                    { "ces", 2, "Cestus"          },  /* claw */
                    { "lbw", 1, "Long Bow"        },  /* bow */
                    { "lst", 1, "Long Staff"      },  /* staff */
                    { NULL, 0, NULL }
                };

                static const struct ItemEntry POTS[] = {
                    { "vps", 5, "Stamina Pot"     },  /* -> 3-socket Light Plate */
                    { "wms", 5, "Thawing Pot"     },  /* -> 3-socket Crown */
                    { NULL, 0, NULL }
                };

                const struct ItemEntry* items = NULL;
                const char* batchName = "?";
                if (g_cheatTestRunes > 0)        { items = RUNES; batchName = "RUNES"; g_cheatTestRunes = 0; }
                else if (g_cheatTestBases > 0)   { items = BASES; batchName = "BASES"; g_cheatTestBases = 0; }
                else if (g_cheatTestRunewords > 0){ items = POTS;  batchName = "POTS";  g_cheatTestRunewords = 0; }

                Log("CHEAT TEST RW [%s]: pGame=%p pPlayer=%p fnCreateItem=%p\n",
                    batchName, (void*)g_cachedPGame, pCurseTarget, fnQuestsCreateItem);

                /* D2's DATATBLS_GetItemRecordFromItemCode uses space-padded 4-char codes (e.g. */
                int spawned = 0, failed = 0;
                if (!items) goto rw_done;
                for (int i = 0; items[i].code3; i++) {
                    DWORD code_null  = ((DWORD)(BYTE)items[i].code3[0])
                                     | ((DWORD)(BYTE)items[i].code3[1] << 8)
                                     | ((DWORD)(BYTE)items[i].code3[2] << 16);
                    DWORD code_space = code_null | ((DWORD)0x20 << 24);
                    for (int q = 0; q < items[i].qty; q++) {
                        void* pItem = NULL;
                        __try {
                            pItem = fnQuestsCreateItem((void*)g_cachedPGame, pCurseTarget,
                                                       code_space, 50, 2, 1);
                            if (!pItem) {
                                /* fallback to null-padding */
                                pItem = fnQuestsCreateItem((void*)g_cachedPGame, pCurseTarget,
                                                           code_null, 50, 2, 1);
                            }
                        } __except(EXCEPTION_EXECUTE_HANDLER) {
                            Log("CHEAT TEST RW: %s EXCEPTION\n", items[i].name);
                            break;
                        }
                        if (pItem) {
                            spawned++;
                        } else {
                            failed++;
                            if (failed <= 5) {
                                Log("CHEAT TEST RW: %s code_space=%08X code_null=%08X "
                                    "BOTH returned NULL\n",
                                    items[i].name, code_space, code_null);
                            }
                        }
                    }
                }
                Log("CHEAT TEST RW [%s]: spawned=%d failed=%d total=%d\n",
                    batchName, spawned, failed, spawned+failed);
            rw_done: ;
            }

            /* Heal full — top up HP, MP, Stamina to max via fnAddStat. */
            if (g_cheatHealFull > 0 && fnGetStat && fnAddStat) {
                __try {
                    int curHp = fnGetStat(pCurseTarget, 6, 0);
                    int maxHp = fnGetStat(pCurseTarget, 7, 0);
                    if (maxHp > curHp) fnAddStat(pCurseTarget, 6, maxHp - curHp, 0);
                    int curMp = fnGetStat(pCurseTarget, 8, 0);
                    int maxMp = fnGetStat(pCurseTarget, 9, 0);
                    if (maxMp > curMp) fnAddStat(pCurseTarget, 8, maxMp - curMp, 0);
                    int curSt = fnGetStat(pCurseTarget, 10, 0);
                    int maxSt = fnGetStat(pCurseTarget, 11, 0);
                    if (maxSt > curSt) fnAddStat(pCurseTarget, 10, maxSt - curSt, 0);
                    Log("CHEAT HEAL: HP %d/%d  MP %d/%d  Sta %d/%d -> full\n",
                        curHp, maxHp, curMp, maxMp, curSt, maxSt);
                } __except(1) { Log("CHEAT HEAL: exception\n"); }
                g_cheatHealFull = 0;
            }

            /* Unified item-drop dispatch via g_cheatItemCmd. */
            if (g_cheatItemCmd > 0 && g_cachedPGame && hD2Game) {
                typedef void* (__fastcall *QUESTS_CreateItem_t)(
                    void* pGame, void* pPlayer,
                    DWORD dwCode, int nLevel, BYTE nQuality, int bDroppable);
                QUESTS_CreateItem_t fnCI =
                    (QUESTS_CreateItem_t)((DWORD)hD2Game + 0x65DF0);

                struct ItemEntry2 { const char* code3; int qty; };

                /* All rune codes r01..r33 */
                static const struct ItemEntry2 RUNES_LO[]   = {
                    {"r01",1},{"r02",1},{"r03",1},{"r04",1},{"r05",1},
                    {"r06",1},{"r07",1},{"r08",1},{"r09",1},{"r10",2}, {NULL,0}
                };
                static const struct ItemEntry2 RUNES_MID[]  = {
                    {"r11",2},{"r12",2},{"r13",2},{"r14",1},{"r15",1},
                    {"r16",1},{"r17",1},{"r18",1},{"r19",1},{"r20",1}, {NULL,0}
                };
                static const struct ItemEntry2 RUNES_HI[]   = {
                    {"r21",1},{"r22",2},{"r23",1},{"r24",1},{"r25",1},
                    {"r26",1},{"r27",1},{"r28",1},{"r29",1},{"r30",1},
                    {"r31",1},{"r32",1},{"r33",1}, {NULL,0}
                };
                static const struct ItemEntry2 RUNES_ALL[]  = {
                    {"r01",1},{"r02",1},{"r03",1},{"r04",1},{"r05",1},
                    {"r06",1},{"r07",1},{"r08",1},{"r09",1},{"r10",2},
                    {"r11",2},{"r12",2},{"r13",2},{"r14",1},{"r15",1},
                    {"r16",1},{"r17",1},{"r18",1},{"r19",1},{"r20",1},
                    {"r21",1},{"r22",2},{"r23",1},{"r24",1},{"r25",1},
                    {"r26",1},{"r27",1},{"r28",1},{"r29",1},{"r30",1},
                    {"r31",1},{"r32",1},{"r33",1}, {NULL,0}
                };
                static const struct ItemEntry2 BASE_BODY[]  = {
                    {"ltp",3},{"ful",2},{"aar",2},{"hpl",1}, {NULL,0}
                };
                static const struct ItemEntry2 BASE_HELM[]  = {
                    {"crn",3},{"msk",2},{"sak",2},{"hlm",2}, {NULL,0}
                };
                static const struct ItemEntry2 BASE_WEAP[]  = {
                    {"lsd",2},{"flb",2},{"ces",2},{"lbw",2},
                    {"lst",2},{"mau",2},{"hax",2},{"spr",2}, {NULL,0}
                };
                static const struct ItemEntry2 BASE_ALL[]   = {
                    {"ltp",2},{"ful",1},{"aar",1},{"hpl",1},
                    {"crn",2},{"msk",1},{"sak",1},{"hlm",1},
                    {"lsd",1},{"flb",1},{"ces",1},{"lbw",1},
                    {"lst",1},{"mau",1},{"hax",1},{"spr",1}, {NULL,0}
                };
                /* 35 gems: 7 colors x 5 grades */
                static const struct ItemEntry2 GEMS_ALL[] = {
                    {"gcv",1},{"gfv",1},{"gsv",1},{"gzv",1},{"gpv",1},  /* Amethyst */
                    {"gcy",1},{"gfy",1},{"gsy",1},{"gly",1},{"gpy",1},  /* Topaz */
                    {"gcb",1},{"gfb",1},{"gsb",1},{"glb",1},{"gpb",1},  /* Sapphire */
                    {"gcg",1},{"gfg",1},{"gsg",1},{"glg",1},{"gpg",1},  /* Emerald */
                    {"gcr",1},{"gfr",1},{"gsr",1},{"glr",1},{"gpr",1},  /* Ruby */
                    {"gcw",1},{"gfw",1},{"gsw",1},{"glw",1},{"gpw",1},  /* Diamond */
                    {"skc",1},{"skf",1},{"sku",1},{"skl",1},{"skz",1},  /* Skull */
                    {NULL,0}
                };
                /* hp1..hp5 = Minor/Light/Healing/Greater/Super */
                static const struct ItemEntry2 POTS_HEAL[] = {
                    {"hp1",2},{"hp2",2},{"hp3",2},{"hp4",2},{"hp5",4},
                    {"rvs",2},{"rvl",2}, {NULL,0}
                };
                static const struct ItemEntry2 POTS_MANA[] = {
                    {"mp1",2},{"mp2",2},{"mp3",2},{"mp4",2},{"mp5",4},
                    {"rvs",2},{"rvl",2}, {NULL,0}
                };
                static const struct ItemEntry2 POTS_RECIPE[] = {
                    {"box",1},{"vps",5},{"wms",5}, {NULL,0}
                };
                /* Cube alone */
                static const struct ItemEntry2 CUBE_ONLY[] = {
                    {"box",1}, {NULL,0}
                };
                /* Pandemonium event items (1.13c codes) */
                static const struct ItemEntry2 PAND_KEYS[] = {
                    {"pk1",1},{"pk2",1},{"pk3",1}, {NULL,0}
                };
                static const struct ItemEntry2 PAND_ORGANS[] = {
                    {"bey",1},{"mbr",1},{"dhn",1}, {NULL,0}
                };
                static const struct ItemEntry2 PAND_ESSENCES[] = {
                    {"tes",1},{"ceh",1},{"bet",1},{"fed",1}, {NULL,0}
                };
                /* Hellfire Torch is a unique cm2 charm — base spawns normal, but with quality 7 (unique) D2 may pick a unique variant. */
                static const struct ItemEntry2 PAND_TORCH[] = {
                    {"cm2",1}, {NULL,0}
                };
                static const struct ItemEntry2 PAND_TOKEN[] = {
                    {"toa",1}, {NULL,0}
                };

                const struct ItemEntry2* list = NULL;
                const char* batchName = "?";
                switch (g_cheatItemCmd) {
                    case 1:  list = CUBE_ONLY;   batchName = "CUBE";       break;
                    case 2:  list = RUNES_LO;    batchName = "RUNES_LO";   break;
                    case 3:  list = RUNES_MID;   batchName = "RUNES_MID";  break;
                    case 4:  list = RUNES_HI;    batchName = "RUNES_HI";   break;
                    case 5:  list = RUNES_ALL;   batchName = "RUNES_ALL";  break;
                    case 6:  list = BASE_BODY;   batchName = "BASE_BODY";  break;
                    case 7:  list = BASE_HELM;   batchName = "BASE_HELM";  break;
                    case 8:  list = BASE_WEAP;   batchName = "BASE_WEAP";  break;
                    case 9:  list = BASE_ALL;    batchName = "BASE_ALL";   break;
                    case 10: list = GEMS_ALL;    batchName = "GEMS";       break;
                    case 11: list = POTS_HEAL;   batchName = "POTS_HEAL";  break;
                    case 12: list = POTS_MANA;   batchName = "POTS_MANA";  break;
                    case 13: list = POTS_RECIPE; batchName = "POTS_RECIPE";break;
                    /* Pandemonium event items */
                    case 40: list = PAND_KEYS;     batchName = "PAND_KEYS";    break;
                    case 41: list = PAND_ORGANS;   batchName = "PAND_ORGANS";  break;
                    case 42: list = PAND_ESSENCES; batchName = "PAND_ESSENCES";break;
                    case 43: list = PAND_TORCH;    batchName = "PAND_TORCH";   break;
                    case 44: list = PAND_TOKEN;    batchName = "PAND_TOKEN";   break;
                    default: break;
                }

                if (list) {
                    Log("CHEAT ITEM [%s]: pGame=%p pPlayer=%p fnCI=%p\n",
                        batchName, (void*)g_cachedPGame, pCurseTarget, fnCI);
                    int spawned = 0, failed = 0;
                    /* Hellfire Torch + Token of Absolution: spawn at item level 99 with quality 7 (unique) so the cm2 charm rolls as the unique Hellfire Torch instead of a normal Charm Large. */
                    BOOL forceUnique = (g_cheatItemCmd == 43);  /* PAND_TORCH */
                    int  spawnLvl    = forceUnique ? 99 : 50;
                    BYTE spawnQual   = forceUnique ? 7  : 2;
                    for (int i = 0; list[i].code3; i++) {
                        DWORD code_null = ((DWORD)(BYTE)list[i].code3[0])
                                        | ((DWORD)(BYTE)list[i].code3[1] << 8)
                                        | ((DWORD)(BYTE)list[i].code3[2] << 16);
                        DWORD code_space = code_null | ((DWORD)0x20 << 24);
                        for (int q = 0; q < list[i].qty; q++) {
                            void* pItem = NULL;
                            __try {
                                pItem = fnCI((void*)g_cachedPGame, pCurseTarget,
                                             code_space, spawnLvl, spawnQual, 1);
                                if (!pItem) {
                                    pItem = fnCI((void*)g_cachedPGame, pCurseTarget,
                                                 code_null, spawnLvl, spawnQual, 1);
                                }
                            } __except(EXCEPTION_EXECUTE_HANDLER) {
                                Log("CHEAT ITEM: %s EXCEPTION\n", list[i].code3);
                                break;
                            }
                            if (pItem) spawned++;
                            else {
                                failed++;
                                if (failed <= 5) {
                                    Log("CHEAT ITEM: %s NULL "
                                        "(code_space=%08X code_null=%08X)\n",
                                        list[i].code3, code_space, code_null);
                                }
                            }
                        }
                    }
                    Log("CHEAT ITEM [%s]: spawned=%d failed=%d\n",
                        batchName, spawned, failed);
                }
                g_cheatItemCmd = 0;
            }

            /* Individual-item dispatch from the new Loot tab in the Ctrl+V dev menu. */
            if (g_cheatSpecificSetIdx >= 0 && g_cachedPGame) {
                int idx = g_cheatSpecificSetIdx;
                Quests_QueueSpecificDrop(REWARD_DROP_SET, idx, "cheat menu");
                Log("CHEAT LOOT: queued SET idx=%d (%s)\n",
                    idx, Quests_SetPieceName(idx));
                g_cheatSpecificSetIdx = -1;
            }
            if (g_cheatSpecificUniqueIdx >= 0 && g_cachedPGame) {
                int idx = g_cheatSpecificUniqueIdx;
                Quests_QueueSpecificDrop(REWARD_DROP_UNIQUE, idx, "cheat menu");
                Log("CHEAT LOOT: queued UNIQUE idx=%d (%s)\n",
                    idx, Quests_UniqueName(idx));
                g_cheatSpecificUniqueIdx = -1;
            }
            if (g_cheatSingleItemCode[0] && g_cachedPGame && hD2Game) {
                typedef void* (__fastcall *QUESTS_CreateItem_t)(
                    void* pGame, void* pPlayer,
                    DWORD dwCode, int nLevel, BYTE nQuality, int bDroppable);
                QUESTS_CreateItem_t fnCI =
                    (QUESTS_CreateItem_t)((DWORD)hD2Game + 0x65DF0);
                char c0 = g_cheatSingleItemCode[0];
                char c1 = g_cheatSingleItemCode[1];
                char c2 = g_cheatSingleItemCode[2];
                BYTE qual = g_cheatSingleItemQuality;
                int  lvl  = g_cheatSingleItemLvl;
                DWORD code_null  = ((DWORD)(BYTE)c0)
                                 | ((DWORD)(BYTE)c1 << 8)
                                 | ((DWORD)(BYTE)c2 << 16);
                DWORD code_space = code_null | ((DWORD)0x20 << 24);
                void* pItem = NULL;
                __try {
                    /* bDroppable=1 — the SAME value every reward path uses. */
                    pItem = fnCI((void*)g_cachedPGame, pCurseTarget,
                                 code_space, lvl, qual, 1);
                    if (!pItem) {
                        pItem = fnCI((void*)g_cachedPGame, pCurseTarget,
                                     code_null, lvl, qual, 1);
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Log("CHEAT LOOT: %c%c%c EXCEPTION\n", c0, c1, c2);
                }
                Log("CHEAT LOOT: spawned single %c%c%c lvl=%d qual=%d -> %s\n",
                    c0, c1, c2, lvl, qual, pItem ? "OK" : "NULL");
                g_cheatSingleItemCode[0] = 0;
            }

            /* Pandemonium uber boss spawn dispatch. */
            if (g_cheatSpawnUber > 0 && g_cachedPGame && fnSpawnMonster) {
                /* (vanilla monster ID, custom SU index) per cheat cmd */
                static const int UBER_VANILLA_IDS[6] = {156, 211, 256, 242, 243, 544};
                static const int UBER_SU_INDICES[6] = { 70,  71,  72,  67,  68,  69};
                static const char* UBER_NAMES[6] = {
                    "Lilith", "Uber Duriel", "Uber Izual",
                    "Uber Mephisto", "Uber Diablo", "Uber Baal"
                };
                /* Each entry: (cmd-1) idx -> (vanilla monId, su index) */
                int monIdList[3] = {-1,-1,-1};
                int suIdxList[3] = {-1,-1,-1};
                int spawnCount = 0;
                int cmd = g_cheatSpawnUber;
                if (cmd >= 1 && cmd <= 6) {
                    monIdList[0] = UBER_VANILLA_IDS[cmd - 1];
                    suIdxList[0] = UBER_SU_INDICES[cmd - 1];
                    spawnCount = 1;
                } else if (cmd == 7) {
                    /* Mini uber trio: Lilith, Duriel, Izual */
                    for (int i = 0; i < 3; i++) {
                        monIdList[i] = UBER_VANILLA_IDS[i];
                        suIdxList[i] = UBER_SU_INDICES[i];
                    }
                    spawnCount = 3;
                } else if (cmd == 8) {
                    /* Final trio: Mephisto, Diablo, Baal */
                    for (int i = 0; i < 3; i++) {
                        monIdList[i] = UBER_VANILLA_IDS[i + 3];
                        suIdxList[i] = UBER_SU_INDICES[i + 3];
                    }
                    spawnCount = 3;
                }

                if (spawnCount > 0 && pCurseTarget) {
                    DWORD pPath2 = 0; DWORD pRoom2 = 0; int playerX = 0, playerY = 0;
                    __try {
                        pPath2 = *(DWORD*)((DWORD)pCurseTarget + 0x2C);
                        if (pPath2) {
                            pRoom2 = *(DWORD*)(pPath2 + 0x1C);
                            playerX = (int)*(unsigned short*)(pPath2 + 0x02);
                            playerY = (int)*(unsigned short*)(pPath2 + 0x06);
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) { pRoom2 = 0; }

                    if (pRoom2) {
                        Log("CHEAT UBER SPAWN: cmd=%d spawnCount=%d player=(%d,%d) pRoom=%08X\n",
                            cmd, spawnCount, playerX, playerY, pRoom2);
                        for (int i = 0; i < spawnCount; i++) {
                            int monId = monIdList[i];
                            int suIdx = suIdxList[i];
                            /* Spread bosses around player */
                            int offsets[3][2] = {{6, 0}, {-5, 4}, {-3, -5}};
                            int spX = playerX + offsets[i][0];
                            int spY = playerY + offsets[i][1];
                            void* pMon = NULL;

                            /* Spawn vanilla boss */
                            __try {
                                pMon = fnSpawnMonster((void*)g_cachedPGame, (void*)pRoom2,
                                                      spX, spY, monId, 1, -1, 0);
                            } __except(EXCEPTION_EXECUTE_HANDLER) { pMon = NULL; }
                            if (!pMon) {
                                __try {
                                    pMon = fnSpawnMonster((void*)g_cachedPGame, (void*)pRoom2,
                                                          spX, spY, monId, 0, 4, 0);
                                } __except(EXCEPTION_EXECUTE_HANDLER) { pMon = NULL; }
                            }

                            /* NOTE: previous attempt to memory-hack the spawned monster's nTypeFlag (SUPERUNIQUE) + wBossHcIdx to point at custom SU rows (indices 67-72) caused ACCESS_VIOLATION because D2's SU table is fixed-size (66 entries in 1.10f) and reading out-of-bounds crashes. */
                            (void)suIdx;  /* unused — see comment above */

                            Log("CHEAT UBER SPAWN: %s monId=%d at (%d,%d) -> pMon=%p\n",
                                UBER_NAMES[(cmd >= 1 && cmd <= 6) ? cmd - 1
                                          : (cmd == 7 ? i : i + 3)],
                                monId, spX, spY, pMon);
                        }
                    } else {
                        Log("CHEAT UBER SPAWN: pRoom NULL — cannot spawn\n");
                    }
                }
                g_cheatSpawnUber = 0;
            }

            /* "Gate Boss (next)" cheat: spawn the gate-boss SuperUniques rows one at a time so a tester can verify all 44 appended rows actually spawn, carry the right monster + gold name, and log correctly. */
            if (g_cheatSpawnGateBoss > 0 && g_cachedPGame && pCurseTarget) {
                extern int  CustomBoss_GateHcFromVanilla(int vanillaHc);
                extern int  CustomBoss_SuRowFromHcIdx(int hcCol);
                static int s_gateCheatCursor = 0;
                int req = g_cheatSpawnGateBoss;
                g_cheatSpawnGateBoss = 0;
                if (req == 2) {
                    s_gateCheatCursor = 0;
                    ShowNotify("Gate-boss test counter reset (next = #1)");
                } else {
                    /* Pairing order mirrors s_gatePairs in d2arch_customboss.c */
                    static const short vanillaList[44] = {
                        0,1,2,3,4,5,6,7,9,10,11,12,13,14,15,16,17,19,20,21,22,23,
                        24,25,32,33,34,35,40,42,46,47,48,49,50,51,52,53,54,55,56,57,58,59 };
                    int vh     = vanillaList[s_gateCheatCursor % 44];
                    int gateHc = CustomBoss_GateHcFromVanilla(vh);
                    int row    = (gateHc >= 0) ? CustomBoss_SuRowFromHcIdx(gateHc) : -1;
                    void* pMon = NULL;
                    DWORD pPathG = 0, pRoomG = 0; int gx = 0, gy = 0;
                    __try {
                        pPathG = *(DWORD*)((DWORD)pCurseTarget + 0x2C);
                        if (pPathG) {
                            pRoomG = *(DWORD*)(pPathG + 0x1C);
                            gx = (int)*(unsigned short*)(pPathG + 0x02);
                            gy = (int)*(unsigned short*)(pPathG + 0x06);
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) { pRoomG = 0; }
                    if (row >= 0 && pRoomG && fnSpawnSuperUnique) {
                        __try { pMon = fnSpawnSuperUnique((void*)g_cachedPGame,
                                                          (void*)pRoomG, gx + 5, gy + 3, row); }
                        __except(EXCEPTION_EXECUTE_HANDLER) { pMon = NULL; }
                    }
                    {
                        const char* nm = Preload_SuperUniqueName(vh, "?");
                        char nb[128];
                        _snprintf(nb, sizeof(nb), "Gate #%d/44 %s hc=%d row=%d -> %s",
                                  (s_gateCheatCursor % 44) + 1, nm ? nm : "?",
                                  gateHc, row, pMon ? "OK" : "FAILED");
                        nb[sizeof(nb) - 1] = 0;
                        ShowNotify(nb);
                        Log("CHEAT GATE BOSS: idx=%d vanillaHc=%d gateHc=%d row=%d pRoom=%08X -> pMon=%p\n",
                            s_gateCheatCursor % 44, vh, gateHc, row, pRoomG, pMon);
                    }
                    s_gateCheatCursor++;
                }
            }

            /* Specific-monster spawn dispatch for the Mons tab in the Ctrl+V menu. */
            if (g_cheatSpawnSuperUniqueIdx >= 0 && g_cachedPGame
                    && fnSpawnSuperUnique && pCurseTarget) {
                int suIdx = g_cheatSpawnSuperUniqueIdx;
                __try {
                    DWORD pPath = *(DWORD*)((DWORD)pCurseTarget + 0x2C);
                    if (pPath) {
                        DWORD pRoom = *(DWORD*)(pPath + 0x1C);
                        int x = (int)*(unsigned short*)(pPath + 0x02) + 4;
                        int y = (int)*(unsigned short*)(pPath + 0x06);
                        if (pRoom) {
                            void* pMon = NULL;
                            __try {
                                pMon = fnSpawnSuperUnique((void*)g_cachedPGame,
                                                          (void*)pRoom, x, y, suIdx);
                            } __except(EXCEPTION_EXECUTE_HANDLER) { pMon = NULL; }
                            Log("CHEAT MONS: SuperUnique idx=%d at (%d,%d) -> pMon=%p\n",
                                suIdx, x, y, pMon);
                        } else {
                            Log("CHEAT MONS: SU spawn skipped, pRoom NULL\n");
                        }
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Log("CHEAT MONS: SU spawn EXCEPTION (idx=%d)\n", suIdx);
                }
                g_cheatSpawnSuperUniqueIdx = -1;
            }

            if (g_cheatSpawnMonsterRowId >= 0 && g_cachedPGame
                    && fnSpawnMonster && pCurseTarget) {
                int monId = g_cheatSpawnMonsterRowId;
                __try {
                    DWORD pPath = *(DWORD*)((DWORD)pCurseTarget + 0x2C);
                    if (pPath) {
                        DWORD pRoom = *(DWORD*)(pPath + 0x1C);
                        int x = (int)*(unsigned short*)(pPath + 0x02) + 4;
                        int y = (int)*(unsigned short*)(pPath + 0x06);
                        if (pRoom) {
                            void* pMon = NULL;
                            __try {
                                pMon = fnSpawnMonster((void*)g_cachedPGame,
                                                      (void*)pRoom, x, y,
                                                      monId, 1, -1, 0);
                                if (!pMon) {
                                    pMon = fnSpawnMonster((void*)g_cachedPGame,
                                                          (void*)pRoom, x, y,
                                                          monId, 0, 4, 0);
                                }
                            } __except(EXCEPTION_EXECUTE_HANDLER) { pMon = NULL; }
                            Log("CHEAT MONS: Monster row=%d at (%d,%d) -> pMon=%p\n",
                                monId, x, y, pMon);
                        } else {
                            Log("CHEAT MONS: Monster spawn skipped, pRoom NULL\n");
                        }
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Log("CHEAT MONS: Monster spawn EXCEPTION (row=%d)\n", monId);
                }
                g_cheatSpawnMonsterRowId = -1;
            }
        }
    }
}

static void __declspec(naked) GameUpdateHook(void) {
    __asm {
        pushad
        call ProcessPendingGameTick
        popad
        jmp [g_gameUpdateTrampolinePtr]
    }
}

static void GiveGold(int amount) {
    g_pendingRewardGold += amount;
    Log("GiveGold: %d added to pending rewards (total: %d)\n", amount, g_pendingRewardGold);
}

/* Deferred quest completion queue OnQuestComplete is called from the game tick (D2DebugGame hook). */
#define DEFERRED_QUEUE_SIZE 32
static volatile int g_deferredQueue[DEFERRED_QUEUE_SIZE]; /* quest IDs */
static volatile int g_deferredDiff[DEFERRED_QUEUE_SIZE];  /* difficulty */
static volatile int g_deferredCount = 0;

static void OnQuestComplete(Quest* quest) {
    if (!quest || quest->id == 0) return;
    int qid = quest->id;
    int diff = g_currentDifficulty;
    if (qid >= MAX_QUEST_ID) return;
    if (g_questCompleted[diff][qid]) return;

    /* quest-type toggle gate. */
    if (!IsQuestTypeActive((int)quest->type)) {
        Log("QUEST SKIPPED (type disabled): [%d] %s type=%d\n",
            qid, quest->name, (int)quest->type);
        return;
    }

    /* set the dedup flag IMMEDIATELY, BEFORE any reward processing. */
    g_questCompleted[diff][qid] = TRUE;
    quest->completed = TRUE;

    Log("QUEST COMPLETE (deferred): [%d] %s AP=%d\n", qid, quest->name, g_apConnected);

    /* F1 Logbook: count this quest. */
    {
        extern void Stats_OnQuestComplete(void);
        extern void Stats_OnActComplete(int difficulty, int actIdx);
        Stats_OnQuestComplete();

        /* Detect act-boss completion -> record clear-time per difficulty. */
        if (quest->type == QTYPE_BOSS) {
            int actIdx = -1;
            switch (quest->param) {
                case 36:  actIdx = 0; break; /* Andariel -> Act 1 */
                case 66:  actIdx = 1; break; /* Duriel -> Act 2 */
                case 242: actIdx = 2; break; /* Mephisto -> Act 3 */
                case 243: actIdx = 3; break; /* Diablo -> Act 4 */
                case 544: actIdx = 4; break; /* Baal -> Act 5 */
                default:                break;
            }
            if (actIdx >= 0) {
                Stats_OnActComplete(diff, actIdx);
                Log("STATS: Act %d cleared on diff=%d\n", actIdx + 1, diff);
                /* Custom goal act-boss target. */
                extern void CustomGoal_OnActBossKilled(int bossIdx, int diff);
                CustomGoal_OnActBossKilled(actIdx, diff);
            }
        }
    }

    /* Queue for deferred processing — NO file I/O here! */
    if (g_deferredCount < DEFERRED_QUEUE_SIZE) {
        g_deferredQueue[g_deferredCount] = qid;
        g_deferredDiff[g_deferredCount] = diff;
        g_deferredCount++;
    }
}

/* Process all deferred quest completions — called from WndProc (safe context) */
static void ProcessDeferredQuests(void) {
    if (g_deferredCount == 0) return;

    int count = g_deferredCount;
    g_deferredCount = 0; /* Reset before processing to avoid reentrancy */

    BOOL anyNew = FALSE;
    for (int i = 0; i < count; i++) {
        int qid = g_deferredQueue[i];
        int diff = g_deferredDiff[i];

        /* Find the quest struct */
        Quest* quest = NULL;
        for (int a = 0; a < 5 && !quest; a++)
            for (int q = 0; q < g_acts[a].num; q++)
                if (g_acts[a].quests[q].id == qid) { quest = &g_acts[a].quests[q]; break; }

        if (!quest) continue;
        anyNew = TRUE;

        Log("DEFERRED PROCESS: [%d] %s (reward=%d) AP=%d\n", qid, quest->name, quest->reward, g_apConnected);

        /* In AP mode, DON'T give rewards locally */
        if (g_apConnected) {
            /* show recipient slot if location is owned by another player. */
            const char* owner = "";
            if (diff >= 0 && diff < 3 && qid >= 0 && qid < MAX_QUEST_ID) {
                owner = g_locationOwner[diff][qid];
            }
            BOOL ownerKnown = (owner[0] != 0);
            BOOL ownerIsSelf = ownerKnown && g_apSlot[0] != 0
                               && _stricmp(owner, g_apSlot) == 0;
            char banner[160];
            if (ownerKnown && !ownerIsSelf) {
                _snprintf(banner, sizeof(banner) - 1, "%s -> %s",
                          quest->name, owner);
                banner[sizeof(banner) - 1] = 0;
                ShowNotify(banner);
                char tag[40];
                _snprintf(tag, sizeof(tag) - 1, "Sent -> %s", owner);
                tag[sizeof(tag) - 1] = 0;
                ItemLogAddA(1, 4, tag, quest->name);
                Log("AP MODE: '%s' (qid=%d diff=%d) sent to %s\n",
                    quest->name, qid, diff, owner);
            } else {
                _snprintf(banner, sizeof(banner) - 1, "Check: %s", quest->name);
                banner[sizeof(banner) - 1] = 0;
                ShowNotify(banner);
                ItemLogAddA(2, 2, "Check (self)", quest->name);
                Log("AP MODE: '%s' (qid=%d diff=%d) own check%s\n",
                    quest->name, qid, diff,
                    ownerKnown ? "" : " [owner not loaded]");
            }
        } else {
            /* Solo mode: give rewards locally. */
            if (quest->reward == REWARD_SKILL) {
                if (g_zoneLockingOn) {
                    /* Unlock zone key first if any remain */
                    int nextKey = GetNextLockedZoneKey();
                    if (nextKey >= 0) {
                        UnlockZoneKey(nextKey);
                    } else if (g_skillHuntingOn) {
                        /* All zones unlocked — give a skill instead. */
                        BOOL gaveSkill = FALSE;
                        for (int si = 0; si < g_poolCount; si++) {
                            if (!g_pool[si].unlocked) {
                                g_pool[si].unlocked = TRUE;
                                char msg[128];
                                sprintf(msg, "UNLOCKED: %s", g_skillDB[g_pool[si].dbIndex].name);
                                ShowNotify(msg);
                                gaveSkill = TRUE;
                                break;
                            }
                        }
                        if (!gaveSkill) {
                            PendAdd(&g_serverPendingSkillPts, 1);
                            ShowNotify("Reward: 1 Skill Point! (pool exhausted)");
                        }
                    }
                    /* If BOTH modes are active, also grant a bonus skill. */
                    if (g_skillHuntingOn) {
                        BOOL gaveBonus = FALSE;
                        for (int si = 0; si < g_poolCount; si++) {
                            if (!g_pool[si].unlocked) {
                                g_pool[si].unlocked = TRUE;
                                char msg2[128];
                                sprintf(msg2, "Bonus: %s", g_skillDB[g_pool[si].dbIndex].name);
                                ShowNotify(msg2);
                                Log("HYBRID bonus skill: %s\n", g_skillDB[g_pool[si].dbIndex].name);
                                gaveBonus = TRUE;
                                break;
                            }
                        }
                        if (!gaveBonus) {
                            PendAdd(&g_serverPendingSkillPts, 1);
                            ShowNotify("Bonus: 1 Skill Point! (pool exhausted)");
                            Log("HYBRID bonus skill: pool exhausted -> +1 Skill Point fallback\n");
                        }
                    }
                } else if (g_skillHuntingOn) {
                    /* Skill Hunting only: progression quests unlock SKILLS. */
                    BOOL gaveSkill = FALSE;
                    for (int si = 0; si < g_poolCount; si++) {
                        if (!g_pool[si].unlocked) {
                            g_pool[si].unlocked = TRUE;
                            char msg[128];
                            sprintf(msg, "UNLOCKED: %s", g_skillDB[g_pool[si].dbIndex].name);
                            ShowNotify(msg);
                            Log("AUTO-UNLOCK: %s (skill %d)\n", g_skillDB[g_pool[si].dbIndex].name, g_skillDB[g_pool[si].dbIndex].id);
                            gaveSkill = TRUE;
                            break;
                        }
                    }
                    if (!gaveSkill) {
                        /* Pool exhausted — grant +1 Skill Point as fallback. */
                        PendAdd(&g_serverPendingSkillPts, 1);
                        ShowNotify("Reward: 1 Skill Point! (pool exhausted)");
                        Log("SKILL REWARD: pool exhausted -> +1 Skill Point fallback (pending=%d)\n",
                            g_serverPendingSkillPts);
                        ItemLogAddA(2, 2, "+1 Skill Point (pool overflow)", quest->name);
                    }
                }
            } else {
                int fillerType = g_questRewardType[diff][qid];
                char msg[128];

                if (fillerType == REWARD_STAT) {
                    /* The amount was pre-rolled into g_questExtra so the spoiler
                     * and the hand-out cannot disagree. Older saves carry 0
                     * there, which still has to mean the historical 5. */
                    int amount = g_questExtra[diff][qid];
                    if (amount <= 0 || amount > 10) amount = 5;
                    PendAdd(&g_serverPendingStatPts, amount);
                    sprintf(msg, "Reward: %d Stat Points!", amount);
                    ShowNotify(msg);
                    Log("STAT REWARD: %d stat points (server pending: %d)\n",
                        amount, g_serverPendingStatPts);
                    char statLbl[32];
                    _snprintf(statLbl, sizeof(statLbl), "+%d Stat Points", amount);
                    ItemLogAddA(2, 7, statLbl, quest->name);
                } else if (fillerType == REWARD_SKILL) {
                    int amount = g_questExtra[diff][qid];
                    if (amount <= 0 || amount > 3) amount = 1;
                    PendAdd(&g_serverPendingSkillPts, amount);
                    sprintf(msg, "Reward: %d Skill Point%s!", amount,
                            amount == 1 ? "" : "s");
                    ShowNotify(msg);
                    Log("SKILL REWARD: %d skill point(s) (server pending: %d)\n",
                        amount, g_serverPendingSkillPts);
                    char skillLbl[32];
                    _snprintf(skillLbl, sizeof(skillLbl), "+%d Skill Point%s",
                              amount, amount == 1 ? "" : "s");
                    ItemLogAddA(2, 2, skillLbl, quest->name);
                } else if (fillerType == REWARD_TRAP) {
                    /* trap type is pre-rolled at char creation (g_questExtra[diff][qid]) so the spoiler file's promise matches what actually fires. */
                    int trapType = g_questExtra[diff][qid];
                    if (trapType < 0 || trapType >= TRAP_TYPE_COUNT) trapType = TRAP_MONSTERS;
                    switch (trapType) {
                    case TRAP_MONSTERS:
                        g_pendingTrapSpawn++;
                        sprintf(msg, "TRAP! Monsters incoming!");
                        Log("TRAP: type=MONSTERS pending=%d\n", g_pendingTrapSpawn);
                        ItemLogAddA(2, 11, "Trap: Monsters", quest->name);
                        break;
                    case TRAP_SLOW:
                        g_pendingTrapSlow++;
                        sprintf(msg, "TRAP! You feel sluggish...");
                        Log("TRAP: type=SLOW (Decrepify)\n");
                        ItemLogAddA(2, 11, "Trap: Slow", quest->name);
                        break;
                    case TRAP_WEAKEN:
                        g_pendingTrapWeaken++;
                        sprintf(msg, "TRAP! Your defenses crumble!");
                        Log("TRAP: type=WEAKEN (Amplify Damage)\n");
                        ItemLogAddA(2, 11, "Trap: Weaken", quest->name);
                        break;
                    case TRAP_POISON:
                        g_pendingTrapPoison++;
                        sprintf(msg, "TRAP! Poison seeps into your veins!");
                        Log("TRAP: type=POISON\n");
                        ItemLogAddA(2, 11, "Trap: Poison", quest->name);
                        break;
                    case TRAP_BARRELS:
                        g_pendingTrapBarrels++;
                        sprintf(msg, "TRAP! Barrels!");
                        Log("TRAP: type=BARRELS\n");
                        ItemLogAddA(2, 11, "Trap: Barrel Field", quest->name);
                        break;
                    case TRAP_URNS:
                        g_pendingTrapUrns++;
                        sprintf(msg, "TRAP! Urns everywhere!");
                        Log("TRAP: type=URNS\n");
                        ItemLogAddA(2, 11, "Trap: Urn Garden", quest->name);
                        break;
                    case TRAP_TREASURE:
                        g_pendingTrapTreasure++;
                        sprintf(msg, "TRAP! Treasure... maybe.");
                        Log("TRAP: type=FALSE TREASURE\n");
                        ItemLogAddA(2, 11, "Trap: False Treasure", quest->name);
                        break;
                    case TRAP_JUNK:
                        g_pendingTrapJunk++;
                        sprintf(msg, "TRAP! Your pack fills with junk!");
                        Log("TRAP: type=JUNK BACKPACK\n");
                        ItemLogAddA(2, 11, "Trap: Junk Backpack", quest->name);
                        break;
                    }
                    ShowNotify(msg);
                } else if (fillerType == REWARD_LOOT) {
                    /* boss is pre-rolled. The drop site (REWARD_LOOT block above) reads g_pendingLootBossId to pick the specific TC instead of randomizing at drop time. */
                    int bossId = g_questExtra[diff][qid];
                    if (bossId < 0 || bossId >= BOSS_LOOT_COUNT) bossId = 2; /* default Mephisto */
                    g_pendingLootDrop++;
                    g_pendingLootBossId = bossId;  /* hint for the drop loop */
                    char lootMsg[64];
                    _snprintf(lootMsg, sizeof(lootMsg), "Drop: %s Loot", g_bossLootNames[bossId]);
                    Log("LOOT DROP queued: boss=%s pending=%d\n", g_bossLootNames[bossId], g_pendingLootDrop);
                    ItemLogAddA(2, 9, lootMsg, quest->name);
                } else if (fillerType == REWARD_RESETPT) {
                    int amount = g_questExtra[diff][qid];
                    if (amount <= 0 || amount > 3) amount = 1;
                    g_resetPoints += amount;
                    char resetMsg[48];
                    _snprintf(resetMsg, sizeof(resetMsg), "%d Reset Point%s earned!",
                              amount, amount == 1 ? "" : "s");
                    ShowNotify(resetMsg);
                    Log("RESET POINT earned: +%d, total %d\n", amount, g_resetPoints);
                    char resetLbl[32];
                    _snprintf(resetLbl, sizeof(resetLbl), "+%d Reset Point%s",
                              amount, amount == 1 ? "" : "s");
                    ItemLogAddA(2, 2, resetLbl, quest->name);
                } else if (fillerType == REWARD_XP) {
                    /* XP injection via fnAddStat (statId 13). */
                    /* one grant = one level. */
                    PendAdd(&g_serverPendingXpLevels, 1);
                    sprintf(msg, "Reward: one level of XP!");
                    ShowNotify(msg);
                    Log("XP REWARD: queued one level\n");
                    ItemLogAddA(2, 7, "+1 level of XP", quest->name);
                } else if (fillerType == REWARD_DROP_CHARM ||
                           fillerType == REWARD_DROP_SET ||
                           fillerType == REWARD_DROP_UNIQUE ||
                           fillerType == REWARD_DROP_BATCH) {
                    /* queue a specific item drop. */
                    Quests_QueueSpecificDrop(fillerType, g_questExtra[diff][qid], quest->name);
                } else {
                    /* REWARD_GOLD (or anything unrecognized — falls through to gold for safety so a bad enum value doesn't drop a quest reward silently). */
                    /* level-scaled gold, amount decided at drain. */
                    PendAdd(&g_serverPendingGoldLevels, 1);
                    sprintf(msg, "Reward: gold!");
                    ShowNotify(msg);
                    Log("GOLD REWARD: queued one level-scaled pile\n");
                    ItemLogAddA(2, 4, "+gold", quest->name);
                }
            }
        }
    }

    /* File I/O only ONCE after all deferred quests processed — safe in WndProc context */
    if (anyNew) {
        SaveStateFile();
        WriteChecksFile();

        /* Check if any completed quest matches the goal. */
        if (!g_apGoalComplete) {
            int goalQid = (g_apGoalScope >= 0 && g_apGoalScope <= 4) ?
                          g_goalQuestIds[g_apGoalScope] : g_goalQuestIds[4];
            for (int gi = 0; gi < count; gi++) {
                if (g_deferredQueue[gi] == goalQid && g_deferredDiff[gi] == g_apDiffScope) {
                    g_apGoalComplete = TRUE;
                    char dir2[MAX_PATH], gpath[MAX_PATH];
                    GetArchDir(dir2, MAX_PATH);
                    sprintf(gpath, "%sap_goal.dat", dir2);
                    FILE* gf = fopen(gpath, "w");
                    if (gf) { fprintf(gf, "goal=complete\n"); fclose(gf); }
                    ApPipe_SendGoal(); /* V2 pipe transport (no-op in V1 mode) */
                    ShowNotify("GOAL COMPLETE!");
                    Log("AP GOAL COMPLETE: quest %d matches goal_scope %d\n", goalQid, g_apGoalScope);
                    break;
                }
            }
        }

        /* Goal=3 (Collection) win-condition check — fires when Coll_IsGoalComplete returns TRUE. */
        if (!g_apGoalComplete && g_apGoal == 3) {
            extern BOOL Coll_IsGoalComplete(void);
            if (Coll_IsGoalComplete()) {
                g_apGoalComplete = TRUE;
                char dir3[MAX_PATH], gpath[MAX_PATH];
                GetArchDir(dir3, MAX_PATH);
                sprintf(gpath, "%sap_goal.dat", dir3);
                FILE* gf = fopen(gpath, "w");
                if (gf) { fprintf(gf, "goal=complete\nsource=collection\n"); fclose(gf); }
                ApPipe_SendGoal(); /* V2 pipe transport (no-op in V1 mode) */
                ShowNotify("COLLECTION GOAL COMPLETE!");
                Log("AP GOAL COMPLETE: collection targets all met\n");
            }
        }

        /* Goal=4 (Custom) win-condition check. */
        if (!g_apGoalComplete && g_apGoal == 4) {
            extern BOOL CustomGoal_IsComplete(void);
            if (CustomGoal_IsComplete()) {
                g_apGoalComplete = TRUE;
                char dir4[MAX_PATH], gpath[MAX_PATH];
                GetArchDir(dir4, MAX_PATH);
                sprintf(gpath, "%sap_goal.dat", dir4);
                FILE* gf = fopen(gpath, "w");
                if (gf) { fprintf(gf, "goal=complete\nsource=custom\n"); fclose(gf); }
                ApPipe_SendGoal(); /* V2 pipe transport (no-op in V1 mode) */
                ShowNotify("CUSTOM GOAL COMPLETE!");
                Log("AP GOAL COMPLETE: all custom-goal targets met + gold target reached\n");
            }
        }
    }
}

/* periodic custom/collection goal-completion check. */
void CheckNonQuestGoalCompletion(void) {
    /* fires in BOTH AP and standalone (was gated on g_apConnected, so standalone Collection/Custom goals never completed). */
    if (g_apGoalComplete) return;
    const char* src = NULL;
    if (g_apGoal == 3) {
        extern BOOL Coll_IsGoalComplete(void);
        if (Coll_IsGoalComplete()) src = "collection";
    } else if (g_apGoal == 4) {
        extern BOOL CustomGoal_IsComplete(void);
        if (CustomGoal_IsComplete()) src = "custom";
    } else if (g_apGoal >= 0 && g_apGoal <= 2) {
        /* Full Normal/NM/Hell — robust periodic re-check. */
        int gd = g_apDiffScope;                 /* = g_apGoal: 0=Norm/1=NM/2=Hell */
        int gq = (g_apGoalScope >= 0 && g_apGoalScope <= 4) ?
                 g_goalQuestIds[g_apGoalScope] : g_goalQuestIds[4];  /* Baal = 406 */
        if (gd >= 0 && gd < 3 && gq > 0 && gq < MAX_QUEST_ID && g_questCompleted[gd][gq])
            src = "full";
    }
    if (!src) return;

    g_apGoalComplete = TRUE;
    char dir[MAX_PATH], gpath[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(gpath, "%sap_goal.dat", dir);
    FILE* gf = fopen(gpath, "w");
    if (gf) { fprintf(gf, "goal=complete\nsource=%s\n", src); fclose(gf); }
    ApPipe_SendGoal(); /* V2 pipe transport (no-op in V1 mode) */
    ShowNotify("GOAL COMPLETE!");
    Log("AP GOAL COMPLETE (periodic check): source=%s g_apGoal=%d\n", src, g_apGoal);
}

/* Map tracker — DIAGNOSTIC struct-layout capture (opt-in via [Debug] MapDiag=1 in d2arch.ini). */
static void D2Map_DumpHex(const char* label, DWORD addr, int n) {
    if (!addr) return;
    __try {
        Log("MAPDIAG %s @ %08X:\n", label, addr);
        char line[160];
        for (int i = 0; i < n; i += 16) {
            int off = _snprintf(line, sizeof(line), "  +%03X:", i);
            for (int j = 0; j < 16 && i + j < n; j++)
                off += _snprintf(line + off, sizeof(line) - off, " %02X",
                                 *(unsigned char*)(addr + i + j));
            line[sizeof(line) - 1] = 0;
            Log("%s\n", line);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { Log("MAPDIAG %s: read fault\n", label); }
}

/* Reverse-engineering aid: for every 4-byte field in [0,nbytes) that looks like a heap pointer, log the field offset + value + the first 12 bytes at the target. */
static void D2Map_ScanPtrs(const char* label, DWORD base, int nbytes) {
    if (!base) return;
    __try {
        Log("MAPDIAG scan %s @ %08X:\n", label, base);
        for (int off = 0; off + 4 <= nbytes; off += 4) {
            DWORD v = *(DWORD*)(base + off);
            if (v > 0x10000 && v < 0x7F000000 && (v & 3) == 0) {
                __try {
                    DWORD t0 = *(DWORD*)(v + 0), t1 = *(DWORD*)(v + 4), t2 = *(DWORD*)(v + 8);
                    Log("  +%03X = %08X -> [%08X %08X %08X]\n", off, v, t0, t1, t2);
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Log("  +%03X = %08X -> (unreadable)\n", off, v);
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* Find the room2 list next-link: from the level's first room2 (level+0x30), try every field offset as a "next" pointer and report which yields a long chain of valid room2s (target+0x00 == pLevel). */
static void D2Map_FindRoomNext(DWORD pLevel) {
    if (!pLevel) return;
    __try {
        DWORD first = *(DWORD*)(pLevel + 0x30);
        int count = *(int*)(pLevel + 0x34);
        Log("MAPDIAG findnext: level=%08X first=%08X count=%d\n", pLevel, first, count);
        if (!first || count < 2 || count > 4096) return;
        for (int off = 0; off <= 0x100; off += 4) {
            DWORD r = first; int valid = 0;
            for (int step = 0; step < count + 4 && r; step++) {
                DWORD lvl = 0;
                __try { lvl = *(DWORD*)(r + 0x00); } __except(EXCEPTION_EXECUTE_HANDLER) { break; }
                if (lvl != pLevel) break;
                valid++;
                __try { r = *(DWORD*)(r + off); } __except(EXCEPTION_EXECUTE_HANDLER) { break; }
                if (r <= 0x10000 || r >= 0x7F000000 || (r & 3)) break;
            }
            if (valid >= 5) Log("MAPDIAG findnext: next-link +%03X -> chain of %d room2s\n", off, valid);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void D2Map_DumpLevelStructs(void* pPlayerUnit, int levelId) {
    static int s_enabled = -1;
    if (s_enabled < 0) {
        char dir[MAX_PATH], ini[MAX_PATH];
        GetArchDir(dir, MAX_PATH);
        _snprintf(ini, sizeof(ini), "%sd2arch.ini", dir); ini[sizeof(ini) - 1] = 0;
        s_enabled = GetPrivateProfileIntA("Debug", "MapDiag", 0, ini);
    }
    if (!s_enabled) return;
    static int s_dumped = -1;
    if (levelId == s_dumped) return;
    s_dumped = levelId;
    __try {
        DWORD pPath = *(DWORD*)((DWORD)pPlayerUnit + 0x2C);
        if (!pPath) return;
        DWORD pRoom = *(DWORD*)(pPath + 0x1C);
        DWORD p38   = pRoom ? *(DWORD*)(pRoom + 0x38) : 0;
        DWORD pLvl  = p38   ? *(DWORD*)(p38 + 0x00)   : 0;
        Log("MAPDIAG ===== level=%d unit=%08X path=%08X room=%08X room+0x38=%08X level=%08X =====\n",
            levelId, (DWORD)pPlayerUnit, pPath, pRoom, p38, pLvl);
        D2Map_DumpHex("room(path+0x1C)", pRoom, 0x90);
        D2Map_DumpHex("room+0x38",       p38,   0x90);
        D2Map_DumpHex("level",           pLvl,  0x60);

        /* Decode the level header + walk its room list (level+0x30, next at room2+0x20) — confirms the walk and captures every room's rectangle for the room-block map. */
        if (pLvl) {
            Log("MAPDIAG decode: levelNo=%d sizeT=(%d,%d) originT=(%d,%d) roomList=%08X roomCount=%d\n",
                *(int*)(pLvl + 0x04), *(int*)(pLvl + 0x28), *(int*)(pLvl + 0x2C),
                *(int*)(pLvl + 0x20), *(int*)(pLvl + 0x24),
                *(DWORD*)(pLvl + 0x30), *(int*)(pLvl + 0x34));
            D2Map_FindRoomNext(pLvl);   /* find the full room-list next-link */
            DWORD r2 = *(DWORD*)(pLvl + 0x30);
            for (int n = 0; r2 && n < 24; n++) {
                DWORD r2lvl = *(DWORD*)(r2 + 0x00);
                Log("MAPDIAG  room2[%2d] @ %08X pLvl=%08X pos=(%d,%d) sizeT=(%d,%d) next=%08X p14=%08X\n",
                    n, r2, r2lvl, *(int*)(r2 + 0x04), *(int*)(r2 + 0x08),
                    *(int*)(r2 + 0x0C), *(int*)(r2 + 0x10),
                    *(DWORD*)(r2 + 0x20), *(DWORD*)(r2 + 0x14));
                if (r2lvl != (DWORD)pLvl) { Log("MAPDIAG  (stop: room2->pLevel mismatch)\n"); break; }
                r2 = *(DWORD*)(r2 + 0x20);
            }
        }
        /* Reverse-engineer the next-room link (now solved: room2+0xC0/+0xE4). */
        D2Map_ScanPtrs("room1", pRoom, 0x100);
        D2Map_ScanPtrs("room2", p38,   0x100);

        /* Units (objects -> markers): the unit-list head is room1+0x2C (type @ +0x00, txtFileNo @ +0x04). */
        if (pRoom) {
            DWORD pUnit = 0;
            __try { pUnit = *(DWORD*)(pRoom + 0x2C); } __except(EXCEPTION_EXECUTE_HANDLER) { pUnit = 0; }
            for (int u = 0; u < 4 && pUnit > 0x10000 && pUnit < 0x7F000000; u++) {
                char lbl[24]; _snprintf(lbl, sizeof(lbl), "unit%d", u); lbl[sizeof(lbl)-1] = 0;
                D2Map_DumpHex(lbl, pUnit, 0x80);
                D2Map_ScanPtrs(lbl, pUnit, 0x100);
                /* the object's position lives in its path (unit+0x2C) — dump it. */
                {
                    DWORD pPath = 0;
                    __try { pPath = *(DWORD*)(pUnit + 0x2C); } __except(EXCEPTION_EXECUTE_HANDLER) { pPath = 0; }
                    char plbl[30]; _snprintf(plbl, sizeof(plbl), "%s.path", lbl); plbl[sizeof(plbl) - 1] = 0;
                    D2Map_DumpHex(plbl, pPath, 0x30);
                }
                __try { pUnit = *(DWORD*)(pUnit + 0xE8); }   /* next unit in room */
                __except(EXCEPTION_EXECUTE_HANDLER) { pUnit = 0; }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { Log("MAPDIAG: exception during dump\n"); }
}

/* Map tracker — export the player's CURRENT room collision to a per-level file the launcher reads, so its graphical map fills in (maphack-style) as the player explores. */
static void D2Map_ExportRoom(DWORD pRoom1, int levelId) {
    if (!pRoom1 || levelId < 0) return;
    __try {
        DWORD pColl = *(DWORD*)(pRoom1 + 0x34);
        if (!pColl) return;
        int posX  = *(int*)(pColl + 0x00);
        int posY  = *(int*)(pColl + 0x04);
        int sizeX = *(int*)(pColl + 0x08);
        int sizeY = *(int*)(pColl + 0x0C);
        DWORD pMap = *(DWORD*)(pColl + 0x20);
        if (!pMap || sizeX < 1 || sizeX > 256 || sizeY < 1 || sizeY > 256) return;
        int cells  = sizeX * sizeY;
        int nbytes = (cells + 7) / 8;

        /* Dedup recent rooms (level+pos) so we don't re-append the same one. */
        static DWORD s_seen[2048]; static int s_seenN = 0;
        DWORD key = ((DWORD)(levelId & 0xFF) << 24)
                  ^ ((DWORD)(posX & 0xFFF) << 12) ^ (DWORD)(posY & 0xFFF);
        for (int i = 0; i < s_seenN; i++) if (s_seen[i] == key) return;
        if (s_seenN < 2048) s_seen[s_seenN++] = key;

        static unsigned char bits[8192];               /* up to 256x256 / 8 */
        if (nbytes > (int)sizeof(bits)) return;
        memset(bits, 0, nbytes);
        const unsigned short* m = (const unsigned short*)pMap;
        for (int i = 0; i < cells; i++)
            if (m[i] == 0) bits[i >> 3] |= (unsigned char)(1 << (i & 7));

        char dir[MAX_PATH], mapdir[MAX_PATH], path[MAX_PATH];
        GetArchDir(dir, MAX_PATH);
        _snprintf(mapdir, sizeof(mapdir), "%smap", dir);  mapdir[sizeof(mapdir) - 1] = 0;
        CreateDirectoryA(mapdir, NULL);
        _snprintf(path, sizeof(path), "%s\\level_%d.map", mapdir, levelId); path[sizeof(path) - 1] = 0;
        FILE* f = fopen(path, "a");
        if (!f) return;
        fprintf(f, "C,%d,%d,%d,%d,", posX, posY, sizeX, sizeY);   /* C = collision detail */
        for (int b = 0; b < nbytes; b++) fprintf(f, "%02X", bits[b]);
        fprintf(f, "\n");
        fclose(f);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* Map tracker — export a room's BOUNDS (rectangle) so the whole level shape appears instantly even for streamed outdoor levels where most rooms have no collision (room1) loaded yet. */
static void D2Map_ExportRect(DWORD pRoom2, int levelId) {
    if (!pRoom2 || levelId < 0) return;
    __try {
        int posX  = *(int*)(pRoom2 + 0x04);
        int posY  = *(int*)(pRoom2 + 0x08);
        int sizeX = *(int*)(pRoom2 + 0x0C);
        int sizeY = *(int*)(pRoom2 + 0x10);
        if (sizeX < 1 || sizeX > 64 || sizeY < 1 || sizeY > 64) return;
        if (posX <= 0 || posY <= 0) return;
        static DWORD s_seen[4096]; static int s_seenN = 0;
        DWORD key = ((DWORD)(levelId & 0xFF) << 24)
                  ^ ((DWORD)(posX & 0xFFF) << 12) ^ (DWORD)(posY & 0xFFF);
        for (int i = 0; i < s_seenN; i++) if (s_seen[i] == key) return;
        if (s_seenN < 4096) s_seen[s_seenN++] = key;
        char dir[MAX_PATH], mapdir[MAX_PATH], path[MAX_PATH];
        GetArchDir(dir, MAX_PATH);
        _snprintf(mapdir, sizeof(mapdir), "%smap", dir);  mapdir[sizeof(mapdir) - 1] = 0;
        CreateDirectoryA(mapdir, NULL);
        _snprintf(path, sizeof(path), "%s\\level_%d.map", mapdir, levelId); path[sizeof(path) - 1] = 0;
        FILE* f = fopen(path, "a");
        if (!f) return;
        fprintf(f, "R,%d,%d,%d,%d,\n", posX * 5, posY * 5, sizeX * 5, sizeY * 5);
        fclose(f);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* Map tracker — INSTANT full level: breadth-first walk every room of the level the moment it's entered, so the whole map appears at once instead of revealing as you explore. */
/* Map tracker — export the OBJECTS in a room (chests/shrines/barrels/urns/ waypoints/portals -> map markers). */
static void D2Map_ExportRoomObjects(DWORD pRoom1, int levelId) {
    if (!pRoom1 || levelId < 0) return;
    __try {
        DWORD pUnit = *(DWORD*)(pRoom1 + 0x2C);
        static DWORD s_seen[8192]; static int s_seenN = 0;
        char dir[MAX_PATH], mapdir[MAX_PATH], path[MAX_PATH];
        GetArchDir(dir, MAX_PATH);
        _snprintf(mapdir, sizeof(mapdir), "%smap", dir); mapdir[sizeof(mapdir) - 1] = 0;
        _snprintf(path, sizeof(path), "%s\\level_%d.map", mapdir, levelId); path[sizeof(path) - 1] = 0;
        FILE* f = NULL;
        for (int n = 0; pUnit > 0x10000 && pUnit < 0x7F000000 && n < 512; n++) {
            int  type = *(int*)(pUnit + 0x00);
            int  txt  = *(int*)(pUnit + 0x04);
            DWORD uid = *(DWORD*)(pUnit + 0x0C);
            if (type == 2 && uid) {                       /* UNIT_OBJECT */
                int seen = 0;
                for (int i = 0; i < s_seenN; i++) if (s_seen[i] == uid) { seen = 1; break; }
                if (!seen) {
                    if (s_seenN < 8192) s_seen[s_seenN++] = uid;
                    DWORD pP = *(DWORD*)(pUnit + 0x2C);
                    if (pP) {
                        int px = *(int*)(pP + 0x0C), py = *(int*)(pP + 0x10);
                        if (px > 0 && py > 0 && px < 100000 && py < 100000) {
                            if (!f) { CreateDirectoryA(mapdir, NULL); f = fopen(path, "a"); if (!f) return; }
                            fprintf(f, "O,%d,%d,%d\n", txt, px, py);
                        }
                    }
                }
            }
            pUnit = *(DWORD*)(pUnit + 0xE8);
        }
        if (f) fclose(f);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* Map tracker — record an entrance/exit crossing. */
static void D2Map_AppendExit(int levelId, int x, int y, int targetLevel) {
    if (levelId <= 0 || x <= 0 || y <= 0) return;
    __try {
        static DWORD s_seen[1024]; static int s_seenN = 0;
        DWORD key = ((DWORD)levelId << 24) ^ ((DWORD)targetLevel << 16)
                  ^ (((DWORD)(x >> 2) & 0xFF) << 8) ^ ((DWORD)(y >> 2) & 0xFF);
        for (int i = 0; i < s_seenN; i++) if (s_seen[i] == key) return;
        if (s_seenN < 1024) s_seen[s_seenN++] = key;
        char dir[MAX_PATH], mapdir[MAX_PATH], path[MAX_PATH];
        GetArchDir(dir, MAX_PATH);
        _snprintf(mapdir, sizeof(mapdir), "%smap", dir); mapdir[sizeof(mapdir) - 1] = 0;
        _snprintf(path, sizeof(path), "%s\\level_%d.map", mapdir, levelId); path[sizeof(path) - 1] = 0;
        CreateDirectoryA(mapdir, NULL);
        FILE* f = fopen(path, "a");
        if (!f) return;
        fprintf(f, "X,%d,%d,%d\n", x, y, targetLevel);
        fclose(f);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void D2Map_ExportLevel(DWORD pStartRoom2, int levelId) {
    if (!pStartRoom2 || levelId < 0) return;
    __try {
        DWORD pLevel = *(DWORD*)(pStartRoom2 + 0x00);
        if (!pLevel) return;
        int count = *(int*)(pLevel + 0x34);
        if (count < 1 || count > 4096) count = 4096;
        /* Walk the level's FULL room list — confirmed: head = level+0x30, next = room2+0xE8 — which reaches EVERY room (loaded or not), unlike pRoom2Near. */
        DWORD r2 = *(DWORD*)(pLevel + 0x30);
        for (int n = 0; r2 && n < count + 4; n++) {
            DWORD lvl = 0;
            __try { lvl = *(DWORD*)(r2 + 0x00); } __except(EXCEPTION_EXECUTE_HANDLER) { break; }
            if (lvl != pLevel) break;                       /* end of list / invalid */
            D2Map_ExportRect(r2, levelId);
            DWORD pRoom1 = 0;
            __try { pRoom1 = *(DWORD*)(r2 + 0xE4); } __except(EXCEPTION_EXECUTE_HANDLER) { pRoom1 = 0; }
            if (pRoom1) { D2Map_ExportRoom(pRoom1, levelId); D2Map_ExportRoomObjects(pRoom1, levelId); }
            __try { r2 = *(DWORD*)(r2 + 0xE8); } __except(EXCEPTION_EXECUTE_HANDLER) { break; }
            if (r2 <= 0x10000 || r2 >= 0x7F000000 || (r2 & 3)) break;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* Map tracker — stream the player's live position to the launcher as "POS:<levelId>|<x>|<y>" so its graphical map can draw a "you are here" dot. */
void D2Zones_WriteLockFile(const char* mapDir);   /* defined in d2arch_zones.c */
/* EMPTY-ZONE DIAGNOSTIC (3.2.4) — see patch header in the tools folder. */
/* --- census v3: ask the patched engine what PopulateRoom would see */
typedef int   (__stdcall *CenPopRooms_t)(void* pAct, int nLevelId);
typedef void* (__stdcall *CenCoordList_t)(void* pRoom1);

static void Census_V3(int levelId, DWORD pRoom1, void* pPlayerUnit) {
    __try {
        static CenPopRooms_t  s_fnPop = NULL;
        static CenCoordList_t s_fnCoord = NULL;
        static BOOL s_resolved = FALSE;
        if (!s_resolved) {
            s_resolved = TRUE;
            /* hD2Common is the PATCH module (documented in InitAPI), and the patch exports by name — so these resolve to the exact D2MOO implementations the patched vanilla code calls. */
            if (hD2Common) {
                s_fnPop = (CenPopRooms_t)GetProcAddress(hD2Common,
                              "DUNGEON_GetNumberOfPopulatedRoomsInLevel");
                s_fnCoord = (CenCoordList_t)GetProcAddress(hD2Common,
                              "DUNGEON_GetRoomCoordList");
            }
            Log("CENSUS3: resolved pop=%p coord=%p\n", s_fnPop, s_fnCoord);
        }

        int popRooms = -1;
        if (s_fnPop && pPlayerUnit) {
            void* pAct = NULL;
            __try { pAct = *(void**)((DWORD)pPlayerUnit + 0x1C); }
            __except(EXCEPTION_EXECUTE_HANDLER) { pAct = NULL; }
            if (pAct) {
                __try { popRooms = s_fnPop(pAct, levelId); }
                __except(EXCEPTION_EXECUTE_HANDLER) { popRooms = -2; }
            }
        }

        /* Walk the coord chain for the player's own room: count nodes and print the first two boxes (pBox[1] is the one PopulateRoom rolls over). */
        int nLists = 0;
        int i0 = -99, nd0 = -99, b0x = 0, b0y = 0, b0w = 0, b0h = 0;
        int i1 = -99, nd1 = -99;
        if (s_fnCoord && pRoom1) {
            DWORD cl = 0;
            __try { cl = (DWORD)s_fnCoord((void*)pRoom1); }
            __except(EXCEPTION_EXECUTE_HANDLER) { cl = 0; }
            DWORD c = cl;
            for (int n = 0; c && n < 64; n++) {
                __try {
                    if (n == 0) {
                        i0 = *(int*)(c + 0x28); nd0 = *(int*)(c + 0x20);
                        b0x = *(int*)(c + 0x10); b0y = *(int*)(c + 0x14);
                        b0w = *(int*)(c + 0x18); b0h = *(int*)(c + 0x1C);
                    } else if (n == 1) {
                        i1 = *(int*)(c + 0x28); nd1 = *(int*)(c + 0x20);
                    }
                    nLists++;
                    c = *(DWORD*)(c + 0x2C);
                } __except(EXCEPTION_EXECUTE_HANDLER) { break; }
            }
        }

        Log("CENSUS3: area=%d poprooms=%d | coord n=%d "
            "[0: idx=%d node=%d box1=(%d,%d,%d,%d)] [1: idx=%d node=%d]\n",
            levelId, popRooms, nLists, i0, nd0, b0x, b0y, b0w, b0h, i1, nd1);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("CENSUS3: area=%d faulted\n", levelId);
    }
}

static void Census_LogLevel(int levelId, DWORD pRoom1) {
    __try {
        /* --- the record, live --- */
        int den = -1, num = -1, nrm = -1, nh = -1, unq = -1;
        short id0 = -1, id1 = -1, id2 = -1, id3 = -1;
        DWORD dt = GetSgptDT();
        if (dt) {
            DWORD pLvls = *(DWORD*)(dt + 0xC58);
            int   nLvls = *(int*)(dt + 0xC5C);
            if (pLvls && levelId > 0 && levelId < nLvls) {
                BYTE* rec = (BYTE*)(pLvls + (DWORD)levelId * 0x220);
                int diff = g_currentDifficulty; if (diff < 0 || diff > 2) diff = 0;
                den = (int)*(DWORD*)(rec + 0x1C + 4 * diff);
                num = rec[0x32]; nrm = rec[0x33]; nh = rec[0x34]; unq = rec[0x35];
                id0 = *(short*)(rec + 0x36); id1 = *(short*)(rec + 0x38);
                id2 = *(short*)(rec + 0x3A); id3 = *(short*)(rec + 0x3C);
            }
        }

        /* --- the level, live --- */
        int rooms = 0, act = 0, mon = 0, obj = 0;
        int clsIds[16], clsCnt[16], clsN = 0;
        if (pRoom1) {
            DWORD pRoom2 = *(DWORD*)(pRoom1 + 0x38);
            DWORD pLevel = pRoom2 ? *(DWORD*)(pRoom2 + 0x00) : 0;
            if (pLevel) {
                int cap = *(int*)(pLevel + 0x34);
                if (cap < 1 || cap > 4096) cap = 4096;
                DWORD r2 = *(DWORD*)(pLevel + 0x30);
                for (int n = 0; r2 && n < cap + 4; n++) {
                    DWORD lvl = 0;
                    __try { lvl = *(DWORD*)(r2 + 0x00); } __except(EXCEPTION_EXECUTE_HANDLER) { break; }
                    if (lvl != pLevel) break;
                    rooms++;
                    DWORD r1 = 0;
                    __try { r1 = *(DWORD*)(r2 + 0xE4); } __except(EXCEPTION_EXECUTE_HANDLER) { r1 = 0; }
                    if (r1) {
                        act++;
                        DWORD u = 0;
                        __try { u = *(DWORD*)(r1 + 0x2C); } __except(EXCEPTION_EXECUTE_HANDLER) { u = 0; }
                        for (int g = 0; u && g < 2048; g++) {
                            DWORD ty = 0, nx = 0;
                            __try { ty = *(DWORD*)(u + 0x00); nx = *(DWORD*)(u + 0xE8); }
                            __except(EXCEPTION_EXECUTE_HANDLER) { break; }
                            if (ty == 1) {
                                mon++;
                                DWORD cid = 0;
                                __try { cid = *(DWORD*)(u + 0x04); }
                                __except(EXCEPTION_EXECUTE_HANDLER) { cid = 0xFFFF; }
                                int k = 0;
                                for (; k < clsN; k++) if (clsIds[k] == (int)cid) { clsCnt[k]++; break; }
                                if (k == clsN && clsN < 16) { clsIds[clsN] = (int)cid; clsCnt[clsN] = 1; clsN++; }
                            }
                            else if (ty == 2) obj++;
                            u = nx;
                        }
                    }
                    __try { r2 = *(DWORD*)(r2 + 0xE8); } __except(EXCEPTION_EXECUTE_HANDLER) { break; }
                }
            }
        }

        Log("CENSUS: area=%d diff=%d | rec den=%d num=%d nrm=%d nh=%d unq=%d "
            "ids=[%d,%d,%d,%d] | live rooms=%d act=%d mon=%d obj=%d\n",
            levelId, g_currentDifficulty, den, num, nrm, nh, unq,
            (int)id0, (int)id1, (int)id2, (int)id3, rooms, act, mon, obj);
        /* v2 — WHICH monsters. mon=10 cannot tell trash from the champion packs and gargoyle traps the reports say are the only things left, so list the class ids with counts. */
        if (clsN > 0) {
            char line[256]; int off = 0;
            for (int k = 0; k < clsN && off < 200; k++)
                off += _snprintf(line + off, sizeof(line) - off - 1, "%s%dx%d",
                                 k ? " " : "", clsIds[k], clsCnt[k]);
            line[sizeof(line) - 1] = 0;
            Log("CENSUS2: area=%d cls=[%s]\n", levelId, line);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("CENSUS: area=%d — walk faulted (structs not as expected)\n", levelId);
    }
}

/* SKILL WATCH (3.2.9) — trail of which wave-2 skill was selected when. */
/* STUCK-SKILL WATCHDOG (Marco, 2026-08-12).
 *
 * Whirlwind — and any other skill with its own animation mode — needs a weapon
 * the animation supports. Start it holding a bow, crossbow or spear and the
 * character enters the skill mode and never leaves: frozen in place, input
 * ignored, only a re-login clears it. That is vanilla D2 behaviour, not
 * something we introduced, but it costs the player their session either way.
 *
 * So: watch for the signature. A player in a SKILL animation mode whose
 * position has not moved for a few seconds is not casting, they are stuck —
 * a working skill either moves them or ends. Nudging them with an ordinary
 * "run to where you already stand" is the same message the client sends on a
 * click, which is exactly what a re-login would have achieved, minus the
 * re-login. */
static void StuckWatch_Tick(void* pPlayerUnit) {
    if (!pPlayerUnit) return;
    __try {
        DWORD mode  = *(DWORD*)((DWORD)pPlayerUnit + 0x10);
        DWORD pPath = *(DWORD*)((DWORD)pPlayerUnit + 0x2C);
        if (!pPath) return;
        int x = (int)*(unsigned short*)(pPath + 0x02);
        int y = (int)*(unsigned short*)(pPath + 0x06);

        /* PLRMODE S1-S4 (13-16) and SQ (18) — the class-skill animations.
         * Attack/cast/walk are deliberately NOT here: those end on their own,
         * and a false positive would interrupt real play. */
        BOOL skillMode = (mode >= 13 && mode <= 16) || mode == 18;

        static DWORD s_since = 0;
        static int   s_x = -1, s_y = -1;
        static DWORD s_mode = 0xFFFFFFFF;
        static DWORD s_lastKick = 0;
        DWORD now = GetTickCount();

        if (!skillMode || x != s_x || y != s_y || mode != s_mode) {
            s_since = now; s_x = x; s_y = y; s_mode = mode;
            return;                                  /* moving or not in a skill = fine */
        }
        if (now - s_since < 3000) return;            /* a real skill finishes well inside this */
        if (now - s_lastKick < 5000) return;         /* one nudge per 5s, never a packet storm */
        s_lastKick = now;

        BYTE pkt[5];
        pkt[0] = 0x03;                               /* run to location */
        *(WORD*)&pkt[1] = (WORD)x;
        *(WORD*)&pkt[3] = (WORD)y;
        SendGamePacket(5, pkt);
        Log("STUCKWATCH: player frozen in skill mode %lu at (%d,%d) for %lums "
            "— sent move packet to break out\n", mode, x, y, now - s_since);
        ShowNotify("Unstuck: that skill needs a different weapon");
        s_since = now;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void SkillWatch_Tick(void* pPlayerUnit) {
    if (!pPlayerUnit || !fnGetRightSkill || !fnGetLeftSkill) return;
    __try {
        static int s_lastR = -1, s_lastL = -1;
        for (int hand = 0; hand < 2; hand++) {
            void* pSk = hand ? fnGetLeftSkill(pPlayerUnit) : fnGetRightSkill(pPlayerUnit);
            int id = -1;
            if (pSk) {
                DWORD pTxt = *(DWORD*)((DWORD)pSk + 0x00);
                if (pTxt) id = (int)*(WORD*)(pTxt + 0x00);
            }
            int* last = hand ? &s_lastL : &s_lastR;
            if (id == *last) continue;
            *last = id;

            /* OWNERSHIP GUARD (P13). D2 stores the hand assignment in the
             * character file and never validates it on load — after a crash
             * the save can hold a skill the (re-randomized) character no
             * longer owns, and the panel's own gate is bypassed. This watch
             * already reads both hands every tick; if the skill is in OUR
             * pool but not unlocked, kick the hand back to normal Attack.
             * Skills outside the pool (scrolls, tomes, base attack) are
             * untouched. */
            if (id > 0) {
                for (int pi = 0; pi < g_poolCount; pi++) {
                    if (g_skillDB[g_pool[pi].dbIndex].id != id) continue;
                    if (!g_pool[pi].unlocked) {
                        static DWORD s_lastKick = 0;
                        DWORD now = GetTickCount();
                        if (now - s_lastKick > 750) {
                            s_lastKick = now;
                            BYTE pkt[9];
                            pkt[0] = 0x3C;
                            *(WORD*)&pkt[1] = 0;                 /* skill 0 = Attack */
                            pkt[3] = 0;
                            pkt[4] = hand ? 0x80 : 0x00;         /* 0x80 = left hand */
                            *(DWORD*)&pkt[5] = 0xFFFFFFFF;       /* no item */
                            SendGamePacket(9, pkt);
                            Log("SKILLGUARD: %s hand held UNOWNED skill %d — reset to Attack\n",
                                hand ? "left" : "right", id);
                            ShowNotify("That skill is not yours yet");
                            *last = -1;                          /* re-check after the packet lands */
                        }
                    }
                    break;
                }
            }

            if (id < 357) continue;                              /* wave 2 only */
            /* Name + tier come from g_skillDB (d2arch_skills.c, included before this file). */
            const char* nm = "?"; int tier = 0;
            for (int i = 0; i < (int)SKILL_DB_COUNT; i++) {
                if (g_skillDB[i].id == id) { nm = g_skillDB[i].name; tier = g_skillDB[i].tier; break; }
            }
            Log("SKILLWATCH: %s hand -> id=%d tier=%d name='%s'\n",
                hand ? "left" : "right", id, tier, nm);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void SendPlayerPosToLauncher(void* pPlayerUnit) {
    if (!pPlayerUnit) return;
    static DWORD s_lastMs = 0;
    DWORD now = GetTickCount();
    if (now - s_lastMs < 250) return;   /* read at most ~4 Hz */
    s_lastMs = now;
    __try {
        DWORD pPath = *(DWORD*)((DWORD)pPlayerUnit + 0x2C);
        if (!pPath) return;
        int nX = (int)*(unsigned short*)(pPath + 0x02);
        int nY = (int)*(unsigned short*)(pPath + 0x06);
        if (nX <= 0 || nY <= 0) return;
        int levelId = -1;
        DWORD pRoom = *(DWORD*)(pPath + 0x1C);
        if (pRoom) {
            DWORD pDrlgRoom = *(DWORD*)(pRoom + 0x38);
            if (pDrlgRoom) {
                DWORD pLevel = *(DWORD*)(pDrlgRoom + 0x00);
                if (pLevel) levelId = *(int*)(pLevel + 0x04);
            }
        }
        if (levelId < 0) return;
        SkillWatch_Tick(pPlayerUnit);
        StuckWatch_Tick(pPlayerUnit);
        /* The diagnostic capture is purely local (logs to file) — run it on every level change regardless of the pipe, so it works even in a launch with no launcher pipe attached. */
        static int s_lvl = -1, s_x = -1, s_y = -1;
        if (levelId != s_lvl) {
            Census_LogLevel(levelId, pRoom);
            Census_V3(levelId, pRoom, pPlayerUnit);
            /* Mark the crossing on BOTH maps: exit on the old, entrance on the new. */
            if (s_lvl > 0 && s_x > 0 && s_y > 0) {
                D2Map_AppendExit(s_lvl, s_x, s_y, levelId);   /* old level -> new */
                D2Map_AppendExit(levelId, nX, nY, s_lvl);     /* new level ← old */
            }
            D2Map_DumpLevelStructs(pPlayerUnit, levelId);
            /* INSTANT full level — BFS every room from the player's room2 (room1+0x38) so the whole map appears the moment you enter. */
            DWORD pRoom2 = pRoom ? *(DWORD*)(pRoom + 0x38) : 0;
            if (pRoom2) D2Map_ExportLevel(pRoom2, levelId);
            /* Refresh the launcher map's green/red area state on every zone change. */
            {
                char zd[MAX_PATH], zm[MAX_PATH];
                GetArchDir(zd, MAX_PATH);
                _snprintf(zm, sizeof(zm), "%smap", zd); zm[sizeof(zm) - 1] = 0;
                D2Zones_WriteLockFile(zm);
            }
        }
        /* v2 — periodic re-census: rooms populate as they ACTIVATE, so the entry snapshot alone cannot show a room that activates empty. */
        {
            static DWORD s_cenMs = 0; static int s_cenLvl = -1;
            DWORD nowC = GetTickCount();
            if (levelId != s_cenLvl) { s_cenLvl = levelId; s_cenMs = nowC; }
            else if (nowC - s_cenMs > 8000) {
                s_cenMs = nowC;
                Census_LogLevel(levelId, pRoom);
                Census_V3(levelId, pRoom, pPlayerUnit);
            }
        }
        /* Also export the current room on every room change — covers rooms that stream in later as the player explores. */
        static DWORD s_lastRoom = 0;
        if (pRoom && pRoom != s_lastRoom) {
            s_lastRoom = pRoom;
            D2Map_ExportRoom(pRoom, levelId);
            D2Map_ExportRoomObjects(pRoom, levelId);
        }
        /* Only SEND on a real change (new level, or moved > 1 tile). */
        if (levelId == s_lvl && (nX - s_x) > -2 && (nX - s_x) < 2 &&
                                (nY - s_y) > -2 && (nY - s_y) < 2) return;
        s_lvl = levelId; s_x = nX; s_y = nY;
        /* The POS stream itself only matters when a launcher pipe is attached. */
        if (!ApPipe_Mode() || !ApPipe_Active()) return;
        char out[64];
        _snprintf(out, sizeof(out), "POS:%d|%d|%d", levelId, nX, nY);
        out[sizeof(out) - 1] = 0;
        ApPipe_QueueLine(out);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* send the FULL active location universe to the launcher so the standalone tracker can show UNCHECKED locations + per-category totals like an AP session does (the AP server normally delivers this on connect). */
BOOL g_activeLocsSent = FALSE;
void SendActiveLocationsToLauncher(void) {
    if (!ApPipe_Mode() || !ApPipe_Active()) return;

    int diffMax = (g_apGoal >= 0 && g_apGoal <= 2) ? g_apGoal : 2;  /* difficulty scope */

    char batch[480]; int blen = 0; batch[0] = 0;
    for (int act = 0; act < 5; act++) {
        for (int q = 0; q < g_acts[act].num; q++) {
            int qid = g_acts[act].quests[q].id;
            if (qid <= 0 || qid >= MAX_QUEST_ID) continue;
            if (!IsQuestTypeActive((int)g_acts[act].quests[q].type)) continue;
            for (int diff = 0; diff <= diffMax; diff++) {
                long locId = (long)LOCATION_BASE + qid + diff * 1000;
                char idStr[16];
                int idLen = _snprintf(idStr, sizeof(idStr), "%ld", locId);
                if (idLen <= 0) continue;
                if (blen > 0 && (blen + 1 + idLen) >= (int)sizeof(batch) - 2) {
                    char out[512];
                    _snprintf(out, sizeof(out), "MISSING:%s", batch);
                    out[sizeof(out) - 1] = 0;
                    ApPipe_QueueLine(out);
                    blen = 0; batch[0] = 0;
                }
                if (blen > 0) batch[blen++] = ',';
                memcpy(batch + blen, idStr, idLen); blen += idLen; batch[blen] = 0;
            }
        }
    }
    if (blen > 0) {
        char out[512];
        _snprintf(out, sizeof(out), "MISSING:%s", batch);
        out[sizeof(out) - 1] = 0;
        ApPipe_QueueLine(out);
    }
    Log("AP PIPE: sent active quest locations to launcher (diffMax=%d)\n", diffMax);
}

/* "unsummon counts as kills": the dead-unit scans counted the player's OWN summons (skeletons/wolves/etc. */
static BOOL IsPlayerOwnedUnit(DWORD unit, DWORD playerId) {
    if (!playerId) return FALSE;
    {
        DWORD ot1 = SafeRead(unit + 0xE0), oi1 = SafeRead(unit + 0xE4);
        DWORD ot2 = SafeRead(unit + 0xEC), oi2 = SafeRead(unit + 0xF0);
        if (ot1 == 0 && oi1 == playerId) return TRUE;
        if (ot2 == 0 && oi2 == playerId) return TRUE;
    }
    return FALSE;
}

static void ScanMonsters(void) {
    void* p = Player(); if (!p) return;
    int currentArea = GetCurrentArea();
    if (IsTown(currentArea) || currentArea <= 0) return;

    __try {
        DWORD pPath = *(DWORD*)((DWORD)p + 0x2C); if (!pPath) return;
        DWORD pRoom = *(DWORD*)(pPath + 0x1C); if (!pRoom) return;

        /* Scan current room + nearby rooms */
        DWORD *ppRoomList = (DWORD*)SafeRead(pRoom + 0x24);
        int nNumRooms = (int)SafeRead(pRoom + 0x28);
        if (nNumRooms > 20) nNumRooms = 20;

        DWORD rooms[21];
        int roomCount = 0;
        rooms[roomCount++] = pRoom;
        if (ppRoomList && nNumRooms > 0) {
            for (int r = 0; r < nNumRooms && roomCount < 21; r++) {
                DWORD nr = SafeRead((DWORD)ppRoomList + r * 4);
                if (nr && nr != pRoom) rooms[roomCount++] = nr;
            }
        }

        for (int ri = 0; ri < roomCount; ri++) {
            DWORD unit = SafeRead(rooms[ri] + 0x2C);
            int chain = 0;
            while (unit && chain++ < 200) {
                DWORD type   = SafeRead(unit + 0x00);
                DWORD txtId  = SafeRead(unit + 0x04);
                DWORD unitId = SafeRead(unit + 0x0C);
                DWORD mode   = SafeRead(unit + 0x10);

                /* 1.9.0 Phase 2 — uber kill scan. */
                if (type == 1 && unitId != 0 &&
                    txtId >= UBER_MONID_MEPHISTO && txtId <= UBER_MONID_BAAL) {
                    DWORD ubFlags = SafeRead(unit + 0xC4);
                    BOOL  ubDead  = (mode == MODE_DEAD || (ubFlags & 0x10000));
                    if (ubDead) {
                        BOOL ubAlready = DeadList_Contains(unitId);   /* C10 ring */
                        if (!ubAlready) {
                            DeadList_Add(unitId);
                            __try {
                                Ubers_OnUnitDeathScan((void*)g_cachedPGame,
                                                      (void*)unit, txtId, unitId);
                            } __except(EXCEPTION_EXECUTE_HANDLER) {
                                Log("UBERS: death-scan EXCEPTION for unitId=%08X\n", unitId);
                            }
                        }
                    }
                }

                if (type == 1 && unitId != 0 && txtId < 700) {
                    /* Check death via mode AND unit flags for robustness. */
                    DWORD unitFlags = SafeRead(unit + 0xC4);
                    BOOL isDead = (mode == MODE_DEAD || (unitFlags & 0x10000));
                    if (isDead) {
                        BOOL alreadyCounted = DeadList_Contains(unitId);   /* C10 ring */
                        if (!alreadyCounted &&
                            IsPlayerOwnedUnit(unit, SafeRead((DWORD)p + 0x0C))) {
                            /* player's own summon dying (unsummon!) is NOT a kill. */
                            DeadList_Add(unitId);
                            alreadyCounted = TRUE;
                        }
                        if (!alreadyCounted) {
                            DeadList_Add(unitId);
                            if (currentArea < MAX_AREA_ID)
                                g_areaKills[KillDiff(g_currentDifficulty)][currentArea]++;

                            /* F1 Logbook stat. Read pMonsterData typeFlag (bit0=unique, bit1=superunique, bit2=champion) and OR in bit 0x80 for our "tracked boss" classification. */
                            {
                                int typeFlag = 0;
                                __try {
                                    DWORD pMon = *(DWORD*)(unit + 0x14);
                                    if (pMon) typeFlag = *(BYTE*)(pMon + 0x16);
                                } __except(EXCEPTION_EXECUTE_HANDLER) { typeFlag = 0; }
                                if (IsTrackedBoss(txtId)) typeFlag |= 0x80;
                                extern void Stats_OnMonsterKill(int, int, int, int);
                                Stats_OnMonsterKill((int)txtId, typeFlag,
                                                    currentArea, g_currentDifficulty);
                                /* 2.x — remember the last NORMAL monster killed (id + area) so a REWARD_TRAP spawns a real, client-loaded monster instead of a guessed formula id (crash-safe). */
                                if ((typeFlag & 0x07) == 0 && txtId > 0) {
                                    g_lastKillMonId = (int)txtId;
                                    g_lastKillArea  = currentArea;
                                }
                            }

                            /* 1.9.2 Extra check Cat 1 — Hell Bovine lifetime counter. */
                            if (txtId == 391) {
                                extern void Extra_OnCowKilled(void);
                                Extra_OnCowKilled();
                            }

                            /* custom-goal boss targets that are ordinary MonStats bosses rather than super-uniques. */
                            {
                                extern void CustomGoal_OnBossMonsterKilled(int txtId);
                                CustomGoal_OnBossMonsterKilled((int)txtId);
                            }

                            if (IsTrackedBoss(txtId)) {
                                Log("BOSS KILLED: txt=%d\n", txtId);
                                for (int a = 0; a < 5; a++)
                                    for (int q = 0; q < g_acts[a].num; q++)
                                        if (g_acts[a].quests[q].type == QTYPE_BOSS &&
                                            g_acts[a].quests[q].param == (int)txtId &&
                                            !g_acts[a].quests[q].completed)
                                            OnQuestComplete(&g_acts[a].quests[q]);
                            }

                            /* 2.x — Monster Revive Trap roll. */
                            if (g_monsterTrap && g_monTrapRemaining > 0 &&
                                txtId > 0 && txtId < 700 && !IsTrackedBoss(txtId) &&
                                !MonTrap_IsSpawned(unitId)) {
                                BOOL mtNormal = TRUE;
                                __try {
                                    DWORD pmd = *(DWORD*)(unit + 0x14);
                                    if (pmd && (*(BYTE*)(pmd + 0x16) & 0x07)) mtNormal = FALSE;
                                } __except(EXCEPTION_EXECUTE_HANDLER) { mtNormal = FALSE; }
                                if (mtNormal && (rand() % 100) < g_monsterTrapChance) {
                                    DWORD mtRoom = rooms[ri]; int mtX = 0, mtY = 0;
                                    __try {
                                        DWORD pPath = *(DWORD*)(unit + 0x2C);
                                        if (pPath) {
                                            DWORD pr = *(DWORD*)(pPath + 0x1C);
                                            if (pr) mtRoom = pr;
                                            mtX = (int)*(unsigned short*)(pPath + 0x02);
                                            mtY = (int)*(unsigned short*)(pPath + 0x06);
                                        }
                                    } __except(EXCEPTION_EXECUTE_HANDLER) { mtX = 0; }
                                    if (mtRoom && mtX > 0 && mtY > 0) {
                                        for (int qi = 0; qi < MONTRAP_MAX_PENDING; qi++) {
                                            if (!g_monTrapQueue[qi].active) {
                                                g_monTrapQueue[qi].active  = TRUE;
                                                g_monTrapQueue[qi].pRoom   = mtRoom;
                                                g_monTrapQueue[qi].x       = mtX;
                                                g_monTrapQueue[qi].y       = mtY;
                                                g_monTrapQueue[qi].monId   = (int)txtId;
                                                /* 2.x — total 8-10 (Marco), spawned 1-2/tick (stagger). */
                                                g_monTrapQueue[qi].count   = 8 + (rand() % 3);
                                                g_monTrapQueue[qi].spawnedSoFar = 0;
                                                g_monTrapQueue[qi].dueTick = GetTickCount() + (DWORD)g_monsterTrapDelayMs;
                                                g_monTrapQueue[qi].src     = 0; /* revive trap — "the slain rise" */
                                                g_monTrapRemaining--;
                                                Log("MONTRAP: kill monId=%d TRAPPED at (%d,%d) — %d copies in %dms (quota left %d)\n",
                                                    (int)txtId, mtX, mtY, g_monsterTrapCount, g_monsterTrapDelayMs, g_monTrapRemaining);
                                                break;
                                            }
                                        }
                                    }
                                }
                            }

                            /* Check for SuperUnique kill using pMonsterData typeFlag + hcIdx */
                            __try {
                                DWORD pMonData = *(DWORD*)(unit + 0x14); /* pMonsterData for monsters */
                                if (pMonData) {
                                    BYTE typeFlag = *(BYTE*)(pMonData + 0x16);
                                    /* the flat 50-kill budget was spent inside the Blood Moor, so every "why didn't this boss count?" report arrived with a log that stops long before the boss. */
                                    static int killLogCount = 0;
                                    if (typeFlag != 0 || killLogCount++ < 50)
                                        Log("KILL: txtId=%d typeFlag=0x%02X area=%d unitId=%d\n", txtId, typeFlag, currentArea, unitId);
                                    if (typeFlag & 0x02) { /* MONTYPEFLAG_SUPERUNIQUE */
                                        /* pMonData+0x26 stores the runtime ROW (SpawnSuperUnique: wBossHcIdx = nSuperUnique param = row). */
                                        WORD rawRow = *(WORD*)(pMonData + 0x26);
                                        extern int CustomBoss_SuHcIdxFromRow(int row);
                                        extern int CustomBoss_VanillaHcFromGate(int gateHc);
                                        int hcCol = CustomBoss_SuHcIdxFromRow((int)rawRow);
                                        BOOL isGateRow = (hcCol >= 100);
                                        int hcIdx = isGateRow ? CustomBoss_VanillaHcFromGate(hcCol) : hcCol;
                                        if (hcIdx < 0) hcIdx = hcCol;
                                        Log("SUPERUNIQUE KILLED: row=%d hcCol=%d -> hcIdx=%d gateRow=%d txtId=%d area=%d\n",
                                            rawRow, hcCol, hcIdx, isGateRow, txtId, currentArea);
                                        /* 1.9.2 Extra check Cat 1 — Cow King kill per difficulty. */
                                        if (hcIdx == 39) {
                                            extern void Extra_OnCowKingKilled(int diff);
                                            Extra_OnCowKingKilled(g_currentDifficulty);
                                            extern void CustomGoal_OnCowKingKilled(int diff);
                                            CustomGoal_OnCowKingKilled(g_currentDifficulty);
                                        }
                                        /* Custom goal super-unique targets. */
                                        {
                                            extern void CustomGoal_OnSuperUniqueKilled(int hcIdx);
                                            CustomGoal_OnSuperUniqueKilled((int)hcIdx);
                                        }
                                        /* identity registry (EX-1.0.23): still the discriminator in FALLBACK mode, i.e. */
                                        BOOL isOurGateBoss = CustomBoss_IsOurGateBoss(unitId);

                                        /* Hunt credit. With DEDICATED gate rows the old "double credit is the lesser evil" reasoning no longer applies: our gate boss no longer occupies the vanilla once-per-game slot, so the real Hunt target can always still spawn. A gate-row kill therefore does NOT credit the Hunt. In fallback mode (no gate rows, vanilla row shared) the kill is credited to BOTH as before — there the engine's 1-instance rule still applies and refusing would brick seeds. */
                                        BOOL matched = FALSE;
                                        for (int a2 = 0; a2 < 5 && !isGateRow; a2++)
                                            for (int q2 = 0; q2 < g_acts[a2].num; q2++) {
                                                Quest* sq = &g_acts[a2].quests[q2];
                                                if (sq->type == QTYPE_SUPERUNIQUE && sq->param == (int)hcIdx) {
                                                    matched = TRUE;
                                                    int sqid = sq->id;
                                                    if (sqid > 0 && sqid < MAX_QUEST_ID && !g_questCompleted[g_currentDifficulty][sqid])
                                                        OnQuestComplete(sq);
                                                    else
                                                        Log("SUPERUNIQUE: quest [%d] already done (diff=%d)\n", sqid, g_currentDifficulty);
                                                }
                                            }
                                        if (isGateRow) {
                                            Log("SUPERUNIQUE: dedicated gate row killed (hc=%d, vanilla=%d) — "
                                                "Hunt NOT credited (C7 v2, separate rows)\n", hcCol, hcIdx);
                                            /* SAY SO ON SCREEN. Gate rows reuse the real boss's Name key, so the guardian and the Hunt target are labelled identically in game. A tester killed "Boneash", got no Hunt credit and reported it as a broken check — the refusal above is correct, but silence made it indistinguishable from a bug. Only fires when a Hunt for this boss actually exists and is still open, so it never nags about bosses nobody is hunting. */
                                            for (int ag = 0; ag < 5; ag++)
                                                for (int qg = 0; qg < g_acts[ag].num; qg++) {
                                                    Quest* gq = &g_acts[ag].quests[qg];
                                                    if (gq->type != QTYPE_SUPERUNIQUE || gq->param != (int)hcIdx)
                                                        continue;
                                                    if (gq->id > 0 && gq->id < MAX_QUEST_ID &&
                                                        !g_questCompleted[g_currentDifficulty][gq->id]) {
                                                        char gmsg[160];
                                                        _snprintf(gmsg, sizeof(gmsg) - 1,
                                                                  "Gate guardian slain — the Hunt still wants the ORIGINAL %s",
                                                                  gq->name);
                                                        gmsg[sizeof(gmsg) - 1] = 0;
                                                        extern void ShowNotify(const char* s);
                                                        ShowNotify(gmsg);
                                                    }
                                                    ag = 5; break;   /* one match is enough */
                                                }
                                        }
                                        else if (isOurGateBoss)
                                            Log("SUPERUNIQUE: hcIdx=%d unit=%d is OUR gate boss on the VANILLA "
                                                "row (fallback mode) — Hunt credit allowed as before\n", hcIdx, unitId);
                                        if (!matched && !isGateRow)
                                            Log("SUPERUNIQUE: hcIdx=%d has NO matching quest!\n", hcIdx);

                                        /* Gate-boss kill detection. */
                                        if (g_zoneLockingOn) {
                                            int diff = g_currentDifficulty;
                                            if (diff >= 0 && diff <= 2) {
                                                for (int actIdx = 1; actIdx <= 5; actIdx++) {
                                                    int preload_id = g_actPreload[actIdx - 1][diff];
                                                    int num_gates = g_actRegions[actIdx - 1].num_gates;
                                                    for (int gi = 0; gi < num_gates; gi++) {
                                                        const GateSpawnDef* gd = Preload_GetGate(actIdx, preload_id, gi);
                                                        if (!gd) continue;
                                                        /* 1.8.5 FIX: relaxed area constraint. */
                                                        if (gd->base_su == (int)hcIdx) {
                                                            /* a kill on the DEDICATED gate row is definitive proof this was the gate boss: no discrimination needed. */
                                                            if (!isGateRow &&
                                                                !isOurGateBoss && CustomBoss_GateSuWasSpawned((int)hcIdx)) {
                                                                Log("GATE BOSS: hcIdx=%d unit=%d killed in area=%d is NOT our "
                                                                    "gate boss (ours spawned separately) — gate NOT cleared (C7)\n",
                                                                    hcIdx, unitId, currentArea);
                                                                continue;
                                                            }
                                                            if (gd->spawn_zone != currentArea) {
                                                                Log("GATE BOSS KILL (1.8.5 relaxed): hcIdx=%d killed in area=%d "
                                                                    "instead of gate zone=%d — accepting (1-per-game guard "
                                                                    "likely blocked our spawn at gate zone)\n",
                                                                    hcIdx, currentArea, gd->spawn_zone);
                                                            }
                                                            int slot = GateKey_SlotFromActGate(actIdx, gi);
                                                            /* 2.x — in combined mode (zone-lock + entrance-shuffle) the gate is cleared by a CONDITION (d2arch_keycond.c), not by this boss kill, so skip the grant here. */
                                                            extern BOOL KeyCond_Active(void);
                                                            BOOL alreadyHandled = (slot >= 0 && g_gateBossKilled[diff][slot]);
                                                            if (!alreadyHandled && slot >= 0 && !KeyCond_Active()) {
                                                                g_gateBossKilled[diff][slot] = TRUE;
                                                                Log("GATE BOSS KILLED: diff=%d act=%d gate=%d slot=%d (hcIdx=%d area=%d)\n",
                                                                    diff, actIdx, gi + 1, slot, hcIdx, currentArea);

                                                                if (g_apConnected) {
                                                                    /* AP mode — gate-boss kills are persisted to the checks file (same path quest completions use). */
                                                                    int locId = 47000 + diff * 1000 + actIdx * 10 + gi;
                                                                    Log("GATE KILL: AP mode — locId=%d will be sent via checks file\n", locId);
                                                                    char killBuf[64];
                                                                    _snprintf(killBuf, sizeof(killBuf),
                                                                        "Gate A%dG%d cleared", actIdx, gi + 1);
                                                                    ItemLogAddA(0, 3, killBuf, "check queued");
                                                                } else {
                                                                    /* Standalone — grant the gate key directly so the next region opens. */
                                                                    UnlockGateKey(diff, slot);
                                                                    Log("GATE KILL: standalone — gate key unlocked locally\n");
                                                                    char killBuf[64];
                                                                    _snprintf(killBuf, sizeof(killBuf),
                                                                        "Gate A%dG%d cleared", actIdx, gi + 1);
                                                                    ItemLogAddA(0, 3, killBuf, "key granted");
                                                                }

                                                                /* Persist immediately — kill survives a crash. */
                                                                SaveStateFile();
                                                                WriteChecksFile();
                                                            }
                                                            break;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    } else if (IsPresetOnlyBossClass(txtId) &&
                                               !CustomBoss_IsOurGateBoss(unitId) &&
                                               (typeFlag & 0x14) == 0) {
                                        /* Hunt credit WITHOUT the super-unique flag.
                                         *
                                         * (typeFlag & 0x14) — minion (0x10) + champion
                                         * (0x04), values read straight from Marco's
                                         * 2026-08-11 session log (minions=0x10, champs=
                                         * 0x0C, SU bosses=0x0A — so 0x08 rides along on
                                         * legit specials and must stay ALLOWED here).
                                         * A super-unique's MINIONS carry the boss row's
                                         * own MinionID class but NOT the SU flag, so they
                                         * used to fall into this class-match fallback:
                                         * Maegis killed a shuffled Treehead-row boss
                                         * (wearing Griswold's identity, credited via row)
                                         * and its brute2 minions ALSO credited "Hunt:
                                         * Treehead" here — one pack, two hunt checks. The
                                         * fallback exists for preset-spawned bosses that
                                         * miss the flag; those are never pack members. */
                                        extern int CustomBoss_SuHcIdxFromClass(int monClass);
                                        int fbHc = CustomBoss_SuHcIdxFromClass((int)txtId);
                                        Log("SUPERUNIQUE (no row flag): txtId=%d typeFlag=0x%02X -> hcIdx=%d area=%d\n",
                                            txtId, typeFlag, fbHc, currentArea);
                                        if (fbHc >= 0) {
                                            for (int a3 = 0; a3 < 5; a3++)
                                                for (int q3 = 0; q3 < g_acts[a3].num; q3++) {
                                                    Quest* fq = &g_acts[a3].quests[q3];
                                                    if (fq->type != QTYPE_SUPERUNIQUE || fq->param != fbHc)
                                                        continue;
                                                    if (fq->id > 0 && fq->id < MAX_QUEST_ID &&
                                                        !g_questCompleted[g_currentDifficulty][fq->id]) {
                                                        Log("SUPERUNIQUE (class match): crediting [%d] %s\n",
                                                            fq->id, fq->name);
                                                        OnQuestComplete(fq);
                                                    }
                                                }
                                        }
                                    }
                                } else {
                                    static int nullMonCount = 0;
                                    if (nullMonCount++ < 10)
                                        Log("KILL: txtId=%d pMonData=NULL\n", txtId);
                                }
                            } __except(EXCEPTION_EXECUTE_HANDLER) {
                                static int suErrCount = 0;
                                if (suErrCount++ < 10)
                                    Log("ScanMonsters: SuperUnique check EXCEPTION txtId=%d\n", txtId);
                            }
                        }
                    }
                }
                unit = SafeRead(unit + 0xE8);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    /* Update kill quest counts */
    for (int a = 0; a < 5; a++) {
        for (int q = 0; q < g_acts[a].num; q++) {
            Quest *quest = &g_acts[a].quests[q];
            if (quest->type == QTYPE_KILL && !quest->completed) {
                int areaId = quest->param;
                if (areaId >= 0 && areaId < MAX_AREA_ID) {
                    quest->killCount = g_areaKills[KillDiff(g_currentDifficulty)][areaId];
                    if (quest->killCount >= quest->killReq)
                        OnQuestComplete(quest);
                }
            }
        }
    }

    /* the old "drop the oldest half on overflow" compaction lived here. */
}


/* CLIENT QUEST-RECORD SYNC - the vanishing waypoint act tabs
 *
 * Repeated reports: every act tab except Act 1 disappears from the waypoint
 * menu, and talking to one or two town NPCs - ANY of them - brings them all
 * back at once. The waypoint BITS are never lost (packet 0x63 carries them
 * fresh every time the menu opens, and the save is fine); what goes stale is
 * the CLIENT's copy of the quest records, which is what the menu gates its
 * act tabs on. The client only learns those records from packet 0x28.
 *
 * Vanilla keeps that copy fresh as a side effect of its own act flow: the
 * caravan NPCs and every quest-bearing dialog resend the full quest buffer
 * (QUESTS_ActChange_*, Quests.cpp - each path ends in SendPacket0x28 with
 * the player's real flags). This mod moves players between acts through its
 * own gates, so a whole session can pass without one of those resends -
 * which is exactly why the old "talk to Jerhyn" advice worked, and why any
 * other quest NPC works just as well.
 *
 * The engine keeps that resend isolated in one two-argument function:
 *   QUESTS_UpdatePlayerFlags @ D2Game+0x66D20   __fastcall(pGame, pPlayer)
 * (D2MOO Quests.cpp:2510 - fetch pClient + pQuestData[difficulty], send
 * 0x28). We call it on every area change and every 10 seconds, so the
 * client's copy can never stay stale longer than that. Same resolve-and-
 * guard pattern as the cow spawner: vanilla RVA into the root D2Game.dll,
 * prolog verified before first use, silently disabled on mismatch. */
#define QSYNC_RVA_UPDATEPLAYERFLAGS  0x66D20
typedef void (__fastcall *QuestSyncUpdate_t)(void* pGame, void* pPlayer);

static QuestSyncUpdate_t QuestSync_Resolve(void) {
    static QuestSyncUpdate_t s_fn = NULL;
    static BOOL s_tried = FALSE;
    if (!s_tried) {
        /* push esi / mov esi,edx / push edi / push esi / mov edi,ecx / call
           - read out of the SHIPPED D2Game.dll, not out of D2MOO. */
        static const BYTE expect[8] = { 0x56, 0x8B, 0xF2, 0x57,
                                        0x56, 0x8B, 0xF9, 0xE8 };
        s_tried = TRUE;
        HMODULE h = GetModuleHandleA("D2Game.dll");
        if (h) {
            BYTE* p = (BYTE*)h + QSYNC_RVA_UPDATEPLAYERFLAGS;
            if (memcmp(p, expect, sizeof(expect)) == 0)
                s_fn = (QuestSyncUpdate_t)p;
        }
        Log("QUESTSYNC: QUESTS_UpdatePlayerFlags (D2Game+0x%X) %s\n",
            QSYNC_RVA_UPDATEPLAYERFLAGS,
            s_fn ? "resolved" : "PROLOG MISMATCH - quest sync disabled");
    }
    return s_fn;
}

static void QuestSync_Tick(void) {
    static DWORD s_lastArea = 0;
    static DWORD s_lastSendMs = 0;
    static BOOL  s_dead = FALSE;
    if (s_dead) return;

    QuestSyncUpdate_t fn = QuestSync_Resolve();
    if (!fn || !g_cachedPGame) return;

    DWORD area = GetCurrentArea();
    DWORD now = GetTickCount();
    BOOL due = FALSE;
    if (area && area != s_lastArea) { s_lastArea = area; due = TRUE; }
    if (now - s_lastSendMs >= 10000) due = TRUE;
    if (!due) return;

    void* pPlayer = CustomBoss_GetServerPlayer(g_cachedPGame);
    if (!pPlayer) return;
    __try {
        fn((void*)g_cachedPGame, pPlayer);
        s_lastSendMs = now;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        s_dead = TRUE;   /* never walk into a faulting engine call twice */
        Log("QUESTSYNC: exception during refresh - disabled for this session\n");
    }
}

static void CheckAreaReach(void) {
    static DWORD lastArea = 0;
    DWORD area = GetCurrentArea();
    if (area != lastArea) {
        lastArea = area;
        for (int a = 0; a < 5; a++)
            for (int q = 0; q < g_acts[a].num; q++)
                if (g_acts[a].quests[q].type == QTYPE_AREA &&
                    g_acts[a].quests[q].param == (int)area &&
                    !g_acts[a].quests[q].completed)
                    OnQuestComplete(&g_acts[a].quests[q]);
    }
}

/* Check waypoint activations — reads player waypoint flags via D2Common ordinal 11146 */
/* Waypoint check — scan for waypoint OBJECTS in nearby rooms. */

/* All waypoint object txtIds from Objects.txt */
static BOOL IsWaypointObject(DWORD txtId) {
    switch (txtId) {
        case 119: case 145: case 156: case 157: case 237:
        case 238: case 288: case 323: case 324: case 398:
        case 402: case 429: case 494: case 496: case 511:
        case 539: return TRUE;
        default: return FALSE;
    }
}

/* Map waypoint quest param (WP number) to area ID */
static int WaypointToArea(int wpNum) {
    switch (wpNum) {
        case 1: return 3;    case 2: return 4;    case 3: return 5;
        case 4: return 6;    case 5: return 27;   case 6: return 29;
        case 7: return 32;   case 8: return 35;
        case 10: return 48;  case 11: return 42;  case 12: return 57;
        case 13: return 43;  case 14: return 44;  case 15: return 52;
        case 16: return 74;  case 17: return 46;
        case 19: return 76;  case 20: return 77;  case 21: return 78;
        case 22: return 79;  case 23: return 80;  case 24: return 81;
        case 25: return 83;  case 26: return 101;
        case 28: return 106; case 29: return 107;
        case 31: return 111; case 32: return 112; case 33: return 113;
        case 34: return 115; case 35: return 123; case 36: return 117;
        case 37: return 118; case 38: return 129;
        default: return -1;
    }
}

/* Track which waypoint areas we've already completed to avoid repeated scans */
static BOOL g_wpAreaDone[200] = {0};

static void CheckWaypoints(void) {
    void* p = Player(); if (!p) return;
    int curArea = GetCurrentArea();
    if (curArea <= 0 || IsTown(curArea)) return;
    if (curArea >= 200) return;

    /* skip during pending zone teleport. */
    extern volatile int g_pendingZoneTeleport;
    if (g_pendingZoneTeleport > 0) return;

    /* Quick check: is there even a waypoint quest for this area? */
    int diff = g_currentDifficulty;
    BOOL hasWPQuest = FALSE;
    for (int a = 0; a < 5 && !hasWPQuest; a++) {
        for (int q = 0; q < g_acts[a].num; q++) {
            Quest* quest = &g_acts[a].quests[q];
            if (quest->type != QTYPE_WAYPOINT) continue;
            int wpArea = WaypointToArea(quest->param);
            if (wpArea == curArea) {
                int qid = quest->id;
                if (qid > 0 && qid < MAX_QUEST_ID && !g_questCompleted[diff][qid]) {
                    hasWPQuest = TRUE;
                }
                break;
            }
        }
    }
    if (!hasWPQuest) return;

    /* Scan rooms for waypoint objects with mode == OPENED (2) */
    __try {
        DWORD pPath = *(DWORD*)((DWORD)p + 0x2C); if (!pPath) return;
        DWORD pRoom = *(DWORD*)(pPath + 0x1C); if (!pRoom) return;

        DWORD *ppRoomList = (DWORD*)SafeRead(pRoom + 0x24);
        int nNumRooms = (int)SafeRead(pRoom + 0x28);
        if (nNumRooms > 20) nNumRooms = 20;

        DWORD rooms[21];
        int roomCount = 0;
        rooms[roomCount++] = pRoom;
        if (ppRoomList && nNumRooms > 0) {
            for (int r = 0; r < nNumRooms && roomCount < 21; r++) {
                DWORD nr = SafeRead((DWORD)ppRoomList + r * 4);
                if (nr && nr != pRoom) rooms[roomCount++] = nr;
            }
        }

        for (int ri = 0; ri < roomCount; ri++) {
            DWORD unit = SafeRead(rooms[ri] + 0x2C);
            int chain = 0;
            while (unit && chain++ < 200) {
                DWORD type  = SafeRead(unit + 0x00);
                DWORD txtId = SafeRead(unit + 0x04);
                DWORD mode  = SafeRead(unit + 0x10);

                /* type 2 = UNIT_OBJECT, mode >= 1 = OPERATING or OPENED (activated) */
                if (type == 2 && IsWaypointObject(txtId) && mode >= 1) {
                    /* This waypoint object is OPENED — complete matching quest */
                    for (int a = 0; a < 5; a++) {
                        for (int q = 0; q < g_acts[a].num; q++) {
                            Quest* quest = &g_acts[a].quests[q];
                            if (quest->type != QTYPE_WAYPOINT) continue;
                            int wpArea = WaypointToArea(quest->param);
                            if (wpArea != curArea) continue;
                            int qid = quest->id;
                            if (qid > 0 && qid < MAX_QUEST_ID && !g_questCompleted[diff][qid]) {
                                Log("WAYPOINT OBJECT OPENED: area=%d txtId=%d mode=%d quest=%d '%s'\n",
                                    curArea, txtId, mode, qid, quest->name);
                                OnQuestComplete(quest);
                            }
                        }
                    }
                }

                unit = SafeRead(unit + 0xE8); /* next unit */
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* Check D2's native quest completion flags for QTYPE_QUESTFLAG quests. */
/* C5 fix: backfill flag for CheckQuestFlags (mirrors g_milestoneBackfillDone). */
static BOOL g_questFlagBackfillDone = FALSE;
/* trust state for the player-side per-difficulty quest record used as a secondary source below: 0 = not yet decided, 1 = validated/trusted, -1 = canary failed/disabled. */
static int  g_playerQuestTrust = 0;
void QuestFlag_ResetBackfill(void) {
    g_questFlagBackfillDone = FALSE;
    g_playerQuestTrust = 0;
    Log("QUEST FLAG: backfill flag reset (next tick will silent-backfill)\n");
}

static void CheckQuestFlags(void) {
    SyncCachedPGame();   /* refresh to the LIVE game before reading +0x10F4 */
    if (!g_cachedPGame || !fnGetQuestState) return;
    /* g_cachedPGame stays stale (freed) after save&quit, so the guard above isn't enough; gate on the live client player getter (NULL the moment the game tears down) before reading off the freed game struct. */
    if (fnGetPlayer) {
        void* cp = NULL;
        __try { cp = fnGetPlayer(); } __except(EXCEPTION_EXECUTE_HANDLER) { cp = NULL; }
        if (!cp) return;
    }
    int diff = g_currentDifficulty;
    if (diff < 0 || diff > 2) return;

    /* Read pGame->pQuestControl->pQuestFlags */
    void* pQuestFlags = NULL;
    __try {
        DWORD pQuestControl = *(DWORD*)(g_cachedPGame + 0x10F4);
        if (!pQuestControl) return;
        pQuestFlags = *(void**)(pQuestControl + 0x0C);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!pQuestFlags) return;

    /* resolve the player's per-difficulty quest record as a SECONDARY source. */
    void* pPlayerQuest = NULL;
    __try {
        void* pSrvP = GetServerPlayer(g_cachedPGame);
        if (pSrvP) {
            DWORD pPlayerData = *(DWORD*)((DWORD)pSrvP + 0x14);
            if (pPlayerData)
                pPlayerQuest = *(void**)(pPlayerData + 0x10 + diff * 4);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { pPlayerQuest = NULL; }

    /* trust the player-side source ONLY once it agrees with the server global on every server-complete quest (the per-char record must be a superset of the global mirror). */
    if (pPlayerQuest && g_playerQuestTrust == 0) {
        int agree = 0, negOK = 0, plrTotalC = 0, total = 0;
        BOOL mismatch = FALSE;
        for (int a = 0; a < 5 && !mismatch; a++) {
            for (int q = 0; q < g_acts[a].num; q++) {
                Quest* cq = &g_acts[a].quests[q];
                if (cq->type != QTYPE_QUESTFLAG) continue;
                int dq = cq->param;
                if (dq <= 0 || dq >= MAX_D2_QUESTS) continue;
                BOOL srvC = FALSE, plrC = FALSE;
                __try {
                    srvC = fnGetQuestState(pQuestFlags, dq, 0) ||
                           fnGetQuestState(pQuestFlags, dq, 14) ||
                           fnGetQuestState(pQuestFlags, dq, 15);
                    plrC = fnGetQuestState(pPlayerQuest, dq, 0) ||
                           fnGetQuestState(pPlayerQuest, dq, 14) ||
                           fnGetQuestState(pPlayerQuest, dq, 15);
                } __except(EXCEPTION_EXECUTE_HANDLER) { mismatch = TRUE; break; }
                total++;
                if (plrC) plrTotalC++;
                if (srvC) { if (plrC) agree++; else { mismatch = TRUE; break; } }
                else if (!plrC) negOK++;   /* negative control: incomplete both sides */
            }
        }
        /* NEGATIVE CONTROL — a wrong offset pointing at readable garbage reads as mostly-TRUE bits (each quest ORs 3 states, ~87% chance TRUE) and would PASS the superset check above, then flood false completions. */
        if (!mismatch && plrTotalC >= total && total > 0 && negOK == 0 && agree < total) {
            mismatch = TRUE;
            Log("QF PLAYER: canary NEGATIVE-CONTROL failed (plr claims %d/%d complete, "
                "server only %d) — offset likely wrong\n", plrTotalC, total, agree);
        }
        if (mismatch) {
            g_playerQuestTrust = -1;
            Log("QF PLAYER: canary FAILED — player-side quest record disabled this session\n");
        } else if (agree > 0 && negOK > 0) {
            /* Require BOTH a positive (agrees on a completion) and a negative (agrees on an incompletion) before trusting the source. */
            g_playerQuestTrust = 1;
            Log("QF PLAYER: canary OK (%d agree-complete, %d agree-incomplete) — player-side record ENABLED\n",
                agree, negOK);
        } else if (agree > 0 && negOK == 0 && agree >= total && total > 0) {
            /* Fully-completed character: everything agrees on both sides. */
            g_playerQuestTrust = 1;
            Log("QF PLAYER: canary OK (all %d quests complete both sides) — player-side record ENABLED\n", agree);
        }
        /* else stay undecided, re-check next tick */
    }

    static int diagCount = 0;
    if (diagCount++ % 500 == 0) {
        /* Log first time to confirm we're reading the right buffer */
        __try {
            BOOL denStarted = fnGetQuestState(pQuestFlags, 1, 2);  /* A1Q1 STARTED */
            BOOL denPrimary = fnGetQuestState(pQuestFlags, 1, 13); /* A1Q1 PRIMARYGOALDONE */
            BOOL denReward  = fnGetQuestState(pQuestFlags, 1, 0);  /* A1Q1 REWARDGRANTED */
            Log("QF SERVER: pQF=%08X Den(1) started=%d primary=%d reward=%d\n",
                (DWORD)pQuestFlags, denStarted, denPrimary, denReward);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Log("QF SERVER: exception reading quest flags\n");
        }
    }

    for (int a = 0; a < 5; a++) {
        for (int q = 0; q < g_acts[a].num; q++) {
            Quest* quest = &g_acts[a].quests[q];
            if (quest->type != QTYPE_QUESTFLAG) continue;
            int qid = quest->id;
            if (qid <= 0 || qid >= MAX_QUEST_ID) continue;
            if (g_questCompleted[diff][qid]) continue;

            int d2QuestId = quest->param; /* D2 QUESTSTATEFLAG values: A1Q1=1, A1Q2=2, etc */
            if (d2QuestId < 0 || d2QuestId >= MAX_D2_QUESTS) continue;

            BOOL completed = FALSE;
            __try {
                /* Cain quest (A1Q4 = quest_id 4) special case: vanilla D2 auto-progresses Cain's quest to state 13 (PRIMARYGOALDONE) the moment the player enters Lut Gholein (or talks to Warriv to travel), even if they never went to Tristram. */
                if (d2QuestId == 4) {
                    completed = fnGetQuestState(pQuestFlags, d2QuestId, 0)  || /* REWARDGRANTED */
                                fnGetQuestState(pQuestFlags, d2QuestId, 14) || /* COMPLETEDNOW */
                                fnGetQuestState(pQuestFlags, d2QuestId, 15);   /* COMPLETEDBEFORE */
                    /* NOTE: state 13 (PRIMARYGOALDONE) intentionally NOT accepted for Cain — that's the vanilla auto-progress trigger we want to filter out. */
                } else {
                    completed = fnGetQuestState(pQuestFlags, d2QuestId, 0)  || /* REWARDGRANTED */
                                fnGetQuestState(pQuestFlags, d2QuestId, 13) || /* PRIMARYGOALDONE */
                                fnGetQuestState(pQuestFlags, d2QuestId, 14) || /* COMPLETEDNOW */
                                fnGetQuestState(pQuestFlags, d2QuestId, 15);   /* COMPLETEDBEFORE */
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }

            /* OR in the player-side per-difficulty record (trusted only after the canary passed). */
            if (!completed && pPlayerQuest && g_playerQuestTrust == 1) {
                __try {
                    if (d2QuestId == 4) {
                        completed = fnGetQuestState(pPlayerQuest, 4, 0)  ||
                                    fnGetQuestState(pPlayerQuest, 4, 14) ||
                                    fnGetQuestState(pPlayerQuest, 4, 15);
                    } else {
                        completed = fnGetQuestState(pPlayerQuest, d2QuestId, 0)  ||
                                    fnGetQuestState(pPlayerQuest, d2QuestId, 13) ||
                                    fnGetQuestState(pPlayerQuest, d2QuestId, 14) ||
                                    fnGetQuestState(pPlayerQuest, d2QuestId, 15);
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }

            if (completed) {
                if (!g_questFlagBackfillDone) {
                    /* C5 fix: first tick after load — silent-mark already- completed quests WITHOUT firing (no AP check flood). */
                    g_questCompleted[diff][qid] = TRUE;
                } else {
                    Log("QUEST FLAG DETECTED: '%s' d2Id=%d diff=%d\n", quest->name, d2QuestId, diff);
                    OnQuestComplete(quest);
                }
            }
        }
    }

    /* C5 fix: end of the first (backfill) tick — switch to normal firing so subsequent ticks send only newly-completed quest checks. */
    if (!g_questFlagBackfillDone) {
        g_questFlagBackfillDone = TRUE;
        Log("QUEST FLAG: silent-backfilled already-completed quests on character load (diff=%d)\n", diff);
    }
}

/* Check player level for QTYPE_LEVEL quests. */
/* 1.9.3 fix: backfill flag for CheckLevelMilestones, file-scope so OnCharacterLoad can reset it via Milestone_ResetBackfill(). */
static BOOL g_milestoneBackfillDone = FALSE;

void Milestone_ResetBackfill(void) {
    g_milestoneBackfillDone = FALSE;
    Log("LEVEL MILESTONE: backfill flag reset (next tick will silent-backfill)\n");
}

static void CheckLevelMilestones(void) {
    void* p = Player();
    if (!p || !fnGetStat) return;
    int level = (int)fnGetStat(p, 12, 0); /* STAT_LEVEL=12 */
    int diff = g_currentDifficulty;

    /* The bug we fix here: a character that loads at e.g. */
    if (!g_milestoneBackfillDone) {
        int silentMarks = 0;
        for (int a = 0; a < 5; a++) {
            for (int q = 0; q < g_acts[a].num; q++) {
                Quest* quest = &g_acts[a].quests[q];
                if (quest->type != QTYPE_LEVEL) continue;
                int qid = quest->id;
                if (qid <= 0 || qid >= MAX_QUEST_ID) continue;
                if (!g_questCompleted[diff][qid] && level >= quest->param) {
                    g_questCompleted[diff][qid] = TRUE;
                    silentMarks++;
                }
            }
        }
        g_milestoneBackfillDone = TRUE;
        if (silentMarks > 0) {
            Log("LEVEL MILESTONE: silent-backfilled %d already-met milestones on character load (level=%d diff=%d)\n",
                silentMarks, level, diff);
        }
        return;  /* skip normal logic this tick — let backfill take effect */
    }

    for (int a = 0; a < 5; a++) {
        for (int q = 0; q < g_acts[a].num; q++) {
            Quest* quest = &g_acts[a].quests[q];
            if (quest->type != QTYPE_LEVEL) continue;
            int qid = quest->id;
            if (qid <= 0 || qid >= MAX_QUEST_ID) continue;

            /* Auto-complete on current difficulty if already done on ANY lower difficulty */
            if (!g_questCompleted[diff][qid]) {
                BOOL doneOnLower = FALSE;
                for (int d = 0; d < diff; d++) {
                    if (g_questCompleted[d][qid]) { doneOnLower = TRUE; break; }
                }
                if (doneOnLower || level >= quest->param)
                    OnQuestComplete(quest);
            }
        }
    }
}

/* Tick-time reconciliation between D2's actual quest state and our Zone Locking gate-key store. */
static void ReconcileGatesFromQuests(void) {
    if (!g_zoneLockingOn) return;
    int diff = g_currentDifficulty;
    if (diff < 0 || diff > 2) return;

    static const int actBossQid[6] = { 0, 6, 106, 206, 303, 406 };
    int implied = 0;
    for (int actDone = 1; actDone <= 5; actDone++) {
        int qid = actBossQid[actDone];
        if (qid <= 0 || qid >= MAX_QUEST_ID) continue;
        if (!g_questCompleted[diff][qid]) continue;

        for (int act = 1; act <= actDone; act++) {
            int numGates = g_actRegions[act - 1].num_gates;
            for (int gi = 0; gi < numGates; gi++) {
                int slot = GateKey_SlotFromActGate(act, gi);
                if (slot < 0) continue;
                if (!g_gateBossKilled[diff][slot]) {
                    g_gateBossKilled[diff][slot] = TRUE;
                    implied++;
                }
                if (!g_gateKeyReceived[diff][slot]) {
                    UnlockGateKey(diff, slot);
                    implied++;
                }
            }
        }
    }

    if (implied > 0) {
        Log("RECONCILE-TICK: %d gate-state changes implied from D2 quests (diff=%d)\n",
            implied, diff);
        SaveStateFile();
    }
}

/* Per-tick scan: kill detection, area-reach, waypoints, quest flags, and the 1.8.2 zone-locking reconciliation that back-fills gate keys from D2's actual quest progress. */
/* 2.x — read the LIVE game difficulty (0=Normal, 1=Nightmare, 2=Hell) from the server game struct. */
static int GetCurrentDifficulty(void) {
    SyncCachedPGame();   /* refresh to the LIVE game before reading +0x6D */
    if (!g_cachedPGame) return -1;
    __try {
        int d = *(BYTE*)(g_cachedPGame + 0x6D);
        if (d >= 0 && d <= 2) return d;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}

/* 2.x — keep g_currentDifficulty (and the working quest struct) aligned with the difficulty the player is ACTUALLY in. */
static void Quests_SyncDifficulty(int newDiff) {
    if (newDiff < 0 || newDiff > 2 || newDiff == g_currentDifficulty) return;
    int oldDiff = g_currentDifficulty;
    g_currentDifficulty = newDiff;
    for (int a = 0; a < 5; a++) {
        for (int q = 0; q < g_acts[a].num; q++) {
            Quest* qq = &g_acts[a].quests[q];
            qq->completed = (qq->id > 0 && qq->id < MAX_QUEST_ID)
                          ? g_questCompleted[newDiff][qq->id] : FALSE;
            /* Re-point at the NEW difficulty's tally instead of zeroing. */
            int aid = (qq->type == QTYPE_KILL) ? qq->param : -1;
            qq->killCount = (aid >= 0 && aid < MAX_AREA_ID)
                          ? g_areaKills[KillDiff(newDiff)][aid] : 0;
        }
    }
    Log("Difficulty change: %d -> %d — quest tracker re-synced to %s\n",
        oldDiff, newDiff, g_diffNames[newDiff]);
}

static void RunCheckDetection(void) {
    if (!Player()) return;
    /* 2.x — align quest tracking with the live game difficulty BEFORE any detection runs, so NM/Hell completions hit g_questCompleted[1]/[2], the tracker shows the right difficulty, and missed checks self-heal. */
    {
        int liveDiff = GetCurrentDifficulty();
        if (liveDiff >= 0) Quests_SyncDifficulty(liveDiff);
    }
    /* SEH net around the whole detection suite. */
    __try {
        DBG_CRUMB("detect:monsters");   ScanMonsters();
        DBG_CRUMB("detect:areareach");  CheckAreaReach();
        DBG_CRUMB("detect:waypoints");  CheckWaypoints();
        DBG_CRUMB("detect:questsync");  QuestSync_Tick();
        DBG_CRUMB("detect:questflags"); CheckQuestFlags();
        DBG_CRUMB("detect:levelmiles"); CheckLevelMilestones();
        DBG_CRUMB("detect:reconcile");  ReconcileGatesFromQuests();
        DBG_CRUMB("detect:idle");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        /* stale game state during teardown — skip this detection tick */
    }
}

