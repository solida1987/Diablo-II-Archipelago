
/* ================================================================
 * NOTIFICATION SYSTEM
 * ================================================================ */
static void ShowNotify(const char* text) {
    MultiByteToWideChar(CP_ACP, 0, text, -1, g_notifyText, 256);
    g_notifyTime = GetTickCount();
}

/* 1.9.0 — INI-tunable layout cache for the small notification overlays
 * and the F4 zone tracker panel. Loaded once on first render so the
 * per-tick cost is zero. Keys live under [Notifications] + [ZoneTracker]
 * in d2arch.ini with documented defaults. */
static int s_renderInitDone = 0;
static int s_notifyY = 80;
static int s_bigWarnOffsetY = -40;
static int s_bigWarnDurationMs = 5000;
static int s_zoneTrackerW = 520;
static int s_zoneTrackerH = 500;
static int s_zoneTrackerRowAct = 24;
static int s_zoneTrackerRowGate = 30;
static int s_zoneTrackerRowSpacer = 8;
static void RenderInitFromIni(void) {
    INI_HOT_RELOAD_GUARD(s_renderInitDone);
    if (s_renderInitDone) return;
    s_renderInitDone = 1;
    char ini[MAX_PATH];
    GetArchDir(ini, MAX_PATH);
    strcat(ini, "d2arch.ini");
    s_notifyY            = GetPrivateProfileIntA("Notifications", "NotifyY",         80,  ini);
    s_bigWarnOffsetY     = GetPrivateProfileIntA("Notifications", "BigWarnOffsetY", -40,  ini);
    s_bigWarnDurationMs  = GetPrivateProfileIntA("Notifications", "BigWarnDurationMs", 5000, ini);
    s_zoneTrackerW       = GetPrivateProfileIntA("ZoneTracker",  "PanelWidth",       520, ini);
    s_zoneTrackerH       = GetPrivateProfileIntA("ZoneTracker",  "PanelHeight",      500, ini);
    s_zoneTrackerRowAct  = GetPrivateProfileIntA("ZoneTracker",  "RowActHeight",      24, ini);
    s_zoneTrackerRowGate = GetPrivateProfileIntA("ZoneTracker",  "RowGateHeight",     30, ini);
    s_zoneTrackerRowSpacer = GetPrivateProfileIntA("ZoneTracker", "RowSpacerHeight",   8, ini);
}

static void RenderNotify(void) {
    if (!g_notifyText[0]) return;
    DWORD elapsed = GetTickCount() - g_notifyTime;
    if (elapsed > 4000) {
        g_notifyText[0] = 0;
        return;
    }
    RenderInitFromIni();
    fnFont(1);
    fnText(g_notifyText, g_screenW / 2, s_notifyY, 4, 1); /* Gold color, centered */
}

/* 1.8.2 — Big red centred warning. Used by Treasure Cow (and any future
 * "something special spawned in this area" event). Message intentionally
 * vague — never says WHAT spawned, just that the player should look around. */
static void ShowBigWarning(const char* text) {
    MultiByteToWideChar(CP_ACP, 0, text, -1, g_bigWarnText, 256);
    g_bigWarnTime = GetTickCount();
}

static void RenderBigWarning(void) {
    if (!g_bigWarnText[0]) return;
    RenderInitFromIni();
    DWORD elapsed = GetTickCount() - g_bigWarnTime;
    const DWORD totalMs = (DWORD)s_bigWarnDurationMs;  /* INI-tunable */
    const DWORD fadeMs  = 1000;   /* last 1 s fades out via reduced opacity */
    if (elapsed > totalMs) {
        g_bigWarnText[0] = 0;
        return;
    }

    /* Multi-pass render to make the text bold/punchy. We can't change font
     * size or alpha directly, so opacity is built up by drawing the text
     * multiple times — and stripped back during the fade-out window. */
    int passes;
    if (elapsed < totalMs - fadeMs)      passes = 4;
    else if (elapsed < totalMs - 500)    passes = 3;
    else                                  passes = 1;

    fnFont(1);
    int cx = g_screenW / 2;
    int cy = g_screenH / 2 + s_bigWarnOffsetY;  /* INI-tunable Y offset */

    /* Subtle dark backing rectangle for legibility against bright tilesets.
     * Calculated to roughly cover the bold red text without scissoring. */
    if (fnRect) {
        int approxW = (int)wcslen(g_bigWarnText) * 9;
        if (approxW < 320) approxW = 320;
        int rx  = cx - approxW / 2 - 16;
        int rx2 = cx + approxW / 2 + 16;
        int ry  = cy - 18;
        int ry2 = cy + 10;
        for (int b = 0; b < 5; b++) fnRect(rx, ry, rx2, ry2, 0, 5);
        fnRect(rx, ry,     rx2, ry + 1, 1, 5);   /* red top edge   */
        fnRect(rx, ry2 - 1, rx2, ry2,    1, 5);   /* red bottom edge */
        fnRect(rx, ry, rx + 1, ry2, 1, 5);
        fnRect(rx2 - 1, ry, rx2, ry2, 1, 5);
    }

    for (int i = 0; i < passes; i++) {
        fnText(g_bigWarnText, cx, cy, 1, 1); /* color 1 = red, last arg = centre */
    }
}

/* ================================================================
 * MENU RENDERING
 * ================================================================ */
#define MENU_X 5
#define MENU_Y 38  /* Moved down to avoid covering merc/summon HP bar */
#define MENU_W 60
#define MENU_H 20
#define MENU_ITEM_H 22
#define MENU_ITEMS_BASE 3
#define MENU_ITEMS (MENU_ITEMS_BASE + (g_zoneLockingOn ? 1 : 0))

static void RenderMenu(void) {
    /* REMOVED: Menu button and dropdown */
    return;
    if (!fnRect || !fnText || !fnFont) return;
    int mx = MouseX();
    int my = MouseY();
    BOOL clicked = MouseClicked();

    /* Menu button: multi-pass background + silver borders */
    int bx2 = MENU_X + MENU_W, by2 = MENU_Y + MENU_H;
    for (int i = 0; i < 3; i++)
        fnRect(MENU_X, MENU_Y, bx2, by2, 0, 5);
    fnRect(MENU_X, MENU_Y, bx2, MENU_Y + 1, 7, 5);     /* top */
    fnRect(MENU_X, MENU_Y, MENU_X + 1, by2, 7, 5);     /* left */
    fnRect(bx2 - 1, MENU_Y, bx2, by2, 7, 5);           /* right */
    fnRect(MENU_X, by2 - 1, bx2, by2, 7, 5);           /* bottom */

    if (InRect(mx, my, MENU_X, MENU_Y, MENU_W, MENU_H)) {
        fnRect(MENU_X + 1, MENU_Y + 1, bx2 - 1, by2 - 1, 7, 5);
    }

    fnFont(0);
    fnText(L"Menu", MENU_X + 10, MENU_Y + 14, 0, 0);

    if (clicked && InRect(mx, my, MENU_X, MENU_Y, MENU_W, MENU_H)) {
        g_menuOpen = !g_menuOpen;
    }

    if (!g_menuOpen) return;

    /* Dropdown: multi-pass background + silver borders */
    int dropY = MENU_Y + MENU_H;
    int dropW = 140;
    int dropH = MENU_ITEM_H * MENU_ITEMS;
    int dx2 = MENU_X + dropW, dy2 = dropY + dropH;
    for (int i = 0; i < 6; i++)
        fnRect(MENU_X, dropY, dx2, dy2, 0, 5);
    fnRect(MENU_X, dropY, dx2, dropY + 1, 7, 5);       /* top */
    fnRect(MENU_X, dropY, MENU_X + 1, dy2, 7, 5);     /* left */
    fnRect(dx2 - 1, dropY, dx2, dy2, 7, 5);            /* right */
    fnRect(MENU_X, dy2 - 1, dx2, dy2, 7, 5);           /* bottom */

    /* Build menu labels with actual keybinding names */
    static wchar_t labelBuf[4][64];
    {
        const char* keyNames[4];
        const char* baseNames[4] = { "Skill Editor", "Quest Log", "Tracker", "Zone Map" };
        BYTE keys[4] = { g_keySkillEditor, g_keyQuestLog, g_keyTracker, g_keyZoneMap };
        for (int li = 0; li < 4; li++) {
            char kn[16] = "?";
            BYTE k = keys[li];
            if (k >= 'A' && k <= 'Z') { kn[0] = (char)k; kn[1] = 0; }
            else if (k >= '0' && k <= '9') { kn[0] = (char)k; kn[1] = 0; }
            else if (k >= VK_F1 && k <= VK_F12) sprintf(kn, "F%d", k - VK_F1 + 1);
            else if (k >= VK_NUMPAD0 && k <= VK_NUMPAD9) sprintf(kn, "Num%d", k - VK_NUMPAD0);
            else if (k == VK_TAB) strcpy(kn, "Tab");
            else if (k == VK_MENU) strcpy(kn, "Alt");
            else if (k == VK_SPACE) strcpy(kn, "Space");
            else sprintf(kn, "0x%02X", k);
            char full[64];
            sprintf(full, "%s [%s]", baseNames[li], kn);
            MultiByteToWideChar(CP_ACP, 0, full, -1, labelBuf[li], 64);
        }
    }
    const wchar_t* labels[4] = { labelBuf[0], labelBuf[1], labelBuf[2], labelBuf[3] };
    int menuItemCount = MENU_ITEMS;
    fnFont(0);
    for (int i = 0; i < menuItemCount; i++) {
        int iy = dropY + i * MENU_ITEM_H;
        BOOL hover = InRect(mx, my, MENU_X, iy, dropW, MENU_ITEM_H);
        if (hover) {
            fnRect(MENU_X + 1, iy, dx2 - 1, iy + MENU_ITEM_H, 7, 5);
        }
        fnText(labels[i], MENU_X + 8, iy + 15, hover ? 0 : 5, 0);

        if (clicked && hover) {
            g_menuOpen = FALSE;
            if (i == 0) g_editorOpen = !g_editorOpen;
            if (i == 1) g_questLogOpen = !g_questLogOpen;
            if (i == 2) g_trackerOn = !g_trackerOn;
            if (i == 3) g_zoneTrackerOpen = !g_zoneTrackerOpen;
        }
    }
}

/* ================================================================
 * 1.8.2 ZONE PROGRESSION TRACKER  (F4 panel, Zone Locking mode)
 *
 * Per-difficulty gate progression view. For each act:
 *   - Act header with cleared/total counter and act-name
 *   - Per gate: status badge, gate name, boss name, zone name,
 *     "→ unlocks Region N" hint
 *   - One row per region in between gates with locked/open status
 *
 * Status badges (text + color):
 *   [DONE]  killed and key received   — green
 *   [PEND]  killed, key in transit    — yellow (AP only)
 *   [NOW ]  current target            — red    (next non-killed gate)
 *   [----]  not yet reachable         — grey
 *
 * Source globals:
 *   g_actRegions[5]                 — region zone-id lists
 *   g_actPreload[5][3]              — preload slot per (act, diff)
 *   g_gateBossKilled[3][18]         — kill state (1.8.2)
 *   g_gateKeyReceived[3][18]        — key-receipt state
 *   g_apConnected                   — AP vs standalone mode
 *
 * The same view works in both AP (waits for key echo) and standalone
 * (key auto-granted on kill).
 * ================================================================ */

/* Render a status badge (4-char + brackets) at (x,y) with a colour. */
static void DrawStatusBadge(int x, int y, const wchar_t* label, int color) {
    fnFont(6);
    fnText(label, x, y, color, 0);
}

/* 1.9.0 — refactor for in-book reuse. The body (difficulty tabs +
 * summary + scrollable content) was inlined in RenderZoneTracker.
 * Now lives in RenderZoneTracker_Body so the new F1-book "Zones"
 * page (Editor_RenderZonesPage below) can render the same content
 * inside the book frame, without the modal chrome.
 *
 * `drawFrame=TRUE` paints the heavy gold border + title bar + close
 * button (modal/F4 path). `drawFrame=FALSE` skips frame and close,
 * just paints title text + content (in-book path; the book parchment
 * art already provides the frame). */
static void RenderZoneTracker_Body(int px, int py, int pw, int ph,
                                   int mx, int my, BOOL clicked, BOOL wasLDown,
                                   BOOL drawFrame, BOOL* outCloseClicked) {
    if (!fnRect || !fnText || !fnFont) return;
    int px2 = px + pw, py2 = py + ph;

    if (drawFrame) {
        /* ----- Background (heavy opacity build-up) ----- */
        for (int i = 0; i < 8; i++)
            fnRect(px, py, px2, py2, 0, 5);
        fnRect(px + 4, py + 4, px2 - 4, py + 6, 6, 5);

        /* Outer gold border (double line) */
        fnRect(px,     py,     px2,     py + 2, 4, 5);
        fnRect(px,     py,     px + 2,  py2,    4, 5);
        fnRect(px2-2,  py,     px2,     py2,    4, 5);
        fnRect(px,     py2-2,  px2,     py2,    4, 5);
        /* Inner thin tan accent */
        fnRect(px + 2, py + 2, px2 - 2, py + 3, 7, 5);
        fnRect(px + 2, py + 2, px + 3,  py2-2,  7, 5);
        fnRect(px2-3,  py + 2, px2 - 2, py2-2,  7, 5);
        fnRect(px + 2, py2-3,  px2 - 2, py2-2,  7, 5);
    }

    /* ----- Title bar ----- */
    int titleH = 26;
    if (drawFrame) {
        fnRect(px + 3, py + 3, px2 - 3, py + 3 + titleH, 0, 5);
        fnRect(px + 3, py + 3, px2 - 3, py + 3 + titleH, 0, 5);
        fnRect(px + 3, py + 3 + titleH, px2 - 3, py + 4 + titleH, 4, 5);
    }
    fnFont(0);
    fnText(L"ZONE  PROGRESSION", px + 14, py + 22, 9, 0);

    /* Close [X] button — modal only. */
    if (drawFrame) {
        int cbw = 22, cbh = 18;
        int cbx = px2 - cbw - 6, cby = py + 6;
        BOOL hoverX = InRect(mx, my, cbx, cby, cbw, cbh);
        if (hoverX) fnRect(cbx, cby, cbx + cbw, cby + cbh, 1, 5);
        fnRect(cbx, cby, cbx + cbw, cby + 1, 7, 5);
        fnRect(cbx, cby, cbx + 1,   cby + cbh, 7, 5);
        fnRect(cbx + cbw - 1, cby, cbx + cbw, cby + cbh, 7, 5);
        fnRect(cbx, cby + cbh - 1, cbx + cbw, cby + cbh, 7, 5);
        fnFont(6);
        fnText(L"X", cbx + 8, cby + 14, hoverX ? 0 : 4, 0);
        if (clicked && hoverX) {
            if (outCloseClicked) *outCloseClicked = TRUE;
            return;
        }
    }

    /* ----- Difficulty tabs ----- */
    int tabY = py + 3 + titleH + 6;
    int tabH = 22;
    {
        int numDiffs = g_apDiffScope + 1;
        if (numDiffs < 1) numDiffs = 1;
        if (numDiffs > 3) numDiffs = 3;
        const wchar_t* tabNames[] = { L"NORMAL", L"NIGHTMARE", L"HELL" };
        int innerW = pw - 30;
        int tabW = innerW / numDiffs;
        fnFont(6);
        for (int t = 0; t < numDiffs; t++) {
            int tx  = px + 15 + t * tabW;
            int tx2 = tx + tabW - 4;
            BOOL isActive = (g_zoneTrackerDiff == t);
            BOOL hoverTab = InRect(mx, my, tx, tabY, tabW - 4, tabH);

            /* Background fill */
            if (isActive) {
                for (int k = 0; k < 3; k++)
                    fnRect(tx, tabY, tx2, tabY + tabH, 4, 5); /* gold */
            } else {
                for (int k = 0; k < 6; k++)
                    fnRect(tx, tabY, tx2, tabY + tabH, 0, 5);
                if (hoverTab)
                    fnRect(tx, tabY, tx2, tabY + tabH, 7, 5);
            }
            /* Tab border */
            int borderColor = isActive ? 4 : 7;
            fnRect(tx,       tabY,       tx2,     tabY + 1,    borderColor, 5);
            fnRect(tx,       tabY,       tx + 1,  tabY + tabH, borderColor, 5);
            fnRect(tx2 - 1,  tabY,       tx2,     tabY + tabH, borderColor, 5);
            if (!isActive)
                fnRect(tx, tabY + tabH - 1, tx2, tabY + tabH, borderColor, 5);

            /* Centered tab text */
            int textColor = isActive ? 0 : (hoverTab ? 4 : 5);
            int textX = tx + (tabW - 4) / 2 - (int)wcslen(tabNames[t]) * 3;
            fnText(tabNames[t], textX, tabY + 16, textColor, 0);

            if (clicked && hoverTab) {
                g_zoneTrackerDiff = t;
                g_zoneTrackerScroll = 0;
            }
        }
    }

    int activeDiff = g_zoneTrackerDiff;
    int maxAct = GetMaxActForGoal();

    /* ----- Summary bar ----- */
    int totalKilled = 0, totalGates = 0;
    int perActKilled[6] = {0}, perActTotal[6] = {0};
    int perActPending[6] = {0};
    for (int act = 1; act <= maxAct; act++) {
        int numGates = g_actRegions[act - 1].num_gates;
        perActTotal[act] = numGates;
        totalGates += numGates;
        for (int g = 0; g < numGates; g++) {
            int slot = GateKey_SlotFromActGate(act, g);
            if (slot < 0) continue;
            if (g_gateBossKilled[activeDiff][slot]) {
                perActKilled[act]++;
                totalKilled++;
                if (!g_gateKeyReceived[activeDiff][slot])
                    perActPending[act]++;
            }
        }
    }

    int summaryY = tabY + tabH + 8;
    int summaryH = 22;
    fnRect(px + 12, summaryY, px2 - 12, summaryY + summaryH, 0, 5);
    fnRect(px + 12, summaryY, px2 - 12, summaryY + summaryH, 0, 5);
    fnRect(px + 12, summaryY, px2 - 12, summaryY + 1, 7, 5);
    fnRect(px + 12, summaryY + summaryH - 1, px2 - 12, summaryY + summaryH, 7, 5);
    fnFont(6);
    {
        wchar_t left[64], right[48];
        swprintf(left, 64, L"PROGRESS  %d / %d gates cleared", totalKilled, totalGates);
        const wchar_t* modeStr = g_apConnected ? L"AP MODE" : L"STANDALONE";
        swprintf(right, 48, L"%ls", modeStr);
        fnText(left,  px + 22,  summaryY + 15, 0, 0);
        fnText(right, px2 - 92, summaryY + 15, g_apConnected ? 9 : 4, 0);
    }

    /* ----- Content area ----- */
    int contentTop = summaryY + summaryH + 8;
    int contentBottom = py2 - 8;
    int listContentH = contentBottom - contentTop;
    int sbW = 12;
    int contentLeft = px + 18;
    int contentRight = px2 - 18 - sbW;

    /* Row layout heights.
     * 1.9.0 Phase 9 — gate row grew from 18 to 30 to make room for
     * the optional 2nd-line AP key location (renders below the
     * boss/zone line when AP mode + scouted data). Standalone mode
     * just leaves the bottom 12 px empty per row — costs ~216 px of
     * extra panel height across all 18 gates but the panel scrolls
     * so visually it's fine. */
    int rowAct = s_zoneTrackerRowAct;
    int rowGate = s_zoneTrackerRowGate;
    int rowSpacer = s_zoneTrackerRowSpacer;

    /* Compute total content height */
    int totalH = 0;
    for (int act = 1; act <= maxAct; act++) {
        totalH += rowAct;
        totalH += g_actRegions[act - 1].num_gates * rowGate;
        if (act < maxAct) totalH += rowSpacer;
    }

    int maxScroll = totalH - listContentH;
    if (maxScroll < 0) maxScroll = 0;
    if (g_zoneTrackerScroll > maxScroll) g_zoneTrackerScroll = maxScroll;
    if (g_zoneTrackerScroll < 0) g_zoneTrackerScroll = 0;

    /* Scrollbar (right side) */
    int sbX = px2 - sbW - 6;
    int sbX2 = sbX + sbW;
    if (totalH > listContentH) {
        for (int k = 0; k < 4; k++)
            fnRect(sbX, contentTop, sbX2, contentBottom, 0, 5);
        fnRect(sbX, contentTop, sbX2, contentTop + 1, 7, 5);
        fnRect(sbX, contentBottom - 1, sbX2, contentBottom, 7, 5);

        int thumbH = (listContentH * listContentH) / totalH;
        if (thumbH < 28) thumbH = 28;
        int thumbY = contentTop;
        if (maxScroll > 0)
            thumbY = contentTop + (g_zoneTrackerScroll * (listContentH - thumbH)) / maxScroll;

        for (int k = 0; k < 3; k++)
            fnRect(sbX + 1, thumbY, sbX2 - 1, thumbY + thumbH, 4, 5);
        fnRect(sbX + 1, thumbY, sbX2 - 1, thumbY + 1, 7, 5);
        fnRect(sbX + 1, thumbY + thumbH - 1, sbX2 - 1, thumbY + thumbH, 7, 5);

        if (InRect(mx, my, sbX - 4, contentTop, sbW + 8, listContentH) && wasLDown) {
            int relY = my - contentTop - thumbH / 2;
            if (listContentH - thumbH > 0) {
                g_zoneTrackerScroll = (relY * maxScroll) / (listContentH - thumbH);
                if (g_zoneTrackerScroll < 0) g_zoneTrackerScroll = 0;
                if (g_zoneTrackerScroll > maxScroll) g_zoneTrackerScroll = maxScroll;
            }
        }
    }

    /* ----- Find current target gate (next non-killed gate after the last killed,
     *       per act). Used to highlight the active gate per act. ----- */
    int curTargetGate[6];
    for (int act = 1; act <= maxAct; act++) {
        int numGates = g_actRegions[act - 1].num_gates;
        curTargetGate[act] = -1;
        for (int g = 0; g < numGates; g++) {
            int slot = GateKey_SlotFromActGate(act, g);
            if (slot < 0) continue;
            if (!g_gateBossKilled[activeDiff][slot]) {
                curTargetGate[act] = g;
                break;
            }
        }
    }

    /* Ascertain which acts are reachable (act N reachable iff act N-1 fully cleared).
     * Act 1 always reachable. Acts beyond that need previous act's last gate killed. */
    BOOL actReachable[6] = { FALSE, TRUE, FALSE, FALSE, FALSE, FALSE };
    for (int act = 2; act <= maxAct; act++) {
        int prev = act - 1;
        actReachable[act] = (perActKilled[prev] == perActTotal[prev]);
    }

    /* ----- Render content ----- */
    int yOff = contentTop - g_zoneTrackerScroll;

    /* Clip rect via per-row visibility check. We intentionally avoid scissoring
     * since fnRect doesn't support it; rows that overflow simply don't render. */
    static const wchar_t* actNames[6] = {
        L"", L"Rogue Encampment", L"Lut Gholein",
        L"Kurast Docks", L"Pandemonium Fortress", L"Harrogath"
    };

    for (int act = 1; act <= maxAct; act++) {
        int preload_id = g_actPreload[act - 1][activeDiff];
        int numGates = g_actRegions[act - 1].num_gates;

        /* ----- Act header ----- */
        int hdrY = yOff;
        if (hdrY + rowAct >= contentTop && hdrY <= contentBottom) {
            int hdrColor = actReachable[act] ? 4 : 5;
            int statusColor = (perActKilled[act] == perActTotal[act] && perActTotal[act] > 0) ? 2 :
                              (perActKilled[act] > 0) ? 8 : 5;

            /* Subtle act-bar background */
            int hdrTop = hdrY + 2;
            int hdrBot = hdrY + rowAct - 2;
            int hdrLeft = contentLeft;
            int hdrRight = contentRight;
            for (int k = 0; k < 4; k++)
                fnRect(hdrLeft, hdrTop, hdrRight, hdrBot, 0, 5);
            fnRect(hdrLeft, hdrTop, hdrRight, hdrTop + 1, hdrColor, 5);
            fnRect(hdrLeft, hdrBot - 1, hdrRight, hdrBot, hdrColor, 5);

            /* Act title (e.g. "ACT 1 — Rogue Encampment") */
            wchar_t title[64];
            swprintf(title, 64, L"ACT %d  -  %ls", act, actNames[act]);
            fnFont(0);
            fnText(title, hdrLeft + 8, hdrY + 17, hdrColor, 0);

            /* Right-aligned gate counter: e.g. "2 / 4 cleared" */
            wchar_t counter[32];
            swprintf(counter, 32, L"%d / %d cleared", perActKilled[act], perActTotal[act]);
            fnFont(6);
            fnText(counter, hdrRight - 100, hdrY + 17, statusColor, 0);
        }
        yOff += rowAct;

        /* ----- Gate rows ----- */
        for (int g = 0; g < numGates; g++) {
            int rowY = yOff;
            yOff += rowGate;
            if (rowY + rowGate < contentTop || rowY > contentBottom) continue;

            int slot = GateKey_SlotFromActGate(act, g);
            BOOL killed = (slot >= 0 && g_gateBossKilled[activeDiff][slot]);
            BOOL keyOk  = (slot >= 0 && g_gateKeyReceived[activeDiff][slot]);
            BOOL isCurrentTarget = (g == curTargetGate[act]) && actReachable[act];

            /* Status badge text + color */
            const wchar_t* badge;
            int badgeColor;
            if (killed && keyOk)         { badge = L"[DONE]"; badgeColor = 2; }   /* green */
            else if (killed && !keyOk)   { badge = L"[PEND]"; badgeColor = 8; }   /* orange */
            else if (isCurrentTarget)    { badge = L"[NOW ]"; badgeColor = 1; }   /* red */
            else                         { badge = L"[----]"; badgeColor = 5; }   /* grey */

            /* Row hover highlight (subtle) on the current target */
            if (isCurrentTarget) {
                fnRect(contentLeft + 2, rowY + 2,
                       contentRight - 2, rowY + rowGate - 1, 6, 5);
            }

            /* Pull boss + zone names */
            const GateSpawnDef* gd = Preload_GetGate(act, preload_id, g);
            const char* bossA = gd ? Preload_BossNameFromLabel(gd->label) : "Boss";
            const char* zoneA = (gd && Preload_ZoneName(gd->spawn_zone))
                                ? Preload_ZoneName(gd->spawn_zone) : "Unknown";

            wchar_t bossW[32], zoneW[40];
            int bi;
            for (bi = 0; bi < 31 && bossA[bi]; bi++) bossW[bi] = (wchar_t)bossA[bi];
            bossW[bi] = 0;
            int zi;
            for (zi = 0; zi < 39 && zoneA[zi]; zi++) zoneW[zi] = (wchar_t)zoneA[zi];
            zoneW[zi] = 0;

            /* Layout columns:
             *   X+8     badge   (60 px)
             *   X+72    "G1"    (24 px)
             *   X+100   boss    (110 px)
             *   X+214   "@"
             *   X+226   zone    (170 px)
             *   right   "→ R%d" or "→ Act boss"
             */
            DrawStatusBadge(contentLeft + 8, rowY + 14, badge, badgeColor);

            wchar_t gtag[8];
            swprintf(gtag, 8, L"G%d", g + 1);
            fnFont(6);
            fnText(gtag, contentLeft + 72, rowY + 14, isCurrentTarget ? 0 : 4, 0);

            int textColor = killed ? 0 : (isCurrentTarget ? 0 : 5);
            fnText(bossW, contentLeft + 100, rowY + 14, textColor, 0);
            fnText(L"@",  contentLeft + 214, rowY + 14, 5, 0);
            fnText(zoneW, contentLeft + 226, rowY + 14, textColor, 0);

            /* Right side: "-> R2" etc. R5 = act boss for Acts 1/2/3/5; R3 = Act 4 boss. */
            wchar_t arrow[20];
            int isLast = (g == numGates - 1);
            if (isLast) {
                swprintf(arrow, 20, L"-> %ls", L"act boss");
            } else {
                swprintf(arrow, 20, L"-> R%d", g + 2);
            }
            int arrowColor = killed ? 2 : (isCurrentTarget ? 1 : 5);
            fnText(arrow, contentRight - 78, rowY + 14, arrowColor, 0);

            /* 1.9.0 Phase 9 — AP key location overlay. When AP-mode
             * AND the boss is killed but key not yet received (PEND
             * state) AND the bridge has scouted location data for
             * this slot, render a second-line tag showing where the
             * key is currently placed in the multiworld.
             *
             * Position: just below the boss/zone line, indented past
             * the badge column. Uses small font (6) for compactness.
             * Color: orange (matches PEND badge) since it indicates
             * pending pickup. The line fits in the 4 px gap between
             * row bottom and the next row — render still leaves a
             * tiny visual gap so adjacent gates don't smush. */
            if (g_apConnected && slot >= 0) {
                extern const char* Quests_GetGateKeyLocStr(int, int);
                const char* loc = Quests_GetGateKeyLocStr(activeDiff, slot);
                if (loc && loc[0]) {
                    wchar_t locW[80];
                    int li = 0;
                    /* "key: " prefix + truncated location */
                    locW[li++] = L'k'; locW[li++] = L'e'; locW[li++] = L'y';
                    locW[li++] = L':'; locW[li++] = L' ';
                    for (int k = 0; loc[k] && li < 79; k++)
                        locW[li++] = (wchar_t)(unsigned char)loc[k];
                    locW[li] = 0;
                    fnFont(6);
                    /* Indent past badge (X+72) and give it space to
                     * extend nearly to the right column edge.
                     * Y position: rowY+27 — sits below the boss/zone
                     * line (at rowY+14) inside the new 30 px row. */
                    int locColor = keyOk ? 2 : 8;  /* green if got, orange pending */
                    fnText(locW, contentLeft + 100,
                           rowY + 27, locColor, 0);
                }
            }
        }

        /* Spacer between acts */
        yOff += rowSpacer;
    }
}

/* Modal F4 entry point — wraps the body with screen-centered geometry
 * and lets the close [X] button toggle g_zoneTrackerOpen. Also bails
 * when the F1 book is open since the in-book Zones page takes over. */
static void RenderZoneTracker(void) {
    if (!g_zoneTrackerOpen || !g_zoneLockingOn) return;
    if (g_editorOpen) return;          /* book renders Zones via Page 10 */
    if (!fnRect || !fnText || !fnFont) return;
    RenderInitFromIni();

    int mx = MouseX(), my = MouseY();
    BOOL clicked = MouseClicked();
    BOOL wasLDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    int pw = s_zoneTrackerW, ph = s_zoneTrackerH;
    int px = (g_screenW > 0 ? g_screenW : 800) / 2 - pw / 2;
    int py = (g_screenH > 0 ? g_screenH : 600) / 2 - ph / 2;
    if (px < 4) px = 4;
    if (py < 4) py = 4;

    BOOL closeClicked = FALSE;
    RenderZoneTracker_Body(px, py, pw, ph, mx, my, clicked, wasLDown,
                           TRUE /*drawFrame*/, &closeClicked);
    if (closeClicked) g_zoneTrackerOpen = FALSE;
}

/* In-book entry point — invoked from d2arch_editor.c page-3 dispatch.
 * 1.9.0 — refactored into d2arch_editor.c (Editor_RenderZonesPage)
 * to match the Quest-page layout (left scrollable list + right detail
 * panel). The legacy "F4-modal-body-inside-book" version below is
 * kept commented out for reference; the F4 modal still uses
 * RenderZoneTracker_Body via its own wrapper. */
#if 0
static void Editor_RenderZonesPage_Legacy(int mx, int my, BOOL clicked) {
    if (!fnRect || !fnText || !fnFont) return;
    RenderInitFromIni();
    static int s_zonesLoaded = 0;
    static int s_zPx = 50, s_zPy = 56, s_zPw = 720, s_zPh = 504;
    INI_HOT_RELOAD_GUARD(s_zonesLoaded);
    if (!s_zonesLoaded) {
        s_zonesLoaded = 1;
        char ini[MAX_PATH]; GetArchDir(ini, MAX_PATH); strcat(ini, "d2arch.ini");
        s_zPx = GetPrivateProfileIntA("EditorZonesPage", "PanelX",  50,  ini);
        s_zPy = GetPrivateProfileIntA("EditorZonesPage", "PanelY",  56,  ini);
        s_zPw = GetPrivateProfileIntA("EditorZonesPage", "PanelW",  720, ini);
        s_zPh = GetPrivateProfileIntA("EditorZonesPage", "PanelH",  504, ini);
    }
    BOOL wasLDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    RenderZoneTracker_Body(s_zPx, s_zPy, s_zPw, s_zPh,
                           mx, my, clicked, wasLDown,
                           FALSE, NULL);
}
#endif

/* ================================================================
 * SKILL EDITOR RENDERING
 * ================================================================ */
#define EDITOR_W 380
#define EDITOR_H 440
static int g_infoIdx = -1; /* Selected skill for info panel (-1=none) */

/* Forward declaration */
static void ApplyAllSlots(void);

/* Get tier for slot based on TREE_POS row: row 1-2=T1, 3-4=T2, 5-6=T3 */
static int SlotTier(int cls, int tab, int s) {
    if (cls < 0 || cls > 6) cls = 0;
    if (tab < 0 || tab > 2 || s < 0 || s >= 10) return 1;
    int row = TREE_POS[cls][tab][s][0];
    if (row <= 2) return 1;
    if (row <= 4) return 2;
    return 3;
}

/* Count available (unlocked, unassigned) skills for a given tier */
static int CountAvailable(int tier) {
    int count = 0;
    for (int i = 0; i < g_poolCount; i++) {
        if (g_pool[i].unlocked && !g_pool[i].assigned && g_skillDB[g_pool[i].dbIndex].tier == tier)
            count++;
    }
    return count;
}

/* Assign a pool skill to a tab/slot — editor only, no point cost.
 * The skill EDITOR chooses which skills go in the tree.
 * The skill TREE (vanilla S key) is where points are spent. */
static void AssignSkill(int poolIdx, int tab, int slot) {
    if (poolIdx < 0 || poolIdx >= g_poolCount) return;
    if (g_tabSlots[tab][slot] != -1) return; /* Slot occupied */

    g_pool[poolIdx].assigned = TRUE;
    g_pool[poolIdx].assignTab = tab;
    g_pool[poolIdx].assignSlot = slot;
    g_tabSlots[tab][slot] = poolIdx;
    g_slotsDirty = TRUE;
    Log("Assigned skill '%s' to tab %d slot %d (spent 1 skill point)\n",
        g_skillDB[g_pool[poolIdx].dbIndex].name, tab, slot);
    ApplyAllSlots();
    SaveSlots();
}

/* Remove a skill from a tab/slot.
 *
 * IMPORTANT: this function does NOT refund skill points or zero the .d2s
 * skill bytes. Refund happens via the full ResetD2SFile flow which runs on
 * "Player gone" (WndProc) and at OnCharacterLoad: it reads the live .d2s
 * skill levels, adds them back to NEWSKILLS, and queues a reinvest file
 * that reapplies whatever slots are currently assigned. If RemoveSkill
 * refunded here it would double-count against what ResetD2SFile credits. */
static void RemoveSkill(int tab, int slot) {
    int poolIdx = g_tabSlots[tab][slot];
    if (poolIdx < 0) return;
    g_pool[poolIdx].assigned = FALSE;
    g_pool[poolIdx].assignTab = -1;
    g_pool[poolIdx].assignSlot = -1;
    g_tabSlots[tab][slot] = -1;
    g_slotsDirty = TRUE;
    Log("Removed skill from tab %d slot %d\n", tab, slot);
    ApplyAllSlots();
    SaveSlots();
}

/* Confirmation dialog state */
static BOOL g_confirmOpen = FALSE;
static int  g_confirmAction = 0;  /* 0=assign, 1=remove */
static int  g_confirmPoolIdx = -1;
static int  g_confirmTab = -1;
static int  g_confirmSlot = -1;
static char g_confirmName[64] = "";

static void RenderConfirmDialog(int mx, int my, BOOL clicked) {
    if (!g_confirmOpen) return;
    if (!fnRect || !fnText || !fnFont) return;

    /* Only REMOVE (action==1) uses the dialog now; ADD is handled directly */
    if (g_confirmAction != 1) { g_confirmOpen = FALSE; return; }

    int dw = 420, dh = 140;
    int dx1 = (g_screenW - dw) / 2, dy1 = (g_screenH - dh) / 2;
    int dx2 = dx1 + dw, dy2 = dy1 + dh;
    int centerX = dx1 + dw / 2;

    /* Background: 6x fnRect dark opaque */
    for (int i = 0; i < 6; i++)
        fnRect(dx1, dy1, dx2, dy2, 0, 5);

    /* Gold/silver borders on all 4 sides */
    fnRect(dx1, dy1, dx2, dy1 + 1, 7, 5);     /* top */
    fnRect(dx1, dy1, dx1 + 1, dy2, 7, 5);     /* left */
    fnRect(dx2 - 1, dy1, dx2, dy2, 7, 5);     /* right */
    fnRect(dx1, dy2 - 1, dx2, dy2, 7, 5);     /* bottom */

    /* Title — centered with font 1 */
    fnFont(1);
    {
        wchar_t wmsg[128];
        char msg[128];
        sprintf(msg, "Remove %s?", g_confirmName);
        MultiByteToWideChar(CP_ACP, 0, msg, -1, wmsg, 128);
        /* D2 fnText alignment: 0=left, 1=right. For centering we use left-align
         * at an estimated offset. Title font ~10px per char. */
        int titleLen = (int)strlen(msg);
        int titleW = titleLen * 9; /* approximate width in pixels */
        fnText(wmsg, centerX - titleW / 2, dy1 + 22, 9, 0);
    }

    /* Body — centered with font 0 */
    fnFont(0);
    {
        /* "Cost: 1 Reset Point" = 20 chars * ~7px = 140px */
        fnText(L"Cost: 1 Reset Point", centerX - 70, dy1 + 50, 7, 0);
    }
    {
        wchar_t wavail[64];
        swprintf(wavail, 64, L"You have: %d available", g_resetPoints);
        int availLen = (int)wcslen(wavail);
        int availW = availLen * 7;
        fnText(wavail, centerX - availW / 2, dy1 + 68, g_resetPoints > 0 ? 2 : 1, 0);
    }

    /* Separator line */
    fnRect(dx1 + 20, dy1 + 82, dx2 - 20, dy1 + 83, 7, 5);

    /* Buttons — wider, properly centered */
    int bw = 80, bh = 24;
    int btnY = dy1 + dh - 38;
    int btnY2 = btnY + bh;
    int gap = 20;
    int yesX = centerX - bw - gap / 2;
    int yesX2 = yesX + bw;
    int noX = centerX + gap / 2;
    int noX2 = noX + bw;
    BOOL canRemove = (g_resetPoints >= 1);

    fnFont(0);
    /* Yes button */
    if (canRemove) {
        for (int i = 0; i < 3; i++) fnRect(yesX, btnY, yesX2, btnY2, 0, 5);
        fnRect(yesX, btnY, yesX2, btnY, 7, 5); fnRect(yesX, btnY, yesX, btnY2, 7, 5);
        fnRect(yesX2, btnY, yesX2, btnY2, 7, 5); fnRect(yesX, btnY2, yesX2, btnY2, 7, 5);
        fnText(L"Yes", yesX + bw / 2 - 10, btnY + 16, 2, 0);
    } else {
        for (int i = 0; i < 3; i++) fnRect(yesX, btnY, yesX2, btnY2, 0, 5);
        fnRect(yesX, btnY, yesX2, btnY2, 0, 5);
        fnText(L"Yes", yesX + bw / 2 - 10, btnY + 16, 5, 0);
    }

    /* No button — highlighted */
    for (int i = 0; i < 3; i++) fnRect(noX, btnY, noX2, btnY2, 0, 5);
    fnRect(noX, btnY, noX2, btnY, 7, 5); fnRect(noX, btnY, noX, btnY2, 7, 5);
    fnRect(noX2, btnY, noX2, btnY2, 7, 5); fnRect(noX, btnY2, noX2, btnY2, 7, 5);
    fnText(L"No", noX + bw / 2 - 7, btnY + 16, 1, 0);

    if (clicked) {
        if (InRect(mx, my, yesX, btnY, bw, bh) && canRemove) {
            g_resetPoints--;
            RemoveSkill(g_confirmTab, g_confirmSlot);
            ShowNotify("Removed skill (-1 Reset Point)");
            g_confirmOpen = FALSE;
        }
        if (InRect(mx, my, noX, btnY, bw, bh)) {
            g_confirmOpen = FALSE;
        }
    }
}

/* Forward declarations for DC6 functions defined later */
static BOOL BuildDC6Path(char* out, int outSize, const char* subPath);
static void* LoadDC6FromDisk(const char* szPath);
static void DrawCel(void* pCelFile, int frame, int x, int y);
static void DrawSkillTooltip(int skillId, int mx, int my);

/* Drag & drop state for skill editor */
static BOOL g_edDragActive = FALSE;
static int  g_edDragPoolIdx = -1;  /* which pool skill is being dragged */
static void* g_icons28 = NULL;     /* 28x28 icons for right-side slots */
static BOOL g_icons28Loaded = FALSE;

/* Editor-wide reset flag — set by character change detection */
static BOOL g_editorNeedsReset = FALSE;
/* Editor page: 0 = Skill Editor, 1 = Quest page (future) */
static int g_editorPage = 0;

