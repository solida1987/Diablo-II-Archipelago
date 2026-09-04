
/* SAFE DRAW WRAPPER */
static void SafeDraw(void) {
    /* After reinvest completes, reload skill levels from files immediately. */
    if (g_reinvestDone && g_charName[0]) {
        g_reinvestDone = FALSE;
        /* Read all 30 level files and store in g_reinvestLevels for the panel to pick up */
        for (int _rn = 0; _rn < 30; _rn++) {
            char _rsp[MAX_PATH], _rsfx[32];
            GetCharFileDir(_rsp, MAX_PATH);
            if (_rn == 0) strcat(_rsp, "d2arch_fireball_");
            else { sprintf(_rsfx, "d2arch_skill%d_", _rn + 1); strcat(_rsp, _rsfx); }
            strcat(_rsp, g_charName); strcat(_rsp, ".dat");
            FILE* _rf = fopen(_rsp, "r");
            if (_rf) { fscanf(_rf, "%d", &g_reinvestLevels[_rn]); fclose(_rf); }
            else g_reinvestLevels[_rn] = 0;
        }
        g_reinvestLevelsReady = TRUE;
        Log("SafeDraw: reinvest done, levels loaded from files\n");
    }
    __try {
        DrawAll();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("Exception in DrawAll\n");
    }
}

/* WNDPROC HOOK - Handle keyboard + mouse */
static LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    /* Process deferred quest completions (file I/O safe here, outside game tick) */
    ProcessDeferredQuests();

    /* also fire custom/collection goal wins that were completed by a NON-quest event (boss/uber/cow/torch kill or bulk poll); the quest-gated check inside ProcessDeferredQuests would otherwise never register them. */
    {
        extern void CheckNonQuestGoalCompletion(void);
        CheckNonQuestGoalCompletion();
    }

    /* poll AP status here. WndProc fires on every Windows message (paint, mouse-move, etc.) at high frequency on BOTH the title screen and in-game, so this is the most reliable place to keep `g_apConnected` and the Connect-button cellfile in sync. Why not the title-screen render hook? `RenderAPPanelD2` is wired into D2GFX_EndScene but the install code for that hook is missing (see d2arch_input.c:415-419 for the trampoline state — only the uninstall code at d2arch_main.c:791-796 references it). Until that hook is properly installed, RenderAPPanelD2 never runs, so polling from there is a no-op. PollAPStatus internally throttles to 2-second granularity, so the file read still happens at most a few times per second even when Windows messages are flooding in. */
    PollAPStatus();

    /* Block WM_ACTIVATEAPP losing focus — prevents D2 from minimizing in windowed mode. */
    if (msg == WM_ACTIVATEAPP && wp == 0) {
        return 0;
    }

    /* Detect player gone while in menu/char select. */
    if (g_lastPlayerPtr != NULL) {
        void* p = NULL;
        __try { p = fnGetPlayer ? fnGetPlayer() : NULL; } __except(EXCEPTION_EXECUTE_HANDLER) {
            /* Match the codebase convention — existing handlers here swallow silently. */
        }
        if (!p) {
            /* fine-grained logging so we can pinpoint which step a crash-on-exit happens in (the previous build crashed mid-shutdown with no exit-handler trace in the log). */
            Log("WndProc EXIT[1]: player gone, beginning cleanup for '%s'\n", g_charName);
            Log("WndProc EXIT[2]: UndoMonsterShuffle...\n");
            UndoMonsterShuffle();
            Log("WndProc EXIT[3]: UndoBossShuffle...\n");
            UndoBossShuffle();
            Log("WndProc EXIT[4]: UndoEntranceShuffle...\n");
            UndoEntranceShuffle();
            Log("WndProc EXIT[5]: SaveSlots...\n");
            SaveSlots();
            Log("WndProc EXIT[6]: SaveStateFile...\n");
            SaveStateFile();
            Log("WndProc EXIT[7]: WriteChecksFile...\n");
            WriteChecksFile();
            if (g_charName[0]) {
                Log("WndProc EXIT[8]: StashSaveAP...\n");
                StashSaveAP(g_charName);
                Log("WndProc EXIT[9]: StashSerSaveAll...\n");
                StashSerSaveAll(g_charName);
                Log("WndProc EXIT[9b]: StkSaveAP...\n");
                StkSaveAP(g_charName);                /* STK_AP */
                {
                    extern void Coll_SaveForCharacter(const char* charName);
                    Log("WndProc EXIT[9c]: Coll_SaveForCharacter...\n");
                    Coll_SaveForCharacter(g_charName); /* Collection flags */
                }
                {
                    extern void Stats_AggregateLifetime(const char* charName);
                    extern void Stats_SaveForCharacter(const char* charName);
                    Log("WndProc EXIT[9d]: Stats save + lifetime aggregate...\n");
                    Stats_AggregateLifetime(g_charName); /* fold deltas into lifetime */
                    Stats_SaveForCharacter(g_charName);  /* per-char sidecar */
                }
            }
            Log("WndProc EXIT[10]: StashSaveShared...\n");
            StashSaveShared();
            Log("WndProc EXIT[10b]: StkSaveShared...\n");
            StkSaveShared();                          /* STK_SH */
            {
                extern void Stats_SaveLifetime(void);
                Log("WndProc EXIT[10c]: Stats_SaveLifetime...\n");
                Stats_SaveLifetime();                 /* account-wide stats */
            }
            Log("WndProc EXIT[11]: StashSwapReset...\n");
            StashSwapReset();
            Log("WndProc EXIT[12]: StashSerResetMemory...\n");
            StashSerResetMemory();
            Log("WndProc EXIT[12b]: StkResetOnPlayerGone...\n");
            StkResetOnPlayerGone();   /* clear STK_AP + cel cache */
            {
                extern void Coll_ResetOnPlayerGone(void);
                extern void Coll_ResetTickState(void);
                Log("WndProc EXIT[12c]: Coll_ResetOnPlayerGone...\n");
                Coll_ResetOnPlayerGone();   /* clear collection flags in mem */
                Coll_ResetTickState();      /* reset gold delta + scan throttle */
            }
            /* restore sgptDT skill-table to vanilla state so the next character's load isn't reading the previous char's polluted charclass/reqLevel/class-skill-list. */
            {
                extern void Skilltree_OnCharacterUnloadHook(void);
                Log("WndProc EXIT[12d]: Skilltree_OnCharacterUnloadHook...\n");
                Skilltree_OnCharacterUnloadHook();
            }
            /* clear uber tracking so a mid-Pandemonium-run exit doesn't leak finale-spawn counts into the next character. */
            {
                extern void Ubers_ResetTrackingOnPlayerGone(void);
                Log("WndProc EXIT[12e]: Ubers_ResetTrackingOnPlayerGone...\n");
                Ubers_ResetTrackingOnPlayerGone();
            }
            Log("WndProc EXIT[13]: ResetD2SFile...\n");
            ResetD2SFile(g_charName);
            Log("WndProc EXIT[14]: SaveStateFile (post-reset)...\n");
            SaveStateFile();
            Log("WndProc EXIT[15]: cleanup complete\n");

            g_lastPlayerPtr = NULL;

            /* Full per-char global reset so a character switch can't leak the previous character's state into the next load. */
            g_charName[0] = 0;
            g_poolInitialized = FALSE;
            g_poolCount = 0;
            /* clear the frozen-settings flag so the NEXT character load can re-initialise settings from title screen / AP fresh. */
            g_settingsFrozen = FALSE;
            g_reinvestLevelsReady = FALSE;
            memset(g_reinvestLevels, 0, sizeof(g_reinvestLevels));
            g_reinvestDone = FALSE;
            g_reinvestPending = FALSE;
            g_reinvestCount = 0;
            g_shuffleApplied = FALSE;
            g_bossShuffleApplied = FALSE;
            /* g_cachedPGame intentionally NOT cleared — it's per-process, not per-character, and the gameloop re-resolves it when the next player appears. */

            /* AP-mode flag reset. Pre-1.9.10 g_apMode was sticky-TRUE for the whole DLL lifetime once any character authed against AP. That meant a NEW character created after loading an AP char would bake AP-mode settings into its state file even if the user wanted standalone. Per feedback_settings_isolation.md the contract is "new char captures from AP slot_data OR d2arch.ini once and freezes" — leaving g_apMode=TRUE between chars violates that. The bridge stays connected; only DLL flags reset so the next char's LoadAPSettings starts from a known state. (g_apMode is non-static — declared in d2arch_input.c at line 481 — accessible directly via unity-build inclusion.) */
            g_apMode = FALSE;
            /* g_apConnected stays as-is — the bridge process is still running and connected to the AP server; if the next char uses the same slot it can resume immediately. */

            /* per-char AP caches that previously leaked across char switches. */
            g_appliedApLoaded = FALSE;
            g_appliedApCount = 0;
            /* the per-location filler set was NOT reset here, so the next character started with the previous one's locations already marked and its stat points / skill points / gold were silently swallowed. */
            g_appliedFillerLocsLoaded = FALSE;
            g_appliedFillerLocCount = 0;

            g_locationOwnersLoaded = FALSE;
            g_apItemLocationsLoaded = FALSE;

            /* pending zone teleport could fire on next char's first tick if a teleport was queued the moment of exit. */
            g_pendingZoneTeleport = 0;

            /* the server-pending counters are no longer zeroed here. */
            g_pendingGold = 0;
            /* clear the skill/stat overflow ledgers so char A's parked points can't leak onto char B. */
            g_skillPtsLedger = 0;
            g_statPtsLedger  = 0;

            Log("WndProc EXIT[16]: per-char globals + AP caches + pending counters reset\n");
        }
    }

    /* Keyboard shortcuts */
    if (msg == WM_KEYDOWN) {
        /* Stash overlay previously toggled via F8 (user feedback: "nothing should be on F8"). */
        /* Ctrl+V = toggle cheat menu — ONLY in-game. */
        if (wp == 'V' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            if (Player()) {
                ShowItems_ForceOff();
                g_cheatMenuOpen = !g_cheatMenuOpen;
            }
            return 0;
        }
        /* Shift+P = toggle packet logging for 30 seconds */
        if (wp == 'P' && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
            g_packetLogEnabled = !g_packetLogEnabled;
            g_packetLogStart = GetTickCount();
            ShowNotify(g_packetLogEnabled ? "Packet logging ON (30s)" : "Packet logging OFF");
            Log("PACKET LOG: %s\n", g_packetLogEnabled ? "ENABLED" : "DISABLED");
            return 0;
        }
        /* (Shift+L diagnostic dump and Shift+R rift dev-warp removed with the abandoned runtime-extension rift attempt. */
        /* Shift+0 = toggle D2MOO Debug window (hidden by default) */
        if (wp == '0' && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
            HWND hDbg = FindWindowA(NULL, "D2Debugger");
            if (!hDbg) hDbg = FindWindowA("D2Debugger", NULL); /* try class name too */
            if (hDbg) {
                BOOL vis = IsWindowVisible(hDbg);
                ShowWindow(hDbg, vis ? SW_HIDE : SW_SHOW);
                Log("Debug window toggled: %s\n", vis ? "HIDDEN" : "VISIBLE");
            } else {
                Log("Debug window not found (D2Debugger not loaded?)\n");
            }
            return 0;
        }
        /* a latched Alt would keep item labels over the panel. */
        if ((BYTE)wp == g_keySkillEditor) { ShowItems_ForceOff(); g_editorOpen = !g_editorOpen; return 0; }
        if ((BYTE)wp == g_keyQuestLog) { g_questLogOpen = !g_questLogOpen; g_menuOpen = FALSE; return 0; }
        if ((BYTE)wp == g_keyTracker) { g_trackerOn = !g_trackerOn; return 0; }
        /* Zone Tracker — F4 now opens the F1 book at the Zones page (page 10). */
        if ((BYTE)wp == g_keyZoneMap && g_zoneLockingOn) {
            if (g_editorOpen && g_editorPage == 10) {
                g_editorOpen = FALSE;
            } else {
                g_editorOpen = TRUE;
                g_editorPage = 10;   /* PAGE_ZONES */
            }
            return 0;
        }
        /* Skill tree is now handled by vanilla's panel system via JMP hook. */
        if (wp == VK_ESCAPE) {
            BOOL closedAny = FALSE;
            if (g_zoneTrackerOpen) { g_zoneTrackerOpen = FALSE; closedAny = TRUE; }
            if (g_questLogOpen)    { g_questLogOpen    = FALSE; closedAny = TRUE; }
            if (g_editorOpen)      { g_editorOpen      = FALSE; g_apPageFocus = -1; closedAny = TRUE; }
            if (g_menuOpen)        { g_menuOpen        = FALSE; closedAny = TRUE; }
            /* Intentionally do NOT return 0 even when closedAny — let D2 see the ESC too so any vanilla panel currently open (stash, char sheet, vanilla skill tree, inventory) also closes. */
            (void)closedAny;
        }

    }

    /* Mouse wheel for scrolling */
    if (msg == WM_MOUSEWHEEL) {
        short delta = (short)HIWORD(wp);
        static int wheelLog = 0;
        if (wheelLog < 5) { Log("WM_MOUSEWHEEL: delta=%d editorOpen=%d\n", delta, g_editorOpen); wheelLog++; }
        if (g_editorOpen) {
            if (g_editorPage == 1) {
                /* Scroll quest page list */
                g_questPageScroll -= (delta / 120) * 40;
                if (g_questPageScroll < 0) g_questPageScroll = 0;
            } else if (g_editorPage == 8) {
                /* Stats / Logbook page. */
                extern int  MouseX(void);
                extern void Stats_HandleScroll(int side, int delta);
                int side = (MouseX() < 350) ? 0 : 1;
                int rows = (delta / 120) * 3;
                if (rows == 0) rows = (delta > 0 ? 3 : -3);
                Stats_HandleScroll(side, rows);
            } else if (g_editorPage == 10) {
                /* Zones page (folded F4). */
                g_zoneTrackerScroll -= (delta / 120) * 20;
                if (g_zoneTrackerScroll < 0) g_zoneTrackerScroll = 0;
            } else {
                /* Scroll red list on left page */
                g_editorScroll -= (delta / 120) * 30;
                if (g_editorScroll < 0) g_editorScroll = 0;
            }
            return 0;
        }
        if (g_zoneTrackerOpen) {
            g_zoneTrackerScroll -= (delta / 120) * 20;
            if (g_zoneTrackerScroll < 0) g_zoneTrackerScroll = 0;
            return 0;
        }
        if (g_questLogOpen) {
            g_questLogScroll -= (delta / 120) * 20;
            if (g_questLogScroll < 0) g_questLogScroll = 0;
            return 0;
        }
        /* Loot tab in the Ctrl+V dev menu uses scrollable lists (Sets, Uniques). */
        if (g_cheatMenuOpen) {
            extern int g_cheatMenuWheelDelta;
            g_cheatMenuWheelDelta += delta;
            return 0;
        }

        /* nothing of ours is open, so the wheel belongs to the game. */
        {
            extern int  g_wheelSkillSlots;
            extern int  g_wheelSkillCur;
            extern void SimulateKey(BYTE vk, BOOL down);
            int step = (delta > 0) ? -1 : 1;      /* wheel up = previous slot */
            g_wheelSkillCur += step;
            while (g_wheelSkillCur < 0) g_wheelSkillCur += g_wheelSkillSlots;
            g_wheelSkillCur %= g_wheelSkillSlots;
            BYTE vk = (BYTE)(VK_F1 + g_wheelSkillCur);
            SimulateKey(vk, TRUE);
            SimulateKey(vk, FALSE);
            return 0;
        }
    }

    /* Title screen button cleanup timer */
    if (msg == WM_TIMER && wp == TITLE_CLEANUP_TIMER_ID) {
        KillTimer(hwnd, TITLE_CLEANUP_TIMER_ID);
        Log("TITLE CLEANUP: Timer fired!\n");
        CleanupTitleButtons();
        /* If cleanup didn't find buttons yet, try again */
        if (!g_titleCleanupDone && !Player()) {
            SetTimer(hwnd, TITLE_CLEANUP_TIMER_ID, 1000, NULL);
            Log("TITLE CLEANUP: Retrying in 1s...\n");
        } else if (!g_titleCleanupDone && Player()) {
            /* Player is in-game, stop trying to clean title buttons */
            g_titleCleanupDone = 1;
            Log("TITLE CLEANUP: Player in-game, stopping cleanup\n");
        }
        return 0;
    }

    /* Block mouse clicks from reaching game when our UI is open */
    /* AP panel input handling (title screen) — only when panel is visible (not in-game) */
    if (msg == WM_KEYDOWN || msg == WM_CHAR) {
        DWORD _now = GetTickCount();
        BOOL apPanelVisible = !g_lastDrawGameUITime || (_now - g_lastDrawGameUITime) >= 500;
        if (apPanelVisible && HandleAPKeyInput(msg, wp, lp)) return 0;
        if (!apPanelVisible && g_apFocusField >= 0) g_apFocusField = -1;
    }
    /* 1.9.0 Phase 5.1 — F1 Collection search-box keyboard input. */
    if (g_editorOpen && g_editorPage >= 3 && g_editorPage <= 7) {
        extern char s_collSearchBuf[40];
        extern BOOL s_collSearchFocus;
        if (s_collSearchFocus) {
            if (msg == WM_KEYDOWN) {
                if (wp == VK_ESCAPE || wp == VK_RETURN) {
                    s_collSearchFocus = FALSE;
                    return 0;
                }
                if (wp == VK_BACK) {
                    int l = (int)strlen(s_collSearchBuf);
                    if (l > 0) s_collSearchBuf[l - 1] = 0;
                    return 0;
                }
            }
            if (msg == WM_CHAR) {
                char ch = (char)wp;
                if (ch >= 32 && ch <= 126) {  /* printable ASCII only */
                    int l = (int)strlen(s_collSearchBuf);
                    if (l < (int)sizeof(s_collSearchBuf) - 1) {
                        s_collSearchBuf[l] = ch;
                        s_collSearchBuf[l + 1] = 0;
                    }
                    return 0;
                }
                /* swallow control chars too so they don't leak */
                if (ch < 32) return 0;
            }
        }
    }

    /* AP page 2 input handling (in-game book) */
    if (g_editorOpen && g_editorPage == 2) {
        /* Find g_apPageFocus — it's static in page 2 render, use extern trick */
        static int* s_pApPageFocus = NULL;
        /* We use a global to communicate between render and WndProc */
        if (msg == WM_KEYDOWN) {
            if (wp == VK_TAB && g_apPageFocus >= 0) { g_apPageFocus = (g_apPageFocus + 1) % 3; return 0; }
            if (wp == VK_RETURN || wp == VK_ESCAPE) { g_apPageFocus = -1; return 0; }
            if (wp == VK_BACK && g_apPageFocus >= 0) {
                char* f = (g_apPageFocus == 0) ? g_apIP : (g_apPageFocus == 1) ? g_apSlot : g_apPassword;
                int l = (int)strlen(f); if (l > 0) f[l-1] = 0;
                return 0;
            }
        }
        if (msg == WM_CHAR && g_apPageFocus >= 0) {
            char ch = (char)wp; if (ch < 32 || ch > 126) return 0;
            char* f = (g_apPageFocus == 0) ? g_apIP : (g_apPageFocus == 1) ? g_apSlot : g_apPassword;
            int maxL = (g_apPageFocus == 0) ? 62 : 30;
            int l = (int)strlen(f);
            if (l < maxL) { f[l] = ch; f[l+1] = 0; }
            return 0;
        }
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        int cmx = (short)LOWORD(lp), cmy = (short)HIWORD(lp);
        /* Convert window coords to game coords for AP panel */
        int gmx = cmx, gmy = cmy;
        {
            RECT rc; GetClientRect(g_gameHwnd, &rc);
            int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
            if (cw > 0 && ch > 0) {
                gmx = (cmx * g_screenW) / cw;
                gmy = (cmy * g_screenH) / ch;
            }
        }
        /* gate the legacy AP-panel click handler on the same title-screen-visible check that the keyboard handler at line ~234 already uses. */
        {
            DWORD _click_now = GetTickCount();
            BOOL apPanelVisible = !g_lastDrawGameUITime ||
                                  (_click_now - g_lastDrawGameUITime) >= 500;
            if (apPanelVisible) HandleAPPanelClick(gmx, gmy);
        }

        /* Route clicks through the stash overlay. */
        if (StashUIHandleClick(gmx, gmy)) {
            return 0;
        }
    }
    /* Shift+right-click -> quick-stash. */
    if (msg == WM_RBUTTONDOWN) {
        BOOL shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        POINT pt = {0, 0};
        int mx = 0, my = 0, pmx = -1, pmy = -1;
        if (GetCursorPos(&pt) && g_gameHwnd) ScreenToClient(g_gameHwnd, &pt);
        extern int MouseX(void);
        extern int MouseY(void);
        mx = MouseX(); my = MouseY();
        if (pMouseX) pmx = *pMouseX;
        if (pMouseY) pmy = *pMouseY;
        if (g_verboseInput) Log("RCLICK | shift=%d | MouseX/Y=(%d,%d) | pMouseX/Y=(%d,%d) "
            "| WndXY=(%ld,%ld) | screen=%dx%d\n",
            (int)shiftHeld, mx, my, pmx, pmy, pt.x, pt.y,
            g_screenW, g_screenH);
        if (shiftHeld) {
            if (StashQuickMoveToStash()) {
                return 0;
            }
        }
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK ||
        msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) {
        if (g_editorOpen || g_questLogOpen || g_menuOpen || g_zoneTrackerOpen
            || g_cheatMenuOpen) {
            return 0; /* Block from game, our UI reads via GetAsyncKeyState */
        }
    }

    /* SHOW-ITEMS TOGGLE. Holding Alt to see what is on the floor is fine for a second and tiring for a whole session, so Alt can latch: tap it once and the labels stay up, tap again and they go away. The game is never told anything new — it still just sees Alt held down, which is exactly what the controller's left trigger has always done (d2arch_input.c). What changes is who decides when to let go. The synthetic Alt carries a marker in dwExtraInfo so this handler can recognise its own keystroke and pass it through; without that, feeding Alt back in would re-enter here and latch forever. */
    if (g_showItemsToggleOn &&
        (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) &&
        (BYTE)wp == g_keyShowItems &&
        !(lp & 0x40000000)) {              /* ignore auto-repeat while held */
        ShowItems_Toggle();
        /* Deliberately NOT swallowed. */
    }
    /* Losing focus with a latched Alt would leave the key stuck down in whatever the player switched to. */
    if (msg == WM_KILLFOCUS || msg == WM_ACTIVATE) {
        if (msg == WM_KILLFOCUS || LOWORD(wp) == WA_INACTIVE) ShowItems_ForceOff();
    }

    /* Vanilla D2 key remapping: translate user's custom key to D2's expected key. */
    if ((msg == WM_KEYDOWN || msg == WM_KEYUP ||
         msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) && g_keyRemapCount > 0) {
        for (int kr = 0; kr < g_keyRemapCount; kr++) {
            if ((BYTE)wp == g_keyRemaps[kr].userKey) {
                wp = (WPARAM)g_keyRemaps[kr].d2Key;
                break;
            }
        }
    }

    return CallWindowProcA(g_origWndProc, hwnd, msg, wp, lp);
}

/* INSTALL WNDPROC HOOK */
static void InstallWndProcHook(void) {
    if (g_gameHwnd) return;
    g_gameHwnd = FindWindowA("Diablo II", NULL);
    if (!g_gameHwnd) return;

    g_origWndProc = (WNDPROC)SetWindowLongA(g_gameHwnd, GWL_WNDPROC, (LONG)HookWndProc);
    Log("WndProc hooked: hwnd=%p orig=%p\n", g_gameHwnd, g_origWndProc);

    /* If title cleanup timer wasn't set yet, set it now */
    if (!g_titleCleanupDone && g_ppControlList && fnControlDestroy) {
        SetTimer(g_gameHwnd, TITLE_CLEANUP_TIMER_ID, 2000, NULL);
        Log("TITLE CLEANUP: Timer set from WndProc install (2s delay)\n");
    }

    /* Load keybindings from INI */
    LoadKeybindings();

    /* Install packet logger hook on D2Client SendPacket */
    InstallSendPacketHook();

    /* redirect the two USER32 key-state imports D2Client uses to decide whether to label items on the ground, so the Alt latch can answer them. */
    ShowItems_InstallHook();
}

/* INIT API - Resolve all function pointers */
static void InitAPI(void) {
    hD2Client = GetModuleHandleA("D2Client.dll");
    hD2Common = GetModuleHandleA("D2Common.dll");
    hD2Win    = GetModuleHandleA("D2Win.dll");
    hD2Gfx   = GetModuleHandleA("D2Gfx.dll");
    hD2Game   = GetModuleHandleA("D2Game.dll");
    hD2Net    = GetModuleHandleA("D2Net.dll");

    if (!hD2Client || !hD2Common || !hD2Win || !hD2Gfx) {
        Log("ERROR: Missing game DLLs\n");
        return;
    }

    /* D2Win drawing functions */
    fnText = (DrawText_t)GetProcAddress(hD2Win, (LPCSTR)10117);
    fnFont = (SetFont_t)GetProcAddress(hD2Win, (LPCSTR)10127);

    /* D2Gfx drawing */
    fnRect = (DrawRect_t)GetProcAddress(hD2Gfx, (LPCSTR)10055);
    fnLine = (DrawLine_t)GetProcAddress(hD2Gfx, (LPCSTR)10057);
    /* D2Win cell file functions (high-level, no CelContext needed) */
    fnCelLoad = (WinLoadCellFile_t)GetProcAddress(hD2Win, (LPCSTR)10039);
    fnCelDraw = (WinDrawCellFile_t)GetProcAddress(hD2Win, (LPCSTR)10134);
    fnCelFree = (WinFreeCellFile_t)GetProcAddress(hD2Win, (LPCSTR)10041);  /* 2.8.1 (Fix 11) */

    /* D2Win control functions for title screen button removal */
    fnControlDestroy = (ControlDestroy_t)GetProcAddress(hD2Win, (LPCSTR)10018);

    /* gpControlList offset depends on D2Win version: Vanilla 1.10f (base 0x6F8A0000): offset 0x5E24C PD2's D2Win (base 0x6F8E0000): offset 0xC9E4C Auto-detect based on ImageBase */
    {
        DWORD dwBase = (DWORD)hD2Win;
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)dwBase;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(dwBase + dos->e_lfanew);
        DWORD imgBase = nt->OptionalHeader.ImageBase;
        if (imgBase == 0x6F8A0000)
            g_ppControlList = (void**)((BYTE*)hD2Win + 0x5E24C);  /* vanilla */
        else
            g_ppControlList = (void**)((BYTE*)hD2Win + 0xC9E4C);  /* PD2 */
    }

    Log("CelDraw=%p CelLoad=%p Line=%p\n", fnCelDraw, fnCelLoad, fnLine);
    Log("ControlDestroy=%p gpControlList=%p\n", fnControlDestroy, g_ppControlList);

    /* D2Common stats */
    fnGetStat     = (GetUnitStat_t)GetProcAddress(hD2Common, (LPCSTR)10520);
    fnSetStat     = (SetUnitStat_t)GetProcAddress(hD2Common, (LPCSTR)10517);
    fnAddStat     = (AddUnitStat_t)GetProcAddress(hD2Common, (LPCSTR)10518);

    /* D2Game PLRTRADE_AddGold — proper gold add with validation + client sync. */
    if (hD2Game) {
        /* PLRTRADE_AddGold: D2Game base + 0x62890 Known address 0x6FC92890, D2Game base 0x6FC30000, offset = 0x62890 */
        fnAddGold = (AddGold_t)((DWORD)hD2Game + 0x62890);
        /* SpawnSuperUnique: address 0x6FC6F690, base 0x6FC30000, offset = 0x3F690 (D2Debugger confirms: 0x6FC6F690 - 0x6FC30000 = 0x3F690) SpawnMonster: 0x6FC69F10 - 0x6FC30000 = 0x39F10 */
        fnSpawnSuperUnique = (SpawnSuperUnique_t)((DWORD)hD2Game + 0x3F690);
        fnSpawnMonster = (SpawnMonster_t)((DWORD)hD2Game + 0x39F10);
        /* Read SuperUnique count from sgptDataTables + 0xADC. */
        {
            void* sgpt = NULL;
            DWORD* pDT = (DWORD*)GetProcAddress(hD2Common, (LPCSTR)10042);
            if (pDT) {
                __try { sgpt = (void*)*pDT; } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (sgpt) {
                g_superUniqueCount = *(int*)((DWORD)sgpt + 0xADC);
                Log("  SuperUnique count from sgptDT+0xADC: %d\n", g_superUniqueCount);
            } else {
                g_superUniqueCount = 66; /* vanilla 1.10f (1.8.0: cow extracted, was 67) */
                Log("  SuperUnique count fallback: %d\n", g_superUniqueCount);
            }
        }
        Log("  D2Game base=%p, fnAddGold=%p, fnSpawnSuperUnique=%p\n", hD2Game, fnAddGold, fnSpawnSuperUnique);
        /* Hook GAME_UpdateClients (ordinal 10005) to process gold in D2Game context */
        FARPROC fnGameUpdate = GetProcAddress(hD2Game, (LPCSTR)10005);
        if (fnGameUpdate && !g_gameUpdateHooked) {
            DWORD hookAddr3 = (DWORD)fnGameUpdate;
            BYTE firstByte = ((BYTE*)hookAddr3)[0];
            /* Skip if already patched. */
            if (firstByte == 0xE9 || firstByte == 0xCC || firstByte == 0x00) {
                Log("  GAME_UpdateClients: skipping hook install — prologue is %02X (already hooked or invalid)\n", firstByte);
            } else {
                g_gameUpdateHookAddr = hookAddr3;
                DWORD oldProt3, tp3;
                if (VirtualProtect((void*)g_gameUpdateHookAddr, 8, PAGE_EXECUTE_READWRITE, &oldProt3)) {
                    memcpy(g_gameUpdateTrampoline, (void*)g_gameUpdateHookAddr, 5);
                    g_gameUpdateTrampoline[5] = 0xE9;
                    DWORD jmpBack3 = (g_gameUpdateHookAddr + 5) - ((DWORD)&g_gameUpdateTrampoline[5] + 5);
                    memcpy(&g_gameUpdateTrampoline[6], &jmpBack3, 4);
                    VirtualProtect(g_gameUpdateTrampoline, sizeof(g_gameUpdateTrampoline), PAGE_EXECUTE_READWRITE, &tp3);
                    g_gameUpdateTrampolinePtr = (DWORD)g_gameUpdateTrampoline;

                    BYTE gPatch[5]; gPatch[0] = 0xE9;
                    DWORD gJmp = (DWORD)GameUpdateHook - (g_gameUpdateHookAddr + 5);
                    memcpy(&gPatch[1], &gJmp, 4);
                    memcpy((void*)g_gameUpdateHookAddr, gPatch, 5);
                    VirtualProtect((void*)g_gameUpdateHookAddr, 8, oldProt3, &oldProt3);
                    g_gameUpdateHooked = TRUE;
                    Log("  GAME_UpdateClients hooked at %08X (for gold processing)\n", g_gameUpdateHookAddr);
                } else {
                    Log("  GAME_UpdateClients: VirtualProtect FAILED (err=%d) — hook NOT installed\n", GetLastError());
                }
            }
        }
        Log("  fnAddGold=%p (D2Game PLRTRADE_AddGold)\n", fnAddGold);

        /* Hook D2Debugger's D2DebugGame export to capture pGame. */
        {
            HMODULE hDbg = GetModuleHandleA("D2Debugger.dll");
            if (!hDbg) {
                hDbg = LoadLibraryA("D2Debugger.dll");
                if (hDbg) Log("  D2Debugger.dll loaded via LoadLibrary\n");
                else Log("  D2Debugger.dll LoadLibrary FAILED (err=%d)\n", GetLastError());
            }
            if (hDbg) {
                /* Try mangled C++ name first */
                FARPROC fnDbgGame = GetProcAddress(hDbg, "?D2DebugGame@@YA_NPAUD2GameStrc@@@Z");
                if (!fnDbgGame) {
                    /* Try unmangled name */
                    fnDbgGame = GetProcAddress(hDbg, "D2DebugGame");
                }
                if (fnDbgGame) {
                    static volatile LONG s_dbgGameHooked = 0;
                    DWORD hookAddr5 = (DWORD)fnDbgGame;
                    BYTE firstByte = ((BYTE*)hookAddr5)[0];

                    /* Log first 16 bytes to see actual prologue */
                    Log("  D2DebugGame at %08X, bytes: ", hookAddr5);
                    for (int b = 0; b < 16; b++) Log("%02X ", ((BYTE*)hookAddr5)[b]);
                    Log("\n");

                    /* Guard against duplicate hook install + invalid prologue. */
                    if (InterlockedExchange(&s_dbgGameHooked, 1) != 0) {
                        Log("  D2DebugGame: already hooked — skipping duplicate install\n");
                    } else if (firstByte == 0xE9 || firstByte == 0xCC || firstByte == 0x00) {
                        Log("  D2DebugGame: skipping hook install — prologue is %02X (already hooked or invalid)\n", firstByte);
                    } else {
                        /* From log: 55 8B EC 51 6A 00 6A 00 68 38 56 DA 53 push ebp(1) mov ebp,esp(3) push ecx(4) push 0(6) push 0(8) Next instruction: push IMM32 (5 bytes, starts at byte 8) So instruction boundary is at exactly 8 bytes. */
                        int copyBytes = 8;

                        __try {
                            DWORD oldProt5, tp5;
                            if (!VirtualProtect((void*)hookAddr5, copyBytes + 4, PAGE_EXECUTE_READWRITE, &oldProt5)) {
                                Log("  D2DebugGame: VirtualProtect FAILED (err=%d) — hook NOT installed\n", GetLastError());
                            } else {
                                /* Build trampoline: copy original N bytes + JMP back */
                                memcpy(g_debugGameTrampoline, (void*)hookAddr5, copyBytes);
                                g_debugGameTrampoline[copyBytes] = 0xE9;
                                DWORD jmpBack5 = (hookAddr5 + copyBytes) - ((DWORD)&g_debugGameTrampoline[copyBytes] + 5);
                                memcpy(&g_debugGameTrampoline[copyBytes + 1], &jmpBack5, 4);
                                VirtualProtect(g_debugGameTrampoline, sizeof(g_debugGameTrampoline), PAGE_EXECUTE_READWRITE, &tp5);
                                g_origD2DebugGame = (D2DebugGame_t)(void*)g_debugGameTrampoline;

                                /* Patch original with JMP to our C wrapper + NOP padding */
                                BYTE dbgPatch[10];
                                dbgPatch[0] = 0xE9;
                                DWORD dbgJmp = (DWORD)HookD2DebugGame - (hookAddr5 + 5);
                                memcpy(&dbgPatch[1], &dbgJmp, 4);
                                for (int n = 5; n < copyBytes; n++) dbgPatch[n] = 0x90; /* NOP */
                                memcpy((void*)hookAddr5, dbgPatch, copyBytes);

                                VirtualProtect((void*)hookAddr5, copyBytes + 4, oldProt5, &oldProt5);
                                Log("  D2DebugGame hooked at %08X (%d bytes, pGame capture)\n", hookAddr5, copyBytes);
                            }
                        } __except(EXCEPTION_EXECUTE_HANDLER) {
                            Log("  D2DebugGame: EXCEPTION during hook install — aborted\n");
                        }
                    }
                } else {
                    Log("  WARNING: D2DebugGame export not found in D2Debugger.dll\n");
                }
            } else {
                Log("  WARNING: D2Debugger.dll not loaded (no pGame capture for traps)\n");
            }
        }
    }

    /* D2Net CLIENT_Send — ordinal 10005, sends client->server packets */
    if (hD2Net)
        fnClientSend = (ClientSend_t)GetProcAddress(hD2Net, (LPCSTR)10005);
    Log("  fnClientSend=%p (D2Net ordinal 10005)\n", fnClientSend);

    /* Quest state check — D2Common ordinal 11107 */
    if (hD2Common)
        fnGetQuestState = (QuestRecordGetState_t)GetProcAddress(hD2Common, (LPCSTR)11107);
    Log("  fnGetQuestState=%p (D2Common ordinal 11107)\n", fnGetQuestState);

    /* Quest flags: read from pGame->pQuestControl->pQuestFlags (server-side). */
    Log("  Quest detection: polling pGame+0x10F4->+0x0C (server quest flags)\n");

    /* Waypoint check — D2Common ordinal 11146 IMPORTANT: We need the function from the ORIGINAL D2Common, not the D2MOO patch. */
    fnWaypointIsActivated = (WaypointIsActivated_t)GetProcAddress(hD2Common, (LPCSTR)11146);
    Log("  fnWaypointIsActivated=%p (D2Common ordinal 11146, patch)\n", fnWaypointIsActivated);

    /* Also try to find the original D2Common by scanning loaded modules */
    {
        HMODULE hOrigD2Common = NULL;
        HMODULE mods[256];
        DWORD cbNeeded;
        typedef BOOL (WINAPI *EnumProc_t)(HANDLE, HMODULE*, DWORD, LPDWORD);
        HMODULE hPsapi = LoadLibraryA("psapi.dll");
        if (hPsapi) {
            EnumProc_t pEnum = (EnumProc_t)GetProcAddress(hPsapi, "EnumProcessModules");
            typedef DWORD (WINAPI *GetModName_t)(HANDLE, HMODULE, LPSTR, DWORD);
            GetModName_t pGetName = (GetModName_t)GetProcAddress(hPsapi, "GetModuleFileNameExA");
            if (pEnum && pGetName) {
                if (pEnum(GetCurrentProcess(), mods, sizeof(mods), &cbNeeded)) {
                    int nMods = cbNeeded / sizeof(HMODULE);
                    for (int i = 0; i < nMods; i++) {
                        char modName[MAX_PATH];
                        pGetName(GetCurrentProcess(), mods[i], modName, MAX_PATH);
                        /* Find D2Common.dll that is NOT in patch/ */
                        char* fname = strrchr(modName, '\\');
                        if (fname) fname++; else fname = modName;
                        if (_stricmp(fname, "D2Common.dll") == 0 &&
                            strstr(modName, "patch") == NULL &&
                            mods[i] != hD2Common) {
                            hOrigD2Common = mods[i];
                            Log("  Found ORIGINAL D2Common at %p: %s\n", mods[i], modName);
                            break;
                        }
                    }
                }
            }
            FreeLibrary(hPsapi);
        }
        if (hOrigD2Common) {
            WaypointIsActivated_t origFn = (WaypointIsActivated_t)GetProcAddress(hOrigD2Common, (LPCSTR)11146);
            if (origFn) {
                fnWaypointIsActivated = origFn;
                Log("  Using ORIGINAL D2Common WaypointIsActivated at %p\n", origFn);
            }
        }
    }

    /* Player function - hardcoded offset for 1.10f */
    fnGetPlayer = (GetPlayer_t)((DWORD)hD2Client + 0x883D0);

    /* Mouse position pointers */
    pMouseX = (int*)((DWORD)hD2Client + 0xB7BC0);
    pMouseY = (int*)((DWORD)hD2Client + 0xB7BD0);

    g_d2clientBase = (DWORD)hD2Client;

    /* D2Common skill functions (by ordinal) */
    fnGetRightSkill = (GetUnitSkill_t)GetProcAddress(hD2Common, (LPCSTR)10322);
    fnGetLeftSkill  = (GetUnitSkill_t)GetProcAddress(hD2Common, (LPCSTR)10321);

    /* ARCHIPELAGO: NOP the 4 CALL instructions in the draw loop that render vanilla's skill tree content. */
    if (g_d2clientBase) {
        /* Remove vanilla skill tree rendering */
        {
            DWORD nopAddrs[] = {
                0x820CC, 0x81FF2, 0x81FED,
                0x76E43, 0x76E25, 0x81F9D,
            };
            int ni;
            for (ni = 0; ni < 6; ni++) {
                DWORD addr = g_d2clientBase + nopAddrs[ni];
                DWORD op;
                if (VirtualProtect((void*)addr, 5, PAGE_EXECUTE_READWRITE, &op)) {
                    memset((void*)addr, 0x90, 5);
                    VirtualProtect((void*)addr, 5, op, &op);
                }
            }
        }

        /* Resolve D2Client GetUIVar function for panel state queries */
        g_getUIVarAddr = g_d2clientBase + 0xBE400;
        /* 1.9.2 fix: cast must include __fastcall so call sites use the right convention (CallGetUIVar trampoline reads ECX). */
        g_fnGetUIVar = (DWORD(__fastcall *)(DWORD))CallGetUIVar;
        Log("SKILL TREE: GetUIVar at %08X\n", g_getUIVarAddr);
    }

    /* Detect game resolution and calculate mouse mapping */
    DetectResolution();
    CalcMouseMapping();

    Log("InitAPI complete:\n");
    Log("  D2Client=%p D2Common=%p D2Win=%p D2Gfx=%p\n", hD2Client, hD2Common, hD2Win, hD2Gfx);
    Log("  fnText=%p fnFont=%p fnRect=%p\n", fnText, fnFont, fnRect);
    Log("  fnGetStat=%p fnSetStat=%p\n", fnGetStat, fnSetStat);
    Log("  fnGetPlayer=%p\n", fnGetPlayer);
    Log("  pMouseX=%p pMouseY=%p\n", pMouseX, pMouseY);
    Log("  getRightSkill=%p getLeftSkill=%p  (raw packets go through fnClientSend, D2Net ordinal 10005)\n",
        fnGetRightSkill, fnGetLeftSkill);
    Log("  screenW=%d screenH=%d\n", g_screenW, g_screenH);
}

/* D2.DETOURS ENTRY POINTS These are called by D2.Detours automatically */

/* Called when D2Client.dll is loaded - we initialize here */
extern "C" __declspec(dllexport) void __cdecl D2Arch_Init(void) {
    /* Process-level one-shot guard via named mutex (NOT a static local). */
    {
        char mutexName[64];
        _snprintf(mutexName, sizeof(mutexName) - 1,
                  "D2Arch_Init_pid%lu", GetCurrentProcessId());
        mutexName[sizeof(mutexName) - 1] = 0;
        HANDLE hMtx = CreateMutexA(NULL, FALSE, mutexName);
        if (hMtx == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (hMtx) CloseHandle(hMtx);
            return;
        }
        /* Intentionally leaked — kept alive for process lifetime. */
    }

    /* Open log */
    char logPath[MAX_PATH];
    GetModuleFileNameA(NULL, logPath, MAX_PATH);
    char* slash = strrchr(logPath, '\\');
    if (slash) {
        int remaining = MAX_PATH - (int)(slash - logPath) - 1;
        if (remaining > 15) strcpy(slash + 1, "d2arch_log.txt");
    }
    g_logFile = fopen(logPath, "w");

    Log("=== D2Archipelago " D2ARCH_VERSION_DISPLAY " (D2MOO + AP) ===\n");

    LoadConfig();
    Log("Config loaded: SavePath=%s GamePath=%s\n",
        g_cfgSavePath[0] ? g_cfgSavePath : "(auto-detect)",
        g_cfgGamePath[0] ? g_cfgGamePath : "(auto-detect)");

    /* Apply config to AP connection fields */
    if (g_cfgServerIP[0]) strncpy(g_apIP, g_cfgServerIP, 63);
    if (g_cfgSlotName[0]) strncpy(g_apSlot, g_cfgSlotName, 31);
    if (g_cfgPassword[0]) strncpy(g_apPassword, g_cfgPassword, 31);

    /* Check if launcher started us in AP mode (env var set before game start) */
    {
        char apModeEnv[8] = {0};
        GetEnvironmentVariableA("D2ARCH_AP_MODE", apModeEnv, sizeof(apModeEnv));
        if (apModeEnv[0] == '1') {
            /* launcher path sets g_apPolling so PollAPStatus runs as soon as the bridge starts, but g_apMode stays FALSE until the bridge actually authenticates. */
            extern BOOL g_apPolling;
            g_apPolling = TRUE;
            Log("AP MODE: enabled (from launcher) — polling on, g_apMode deferred to auth\n");
        } else {
            /* In standalone launches, scrub any leftover ap_settings.dat from a prior AP session (a clean disconnect deletes it via PollAPStatus, but alt-F4 / crashes leave it behind, after which it would leak into new offline characters via LoadAPSettings()). */
            char dir[MAX_PATH], path[MAX_PATH];
            GetArchDir(dir, MAX_PATH);
            sprintf(path, "%sap_settings.dat", dir);
            if (DeleteFileA(path)) {
                Log("AP: scrubbed stale ap_settings.dat at startup (standalone launch)\n");
            }
        }
    }

    /* the Multiworld-Launcher sets D2ARCH_LAUNCHER=1 before launch when it owns randomization + the AP connection. */
    {
        char launcherEnv[8] = {0};
        GetEnvironmentVariableA("D2ARCH_LAUNCHER", launcherEnv, sizeof(launcherEnv));
        BOOL byEnv = (launcherEnv[0] == '1');
        /* Also honor d2arch.ini [launcher] HideInGameUI=1 — bulletproof even if the env var doesn't propagate through the bootstrap/D2.Detours chain (the mod always reads the ini from disk). */
        char iniL[MAX_PATH]; GetArchDir(iniL, MAX_PATH); strcat(iniL, "d2arch.ini");
        BOOL byIni = (GetPrivateProfileIntA("launcher", "HideInGameUI", 0, iniL) != 0);
        if (byEnv || byIni) {
            g_launcherHidesTitleUI = TRUE;
            Log("LAUNCHER MODE: in-game AP/randomizer UI hidden (env=%d ini=%d)\n",
                (int)byEnv, (int)byIni);
        }
    }

    /* V2 launcher named-pipe transport. */
    ApPipe_InitFromIni();

    InitAPI();

    /* Initialize new subsystems */
    ItemLogInit();
    /* 1.8.0 cleanup: CustomSU_Init() call extracted */
    StashInit();
    StashLoadShared();   /* Shared stash persists across characters */
    StashLogicInit();    /* stacking whitelist + insertion scanner */
    StashUIInit();       /* tab bar + click handling overlay */
    VersionPatchApply(); /* Overwrite D2's "v %d.%02d" format strings with "Beta 1.8.0" */
    CelHookInstall();    /* log every cellfile load path */
    Ubers_InstallHook(); /* 1.9.0 NEW: hook PLRTRADE_CreateCowPortal for Pandemonium recipes */

    {
        /* 1.9.0 NEW: F1 Collection page — validate static catalog at startup. */
        extern BOOL Coll_Init(void);
        if (!Coll_Init()) {
            Log("WARN: Coll_Init() failed — F1 Collection page will not function\n");
        }
    }

    g_initialized = TRUE;
    /* Restore any previously hidden characters (isolation disabled) */
    RestoreAllCharacters();

    /* run orphan-save cleanup at DLL load too, not only from OnCharacterLoad. */
    CleanupOrphanedSaves();

    Log("Init complete, waiting for player...\n");

    /* Install WndProc hook early so we catch title screen events too */
    InstallWndProcHook();

    /* Schedule title screen button cleanup. */
    if (g_ppControlList && fnControlDestroy && g_gameHwnd) {
        SetTimer(g_gameHwnd, TITLE_CLEANUP_TIMER_ID, 2000, NULL);
        Log("TITLE CLEANUP: Timer set (2s delay, hwnd=%p)\n", g_gameHwnd);
    } else if (g_ppControlList && fnControlDestroy) {
        /* Window not ready yet - timer will be set when WndProc installs */
        Log("TITLE CLEANUP: HWND not ready, will set timer when WndProc hooks\n");
    }
}

/* Called every frame when game UI is drawn */
extern "C" __declspec(dllexport) void __cdecl D2Arch_OnDrawGameUI(void) {
    if (!g_initialized) return;

    g_lastDrawGameUITime = GetTickCount();

    /* Install WndProc hook on first frame */
    if (!g_gameHwnd) {
        InstallWndProcHook();
    }

    /* ALSO drain the deferred-quest queue from the render path. */
    ProcessDeferredQuests();

    /* The non-quest goal check (boss/uber/cow kills) hung on the same message pump, but it actually scans, so it gets a throttle instead. */
    {
        static DWORD s_lastGoalScan = 0;
        DWORD now = GetTickCount();
        if (now - s_lastGoalScan >= 500) {
            extern void CheckNonQuestGoalCompletion(void);
            s_lastGoalScan = now;
            CheckNonQuestGoalCompletion();
        }
    }

    SafeDraw();
}

/* Called when the game shuts down */
extern "C" __declspec(dllexport) void __cdecl D2Arch_OnUnload(void) {
    Log("Unloading D2Archipelago...\n");

    /* persist a SESSION-END breadcrumb to d2arch_crash.txt (append; it survives the next launch's d2arch_log.txt truncation). */
    {
        extern char g_dbgCrumb[64];
        char cpth[MAX_PATH]; GetModuleFileNameA(NULL, cpth, MAX_PATH);
        char* sl2 = strrchr(cpth, '\\');
        if (sl2 && (int)(MAX_PATH - (sl2 - cpth) - 1) > 17) {
            strcpy(sl2 + 1, "d2arch_crash.txt");
            FILE* cf = fopen(cpth, "a");
            if (cf) {
                SYSTEMTIME st; GetLocalTime(&st);
                fprintf(cf, "\n# SESSION END %04d-%02d-%02d %02d:%02d:%02d  ver=%ls  "
                            "last-breadcrumb='%s'  char='%s'  g_cachedPGame=0x%08X\n",
                        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                        D2ARCH_VERSION_DISPLAY_W, g_dbgCrumb,
                        g_charName[0] ? g_charName : "?", g_cachedPGame);

                /* also persist the TAIL of d2arch_log.txt. */
                char lpth[MAX_PATH];
                GetModuleFileNameA(NULL, lpth, MAX_PATH);
                char* sl3 = strrchr(lpth, '\\');
                if (sl3 && (int)(MAX_PATH - (sl3 - lpth) - 1) > 15) {
                    strcpy(sl3 + 1, "d2arch_log.txt");
                    FILE* lf = fopen(lpth, "rb");
                    if (lf) {
                        fseek(lf, 0, SEEK_END);
                        long sz = ftell(lf);
                        long start = (sz > 24000) ? (sz - 24000) : 0;  /* last ~24KB */
                        fseek(lf, start, SEEK_SET);
                        fprintf(cf, "# ---- last %ld bytes of d2arch_log.txt (most recent activity) ----\n",
                                sz - start);
                        char buf[2048]; size_t rd;
                        while ((rd = fread(buf, 1, sizeof(buf), lf)) > 0) fwrite(buf, 1, rd, cf);
                        fprintf(cf, "\n# ---- end log tail ----\n");
                        fclose(lf);
                    }
                }
                fclose(cf);
            }
        }
    }

    /* Stop AP bridge if running */
    StopAPBridge();

    /* Restore EndScene hook */
    if (g_endSceneHooked && g_endSceneHookAddr) {
        DWORD oldProt;
        VirtualProtect((void*)g_endSceneHookAddr, 5, PAGE_EXECUTE_READWRITE, &oldProt);
        memcpy((void*)g_endSceneHookAddr, g_endSceneTrampoline, 5);
        VirtualProtect((void*)g_endSceneHookAddr, 5, oldProt, &oldProt);
    }

    /* Restore WndProc */
    if (g_gameHwnd && g_origWndProc) {
        SetWindowLongA(g_gameHwnd, GWL_WNDPROC, (LONG)g_origWndProc);
    }

    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = NULL;
    }
}

/* UI HOOK - called instead of original DrawGameUI Calls original (trampoline) then our overlay */
static DWORD g_trampolinePtr = 0; /* set after trampoline is built */

/* SetQuestState hook — intercept D2Game setting quest flags */
/* IAT hook: intercepts D2Game's calls to QUESTRECORD_SetQuestState */
static void __stdcall SetQuestState_IATHook(void* pQuestRecord, int nQuest, int nState) {
    /* Call original function first */
    if (g_origSetQuestState)
        g_origSetQuestState(pQuestRecord, nQuest, nState);

    /* Track completion flags */
    if (nQuest >= 0 && nQuest < MAX_D2_QUESTS) {
        int diff = g_currentDifficulty;
        if (diff < 0 || diff > 2) diff = 0;

        if (nState == 0 || nState == 13 || nState == 14 || nState == 15) {
            Log("QUEST SET: d2Quest=%d flag=%d diff=%d\n", nQuest, nState, diff);
            g_serverQuestComplete[diff][nQuest] = TRUE;
        }
    }
}

static void __declspec(naked) UIHook(void) {
    __asm {
        /* Call original function via trampoline pointer */
        call [g_trampolinePtr]
        /* Save registers, call our draw, restore */
        pushad
        call D2Arch_OnDrawGameUI
        popad
        ret
    }
}

/* MAIN THREAD - waits for D2Client then hooks DrawGameUI */
static DWORD WINAPI MainThread(LPVOID param) {
    (void)param;

    /* Process-level one-shot guard via named mutex. */
    char mutexName[64];
    _snprintf(mutexName, sizeof(mutexName) - 1,
              "D2Arch_MainThread_pid%lu", GetCurrentProcessId());
    mutexName[sizeof(mutexName) - 1] = 0;
    HANDLE hMtx = CreateMutexA(NULL, FALSE, mutexName);
    if (hMtx == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        /* Another DLL image already started MainThread for this process. */
        if (hMtx) CloseHandle(hMtx);
        return 0;
    }
    /* hMtx leaks intentionally — kept alive for process lifetime so a crash/restart of MainThread doesn't fail the guard. */

    /* PHASE 1: Title screen button cleanup. */
    {
        char ePath[MAX_PATH];
        GetModuleFileNameA(NULL, ePath, MAX_PATH);
        char* esl = strrchr(ePath, '\\');
        if (esl) strcpy(esl + 1, "d2arch_early.txt");
        FILE* ef = fopen(ePath, "a");

        if (ef) fprintf(ef, "PHASE1: Waiting for D2Win.dll...\n");

        while (!GetModuleHandleA("D2Win.dll")) { Sleep(10); }
        Sleep(50);

        HMODULE hW = GetModuleHandleA("D2Win.dll");
        if (ef) fprintf(ef, "PHASE1: D2Win.dll = %p\n", hW);

        if (hW) {
            hD2Win = hW;
            fnControlDestroy = (ControlDestroy_t)GetProcAddress(hW, (LPCSTR)10018);
            /* Auto-detect gpControlList offset based on ImageBase */
            {
                DWORD dwB = (DWORD)hW;
                IMAGE_DOS_HEADER* d = (IMAGE_DOS_HEADER*)dwB;
                IMAGE_NT_HEADERS* n = (IMAGE_NT_HEADERS*)(dwB + d->e_lfanew);
                DWORD ib = n->OptionalHeader.ImageBase;
                if (ib == 0x6F8A0000)
                    g_ppControlList = (void**)((BYTE*)hW + 0x5E24C);
                else
                    g_ppControlList = (void**)((BYTE*)hW + 0xC9E4C);
                if (ef) fprintf(ef, "PHASE1: ImageBase=0x%X offset=0x%X\n", ib,
                        (ib == 0x6F8A0000) ? 0x5E24C : 0xC9E4C);
            }
            if (ef) fprintf(ef, "PHASE1: fnControlDestroy=%p g_ppControlList=%p\n",
                    fnControlDestroy, g_ppControlList);
        }

        /* Resolve D2Win button creation functions */
        if (hW) {
            fnButtonCreate  = (ButtonCreate_t)GetProcAddress(hW, (LPCSTR)10107);
            fnButtonSetText = (ButtonSetText_t)GetProcAddress(hW, (LPCSTR)10186);
            fnCellFileLoad  = (CellFileLoad_t)GetProcAddress(hW, (LPCSTR)10039);
            if (ef) fprintf(ef, "PHASE1: ButtonCreate=%p ButtonSetText=%p CellFileLoad=%p\n",
                    fnButtonCreate, fnButtonSetText, fnCellFileLoad);

            /* install cel hook EARLY, before main menu renders, so we can see splash / titlescreen / background DC6 load paths. */
            CelHookInstall();

            /* Load settings from ini */
            TitleSettings_Load();
            if (ef) fprintf(ef, "PHASE1: Settings loaded from %s\n", ts_iniPath);
        }

        if (ef) { fflush(ef); fclose(ef); }
    }

    /* Wait for main menu to appear (detect SINGLE PLAYER button) */
    {
        char ePath5[MAX_PATH];
        GetModuleFileNameA(NULL, ePath5, MAX_PATH);
        char* esl5 = strrchr(ePath5, '\\');
        if (esl5) strcpy(esl5 + 1, "d2arch_early.txt");
        FILE* ef5 = fopen(ePath5, "a");

        if (ef5) fprintf(ef5, "BUTTONS: Waiting for main menu...\n");
        if (ef5) { fflush(ef5); fclose(ef5); }

        /* Wait until main menu is detected (SINGLE PLAYER button exists) */
        for (int wait = 0; wait < 60; wait++) { /* up to 30 seconds */
            Sleep(500);
            if (IsMainMenuActive()) break;
        }

        ef5 = fopen(ePath5, "a");
        if (ef5) fprintf(ef5, "BUTTONS: Main menu detected=%d\n", IsMainMenuActive());

        /* Patch D2Launch version string NOW, before main menu first renders. */
        VersionPatchApply();

        __try {
            /* Load toggle button cellfiles (130x35 pixels) + wide variants (260x35) for "Skill Hunting" / "Zone Locking" game-mode toggles that need more room for their longer labels. */
            if (fnCellFileLoad) {
                g_btnCellFile        = fnCellFileLoad("data\\global\\ui\\CharSelect\\settings_toggle", 0);
                g_btnCellFileRed     = fnCellFileLoad("data\\global\\ui\\CharSelect\\settings_toggle_red", 0);
                g_btnCellFileGreen   = fnCellFileLoad("data\\global\\ui\\CharSelect\\settings_toggle_green", 0); /* 1.8.2 */
                g_btnCellFileWide    = fnCellFileLoad("data\\global\\ui\\CharSelect\\settings_toggle_wide", 0);
                g_btnCellFileWideRed = fnCellFileLoad("data\\global\\ui\\CharSelect\\settings_toggle_wide_red", 0);
            }
            if (ef5) fprintf(ef5, "BUTTONS: CellFile=%p Wide=%p\n", g_btnCellFile, g_btnCellFileWide);

            /* Create settings buttons on right side of screen */
            TitleSettings_CreateButtons();
            if (ef5) fprintf(ef5, "BUTTONS: Created %d buttons\n", g_titleBtnCount);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            if (ef5) fprintf(ef5, "BUTTONS: CRASH during creation\n");
        }

        if (ef5) { fflush(ef5); fclose(ef5); }

        /* PHASE 2: Init D2Arch and hook DrawGameUI BEFORE the visibility loop */
        {
            HMODULE hC2 = NULL;
            int wc2 = 0;
            while (!hC2 && wc2 < 300) {
                hC2 = GetModuleHandleA("D2Client.dll");
                if (!hC2) hC2 = GetModuleHandleA("D2Client_original.dll");
                if (!hC2) { Sleep(100); wc2++; }
            }
            if (hC2) {
                D2Arch_Init();

                /* Hook DrawGameUI at D2Client + 0x5E650 */
                static volatile LONG s_drawGameUIHooked = 0;
                DWORD hookAddrUI = (DWORD)hC2 + 0x5E650;
                BYTE firstByteUI = ((BYTE*)hookAddrUI)[0];

                /* Guard against duplicate hook install. */
                if (InterlockedExchange(&s_drawGameUIHooked, 1) != 0) {
                    Log("DrawGameUI: already hooked — skipping duplicate install\n");
                } else if (firstByteUI == 0xE9 || firstByteUI == 0xCC || firstByteUI == 0x00) {
                    Log("DrawGameUI: skipping hook install — prologue is %02X (already hooked or invalid)\n", firstByteUI);
                } else {
                    g_hookAddr = hookAddrUI;
                    DWORD oldP2, tp2;
                    if (!VirtualProtect((void*)g_hookAddr, 8, PAGE_EXECUTE_READWRITE, &oldP2)) {
                        Log("DrawGameUI: VirtualProtect FAILED (err=%d) — hook NOT installed\n", GetLastError());
                    } else {
                        memcpy(g_trampoline, (void*)g_hookAddr, 8);
                        g_trampoline[8] = 0xE9;
                        DWORD jb2 = (g_hookAddr + 8) - ((DWORD)&g_trampoline[8] + 5);
                        memcpy(&g_trampoline[9], &jb2, 4);
                        VirtualProtect(g_trampoline, sizeof(g_trampoline), PAGE_EXECUTE_READWRITE, &tp2);
                        g_trampolinePtr = (DWORD)g_trampoline;
                        BYTE p2[8]; p2[0] = 0xE9;
                        DWORD jt2 = (DWORD)UIHook - (g_hookAddr + 5);
                        memcpy(&p2[1], &jt2, 4);
                        p2[5] = 0x90; p2[6] = 0x90; p2[7] = 0x90;
                        memcpy((void*)g_hookAddr, p2, 8);
                        VirtualProtect((void*)g_hookAddr, 8, oldP2, &oldP2);
                        Log("DrawGameUI HOOKED at %08X (UIHook=%p)\n", g_hookAddr, UIHook);
                    }
                }
            } else {
                Log("DrawGameUI: D2Client NOT FOUND after 30s wait\n");
            }
        }

        /* Visibility loop - destroy buttons when leaving, recreate when returning */
        {
            BOOL wasOnMenu = TRUE;
            while (1) {
                Sleep(200);
                __try {
                    BOOL onMenu = IsMainMenuActive();

                    if (!onMenu && wasOnMenu) {
                        /* Just LEFT main menu - destroy all our buttons */
                        for (int d = 0; d < g_titleBtnCount; d++) {
                            if (g_titleBtns[d] && fnControlDestroy) {
                                void* tmp = g_titleBtns[d];
                                fnControlDestroy(&tmp);
                                g_titleBtns[d] = NULL;
                            }
                        }
                        g_titleBtnCount = 0;
                        g_btnGoal = NULL; g_btnXP = NULL;
                        CloseAllDropdowns();
                    }

                    if (onMenu && !wasOnMenu) {
                        /* Just RETURNED to main menu - recreate everything */
                        Sleep(300);
                        /* Free the previous handles BEFORE reloading (KleinTimmi,
                         * 2026-08-29): this reload ran on every menu return and
                         * leaked 5 D2Win cel handles per save&quit cycle. Same
                         * free-then-reload pattern as the editor nav buttons
                         * (2.8.9); EdCelFree __try-guards and NULLs the slot. */
                        EdCelFree(&g_btnCellFile);
                        EdCelFree(&g_btnCellFileRed);
                        EdCelFree(&g_btnCellFileGreen);
                        EdCelFree(&g_btnCellFileWide);
                        EdCelFree(&g_btnCellFileWideRed);
                        if (fnCellFileLoad) {
                            g_btnCellFile        = fnCellFileLoad("data\\global\\ui\\CharSelect\\settings_toggle", 0);
                            g_btnCellFileRed     = fnCellFileLoad("data\\global\\ui\\CharSelect\\settings_toggle_red", 0);
                            g_btnCellFileGreen   = fnCellFileLoad("data\\global\\ui\\CharSelect\\settings_toggle_green", 0); /* 1.8.2 */
                            g_btnCellFileWide    = fnCellFileLoad("data\\global\\ui\\CharSelect\\settings_toggle_wide", 0);
                            g_btnCellFileWideRed = fnCellFileLoad("data\\global\\ui\\CharSelect\\settings_toggle_wide_red", 0);
                        }
                        TitleSettings_Load();
                        TitleSettings_CreateButtons();
                        for (int ic = 0; ic < g_titleBtnCount; ic++) {
                            if (g_titleBtns[ic] && g_titleBtnVals[ic])
                                SetBtnColor(g_titleBtns[ic], *g_titleBtnVals[ic]);
                        }
                        UpdateClassColors();
                        /* if AP is still authenticated when the player returns from char-select / in-game to the title menu, immediately overlay the slot_data values onto the freshly-created buttons. */
                        if (g_apConnected) SyncAPToTitleSettings();
                    }

                    wasOnMenu = onMenu;
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    }

    /* TITLE SCREEN CLEANUP: DISABLED - buttons are removed by patching D2Launch.dll Y coordinates instead of runtime cleanup. */
    #if 0
    __try {
    {
        char ePath3[MAX_PATH];
        GetModuleFileNameA(NULL, ePath3, MAX_PATH);
        char* esl3 = strrchr(ePath3, '\\');
        if (esl3) strcpy(esl3 + 1, "d2arch_early.txt");
        FILE* ef3 = fopen(ePath3, "a");

        if (ef3) fprintf(ef3, "CLEANUP: g_ppControlList=%p fnControlDestroy=%p\n",
                g_ppControlList, fnControlDestroy);

        if (g_ppControlList && fnControlDestroy) {
            if (ef3) fprintf(ef3, "CLEANUP: Reading *g_ppControlList...\n");
            if (ef3) { fflush(ef3); fclose(ef3); ef3 = NULL; }
        }
    }
    if (g_ppControlList && fnControlDestroy) {
        /* Wait up to 10 seconds for title screen controls to appear */
        int waited = 0;
        while (waited < 10000) {
            Sleep(500);
            waited += 500;

            /* Re-open log each iteration */
            char ePath4[MAX_PATH];
            GetModuleFileNameA(NULL, ePath4, MAX_PATH);
            char* esl4 = strrchr(ePath4, '\\');
            if (esl4) strcpy(esl4 + 1, "d2arch_early.txt");

            void* pCtrl = NULL;
            __try { pCtrl = *g_ppControlList; }
            __except(EXCEPTION_EXECUTE_HANDLER) {
                FILE* ef4 = fopen(ePath4, "a");
                if (ef4) { fprintf(ef4, "CRASH reading *g_ppControlList at %p\n", g_ppControlList); fclose(ef4); }
                pCtrl = NULL;
                break;
            }

            /* Log every attempt */
            {
                FILE* ef5 = fopen(ePath4, "a");
                if (ef5) {
                    fprintf(ef5, "  wait=%dms pCtrl=%p\n", waited, pCtrl);
                    /* At 5 seconds, scan ENTIRE .data section for pointers that look like controls */
                    if (waited == 5000) {
                        fprintf(ef5, "  FULL SCAN of D2Win .data section:\n");
                        /* .data starts at hD2Win+0x1F000, size ~0xACC58 */
                        BYTE* dataStart = (BYTE*)hD2Win + 0x1F000;
                        int found = 0;
                        for (DWORD off = 0; off < 0xACC58 && found < 50; off += 4) {
                            __try {
                                DWORD val = *(DWORD*)(dataStart + off);
                                /* Look for pointers that point to heap/stack (non-zero, in reasonable address range, and whose target starts with a small int that could be nType) */
                                if (val > 0x10000 && val < 0x7FFFFFFF) {
                                    __try {
                                        int type = *(int*)val;
                                        int x = *(int*)(val + 0x0C);
                                        int y = *(int*)(val + 0x10);
                                        int w = *(int*)(val + 0x14);
                                        int h = *(int*)(val + 0x18);
                                        /* Check if it looks like a D2WinControl */
                                        if (type >= 1 && type <= 13 &&
                                            x >= 0 && x < g_screenW + 100 && y >= 0 && y < g_screenH + 100 &&
                                            w > 10 && w < g_screenW && h > 10 && h < g_screenH) {
                                            fprintf(ef5, "    +0x%X: ptr=%p -> type=%d x=%d y=%d w=%d h=%d\n",
                                                    0x1F000 + off, (void*)val, type, x, y, w, h);
                                            found++;
                                        }
                                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                                }
                            } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        }
                        fprintf(ef5, "  SCAN done, found %d potential controls\n", found);
                    }
                    fflush(ef5); fclose(ef5);
                }
            }
            if (!pCtrl) continue;

            /* Count type=6 buttons */
            int btnCount = 0;
            void* p = pCtrl;
            while (p) {
                if (CTRL_TYPE(p) == D2WIN_BUTTON) btnCount++;
                p = CTRL_NEXT(p);
            }
            if (btnCount >= 3) {
                /* Title screen has loaded - clean up buttons */
                /* Find min and max Y among buttons (SINGLE PLAYER = top, EXIT = bottom) */
                int minY = 99999, maxY = -1;
                p = *g_ppControlList;
                while (p) {
                    if (CTRL_TYPE(p) == D2WIN_BUTTON) {
                        int y = CTRL_Y(p);
                        if (y < minY) minY = y;
                        if (y > maxY) maxY = y;
                    }
                    p = CTRL_NEXT(p);
                }

                /* Open early log for diagnostics */
                {
                    char ePath[MAX_PATH];
                    GetModuleFileNameA(NULL, ePath, MAX_PATH);
                    char* esl = strrchr(ePath, '\\');
                    if (esl) strcpy(esl + 1, "d2arch_early.txt");
                    FILE* ef = fopen(ePath, "a");
                    if (ef) {
                        fprintf(ef, "CLEANUP: Found %d buttons, Y range %d-%d\n", btnCount, minY, maxY);

                        /* Destroy all buttons except top (SINGLE PLAYER) and bottom (EXIT) */
                        p = *g_ppControlList;
                        int removed = 0;
                        while (p) {
                            void* next = CTRL_NEXT(p);
                            if (CTRL_TYPE(p) == D2WIN_BUTTON) {
                                int y = CTRL_Y(p);
                                if (y != minY && y != maxY) {
                                    fprintf(ef, "  REMOVING btn x=%d y=%d w=%d h=%d\n",
                                            CTRL_X(p), y, CTRL_W(p), CTRL_H(p));
                                    void* temp = p;
                                    fnControlDestroy(&temp);
                                    removed++;
                                } else {
                                    fprintf(ef, "  KEEPING btn x=%d y=%d w=%d h=%d\n",
                                            CTRL_X(p), y, CTRL_W(p), CTRL_H(p));
                                }
                            }
                            p = next;
                        }
                        fprintf(ef, "CLEANUP: Removed %d buttons\n", removed);
                        fclose(ef);
                    }
                }
                g_titleCleanupDone = 1;
                break;
            }
        }
    }

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        char ePath2[MAX_PATH];
        GetModuleFileNameA(NULL, ePath2, MAX_PATH);
        char* esl2 = strrchr(ePath2, '\\');
        if (esl2) strcpy(esl2 + 1, "d2arch_early.txt");
        FILE* ef2 = fopen(ePath2, "a");
        if (ef2) { fprintf(ef2, "CRASH in cleanup: exception caught\n"); fclose(ef2); }
    }
    #endif /* cleanup disabled */

    /* PHASE 2 code moved above visibility loop */

    return 0;
}

/* DLL ENTRY POINT */

/* Address -> "module.dll + 0x1234".
 *
 * The crash report used to resolve only the faulting address, which named the
 * module that DIED but never the one that CALLED. For a fault on the first
 * instruction of a function -- exactly what D2's SCmd.cpp assert looks like,
 * since it opens with cmp byte ptr [ecx], 0x97 -- the caller is sitting at
 * [ESP+00], and knowing whether that is D2's own code or ours is the whole
 * question. So the stack dump is annotated too. */
typedef struct { DWORD base, size; char leaf[24]; } ModSpan;

static int SnapshotModules(ModSpan* out, int cap) {
    HMODULE mods[128];
    DWORD needed;
    int n = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed))
        return 0;
    int count = needed / sizeof(HMODULE);
    for (int i = 0; i < count && n < cap; i++) {
        MODULEINFO mi;
        if (!GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof(mi)))
            continue;
        char path[MAX_PATH];
        if (!GetModuleFileNameA(mods[i], path, MAX_PATH)) continue;
        const char* leaf = strrchr(path, '\\');
        leaf = leaf ? leaf + 1 : path;
        out[n].base = (DWORD)mi.lpBaseOfDll;
        out[n].size = mi.SizeOfImage;
        strncpy(out[n].leaf, leaf, 23);
        out[n].leaf[23] = 0;
        n++;
    }
    return n;
}

static const ModSpan* FindModule(const ModSpan* mods, int n, DWORD addr) {
    for (int i = 0; i < n; i++)
        if (addr >= mods[i].base && addr < mods[i].base + mods[i].size)
            return &mods[i];
    return NULL;
}

/* Crash handler — logs exact crash address and module */
static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    DWORD excCode = ep->ExceptionRecord->ExceptionCode;
    BOOL isAV = (excCode == EXCEPTION_ACCESS_VIOLATION);
    /* ALSO capture NON-AV fatal-class faults. */
    BOOL isFatalClass = (excCode == EXCEPTION_BREAKPOINT)
                     || (excCode == EXCEPTION_ILLEGAL_INSTRUCTION)
                     || (excCode == EXCEPTION_PRIV_INSTRUCTION)
                     || (excCode == EXCEPTION_INT_DIVIDE_BY_ZERO)
                     || (excCode == EXCEPTION_ARRAY_BOUNDS_EXCEEDED)
                     || (((excCode & 0xF0000000) == 0xC0000000)
                          && excCode != EXCEPTION_ACCESS_VIOLATION
                          && excCode != EXCEPTION_STACK_OVERFLOW);
    if (isAV || isFatalClass) {
        DWORD crashAddr = (DWORD)ep->ExceptionRecord->ExceptionAddress;
        DWORD accessAddr = isAV ? (DWORD)ep->ExceptionRecord->ExceptionInformation[1] : 0;
        int wasWrite = isAV ? (int)ep->ExceptionRecord->ExceptionInformation[0] : 0;

        /* Find which module the crash is in */
        HMODULE hMods[128];
        DWORD cbNeeded;
        char modName[MAX_PATH] = "UNKNOWN";
        DWORD modBase = 0, modOffset = 0;

        if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded)) {
            int count = cbNeeded / sizeof(HMODULE);
            for (int i = 0; i < count; i++) {
                MODULEINFO mi;
                if (GetModuleInformation(GetCurrentProcess(), hMods[i], &mi, sizeof(mi))) {
                    DWORD base = (DWORD)mi.lpBaseOfDll;
                    DWORD end = base + mi.SizeOfImage;
                    if (crashAddr >= base && crashAddr < end) {
                        GetModuleFileNameA(hMods[i], modName, MAX_PATH);
                        modBase = base;
                        modOffset = crashAddr - base;
                        break;
                    }
                }
            }
        }

        /* module LEAF name (file, not full path) for compact, stable dedup keys. */
        const char* modLeaf = strrchr(modName, '\\');
        modLeaf = modLeaf ? modLeaf + 1 : modName;

        /* DEDUP + VERSION STAMP + CONTEXT. */
        static volatile LONG s_lock = 0;
        static DWORD s_off[128]; static char s_mod[128][24]; static DWORD s_cnt[128];
        static int s_n = 0; static BOOL s_hdr = FALSE;
        while (InterlockedExchange(&s_lock, 1)) Sleep(0);
        int idx = -1;
        for (int i = 0; i < s_n; i++)
            if (s_off[i] == modOffset && strncmp(s_mod[i], modLeaf, 23) == 0) { idx = i; break; }
        BOOL firstTime = (idx < 0);
        if (firstTime && s_n < 128) {
            idx = s_n++; s_off[idx] = modOffset;
            strncpy(s_mod[idx], modLeaf, 23); s_mod[idx][23] = 0; s_cnt[idx] = 0;
        }
        if (idx >= 0) s_cnt[idx]++;
        BOOL writeHdr = firstTime && !s_hdr;
        if (writeHdr) s_hdr = TRUE;
        InterlockedExchange(&s_lock, 0);

        /* Repeat of a site we already logged — count it, don't re-write. */
        if (!firstTime) return EXCEPTION_CONTINUE_SEARCH;

        /* Log to file */
        char logPath[MAX_PATH];
        GetModuleFileNameA(NULL, logPath, MAX_PATH);
        char* sl = strrchr(logPath, '\\');
        if (sl) strcpy(sl + 1, "d2arch_crash.txt");

        FILE* f = fopen(logPath, "a");
        if (f) {
            if (writeHdr) {
                SYSTEMTIME st; GetLocalTime(&st);
                fprintf(f, "\n##################################################\n");
                fprintf(f, "# D2Archipelago %ls  |  session %04d-%02d-%02d %02d:%02d:%02d\n",
                        D2ARCH_VERSION_DISPLAY_W, st.wYear, st.wMonth, st.wDay,
                        st.wHour, st.wMinute, st.wSecond);
                fprintf(f, "# each unique crash site logged ONCE; teardown=YES means the\n");
                fprintf(f, "# game was already torn down (save&quit race) when it fired\n");
                fprintf(f, "##################################################\n");
            }
            /* Live-state context — the single most useful clue for the save&quit teardown crashes: is the client player already gone, and what (stale?) game pointer were we using. */
            DWORD cg = g_cachedPGame;
            DWORD gp = 0;   /* live D2Game gpGame, to see if our cache drifted from it */
            if (hD2Game) { __try { gp = *(DWORD*)((DWORD)hD2Game + 0x1157FC); } __except(1) { gp = 0xDEADBEEF; } }
            void* cp = (void*)-1;
            if (fnGetPlayer) { __try { cp = fnGetPlayer(); } __except(1) { cp = (void*)-2; } }
            if (isAV) {
                fprintf(f, "=== first-chance ACCESS VIOLATION (usually CAUGHT by __try — not necessarily the crash) ===\n");
                fprintf(f, "Crash at: 0x%08X (%s + 0x%X)\n", crashAddr, modLeaf, modOffset);
                fprintf(f, "Tried to %s address: 0x%08X\n", wasWrite ? "WRITE" : "READ", accessAddr);
            } else {
                fprintf(f, "=== NON-AV FAULT code=0x%08X (almost always THE FATAL crash — e.g. 0x80000003 = D2 assert) ===\n", excCode);
                fprintf(f, "Crash at: 0x%08X (%s + 0x%X)\n", crashAddr, modLeaf, modOffset);
            }
            fprintf(f, "Module base: 0x%08X\n", modBase);
            fprintf(f, "Context: char='%s' g_cachedPGame=0x%08X gpGame=0x%08X clientPlayer=%p teardown=%s\n",
                    g_charName[0] ? g_charName : "?", cg, gp, cp, (cp == NULL) ? "YES" : "no");
            fprintf(f, "EAX=%08X EBX=%08X ECX=%08X EDX=%08X\n",
                ep->ContextRecord->Eax, ep->ContextRecord->Ebx,
                ep->ContextRecord->Ecx, ep->ContextRecord->Edx);
            fprintf(f, "ESI=%08X EDI=%08X EBP=%08X ESP=%08X\n",
                ep->ContextRecord->Esi, ep->ContextRecord->Edi,
                ep->ContextRecord->Ebp, ep->ContextRecord->Esp);
            fprintf(f, "EIP=%08X\n", ep->ContextRecord->Eip);

            /* Stack trace — dump top 16 dwords from stack */
            fprintf(f, "Stack:\n");
            DWORD* stack = (DWORD*)ep->ContextRecord->Esp;
            ModSpan mods[128];
            int nMods = SnapshotModules(mods, 128);
            for (int i = 0; i < 16; i++) {
                __try {
                    DWORD v = stack[i];
                    const ModSpan* ms = FindModule(mods, nMods, v);
                    if (ms)
                        fprintf(f, "  [ESP+%02X] = %08X   %s + 0x%X\n",
                                i*4, v, ms->leaf, v - ms->base);
                    else
                        fprintf(f, "  [ESP+%02X] = %08X\n", i*4, v);
                }
                __except(1) { break; }
            }
            fprintf(f, "===\n\n");
            fclose(f);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* FATAL handler — the one that means the process is going down. */
typedef BOOL (WINAPI *MiniDumpWriteDump_t)(HANDLE, DWORD, HANDLE, int,
                                           void*, void*, void*);

static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFatalFilter = NULL;

static void FatalCrashPath(char* out, int cap, const char* leaf) {
    GetModuleFileNameA(NULL, out, cap);
    char* sl = strrchr(out, '\\');
    if (sl && (int)(cap - (sl - out) - 1) > (int)strlen(leaf))
        strcpy(sl + 1, leaf);
}

static LONG WINAPI FatalHandler(EXCEPTION_POINTERS* ep) {
    static LONG s_once = 0;
    if (InterlockedExchange(&s_once, 1) == 0) {
        SYSTEMTIME st; GetLocalTime(&st);
        char dumpName[64];
        _snprintf(dumpName, sizeof(dumpName), "d2arch_crash_%04d%02d%02d_%02d%02d%02d.dmp",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        dumpName[sizeof(dumpName) - 1] = 0;

        char dumpPath[MAX_PATH];
        FatalCrashPath(dumpPath, MAX_PATH, dumpName);

        BOOL wrote = FALSE;
        /* Load by name at crash time — a machine without dbghelp should lose the dump, not fail to start the game. */
        HMODULE hDbg = LoadLibraryA("dbghelp.dll");
        if (hDbg) {
            MiniDumpWriteDump_t fnDump =
                (MiniDumpWriteDump_t)GetProcAddress(hDbg, "MiniDumpWriteDump");
            if (fnDump) {
                HANDLE hFile = CreateFileA(dumpPath, GENERIC_WRITE, 0, NULL,
                                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    /* Layout matches MINIDUMP_EXCEPTION_INFORMATION without pulling in dbghelp.h: {DWORD ThreadId; PEXCEPTION_POINTERS ExceptionPointers; BOOL ClientPointers;} */
                    struct { DWORD tid; EXCEPTION_POINTERS* ep; BOOL client; } mei;
                    mei.tid = GetCurrentThreadId();
                    mei.ep = ep;
                    mei.client = FALSE;
                    /* MiniDumpWithIndirectlyReferencedMemory (0x40) | MiniDumpScanMemory (0x10) | MiniDumpWithDataSegs (0x01): enough to walk the stack and read our own globals, without dumping the whole address space. */
                    wrote = fnDump(GetCurrentProcess(), GetCurrentProcessId(),
                                   hFile, 0x40 | 0x10 | 0x01, &mei, NULL, NULL);
                    CloseHandle(hFile);
                }
            }
        }

        char crashPath[MAX_PATH];
        FatalCrashPath(crashPath, MAX_PATH, "d2arch_crash.txt");
        FILE* cf = fopen(crashPath, "a");
        if (cf) {
            extern char g_dbgCrumb[64];
            fprintf(cf, "\n########## FATAL CRASH %04d-%02d-%02d %02d:%02d:%02d ##########\n",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            fprintf(cf, "# build=%ls  char='%s'  last-breadcrumb='%s'\n",
                    D2ARCH_VERSION_DISPLAY_W,
                    g_charName[0] ? g_charName : "?", g_dbgCrumb);
            if (ep && ep->ExceptionRecord) {
                fprintf(cf, "# code=0x%08X  address=0x%08X\n",
                        (unsigned)ep->ExceptionRecord->ExceptionCode,
                        (unsigned)(DWORD)ep->ExceptionRecord->ExceptionAddress);
            }
            fprintf(cf, "# minidump: %s\n", wrote ? dumpName : "NOT WRITTEN (dbghelp unavailable)");
            fprintf(cf, "# Everything above this line may be harmless first-chance\n"
                        "# noise. THIS is the exception that ended the process.\n");
            fprintf(cf, "###############################################################\n");
            fclose(cf);
        }
    }
    /* Let the previous filter (and then Windows) do whatever it would have. */
    if (g_prevFatalFilter) return g_prevFatalFilter(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    (void)hInst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        AddVectoredExceptionHandler(1, CrashHandler);
        /* Fatal-only filter: writes a minidump with a real call stack. */
        g_prevFatalFilter = SetUnhandledExceptionFilter(FatalHandler);
        /* Early log to verify DLL loads at all */
        {
            char earlyLog[MAX_PATH];
            GetModuleFileNameA(NULL, earlyLog, MAX_PATH);
            char* sl = strrchr(earlyLog, '\\');
            if (sl) {
                int remaining = MAX_PATH - (int)(sl - earlyLog) - 1;
                if (remaining > 17) strcpy(sl + 1, "d2arch_early.txt");
            }
            FILE* ef = fopen(earlyLog, "w");
            if (ef) {
                fprintf(ef, "D2Archipelago.dll LOADED via DllMain\n");
                fprintf(ef, "D2Client.dll = %p\n", GetModuleHandleA("D2Client.dll"));
                fclose(ef);
            }
        }

        /* stamp the build version as the GUARANTEED FIRST LINE of the MAIN log (d2arch_log.txt) at DLL load, so we can ALWAYS verify which build a tester is running straight from their log — even if later init never reaches its own banner, or they only paste the top. */
        {
            char vlog[MAX_PATH];
            GetModuleFileNameA(NULL, vlog, MAX_PATH);
            char* vs = strrchr(vlog, '\\');
            if (vs) {
                int rem = MAX_PATH - (int)(vs - vlog) - 1;
                if (rem > 15) strcpy(vs + 1, "d2arch_log.txt");
                FILE* vf = fopen(vlog, "w");
                if (vf) {
                    SYSTEMTIME st; GetLocalTime(&st);
                    fprintf(vf, "================================================\n");
                    fprintf(vf, "  D2Archipelago %ls\n", D2ARCH_VERSION_DISPLAY_W);
                    fprintf(vf, "  session start %04d-%02d-%02d %02d:%02d:%02d\n",
                            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                    fprintf(vf, "================================================\n");
                    fclose(vf);
                }
            }
        }
        DisableThreadLibraryCalls(hInst);
        CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
    }
    if (reason == DLL_PROCESS_DETACH) {
        D2Arch_OnUnload();
    }
    return TRUE;
}
