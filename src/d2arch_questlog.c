
/* Check if a quest type should be visible based on AP settings */
static BOOL IsQuestTypeActive(int questType) {
    /* Respect quest type toggles in ALL modes (AP + standalone) */
    switch (questType) {
        case QTYPE_QUESTFLAG: return g_apQuestStory;
        case QTYPE_SUPERUNIQUE: return g_apQuestHunting;
        case QTYPE_KILL: return g_apQuestKillZones;
        case QTYPE_AREA: return g_apQuestExploration;
        case QTYPE_WAYPOINT: return g_apQuestWaypoints;
        case QTYPE_LEVEL: return g_apQuestLevelMilestones;
        default: return TRUE;
    }
}

/* Check if a difficulty tab should be available */
static BOOL IsDifficultyActive(int diff) {
    if (!g_apMode) return TRUE; /* Singleplayer: all difficulties */
    return (diff <= g_apDiffScope);
}

/* QUEST LOG RENDERING */
#define QL_W   540
#define QL_H   430
#define QL_TAB_H 24

static void RenderQuestLog(void) {
    /* Old F2 quest log removed — quest book is now on F1 page 2 */
    g_questLogOpen = FALSE;
    return;
    if (!g_questLogOpen) return;
    if (!fnRect || !fnText || !fnFont) return;
    int QL_X = (g_screenW - QL_W) / 2;
    int QL_Y = (g_screenH - QL_H) / 2;

    int x1 = QL_X, y1 = QL_Y;
    int x2 = QL_X + QL_W, y2 = QL_Y + QL_H;
    int mx = MouseX();
    int my = MouseY();
    BOOL clicked = MouseClicked();
    BOOL wasLDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    int rewardPanelW = 120;
    int questListW = QL_W - rewardPanelW - 14;

    /* === 1. Background: 6x fnRect dark opaque + silver borders === */
    for (int i = 0; i < 6; i++)
        fnRect(x1, y1, x2, y2, 0, 5);

    /* Silver borders on all 4 sides */
    fnRect(x1, y1, x2, y1 + 1, 7, 5);       /* top */
    fnRect(x1, y1, x1 + 1, y2, 7, 5);       /* left */
    fnRect(x2 - 1, y1, x2, y2, 7, 5);       /* right */
    fnRect(x1, y2 - 1, x2, y2, 7, 5);       /* bottom */

    /* === 2. Title: "QUEST LOG" yellow, close [X] === */
    fnFont(1);
    {
        wchar_t wt[] = L"QUEST LOG";
        fnText(wt, x1 + QL_W / 2, y1 + 18, 9, 1);
    }

    /* Close button — inside the border */
    int closeX = x2 - 22;
    int closeY = y1 + 4;
    if (InRect(mx, my, closeX, closeY, 18, 18)) {
        fnRect(closeX, closeY, closeX + 18, closeY + 18, 7, 5);
    }
    fnFont(0);
    fnText(L"X", closeX + 4, closeY + 13, 1, 0);
    if (clicked && InRect(mx, my, closeX, closeY, 20, 20)) {
        g_questLogOpen = FALSE;
        return;
    }

    /* === 3. Difficulty tabs (20px) === */
    int diffY = y1 + 26;
    int diffW = QL_W / 3;
    fnFont(0);
    for (int d = 0; d < 3; d++) {
        int tx = x1 + d * diffW;
        int tx2 = tx + diffW;
        BOOL diffAvail = IsDifficultyActive(d);
        BOOL active = (g_questLogDifficulty == d);
        BOOL hover = InRect(mx, my, tx, diffY, diffW, 20);
        if (active && diffAvail) {
            fnRect(tx + 1, diffY, tx2 - 1, diffY + 20, 7, 5);
            fnRect(tx + 1, diffY, tx2 - 1, diffY + 20, 7, 5);
        }
        {
            wchar_t wbuf[32];
            MultiByteToWideChar(CP_ACP, 0, g_diffNames[d], -1, wbuf, 32);
            int col;
            if (!diffAvail) col = 5; /* gray out inactive difficulties */
            else if (active) col = 9;
            else if (hover) col = 0;
            else col = 5;
            fnText(wbuf, tx + diffW / 2, diffY + 15, col, 1);
        }
        if (clicked && hover && diffAvail) {
            g_questLogDifficulty = d;
            g_questLogScroll = 0;
        }
    }

    /* Separator after difficulty tabs */
    fnRect(x1, diffY + 20, x2, diffY + 21, 7, 5);

    /* === 4. Act tabs (22px) === */
    int tabY = diffY + 22;
    int tabW = QL_W / 5;
    fnFont(0);
    for (int a = 0; a < 5; a++) {
        int tx = x1 + a * tabW;
        int tx2 = tx + tabW;
        BOOL active = (g_questLogAct == a);
        BOOL hover = InRect(mx, my, tx, tabY, tabW, 22);
        if (active) {
            fnRect(tx + 1, tabY, tx2 - 1, tabY + 22, 7, 5);
            fnRect(tx + 1, tabY, tx2 - 1, tabY + 22, 7, 5);
        }
        {
            wchar_t wbuf[16];
            swprintf(wbuf, 16, L"Act %d", a + 1);
            fnText(wbuf, tx + tabW / 2, tabY + 16, active ? 9 : (hover ? 0 : 5), 1);
        }
        if (clicked && hover) {
            g_questLogAct = a;
            g_questLogScroll = 0;
        }
    }

    /* Separator after act tabs */
    fnRect(x1, tabY + 22, x2, tabY + 23, 7, 5);

    /* === 5. Sub-tabs (20px): Main Quests / Side Quests === */
    int subY = tabY + 24;
    int subW = QL_W / 2;
    {
        const wchar_t* subLabels[] = { L"Main Quests", L"Side Quests" };
        fnFont(0);
        for (int st = 0; st < 2; st++) {
            int sx = x1 + st * subW;
            int sx2 = sx + subW;
            BOOL active = (g_questLogSubTab == st);
            BOOL hover = InRect(mx, my, sx, subY, subW, 20);
            if (active) {
                fnRect(sx + 1, subY, sx2 - 1, subY + 20, 7, 5);
                fnRect(sx + 1, subY, sx2 - 1, subY + 20, 7, 5);
            }
            fnText(subLabels[st], sx + subW / 2, subY + 15, active ? 2 : (hover ? 0 : 5), 1);
            if (clicked && hover) {
                g_questLogSubTab = st;
                g_questLogScroll = 0;
            }
        }
    }

    /* Separator after sub-tabs */
    fnRect(x1, subY + 20, x2, subY + 21, 7, 5);

    /* === 6. Content area === */
    int contentTop = subY + 22;
    int contentBottom = y2 - 4;
    int itemH = 42;

    /* --- Rewards panel (right column) --- */
    int rpX = x1 + QL_W - rewardPanelW;
    int rpY = contentTop;
    int rpH = contentBottom - contentTop;
    int rpX2 = rpX + rewardPanelW;
    int rpY2 = rpY + rpH;

    /* Background: 3x fnRect + silver borders */
    for (int i = 0; i < 3; i++)
        fnRect(rpX, rpY, rpX2, rpY2, 0, 5);
    fnRect(rpX, rpY, rpX2, rpY + 1, 7, 5);           /* top */
    fnRect(rpX, rpY, rpX + 1, rpY2, 7, 5);           /* left */
    fnRect(rpX2 - 1, rpY, rpX2, rpY2, 7, 5);         /* right */
    fnRect(rpX, rpY2 - 1, rpX2, rpY2, 7, 5);         /* bottom */

    /* Rewards title — left-aligned inside panel, not centered (avoids overflow) */
    fnFont(0);
    fnText(L"REWARDS", rpX + 6, rpY + 14, 9, 0);
    fnRect(rpX + 4, rpY + 18, rpX2 - 4, rpY + 19, 7, 5); /* separator */

    /* Reward rows — compact, all left-aligned inside panel */
    fnFont(0);
    int rrY = rpY + 24;
    int rrSpace = 26;
    {
        wchar_t wval[32];

        fnText(L"Gold", rpX + 6, rrY, 7, 0);
        swprintf(wval, 32, L"%d", g_pendingRewardGold);
        fnText(wval, rpX + 6, rrY + 12, g_pendingRewardGold > 0 ? 9 : 5, 0);
        rrY += rrSpace;

        fnText(L"Stat Pts", rpX + 6, rrY, 7, 0);
        swprintf(wval, 32, L"%d", g_pendingRewardStatPts);
        fnText(wval, rpX + 6, rrY + 12, g_pendingRewardStatPts > 0 ? 9 : 5, 0);
        rrY += rrSpace;

        fnText(L"Skill Pts", rpX + 6, rrY, 7, 0);
        swprintf(wval, 32, L"%d", g_pendingRewardSkillPts);
        fnText(wval, rpX + 6, rrY + 12, g_pendingRewardSkillPts > 0 ? 9 : 5, 0);
        rrY += rrSpace;

        fnText(L"Resets", rpX + 6, rrY, 7, 0);
        swprintf(wval, 32, L"%d", g_resetPoints);
        fnText(wval, rpX + 6, rrY + 12, g_resetPoints > 0 ? 9 : 5, 0);
        rrY += rrSpace;
    }

    /* Rewards now given directly — no relog needed */

    /* --- Quest list (left side with scrollbar) --- */
    ActData *act = &g_acts[g_questLogAct];
    int viewDiff = g_questLogDifficulty;

    /* Count filtered quests for scroll calculation */
    int filteredCount = 0;
    int incompleteCount = 0;
    for (int q = 0; q < act->num; q++) {
        Quest *quest = &act->quests[q];
        if (quest->id == 0) continue;
        /* AP mode: skip quests whose type is disabled */
        if (!IsQuestTypeActive(quest->type)) continue;
        BOOL isMainQuest = (quest->type == QTYPE_QUESTFLAG || quest->type == QTYPE_SUPERUNIQUE || quest->type == QTYPE_LEVEL);
        if (g_questLogSubTab == 0 && !isMainQuest) continue;
        if (g_questLogSubTab == 1 && isMainQuest) continue;
        int qid = quest->id;
        BOOL isDone = (qid >= 0 && qid < MAX_QUEST_ID) ? g_questCompleted[viewDiff][qid] : quest->completed;
        if (!isDone) incompleteCount++;
        filteredCount++;
    }
    /* +1 for separator if both incomplete and completed exist */
    int totalItems = filteredCount + ((incompleteCount > 0 && incompleteCount < filteredCount) ? 1 : 0);
    int totalH = totalItems * itemH;
    int listContentH = contentBottom - contentTop;
    int maxScroll = totalH - listContentH;
    if (maxScroll < 0) maxScroll = 0;
    if (g_questLogScroll > maxScroll) g_questLogScroll = maxScroll;
    if (g_questLogScroll < 0) g_questLogScroll = 0;

    /* Scrollbar (right of quest list, left of reward panel) */
    int sbX = rpX - 12;
    int sbX2 = sbX + 8;
    if (totalH > listContentH) {
        /* Track */
        fnRect(sbX, contentTop, sbX2, contentBottom, 0, 5);

        int thumbH = (listContentH * listContentH) / totalH;
        if (thumbH < 20) thumbH = 20;
        int thumbY = contentTop;
        if (maxScroll > 0)
            thumbY = contentTop + (g_questLogScroll * (listContentH - thumbH)) / maxScroll;

        /* Thumb */
        fnRect(sbX, thumbY, sbX2, thumbY + thumbH, 7, 5);

        /* Draggable thumb */
        if (InRect(mx, my, sbX - 4, contentTop, 16, listContentH) && wasLDown) {
            int relY = my - contentTop - thumbH / 2;
            if (listContentH - thumbH > 0) {
                g_questLogScroll = (relY * maxScroll) / (listContentH - thumbH);
                if (g_questLogScroll < 0) g_questLogScroll = 0;
                if (g_questLogScroll > maxScroll) g_questLogScroll = maxScroll;
            }
        }
    }

    /* Render quests -- two passes: 0=incomplete, 1=completed */
    int renderIdx = 0;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1 && incompleteCount > 0 && incompleteCount < filteredCount) {
            /* Separator between incomplete and completed */
            int sepItemY = contentTop + renderIdx * itemH - g_questLogScroll;
            if (sepItemY >= contentTop && sepItemY + itemH <= contentBottom) {
                fnRect(x1 + 8, sepItemY + 8, x1 + questListW - 8, sepItemY + 10, 7, 5);
                fnFont(0);
                fnText(L"-- Completed --", x1 + questListW / 2, sepItemY + 22, 2, 1);
            }
            renderIdx++;
        }

        for (int q = 0; q < act->num; q++) {
            Quest *quest = &act->quests[q];
            if (quest->id == 0) continue;
            /* AP mode: skip quests whose type is disabled */
            if (!IsQuestTypeActive(quest->type)) continue;

            /* Sub-tab filter */
            BOOL isMainQuest = (quest->type == QTYPE_QUESTFLAG || quest->type == QTYPE_SUPERUNIQUE || quest->type == QTYPE_LEVEL);
            if (g_questLogSubTab == 0 && !isMainQuest) continue;
            if (g_questLogSubTab == 1 && isMainQuest) continue;

            int qid = quest->id;
            BOOL isDone = (qid >= 0 && qid < MAX_QUEST_ID) ? g_questCompleted[viewDiff][qid] : quest->completed;

            if (pass == 0 && isDone) continue;
            if (pass == 1 && !isDone) continue;

            int itemY = contentTop + renderIdx * itemH - g_questLogScroll;
            /* CLIP: skip items outside content area */
            if (itemY < contentTop - 2 || itemY + itemH > contentBottom + 2) { renderIdx++; continue; }

            /* Quest name with status (truncate to prevent overflow into rewards panel) */
            fnFont(1);
            {
                wchar_t wname[128];
                char name[96];
                if (isDone) {
                    sprintf(name, "[DONE] %s", quest->name);
                } else if (quest->type == QTYPE_KILL) {
                    /* Read the tally of the difficulty being VIEWED, not the one being played -- the book has a difficulty selector and quest->killCount only ever holds the live one. */
                    int vk = (quest->param >= 0 && quest->param < MAX_AREA_ID)
                           ? g_areaKills[KillDiff(viewDiff)][quest->param] : 0;
                    sprintf(name, "[%d/%d] %s", vk, quest->killReq, quest->name);
                } else {
                    sprintf(name, "[    ] %s", quest->name);
                }
                /* Truncate long names to prevent bleeding into rewards panel */
                if (strlen(name) > 40) { name[37] = '.'; name[38] = '.'; name[39] = '.'; name[40] = '\0'; }
                MultiByteToWideChar(CP_ACP, 0, name, -1, wname, 128);

                int nameColor;
                if (isDone) nameColor = 2;              /* green */
                else if (quest->reward == REWARD_SKILL) nameColor = 9; /* yellow/gold */
                else nameColor = 0;                     /* white */
                fnText(wname, x1 + 12, itemY + 14, nameColor, 0);
            }

            /* Description (truncate to prevent overflow) */
            fnFont(0);
            {
                wchar_t wdesc[256];
                char desc[128];
                strncpy(desc, quest->desc, 127); desc[127] = '\0';
                if (strlen(desc) > 50) { desc[47] = '.'; desc[48] = '.'; desc[49] = '.'; desc[50] = '\0'; }
                MultiByteToWideChar(CP_ACP, 0, desc, -1, wdesc, 256);
                fnText(wdesc, x1 + 20, itemY + 28, 5, 0);
            }

            /* Reward info (right side of quest list area) */
            fnFont(0);
            if (g_apMode && g_locationOwnersLoaded) {
                /* AP mode: show who gets the item for this check */
                int ownerQid = quest->id;
                const char* owner = (ownerQid > 0 && ownerQid < MAX_QUEST_ID)
                    ? g_locationOwner[viewDiff][ownerQid] : "";
                if (owner[0]) {
                    wchar_t wowner[32];
                    MultiByteToWideChar(CP_ACP, 0, owner, -1, wowner, 32);
                    /* Truncate if too long for the column */
                    if (wcslen(wowner) > 12) { wowner[11] = L'.'; wowner[12] = L'.'; wowner[13] = 0; }
                    fnText(wowner, x1 + questListW - 80, itemY + 14,
                        (_stricmp(owner, g_apSlot) == 0) ? 9 : 0, 0); /* gold=yours, white=other */
                } else {
                    fnText(L"Check", x1 + questListW - 50, itemY + 14, 5, 0);
                }
            } else {
                /* Singleplayer or owners not loaded yet */
                fnText(L"Check", x1 + questListW - 50, itemY + 14, 5, 0);
            }

            /* Thin separator between quest items */
            {
                int sepY = itemY + itemH - 2;
                if (sepY > contentTop && sepY < contentBottom)
                    fnRect(x1 + 8, sepY, x1 + questListW - 16, sepY + 1, 7, 5);
            }

            renderIdx++;
        }
    }

    /* === REDRAW header area to clip leaked text (D2 has no text clipping) === */
    for (int i = 0; i < 6; i++)
        fnRect(x1, y1, x2, y1 + (contentTop - y1), 0, 5);

    /* Silver borders (top portion) */
    fnRect(x1, y1, x2, y1 + 1, 7, 5);
    fnRect(x1, y1, x1 + 1, y1 + (contentTop - y1), 7, 5);
    fnRect(x2 - 1, y1, x2, y1 + (contentTop - y1), 7, 5);

    /* Re-render title */
    fnFont(1);
    fnText(L"QUEST LOG", x1 + QL_W / 2, y1 + 18, 9, 1);
    fnFont(0);
    fnText(L"X", closeX + 8, closeY + 15, 1, 1);

    /* Re-render difficulty tabs */
    fnFont(0);
    for (int d = 0; d < 3; d++) {
        int tx = x1 + d * diffW;
        int tx2r = tx + diffW;
        BOOL diffAvail2 = IsDifficultyActive(d);
        if (g_questLogDifficulty == d && diffAvail2) {
            fnRect(tx + 1, diffY, tx2r - 1, diffY + 20, 7, 5);
            fnRect(tx + 1, diffY, tx2r - 1, diffY + 20, 7, 5);
        }
        {
            wchar_t wbuf[32];
            MultiByteToWideChar(CP_ACP, 0, g_diffNames[d], -1, wbuf, 32);
            int col2;
            if (!diffAvail2) col2 = 5; /* gray out inactive */
            else if (g_questLogDifficulty == d) col2 = 9;
            else col2 = 5;
            fnText(wbuf, tx + diffW / 2, diffY + 15, col2, 1);
        }
    }
    fnRect(x1, diffY + 20, x2, diffY + 21, 7, 5);

    /* Re-render act tabs */
    for (int a = 0; a < 5; a++) {
        int tx = x1 + a * tabW;
        int tx2r = tx + tabW;
        if (g_questLogAct == a) {
            fnRect(tx + 1, tabY, tx2r - 1, tabY + 22, 7, 5);
            fnRect(tx + 1, tabY, tx2r - 1, tabY + 22, 7, 5);
        }
        {
            wchar_t wbuf[16];
            swprintf(wbuf, 16, L"Act %d", a + 1);
            fnText(wbuf, tx + tabW / 2, tabY + 16, g_questLogAct == a ? 9 : 5, 1);
        }
    }
    fnRect(x1, tabY + 22, x2, tabY + 23, 7, 5);

    /* Re-render sub-tabs */
    {
        const wchar_t* subLabels[] = { L"Main Quests", L"Side Quests" };
        for (int st = 0; st < 2; st++) {
            int sx = x1 + st * subW;
            int sx2 = sx + subW;
            if (g_questLogSubTab == st) {
                fnRect(sx + 1, subY, sx2 - 1, subY + 20, 7, 5);
                fnRect(sx + 1, subY, sx2 - 1, subY + 20, 7, 5);
            }
            fnText(subLabels[st], sx + subW / 2, subY + 15, g_questLogSubTab == st ? 2 : 5, 1);
        }
    }
    fnRect(x1, subY + 20, x2, subY + 21, 7, 5);

    /* Paint over bottom edge if items leaked below */
    if (contentBottom < y2) {
        for (int i = 0; i < 3; i++)
            fnRect(x1, contentBottom, x1 + QL_W - rewardPanelW, y2, 0, 5);
    }
}

/* QUEST TRACKER RENDERING */
/* Tracker position (draggable) */
static int g_trackerX = -1; /* -1 = default position */
static int g_trackerY = 60;
static BOOL g_trackerDragging = FALSE;
static int g_dragOffX = 0, g_dragOffY = 0;

/* the tracker sits under the cursor during normal play, so a click that misses a monster grabs the handle and drags it somewhere else. */
static BOOL g_trackerLocked = FALSE;
static BOOL g_trackerPrefsLoaded = FALSE;

#define TRACKER_W 180
#define TRACKER_HANDLE_H 14

static void TrackerIniPath(char* out, int cap) {
    GetArchDir(out, cap);
    strncat(out, "d2arch.ini", cap - strlen(out) - 1);
}

static void TrackerLoadPrefs(void) {
    if (g_trackerPrefsLoaded) return;
    g_trackerPrefsLoaded = TRUE;
    char ini[MAX_PATH];
    TrackerIniPath(ini, MAX_PATH);
    g_trackerLocked = GetPrivateProfileIntA("Tracker", "Locked", 0, ini) != 0;
    int x = GetPrivateProfileIntA("Tracker", "X", -1, ini);
    int y = GetPrivateProfileIntA("Tracker", "Y", -1, ini);
    /* Only accept a stored position that still fits this resolution — a leftover from a bigger monitor would park the tracker off-screen with no handle left to grab. */
    if (x >= 0 && y >= 0 && x <= g_screenW - 50 && y <= g_screenH - 30) {
        g_trackerX = x;
        g_trackerY = y;
    }
}

static void TrackerSavePrefs(void) {
    char ini[MAX_PATH], buf[16];
    TrackerIniPath(ini, MAX_PATH);
    WritePrivateProfileStringA("Tracker", "Locked", g_trackerLocked ? "1" : "0", ini);
    _snprintf(buf, sizeof(buf), "%d", g_trackerX);
    WritePrivateProfileStringA("Tracker", "X", buf, ini);
    _snprintf(buf, sizeof(buf), "%d", g_trackerY);
    WritePrivateProfileStringA("Tracker", "Y", buf, ini);
}

static void RenderTracker(void) {
    if (!g_trackerOn) return;
    /* P28 (Maegis screenshot): the HUD tracker kept drawing OVER the open
     * book — goal/act/hunt lines in a different font straight across the
     * quest pages, unreadable. The book shows all of this itself. */
    if (g_editorOpen) return;

    int mx = MouseX();
    int my = MouseY();

    TrackerLoadPrefs();

    /* Default position */
    if (g_trackerX < 0) g_trackerX = g_screenW - TRACKER_W - 10;

    /* Handle dragging — use physical key state (works regardless of WndProc) */
    BOOL lmbDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    /* "Book" button at the right end of the drag handle. */
    int bookBW = 40, bookBH = TRACKER_HANDLE_H;
    int bookBX = g_trackerX + TRACKER_W - bookBW - 2;
    int bookBY = g_trackerY;
    BOOL overBook = InRect(mx, my, bookBX, bookBY, bookBW, bookBH);

    /* Lock button, immediately left of Book on the same handle bar. */
    int lockBW = 26, lockBH = TRACKER_HANDLE_H;
    int lockBX = bookBX - lockBW - 2;
    int lockBY = g_trackerY;
    BOOL overLock = InRect(mx, my, lockBX, lockBY, lockBW, lockBH);

    static BOOL s_prevTrkLmb = FALSE;
    BOOL lmbEdge = (lmbDown && !s_prevTrkLmb);
    s_prevTrkLmb = lmbDown;

    /* Debug: log when mouse is near tracker and clicking */
    {
        static DWORD s_dragDbg = 0;
        DWORD now2 = GetTickCount();
        if (lmbDown && now2 - s_dragDbg > 500) {
            s_dragDbg = now2;
            BOOL hit = InRect(mx, my, g_trackerX, g_trackerY, TRACKER_W, TRACKER_HANDLE_H + 6);
            if (g_verboseInput) Log("DRAG: lmb=1 mx=%d my=%d tracker=(%d,%d) w=%d h=%d hit=%d dragging=%d\n",
                mx, my, g_trackerX, g_trackerY, TRACKER_W, TRACKER_HANDLE_H + 6, hit, g_trackerDragging);
        }
    }

    if (g_trackerDragging) {
        if (lmbDown) {
            g_trackerX = mx - g_dragOffX;
            g_trackerY = my - g_dragOffY;
            if (g_trackerX < 0) g_trackerX = 0;
            if (g_trackerY < 0) g_trackerY = 0;
            if (g_trackerX > g_screenW - 50) g_trackerX = g_screenW - 50;
            if (g_trackerY > g_screenH - 30) g_trackerY = g_screenH - 30;
        } else {
            g_trackerDragging = FALSE;
            /* Remember where it ended up, so the next session starts here. */
            TrackerSavePrefs();
        }
    } else {
        if (lmbEdge && overLock) {
            g_trackerLocked = !g_trackerLocked;
            TrackerSavePrefs();
            ShowNotify(g_trackerLocked ? "Tracker locked" : "Tracker unlocked");
            Log("TRACKER: lock button -> %s\n", g_trackerLocked ? "LOCKED" : "unlocked");
        } else if (lmbEdge && overBook) {
            /* Book button: TOGGLE the F1 book (open AND close, like the F1 key itself), don't start a drag. */
            g_editorOpen = !g_editorOpen;
            Log("TRACKER: Book button clicked -> %s F1 book\n", g_editorOpen ? "opening" : "closing");
        } else if (!g_trackerLocked && lmbDown && !overBook && !overLock &&
                   InRect(mx, my, g_trackerX, g_trackerY, TRACKER_W, TRACKER_HANDLE_H + 6)) {
            g_trackerDragging = TRUE;
            g_dragOffX = mx - g_trackerX;
            g_dragOffY = my - g_trackerY;
            Log("DRAG START: offX=%d offY=%d\n", g_dragOffX, g_dragOffY);
        }
    }

    int tx = g_trackerX;
    int ty = g_trackerY;

    /* Drag handle bar */
    DrawBox(tx, ty, TRACKER_W, TRACKER_HANDLE_H, 0x1A1A2F, 3);

    /* Grip dots — only when the bar can actually be grabbed. */
    fnFont(6);
    if (!g_trackerLocked)
        DrawText2("::::", tx + TRACKER_W / 2 - 24, ty + 11, 5, 1);

    /* "Book" button on the handle bar (opens the F1 book). */
    {
        int bbx = tx + TRACKER_W - bookBW - 2;
        int bby = ty;
        DrawBox(bbx, bby, bookBW, bookBH, overBook ? 0x3A2A10 : 0x2A1A0A, 3);
        fnFont(6);
        DrawText2("Book", bbx + bookBW / 2, bby + 11, 4, 1);   /* gold, centered */

        /* lock button. Locked reads as a solid, "closed" state (red text on a darker plate); unlocked is muted, because unlocked is the state you pass through rather than sit in. */
        int lbx = bbx - lockBW - 2;
        int lby = ty;
        DrawBox(lbx, lby, lockBW, lockBH,
                overLock ? (g_trackerLocked ? 0x3A1010 : 0x2A2A3A)
                         : (g_trackerLocked ? 0x2A0A0A : 0x1A1A2A), 3);
        DrawText2(g_trackerLocked ? "[L]" : "[ ]",
                  lbx + lockBW / 2, lby + 11,
                  g_trackerLocked ? 1 : 5, 1);   /* red when locked, grey when not */
    }

    /* Tracker content */
    int cy = ty + TRACKER_HANDLE_H + 2;

    /* AP Goal tracking — show at top of tracker if AP is connected */
    if (g_apConnected) {
        fnFont(6);
        if (g_apGoalComplete) {
            DrawText2("GOAL COMPLETE!", tx + 4, cy + 10, 2, 0); /* green */
            cy += 14;
        } else {
            /* Count completed progression quests vs total for goal scope */
            int goalQid = (g_apGoalScope >= 0 && g_apGoalScope <= 4) ?
                          g_goalQuestIds[g_apGoalScope] : g_goalQuestIds[4];
            const char* bossName = (g_apGoalScope >= 0 && g_apGoalScope <= 4) ?
                                   g_goalBossNames[g_apGoalScope] : "Baal";

            /* Count progression quests completed across all acts up to goal */
            int progDone = 0, progTotal = 0;
            for (int a = 0; a < 5; a++) {
                for (int q = 0; q < g_acts[a].num; q++) {
                    Quest* gq = &g_acts[a].quests[q];
                    if (gq->reward != REWARD_SKILL) continue; /* only progression */
                    if (gq->id == 0) continue;
                    progTotal++;
                    if (gq->id < MAX_QUEST_ID && g_questCompleted[g_currentDifficulty][gq->id])
                        progDone++;
                    if (gq->id == goalQid) goto goalCountDone;
                }
            }
            goalCountDone:;

            char goalLine[96];
            sprintf(goalLine, "GOAL: %s | %d/%d", bossName, progDone, progTotal);
            DrawText2(goalLine, tx + 4, cy + 10, 4, 0); /* gold */
            cy += 14;
        }
    }

    int curArea = GetCurrentArea();

    /* Determine which act we're in */
    int curAct = -1;
    if (curArea >= 1 && curArea <= 39) curAct = 0;
    else if (curArea >= 40 && curArea <= 74) curAct = 1;
    else if (curArea >= 75 && curArea <= 102) curAct = 2;
    else if (curArea >= 103 && curArea <= 108) curAct = 3;
    else if (curArea >= 109 && curArea <= 132) curAct = 4;

    if (curAct < 0) {
        fnFont(6);
        DrawText2("-- Quest Tracker --", tx + 4, cy + 10, 4, 0);
        return;
    }

    /* Difficulty indicator — ABOVE the act header (small font) */
    fnFont(6);
    {
        char diffBuf[32];
        sprintf(diffBuf, "[%s]", g_diffNames[g_currentDifficulty]);
        DrawText2(diffBuf, tx + 4, cy + 10, 8, 0);
        cy += 14;
    }

    /* Act header with completion count (largest font). */
    int actDone = 0, actTotal = 0;
    for (int q = 0; q < g_acts[curAct].num; q++) {
        Quest* cq = &g_acts[curAct].quests[q];
        int qid = cq->id;
        if (qid <= 0) continue;
        if (!IsQuestTypeActive((int)cq->type)) continue;
        actTotal++;
        if (qid < MAX_QUEST_ID && g_questCompleted[g_currentDifficulty][qid]) actDone++;
    }

    fnFont(0);
    char header[64];
    sprintf(header, "%s: %d/%d", g_acts[curAct].name, actDone, actTotal);
    DrawText2(header, tx + 4, cy + 16, 4, 0);
    cy += 22;
    fnFont(1);

    /* Checks block: compact one-liner per category. */
    {
        extern BOOL Bonus_IsCategoryEnabled(int cat);
        extern int  Bonus_GetSlotCount(int cat, int diff);
        extern int  Bonus_GetQuota(int cat);
        extern int  Bonus_CountFiredSetPickups(void);
        extern BOOL Bonus_IsGoldMilestoneFired(int idx);

        char buf[160];
        int written;

        /* Line 1: Quests, Skills, Collection (the per-character "main" progress signals). */
        int line1Cat = 0;
        char line1[160]; line1[0] = 0;
        if (g_apQuestStory || g_apQuestHunting || g_apQuestKillZones ||
            g_apQuestExploration || g_apQuestWaypoints || g_apQuestLevelMilestones)
        {
            int qd = CompletedQuests();
            int qa = TotalQuests();
            written = sprintf(buf, "Q %d/%d", qd, qa);
            (void)written;
            strcat(line1, buf);
            line1Cat++;
        }
        if (g_skillHuntingOn && g_poolCount > 0) {
            int unlocked = 0;
            for (int i = 0; i < g_poolCount; i++) if (g_pool[i].unlocked) unlocked++;
            if (line1Cat) strcat(line1, "  ");
            sprintf(buf, "S %d/%d", unlocked, g_poolCount);
            strcat(line1, buf);
            line1Cat++;
        }
        {
            int collGot = Coll_CountCollectedInRange(0, COLL_NUM_SLOTS - 1);
            if (line1Cat) strcat(line1, "  ");
            sprintf(buf, "C %d/%d", collGot, COLL_NUM_SLOTS);
            strcat(line1, buf);
            line1Cat++;
        }
        if (line1Cat > 0) {
            fnFont(6);
            DrawText2(line1, tx + 4, cy + 10, 5, 0);
            cy += 12;
        }

        /* Line 2: Bonus checks (sum across all enabled subcategories) + Zones (if zone-locking is on). */
        int line2Cat = 0;
        char line2[160]; line2[0] = 0;
        {
            BOOL anyBonus = FALSE;
            int bGot = 0, bAll = 0;
            /* scope counters to enabled difficulties. */
            int diffsEnabled = g_apDiffScope + 1;
            if (diffsEnabled < 1) diffsEnabled = 1;
            if (diffsEnabled > 3) diffsEnabled = 3;
            for (int c = 0; c < 4; c++) {
                if (!Bonus_IsCategoryEnabled(c)) continue;
                anyBonus = TRUE;
                for (int d = 0; d < diffsEnabled; d++) bGot += Bonus_GetSlotCount(c, d);
                bAll += Bonus_GetQuota(c) * diffsEnabled;
            }
            if (Bonus_IsCategoryEnabled(4)) {
                anyBonus = TRUE;
                bGot += Bonus_CountFiredSetPickups();
                bAll += 127;
            }
            if (Bonus_IsCategoryEnabled(5)) {
                anyBonus = TRUE;
                static const int gmPerDiff[3] = { 7, 5, 5 };
                int gmMax = 0;
                for (int d = 0; d < diffsEnabled; d++) gmMax += gmPerDiff[d];
                int got = 0;
                for (int i = 0; i < gmMax; i++) if (Bonus_IsGoldMilestoneFired(i)) got++;
                bGot += got;
                bAll += gmMax;
            }
            if (anyBonus) {
                /* K-suffix for big totals so it fits in the tracker. */
                if (bAll >= 1000)
                    sprintf(buf, "B %d/%d.%dK", bGot, bAll / 1000, (bAll % 1000) / 100);
                else
                    sprintf(buf, "B %d/%d", bGot, bAll);
                strcat(line2, buf);
                line2Cat++;
            }
        }
        if (g_zoneLockingOn) {
            int killed = 0, total = 0;
            int maxAct = GetMaxActForGoal();
            for (int act = 1; act <= maxAct; act++) {
                int numGates = g_actRegions[act - 1].num_gates;
                total += numGates;
                for (int gi = 0; gi < numGates; gi++) {
                    int slot = GateKey_SlotFromActGate(act, gi);
                    if (slot >= 0 && g_gateBossKilled[g_currentDifficulty][slot]) killed++;
                }
            }
            if (line2Cat) strcat(line2, "  ");
            sprintf(buf, "Z %d/%d", killed, total);
            strcat(line2, buf);
            line2Cat++;
        }
        if (line2Cat > 0) {
            fnFont(6);
            DrawText2(line2, tx + 4, cy + 10, 5, 0);
            cy += 14;
        }
    }

    /* Extra check categories (Cow / Merc / HF+Runes / NPC / Runeword / Cube). */
    {
        extern BOOL Extra_IsCategoryEnabled(int cat);
        extern int  Extra_GetSlotCount(int cat);
        extern int  Extra_CountFiredCategory(int cat);
        BOOL anyExtra = FALSE;
        int eGot = 0, eAll = 0;
        for (int c = 0; c < 6; c++) {
            if (!Extra_IsCategoryEnabled(c)) continue;
            anyExtra = TRUE;
            eGot += Extra_CountFiredCategory(c);
            eAll += Extra_GetSlotCount(c);
        }
        if (anyExtra) {
            char ebuf[32];
            if (eAll >= 1000)
                sprintf(ebuf, "E %d/%d.%dK", eGot, eAll / 1000, (eAll % 1000) / 100);
            else
                sprintf(ebuf, "E %d/%d", eGot, eAll);
            fnFont(6);
            DrawText2(ebuf, tx + 4, cy + 10, 5, 0);
            cy += 14;
        }
    }

    /* THE LOCKED FRONTIER. Until now the tracker said nothing at all about zone locking beyond the "Z x/y" counter. A player walked into a locked entrance, got bounced, and had no way to learn WHY or WHAT WOULD FIX IT without opening the book and finding the Zones page. Test C called this the biggest missing piece of information in the game. Two lines, and deliberately only two — the tracker is 180px wide and competes with the quest list for space: line 1 which region is shut and which key opens it line 2 what the player has to DO to get that key Line 2 is the part that matters, and it has three genuinely different answers that used to look identical from the player's side: - condition mode -> live progress ("Chests 6/8") - boss not killed -> the boss's name (post-shuffle name, via the same preload lookup the Zones page uses) - boss killed but key not here -> the check is sent and we are waiting on the multiworld. Nothing to do. That last state is why the block exists at all: it is the difference between "you have more work to do" and "go play something else while someone finds your key", and the player could not tell them apart. Rendered BEFORE the town early-out on purpose: town is exactly where you plan your next move. */
    if (g_zoneLockingOn) {
        extern int  KeyCond_Active(void);
        extern const char* KeyCond_TypeName(int diff, int slot);
        extern unsigned KeyCond_GetThreshold(int diff, int slot);
        extern unsigned KeyCond_GetProgress(int diff, int slot);

        int act   = curAct + 1;
        int diff  = g_currentDifficulty;
        int gates = g_actRegions[curAct].num_gates;

        /* The frontier is the FIRST gate with no key: every region past it is shut regardless of what else is open. */
        int blockedGate = -1;
        for (int g = 0; g < gates; g++) {
            int slot = GateKey_SlotFromActGate(act, g);
            if (slot >= 0 && !g_gateKeyReceived[diff][slot]) { blockedGate = g; break; }
        }

        if (blockedGate >= 0) {
            int slot = GateKey_SlotFromActGate(act, blockedGate);
            char line[96];

            fnFont(6);
            sprintf(line, "R%d LOCKED - Act%d Key%d",
                    blockedGate + 2, act, blockedGate + 1);
            DrawText2(line, tx + 4, cy + 10, 1, 0);      /* red */
            cy += 12;

            if (KeyCond_Active() && slot >= 0) {
                unsigned prog = KeyCond_GetProgress(diff, slot);
                unsigned thr  = KeyCond_GetThreshold(diff, slot);
                if (prog > thr) prog = thr;
                sprintf(line, "  %s %u/%u", KeyCond_TypeName(diff, slot),
                        (unsigned)prog, (unsigned)thr);
                DrawText2(line, tx + 4, cy + 10, 8, 0);  /* orange */
                cy += 14;
            } else if (slot >= 0 && g_gateBossKilled[diff][slot]) {
                DrawText2("  sent - awaiting key", tx + 4, cy + 10, 5, 0); /* grey */
                cy += 14;
            } else {
                int preload_id = g_actPreload[curAct][diff];
                const GateSpawnDef* gd = Preload_GetGate(act, preload_id, blockedGate);
                const char* boss = gd ? Preload_SuperUniqueName(gd->base_su, gd->label) : NULL;
                if (boss) sprintf(line, "  Kill: %s", boss);
                else      sprintf(line, "  Kill the gate boss");
                DrawText2(line, tx + 4, cy + 10, 8, 0);  /* orange */
                cy += 14;
            }
        } else if (Preload_GetZoneRegion(act, curArea) > 0) {
            /* Whole act is open. Say so once, quietly — an empty space here reads as "the tracker is broken", not as "nothing is locked". */
            fnFont(6);
            DrawText2("Act fully unlocked", tx + 4, cy + 10, 2, 0); /* green */
            cy += 14;
        }
    }

    if (IsTown(curArea)) return;

    /* Show waypoint quest for current area (if any) */
    for (int q = 0; q < g_acts[curAct].num && IsQuestTypeActive(QTYPE_WAYPOINT); q++) {
        Quest *quest = &g_acts[curAct].quests[q];
        if (quest->type != QTYPE_WAYPOINT) continue;
        /* was a hand-copied SECOND waypoint->area table, and it had drifted: WP 13 said area 42 (Dry Hills) and WP 14 said 43 (Far Oasis), i.e. */
        int wpArea = WaypointToArea(quest->param);
        if (wpArea == curArea) {
            char line[96];
            int qid = quest->id;
            BOOL done = (qid > 0 && qid < MAX_QUEST_ID && g_questCompleted[g_currentDifficulty][qid]);
            if (done) {
                sprintf(line, "Waypoint [DONE]");
                DrawText2(line, tx + 4, cy + 12, 2, 0);
            } else {
                sprintf(line, "Waypoint [NOT FOUND]");
                DrawText2(line, tx + 4, cy + 12, 8, 0);
            }
            cy += 16;
            break;
        }
    }

    /* Show kill quest for current area */
    for (int q = 0; q < g_acts[curAct].num && IsQuestTypeActive(QTYPE_KILL); q++) {
        Quest *quest = &g_acts[curAct].quests[q];
        if (quest->type == QTYPE_KILL && quest->param == curArea) {
            char line[96];
            int qid = quest->id;
            BOOL done = (qid > 0 && qid < MAX_QUEST_ID && g_questCompleted[g_currentDifficulty][qid]);
            if (done) {
                sprintf(line, "%s [DONE]", quest->name);
                DrawText2(line, tx + 4, cy + 12, 2, 0);
            } else {
                sprintf(line, "%s (%d/%d)", quest->name, quest->killCount, quest->killReq);
                DrawText2(line, tx + 4, cy + 12, 0, 0);
            }
            cy += 16;
            break;
        }
    }

    /* Show next uncomplete SuperUnique hunt for current act */
    for (int q = 0; q < g_acts[curAct].num && IsQuestTypeActive(QTYPE_SUPERUNIQUE); q++) {
        Quest* quest = &g_acts[curAct].quests[q];
        if (quest->type != QTYPE_SUPERUNIQUE) continue;
        int qid = quest->id;
        if (qid > 0 && qid < MAX_QUEST_ID && !g_questCompleted[g_currentDifficulty][qid]) {
            fnFont(1);
            char line[160];
            /* If name already starts with "Hunt: ", show as-is; else add prefix */
            if (strncmp(quest->name, "Hunt: ", 6) == 0)
                sprintf(line, "%s", quest->name);
            else
                sprintf(line, "Hunt: %s", quest->name);
            /* Say WHERE, not just who. With the shuffle off that is the hunt's
             * own verified zone; with it on, the identity stands on some other
             * boss's spot, so the place to name is THAT boss's zone. The host
             * always has a hunt of its own to read the zone from, because the
             * shuffle only swaps within one act/region bucket. */
            {
                const char* at = SUShuffle_HostFor(quest->param);
                int qz = QuestZone_For(qid);
                if (at && at[0]) {
                    for (int am = 0; am < 5; am++) {
                        for (int qm = 0; qm < g_acts[am].num; qm++) {
                            Quest* hq = &g_acts[am].quests[qm];
                            if (hq->type != QTYPE_SUPERUNIQUE) continue;
                            if (!strstr(hq->name, at)) continue;
                            int hz = QuestZone_For(hq->id);
                            if (hz >= 1) qz = hz;
                            am = 5; break;
                        }
                    }
                }
                /* Preload_ZoneName indexes by area id directly; see the note in
                 * d2arch_editor.c about the off-by-one this replaces. */
                const char* zn = Preload_ZoneName(qz);
                char tail[100];

                /* NAME THE MONSTER. "Bishibosh's spot" reads as geography, and
                 * players hunted the name in the check instead -- which under
                 * the shuffle is standing somewhere else entirely, wearing the
                 * wrong name. What they can act on is the name over its head,
                 * so say that, and say "here" once they are in the zone. */
                const char* kill = SUShuffle_KillNameFor(quest->param);
                BOOL here = (qz >= 1 && qz == GetCurrentArea());

                if (kill && kill[0] && here)
                    _snprintf(tail, sizeof(tail), " - kill %s, here", kill);
                else if (kill && kill[0] && zn && zn[0])
                    _snprintf(tail, sizeof(tail), " - kill %s in %s", kill, zn);
                else if (kill && kill[0])
                    _snprintf(tail, sizeof(tail), " - kill %s", kill);
                else if (zn && zn[0])
                    _snprintf(tail, sizeof(tail), here ? " - %s (here)" : " - %s", zn);
                else
                    tail[0] = 0;
                tail[sizeof(tail) - 1] = 0;
                if (tail[0])
                    strncat(line, tail, sizeof(line) - strlen(line) - 1);
            }
            DrawText2(line, tx + 4, cy + 12, 8, 0); /* orange */
            cy += 16;
            break; /* Only show first uncomplete */
        }
    }

    /* Show next level milestone */
    if (IsQuestTypeActive(QTYPE_LEVEL)) {
        void* p = Player();
        if (p && fnGetStat) {
            int level = (int)fnGetStat(p, 12, 0);
            for (int a2 = 0; a2 < 5; a2++)
                for (int q2 = 0; q2 < g_acts[a2].num; q2++) {
                    Quest* quest = &g_acts[a2].quests[q2];
                    if (quest->type != QTYPE_LEVEL) continue;
                    int qid = quest->id;
                    if (qid > 0 && qid < MAX_QUEST_ID && !g_questCompleted[g_currentDifficulty][qid]) {
                        fnFont(1);
                        char line[96];
                        sprintf(line, "Level: %d/%d", level, quest->param);
                        DrawText2(line, tx + 4, cy + 12, 4, 0); /* gold */
                        cy += 16;
                        goto trackerLevelDone;
                    }
                }
            trackerLevelDone:;
        }
    }

    /* Show next incomplete story/area/boss quest for this act */
    for (int q = 0; q < g_acts[curAct].num; q++) {
        Quest *quest = &g_acts[curAct].quests[q];
        if (quest->id == 0) continue;
        if (!IsQuestTypeActive(quest->type)) continue;
        int qid = quest->id;
        if (qid > 0 && qid < MAX_QUEST_ID && g_questCompleted[g_currentDifficulty][qid]) continue;
        if (quest->type == QTYPE_KILL || quest->type == QTYPE_WAYPOINT || quest->type == QTYPE_SUPERUNIQUE || quest->type == QTYPE_LEVEL) continue;
        char line[96];
        sprintf(line, "Goal: %s", quest->name);
        DrawText2(line, tx + 4, cy + 12, 4, 0);
        cy += 16;
        break;
    }
}
