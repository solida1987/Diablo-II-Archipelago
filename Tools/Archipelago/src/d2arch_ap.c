
/* ================================================================
 * MAIN DRAW FUNCTION
 * ================================================================ */
/* ================================================================
 * AP PANEL — drawn from EndScene hook in menu screens
 * ================================================================ */
/* ================================================================
 * ARCHIPELAGO IPC — File-based communication with ap_bridge.exe
 * ================================================================ */

/* Start AP bridge process if not already running */
static PROCESS_INFORMATION g_bridgePI = {0};
static BOOL g_bridgeStarted = FALSE;

/* 1.7.1: additional slot_data fields from APworld that the DLL now acknowledges.
 * Shop shuffle and treasure cows are applied by d2arch_shuffle.c when it is
 * eventually updated; for now we parse + log so the launcher's
 * "Settings received from server" status is honest. */
/* g_shopShuffleEnabled moved to d2arch_shuffle.c so earlier files can see it */
/* 1.8.0 cleanup: g_treasureCowsEnabled extracted to
 * Tools/Archipelago/pending_reimplementation/TREASURE_COWS/ */

/* 1.7.1: persistent applied-AP-id set for filler dedup across reconnects.
 * AP sends all items on reconnect (items_handling 0b111). Without dedup, each
 * reconnect re-grants gold/stat/skill-point/trap/loot. We persist to a per-
 * character file so dedup survives character switches too. */
#define G_APPLIED_AP_CAP 4096
static int  g_appliedApIds[G_APPLIED_AP_CAP];
static int  g_appliedApCount = 0;
static BOOL g_appliedApLoaded = FALSE;

static void GetAppliedApFilePath(char* path, int pathSize) {
    char dir[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    _snprintf(path, pathSize, "%sd2arch_applied_%s.dat", dir, g_charName);
    path[pathSize - 1] = 0;
}

static void LoadAppliedApIds(void) {
    if (g_appliedApLoaded) return;
    g_appliedApLoaded = TRUE;
    g_appliedApCount = 0;
    if (!g_charName[0]) return;
    char path[MAX_PATH];
    GetAppliedApFilePath(path, MAX_PATH);
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[64];
    while (fgets(line, sizeof(line), f) && g_appliedApCount < G_APPLIED_AP_CAP) {
        int id = 0;
        if (sscanf(line, "applied_ap_id=%d", &id) == 1 && id > 0) {
            g_appliedApIds[g_appliedApCount++] = id;
        }
    }
    fclose(f);
    Log("AP DEDUP: loaded %d applied ap ids from %s\n", g_appliedApCount, path);
}

static BOOL IsApIdApplied(int apId) {
    if (!g_appliedApLoaded) LoadAppliedApIds();
    for (int i = 0; i < g_appliedApCount; i++) {
        if (g_appliedApIds[i] == apId) return TRUE;
    }
    return FALSE;
}

static void MarkApIdApplied(int apId) {
    if (!g_appliedApLoaded) LoadAppliedApIds();
    if (IsApIdApplied(apId)) return;
    if (g_appliedApCount >= G_APPLIED_AP_CAP) {
        /* Buffer full — drop the oldest half to make room. */
        memmove(g_appliedApIds, g_appliedApIds + (G_APPLIED_AP_CAP / 2),
                (G_APPLIED_AP_CAP / 2) * sizeof(int));
        g_appliedApCount = G_APPLIED_AP_CAP / 2;
    }
    g_appliedApIds[g_appliedApCount++] = apId;

    /* Append to per-character file so dedup survives restarts. */
    if (!g_charName[0]) return;
    char path[MAX_PATH];
    GetAppliedApFilePath(path, MAX_PATH);
    FILE* f = fopen(path, "a");
    if (f) {
        fprintf(f, "applied_ap_id=%d\n", apId);
        fclose(f);
    }
}

static void StartAPBridge(void) {
    if (g_bridgeStarted) {
        /* Check if still running */
        if (g_bridgePI.hProcess) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(g_bridgePI.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                Log("AP Bridge: process exited (code %d), restarting...\n", exitCode);
                CloseHandle(g_bridgePI.hProcess);
                CloseHandle(g_bridgePI.hThread);
                memset(&g_bridgePI, 0, sizeof(g_bridgePI));
                g_bridgeStarted = FALSE;
            } else {
                return; /* Still running */
            }
        }
    }

    /* Find ap_bridge.exe */
    char dir[MAX_PATH], bridgePath[MAX_PATH], bridgeCmd[MAX_PATH * 2];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char* sl = strrchr(dir, '\\');
    if (sl) *(sl + 1) = 0;

    /* Try ap_bridge_dist folder first */
    sprintf(bridgePath, "%sap_bridge_dist\\ap_bridge.exe", dir);
    if (GetFileAttributesA(bridgePath) == INVALID_FILE_ATTRIBUTES) {
        sprintf(bridgePath, "%sap_bridge.exe", dir);
        if (GetFileAttributesA(bridgePath) == INVALID_FILE_ATTRIBUTES) {
            Log("AP Bridge: not found (tried ap_bridge_dist/ and root)\n");
            return;
        }
    }

    sprintf(bridgeCmd, "\"%s\" --gamedir \".\"", bridgePath);
    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; /* Hidden — debug output goes to log file */

    if (CreateProcessA(bridgePath, bridgeCmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &g_bridgePI)) {
        g_bridgeStarted = TRUE;
        Log("AP Bridge: started PID %d (%s)\n", g_bridgePI.dwProcessId, bridgePath);
    } else {
        Log("AP Bridge: CreateProcess FAILED (err=%d) path=%s\n", GetLastError(), bridgePath);
    }
}

static void StopAPBridge(void) {
    if (g_bridgeStarted && g_bridgePI.hProcess) {
        TerminateProcess(g_bridgePI.hProcess, 0);
        CloseHandle(g_bridgePI.hProcess);
        CloseHandle(g_bridgePI.hThread);
        memset(&g_bridgePI, 0, sizeof(g_bridgePI));
        g_bridgeStarted = FALSE;
        Log("AP Bridge: stopped\n");
    }
}

/* Write command file for AP bridge (DLL → Bridge) */
static void WriteAPCommand(const char* action) {
    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(path, "%sap_command.dat", dir);
    FILE* f = fopen(path, "w");
    if (!f) { Log("AP: Failed to write command file\n"); return; }
    fprintf(f, "action=%s\n", action);
    fprintf(f, "server=%s\n", g_apIP);
    fprintf(f, "slot=%s\n", g_apSlot);
    fprintf(f, "password=%s\n", g_apPassword);
    fprintf(f, "char=%s\n", g_charName);
    fprintf(f, "deathlink=%d\n", g_apDeathLink ? 1 : 0);
    fclose(f);
    Log("AP: Command '%s' written (server=%s slot=%s)\n", action, g_apIP, g_apSlot);
}

/* Save AP settings to d2arch.ini for persistence */
static void SaveAPConfig(void) {
    char iniPath[MAX_PATH];
    GetArchDir(iniPath, MAX_PATH);
    strcat(iniPath, "d2arch.ini");
    WritePrivateProfileStringA("ap", "ServerIP", g_apIP, iniPath);
    WritePrivateProfileStringA("ap", "SlotName", g_apSlot, iniPath);
    WritePrivateProfileStringA("ap", "Password", g_apPassword, iniPath);
    Log("AP: Config saved to d2arch.ini\n");
}

/* ================================================================
 * PER-CHARACTER AP CONFIG — save/load AP connection info per char
 * ================================================================ */
static void SaveAPCharConfig(void) {
    char dir[MAX_PATH], path[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_ap_%s.dat", dir, g_charName);
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "ap_mode=%d\n", g_apConnected ? 1 : 0);
    fprintf(f, "ap_server=%s\n", g_apIP);
    fprintf(f, "ap_slot=%s\n", g_apSlot);
    fprintf(f, "ap_password=%s\n", g_apPassword);
    fclose(f);
    Log("AP: Saved per-char config for '%s'\n", g_charName);
}

static BOOL LoadAPCharConfig(void) {
    char dir[MAX_PATH], path[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_ap_%s.dat", dir, g_charName);
    FILE* f = fopen(path, "r");
    if (!f) return FALSE;
    char line[256];
    BOOL apMode = FALSE;
    while (fgets(line, sizeof(line), f)) {
        char val[64];
        int n = 0;
        if (sscanf(line, "ap_mode=%d", &n) == 1) apMode = (n != 0);
        if (sscanf(line, "ap_server=%63[^\n]", val) == 1) strncpy(g_apIP, val, 63);
        if (sscanf(line, "ap_slot=%31[^\n]", val) == 1) strncpy(g_apSlot, val, 31);
        if (sscanf(line, "ap_password=%31[^\n]", val) == 1) strncpy(g_apPassword, val, 31);
    }
    fclose(f);
    Log("AP: Loaded per-char config for '%s' (mode=%d)\n", g_charName, apMode);
    return apMode;
}

/* ================================================================
 * CHARACTER ISOLATION — hide other .d2s files during AP session
 * ================================================================ */
static void IsolateAPCharacter(void) {
    char saveDir[MAX_PATH];
    GetSaveDir(saveDir, MAX_PATH);
    if (!saveDir[0]) { Log("IsolateAPCharacter: no save dir\n"); return; }

    /* Create hidden subfolder */
    char hiddenDir[MAX_PATH];
    sprintf(hiddenDir, "%s_arch_hidden\\", saveDir);
    CreateDirectoryA(hiddenDir, NULL);

    /* Move all .d2s files except current character */
    char searchPath[MAX_PATH];
    sprintf(searchPath, "%s*.d2s", saveDir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    int moved = 0;
    do {
        /* Extract character name from filename (strip .d2s) */
        char baseName[MAX_PATH];
        strncpy(baseName, fd.cFileName, MAX_PATH - 1);
        char* dot = strrchr(baseName, '.');
        if (dot) *dot = 0;

        /* Skip current character */
        if (_stricmp(baseName, g_charName) == 0) continue;

        /* Move file to hidden dir */
        char srcPath[MAX_PATH], dstPath[MAX_PATH];
        sprintf(srcPath, "%s%s", saveDir, fd.cFileName);
        sprintf(dstPath, "%s%s", hiddenDir, fd.cFileName);
        if (MoveFileA(srcPath, dstPath)) moved++;

        /* Also move companion files (.key, .ma0, .map) */
        const char* exts[] = {".key", ".ma0", ".map"};
        for (int e = 0; e < 3; e++) {
            sprintf(srcPath, "%s%s%s", saveDir, baseName, exts[e]);
            sprintf(dstPath, "%s%s%s", hiddenDir, baseName, exts[e]);
            MoveFileA(srcPath, dstPath); /* OK if fails (file may not exist) */
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    Log("IsolateAPCharacter: moved %d characters to _arch_hidden\n", moved);
}

static void RestoreAllCharacters(void) {
    char saveDir[MAX_PATH];
    GetSaveDir(saveDir, MAX_PATH);
    if (!saveDir[0]) return;

    char hiddenDir[MAX_PATH];
    sprintf(hiddenDir, "%s_arch_hidden\\", saveDir);

    /* Move everything back from hidden dir */
    char searchPath[MAX_PATH];
    sprintf(searchPath, "%s*.*", hiddenDir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    int restored = 0;
    do {
        if (fd.cFileName[0] == '.') continue; /* skip . and .. */
        char srcPath[MAX_PATH], dstPath[MAX_PATH];
        sprintf(srcPath, "%s%s", hiddenDir, fd.cFileName);
        sprintf(dstPath, "%s%s", saveDir, fd.cFileName);
        if (MoveFileA(srcPath, dstPath)) restored++;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);

    /* Try to remove the hidden directory (only succeeds if empty) */
    RemoveDirectoryA(hiddenDir);
    Log("RestoreAllCharacters: restored %d files\n", restored);
}

/* ================================================================
 * AP SETTINGS — read slot_data from bridge (ap_settings.dat)
 * (globals defined at top of file)
 * ================================================================ */

/* 1.8.5 — Sync AP-managed settings (g_*) into the title-screen UI state
 * (ts_*) and immediately re-render every toggle/dropdown button so the
 * player sees the AP server's actual values instead of their stale local
 * d2arch.ini values. Called when PollAPStatus detects authentication
 * transition.
 *
 * The bridge's slot_data overrides apply to the ts_* values used by the
 * button RENDER, not just the gameplay g_* values — this way the user
 * can SEE on the menu what the AP server has configured before clicking
 * Single Player. Click handlers are blocked when g_apConnected is TRUE
 * (see OnToggleClick / OnGoalClick / OnXPClick), so the displayed values
 * cannot drift from g_* once authenticated. */
static void SyncAPToTitleSettings(void) {
    /* Copy g_* → ts_* so the buttons render the authoritative AP values.
     *
     * Goal encoding mismatch: g_apGoal is the new 0..2 difficulty scope
     * (0=Full Norm, 1=Full NM, 2=Full Hell). ts_Goal indexes the legacy
     * 15-entry goalOptions[] table where Full-game entries live at idx 4
     * (Norm), 9 (NM), 14 (Hell). Map across so the button shows the right
     * label. If g_apGoal is somehow already in the legacy 0..14 range
     * (older slot_data shipped with a 0..14 value), pass it through. */
    if (g_apGoal == 3) {
        ts_Goal = 15;                    /* 1.9.0 Collection — display index 15 */
    } else if (g_apGoal >= 0 && g_apGoal <= 2) {
        ts_Goal = (g_apGoal * 5) + 4;   /* 0→4, 1→9, 2→14 */
    } else if (g_apGoal >= 0 && g_apGoal < GOAL_COUNT) {
        ts_Goal = g_apGoal;
    } else {
        ts_Goal = 14;                    /* fallback: Full Hell */
    }
    ts_SkillHuntingOn        = g_skillHuntingOn ? 1 : 0;
    ts_ZoneLockingOn         = g_zoneLockingOn  ? 1 : 0;
    ts_QuestStory            = g_apQuestStory   ? 1 : 0;
    ts_QuestHunting          = g_apQuestHunting ? 1 : 0;
    ts_QuestKillZones        = g_apQuestKillZones ? 1 : 0;
    ts_QuestExploration      = g_apQuestExploration ? 1 : 0;
    ts_QuestWaypoints        = g_apQuestWaypoints ? 1 : 0;
    ts_QuestLevelMilestones  = g_apQuestLevelMilestones ? 1 : 0;
    ts_MonsterShuffle        = g_monsterShuffleEnabled ? 1 : 0;
    ts_BossShuffle           = g_bossShuffleEnabled ? 1 : 0;
    ts_ShopShuffle           = g_shopShuffleEnabled ? 1 : 0;
    ts_EntranceShuffle       = g_entranceShuffleEnabled ? 1 : 0; /* 1.9.0 */
    /* TrapsEnabled is encoded indirectly: LoadAPSettings zeroes
     * g_fillerTrapPct when slot_data has traps_enabled=0. Reverse the
     * mapping so the UI button reflects the correct ON/OFF state. */
    ts_TrapsEnabled          = (g_fillerTrapPct > 0) ? 1 : 0;
    ts_ClassFilter           = g_classFilter;
    for (int i = 0; i < 7; i++) ts_ClsEnabled[i] = g_clsEnabled[i];
    ts_IPlayAssassin         = g_iPlayAssassin;
    /* XP multiplier: g_* stores 1..10, ts_* stores 0..9 idx + the literal */
    int idx = g_xpMultiplier - 1;
    if (idx < 0) idx = 0;
    if (idx >= XP_COUNT) idx = XP_COUNT - 1;
    ts_XPMultiplier_idx = idx;
    ts_XPMultiplier     = g_xpMultiplier;

    /* Re-render all toggle buttons via SetBtnColor + ButtonSetText.
     * g_titleBtnVals[i] is non-NULL for toggles only; dropdowns/AP
     * editboxes have NULL there and are skipped. */
    for (int i = 0; i < g_titleBtnCount; i++) {
        void* btn = g_titleBtns[i];
        int*  val = g_titleBtnVals[i];
        const wchar_t* lbl = g_titleBtnLabels[i];
        if (!btn || !val || !lbl) continue;
        if (fnButtonSetText) {
            wchar_t txt[64];
            _snwprintf(txt, 63, L"%s:%s", lbl, *val ? L"ON" : L"OFF");
            fnButtonSetText(btn, 1, txt, *val ? 1 : 3);
        }
        SetBtnColor(btn, *val);
    }

    /* Re-render the two dropdowns (Goal + XP). They have NULL valptr
     * so the toggle loop above skipped them. */
    if (g_btnGoal && fnButtonSetText && ts_Goal >= 0 && ts_Goal < GOAL_COUNT)
        fnButtonSetText(g_btnGoal, 1, goalOptions[ts_Goal], 0);
    if (g_btnXP && fnButtonSetText && ts_XPMultiplier_idx >= 0 && ts_XPMultiplier_idx < XP_COUNT)
        fnButtonSetText(g_btnXP, 1, xpOptions[ts_XPMultiplier_idx], 0);

    /* Class filter button affects sibling class buttons' tint; refresh. */
    UpdateClassColors();

    Log("AP: synced settings to title-screen buttons "
        "(Goal=%d XP=%dx skillHunt=%d zoneLock=%d traps=%d monShuf=%d bossShuf=%d)\n",
        ts_Goal, ts_XPMultiplier, ts_SkillHuntingOn, ts_ZoneLockingOn,
        ts_TrapsEnabled, ts_MonsterShuffle, ts_BossShuffle);
}

/* 1.8.5 — Reverse of SyncAPToTitleSettings: when AP disconnects, reload
 * the d2arch.ini values into ts_* and re-render so the buttons go back
 * to displaying the user's offline preferences. Called from PollAPStatus
 * on the AUTH→DISCONNECT transition. */
static void RestoreTitleSettingsFromINI(void) {
    /* Calling TitleSettings_Load reloads the entire ts_* set from
     * d2arch.ini. Then we re-render every button using the same loop
     * SyncAPToTitleSettings uses. */
    TitleSettings_Load();
    for (int i = 0; i < g_titleBtnCount; i++) {
        void* btn = g_titleBtns[i];
        int*  val = g_titleBtnVals[i];
        const wchar_t* lbl = g_titleBtnLabels[i];
        if (!btn || !val || !lbl) continue;
        if (fnButtonSetText) {
            wchar_t txt[64];
            _snwprintf(txt, 63, L"%s:%s", lbl, *val ? L"ON" : L"OFF");
            fnButtonSetText(btn, 1, txt, *val ? 1 : 3);
        }
        SetBtnColor(btn, *val);
    }
    if (g_btnGoal && fnButtonSetText && ts_Goal >= 0 && ts_Goal < GOAL_COUNT)
        fnButtonSetText(g_btnGoal, 1, goalOptions[ts_Goal], 0);
    if (g_btnXP && fnButtonSetText && ts_XPMultiplier_idx >= 0 && ts_XPMultiplier_idx < XP_COUNT)
        fnButtonSetText(g_btnXP, 1, xpOptions[ts_XPMultiplier_idx], 0);
    UpdateClassColors();
    Log("AP: restored title-screen buttons from d2arch.ini after disconnect\n");
}

static void LoadAPSettings(void) {
    /* 1.8.0: settings are baked into the per-character state file at
     * creation time. Once a character is loaded, any later call to
     * LoadAPSettings (e.g. AP reconnect mid-session, or the user fiddling
     * with the title-screen toggles after exiting to char select without
     * fully exiting to title) is a no-op — the character keeps the
     * settings it was created with. Flag is cleared in WndProc Player-
     * gone so next char load can re-initialise freely. */
    if (g_settingsFrozen) {
        Log("LoadAPSettings: settings frozen for character '%s' — skipping\n",
            g_charName);
        return;
    }

    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);

    /* 1.8.2 BUGFIX — Only read ap_settings.dat if AP is currently connected.
     * Without this guard, a stale ap_settings.dat from a previous AP session
     * (left behind by an alt-F4 / crash that bypassed PollAPStatus' cleanup)
     * leaks into new characters created in offline mode, overriding the
     * title-screen UI toggles. When AP is offline, the title-screen UI
     * (d2arch.ini [settings]) is the single source of truth. */
    FILE* f = NULL;
    if (g_apConnected) {
        sprintf(path, "%sap_settings.dat", dir);
        f = fopen(path, "r");
        if (!f) {
            sprintf(path, "%sstandalone_settings.dat", dir);
            f = fopen(path, "r");
        }
    } else {
        Log("LoadAPSettings: AP disconnected — using d2arch.ini [settings] as source\n");
    }
    if (!f) {
        /* Read from d2arch.ini [settings] section (written by D2Launch main menu) */
        char iniPath[MAX_PATH]; int v;
        GetArchDir(iniPath, MAX_PATH); strcat(iniPath, "d2arch.ini");
        Log("Loading settings from d2arch.ini [settings]\n");
        /* 1.8.5 — normalize Goal: 1.8.x uses 0..2 (Full Norm/NM/Hell);
         * older INIs may still carry the legacy 0..14 (act × 3 + diff)
         * encoding which makes gate logic and goal-completion comparisons
         * misfire. Apply the same modulo normalization as the slot_data
         * path so a stale "Goal=14" reads as Hell (2), not 14. */
        {
            /* 1.9.0: goal range now 0..3. 3 = Collection (F1 page tracker
             * win condition). Legacy 0..14 encoding still normalizes via
             * %3 so old INIs map onto Normal/NM/Hell. Title-screen UI
             * saves Goal=15 for Collection, which maps to internal 3. */
            int rawGoal = GetPrivateProfileIntA("settings", "Goal", 2, iniPath);
            if (rawGoal == 15)            g_apGoal = 3;   /* Title UI Collection */
            else if (rawGoal >= 0 && rawGoal <= 3) g_apGoal = rawGoal;
            else                           g_apGoal = (rawGoal % 3);
            if (g_apGoal < 0) g_apGoal = 0;
            if (g_apGoal > 3) g_apGoal = 3;

            /* 1.9.0 — Collection: standalone INI fallback. The apworld
             * slot_data path encodes per-item toggles as bitmasks, but
             * standalone-mode users have no apworld and used to fall
             * back to a single CollGoalSets/Runes/Specials INI key.
             *
             * Per user feedback: standalone mode now ALWAYS uses every
             * item as a target — no INI knobs needed. The default
             * "all on" state of the bitmasks is the correct behavior.
             * INI keys preserved as undocumented overrides for power
             * users only. */
            if (g_apGoal == 3) {
                extern uint32_t g_collGoalOverrideSetsMask;
                extern uint64_t g_collGoalOverrideRunesMask;
                extern uint16_t g_collGoalOverrideSpecialsMask;
                extern uint8_t  g_collGoalOverrideGems;
                extern BOOL     g_collGoalOverridePresent;
                /* Default: keep masks at "all on". User can override
                 * by setting CollSetsMask=N (32-bit decimal) etc. */
                int setsHi = GetPrivateProfileIntA("settings", "CollSetsMaskHi", -1, iniPath);
                int setsLo = GetPrivateProfileIntA("settings", "CollSetsMaskLo", -1, iniPath);
                if (setsLo >= 0 && setsHi >= 0) {
                    g_collGoalOverrideSetsMask =
                        (uint32_t)setsLo | ((uint32_t)setsHi << 16);
                    g_collGoalOverridePresent = TRUE;
                }
                int runesLo = GetPrivateProfileIntA("settings", "CollRunesMaskLo", -1, iniPath);
                int runesMd = GetPrivateProfileIntA("settings", "CollRunesMaskMd", -1, iniPath);
                int runesHi = GetPrivateProfileIntA("settings", "CollRunesMaskHi", -1, iniPath);
                if (runesLo >= 0 && runesMd >= 0 && runesHi >= 0) {
                    g_collGoalOverrideRunesMask =
                        (uint64_t)runesLo |
                        ((uint64_t)runesMd << 16) |
                        ((uint64_t)runesHi << 32);
                    g_collGoalOverridePresent = TRUE;
                }
                int specs = GetPrivateProfileIntA("settings", "CollSpecialsMask", -1, iniPath);
                if (specs >= 0) {
                    g_collGoalOverrideSpecialsMask = (uint16_t)specs;
                    g_collGoalOverridePresent = TRUE;
                }
                int gems = GetPrivateProfileIntA("settings", "CollGoalGems", -1, iniPath);
                if (gems >= 0) {
                    g_collGoalOverrideGems = (uint8_t)(gems != 0);
                    g_collGoalOverridePresent = TRUE;
                }
            }
        }
        /* 1.8.0: two independent game-mode toggles replace the old enum.
         * Read both; legacy "GameMode" provides fallback if user had an
         * old INI and hasn't yet seen the new toggles save their defaults. */
        {
            int legacyMode = GetPrivateProfileIntA("settings", "GameMode", -1, iniPath);
            g_skillHuntingOn = GetPrivateProfileIntA("settings", "SkillHunting",
                                                    (legacyMode == 1) ? 0 : 1,
                                                    iniPath) != 0;
            g_zoneLockingOn  = GetPrivateProfileIntA("settings", "ZoneLocking",
                                                    (legacyMode == 1) ? 1 : 0,
                                                    iniPath) != 0;
        }
        /* 1.8.0: story quests always ON (toggle removed — D2 requires them anyway) */
        g_apQuestStory = TRUE;
        g_apQuestHunting = GetPrivateProfileIntA("settings", "QuestHunting", 1, iniPath) != 0;
        g_apQuestKillZones = GetPrivateProfileIntA("settings", "QuestKillZones", 1, iniPath) != 0;
        g_apQuestExploration = GetPrivateProfileIntA("settings", "QuestExploration", 1, iniPath) != 0;
        g_apQuestWaypoints = GetPrivateProfileIntA("settings", "QuestWaypoints", 1, iniPath) != 0;
        g_apQuestLevelMilestones = GetPrivateProfileIntA("settings", "QuestLevelMilestones", 1, iniPath) != 0;
        g_apSkillPoolSize = GetPrivateProfileIntA("settings", "SkillPoolSize", 210, iniPath);
        g_apStartingSkills = GetPrivateProfileIntA("settings", "StartingSkills", 6, iniPath);
        g_fillerTrapPct = GetPrivateProfileIntA("settings", "TrapPct", 15, iniPath);
        /* 1.8.4: TrapsEnabled toggle. When 0, force trap filler weight to 0
         * so the weighted-roll redistributes proportionally to gold/stat/
         * skill/reset/loot — pool stays balanced, no traps generated. */
        if (GetPrivateProfileIntA("settings", "TrapsEnabled", 1, iniPath) == 0) {
            g_fillerTrapPct = 0;
        }
        g_fillerGoldPct = GetPrivateProfileIntA("settings", "GoldPct", 30, iniPath);
        g_fillerStatPct = GetPrivateProfileIntA("settings", "StatPtsPct", 15, iniPath);
        g_fillerSkillPct = GetPrivateProfileIntA("settings", "SkillPtsPct", 15, iniPath);
        g_fillerResetPct = GetPrivateProfileIntA("settings", "ResetPtsPct", 25, iniPath);
        g_fillerLootPct = GetPrivateProfileIntA("settings", "LootPct", 18, iniPath);
        g_monsterShuffleEnabled = GetPrivateProfileIntA("settings", "MonsterShuffle", 0, iniPath) != 0;
        g_bossShuffleEnabled = GetPrivateProfileIntA("settings", "BossShuffle", 0, iniPath) != 0;
        g_shopShuffleEnabled = GetPrivateProfileIntA("settings", "ShopShuffle", 0, iniPath) != 0;
        g_entranceShuffleEnabled = GetPrivateProfileIntA("settings", "EntranceShuffle", 0, iniPath) != 0;
        /* 1.8.0 cleanup: TreasureCows INI parse removed — pending reimplementation */
        g_xpMultiplier = GetPrivateProfileIntA("settings", "XPMultiplier", 1, iniPath);
        if (g_xpMultiplier < 1) g_xpMultiplier = 1;
        if (g_xpMultiplier > 10) g_xpMultiplier = 10;
        g_classFilter = GetPrivateProfileIntA("settings", "ClassFilter", 0, iniPath) != 0;
        g_clsEnabled[0] = GetPrivateProfileIntA("settings", "ClsAmazon", 1, iniPath) != 0;
        g_clsEnabled[1] = GetPrivateProfileIntA("settings", "ClsSorceress", 1, iniPath) != 0;
        g_clsEnabled[2] = GetPrivateProfileIntA("settings", "ClsNecromancer", 1, iniPath) != 0;
        g_clsEnabled[3] = GetPrivateProfileIntA("settings", "ClsPaladin", 1, iniPath) != 0;
        g_clsEnabled[4] = GetPrivateProfileIntA("settings", "ClsBarbarian", 1, iniPath) != 0;
        g_clsEnabled[5] = GetPrivateProfileIntA("settings", "ClsDruid", 1, iniPath) != 0;
        g_clsEnabled[6] = GetPrivateProfileIntA("settings", "ClsAssassin", 1, iniPath) != 0;
        g_iPlayAssassin = GetPrivateProfileIntA("settings", "IPlayAssassin", 0, iniPath) != 0;
        g_stHideColors = GetPrivateProfileIntA("settings", "show_tier_colors", 0, iniPath) == 0;
        /* 1.9.0 — Bonus check toggles for standalone */
        {
            BOOL shr  = GetPrivateProfileIntA("settings", "CheckShrines",        0, iniPath) != 0;
            BOOL urn  = GetPrivateProfileIntA("settings", "CheckUrns",           0, iniPath) != 0;
            BOOL bar  = GetPrivateProfileIntA("settings", "CheckBarrels",        0, iniPath) != 0;
            BOOL chs  = GetPrivateProfileIntA("settings", "CheckChests",         0, iniPath) != 0;
            BOOL set  = GetPrivateProfileIntA("settings", "CheckSetPickups",     0, iniPath) != 0;
            BOOL gold = GetPrivateProfileIntA("settings", "CheckGoldMilestones", 0, iniPath) != 0;
            extern void Bonus_ApplyToggles(BOOL,BOOL,BOOL,BOOL,BOOL,BOOL);
            Bonus_ApplyToggles(shr, urn, bar, chs, set, gold);
        }
        /* 1.9.2 — Extra check toggles for standalone */
        {
            BOOL cow  = GetPrivateProfileIntA("settings", "CheckCowLevel",         0, iniPath) != 0;
            BOOL merc = GetPrivateProfileIntA("settings", "CheckMercMilestones",   0, iniPath) != 0;
            BOOL hf   = GetPrivateProfileIntA("settings", "CheckHellforgeRunes",   0, iniPath) != 0;
            BOOL npc  = GetPrivateProfileIntA("settings", "CheckNpcDialogue",      0, iniPath) != 0;
            BOOL rw   = GetPrivateProfileIntA("settings", "CheckRunewordCrafting", 0, iniPath) != 0;
            BOOL cube = GetPrivateProfileIntA("settings", "CheckCubeRecipes",      0, iniPath) != 0;
            extern void Extra_ApplyToggles(BOOL,BOOL,BOOL,BOOL,BOOL,BOOL);
            Extra_ApplyToggles(cow, merc, hf, npc, rw, cube);
        }
        Log("INI settings: goal=%d mode=%d monshuffle=%d bossshuffle=%d xp=%dx pool=%d classFilter=%d\n",
            g_apGoal, g_skillHuntingOn, g_monsterShuffleEnabled, g_bossShuffleEnabled, g_xpMultiplier, g_apSkillPoolSize, g_classFilter);
        return;
    }
    if (strstr(path, "ap_settings")) {
        g_apMode = TRUE;
    } else {
        Log("Loading standalone settings from %s\n", path);
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int ival;
        if (sscanf(line, "goal=%d", &ival) == 1 && ival >= 0 && ival <= 14) {
            /* 1.8.0: accept both new (0-2) and legacy (0-14) encoding.
             * Legacy 0-14 was act_scope*3 + diff; extract diff via %3.
             * 1.9.0: goal=3 (Collection) passes through directly. */
            if (ival == 3)       g_apGoal = 3;          /* Collection mode */
            else                 g_apGoal = (ival <= 2) ? ival : (ival % 3);
        }
        /* Legacy goal_scope/difficulty_scope parsing removed 1.8.0 — apworlds
         * have emitted unified `goal` since 1.6.x. Restore from the backup
         * `Backups/2026-04-23_pre_cleanup/` if old save data ever needs to be
         * re-read. */
        if (sscanf(line, "starting_gold=%d", &ival) == 1) g_apStartingGold = ival;
        if (sscanf(line, "death_link=%d", &ival) == 1) g_apDeathLink = (ival != 0);
        if (sscanf(line, "skill_pool_size=%d", &ival) == 1 && ival >= 1 && ival <= 210)
            g_apSkillPoolSize = ival;
        if (sscanf(line, "starting_skills=%d", &ival) == 1 && ival >= 0 && ival <= 20)
            g_apStartingSkills = ival;
        /* 1.8.0: quest_story from AP slot_data is IGNORED — story quests are
         * always required by D2's engine, so we keep g_apQuestStory = TRUE. */
        (void)0; /* was: if (sscanf(line, "quest_story=%d", ...)) g_apQuestStory = ...; */
        if (sscanf(line, "quest_hunting=%d", &ival) == 1) g_apQuestHunting = (ival != 0);
        if (sscanf(line, "quest_kill_zones=%d", &ival) == 1) g_apQuestKillZones = (ival != 0);
        if (sscanf(line, "quest_exploration=%d", &ival) == 1) g_apQuestExploration = (ival != 0);
        if (sscanf(line, "quest_waypoints=%d", &ival) == 1) g_apQuestWaypoints = (ival != 0);
        if (sscanf(line, "quest_level_milestones=%d", &ival) == 1) g_apQuestLevelMilestones = (ival != 0);
        /* 1.8.0: new independent toggles; legacy game_mode kept as fallback
         * so older apworld-generated ap_settings.dat files still work. */
        if (sscanf(line, "skill_hunting=%d", &ival) == 1) g_skillHuntingOn = (ival != 0);
        if (sscanf(line, "zone_locking=%d",  &ival) == 1) g_zoneLockingOn  = (ival != 0);
        if (sscanf(line, "game_mode=%d", &ival) == 1 && ival >= 0 && ival <= 1) {
            /* Only apply legacy if neither new field present (handled above).
             * Apply here as a best-effort default — newer toggles override. */
            g_skillHuntingOn = (ival == 0);
            g_zoneLockingOn  = (ival == 1);
        }
        if (sscanf(line, "controller=%d", &ival) == 1)
            g_controllerEnabled = (ival != 0);
        /* Filler distribution */
        if (sscanf(line, "filler_gold_pct=%d", &ival) == 1) g_fillerGoldPct = ival;
        if (sscanf(line, "filler_stat_pts_pct=%d", &ival) == 1) g_fillerStatPct = ival;
        if (sscanf(line, "filler_skill_pts_pct=%d", &ival) == 1) g_fillerSkillPct = ival;
        if (sscanf(line, "filler_trap_pct=%d", &ival) == 1) g_fillerTrapPct = ival;
        /* 1.8.4: TrapsEnabled override from slot_data */
        if (sscanf(line, "traps_enabled=%d", &ival) == 1 && ival == 0) g_fillerTrapPct = 0;
        if (sscanf(line, "filler_reset_pts_pct=%d", &ival) == 1) g_fillerResetPct = ival;
        if (sscanf(line, "filler_loot_pct=%d", &ival) == 1) g_fillerLootPct = ival;
        if (sscanf(line, "monster_shuffle=%d", &ival) == 1) g_monsterShuffleEnabled = (ival != 0);
        if (sscanf(line, "boss_shuffle=%d", &ival) == 1) g_bossShuffleEnabled = (ival != 0);
        if (sscanf(line, "entrance_shuffle=%d", &ival) == 1) g_entranceShuffleEnabled = (ival != 0); /* 1.9.0 */
        /* 1.9.0 — Bonus check toggles. Stashed locally; applied via
         * Bonus_ApplyToggles below the loop. */
        {
            static int s_chShr=0, s_chUrn=0, s_chBar=0, s_chCh=0, s_chSet=0, s_chGold=0;
            static int s_anyBonusSeen=0;
            if (sscanf(line, "check_shrines=%d",         &ival) == 1) { s_chShr  = ival; s_anyBonusSeen=1; }
            if (sscanf(line, "check_urns=%d",            &ival) == 1) { s_chUrn  = ival; s_anyBonusSeen=1; }
            if (sscanf(line, "check_barrels=%d",         &ival) == 1) { s_chBar  = ival; s_anyBonusSeen=1; }
            if (sscanf(line, "check_chests=%d",          &ival) == 1) { s_chCh   = ival; s_anyBonusSeen=1; }
            if (sscanf(line, "check_set_pickups=%d",     &ival) == 1) { s_chSet  = ival; s_anyBonusSeen=1; }
            if (sscanf(line, "check_gold_milestones=%d", &ival) == 1) { s_chGold = ival; s_anyBonusSeen=1; }
            /* Apply on every line — idempotent, so the final state
             * after the loop is the union of all parsed values. */
            if (s_anyBonusSeen) {
                extern void Bonus_ApplyToggles(BOOL,BOOL,BOOL,BOOL,BOOL,BOOL);
                Bonus_ApplyToggles(s_chShr!=0, s_chUrn!=0, s_chBar!=0,
                                   s_chCh!=0,  s_chSet!=0, s_chGold!=0);
            }
        }
        /* 1.9.2 — Extra check toggles (Cow / Merc / Hellforge+High runes /
         * NPC dialogue / Runeword crafting / Cube recipes). Same parse
         * pattern as Bonus toggles. */
        {
            static int s_xCow=0, s_xMerc=0, s_xHF=0, s_xNpc=0, s_xRw=0, s_xCube=0;
            static int s_anyExtraSeen=0;
            if (sscanf(line, "check_cow_level=%d",       &ival) == 1) { s_xCow  = ival; s_anyExtraSeen=1; }
            if (sscanf(line, "check_merc_milestones=%d", &ival) == 1) { s_xMerc = ival; s_anyExtraSeen=1; }
            if (sscanf(line, "check_hellforge_runes=%d", &ival) == 1) { s_xHF   = ival; s_anyExtraSeen=1; }
            if (sscanf(line, "check_npc_dialogue=%d",    &ival) == 1) { s_xNpc  = ival; s_anyExtraSeen=1; }
            if (sscanf(line, "check_runeword_crafting=%d", &ival) == 1) { s_xRw   = ival; s_anyExtraSeen=1; }
            if (sscanf(line, "check_cube_recipes=%d",    &ival) == 1) { s_xCube = ival; s_anyExtraSeen=1; }
            if (s_anyExtraSeen) {
                extern void Extra_ApplyToggles(BOOL,BOOL,BOOL,BOOL,BOOL,BOOL);
                Extra_ApplyToggles(s_xCow!=0, s_xMerc!=0, s_xHF!=0,
                                   s_xNpc!=0, s_xRw!=0,   s_xCube!=0);
            }
        }
        /* 1.9.0 — Collection-goal sub-targets. Only applied when g_apGoal==3.
         * The default-on logic in Coll_LoadForCharacter still handles the
         * case where these aren't in slot_data (older bridge / standalone
         * INI run). When they ARE in slot_data, they override the defaults.
         * Stored into Coll module's g_collGoal struct, persisted via
         * sidecar at character unload. */
        {
            /* 1.9.0 — granular per-item Collection toggles. The apworld
             * encodes the 32 set toggles + 33 rune toggles + 10 special
             * toggles as 6 bitmask integers (sets_lo/hi each 16 bits,
             * runes_lo/md/hi for 33 bits, specials_mask for 10 bits).
             * Gems remains a single boolean since per-gem granularity
             * was deemed unnecessary. The DLL stores these directly
             * into g_collGoalOverride* fields and applies them at
             * Coll_LoadForCharacter time. */
            extern uint32_t g_collGoalOverrideSetsMask;     /* 32 bits */
            extern uint64_t g_collGoalOverrideRunesMask;    /* 33 bits in low part */
            extern uint16_t g_collGoalOverrideSpecialsMask; /* 10 bits */
            extern uint8_t  g_collGoalOverrideGems;
            extern uint64_t g_collGoalOverrideGold;
            extern BOOL     g_collGoalOverridePresent;
            int   tmp = 0;
            unsigned long long tmpGold = 0;
            if (sscanf(line, "collection_sets_mask_lo=%d", &tmp) == 1) {
                g_collGoalOverrideSetsMask =
                    (g_collGoalOverrideSetsMask & 0xFFFF0000u) | (uint32_t)(tmp & 0xFFFF);
                g_collGoalOverridePresent = TRUE;
            }
            if (sscanf(line, "collection_sets_mask_hi=%d", &tmp) == 1) {
                g_collGoalOverrideSetsMask =
                    (g_collGoalOverrideSetsMask & 0x0000FFFFu) | ((uint32_t)(tmp & 0xFFFF) << 16);
                g_collGoalOverridePresent = TRUE;
            }
            if (sscanf(line, "collection_runes_mask_lo=%d", &tmp) == 1) {
                g_collGoalOverrideRunesMask =
                    (g_collGoalOverrideRunesMask & ~(uint64_t)0xFFFFu) | (uint64_t)(tmp & 0xFFFF);
                g_collGoalOverridePresent = TRUE;
            }
            if (sscanf(line, "collection_runes_mask_md=%d", &tmp) == 1) {
                g_collGoalOverrideRunesMask =
                    (g_collGoalOverrideRunesMask & ~((uint64_t)0xFFFF << 16)) |
                    ((uint64_t)(tmp & 0xFFFF) << 16);
                g_collGoalOverridePresent = TRUE;
            }
            if (sscanf(line, "collection_runes_mask_hi=%d", &tmp) == 1) {
                g_collGoalOverrideRunesMask =
                    (g_collGoalOverrideRunesMask & ~((uint64_t)0xFF << 32)) |
                    ((uint64_t)(tmp & 0xFF) << 32);
                g_collGoalOverridePresent = TRUE;
            }
            if (sscanf(line, "collection_specials_mask=%d", &tmp) == 1) {
                g_collGoalOverrideSpecialsMask = (uint16_t)(tmp & 0xFFFF);
                g_collGoalOverridePresent = TRUE;
            }
            if (sscanf(line, "collection_target_gems=%d", &tmp) == 1) {
                g_collGoalOverrideGems = (uint8_t)(tmp != 0);
                g_collGoalOverridePresent = TRUE;
            }
            if (sscanf(line, "collection_gold_target=%llu", &tmpGold) == 1) {
                g_collGoalOverrideGold = (uint64_t)tmpGold;
                g_collGoalOverridePresent = TRUE;
            }
        }
        /* 1.9.2 — Custom goal slot_data: gold target + targets CSV.
         * Sent by apworld fill_slot_data when goal=4 (custom). Both
         * keys are stashed; the final ParseTargetsCSV call wires them
         * together. The parser is a no-op at runtime when g_apGoal != 4
         * (the completion check in d2arch_gameloop.c gates on goal). */
        {
            static uint64_t s_cgGold = 0;
            static char     s_cgCsv[1024] = "";
            static int      s_cgGoldSeen = 0, s_cgCsvSeen = 0;
            unsigned long long tmpGold2 = 0;
            if (sscanf(line, "custom_goal_gold_target=%llu", &tmpGold2) == 1) {
                s_cgGold = (uint64_t)tmpGold2;
                s_cgGoldSeen = 1;
            }
            if (strncmp(line, "custom_goal_targets_csv=", 24) == 0) {
                strncpy(s_cgCsv, line + 24, sizeof(s_cgCsv) - 1);
                s_cgCsv[sizeof(s_cgCsv) - 1] = 0;
                /* Trim trailing newline if present */
                size_t L = strlen(s_cgCsv);
                while (L > 0 && (s_cgCsv[L-1] == '\n' || s_cgCsv[L-1] == '\r')) {
                    s_cgCsv[--L] = 0;
                }
                s_cgCsvSeen = 1;
            }
            /* When BOTH keys have been seen, commit to the parser.
             * Either order works — both keys can be re-applied any
             * number of times (idempotent). */
            if (s_cgGoldSeen && s_cgCsvSeen) {
                extern void CustomGoal_ParseTargetsCSV(const char* csv, uint64_t goldTarget);
                CustomGoal_ParseTargetsCSV(s_cgCsv, s_cgGold);
            }
        }
        if (sscanf(line, "xp_multiplier=%d", &ival) == 1) {
            g_xpMultiplier = ival;
            if (g_xpMultiplier < 1) g_xpMultiplier = 1;
            if (g_xpMultiplier > 10) g_xpMultiplier = 10;
        }
        /* 1.7.1: additional slot_data fields */
        if (sscanf(line, "shop_shuffle=%d", &ival) == 1) g_shopShuffleEnabled = (ival != 0);
        /* 1.8.0 cleanup: treasure_cows slot_data parse removed */
        if (sscanf(line, "i_play_assassin=%d", &ival) == 1) {
            /* Acknowledge legacy field — 1.7.0 removed the trap-skill filter so
             * this no longer gates skill inclusion, but we still log it. */
            g_iPlayAssassin = (ival != 0);
        }

        /* 1.8.0 NEW — 15 preload-id fields for gated zone-locking.
         * Parsed per (act × difficulty). AP's fill-algorithm decides these
         * at slot_data generation. Values clamp to 0..3 (Act 1/2/3/5) or
         * 0..2 (Act 4 has only 3 preloads). */
        if (sscanf(line, "act1_preload_normal=%d",    &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[0][0] = ival;
        if (sscanf(line, "act1_preload_nightmare=%d", &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[0][1] = ival;
        if (sscanf(line, "act1_preload_hell=%d",      &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[0][2] = ival;
        if (sscanf(line, "act2_preload_normal=%d",    &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[1][0] = ival;
        if (sscanf(line, "act2_preload_nightmare=%d", &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[1][1] = ival;
        if (sscanf(line, "act2_preload_hell=%d",      &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[1][2] = ival;
        if (sscanf(line, "act3_preload_normal=%d",    &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[2][0] = ival;
        if (sscanf(line, "act3_preload_nightmare=%d", &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[2][1] = ival;
        if (sscanf(line, "act3_preload_hell=%d",      &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[2][2] = ival;
        if (sscanf(line, "act4_preload_normal=%d",    &ival) == 1 && ival >= 0 && ival < 3) g_actPreload[3][0] = ival;
        if (sscanf(line, "act4_preload_nightmare=%d", &ival) == 1 && ival >= 0 && ival < 3) g_actPreload[3][1] = ival;
        if (sscanf(line, "act4_preload_hell=%d",      &ival) == 1 && ival >= 0 && ival < 3) g_actPreload[3][2] = ival;
        if (sscanf(line, "act5_preload_normal=%d",    &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[4][0] = ival;
        if (sscanf(line, "act5_preload_nightmare=%d", &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[4][1] = ival;
        if (sscanf(line, "act5_preload_hell=%d",      &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[4][2] = ival;
    }
    fclose(f);
    Log("AP: Settings loaded (goal=%d [act=%d diff=%d], skillHunt=%d zoneLock=%d, "
        "death_link=%d, monShuf=%d bossShuf=%d shopShuf=%d)\n",
        g_apGoal, GOAL_ACT_SCOPE, GOAL_DIFF_SCOPE,
        g_skillHuntingOn, g_zoneLockingOn, g_apDeathLink,
        g_monsterShuffleEnabled, g_bossShuffleEnabled, g_shopShuffleEnabled);
    Log("AP: Settings loaded extra (entrance_shuffle=%d)\n", g_entranceShuffleEnabled);
}

/* ================================================================
 * DEATHLINK — detect player death and write to file for bridge
 * ================================================================ */
static DWORD g_lastDeathSendTime = 0;
static BOOL  g_wasAlive = TRUE;

static void CheckPlayerDeath(void) {
    if (!g_apConnected || !g_apDeathLink) return;
    void* p = Player();
    if (!p) return;

    __try {
        int mode = *(int*)((DWORD)p + 0x10); /* unit mode */
        BOOL isDead = (mode == 0 || mode == 12); /* death animation or dead */

        if (isDead && g_wasAlive) {
            DWORD now = GetTickCount();
            if (now - g_lastDeathSendTime > 30000) { /* 30sec cooldown */
                g_lastDeathSendTime = now;
                char dir[MAX_PATH], path[MAX_PATH];
                GetArchDir(dir, MAX_PATH);
                sprintf(path, "%sap_death.dat", dir);
                FILE* f = fopen(path, "w");
                if (f) {
                    fprintf(f, "death=1\nplayer=%s\ncause=Killed in combat\n", g_charName);
                    fclose(f);
                    Log("AP DEATHLINK: Death sent for '%s'\n", g_charName);
                }
            }
        }
        g_wasAlive = !isDead;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* Load location owner names from bridge (who gets the item for each check) */
static void LoadLocationOwners(void) {
    if (g_locationOwnersLoaded) return;
    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(path, "%sap_location_owners.dat", dir);
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[128];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        int locId = 0;
        char owner[24] = "";
        if (sscanf(line, "%d=%23[^\n]", &locId, owner) == 2 && locId > LOCATION_BASE) {
            /* Map AP location ID back to quest ID + difficulty */
            int diff = 0;
            int questId = locId - LOCATION_BASE;
            if (questId >= 2000) { diff = 2; questId -= 2000; }
            else if (questId >= 1000) { diff = 1; questId -= 1000; }
            if (questId >= 0 && questId < MAX_QUEST_ID && diff >= 0 && diff < 3) {
                strncpy(g_locationOwner[diff][questId], owner, 23);
                g_locationOwner[diff][questId][23] = 0;
                count++;
            }
        }
    }
    fclose(f);
    if (count > 0) {
        g_locationOwnersLoaded = TRUE;
        Log("Loaded %d location owners from bridge\n", count);
    }
}

/* 1.8.5 — Load AP gate-key item locations from bridge. File format per line:
 *   item_id=loc_id|finder_slot|finder_name|recipient_slot
 *
 * Bridge writes this after a successful LocationScouts response. Currently
 * only captures keys placed in OUR own world (the scout target); keys placed
 * in OTHER players' worlds will appear once hint data is added in a future
 * release. The DLL stores a printable "loc X in PlayerName's world" string
 * indexed by [difficulty][gate_slot 0..17] so F4 tracker can render it. */
static DWORD g_lastAPItemLocPoll = 0;
static void LoadAPItemLocations(void) {
    /* The bridge updates this file as new scouts complete, so we re-read
     * it periodically rather than once. 5-second throttle is plenty fast
     * for F4 to feel responsive without thrashing the disk. */
    DWORD now = GetTickCount();
    if (g_apItemLocationsLoaded && now - g_lastAPItemLocPoll < 5000) return;
    g_lastAPItemLocPoll = now;

    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(path, "%sap_item_locations.dat", dir);
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        int itemId = 0;
        char value[200] = "";
        if (sscanf(line, "%d=%199[^\n]", &itemId, value) != 2) continue;

        /* Parse value: "loc_id|finder_slot|finder_name|recipient_slot" */
        int locId = 0, finderSlot = 0, recipientSlot = 0;
        char finderName[64] = "";
        char* p1 = strchr(value, '|');
        if (!p1) continue;
        *p1++ = 0;
        locId = atoi(value);
        char* p2 = strchr(p1, '|');
        if (!p2) continue;
        *p2++ = 0;
        finderSlot = atoi(p1);
        char* p3 = strchr(p2, '|');
        if (!p3) continue;
        *p3++ = 0;
        strncpy(finderName, p2, sizeof(finderName) - 1);
        finderName[sizeof(finderName) - 1] = 0;
        recipientSlot = atoi(p3);
        (void)finderSlot; (void)recipientSlot;  /* reserved for future filtering */

        /* Map item ID to (diff, gate_slot). GATEKEY_AP_BASE_NORMAL=46101 etc. */
        int diff = -1, slot = -1;
        if (itemId >= 46101 && itemId <= 46118)      { diff = 0; slot = itemId - 46101; }
        else if (itemId >= 46121 && itemId <= 46138) { diff = 1; slot = itemId - 46121; }
        else if (itemId >= 46141 && itemId <= 46158) { diff = 2; slot = itemId - 46141; }
        else continue;  /* not a gate-key item — skip silently */

        _snprintf(g_apItemLocation[diff][slot], APKEY_DISPLAY_LEN - 1,
                  "loc %d @ %s", locId, finderName);
        g_apItemLocation[diff][slot][APKEY_DISPLAY_LEN - 1] = 0;
        count++;
    }
    fclose(f);
    if (count > 0) {
        g_apItemLocationsLoaded = TRUE;
        Log("Loaded %d AP gate-key locations from bridge\n", count);
    }
}

/* Poll AP bridge status file (Bridge → DLL) */
static DWORD g_lastAPStatusPoll = 0;
static void PollAPStatus(void) {
    DWORD now = GetTickCount();
    if (now - g_lastAPStatusPoll < 2000) return;
    g_lastAPStatusPoll = now;

    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    /* 1.9.0: poll only after the user has actually pressed Connect (bridge
     * has been started). g_apMode no longer gates this — it's only flipped
     * TRUE below on first auth success. */
    if (!g_apPolling) return;

    sprintf(path, "%sap_status.dat", dir);

    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    BOOL wasConnected = g_apConnected;
    while (fgets(line, sizeof(line), f)) {
        char val[64];
        if (sscanf(line, "status=%63s", val) == 1) {
            if (strcmp(val, "authenticated") == 0) {
                g_apConnected = TRUE;
                strcpy(g_apStatus, "Connected");
            } else if (strcmp(val, "connected") == 0) {
                /* 1.8.5 — websocket open but slot_data not yet received.
                 * Treat as "still connecting" so the green button doesn't
                 * flash on prematurely. */
                g_apConnected = FALSE;
                strcpy(g_apStatus, "Authenticating...");
            } else if (strcmp(val, "reconnecting") == 0) {
                /* 1.8.5 — bridge is in retry-backoff. Show that the
                 * connection has dropped instead of leaving the UI frozen
                 * on "Connected" while the bridge is offline. */
                g_apConnected = FALSE;
                strcpy(g_apStatus, "Reconnecting...");
            } else if (strcmp(val, "disconnected") == 0) {
                g_apConnected = FALSE;
                strcpy(g_apStatus, "Disconnected");
            } else if (strcmp(val, "connecting") == 0) {
                g_apConnected = FALSE;
                strcpy(g_apStatus, "Connecting...");
            } else if (strcmp(val, "error") == 0) {
                g_apConnected = FALSE;
                strcpy(g_apStatus, "Error");
            } else if (strcmp(val, "refused") == 0) {
                g_apConnected = FALSE;
                strcpy(g_apStatus, "Refused");
            }
        }
    }
    fclose(f);
    if (!wasConnected && g_apConnected) {
        /* 1.8.4 — banner removed at user request; the green Connect button
         * already signals authentication. Log line kept for diagnostics. */
        Log("AP: Connection established\n");
        /* 1.9.0 — flip g_apMode TRUE here, on actual auth, instead of at
         * Connect-click time. This is the gating moment that determines
         * whether subsequently-loaded characters bake AP settings + default
         * to AP stash tabs. A click that never authenticates leaves g_apMode
         * FALSE forever (within this session), so a character created during
         * the wait stays standalone. */
        g_apMode = TRUE;
        SaveAPCharConfig();
        /* 1.9.0 — race-recovery removed. Per the new "AP only takes over
         * if there is a connection" rule, a character loaded while
         * g_apMode=FALSE bakes as standalone and freezes immediately
         * (see d2arch_save.c OnCharacterLoad). If auth then succeeds
         * mid-session, the character STAYS standalone — we do not
         * retroactively re-bake its settings from slot_data.
         *
         * The LoadAPSettings call below populates the in-memory globals
         * from ap_settings.dat for any future character that loads while
         * still on the title screen, and SyncAPToTitleSettings pushes
         * those values onto the visible toggle buttons. For an already-
         * loaded character, LoadAPSettings sees g_settingsFrozen=TRUE
         * and is a no-op. */
        LoadAPSettings();
        if (g_charName[0] && g_settingsFrozen) {
            /* 1.8.5 — Existing-character soft-lock reconcile. The per-
             * character state file is the source of truth for AP chars
             * across reloads, BUT if this character was originally baked
             * during an AP race window OR the user re-rolled their YAML
             * with different options after generation, we can hit a known
             * permanent soft-lock: per-char says zone_locking=1 but
             * slot_data says zone_locking=0 — apworld didn't generate any
             * gate keys, so the locked zones can never open.
             *
             * Detect that exact mismatch and force zone_locking=OFF so
             * the player can finish the run. We don't auto-correct other
             * settings (XP multiplier, class filters etc.) because they
             * don't soft-lock — they just feel different. */
            char checkPath[MAX_PATH];
            GetArchDir(checkPath, MAX_PATH);
            strcat(checkPath, "ap_settings.dat");
            FILE* sf = fopen(checkPath, "r");
            if (sf) {
                char line[256];
                int  slotZL    = -1;   /* -1 = not present in file */
                while (fgets(line, sizeof(line), sf)) {
                    int v;
                    if (sscanf(line, "zone_locking=%d", &v) == 1) {
                        slotZL = (v != 0) ? 1 : 0;
                        break;
                    }
                }
                fclose(sf);
                if (slotZL == 0 && g_zoneLockingOn) {
                    Log("AP-MISMATCH RECONCILE: per-char zone_locking=ON "
                        "but slot_data=OFF — auto-correcting to OFF "
                        "(no gate keys exist in this multiworld so any "
                        "locked zones would soft-lock the run)\n");
                    g_zoneLockingOn = FALSE;
                    InitZoneLocks();
                    /* Bypass freeze just long enough to persist the fix. */
                    BOOL wasFrozen = g_settingsFrozen;
                    g_settingsFrozen = FALSE;
                    SaveStateFile();
                    g_settingsFrozen = wasFrozen;
                    ShowNotify("Zone Locking auto-disabled (matches AP server)");
                }
            }
        }
        g_locationOwnersLoaded   = FALSE; /* Reload owners on new connection */
        g_apItemLocationsLoaded  = FALSE; /* 1.8.5 — Reload key locations too */
        memset(g_apItemLocation, 0, sizeof(g_apItemLocation));
        /* 1.8.2: swap Connect button to GREEN so the player gets a clear
         * visual confirmation that the AP server authenticated and the
         * randomization settings are now coming from the server's slot_data.
         * Falls back to yellow if the green DC6 didn't load. */
        {
            void* cell = g_btnCellFileGreen ? g_btnCellFileGreen : g_btnCellFile;
            if (g_btnConnectBtn && cell) {
                *(void**)((BYTE*)g_btnConnectBtn + 0x04) = cell;
                Log("AP: Connect button -> %s (connected)\n",
                    g_btnCellFileGreen ? "green" : "yellow (green DC6 missing)");
            }
        }
        /* 1.8.5 — push slot_data values onto every title-screen toggle/
         * dropdown so the user can SEE the server's settings. Without
         * this, the buttons keep showing stale d2arch.ini values until
         * the user creates a new character. */
        SyncAPToTitleSettings();
    } else if (wasConnected && !g_apConnected) {
        Log("AP: Connection lost\n");
        /* 1.8.0: swap Connect button back to red (disconnected) */
        if (g_btnConnectBtn && g_btnCellFileRed) {
            *(void**)((BYTE*)g_btnConnectBtn + 0x04) = g_btnCellFileRed;
            Log("AP: Connect button -> red (disconnected)\n");
        }
        /* 1.8.0: clean up ap_settings.dat so the next character load won't
         * re-apply stale AP settings. Without this, disconnecting and loading
         * a standalone character would still boot it as AP mode because the
         * file persists from the previous session.
         *
         * NOTE: per-character AP binding (d2arch_ap_<char>.dat) is kept —
         * that file records "this char was created under AP" and is used to
         * auto-fill the Server/Slot fields on re-login. We only remove the
         * CURRENT-SESSION settings snapshot. */
        {
            char cleanupPath[MAX_PATH];
            GetArchDir(cleanupPath, MAX_PATH);
            strcat(cleanupPath, "ap_settings.dat");
            if (DeleteFileA(cleanupPath)) {
                Log("AP: deleted stale ap_settings.dat on disconnect\n");
            }
        }
        /* 1.8.5 — restore title-screen buttons from d2arch.ini so the
         * user can edit them again in standalone mode. */
        RestoreTitleSettingsFromINI();
        /* RestoreAllCharacters() disabled — all characters always visible */
    }
}

/* Poll for AP skill unlocks (Bridge → DLL via ap_unlocks.dat) */
static void PollAPUnlocks(void) {
    if (!g_apConnected || !g_charName[0] || !g_poolInitialized) return;

    char dir[MAX_PATH], path[MAX_PATH], processing[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(path, "%sap_unlocks.dat", dir);
    sprintf(processing, "%sap_unlocks.dat.processing", dir);

    /* 1.8.0 FIX: atomic rename-then-read to avoid race with bridge's
     * atomic write. Before this fix, the bridge could os.replace() a new
     * ap_unlocks.dat between our fopen() and DeleteFileA(), causing us
     * to delete a file containing unread unlocks. By renaming first,
     * any subsequent bridge write lands on a fresh ap_unlocks.dat that
     * we pick up on the next poll. */
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return;
    /* If a previous poll crashed mid-process, clean up the old .processing
     * file first so MoveFileEx's REPLACE_EXISTING succeeds. */
    if (GetFileAttributesA(processing) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileA(processing);
    }
    if (!MoveFileExA(path, processing, MOVEFILE_REPLACE_EXISTING)) {
        /* Rename failed — either file vanished between our check and the
         * move, or another process has it locked. Skip this tick; bridge's
         * own dedup will keep retrying. */
        return;
    }

    FILE* f = fopen(processing, "r");
    if (!f) return;

    /* 1.7.1: ensure dedup set is loaded for the CURRENT character.
     * When the player switches character in-session, reset the dedup set and
     * re-load so the new character's applied-ids are used. */
    {
        static char s_dedupForChar[64] = {0};
        if (strcmp(s_dedupForChar, g_charName) != 0) {
            g_appliedApLoaded = FALSE;
            g_appliedApCount = 0;
            strncpy(s_dedupForChar, g_charName, sizeof(s_dedupForChar) - 1);
            s_dedupForChar[sizeof(s_dedupForChar) - 1] = 0;
        }
    }
    if (!g_appliedApLoaded) LoadAppliedApIds();

    char line[160];
    int unlockCount = 0;
    while (fgets(line, sizeof(line), f)) {
        int apId = 0;
        char unlockSender[40] = "";
        int unlockLoc = 0;
        /* Line format evolution (parser tolerates all three):
         *   1.8.4 added sender:    unlock=<id>|<sender>
         *   1.9.0 added location:  unlock=<id>|<sender>|<loc>
         *   legacy fallback:       unlock=<id>
         * The location lets us mark a self-released quest as locally
         * complete (same effect as if the player completed it in-game)
         * so the F1 quest list and check counters update correctly.
         *
         * Try the 3-field format first; if it fails, fall back to 2-field
         * which is forward-compatible with the legacy single-field. */
        int parsed = sscanf(line, "unlock=%d|%39[^|]|%d", &apId, unlockSender, &unlockLoc);
        if (parsed < 3) {
            /* Reset and try 2-field format. */
            unlockSender[0] = 0;
            unlockLoc = 0;
            parsed = sscanf(line, "unlock=%d|%39[^\r\n]", &apId, unlockSender);
        }
        /* Trim possible trailing CR on the sender (no |loc field present). */
        {
            int sl = (int)strlen(unlockSender);
            while (sl > 0 && (unlockSender[sl-1] == '\r' || unlockSender[sl-1] == '\n')) {
                unlockSender[--sl] = 0;
            }
        }
        if (parsed >= 1 && apId > 45000) {
            /* 1.9.0 — AP self-release auto-complete. When THIS player
             * triggers a check via the AP server (whether by completing
             * the quest in-game OR via /send_location admin / WebSocket
             * release), the local g_questCompleted[] flag must be set so
             * the F1 quest list, F3 tracker, Overview/ItemLog counters
             * all reflect the change.
             *
             * Pre-1.9.0 bug: the unlock pipeline was item-centric. When
             * an AP item arrived, the DLL applied the item (gold/skill/
             * etc.) but never traced it back to "which check produced
             * this", so the local quest state stayed at 0/N.
             *
             * Loc-id encoding (mirrors WriteChecksFile + bridge offset):
             *   loc = 42000 + (quest_id + diff * 1000)
             *   reverse: loc-42000 = check_n, then qid = check_n % 1000,
             *            diff = check_n / 1000.
             * Quest range covers 42000-44999 (3 difficulties × 1000 qids).
             * Higher loc IDs are bonus checks / gate kills, handled
             * elsewhere by their own dispatchers. */
            if (unlockLoc >= 42000 && unlockLoc < 45000 &&
                unlockSender[0] != 0 && g_apSlot[0] != 0 &&
                _stricmp(unlockSender, g_apSlot) == 0) {
                int locOff = unlockLoc - 42000;
                int rDiff = locOff / 1000;
                int rQid  = locOff % 1000;
                if (rDiff >= 0 && rDiff < 3 && rQid > 0 && rQid < MAX_QUEST_ID) {
                    if (!g_questCompleted[rDiff][rQid]) {
                        g_questCompleted[rDiff][rQid] = TRUE;
                        Log("AP SELF-COMPLETE: marked qid=%d diff=%d (loc=%d, item=%d)\n",
                            rQid, rDiff, unlockLoc, apId);
                    }
                }
            }

            /* 1.9.0 — Bonus check auto-track. When a self-released
             * shrine/urn/barrel/chest/set-pickup/gold-milestone check
             * comes back, mark the corresponding fired bit + bump the
             * per-diff slot counter so the F1 Overview / Item Log
             * counters reflect the AP-server view (not just physical
             * triggers in-game).
             *
             * Bonus loc range: 60000..65299 (1.9.2 split — was 65999). */
            if (unlockLoc >= 60000 && unlockLoc < 65300 &&
                unlockSender[0] != 0 && g_apSlot[0] != 0 &&
                _stricmp(unlockSender, g_apSlot) == 0) {
                extern BOOL Bonus_OnAPItemReceived(int apId);
                if (Bonus_OnAPItemReceived(unlockLoc)) {
                    Log("AP BONUS-TRACKED: loc=%d (item=%d)\n", unlockLoc, apId);
                }
            }

            /* 1.9.2 — Extra check auto-track. When a self-released
             * cow / merc / hellforge / NPC / runeword / cube check
             * comes back, mark the corresponding fired bit so the F1
             * Overview / Logbook counters match the AP-server view.
             *
             * Extra loc range: 65300..65799. */
            if (unlockLoc >= 65300 && unlockLoc < 65800 &&
                unlockSender[0] != 0 && g_apSlot[0] != 0 &&
                _stricmp(unlockSender, g_apSlot) == 0) {
                extern BOOL Extra_OnAPItemReceived(int apId);
                if (Extra_OnAPItemReceived(unlockLoc)) {
                    Log("AP EXTRA-TRACKED: loc=%d (item=%d)\n", unlockLoc, apId);
                }
            }

            /* 1.7.1 DEDUP: skip already-applied items, EXCEPT stackable
             * fillers (45500-45999 range). The bridge now deduplicates by
             * AP location id, so each filler line in ap_unlocks.dat
             * represents a distinct check and must be applied. Skills
             * (45001-45499) and zone keys (46001+) are non-stackable and
             * keep apId-based dedup as defense-in-depth against partial
             * writes / manual file edits. */
            BOOL isFiller = (apId >= 45500 && apId < 46000);
            /* 1.8.2 — gate-keys (46101-46158) are exempt from dedup. They're
             * idempotent (UnlockGateKey early-returns if already received) but
             * we MUST always run UnlockGateKey to ensure g_zoneLocked[] is in
             * sync — otherwise AP-server replay on reconnect, or a stale
             * applied-ids file from a pre-1.8.2 build, can leave the player
             * holding the key in inventory but staring at a locked region. */
            BOOL isGateKey = (apId >= GATEKEY_AP_BASE_NORMAL &&
                              apId <  GATEKEY_AP_BASE_HELL + GATEKEY_PER_DIFF);
            if (!isFiller && !isGateKey && IsApIdApplied(apId)) {
                Log("AP DEDUP: apId=%d already applied (non-filler), skipping\n", apId);
                continue;
            }
            /* 1.8.0 NEW: Handle gate-keys FIRST (46101-46158) */
            {
                int gDiff, gSlot;
                if (GateKey_FromAPId(apId, &gDiff, &gSlot)) {
                    UnlockGateKey(gDiff, gSlot);
                    MarkApIdApplied(apId);
                    unlockCount++;
                    continue; /* Gate key handled */
                }
            }

            /* Handle legacy zone keys (46001+) — before skill check */
            {
                int zoneKeyIdx = APItemToZoneKeyIdx(apId);
                if (zoneKeyIdx >= 0) {
                    UnlockZoneKey(zoneKeyIdx);
                    MarkApIdApplied(apId);
                    unlockCount++;
                    continue; /* Zone key handled, skip skill/filler checks */
                }
            }

            /* Handle skill items (45001-45499) */
            if (apId >= 45001 && apId < 45500) {
                int skillId = apId - 45000;
                for (int i = 0; i < g_poolCount; i++) {
                    if (g_skillDB[g_pool[i].dbIndex].id == skillId && !g_pool[i].unlocked) {
                        g_pool[i].unlocked = TRUE;
                        /* 1.7.1 FIX: rebuild the skill tree on next tick so the
                         * unlock is visible without reloading the character. */
                        g_slotsDirty = TRUE;
                        g_slotsApplied = FALSE;
                        char msg[160];
                        /* 1.8.4: include sender slot in banner if another
                         * player found this skill for us. Self-finds and
                         * unknown-sender keep the original banner. */
                        if (unlockSender[0] != 0 && g_apSlot[0] != 0
                            && _stricmp(unlockSender, g_apSlot) != 0) {
                            _snprintf(msg, sizeof(msg) - 1, "AP: %s unlocked! (from %s)",
                                      g_skillDB[g_pool[i].dbIndex].name, unlockSender);
                            msg[sizeof(msg) - 1] = 0;
                        } else {
                            sprintf(msg, "AP: %s unlocked!", g_skillDB[g_pool[i].dbIndex].name);
                        }
                        ShowNotify(msg);
                        Log("AP UNLOCK: %s (skill %d, AP item %d) from='%s' — tree rebuild queued\n",
                            g_skillDB[g_pool[i].dbIndex].name, skillId, apId,
                            unlockSender[0] ? unlockSender : "(self/unknown)");
                        MarkApIdApplied(apId);
                        unlockCount++;
                        /* 1.8.0 — Item Log: AP skill unlock (inbound) */
                        {
                            char skillNameBuf[64];
                            sprintf(skillNameBuf, "%s unlock", g_skillDB[g_pool[i].dbIndex].name);
                            ItemLogAddA(0, 2, skillNameBuf, "AP server");
                        }
                        break;
                    }
                }
                /* Already-unlocked skill: still mark applied so we never
                 * accidentally double-count it if the pool state drifts. */
                if (!IsApIdApplied(apId)) MarkApIdApplied(apId);
            }

            /* Handle filler items (45500-45999) — use server-side delivery */
            if (apId >= 45500 && apId < 46000) {
                BOOL filler_handled = TRUE;
                switch (apId) {
                    case 45500: {
                        /* 1.9.0: Gold is now uniform 1..10000 rolled at
                         * receive time. The amount is non-deterministic
                         * across runs but ALWAYS something concrete the
                         * player can see in the notify. Legacy IDs
                         * 45501/45502 below keep their old fixed amounts
                         * for in-flight seeds generated before the
                         * redesign. */
                        int gold = 1 + (rand() % 10000);
                        g_serverPendingGold += gold;
                        char gn[48]; _snprintf(gn, sizeof(gn), "AP: %d Gold!", gold);
                        ShowNotify(gn);
                        char gl[48]; _snprintf(gl, sizeof(gl), "+%d gold", gold);
                        ItemLogAddA(0, 4, gl, "AP server");
                        break;
                    }
                    case 45501: g_serverPendingGold += 2000; ShowNotify("AP: 2000 Gold!");
                                ItemLogAddA(0, 4, "+2000 gold", "AP server"); break;
                    case 45502: g_serverPendingGold += 5000; ShowNotify("AP: 5000 Gold!");
                                ItemLogAddA(0, 4, "+5000 gold", "AP server"); break;
                    case 45503: g_serverPendingStatPts += 5; ShowNotify("AP: 5 Stat Points!");
                                ItemLogAddA(0, 7, "+5 Stat Points", "AP server"); break;
                    case 45504: g_serverPendingSkillPts += 1; ShowNotify("AP: 1 Skill Point!");
                                ItemLogAddA(0, 2, "+1 Skill Point", "AP server"); break;
                    case 45505: {
                        /* DeathLink trap. Read optional source name from
                         * ap_deathlink_event.dat (written by bridge on Bounced). */
                        g_pendingTrapSpawn++;
                        char eventPath[MAX_PATH];
                        GetArchDir(eventPath, MAX_PATH);
                        strcat(eventPath, "ap_deathlink_event.dat");
                        char sourceName[48] = "";
                        FILE* ef = fopen(eventPath, "r");
                        if (ef) {
                            char line[128];
                            while (fgets(line, sizeof(line), ef)) {
                                char tmp[48];
                                if (sscanf(line, "source=%47[^\n]", tmp) == 1) {
                                    strncpy(sourceName, tmp, 47);
                                    sourceName[47] = 0;
                                }
                            }
                            fclose(ef);
                            DeleteFileA(eventPath);
                        }
                        char notifyBuf[128];
                        if (sourceName[0]) {
                            _snprintf(notifyBuf, sizeof(notifyBuf),
                                "DEATHLINK: %s died - trap incoming!", sourceName);
                        } else {
                            strcpy(notifyBuf, "AP TRAP! Monster incoming!");
                        }
                        ShowNotify(notifyBuf);
                        char logBuf[96];
                        if (sourceName[0])
                            _snprintf(logBuf, sizeof(logBuf), "DeathLink trap from %s", sourceName);
                        else
                            strcpy(logBuf, "TRAP: Monsters");
                        ItemLogAddA(0, 11, logBuf, "AP server");
                        break;
                    }
                    case 45506: g_resetPoints++; ShowNotify("AP: Reset Point!");
                                ItemLogAddA(0, 2, "+1 Reset Point", "AP server"); break;
                    /* 1.7.1 LEGACY: generic boss loot drop — kept for
                     * in-flight seeds from before the 1.9.0 redesign
                     * (which ships specific boss IDs 45514-45518). */
                    case 45507:
                        g_pendingLootDrop++;
                        g_pendingLootBossId = -1; /* leave random */
                        Log("AP LOOT DROP (legacy generic): pending=%d\n", g_pendingLootDrop);
                        ItemLogAddA(0, 9, "Boss loot drop", "AP server");
                        break;

                    /* 1.9.0 NEW: Experience filler — DLL injects via the
                     * same fnAddStat(statId 13) path the XP Multiplier
                     * feature uses. Amount is rolled 1..250000 at
                     * receive time per user spec (uniform like Gold). */
                    case 45508: {
                        int xp = 1 + (rand() % 250000);
                        if (fnAddStat && g_cachedPGame) {
                            void* pXPp = GetServerPlayer(g_cachedPGame);
                            if (pXPp) {
                                __try { fnAddStat(pXPp, 13, xp, 0); } __except(1) {}
                            }
                        }
                        char xn[48]; _snprintf(xn, sizeof(xn), "AP: %d XP!", xp);
                        ShowNotify(xn);
                        char xl[48]; _snprintf(xl, sizeof(xl), "+%d XP", xp);
                        ItemLogAddA(0, 7, xl, "AP server");
                        break;
                    }

                    /* 1.9.0 NEW: typed trap variants. Each maps directly
                     * to a single g_pendingTrap* counter so the trap
                     * processor knows which effect to apply. */
                    case 45511:
                        g_pendingTrapSlow++;
                        ShowNotify("AP TRAP! You feel sluggish...");
                        ItemLogAddA(0, 11, "Trap: Slow", "AP server");
                        break;
                    case 45512:
                        g_pendingTrapWeaken++;
                        ShowNotify("AP TRAP! Your defenses crumble!");
                        ItemLogAddA(0, 11, "Trap: Weaken", "AP server");
                        break;
                    case 45513:
                        g_pendingTrapPoison++;
                        ShowNotify("AP TRAP! Poison seeps in!");
                        ItemLogAddA(0, 11, "Trap: Poison", "AP server");
                        break;

                    /* 1.9.0 NEW: specific boss-loot drops (Andariel..Baal). */
                    case 45514: case 45515: case 45516: case 45517: case 45518: {
                        int bossIdx = apId - 45514;  /* 0..4 */
                        g_pendingLootDrop++;
                        g_pendingLootBossId = bossIdx;
                        char ln[48]; _snprintf(ln, sizeof(ln), "AP: %s Loot incoming!",
                                               g_bossLootNames[bossIdx]);
                        ShowNotify(ln);
                        char lg[48]; _snprintf(lg, sizeof(lg), "Drop: %s Loot",
                                               g_bossLootNames[bossIdx]);
                        ItemLogAddA(0, 9, lg, "AP server");
                        Log("AP LOOT DROP queued: boss=%s pending=%d\n",
                            g_bossLootNames[bossIdx], g_pendingLootDrop);
                        break;
                    }

                    /* 1.9.0 NEW: specific-item drops (charm/set/unique).
                     * The receive picks WHICH charm / WHICH set piece /
                     * WHICH unique to spawn (rand for now — deterministic
                     * per-character pre-rolling could be added later if
                     * spoiler visibility for AP becomes a hard requirement).
                     * Quests_QueueSpecificDrop hands off to the gameloop
                     * tick processor that calls QUESTS_CreateItem. */
                    case 45519: {
                        int idx = rand() % 3; /* small/large/grand */
                        Quests_QueueSpecificDrop(REWARD_DROP_CHARM, idx, "AP server");
                        break;
                    }
                    case 45520: {
                        int idx = rand() % 127; /* set piece catalog */
                        Quests_QueueSpecificDrop(REWARD_DROP_SET, idx, "AP server");
                        break;
                    }
                    case 45521: {
                        if (!g_uniqueCatalogLoaded) Quests_LoadUniqueCatalog();
                        int n = g_uniqueCatalogCount > 0 ? g_uniqueCatalogCount : 1;
                        int idx = rand() % n;
                        Quests_QueueSpecificDrop(REWARD_DROP_UNIQUE, idx, "AP server");
                        break;
                    }

                    default:
                        Log("AP: Unknown filler item %d\n", apId);
                        filler_handled = FALSE;
                        break;
                }
                /* 1.7.1: do NOT mark filler apId as applied. Stackable
                 * fillers must be applicable multiple times across the
                 * run — bridge deduplicates by location id instead. */
                if (filler_handled) unlockCount++;
            }
        }
    }
    fclose(f);

    /* 1.8.0 FIX: delete the .processing copy (not the live ap_unlocks.dat
     * which may already contain new unlocks from the bridge). This is the
     * pair to the MoveFileExA rename at the top of this function. */
    DeleteFileA(processing);
    if (unlockCount > 0) {
        SaveStateFile();
        Log("AP: Processed %d unlocks, state saved\n", unlockCount);
    }
}

static void HandleAPPanelClick(int mx, int my) {
    /* Read panel position from INI to match rendering */
    char iniP[MAX_PATH]; GetArchDir(iniP, MAX_PATH); strcat(iniP, "d2arch.ini");
    int px = GetPrivateProfileIntA("layout", "APX", 300, iniP);
    int py = GetPrivateProfileIntA("layout", "APY", 300, iniP);
    int pw = 210;
    int fy = py + 16;
    if (mx >= px + 32 && mx <= px + pw && my >= fy && my <= fy + 14) { g_apFocusField = 0; return; }
    fy += 16;
    if (mx >= px + 32 && mx <= px + pw && my >= fy && my <= fy + 14) { g_apFocusField = 1; return; }
    fy += 16;
    if (mx >= px + 32 && mx <= px + pw && my >= fy && my <= fy + 14) { g_apFocusField = 2; return; }
    fy += 18;
    int bx = px + 20, bw = pw - 40, bh = 20;
    if (mx >= bx && mx <= bx + bw && my >= fy && my <= fy + bh) {
        if (g_apConnected) {
            WriteAPCommand("disconnect");
            g_apConnected = FALSE;
            strcpy(g_apStatus, "Disconnecting...");
        } else if (g_apIP[0] && g_apSlot[0]) {
            Log("AP CONNECT: starting bridge and writing command (IP=%s Slot=%s)\n", g_apIP, g_apSlot);
            StartAPBridge();
            WriteAPCommand("connect");
            SaveAPConfig();
            /* 1.9.0 — set g_apPolling so PollAPStatus runs; g_apMode stays
             * FALSE until auth actually succeeds. */
            g_apPolling = TRUE;
            strcpy(g_apStatus, "Connecting...");
        } else {
            Log("AP CONNECT: no IP or Slot entered\n");
            strcpy(g_apStatus, "Enter IP and Slot");
        }
        return;
    }
    g_apFocusField = -1;
}

static BOOL HandleAPKeyInput(UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (g_apFocusField < 0) return FALSE;
    if (msg == WM_KEYDOWN) {
        if (wp == VK_TAB) { g_apFocusField = (g_apFocusField + 1) % 3; return TRUE; }
        if (wp == VK_RETURN || wp == VK_ESCAPE) { g_apFocusField = -1; return TRUE; }
        if (wp == VK_BACK) {
            char* field = (g_apFocusField == 0) ? g_apIP : (g_apFocusField == 1) ? g_apSlot : g_apPassword;
            int len = (int)strlen(field); if (len > 0) field[len - 1] = 0; return TRUE;
        }
    }
    if (msg == WM_CHAR) {
        char ch = (char)wp; if (ch < 32 || ch > 126) return TRUE;
        char* field = (g_apFocusField == 0) ? g_apIP : (g_apFocusField == 1) ? g_apSlot : g_apPassword;
        int maxLen = (g_apFocusField == 0) ? 62 : 30;
        int len = (int)strlen(field);
        if (len < maxLen) { field[len] = ch; field[len + 1] = 0; }
        return TRUE;
    }
    return FALSE;
}

static void RenderAPPanelD2(void) {
    if (!fnRect || !fnText || !fnFont) return;

    /* Only show on menu screens (NOT in-game) */
    DWORD now = GetTickCount();
    if (g_lastDrawGameUITime && (now - g_lastDrawGameUITime) < 500) return;

    /* 1.8.5 FIX: poll AP status here too. Previously the only caller was
     * DrawAll() (DrawGameUI hook), which doesn't fire on the title screen,
     * so g_apConnected didn't flip from FALSE→TRUE until the user entered
     * a character and exited back — that's why the Connect button stayed
     * yellow on the menu after pressing Connect. PollAPStatus is internally
     * throttled to 2s so calling it every frame here is cheap, and the
     * authentication transition path inside it now also runs
     * SyncAPToTitleSettings() to push the new state onto every button. */
    PollAPStatus();

    /* Simple status display — no input fields, AP setup is in the launcher now */
    if (!g_apConnected) return; /* Don't show anything if not connected */

    int px = 620, py = 5;
    fnFont(0);
    DrawText2("AP", px, py + 11, 2, 0);
    fnFont(6);
    DrawText2(g_apStatus, px + 20, py + 11, 2, 0);
}

/* EndScene hook asm */
static void __declspec(naked) EndSceneHook(void) {
    __asm {
        pushad
        call RenderAPPanelD2
        popad
        jmp [g_endSceneTrampolinePtr]
    }
}

/* ================================================================
 * ARCHIPELAGO SKILL TREE PANEL — 30 skills, single page, no tabs
 * Replaces the vanilla 3-tab skill tree entirely.
 * Uses D2's CelDraw for DC6 graphics when available, falls back to fnRect.
 * ================================================================ */

/* Skill tree panel DC6 resources */
static void* g_sklTreeBg = NULL;      /* Background cell file */
static void* g_sklIconFile = NULL;    /* Skill icon cell file */
static BOOL  g_sklTreeLoaded = FALSE;

/* D2CMP ordinal 10024: CelFileNormalize
 * Converts raw DC6 file data (read from disk) into a D2CellFileStrc with
 * resolved pointers. This is exactly what D2Win's ARCHIVE_LoadCellFileWithFileSize
 * calls internally after reading the file from the MPQ archive.
 *
 * Signature (from D2MOO source):
 *   void __stdcall CelFileNormalize(
 *       D2CellFileStrc* pRawFile,       // raw DC6 bytes in memory
 *       D2CellFileStrc** ppOutFile,      // receives the normalized cell file
 *       const char* szFile,              // source file name (for debug)
 *       int nLine,                       // source line (for debug)
 *       int nSpecVersion,                // -1 for default
 *       int nUnused                      // 0
 *   );
 */
typedef void (__stdcall *CelFileNormalize_t)(void* pRawFile, void** ppOutFile,
                                              const char* szFile, int nLine,
                                              int nSpecVersion, int nUnused);
static CelFileNormalize_t g_fnCelNormalize = NULL;

/* Fog ordinal 10042: FOG_Alloc (__fastcall)
 * Fog ordinal 10043: FOG_Free  (__fastcall)
 * CelFileNormalize expects memory from Fog's allocator because it may
 * call FOG_Free internally on error paths. */
typedef void* (__fastcall *FogAlloc_t)(int nSize, const char* szFile, int nLine, int nFlags);
typedef void  (__fastcall *FogFree_t)(void* pFree, const char* szFile, int nLine, int nFlags);
static FogAlloc_t g_fnFogAlloc = NULL;
static FogFree_t  g_fnFogFree  = NULL;

/* Read a DC6 file from disk and normalize it into a D2CellFileStrc.
 * Returns a valid D2CellFileStrc pointer, or NULL on failure.
 * The file path should include the .DC6 extension. */
static void* LoadDC6FromDisk(const char* szPath) {
    FILE* fp;
    long fileSize;
    void* pRawBuf;
    void* pCellFile = NULL;

    /* Resolve D2CMP CelFileNormalize (ordinal 10024) and Fog allocator once */
    if (!g_fnCelNormalize) {
        HMODULE hCMP = GetModuleHandleA("D2CMP.dll");
        HMODULE hFog = GetModuleHandleA("Fog.dll");
        if (hCMP) {
            g_fnCelNormalize = (CelFileNormalize_t)GetProcAddress(hCMP, (LPCSTR)10024);
        }
        if (hFog) {
            g_fnFogAlloc = (FogAlloc_t)GetProcAddress(hFog, (LPCSTR)10042);
            g_fnFogFree  = (FogFree_t)GetProcAddress(hFog, (LPCSTR)10043);
        }
        if (!g_fnCelNormalize) {
            Log("LoadDC6: FAILED to get D2CMP ordinal 10024 (CelFileNormalize)\n");
            return NULL;
        }
        if (!g_fnFogAlloc) {
            Log("LoadDC6: FAILED to get Fog ordinal 10042 (Alloc) -- using HeapAlloc fallback\n");
        }
        Log("LoadDC6: D2CMP CelFileNormalize at %p, FogAlloc at %p\n",
            g_fnCelNormalize, g_fnFogAlloc);
    }

    /* Open and read the DC6 file */
    fp = fopen(szPath, "rb");
    if (!fp) {
        Log("LoadDC6: fopen FAILED: %s\n", szPath);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fileSize < 24) { /* DC6 header is at minimum 24 bytes */
        Log("LoadDC6: file too small (%ld bytes): %s\n", fileSize, szPath);
        fclose(fp);
        return NULL;
    }

    /* Allocate using Fog's allocator (same allocator D2's archive system uses).
     * CelFileNormalize does IN-PLACE normalization of the raw DC6 buffer,
     * converting file-relative offsets to memory pointers. It may also call
     * FOG_Free internally on error, so the buffer MUST come from Fog. */
    if (g_fnFogAlloc) {
        pRawBuf = g_fnFogAlloc((int)fileSize, "d2arch.c", __LINE__, 0);
    } else {
        pRawBuf = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)fileSize);
    }
    if (!pRawBuf) {
        Log("LoadDC6: Alloc FAILED (%ld bytes)\n", fileSize);
        fclose(fp);
        return NULL;
    }

    if ((long)fread(pRawBuf, 1, (size_t)fileSize, fp) != fileSize) {
        Log("LoadDC6: fread FAILED (expected %ld bytes)\n", fileSize);
        if (g_fnFogFree) g_fnFogFree(pRawBuf, "d2arch.c", __LINE__, 0);
        else HeapFree(GetProcessHeap(), 0, pRawBuf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    Log("LoadDC6: Read %ld bytes from %s\n", fileSize, szPath);

    /* Call CelFileNormalize -- this converts raw DC6 data into a
     * D2CellFileStrc with resolved D2GfxCellStrc pointers.
     * The raw buffer offsets get converted to real memory pointers. */
    __try {
        g_fnCelNormalize(pRawBuf, &pCellFile, "d2arch.c", __LINE__, -1, 0);
    } __except(1) {
        Log("LoadDC6: CelFileNormalize CRASHED for %s\n", szPath);
        pCellFile = NULL;
    }

    /* Note: We do NOT free pRawBuf -- CelFileNormalize may have stored it
     * as the output (pCellFile == pRawBuf after in-place normalization),
     * or it allocated new memory. The buffer is owned by D2CMP now. */

    if (pCellFile) {
        Log("LoadDC6: SUCCESS -> CellFile=%p\n", pCellFile);
    } else {
        Log("LoadDC6: CelFileNormalize returned NULL for %s\n", szPath);
        if (g_fnFogFree) g_fnFogFree(pRawBuf, "d2arch.c", __LINE__, 0);
        else HeapFree(GetProcessHeap(), 0, pRawBuf);
    }

    return pCellFile;
}

/* Build the full path to a DC6 file in the runtime data directory.
 * Looks in: <Game.exe dir>\data\global\ui\SPELLS\<filename> */
static BOOL BuildDC6Path(char* out, int outSize, const char* subPath) {
    char exeDir[MAX_PATH];
    GetModuleFileNameA(NULL, exeDir, MAX_PATH);
    {
        char* sl = strrchr(exeDir, '\\');
        if (sl) *(sl + 1) = 0;
    }
    _snprintf(out, outSize - 1, "%sdata\\global\\ui\\SPELLS\\%s", exeDir, subPath);
    out[outSize - 1] = 0;

    /* Check if file exists */
    if (GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES) {
        Log("BuildDC6Path: NOT FOUND: %s\n", out);
        return FALSE;
    }
    return TRUE;
}
