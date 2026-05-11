
/* ================================================================
 * SAFE DRAW WRAPPER
 * ================================================================ */
static void SafeDraw(void) {
    /* After reinvest completes, reload skill levels from files immediately.
     * Can't use g_skillPanelReset because it only triggers inside the panel render.
     * Instead, read the files directly into the static s_dynLevel array via a global pointer. */
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

/* ================================================================
 * WNDPROC HOOK - Handle keyboard + mouse
 * ================================================================ */
static LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    /* Process deferred quest completions (file I/O safe here, outside game tick) */
    ProcessDeferredQuests();

    /* 1.8.5 FIX — poll AP status here. WndProc fires on every Windows
     * message (paint, mouse-move, etc.) at high frequency on BOTH the
     * title screen and in-game, so this is the most reliable place to
     * keep `g_apConnected` and the Connect-button cellfile in sync.
     *
     * Why not the title-screen render hook? `RenderAPPanelD2` is wired
     * into D2GFX_EndScene but the install code for that hook is missing
     * (see d2arch_input.c:415-419 for the trampoline state — only the
     * uninstall code at d2arch_main.c:791-796 references it). Until that
     * hook is properly installed, RenderAPPanelD2 never runs, so
     * polling from there is a no-op.
     *
     * PollAPStatus internally throttles to 2-second granularity, so the
     * file read still happens at most a few times per second even when
     * Windows messages are flooding in. */
    PollAPStatus();

    /* Block WM_ACTIVATEAPP losing focus — prevents D2 from minimizing in windowed mode.
     * D2's handler calls WINDOW_SetPaused(1) → ShowWindow(SW_MINIMIZE). We eat it. */
    if (msg == WM_ACTIVATEAPP && wp == 0) {
        return 0;
    }

    /* Detect player gone while in menu/char select.
     * WndProc keeps running even when DrawGameUI doesn't.
     * This is where we catch the exit-to-menu transition. */
    if (g_lastPlayerPtr != NULL) {
        void* p = NULL;
        __try { p = fnGetPlayer ? fnGetPlayer() : NULL; } __except(EXCEPTION_EXECUTE_HANDLER) {
            /* Match the codebase convention — existing handlers here swallow
             * silently. Fix #13's logging requirement scopes to d2arch_save.c,
             * which has zero __except blocks. */
        }
        if (!p) {
            /* 1.9.0: fine-grained logging so we can pinpoint which step
             * a crash-on-exit happens in (the previous build crashed
             * mid-shutdown with no exit-handler trace in the log). Each
             * Log() fflushes immediately, so the log records the LAST
             * successful step before any crash. */
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
                StkSaveAP(g_charName);                /* 1.9.0 — STK_AP */
                {
                    extern void Coll_SaveForCharacter(const char* charName);
                    Log("WndProc EXIT[9c]: Coll_SaveForCharacter...\n");
                    Coll_SaveForCharacter(g_charName); /* 1.9.0 — Collection flags */
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
            StkSaveShared();                          /* 1.9.0 — STK_SH */
            {
                extern void Stats_SaveLifetime(void);
                Log("WndProc EXIT[10c]: Stats_SaveLifetime...\n");
                Stats_SaveLifetime();                 /* 1.9.0 — account-wide stats */
            }
            Log("WndProc EXIT[11]: StashSwapReset...\n");
            StashSwapReset();
            Log("WndProc EXIT[12]: StashSerResetMemory...\n");
            StashSerResetMemory();
            Log("WndProc EXIT[12b]: StkResetOnPlayerGone...\n");
            StkResetOnPlayerGone();   /* 1.9.0 — clear STK_AP + cel cache */
            {
                extern void Coll_ResetOnPlayerGone(void);
                extern void Coll_ResetTickState(void);
                Log("WndProc EXIT[12c]: Coll_ResetOnPlayerGone...\n");
                Coll_ResetOnPlayerGone();   /* 1.9.0 — clear collection flags in mem */
                Coll_ResetTickState();      /* 1.9.0 — reset gold delta + scan throttle */
            }
            Log("WndProc EXIT[13]: ResetD2SFile...\n");
            ResetD2SFile(g_charName);
            Log("WndProc EXIT[14]: SaveStateFile (post-reset)...\n");
            SaveStateFile();
            Log("WndProc EXIT[15]: cleanup complete\n");

            g_lastPlayerPtr = NULL;

            /* Full per-char global reset so a character switch can't leak the
             * previous character's state into the next load. Everything here
             * is safe to touch from WndProc thread. */
            g_charName[0] = 0;
            g_poolInitialized = FALSE;
            g_poolCount = 0;
            /* 1.8.0: clear the frozen-settings flag so the NEXT character
             * load can re-initialise settings from title screen / AP fresh. */
            g_settingsFrozen = FALSE;
            g_reinvestLevelsReady = FALSE;
            memset(g_reinvestLevels, 0, sizeof(g_reinvestLevels));
            g_reinvestDone = FALSE;
            g_reinvestPending = FALSE;
            g_reinvestCount = 0;
            g_shuffleApplied = FALSE;
            g_bossShuffleApplied = FALSE;
            /* g_cachedPGame intentionally NOT cleared — it's per-process, not
             * per-character, and the gameloop re-resolves it when the next
             * player appears. Zeroing it would force a re-resolve that can
             * briefly drop reinvest opportunities. */
        }
    }

    /* Keyboard shortcuts */
    if (msg == WM_KEYDOWN) {
        /* 1.8.0 — Stash overlay previously toggled via F8 (user feedback:
         * "nothing should be on F8"). Removed the hotkey. The stash UI will
         * need to hook D2's actual stash-open event (triggered by clicking
         * the stash chest in town) before it's user-visible. TODO for
         * future session: find D2Client's stash-panel open callback. */
        /* Ctrl+V = toggle cheat menu — ONLY in-game.
         * 1.9.3 fix: pressing Ctrl+V in the main menu used to open an
         * invisible cheat menu that captured input (player couldn't
         * click anything). Per Thedragon005 bug report. Now we gate on
         * Player() != NULL so the hotkey is a no-op outside an active
         * game session. */
        if (wp == 'V' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            if (Player()) {
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
        /* (Shift+L diagnostic dump and Shift+R rift dev-warp removed
         *  with the abandoned runtime-extension rift attempt. The
         *  replacement Maps system uses cube recipes for entry, not
         *  hotkeys.) */
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
        if ((BYTE)wp == g_keySkillEditor) { g_editorOpen = !g_editorOpen; return 0; }
        if ((BYTE)wp == g_keyQuestLog) { g_questLogOpen = !g_questLogOpen; g_menuOpen = FALSE; return 0; }
        if ((BYTE)wp == g_keyTracker) { g_trackerOn = !g_trackerOn; return 0; }
        /* Zone Tracker — F4 now opens the F1 book at the Zones page
         * (page 10). Folds the previous standalone modal into the book.
         * If the book is already open and on Zones, F4 closes it. */
        if ((BYTE)wp == g_keyZoneMap && g_zoneLockingOn) {
            if (g_editorOpen && g_editorPage == 10) {
                g_editorOpen = FALSE;
            } else {
                g_editorOpen = TRUE;
                g_editorPage = 10;   /* PAGE_ZONES */
            }
            return 0;
        }
        /* Skill tree is now handled by vanilla's panel system via JMP hook.
         * No separate T key toggle needed — vanilla's hotkey opens the panel. */
        if (wp == VK_ESCAPE) {
            if (g_zoneTrackerOpen) { g_zoneTrackerOpen = FALSE; return 0; }
            if (g_questLogOpen) { g_questLogOpen = FALSE; return 0; }
            if (g_editorOpen) { g_editorOpen = FALSE; g_apPageFocus = -1; return 0; }
            if (g_menuOpen) { g_menuOpen = FALSE; return 0; }
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
                /* 1.9.0 — Stats / Logbook page. Forward wheel deltas
                 * directly to the stats module so each page side
                 * scrolls independently based on cursor X. */
                extern int  MouseX(void);
                extern void Stats_HandleScroll(int side, int delta);
                int side = (MouseX() < 350) ? 0 : 1;
                int rows = (delta / 120) * 3;
                if (rows == 0) rows = (delta > 0 ? 3 : -3);
                Stats_HandleScroll(side, rows);
            } else if (g_editorPage == 10) {
                /* 1.9.0 — Zones page (folded F4). Reuses the same
                 * g_zoneTrackerScroll variable as the F4 modal. */
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
        /* 1.9.1 — Loot tab in the Ctrl+V dev menu uses scrollable lists
         * (Sets, Uniques). Accumulate raw delta into a global counter
         * the menu's render loop drains each frame. */
        if (g_cheatMenuOpen) {
            extern int g_cheatMenuWheelDelta;
            g_cheatMenuWheelDelta += delta;
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
    /* 1.9.0 Phase 5.1 — F1 Collection search-box keyboard input.
     * Active only when the F1 book is open AND on a Collection page
     * (3..7) AND the search box has keyboard focus (set by clicking
     * the box). Captures printable chars + Backspace + Escape; lets
     * everything else fall through to D2's normal handlers. */
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
        /* 1.9.0 — gate the legacy AP-panel click handler on the same
         * title-screen-visible check that the keyboard handler at line ~234
         * already uses. Without this guard, the legacy panel's click
         * rectangle (px=300 py=300, button at ~320,366..490,386 in the
         * default INI layout) silently intercepts in-game mouse clicks at
         * those coordinates and fires Connect — which the user hit while
         * placing items in the stash and got their character flipped onto
         * AP settings. The legacy panel is never rendered in-game (its
         * RenderAPPanelD2 only draws the small status text on menu
         * screens), so its click handler must not run there either.
         *
         * `g_lastDrawGameUITime` is updated every in-game frame; if it's
         * fresh (<500ms) we are in-game and the legacy panel is hidden. */
        {
            DWORD _click_now = GetTickCount();
            BOOL apPanelVisible = !g_lastDrawGameUITime ||
                                  (_click_now - g_lastDrawGameUITime) >= 500;
            if (apPanelVisible) HandleAPPanelClick(gmx, gmy);
        }

        /* 1.8.0 — Route clicks through the stash overlay. The handler
         * internally checks StashUI_IsStashOpen() (screen_shift based)
         * and returns TRUE only when it actually hit one of our tab
         * buttons. Clicks outside our buttons fall through to D2 so
         * the native stash still works normally. */
        if (StashUIHandleClick(gmx, gmy)) {
            return 0;
        }
    }
    /* Shift+right-click → quick-stash. Always dump mouse coords on
     * any right-click for calibration purposes (click corners of
     * inventory, see where coords land). */
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
        Log("RCLICK | shift=%d | MouseX/Y=(%d,%d) | pMouseX/Y=(%d,%d) "
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

    /* Vanilla D2 key remapping: translate user's custom key to D2's expected key */
    if ((msg == WM_KEYDOWN || msg == WM_KEYUP) && g_keyRemapCount > 0) {
        for (int kr = 0; kr < g_keyRemapCount; kr++) {
            if ((BYTE)wp == g_keyRemaps[kr].userKey) {
                wp = (WPARAM)g_keyRemaps[kr].d2Key;
                break;
            }
        }
    }

    return CallWindowProcA(g_origWndProc, hwnd, msg, wp, lp);
}

/* ================================================================
 * INSTALL WNDPROC HOOK
 * ================================================================ */
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
}

/* ================================================================
 * INIT API - Resolve all function pointers
 * ================================================================ */
static void InitAPI(void) {
    hD2Client = GetModuleHandleA("D2Client.dll");
    if (!hD2Client) hD2Client = GetModuleHandleA("D2Client.dll");
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

    /* D2Win control functions for title screen button removal */
    fnControlDestroy = (ControlDestroy_t)GetProcAddress(hD2Win, (LPCSTR)10018);

    /* gpControlList offset depends on D2Win version:
     * Vanilla 1.10f (base 0x6F8A0000): offset 0x5E24C
     * PD2's D2Win   (base 0x6F8E0000): offset 0xC9E4C
     * Auto-detect based on ImageBase */
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

    /* D2Game PLRTRADE_AddGold — proper gold add with validation + client sync.
     * Works because D2Game.dll runs in same process in single-player D2MOO. */
    if (hD2Game) {
        /* PLRTRADE_AddGold: D2Game base + 0x62890
         * Known address 0x6FC92890, D2Game base 0x6FC30000, offset = 0x62890 */
        fnAddGold = (AddGold_t)((DWORD)hD2Game + 0x62890);
        /* SpawnSuperUnique: address 0x6FC6F690, base 0x6FC30000, offset = 0x3F690
         * (D2Debugger confirms: 0x6FC6F690 - 0x6FC30000 = 0x3F690)
         * SpawnMonster: 0x6FC69F10 - 0x6FC30000 = 0x39F10 */
        fnSpawnSuperUnique = (SpawnSuperUnique_t)((DWORD)hD2Game + 0x3F690);
        fnSpawnMonster = (SpawnMonster_t)((DWORD)hD2Game + 0x39F10);
        /* Read SuperUnique count from sgptDataTables + 0xADC.
         *
         * 1.8.4 FIX: ordinal 10042 in D2Common is the ADDRESS of the
         * sgptDataTables global pointer — it is NOT a function. The old
         * code cast it to a function pointer and called it, which jumped
         * execution into the data bytes (interpreted as x86 instructions)
         * and could crash with ACCESS_VIOLATION before SEH unwound. The
         * correct pattern (already used in d2arch_shuffle.c) is to treat
         * the ordinal as a pointer and dereference it. */
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
            /* 1.8.4 — Skip if already patched. If we read our own E9 JMP as
             * the "original prologue", the trampoline becomes a JMP-to-self
             * loop → stack overflow → EIP=0 crash. Also skip on int 3 (CC)
             * or NUL bytes (00) which indicate corrupt/unmapped memory. */
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

        /* Hook D2Debugger's D2DebugGame export to capture pGame.
         * D2DebugGame is called every game tick by D2Debugger with valid pGame.
         * C++ mangled name: ?D2DebugGame@@YA_NPAUD2GameStrc@@@Z
         * Signature: bool __cdecl D2DebugGame(D2GameStrc* pGame) */
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

                    /* 1.8.4 — Guard against duplicate hook install + invalid
                     * prologue. Reading our own E9 as "original" makes the
                     * trampoline a JMP-to-self loop. Also abort cleanly if
                     * VirtualProtect fails instead of letting the memcpy fire. */
                    if (InterlockedExchange(&s_dbgGameHooked, 1) != 0) {
                        Log("  D2DebugGame: already hooked — skipping duplicate install\n");
                    } else if (firstByte == 0xE9 || firstByte == 0xCC || firstByte == 0x00) {
                        Log("  D2DebugGame: skipping hook install — prologue is %02X (already hooked or invalid)\n", firstByte);
                    } else {
                        /* From log: 55 8B EC 51 6A 00 6A 00 68 38 56 DA 53
                         * push ebp(1) mov ebp,esp(3) push ecx(4) push 0(6) push 0(8)
                         * Next instruction: push IMM32 (5 bytes, starts at byte 8)
                         * So instruction boundary is at exactly 8 bytes. */
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

    /* D2Net CLIENT_Send — ordinal 10005, sends client→server packets */
    if (hD2Net)
        fnClientSend = (ClientSend_t)GetProcAddress(hD2Net, (LPCSTR)10005);
    Log("  fnClientSend=%p (D2Net ordinal 10005)\n", fnClientSend);

    /* Quest state check — D2Common ordinal 11107 */
    if (hD2Common)
        fnGetQuestState = (QuestRecordGetState_t)GetProcAddress(hD2Common, (LPCSTR)11107);
    Log("  fnGetQuestState=%p (D2Common ordinal 11107)\n", fnGetQuestState);

    /* Quest flags: read from pGame->pQuestControl->pQuestFlags (server-side).
     * pQuestControl at pGame+0x10F4, pQuestFlags at pQuestControl+0x0C.
     * No hooks needed — just poll the server's global quest state via fnGetQuestState. */
    Log("  Quest detection: polling pGame+0x10F4->+0x0C (server quest flags)\n");

    /* Waypoint check — D2Common ordinal 11146
     * IMPORTANT: We need the function from the ORIGINAL D2Common, not the D2MOO patch.
     * D2.Detours loads both: original at game address, patch at different address.
     * GetModuleHandle returns the PATCH version. We need to find the original. */
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

    /* SendGamePacket function */
    g_d2clientBase = (DWORD)hD2Client;
    g_sendPacketAddr = (DWORD)hD2Client + 0x143E0;

    /* D2Common skill functions (by ordinal) */
    fnGetRightSkill = (GetUnitSkill_t)GetProcAddress(hD2Common, (LPCSTR)10322);
    fnGetLeftSkill  = (GetUnitSkill_t)GetProcAddress(hD2Common, (LPCSTR)10321);

    /* ARCHIPELAGO: NOP the 4 CALL instructions in the draw loop that render
     * vanilla's skill tree content. This is RUNTIME ONLY (in-memory patches,
     * the D2Client.dll FILE is never modified).
     *
     * The draw loop (FUN_6fb21b70) calls these when DAT_6fbba72c != 0:
     *   0x81FED: CALL FUN_6fad81f0 (load borders/bullets)
     *   0x81FF2: CALL FUN_6fad82e0 (background overlay)
     *   0x820CC: CALL FUN_6fad8310 (main content: icons, tabs, text)
     * And under DAT_6fbba730:
     *   0x82023: CALL FUN_6fada350 (help/tooltip panel)
     *
     * Panel open/close state, hotkeys, viewport shift, and click handling
     * all remain functional. Only the RENDERING is suppressed.
     * We draw our own content from DrawAll (DrawGameUI hook). */
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
        /* 1.9.2 fix: cast must include __fastcall so call sites use
         * the right convention (CallGetUIVar trampoline reads ECX). */
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
    Log("  sendPacket=%p getRightSkill=%p getLeftSkill=%p\n",
        (void*)g_sendPacketAddr, fnGetRightSkill, fnGetLeftSkill);
    Log("  screenW=%d screenH=%d\n", g_screenW, g_screenH);
}

/* ================================================================
 * D2.DETOURS ENTRY POINTS
 * These are called by D2.Detours automatically
 * ================================================================ */

/* Called when D2Client.dll is loaded - we initialize here */
extern "C" __declspec(dllexport) void __cdecl D2Arch_Init(void) {
    /* 1.8.4 — Process-level one-shot guard via named mutex (NOT a static
     * local). The DLL is loaded twice in the same process (launcher path +
     * D2.Detours patch path), so each image has its own statics. A named
     * kernel object is shared across both images. */
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
            /* 1.9.0 — launcher path sets g_apPolling so PollAPStatus runs as
             * soon as the bridge starts, but g_apMode stays FALSE until the
             * bridge actually authenticates. That way a character created
             * before auth completes (or if auth never happens) does not bake
             * AP settings. The same rule that applies to the title-screen
             * Connect button now applies here. */
            extern BOOL g_apPolling;
            g_apPolling = TRUE;
            Log("AP MODE: enabled (from launcher) — polling on, g_apMode deferred to auth\n");
        } else {
            /* 1.8.2 BUGFIX — In standalone launches, scrub any leftover
             * ap_settings.dat from a prior AP session (a clean disconnect
             * deletes it via PollAPStatus, but alt-F4 / crashes leave it
             * behind, after which it would leak into new offline characters
             * via LoadAPSettings()). The bridge will rewrite the file as
             * soon as a new AP connection authenticates, so this is safe. */
            char dir[MAX_PATH], path[MAX_PATH];
            GetArchDir(dir, MAX_PATH);
            sprintf(path, "%sap_settings.dat", dir);
            if (DeleteFileA(path)) {
                Log("AP: scrubbed stale ap_settings.dat at startup (standalone launch)\n");
            }
        }
    }

    InitAPI();

    /* 1.8.0 — Initialize new subsystems */
    ItemLogInit();
    /* 1.8.0 cleanup: CustomSU_Init() call extracted */
    StashInit();
    StashLoadShared();   /* Shared stash persists across characters */
    StashLogicInit();    /* 1.8.0: stacking whitelist + insertion scanner */
    StashUIInit();       /* 1.8.0: tab bar + click handling overlay */
    VersionPatchApply(); /* Overwrite D2's "v %d.%02d" format strings with "Beta 1.8.0" */
    CelHookInstall();    /* 1.8.0 diagnostic — log every cellfile load path */
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

    /* 1.8.0: run orphan-save cleanup at DLL load too, not only from
     * OnCharacterLoad. OnCharacterLoad only fires when the user enters a
     * character — if every character has just been deleted, no load event
     * ever runs and the orphan files sit forever. Running here catches
     * the "launch → delete → exit" flow that left ghjghj files behind. */
    CleanupOrphanedSaves();

    Log("Init complete, waiting for player...\n");

    /* Install WndProc hook early so we catch title screen events too */
    InstallWndProcHook();

    /* Schedule title screen button cleanup.
     * D2Arch_Init runs when D2Client loads, BEFORE the title screen appears.
     * We use SetTimer to delay cleanup until the title screen controls exist.
     * The timer fires on the main thread's message loop, safe for D2Win calls. */
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

    SafeDraw();
}

/* Called when the game shuts down */
extern "C" __declspec(dllexport) void __cdecl D2Arch_OnUnload(void) {
    Log("Unloading D2Archipelago...\n");

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

/* ================================================================
 * UI HOOK - called instead of original DrawGameUI
 * Calls original (trampoline) then our overlay
 * ================================================================ */
static DWORD g_trampolinePtr = 0; /* set after trampoline is built */

/* ================================================================
 * SetQuestState hook — intercept D2Game setting quest flags
 * ================================================================ */
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

/* ================================================================
 * MAIN THREAD - waits for D2Client then hooks DrawGameUI
 * ================================================================ */
static DWORD WINAPI MainThread(LPVOID param) {
    (void)param;

    /* 1.8.4 — Process-level one-shot guard via named mutex.
     *
     * Why named mutex (not static volatile LONG): D2Archipelago.dll gets
     * loaded TWO TIMES in the same Game.exe process — once from
     * Game\D2Archipelago.dll (launcher injection) and once from
     * Game\patch\D2Archipelago.dll (D2.Detours patch path). Each DLL image
     * has its own copy of any `static` variable, so a static-local guard is
     * useless: each copy thinks it's the first. A named kernel object lives
     * in the per-process namespace and IS shared across DLL images, so
     * whichever MainThread races to CreateMutexA first wins, and the loser
     * sees ERROR_ALREADY_EXISTS and bails. PID is in the name so two game
     * processes (e.g. dual-install play) don't block each other. */
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
    /* hMtx leaks intentionally — kept alive for process lifetime so a
     * crash/restart of MainThread doesn't fail the guard. */

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

            /* 1.8.0 — install cel hook EARLY, before main menu renders, so we
             * can see splash / titlescreen / background DC6 load paths. */
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

        /* 1.8.0 — Patch D2Launch version string NOW, before main menu first renders.
         * D2Arch_Init() below only runs when D2Client.dll loads (after Single Player
         * click), which is too late for the initial main menu view. Calling the patch
         * here catches D2Launch.dll while it's loaded but D2Client may not be yet.
         * The patch is idempotent — it will also run again from D2Arch_Init. */
        VersionPatchApply();

        __try {
            /* Load toggle button cellfiles (130x35 pixels) + wide variants
             * (260x35) for "Skill Hunting" / "Zone Locking" game-mode
             * toggles that need more room for their longer labels. */
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

                /* 1.8.4 — Guard against duplicate hook install. Two MainThread
                 * invocations would otherwise overlay JMPs on top of JMPs:
                 * the second trampoline would copy our own E9-jmp as if it
                 * were the original prologue, producing a JMP-to-self loop
                 * → stack overflow → EIP=0 access violation. */
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
                        /* 1.8.5 — if AP is still authenticated when the
                         * player returns from char-select / in-game to the
                         * title menu, immediately overlay the slot_data
                         * values onto the freshly-created buttons. Without
                         * this, the buttons would briefly display the
                         * stale d2arch.ini values until the user clicked
                         * Single Player again or PollAPStatus' next
                         * transition fired. */
                        if (g_apConnected) SyncAPToTitleSettings();
                    }

                    wasOnMenu = onMenu;
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    }

    /* TITLE SCREEN CLEANUP: DISABLED - buttons are removed by patching D2Launch.dll Y coordinates
     * instead of runtime cleanup. The cleanup code was removing OTHER MULTIPLAYER and EXIT by mistake. */
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
                                /* Look for pointers that point to heap/stack (non-zero,
                                   in reasonable address range, and whose target starts with
                                   a small int that could be nType) */
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

/* ================================================================
 * DLL ENTRY POINT
 * ================================================================ */
/* Crash handler — logs exact crash address and module */
static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        DWORD crashAddr = (DWORD)ep->ExceptionRecord->ExceptionAddress;
        DWORD accessAddr = (DWORD)ep->ExceptionRecord->ExceptionInformation[1];
        int wasWrite = (int)ep->ExceptionRecord->ExceptionInformation[0];

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

        /* Log to file */
        char logPath[MAX_PATH];
        GetModuleFileNameA(NULL, logPath, MAX_PATH);
        char* sl = strrchr(logPath, '\\');
        if (sl) strcpy(sl + 1, "d2arch_crash.txt");

        FILE* f = fopen(logPath, "a");
        if (f) {
            fprintf(f, "=== ACCESS VIOLATION ===\n");
            fprintf(f, "Crash at: 0x%08X (%s + 0x%X)\n", crashAddr, modName, modOffset);
            fprintf(f, "Tried to %s address: 0x%08X\n", wasWrite ? "WRITE" : "READ", accessAddr);
            fprintf(f, "Module base: 0x%08X\n", modBase);
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
            for (int i = 0; i < 16; i++) {
                __try { fprintf(f, "  [ESP+%02X] = %08X\n", i*4, stack[i]); }
                __except(1) { break; }
            }
            fprintf(f, "===\n\n");
            fclose(f);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    (void)hInst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        AddVectoredExceptionHandler(1, CrashHandler);
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
        DisableThreadLibraryCalls(hInst);
        CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
    }
    if (reason == DLL_PROCESS_DETACH) {
        D2Arch_OnUnload();
    }
    return TRUE;
}
