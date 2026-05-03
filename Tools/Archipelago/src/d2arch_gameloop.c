static D2DebugGame_t g_origD2DebugGame = NULL;
static BYTE  g_debugGameTrampoline[32] = {0};  /* big enough for any prologue */

/* Forward declarations — Treasure Cow module is included AFTER this file
 * so ProcessPendingGameTick can call it. */
static void TreasureCow_Tick(void* pGame);
static void TreasureCow_Reset(void);

/* 1.8.0: quest-type toggle check lives in d2arch_questlog.c (included later
 * in the TU). Forward-declare so OnQuestComplete can gate reward+check on
 * whether the quest type is enabled in settings. */
static BOOL IsQuestTypeActive(int questType);

/* 1.9.0 Phase 2: forward decl for the uber death-scan callback.
 * Defined as non-static in d2arch_ubers.c (which is now included BEFORE
 * this file in d2arch.c, so the symbol is already in scope at link
 * time within the unity TU). The unit-death walk below calls it for
 * every dead type==1 unit so finale-ubers can be detected and the
 * Hellfire Torch dropped. */
extern void Ubers_OnUnitDeathScan(void* pGame, void* pUnit, DWORD txtId, DWORD unitId);

/* Get server-side player + room from pGame's unit list.
 * pGame->pUnitList is at offset +0x1C in D2GameStrc.
 * Players are unit type 0 → pUnitList[0] is D2UnitStrc*[128] hash table.
 * In single-player there's exactly 1 player. We scan hash buckets. */
static void* GetServerPlayer(DWORD pGame) {
    if (!pGame) return NULL;
    __try {
        /* D2GameStrc->pUnitList is at offset 0x1120 (from D2MOO Game.h)
         * pUnitList[5][128] — index 0 = players (UNIT_PLAYER remaps to 0)
         * Each entry in pUnitList[0][0..127] is a D2UnitStrc* hash bucket */
        DWORD* pPlayerBuckets = (DWORD*)(pGame + 0x1120);
        /* Scan 128 hash buckets for first player unit */
        for (int i = 0; i < 128; i++) {
            DWORD pUnit = pPlayerBuckets[i];
            if (pUnit) return (void*)pUnit;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("GetServerPlayer: exception reading pGame+0x1120\n");
    }
    return NULL;
}

/* Our wrapper — called instead of D2DebugGame.
 * Captures pGame AND processes trap spawns in the correct server context.
 * This runs inside GAME_UpdateProgress — same context as debug menu spawner. */
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

    /* Note: CustomBoss_Tick() was originally here but moved to
     * ProcessPendingGameTick below — HookD2DebugGame is unreliable on many
     * user setups, while GAME_UpdateClients always fires. */

    /* Process trap spawns HERE — in GAME_UpdateProgress context, like the debug menu */
    if (g_pendingTrapSpawn > 0 && fnSpawnSuperUnique && pGame) {
        __try {
            void* pServerPlayer = GetServerPlayer((DWORD)pGame);
            if (!pServerPlayer) {
                static int noPlayerLog = 0;
                if (noPlayerLog++ < 3) Log("TRAP: GetServerPlayer returned NULL\n");
            } else {
                DWORD pPath = *(DWORD*)((DWORD)pServerPlayer + 0x2C);
                if (!pPath) {
                    Log("TRAP: server player pPath is NULL\n");
                } else {
                    DWORD pRoom = *(DWORD*)(pPath + 0x1C);
                    int nX = (int)*(unsigned short*)(pPath + 0x02);
                    int nY = (int)*(unsigned short*)(pPath + 0x06);

                    Log("TRAP: serverPlayer=%08X pPath=%08X pRoom=%08X coords=(%d,%d)\n",
                        (DWORD)pServerPlayer, pPath, pRoom, nX, nY);

                    if (pRoom && nX > 0 && nY > 0) {
                        /* 1.8.0 — Level-matched trap spawn.
                         * Old behavior always used Baal Subjects (SU 61-65, lvl 85)
                         * which instakilled low-level characters. Now we pick a SU
                         * tier appropriate to player's current character level:
                         *   lvl 1-15:  early SUs (Bishibosh, Rakanishu, Corpsefire, Bonebreak)
                         *   lvl 15-30: mid Act 2-3 (Fangskin, Bloodwitch, Coldworm, Fire Eye)
                         *   lvl 30-50: Act 3-4 (Endugu, Stormtree, Winged Death, Tormentor)
                         *   lvl 50-70: Act 5 mid-tier (Eyeback, Snapchip, Sharp Tooth)
                         *   lvl 70+:   Baal Subjects (old behavior — end-game difficulty)
                         */
                        int charLevel = 1;
                        if (fnGetStat) {
                            __try { charLevel = fnGetStat(pServerPlayer, 12, 0); }  /* STAT_LEVEL */
                            __except(EXCEPTION_EXECUTE_HANDLER) { charLevel = 1; }
                        }
                        if (charLevel < 1) charLevel = 1;
                        if (charLevel > 99) charLevel = 99;

                        const int* trapBossIds;
                        int numBosses;
                        static const int tierLow[]  = { 0, 1, 2, 3, 40 };          /* 5 — lvl 1-15 */
                        static const int tierMidL[] = { 4, 7, 11, 12, 14, 15, 16 };/* 7 — lvl 15-30 */
                        static const int tierMidH[] = { 22, 23, 25, 32, 33, 34 };  /* 6 — lvl 30-50 */
                        static const int tierHigh[] = { 19, 48, 50, 52, 53, 56 };  /* 6 — lvl 50-70 */
                        static const int tierEnd[]  = { 61, 62, 63, 64, 65 };      /* 5 — lvl 70+ */

                        if      (charLevel < 15) { trapBossIds = tierLow;  numBosses = 5; }
                        else if (charLevel < 30) { trapBossIds = tierMidL; numBosses = 7; }
                        else if (charLevel < 50) { trapBossIds = tierMidH; numBosses = 6; }
                        else if (charLevel < 70) { trapBossIds = tierHigh; numBosses = 6; }
                        else                     { trapBossIds = tierEnd;  numBosses = 5; }

                        DWORD tickSeed = GetTickCount();
                        int suId = trapBossIds[(tickSeed ^ (tickSeed >> 16)) % numBosses];

                        Log("TRAP: charLvl=%d tier-picked SU=%d (pool size %d)\n",
                            charLevel, suId, numBosses);

                        Log("TRAP: Spawning SuperUnique #%d at (%d,%d) pGame=%08X pRoom=%08X\n",
                            suId, nX, nY, (DWORD)pGame, pRoom);

                        void* spawned = fnSpawnSuperUnique(pGame, (void*)pRoom, nX, nY, suId);
                        if (spawned) {
                            Log("TRAP: SuperUnique #%d spawned OK!\n", suId);
                        } else {
                            for (int retry = 0; retry < 5; retry++) {
                                DWORD retrySeed = GetTickCount();
                                suId = trapBossIds[((retrySeed + retry * 7919) ^ (retrySeed >> 8)) % numBosses];
                                spawned = fnSpawnSuperUnique(pGame, (void*)pRoom, nX, nY, suId);
                                if (spawned) { Log("TRAP: Retry %d: #%d spawned!\n", retry+1, suId); break; }
                            }
                            if (!spawned) Log("TRAP: All retries failed\n");
                        }
                        g_pendingTrapSpawn--;
                    } else {
                        Log("TRAP: bad room/coords: pRoom=%08X nX=%d nY=%d\n", pRoom, nX, nY);
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Log("TRAP: Exception in spawn!\n");
            g_pendingTrapSpawn--;
        }
    }

    /* 1.8.0 cleanup: Treasure Cow Levels.txt approach comment extracted */

    /* Cheat commands processed in ProcessPendingGameTick */

    /* Apply pending rewards LIVE via server-side fnAddStat.
     * Called on GetServerPlayer in game tick context — same approach
     * as D2MOO's own quest reward code (A1Q1.cpp: STATLIST_AddUnitStat). */
    {
        static int s_appliedGold = 0;
        static int s_appliedStatPts = 0;
        static int s_appliedSkillPts = 0;
        void* pPlayer = GetServerPlayer((DWORD)pGame);
        if (pPlayer && fnAddStat) {
            int newGold = g_pendingRewardGold - s_appliedGold;
            int newStatPts = g_pendingRewardStatPts - s_appliedStatPts;
            int newSkillPts = g_pendingRewardSkillPts - s_appliedSkillPts;
            __try {
                if (newGold > 0) {
                    fnAddStat(pPlayer, STAT_GOLD, newGold, 0);
                    s_appliedGold = g_pendingRewardGold;
                }
                if (newStatPts > 0) {
                    fnAddStat(pPlayer, STAT_STATPTS, newStatPts, 0);
                    s_appliedStatPts = g_pendingRewardStatPts;
                }
                if (newSkillPts > 0) {
                    fnAddStat(pPlayer, STAT_NEWSKILLS, newSkillPts, 0);
                    s_appliedSkillPts = g_pendingRewardSkillPts;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                static int addStatErr = 0;
                if (addStatErr++ < 3)
                    Log("Live reward apply exception\n");
            }
        }
        /* Reset applied counters when pending is zeroed (after .d2s write) */
        if (g_pendingRewardGold == 0) s_appliedGold = 0;
        if (g_pendingRewardStatPts == 0) s_appliedStatPts = 0;
        if (g_pendingRewardSkillPts == 0) s_appliedSkillPts = 0;
    }

    /* Zone Gating: use LEVEL_WarpUnit from server context.
     * pGame+0x88 = pClientList (D2ClientStrc*), pClient+0x174 = pPlayer (D2UnitStrc*)
     * LEVEL_WarpUnit at D2Game+0x3C410: __fastcall(pGame, pPlayer, levelId, tileCalc) */
    if (g_pendingZoneTeleport > 0 && pGame) {
        int townArea = g_pendingZoneTeleport;
        g_pendingZoneTeleport = 0;

        static FARPROC fnWarpUnit = NULL;
        static BOOL warpResolved = FALSE;
        if (!warpResolved) {
            warpResolved = TRUE;
            HMODULE hD2Game = GetModuleHandleA("D2Game.dll");
            if (hD2Game) {
                fnWarpUnit = (FARPROC)((DWORD)hD2Game + 0xC410);
                Log("WARP: resolved LEVEL_WarpUnit at %08X (D2Game=%08X)\n",
                    (DWORD)fnWarpUnit, (DWORD)hD2Game);
            }
        }

        /* (Hook prologue probe lives in ProcessPendingGameTick now —
         * the previous placement was inside g_pendingZoneTeleport
         * gating which only fires on death-respawn, so the probe
         * never ran during normal play.) */

        if (fnWarpUnit) {
            __try {
                /* Scan pGame for the client list pointer.
                 * D2MOO says pClientList at +0x88, pPlayer at pClient+0x174.
                 * But D2MOO reference may not match our D2.Detours build exactly.
                 * Dump nearby offsets to find the right one. */
                static BOOL dumpDone = FALSE;
                if (!dumpDone) {
                    dumpDone = TRUE;
                    Log("WARP DIAGNOSTIC: pGame=%08X, scanning for client pointer...\n", (DWORD)pGame);
                    for (int off = 0x00; off <= 0x200; off += 4) {
                        DWORD val = 0;
                        __try { val = *(DWORD*)((DWORD)pGame + off); } __except(1) { continue; }
                        if (val < 0x10000 || val > 0x7FFFFFFF) continue;
                        /* Check if this looks like a D2ClientStrc by reading offset 0x174 */
                        __try {
                            DWORD maybeUnit = *(DWORD*)(val + 0x174);
                            if (maybeUnit > 0x10000 && maybeUnit < 0x7FFFFFFF) {
                                /* Check if the unit looks valid: unit+0x00 = unitType (should be 0 for player) */
                                DWORD unitType = *(DWORD*)(maybeUnit + 0x00);
                                DWORD classId = *(DWORD*)(maybeUnit + 0x04);
                                DWORD unitId = *(DWORD*)(maybeUnit + 0x08);
                                Log("  pGame+0x%03X = %08X -> +0x174 = %08X (type=%d class=%d id=%d)\n",
                                    off, val, maybeUnit, unitType, classId, unitId);
                            }
                        } __except(1) {}
                    }
                }

                /* Try to get player unit */
                DWORD pClient = 0;
                DWORD pUnit = 0;

                /* Try offset 0x88 first (D2MOO reference) */
                __try { pClient = *(DWORD*)((DWORD)pGame + 0x88); } __except(1) {}
                if (pClient > 0x10000) {
                    __try { pUnit = *(DWORD*)(pClient + 0x174); } __except(1) {}
                }

                if (pUnit > 0x10000) {
                    typedef void (__fastcall *WarpUnit_t)(void*, void*, int, int);
                    Log("WARP: calling LEVEL_WarpUnit(pGame=%08X, pUnit=%08X, level=%d, tile=0)\n",
                        (DWORD)pGame, pUnit, townArea);
                    ((WarpUnit_t)fnWarpUnit)(pGame, (void*)pUnit, townArea, 0);
                    Log("WARP: LEVEL_WarpUnit returned OK!\n");
                } else {
                    Log("WARP: could not find player unit (pClient=%08X, pUnit=%08X)\n", pClient, pUnit);
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Log("WARP: LEVEL_WarpUnit CRASHED!\n");
            }
        }
    }

    /* Call original D2DebugGame */
    return ((D2DebugGame_t)(void*)g_debugGameTrampoline)(pGame);
}

static void ProcessPendingGameTick(void) {
    g_pendingGold = 0;

    /* 1.9.0 — install the Logbook event hooks once D2Game.dll is loaded.
     * Hooks_InstallLogbookHooks is internally idempotent (static guard),
     * so running it on every tick is harmless after the first install.
     * Using the tick site means we don't have to find a DLL_PROCESS_ATTACH-
     * adjacent point that's already past D2Game's load. */
    {
        HMODULE hG = GetModuleHandleA("D2Game.dll");
        if (hG) {
            extern void Hooks_InstallLogbookHooks(HMODULE hD2Game);
            Hooks_InstallLogbookHooks(hG);
        }
    }

    /* Get pGame from D2Game's global gpGame_6FD457FC (base + 0x1157FC).
     * This is always valid when a game is running. */
    if (!g_cachedPGame && hD2Game) {
        __try {
            DWORD* ppGame = (DWORD*)((DWORD)hD2Game + 0x1157FC);
            if (*ppGame) {
                g_cachedPGame = *ppGame;
                Log("pGame captured from D2Game global: %08X\n", g_cachedPGame);
            }
        } __except(1) {}
    }

    /* 1.8.0 NEW: Custom Boss framework — moved here from HookD2DebugGame
     * because GAME_UpdateClients fires reliably on all user setups whereas
     * D2DebugGame hook is environment-dependent. */
    if (g_cachedPGame) {
        CustomBoss_Tick((void*)g_cachedPGame);
        TreasureCow_Tick((void*)g_cachedPGame);
    }

    /* 1.9.0 NEW: F1 Collection page tick — scans player inventory and
     * polls gold delta. Uses CLIENT player unit (not pGame), so we
     * resolve via fnGetPlayer which is already set up by InitAPI. */
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
                Stats_OnGameTick(pCliPlayer);  /* 1.9.0 — playtime + death-edge */
                Extra_PollMerc(pCliPlayer);    /* 1.9.2 — merc hire/resurrect/level */
                Extra_PollNpcDialogue(pCliPlayer); /* 1.9.2 — NPC near + stationary */
                CustomGoal_PollBulkTargets();  /* 1.9.2 — custom goal bulk completions */
            }
        }
    }

    /* REINVEST: re-apply saved skill points from game tick context.
     * This runs in D2Game thread where server player is available. */
    if (g_reinvestPending && g_cachedPGame) {
        /* 1.7.1 SAFETY NET: if reinvest hasn't fired within 30 seconds of
         * being queued (g_reinvestTime was set to NOW+2000 on enqueue),
         * grant the pending points back to the NEWSKILLS free pool. This
         * prevents permanent skill-point loss when pGame never resolves,
         * the server player is never found, or the user AFKs at a screen
         * where no input is detected. */
        if (g_reinvestTime > 0 &&
            (int)(GetTickCount() - g_reinvestTime) > 30000) {
            Log("REINVEST TIMEOUT: 30s elapsed without apply — "
                "granting %d pending entries back to NEWSKILLS pool\n",
                g_reinvestCount);
            GrantPendingReinvestAsFreePool();
            g_reinvestPending = FALSE;
            g_reinvestCount = 0;
            g_reinvestTime = 0;
            goto skipReinvest;
        }

        /* Wait for first player input (any key or mouse click) = player is in game */
        BOOL anyInput = FALSE;
        for (int ki = 0; ki < 256; ki++) {
            if (GetAsyncKeyState(ki) & 0x8000) { anyInput = TRUE; break; }
        }
        if (!anyInput) goto skipReinvest;

        /* Re-read pGame fresh */
        __try {
            DWORD* ppG2 = (DWORD*)((DWORD)hD2Game + 0x1157FC);
            if (*ppG2) g_cachedPGame = *ppG2;
        } __except(1) {}
        void* pSrvR = GetServerPlayer(g_cachedPGame);
        void* pCliR = Player();
        if (pSrvR && pCliR) {
            /* 1.7.1 ATOMIC CONSUME: rename reinvest file to `.consuming`
             * so it's hidden from OnCharacterLoad's rehydrate path but
             * survives a crash mid-apply. If we crash, next load sees
             * `.dat.consuming` and can either replay via rename-back or
             * fall back to GrantPendingReinvestAsFreePool. */
            char consumingPath[MAX_PATH];
            if (!BeginReinvestConsume(consumingPath, sizeof(consumingPath))) {
                /* File missing or rename failed — skip this tick and retry
                 * next. The timeout safety-net above will eventually kick
                 * in if the file is truly gone. */
                Log("REINVEST: BeginReinvestConsume failed, skipping tick\n");
                goto skipReinvest;
            }
            typedef void* (__stdcall *AddSkill_t)(void* pUnit, int nSkillId);
            static AddSkill_t fnAddSkillR = NULL;
            if (!fnAddSkillR && hD2Common) {
                fnAddSkillR = (AddSkill_t)GetProcAddress(hD2Common, (LPCSTR)10952);
            }
            /* 1.8.0 CRITICAL: apply Skills.txt patches (maxlvl 20->99, cross-class
             * animations, weapon restrictions) BEFORE reinvest fires. Otherwise
             * SKILLS_AddSkill reads the unpatched wMaxLvl=0 (defaults to 20) and
             * silently refuses to increment past 20 — causing saved levels 21+
             * to reset to 20 after game-exit. PatchAllSkillAnimations is
             * idempotent (gated by g_animPatchApplied), so calling it here is
             * safe even if PeriodicApply already ran. */
            PatchAllSkillAnimations();
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
                        __try { fnAddStat(pSrvR, 5, -1, 0); }
                        __except(1) { Log("REINVEST: AddStat(NEWSKILLS,-1) exception\n"); }
                    }
                }
                Log("REINVEST: skill %d = %d pts (btnIdx=%d)\n",
                    skId, skPts, g_reinvestBtnIdx[ri]);
                /* 1.8.2 FIX: write to the per-button file matching the
                 * ORIGINAL btnIdx (= tab*10+slot) the user invested at —
                 * NOT the compact reinvest array index `ri`. The previous
                 * version corrupted the panel display whenever any slot
                 * was un-leveled (gap), because subsequent invested skills
                 * collapsed onto buttons 0..N-1 instead of staying at their
                 * real layout positions. */
                {
                    int btnIdx = g_reinvestBtnIdx[ri];
                    if (btnIdx < 0 || btnIdx >= 30) btnIdx = ri; /* defensive */
                    char rsp[MAX_PATH], rsfx[32];
                    GetCharFileDir(rsp, MAX_PATH);
                    if (btnIdx == 0) strcat(rsp, "d2arch_fireball_");
                    else { sprintf(rsfx, "d2arch_skill%d_", btnIdx + 1); strcat(rsp, rsfx); }
                    strcat(rsp, g_charName); strcat(rsp, ".dat");
                    FILE* rsf = fopen(rsp, "w");
                    if (rsf) { fprintf(rsf, "%d", skPts); fclose(rsf); }
                }
            }
            g_reinvestPending = FALSE;
            g_reinvestCount = 0;
            g_reinvestTime = 0;
            g_reinvestDone = TRUE; /* trigger skill tree panel to reload levels */
            /* 1.7.1: atomic cleanup — delete the .consuming file now that
             * all points have been applied successfully. On crash between
             * BeginReinvestConsume above and this EndReinvestConsume call,
             * the .consuming file remains for recovery. */
            EndReinvestConsume(consumingPath);
        }
    }
    skipReinvest:

    /* Consume server-side pending rewards — give directly to server player */
    if (g_cachedPGame && fnAddStat &&
        (g_serverPendingGold > 0 || g_serverPendingStatPts > 0 || g_serverPendingSkillPts > 0)) {
        void* pSrvReward = GetServerPlayer(g_cachedPGame);
        if (pSrvReward) {
            __try {
                if (g_serverPendingGold > 0) {
                    int g = g_serverPendingGold; g_serverPendingGold = 0;
                    fnAddStat(pSrvReward, 14, g, 0); /* STAT_GOLD */
                    Log("SERVER REWARD: +%d gold\n", g);
                }
                if (g_serverPendingStatPts > 0) {
                    int s = g_serverPendingStatPts; g_serverPendingStatPts = 0;
                    fnAddStat(pSrvReward, 4, s, 0); /* STAT_STATPTS */
                    Log("SERVER REWARD: +%d stat points\n", s);
                }
                if (g_serverPendingSkillPts > 0) {
                    int k = g_serverPendingSkillPts; g_serverPendingSkillPts = 0;
                    fnAddStat(pSrvReward, 5, k, 0); /* STAT_NEWSKILLS */
                    Log("SERVER REWARD: +%d skill points\n", k);
                }
            } __except(1) { Log("SERVER REWARD: exception\n"); }
        }
    }

    /* XP Multiplier: detect XP gain from kills and add bonus.
     * Uses a cooldown flag to prevent compounding (our own bonus triggers another delta). */
    if (g_xpMultiplier > 1 && g_cachedPGame && fnAddStat && fnGetStat) {
        static int s_lastXP = 0;
        static BOOL s_justGaveBonus = FALSE;
        void* pXP = GetServerPlayer(g_cachedPGame);
        if (pXP) {
            int curXP = 0;
            __try { curXP = fnGetStat(pXP, 13, 0); } __except(1) {}
            if (s_lastXP > 0 && curXP > s_lastXP) {
                if (s_justGaveBonus) {
                    /* This delta is from our own bonus - skip it */
                    s_justGaveBonus = FALSE;
                } else {
                    int delta = curXP - s_lastXP;
                    /* 1.7.1 FIX: widen to int64 and clamp to INT32 so 10x on
                     * high-XP kills does not wrap to a negative bonus and
                     * subtract XP from the player. */
                    __int64 bonus64 = (__int64)delta * (__int64)(g_xpMultiplier - 1);
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

    /* TRAP SPAWN: spawn 8-12 area-matching monsters near player.
     * 1.8.5 FIX: defer-with-watchdog instead of consume-in-town.
     * File-static so the else-branch can clear them when player leaves
     * town. Pre-1.8.5 logic decremented `g_pendingTrapSpawn` while in
     * town which silently DROPPED every trap that arrived during
     * shopping / stash / identify. Now traps wait until the player
     * leaves town, with a 5-min watchdog so a queue can't accumulate
     * indefinitely if the player parks in town and quits. */
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
                /* Player left town — reset watchdog so next town visit gets
                 * a fresh 5-min grace window for any new pending traps. */
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
                                /* Pick common monsters for current act area */
                                int area = GetCurrentArea();
                                int monId = 0;
                                if (area < 40) monId = 3 + (area % 10);            /* Act 1 range */
                                else if (area < 75) monId = 100 + (area % 15);     /* Act 2 */
                                else if (area < 103) monId = 180 + (area % 12);    /* Act 3 */
                                else if (area < 109) monId = 270 + (area % 8);     /* Act 4 */
                                else monId = 360 + (area % 15);                    /* Act 5 */

                                int count = 8 + (GetTickCount() % 5);              /* 8-12 monsters */
                                int spawned = 0;
                                for (int ti = 0; ti < count; ti++) {
                                    int ox = nX + (ti % 4) * 4 - 6;
                                    int oy = nY + (ti / 4) * 4 - 4;
                                    __try {
                                        /* FIXED: nAnimMode=1, a7=-1 (proven working params) */
                                        void* unit = fnSpawnMonster((void*)g_cachedPGame, (void*)pRoom, ox, oy, monId, 1, -1, 0);
                                        if (unit) spawned++;
                                    } __except(1) {}
                                }
                                Log("TRAP: spawned %d/%d monsters (id=%d) at (%d,%d) area=%d\n", spawned, count, monId, nX, nY, area);
                                if (spawned > 0) ShowNotify("TRAP! Monsters incoming!");
                            }
                        }
                    } __except(1) { Log("TRAP: exception\n"); }
                    g_pendingTrapSpawn--;
                }
            }
        }
    }

    /* 1.9.0: object-spawn test removed 2026-04-27 ? findings in
     * Research/object_spawn_findings_2026-04-27.md. Runtime spawn
     * via OBJECTS_CreateObject works but doesn't bake objects into
     * the map. Next path: edit objects.txt + Levels.txt + objgroup.txt
     * to make D2's vanilla level-gen pipeline place objects naturally. */


    /* 1.9.0: Cheat physical portal spawn.
     * D2GAME_CreatePortalObject (D2Game.0x6FD13DF0 -> +0xE3DF0 offset, per
     * D2MOO 1.10 source comment) creates a physical portal object at
     * specified coords with a destination level ID + portal object class.
     *
     * Object IDs (from ObjectsIds.h):
     *   59 = OBJECT_TOWN_PORTAL (blue, like cast Town Portal scroll)
     *   60 = OBJECT_PERMANENT_TOWN_PORTAL (red, Cow Portal / Tristram quest)
     *
     * Function checks (from D2MOO Skills.cpp:3152): when pUnit is in town,
     * only LEVEL_MOOMOOFARM=39 with OBJECT_PERMANENT_TOWN_PORTAL is allowed.
     * Outside town, any combination works.
     *
     * Spawning is at player's current coordinates with bPerm=1. */
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
                        if (pRoom && destLevel == 39 && objId == 60) {
                            int spX = playerX + 3;
                            int spY = playerY;
                            void* pPortal = NULL;
                            int rc = fnCreatePortal((void*)g_cachedPGame, pSrvPlayer,
                                                    (void*)pRoom, spX, spY,
                                                    destLevel, &pPortal,
                                                    objId, 1);
                            Log("CHEAT PORTAL: src=%d dest=%d objId=%d at (%d,%d) "
                                "rc=%d pPortal=%p\n",
                                curLevelId, destLevel, objId, spX, spY, rc, pPortal);
                        } else {
                            Log("CHEAT PORTAL: rejected src=%d dest=%d objId=%d "
                                "(only Cow Portal supported)\n",
                                curLevelId, destLevel, objId);
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

    /* ZONE TELEPORT: warp player to town if in locked zone */
    if (g_pendingZoneTeleport > 0 && g_cachedPGame && hD2Game) {
        int townArea = g_pendingZoneTeleport;
        __try {
            FARPROC warpFn = (FARPROC)((DWORD)hD2Game + 0xC410);
            DWORD pClient = *(DWORD*)(g_cachedPGame + 0x88);
            DWORD pUnit = 0;
            if (pClient > 0x10000) pUnit = *(DWORD*)(pClient + 0x174);
            if (pUnit > 0x10000 && warpFn) {
                typedef void (__fastcall *WarpUnit_t)(void*, void*, int, int);
                ((WarpUnit_t)warpFn)((void*)g_cachedPGame, (void*)pUnit, townArea, 0);
                Log("ZONE WARP: teleported to area %d\n", townArea);
            }
        } __except(1) { Log("ZONE WARP: exception\n"); }
        g_pendingZoneTeleport = 0;
    }

    /* Process cheat menu commands (set from UI, consumed here in game tick) */
    if (g_cachedPGame && fnAddStat && (g_cheatGold || g_cheatStatPts || g_cheatSkillPts || g_cheatLevel)) {
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
                __try { fnAddStat(pSC, 13, g_cheatLevel * 50000, 0); }
                __except(1) { Log("CHEAT: AddStat XP(%d*50000) exception\n", g_cheatLevel); }
                g_cheatLevel = 0;
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
                        /* Test multiple monster IDs and param combos to find what works.
                         * D2MOO says: nAnimMode=1 (MONMODE_NEUTRAL), a7=-1 (wide search) */
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
    /* 1.9.0 DIAG — log entry to OUTER block so we can see if any of the
     * three gating conditions (g_cachedPGame, fnAddStat, GetServerPlayer
     * != NULL) is failing. Throttled to once per 2 sec. */
    {
        static DWORD s_lastOuter = 0;
        DWORD nd = GetTickCount();
        if (nd - s_lastOuter > 2000) {
            s_lastOuter = nd;
            void* probe = (g_cachedPGame && fnAddStat) ? GetServerPlayer(g_cachedPGame) : NULL;
            Log("OUTER DRAIN diag: pGame=%p fnAddStat=%p pCurseTarget=%p pendLoot=%d\n",
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
                    /* 1.7.1 FIX: stat 21 (mindamage) removal — toggle state alone provides
                     * the AMPLIFYDAMAGE effect (2x damage taken). Previous code corrupted
                     * the player's own min damage on curse restoration. */
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

            /* === Apply pending trap curses (from quest rewards) ===
             * 1.7.1: Curse stacking protection — if the same curse is already
             * active, EXTEND the existing expiration instead of re-applying the
             * stat changes. Stacking -50 velocity twice leaves +50 after the
             * first expiry, which corrupts the player's stat. Extending duration
             * only touches the expire timestamp. */
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
                    /* 1.7.1 FIX: stat 21 (mindamage) mutation removed — state 9
                     * (STATE_AMPLIFYDAMAGE) alone provides 2x damage taken.
                     * The previous code corrupted the player's own min damage. */
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

            /* === Drop gold (debug menu) === */
            if (g_cheatDropGold > 0) {
                __try {
                    fnAddStat(pCurseTarget, 14, 1000, 0);
                    Log("CHEAT DROP GOLD: +1000 gold\n");
                } __except(1) { Log("CHEAT DROP GOLD: exception\n"); }
                g_cheatDropGold = 0;
            }

            /* === REWARD_LOOT: Drop random boss loot table from quest reward ===
             * 1.7.1: cap the queue at 5 so accumulated rewards don't carpet the
             *        floor at once; 3-second cooldown between drops to avoid
             *        item-count overflow; IsTown guard so towns stay clean;
             *        ilvl floor of 30 keeps low-level drops useful;
             *        notify only on successful drop (nDropped > 0). */
            {
                static DWORD s_lastLootDropMs = 0;
                if (g_pendingLootDrop > 5) g_pendingLootDrop = 5;
                /* 1.9.0 DIAG — log entry to drain block so we can see
                 * which condition is blocking. Throttled so we don't
                 * flood the log on every tick. */
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
                    /* 1.9.0 — Town-skip removed. User explicitly wants
                     * loot delivered everywhere; only TRAPS (monster
                     * spawns) need a town guard. Boss-loot TC drops on
                     * the ground around the player; in town that's just
                     * cosmetic clutter the player accepted by enabling
                     * the bonus. */
                    if (nowMs - s_lastLootDropMs < 3000) {
                        /* Cooldown — try again next tick. Do NOT decrement. */
                    } else {
                        s_lastLootDropMs = nowMs;
                        typedef void (__fastcall *DropTCWrapper_t)(
                            void* pGame, void* pMonster, void* pPlayer,
                            int nTCId, int nQuality, int nItemLevel,
                            int a7, void** ppItems, int* pnDropped, int nMaxItems);
                        DropTCWrapper_t fnDropTC = (DropTCWrapper_t)((DWORD)hD2Game + 0x22110);

                        /* 1.9.0: pre-rolled boss takes priority over the
                         * legacy random pick. g_pendingLootBossId is set by
                         * REWARD_LOOT delivery and by AP receive of
                         * Drop: <Boss> Loot. Fall back to a tick-derived
                         * random if nothing pre-rolled the value (which
                         * keeps debug / cheat paths working unchanged). */
                        int bossIdx;
                        if (g_pendingLootBossId >= 0 && g_pendingLootBossId < BOSS_LOOT_COUNT) {
                            bossIdx = g_pendingLootBossId;
                            g_pendingLootBossId = -1;
                        } else {
                            DWORD t = GetTickCount();
                            bossIdx = (int)((t ^ (t >> 11)) % BOSS_LOOT_COUNT);
                        }
                        int tcId = g_bossLootTCs[bossIdx];
                        int playerLvl = 1;
                        __try { playerLvl = fnGetStat(pCurseTarget, 12, 0); } __except(1) {}
                        if (playerLvl < 1) playerLvl = 1;
                        int itemLevel = playerLvl + 5;
                        /* 1.9.0 — floor at 85 so boss TCs can roll their
                         * signature unique/set items (Andariel/Duriel/
                         * Mephisto's full TCs need ilvl 70+ for the high-
                         * end items, otherwise they fall back to low-tier
                         * potions/keys/gold). Boss-loot drops are the
                         * "reward" for completing a check; they need to
                         * feel meaningful regardless of player level. */
                        if (itemLevel < 85) itemLevel = 85;
                        if (itemLevel > 99) itemLevel = 99;

                        int nDropped = 0;
                        __try {
                            fnDropTC((void*)g_cachedPGame, pCurseTarget, pCurseTarget,
                                     tcId, 0, itemLevel, 0, NULL, &nDropped, 6);
                        } __except(1) {
                            Log("LOOT DROP: boss=%s tcId=%d CRASHED\n", g_bossLootNames[bossIdx], tcId);
                            nDropped = 0;
                        }
                        if (nDropped > 0) {
                            Log("LOOT DROP: boss=%s tcId=%d ilvl=%d -> %d items\n",
                                g_bossLootNames[bossIdx], tcId, itemLevel, nDropped);
                            ShowNotify("Awesome loot incoming!");
                        } else {
                            Log("LOOT DROP: boss=%s tcId=%d ilvl=%d -> 0 items (TC rolled nothing)\n",
                                g_bossLootNames[bossIdx], tcId, itemLevel);
                        }
                        g_pendingLootDrop--;
                    }
                }
            }

            /* === 1.9.0: Specific-item drop processor — pops one entry
             * from g_pendingDropQueue per tick (with cooldown) and
             * spawns the right charm/set/unique via QUESTS_CreateItem.
             * Items always drop with bDroppable=1 so they land on the
             * floor instead of going straight into inventory; the player
             * picks them up and identifies them like a real monster
             * drop. Skipped while in town to keep townscapes clean. */
            {
                static DWORD s_lastSpecDropMs = 0;
                int peekKind = 0, peekIdx = 0;
                /* 1.9.0 DIAG — log entry to drain block. Throttled. */
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
                    /* 1.9.0 — Town-skip removed. Items go directly to
                     * inventory via bDroppable=0, so they appear safely
                     * regardless of where the player is standing. Only
                     * trap-based monster spawns need a town guard. */
                    if (nowMs - s_lastSpecDropMs < 1500) {
                        /* Cooldown — try again next tick. Don't pop. */
                    } else {
                        s_lastSpecDropMs = nowMs;
                        DWORD code = Quests_GetDropCode(peekKind, peekIdx);
                        BYTE  qual = Quests_GetDropQuality(peekKind);
                        if (code == 0) {
                            Log("SPEC DROP: invalid code for kind=%d idx=%d — skipping\n",
                                peekKind, peekIdx);
                            Quests_ConsumePendingDrop();
                        } else {
                            int playerLvl = 1;
                            __try { playerLvl = fnGetStat(pCurseTarget, 12, 0); } __except(1) {}
                            if (playerLvl < 1) playerLvl = 1;
                            int itemLevel = playerLvl + 5;
                            /* 1.9.0 — floor at 85 so set/unique items
                             * always meet their req-level and spawn at
                             * proper quality. Pre-1.9.0 floor of 30
                             * caused items like Sazabi's Mental Sheath
                             * (req lvl 34) to fall back to magic basinet
                             * for low-level players. */
                            if (itemLevel < 85) itemLevel = 85;
                            if (itemLevel > 99) itemLevel = 99;

                            typedef void* (__fastcall *QUESTS_CreateItem_t)(
                                void* pGame, void* pPlayer,
                                DWORD dwCode, int nLevel, BYTE nQuality, int bDroppable);
                            QUESTS_CreateItem_t fnCI =
                                (QUESTS_CreateItem_t)((DWORD)hD2Game + 0x65DF0);

                            void* pSpawned = NULL;
                            __try {
                                /* 1.9.0 — bDroppable=0: item goes directly
                                 * into inventory if there's room (matches
                                 * how cheat-menu / runes / gems are
                                 * delivered). Pre-1.9.0 used bDroppable=1
                                 * which spawned on the ground; user reported
                                 * never seeing the items in dense areas like
                                 * Andariel's Lair. fnCI returns NULL when
                                 * inventory is full — caller retries next
                                 * tick (see else branch below). */
                                pSpawned = fnCI((void*)g_cachedPGame, pCurseTarget,
                                                code, itemLevel, qual, 0);
                            } __except(EXCEPTION_EXECUTE_HANDLER) { pSpawned = NULL; }

                            if (pSpawned) {
                                /* 1.9.0 — strip required level. AP-delivered
                                 * items must be usable at any character
                                 * level (the player earned them via a check,
                                 * not via combat scaling). Stat 92 =
                                 * item_levelreq. Setting to 1 makes any
                                 * character of level 1+ able to equip the
                                 * item. */
                                if (fnSetStat) {
                                    __try {
                                        fnSetStat(pSpawned, 92, 1, 0);
                                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                                }
                                Log("SPEC DROP: kind=%d idx=%d code=%08X qual=%d ilvl=%d -> OK (inventory, req=1)\n",
                                    peekKind, peekIdx, code, qual, itemLevel);
                                Quests_ConsumePendingDrop();
                            } else {
                                /* Inventory full + bDroppable=0 means
                                 * QUESTS_CreateItem couldn't place the item.
                                 * Retry next tick — gives the player time to
                                 * make room. Item is NOT dropped on the
                                 * floor (would lose track of it). */
                                Log("SPEC DROP: kind=%d idx=%d code=%08X qual=%d FAILED (inv full?) — retry next tick\n",
                                    peekKind, peekIdx, code, qual);
                            }
                        }
                    }
                }
            }

            /* === Drop specific boss loot table (debug menu) === */
            if (g_cheatDropLoot > 0 && g_cachedPGame && hD2Game) {
                typedef void (__fastcall *DropTCWrapper_t)(
                    void* pGame, void* pMonster, void* pPlayer,
                    int nTCId, int nQuality, int nItemLevel,
                    int a7, void** ppItems, int* pnDropped, int nMaxItems);
                DropTCWrapper_t fnDropTC = (DropTCWrapper_t)((DWORD)hD2Game + 0x22110);

                int bossIdx = g_cheatDropBossId;
                if (bossIdx < 0 || bossIdx >= BOSS_LOOT_COUNT) bossIdx = 2; /* default Mephisto */
                int tcId = g_bossLootTCs[bossIdx];
                int playerLvl = 1;
                __try { playerLvl = fnGetStat(pCurseTarget, 12, 0); } __except(1) {}
                if (playerLvl < 1) playerLvl = 1;
                int itemLevel = playerLvl + 5;
                if (itemLevel > 99) itemLevel = 99;

                int nDropped = 0;
                __try {
                    fnDropTC((void*)g_cachedPGame, pCurseTarget, pCurseTarget,
                             tcId, 0, itemLevel, 0, NULL, &nDropped, 6);
                    Log("CHEAT LOOT: %s tcId=%d ilvl=%d → %d items\n",
                        g_bossLootNames[bossIdx], tcId, itemLevel, nDropped);
                } __except(1) {
                    Log("CHEAT LOOT: %s tcId=%d CRASHED\n", g_bossLootNames[bossIdx], tcId);
                }
                g_cheatDropLoot = 0;
            }

            /* 1.9.0: Drop runeword test items via QUESTS_CreateItem. Three
             * separate cheat triggers so the user can pick up between
             * batches (runes + bases + cube pots = ~70 items total in
             * a single trigger overflows inventory + tile placement). */
            if ((g_cheatTestRunewords > 0 || g_cheatTestRunes > 0 || g_cheatTestBases > 0)
                && g_cachedPGame && hD2Game) {
                typedef void* (__fastcall *QUESTS_CreateItem_t)(
                    void* pGame, void* pPlayer,
                    DWORD dwCode, int nLevel, BYTE nQuality, int bDroppable);
                QUESTS_CreateItem_t fnQuestsCreateItem =
                    (QUESTS_CreateItem_t)((DWORD)hD2Game + 0x65DF0);

                /* Three item lists, picked based on which flag triggered.
                 * Splitting prevents overflow when 60+ items spawn in a
                 * single tick at the same coordinates. */
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

                /* D2's DATATBLS_GetItemRecordFromItemCode uses space-padded
                 * 4-char codes (e.g. 'box ' = 0x20786F62), not null-padded. */
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

            /* 1.9.0: Heal full — top up HP, MP, Stamina to max via fnAddStat. */
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

            /* 1.9.0: Unified item-drop dispatch via g_cheatItemCmd. */
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
                /* Hellfire Torch is a unique cm2 charm — base spawns
                 * normal, but with quality 7 (unique) D2 may pick a
                 * unique variant. Drop both base and rely on next pickup
                 * mechanic to roll the unique. */
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
                    /* 1.9.0: Pandemonium event items */
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
                    /* Hellfire Torch + Token of Absolution: spawn at item
                     * level 99 with quality 7 (unique) so the cm2 charm
                     * rolls as the unique Hellfire Torch instead of a
                     * normal Charm Large. Other Pandemonium items use
                     * normal quality 2 (their type=key in Misc.txt makes
                     * them a single-variant unique-look item). */
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

            /* 1.9.1 — Individual-item dispatch from the new Loot tab in
             * the Ctrl+V dev menu. Three independent slots:
             *   g_cheatSpecificSetIdx    -> Quests_QueueSpecificDrop SET
             *   g_cheatSpecificUniqueIdx -> Quests_QueueSpecificDrop UNIQUE
             *   g_cheatSingleItemCode    -> direct QUESTS_CreateItem
             *
             * Routing through Quests_QueueSpecificDrop for set/unique pieces
             * mirrors the AP delivery path exactly so the menu doubles as
             * an end-to-end test that AP rewards arrive correctly. */
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
                    /* Inventory delivery (bDroppable=0) so charm/set/
                     * unique items land in the backpack the same way
                     * AP rewards do. Falls back to ground-drop only if
                     * the inventory is full. */
                    pItem = fnCI((void*)g_cachedPGame, pCurseTarget,
                                 code_space, lvl, qual, 0);
                    if (!pItem) {
                        pItem = fnCI((void*)g_cachedPGame, pCurseTarget,
                                     code_null, lvl, qual, 0);
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Log("CHEAT LOOT: %c%c%c EXCEPTION\n", c0, c1, c2);
                }
                Log("CHEAT LOOT: spawned single %c%c%c lvl=%d qual=%d -> %s\n",
                    c0, c1, c2, lvl, qual, pItem ? "OK" : "NULL");
                g_cheatSingleItemCode[0] = 0;
            }

            /* 1.9.0: Pandemonium uber boss spawn dispatch.
             *
             * Strategy: spawn the VANILLA boss via fnSpawnMonster (works
             * because vanilla MonStats IDs have full data), then HACK
             * the spawned unit's pMonsterData fields:
             *   - nTypeFlag |= 0x02 (MONTYPEFLAG_SUPERUNIQUE) at +0x16
             *   - wBossHcIdx = SU index (67-72) at +0x26
             * This makes D2's render code look up the SU entry from
             * SuperUniques.txt for the display name.
             *
             * SuperUniques.txt rows 69-74 (indices 67-72) we appended:
             *   67=Uber Mephisto, 68=Uber Diablo, 69=Uber Baal,
             *   70=Lilith, 71=Uber Duriel, 72=Uber Izual.
             *
             * fnSpawnSuperUnique with index>66 crashes because the
             * function ALLOCATES new SU storage; but READING SU table
             * for display works for any valid row.
             *
             * Cheat cmd mapping: 1=Lilith, 2=Duriel, 3=Izual,
             * 4=Mephisto, 5=Diablo, 6=Baal. Vanilla monster IDs and
             * the corresponding SU index are paired below. */
            if (g_cheatSpawnUber > 0 && g_cachedPGame && fnSpawnMonster) {
                /* (vanilla monster ID, custom SU index) per cheat cmd */
                static const int UBER_VANILLA_IDS[6] = {156, 211, 256, 242, 243, 544};
                static const int UBER_SU_INDICES[6] = { 70,  71,  72,  67,  68,  69};
                static const char* UBER_NAMES[6] = {
                    "Lilith", "Uber Duriel", "Uber Izual",
                    "Uber Mephisto", "Uber Diablo", "Uber Baal"
                };
                /* Each entry: (cmd-1) idx → (vanilla monId, su index) */
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

                            /* NOTE: previous attempt to memory-hack the
                             * spawned monster's nTypeFlag (SUPERUNIQUE) +
                             * wBossHcIdx to point at custom SU rows
                             * (indices 67-72) caused ACCESS_VIOLATION
                             * because D2's SU table is fixed-size (66
                             * entries in 1.10f) and reading out-of-bounds
                             * crashes. Our research/CUSTOM_SUPERUNIQUE_RECIPE.md
                             * confirms: index 66+ crashes. Extending the
                             * table requires patching D2's allocator,
                             * which we have NOT done. So the spawned boss
                             * displays its vanilla name (Mephisto / Diablo
                             * etc.) — the cheat-menu notification text is
                             * the only place that says "Uber Mephisto". */
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

            /* 1.9.2 — Specific-monster spawn dispatch for the Mons tab in
             * the Ctrl+V menu. Two independent slots; each spawns one
             * unit at the player's position + a small offset.
             *
             *   g_cheatSpawnSuperUniqueIdx (0..65 valid)
             *     -> fnSpawnSuperUnique(pGame, pRoom, x, y, suIdx)
             *     SU rows 66+ are our custom Pandemonium ubers and crash
             *     the vanilla SU allocator (per d2arch_gameloop.c:1530
             *     comment). Use g_cheatSpawnUber for those instead.
             *
             *   g_cheatSpawnMonsterRowId (MonStats.txt row >= 0)
             *     -> fnSpawnMonster(pGame, pRoom, x, y, monId, 1, -1, 0)
             *     Same call shape as the random monster trap path. */
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

/* ---- Deferred quest completion queue ----
 * OnQuestComplete is called from the game tick (D2DebugGame hook).
 * Doing file I/O (SaveStateFile, WriteChecksFile) during the tick can crash
 * D2MOO's sUpdateClients. Instead, we queue completions and process them
 * from WndProc (UI thread) which is safe. */
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

    /* 1.8.0: quest-type toggle gate. If the user has disabled this quest's
     * type via the title screen (Hunting / KillZn / Explore / Waypnt /
     * Levels), skip the completion entirely — no reward, no AP check, no
     * completed-flag flip. This makes the toggle a REAL disable rather than
     * just hiding the entry from the UI. Story-type quests always pass
     * (g_apQuestStory is forced to TRUE elsewhere). */
    if (!IsQuestTypeActive((int)quest->type)) {
        Log("QUEST SKIPPED (type disabled): [%d] %s type=%d\n",
            qid, quest->name, (int)quest->type);
        return;
    }

    /* Mark as completed immediately (memory-only, no file I/O) */
    g_questCompleted[diff][qid] = TRUE;
    quest->completed = TRUE;
    Log("QUEST COMPLETE (deferred): [%d] %s AP=%d\n", qid, quest->name, g_apConnected);

    /* 1.9.0 — F1 Logbook: count this quest. Also stamp act-completion
     * time on the act-boss kill quest. The act-boss QTYPE_BOSS quests
     * have param == monster txtId (Andariel/Duriel/Mephisto/Diablo/Baal). */
    {
        extern void Stats_OnQuestComplete(void);
        extern void Stats_OnActComplete(int difficulty, int actIdx);
        Stats_OnQuestComplete();

        /* Detect act-boss completion → record clear-time per difficulty.
         * txtId mapping: Andariel=36, Duriel=66, Mephisto=242, Diablo=243, Baal=544. */
        if (quest->type == QTYPE_BOSS) {
            int actIdx = -1;
            switch (quest->param) {
                case 36:  actIdx = 0; break; /* Andariel  → Act 1 */
                case 66:  actIdx = 1; break; /* Duriel    → Act 2 */
                case 242: actIdx = 2; break; /* Mephisto  → Act 3 */
                case 243: actIdx = 3; break; /* Diablo    → Act 4 */
                case 544: actIdx = 4; break; /* Baal      → Act 5 */
                default:                break;
            }
            if (actIdx >= 0) {
                Stats_OnActComplete(diff, actIdx);
                Log("STATS: Act %d cleared on diff=%d\n", actIdx + 1, diff);
                /* 1.9.2 — Custom goal act-boss target. actIdx 0..4
                 * matches Andariel/Duriel/Mephisto/Diablo/Baal which
                 * is exactly what CustomGoal_OnActBossKilled expects. */
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
            /* 1.8.4 — show recipient slot if location is owned by another
             * player. Falls back to generic banner if owner unknown or self.
             * g_locationOwner[diff][qid] populated by LoadLocationOwners. */
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
            /* Solo mode: give rewards locally.
             * 1.8.0: Skill Hunting and Zone Locking are now independent. When
             * Zone Locking is ON, progression quests unlock zone keys first
             * (until all zones are unlocked) — and if Skill Hunting is ALSO
             * ON, each quest also awards a bonus skill unlock. When only
             * Skill Hunting is ON, quests unlock skills. If both are OFF,
             * the quest falls through to filler reward. */
            if (quest->reward == REWARD_SKILL) {
                if (g_zoneLockingOn) {
                    /* Unlock zone key first if any remain */
                    int nextKey = GetNextLockedZoneKey();
                    if (nextKey >= 0) {
                        UnlockZoneKey(nextKey);
                    } else if (g_skillHuntingOn) {
                        /* All zones unlocked — give a skill instead.
                         * 1.9.2 fix: pool-exhaustion fallback to skill point. */
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
                            g_serverPendingSkillPts += 1;
                            ShowNotify("Reward: 1 Skill Point! (pool exhausted)");
                        }
                    }
                    /* If BOTH modes are active, also grant a bonus skill.
                     * 1.9.2 fix: same pool-exhaustion fallback. */
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
                            g_serverPendingSkillPts += 1;
                            ShowNotify("Bonus: 1 Skill Point! (pool exhausted)");
                            Log("HYBRID bonus skill: pool exhausted -> +1 Skill Point fallback\n");
                        }
                    }
                } else if (g_skillHuntingOn) {
                    /* Skill Hunting only: progression quests unlock SKILLS.
                     * 1.9.2 fix: when the pool is exhausted (210 progression
                     * quests have already fired), fall back to a Skill Point
                     * grant so the remaining ~54 progression quests don't
                     * silently no-op. Previously: for-loop completed without
                     * matching any un-unlocked skill, the player got nothing.
                     * AP-mode players were fine because AP fill placed
                     * filler items at the overflow slots; standalone players
                     * were silently losing rewards. */
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
                        g_serverPendingSkillPts += 1;
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
                    /* Give 5 stat points directly to server player */
                    g_serverPendingStatPts += 5;
                    sprintf(msg, "Reward: 5 Stat Points!");
                    ShowNotify(msg);
                    Log("STAT REWARD: 5 stat points (server pending: %d)\n", g_serverPendingStatPts);
                    ItemLogAddA(2, 7, "+5 Stat Points", quest->name);
                } else if (fillerType == REWARD_SKILL) {
                    /* Give 1 skill point directly to server player */
                    g_serverPendingSkillPts += 1;
                    sprintf(msg, "Reward: 1 Skill Point!");
                    ShowNotify(msg);
                    Log("SKILL REWARD: 1 skill point (server pending: %d)\n", g_serverPendingSkillPts);
                    ItemLogAddA(2, 2, "+1 Skill Point", quest->name);
                } else if (fillerType == REWARD_TRAP) {
                    /* 1.9.0: trap type is pre-rolled at char creation
                     * (g_questExtra[diff][qid]) so the spoiler file's
                     * promise matches what actually fires. */
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
                    }
                    ShowNotify(msg);
                } else if (fillerType == REWARD_LOOT) {
                    /* 1.9.0: boss is pre-rolled. The drop site (REWARD_LOOT
                     * block above) reads g_pendingLootBossId to pick the
                     * specific TC instead of randomizing at drop time. */
                    int bossId = g_questExtra[diff][qid];
                    if (bossId < 0 || bossId >= BOSS_LOOT_COUNT) bossId = 2; /* default Mephisto */
                    g_pendingLootDrop++;
                    g_pendingLootBossId = bossId;  /* hint for the drop loop */
                    char lootMsg[64];
                    _snprintf(lootMsg, sizeof(lootMsg), "Drop: %s Loot", g_bossLootNames[bossId]);
                    Log("LOOT DROP queued: boss=%s pending=%d\n", g_bossLootNames[bossId], g_pendingLootDrop);
                    ItemLogAddA(2, 9, lootMsg, quest->name);
                } else if (fillerType == REWARD_RESETPT) {
                    g_resetPoints++;
                    ShowNotify("Reset Point earned!");
                    Log("RESET POINT earned! Total: %d\n", g_resetPoints);
                    ItemLogAddA(2, 2, "+1 Reset Point", quest->name);
                } else if (fillerType == REWARD_XP) {
                    /* 1.9.0: XP injection via fnAddStat (statId 13).
                     * Same mechanism the XP Multiplier feature uses. */
                    int xp = g_questXP[diff][qid];
                    if (xp <= 0) xp = 1000;
                    if (fnAddStat && g_cachedPGame) {
                        void* pXPp = GetServerPlayer(g_cachedPGame);
                        if (pXPp) {
                            __try { fnAddStat(pXPp, 13, xp, 0); } __except(1) {}
                        }
                    }
                    sprintf(msg, "Reward: %d XP!", xp);
                    ShowNotify(msg);
                    Log("XP REWARD: %d xp\n", xp);
                    char xpBuf[48];
                    sprintf(xpBuf, "+%d XP", xp);
                    ItemLogAddA(2, 7, xpBuf, quest->name);
                } else if (fillerType == REWARD_DROP_CHARM ||
                           fillerType == REWARD_DROP_SET ||
                           fillerType == REWARD_DROP_UNIQUE) {
                    /* 1.9.0: queue a specific item drop. Apply happens in
                     * the per-tick drop pipeline (Quests_TryProcessPendingDrops). */
                    Quests_QueueSpecificDrop(fillerType, g_questExtra[diff][qid], quest->name);
                } else {
                    /* REWARD_GOLD (or anything unrecognized — falls through
                     * to gold for safety so a bad enum value doesn't drop
                     * a quest reward silently). */
                    int gold = g_questGold[diff][qid];
                    if (gold <= 0) gold = 1 + (rand() % 10000);
                    g_serverPendingGold += gold;
                    sprintf(msg, "Reward: %d gold!", gold);
                    ShowNotify(msg);
                    Log("GOLD REWARD: %d gold (server pending: %d)\n", gold, g_serverPendingGold);
                    char goldBuf[48];
                    sprintf(goldBuf, "+%d gold", gold);
                    ItemLogAddA(2, 4, goldBuf, quest->name);
                }
            }
        }
    }

    /* File I/O only ONCE after all deferred quests processed — safe in WndProc context */
    if (anyNew) {
        SaveStateFile();
        WriteChecksFile();

        /* Check if any completed quest matches the AP goal */
        if (g_apConnected && !g_apGoalComplete) {
            int goalQid = (g_apGoalScope >= 0 && g_apGoalScope <= 4) ?
                          g_goalQuestIds[g_apGoalScope] : g_goalQuestIds[4];
            for (int gi = 0; gi < count; gi++) {
                if (g_deferredQueue[gi] == goalQid) {
                    g_apGoalComplete = TRUE;
                    char dir2[MAX_PATH], gpath[MAX_PATH];
                    GetArchDir(dir2, MAX_PATH);
                    sprintf(gpath, "%sap_goal.dat", dir2);
                    FILE* gf = fopen(gpath, "w");
                    if (gf) { fprintf(gf, "goal=complete\n"); fclose(gf); }
                    ShowNotify("GOAL COMPLETE!");
                    Log("AP GOAL COMPLETE: quest %d matches goal_scope %d\n", goalQid, g_apGoalScope);
                    break;
                }
            }
        }

        /* 1.9.0: Goal=3 (Collection) win-condition check — fires when
         * Coll_IsGoalComplete returns TRUE. Uses the same g_apGoalComplete
         * flag + ap_goal.dat sidecar as the difficulty-based goal so the
         * downstream AP "you won" pipeline doesn't need to know the goal
         * source. */
        if (g_apConnected && !g_apGoalComplete && g_apGoal == 3) {
            extern BOOL Coll_IsGoalComplete(void);
            if (Coll_IsGoalComplete()) {
                g_apGoalComplete = TRUE;
                char dir3[MAX_PATH], gpath[MAX_PATH];
                GetArchDir(dir3, MAX_PATH);
                sprintf(gpath, "%sap_goal.dat", dir3);
                FILE* gf = fopen(gpath, "w");
                if (gf) { fprintf(gf, "goal=complete\nsource=collection\n"); fclose(gf); }
                ShowNotify("COLLECTION GOAL COMPLETE!");
                Log("AP GOAL COMPLETE: collection targets all met\n");
            }
        }

        /* 1.9.2 — Goal=4 (Custom) win-condition check. Mirrors the
         * Collection branch above. CustomGoal_IsComplete walks the
         * required-vs-fired bitmap + checks lifetime gold against
         * the user-set target. */
        if (g_apConnected && !g_apGoalComplete && g_apGoal == 4) {
            extern BOOL CustomGoal_IsComplete(void);
            if (CustomGoal_IsComplete()) {
                g_apGoalComplete = TRUE;
                char dir4[MAX_PATH], gpath[MAX_PATH];
                GetArchDir(dir4, MAX_PATH);
                sprintf(gpath, "%sap_goal.dat", dir4);
                FILE* gf = fopen(gpath, "w");
                if (gf) { fprintf(gf, "goal=complete\nsource=custom\n"); fclose(gf); }
                ShowNotify("CUSTOM GOAL COMPLETE!");
                Log("AP GOAL COMPLETE: all custom-goal targets met + gold target reached\n");
            }
        }
    }
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

                /* 1.9.0 Phase 2 — uber kill scan. Runs BEFORE the
                 * txtId<700 filter below because uber MonStats rows are
                 * 704-712 (Mephisto/Diablo/Izual/Lilith/Duriel/Baal) so
                 * they'd otherwise be skipped entirely. The dedup uses
                 * the existing g_deadIds[] array; if we end up adding
                 * the uber kill here, the txtId<700 branch below will
                 * see alreadyCounted=TRUE and skip its own bookkeeping
                 * (which is fine — ubers don't need quest/SU/gate-boss
                 * processing). */
                if (type == 1 && unitId != 0 &&
                    txtId >= UBER_MONID_MEPHISTO && txtId <= UBER_MONID_BAAL) {
                    DWORD ubFlags = SafeRead(unit + 0xC4);
                    BOOL  ubDead  = (mode == MODE_DEAD || (ubFlags & 0x10000));
                    if (ubDead) {
                        BOOL ubAlready = FALSE;
                        for (int d = 0; d < g_deadCount; d++) {
                            if (g_deadIds[d] == unitId) { ubAlready = TRUE; break; }
                        }
                        if (!ubAlready) {
                            if (g_deadCount < MAX_DEAD_TRACKED)
                                g_deadIds[g_deadCount++] = unitId;
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
                    /* Check death via mode AND unit flags for robustness.
                     * UNITFLAG_ISDEAD (0x10000) in dwFlags at offset 0xC4 is the most
                     * reliable indicator, as mode can transition quickly.
                     * NOTE: MODE_DEATH (0) is skipped because SafeRead returns 0 on
                     * exception, which would cause false positives. MODE_DEAD (12) and
                     * UNITFLAG_ISDEAD are sufficient. */
                    DWORD unitFlags = SafeRead(unit + 0xC4);
                    BOOL isDead = (mode == MODE_DEAD || (unitFlags & 0x10000));
                    if (isDead) {
                        BOOL alreadyCounted = FALSE;
                        for (int d = 0; d < g_deadCount; d++) {
                            if (g_deadIds[d] == unitId) { alreadyCounted = TRUE; break; }
                        }
                        if (!alreadyCounted) {
                            if (g_deadCount < MAX_DEAD_TRACKED)
                                g_deadIds[g_deadCount++] = unitId;
                            if (currentArea < MAX_AREA_ID)
                                g_areaKills[currentArea]++;

                            /* 1.9.0 — F1 Logbook stat. Read pMonsterData
                             * typeFlag (bit0=unique, bit1=superunique,
                             * bit2=champion) and OR in bit 0x80 for our
                             * "tracked boss" classification. */
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
                            }

                            /* 1.9.2 Extra check Cat 1 — Hell Bovine lifetime counter.
                                * Every cow kill (regular OR Cow King) bumps the counter
                                * which fires the 100/500/1000 milestones. Idempotent
                                * dedup via the bitmap inside Extra_OnCowKilled. */
                            if (txtId == 391) {
                                extern void Extra_OnCowKilled(void);
                                Extra_OnCowKilled();
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

                            /* Check for SuperUnique kill using pMonsterData typeFlag + hcIdx */
                            __try {
                                DWORD pMonData = *(DWORD*)(unit + 0x14); /* pMonsterData for monsters */
                                if (pMonData) {
                                    BYTE typeFlag = *(BYTE*)(pMonData + 0x16);
                                    static int killLogCount = 0;
                                    if (killLogCount++ < 50)
                                        Log("KILL: txtId=%d typeFlag=0x%02X area=%d unitId=%d\n", txtId, typeFlag, currentArea, unitId);
                                    if (typeFlag & 0x02) { /* MONTYPEFLAG_SUPERUNIQUE */
                                        WORD hcIdx = *(WORD*)(pMonData + 0x26);
                                        Log("SUPERUNIQUE KILLED: hcIdx=%d txtId=%d typeFlag=0x%02X area=%d\n", hcIdx, txtId, typeFlag, currentArea);
                                        /* 1.9.2 Extra check Cat 1 — Cow King kill per difficulty.
                                         * SU row 39 = "The Cow King". Forward-decl because
                                         * d2arch_extrachecks.c is included after gameloop.c. */
                                        if (hcIdx == 39) {
                                            extern void Extra_OnCowKingKilled(int diff);
                                            Extra_OnCowKingKilled(g_currentDifficulty);
                                            extern void CustomGoal_OnCowKingKilled(int diff);
                                            CustomGoal_OnCowKingKilled(g_currentDifficulty);
                                        }
                                        /* 1.9.2 — Custom goal super-unique targets.
                                         * Bishibosh / Corpsefire / Rakanishu / etc. */
                                        {
                                            extern void CustomGoal_OnSuperUniqueKilled(int hcIdx);
                                            CustomGoal_OnSuperUniqueKilled((int)hcIdx);
                                        }
                                        BOOL matched = FALSE;
                                        for (int a2 = 0; a2 < 5; a2++)
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
                                        if (!matched)
                                            Log("SUPERUNIQUE: hcIdx=%d has NO matching quest!\n", hcIdx);

                                        /* 1.8.2 — Gate-boss kill detection.
                                         * Works in BOTH AP and standalone modes.
                                         * - Marks g_gateBossKilled[diff][slot] = TRUE (persistent)
                                         * - AP mode: sends location check, AP echoes back the gate key
                                         * - Standalone: grants the key locally via UnlockGateKey()
                                         * - Saves state immediately so kill survives a crash
                                         */
                                        if (g_zoneLockingOn) {
                                            int diff = g_currentDifficulty;
                                            if (diff >= 0 && diff <= 2) {
                                                for (int actIdx = 1; actIdx <= 5; actIdx++) {
                                                    int preload_id = g_actPreload[actIdx - 1][diff];
                                                    int num_gates = g_actRegions[actIdx - 1].num_gates;
                                                    for (int gi = 0; gi < num_gates; gi++) {
                                                        const GateSpawnDef* gd = Preload_GetGate(actIdx, preload_id, gi);
                                                        if (!gd) continue;
                                                        /* 1.8.5 FIX: relaxed area constraint. D2's 1-per-game SuperUnique
                                                         * guard prevents our gate-boss spawn from succeeding when vanilla
                                                         * map placement has already instantiated the same hcIdx elsewhere
                                                         * (e.g. Bishibosh in canonical Cave L2 vs gate-boss in Mausoleum).
                                                         * By accepting the kill regardless of area, the player can unlock
                                                         * the gate by killing the SU wherever it actually spawned —
                                                         * canonical vanilla zone OR our gate zone. hcIdx is unique within
                                                         * each preload's 4 gates, so this can't false-trigger a sibling
                                                         * gate. The original spawn_zone match is preserved as a soft hint
                                                         * via logging only. */
                                                        if (gd->base_su == (int)hcIdx) {
                                                            if (gd->spawn_zone != currentArea) {
                                                                Log("GATE BOSS KILL (1.8.5 relaxed): hcIdx=%d killed in area=%d "
                                                                    "instead of gate zone=%d — accepting (1-per-game guard "
                                                                    "likely blocked our spawn at gate zone)\n",
                                                                    hcIdx, currentArea, gd->spawn_zone);
                                                            }
                                                            int slot = GateKey_SlotFromActGate(actIdx, gi);
                                                            BOOL alreadyHandled = (slot >= 0 && g_gateBossKilled[diff][slot]);
                                                            if (!alreadyHandled && slot >= 0) {
                                                                g_gateBossKilled[diff][slot] = TRUE;
                                                                Log("GATE BOSS KILLED: diff=%d act=%d gate=%d slot=%d (hcIdx=%d area=%d)\n",
                                                                    diff, actIdx, gi + 1, slot, hcIdx, currentArea);

                                                                if (g_apConnected) {
                                                                    /* AP mode — gate-boss kills are persisted to the
                                                                     * checks file (same path quest completions use).
                                                                     * The bridge's read_pending_checks polls that file
                                                                     * and uploads new entries to the AP server, which
                                                                     * echoes back the gate-key item.
                                                                     *
                                                                     * 1.8.5 FIX: pre-1.8.5 we wrote
                                                                     *   WriteAPCommand("location N")
                                                                     * but the bridge's command dispatcher only handles
                                                                     * connect/disconnect — every gate kill was silently
                                                                     * dropped. WriteChecksFile now also emits gate-kill
                                                                     * lines so this works through the existing pipeline. */
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

                                                                /* Persist immediately — kill survives a crash.
                                                                 * 1.8.5 — also write checks file so the bridge sees the new
                                                                 * gate-boss check and uploads it on next poll. Without this
                                                                 * the AP server never learns about gate kills. */
                                                                SaveStateFile();
                                                                WriteChecksFile();
                                                            }
                                                            break;
                                                        }
                                                    }
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
                    quest->killCount = g_areaKills[areaId];
                    if (quest->killCount >= quest->killReq)
                        OnQuestComplete(quest);
                }
            }
        }
    }

    /* When dead list is full, keep the most recent half to avoid losing
     * recent entries while still allowing new kills to be tracked.
     * Move the second half to the beginning and continue from there. */
    if (g_deadCount >= MAX_DEAD_TRACKED) {
        int keep = MAX_DEAD_TRACKED / 2;
        memmove(g_deadIds, g_deadIds + keep, keep * sizeof(DWORD));
        g_deadCount = keep;
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
/* Waypoint check — scan for waypoint OBJECTS in nearby rooms.
 * Waypoint objects are unit type 2 (UNIT_OBJECT).
 * When a waypoint is activated by the player, its mode changes to 2 (OBJMODE_OPENED).
 * We scan rooms like ScanMonsters but look for objects with waypoint txtIds. */

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

/* Check D2's native quest completion flags for QTYPE_QUESTFLAG quests.
 * Reads from pGame->pQuestControl->pQuestFlags (the SERVER-SIDE global quest state).
 * Offsets from D2MOO source: pQuestControl at pGame+0x10F4, pQuestFlags at pQuestControl+0x0C.
 * The player-side pQuestData is a CLIENT copy that stays empty in D2MOO single-player. */
static void CheckQuestFlags(void) {
    if (!g_cachedPGame || !fnGetQuestState) return;
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
                completed = fnGetQuestState(pQuestFlags, d2QuestId, 0) ||  /* REWARDGRANTED */
                            fnGetQuestState(pQuestFlags, d2QuestId, 13) || /* PRIMARYGOALDONE */
                            fnGetQuestState(pQuestFlags, d2QuestId, 14) || /* COMPLETEDNOW */
                            fnGetQuestState(pQuestFlags, d2QuestId, 15);   /* COMPLETEDBEFORE */
            } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }

            if (completed) {
                Log("QUEST FLAG DETECTED: '%s' d2Id=%d diff=%d\n", quest->name, d2QuestId, diff);
                OnQuestComplete(quest);
            }
        }
    }
}

/* Check player level for QTYPE_LEVEL quests.
 * Level milestones auto-complete on ALL difficulties if already done on a lower one.
 * E.g. if "Reach Level 5" is done on Normal, it auto-completes on Nightmare+Hell too. */
/* 1.9.3 fix: backfill flag for CheckLevelMilestones, file-scope so
 * OnCharacterLoad can reset it via Milestone_ResetBackfill(). */
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

    /* The bug we fix here: a character that loads at e.g. level 30 with
     * NO existing level-milestone quest completions (first-time mod
     * load, or per-character state reset) used to trigger
     * OnQuestComplete for EVERY milestone where level >= quest->param
     * on the first tick — handing out rewards for level 5, 10, 15, 20,
     * 30 all at once "without doing anything" (per Maegis bug report
     * 2026-05-03).
     *
     * Fix: on the FIRST tick after a character load (signalled by
     * Milestone_ResetBackfill clearing g_milestoneBackfillDone), we
     * silently mark milestones <= current level as completed without
     * firing OnQuestComplete. Subsequent ticks run the normal
     * "trigger when reaching a new milestone" logic. */
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

/* 1.8.2 — Tick-time reconciliation between D2's actual quest state and our
 * Zone Locking gate-key store. Runs every tick (idempotent — UnlockGateKey
 * early-returns if already received), but in practice does work only on the
 * first tick after a character load whose state file is missing entries
 * the .d2s file would imply.
 *
 * Use case: a player upgrading from a pre-1.8.2 build mid-game whose per-
 * character state file never recorded gate-key receipts (or recorded them
 * for a different setting) but whose vanilla D2 progress shows they killed
 * Andariel/Duriel/Mephisto/Diablo. CheckQuestFlags() populates
 * g_questCompleted[diff][qid] from D2's authoritative flags within a few
 * ticks; this function then back-fills the corresponding gate keys.
 *
 * Quest IDs (from d2arch_quests.c):
 *   6   = Sisters to the Slaughter (Andariel — Act 1)
 *   106 = Seven Tombs              (Duriel    — Act 2)
 *   206 = The Guardian             (Mephisto  — Act 3)
 *   303 = Terror's End             (Diablo    — Act 4)
 *   406 = Eve of Destruction       (Baal      — Act 5) */
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

/* Per-tick scan: kill detection, area-reach, waypoints, quest flags, and the
 * 1.8.2 zone-locking reconciliation that back-fills gate keys from D2's
 * actual quest progress. Forward-declared in d2arch_skilltree.c. */
static void RunCheckDetection(void) {
    if (!Player()) return;
    ScanMonsters();
    CheckAreaReach();
    CheckWaypoints();
    CheckQuestFlags();
    CheckLevelMilestones();
    ReconcileGatesFromQuests();
}

