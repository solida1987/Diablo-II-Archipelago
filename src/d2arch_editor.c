/* Forward declaration — IsQuestTypeActive is defined in d2arch_questlog.c which is #included AFTER this file, but we need it for the quest-book filter (respecting Hunting/KillZn/Explore/Waypnt/Levels toggles). */
static BOOL IsQuestTypeActive(int questType);
/* editor view toggles. Persisted to d2arch.ini [Editor] so they survive a relog; loaded once, lazily, on the first draw that needs them. */
static int  g_edHideLocked  = 0;
static int  g_edColorblind  = 0;
static BOOL g_edViewLoaded  = FALSE;

static void EdLoadViewToggles(void) {
    if (g_edViewLoaded) return;
    g_edViewLoaded = TRUE;
    char ini[MAX_PATH], buf[16];
    GetArchDir(ini, MAX_PATH);
    strcat(ini, "d2arch.ini");
    GetPrivateProfileStringA("Editor", "HideLocked", "0", buf, 15, ini);
    g_edHideLocked = atoi(buf) ? 1 : 0;
    GetPrivateProfileStringA("Editor", "Colorblind", "0", buf, 15, ini);
    g_edColorblind = atoi(buf) ? 1 : 0;
}

static void EdSaveViewToggle(const char* key, int val) {
    char ini[MAX_PATH];
    GetArchDir(ini, MAX_PATH);
    strcat(ini, "d2arch.ini");
    WritePrivateProfileStringA("Editor", key, val ? "1" : "0", ini);
}

static void EdCelFree(void** pSlot);

/* F1 BOOK PAGE LAYOUT Pages are addressed by g_editorPage (raw int). */
#define PAGE_SKILLS    0
#define PAGE_QUESTS    1
#define PAGE_AP        2
#define PAGE_SETS_1    3
#define PAGE_SETS_2    4
#define PAGE_RUNES     5
#define PAGE_GEMS      6
#define PAGE_SPECIALS  7
#define PAGE_LOGBOOK   8
#define PAGE_OVERVIEW  9
#define PAGE_ZONES     10
#define PAGE_STATS     11

typedef struct {
    int rawPageIdx;
    const wchar_t* label;
    const char* iniName;       /* prefix for per-tab INI keys */
    const char* celName;       /* optional DC6 background in data\global\ui\SPELLS */
    int conditional;           /* 0=always, 1=only when g_zoneLockingOn */
} EditorTab;

static const EditorTab g_tabs[] = {
    { PAGE_SKILLS,   L"Skills",   "Skills",   "tab_skills",   0 },
    { PAGE_OVERVIEW, L"Overview", "Overview", "tab_overview", 0 },
    { PAGE_QUESTS,   L"Quests",   "Quests",   "tab_quests",   0 },
    { PAGE_ZONES,    L"Zones",    "Zones",    "tab_zones",    1 },
    { PAGE_STATS,    L"Stats",    "Stats",    "tab_stats",    0 },
    { PAGE_SETS_1,   L"Sets I",   "Sets1",    "tab_sets1",    0 },
    { PAGE_SETS_2,   L"Sets II",  "Sets2",    "tab_sets2",    0 },
    { PAGE_RUNES,    L"Runes",    "Runes",    "tab_runes",    0 },
    { PAGE_GEMS,     L"Gems",     "Gems",     "tab_gems",     0 },
    { PAGE_SPECIALS, L"Special",  "Special",  "tab_special",  0 },
    { PAGE_LOGBOOK,  L"Logbook",  "Logbook",  "tab_logbook",  0 },
    { PAGE_AP,       L"AP",       "AP",       "tab_ap",       0 },
};
#define NUM_EDITOR_TABS ((int)(sizeof(g_tabs) / sizeof(g_tabs[0])))

/* Map raw g_editorPage to tab index. */
static int Editor_TabFromPage(int rawPage) {
    for (int t = 0; t < NUM_EDITOR_TABS; t++) {
        if (g_tabs[t].rawPageIdx == rawPage) return t;
    }
    return -1;
}

/* Test whether a tab is currently visible (non-conditional, or conditional and the relevant feature is enabled). */
static BOOL Editor_TabVisible(int tabIdx) {
    if (tabIdx < 0 || tabIdx >= NUM_EDITOR_TABS) return FALSE;
    const EditorTab* t = &g_tabs[tabIdx];
    if (t->conditional == 1 && !g_zoneLockingOn) return FALSE;
    return TRUE;
}

/* Walk to the next/previous visible tab, wrapping at boundaries. */
static int Editor_NextVisibleTab(int curTab, int dir) {
    int cur = curTab;
    for (int step = 0; step < NUM_EDITOR_TABS; step++) {
        cur = cur + dir;
        if (cur < 0) cur = NUM_EDITOR_TABS - 1;
        if (cur >= NUM_EDITOR_TABS) cur = 0;
        if (Editor_TabVisible(cur)) return cur;
    }
    return curTab;
}

/* Render the top tab bar. */
static int Editor_DrawTabBar(int mx, int my, BOOL clicked) {
    /* the tab row is laid out from these, and every tab may override its rect in the INI. */
    #define EDITOR_TAB_LAYOUT_VERSION 2

    static int s_loaded = 0;
    /* Defaults match the art: the row sits at y=38 and spans x=43..657, which is the width of the book's header band. */
    static int s_barX = 43, s_barY = 38;
    static int s_barW = 614, s_barH = 22;
    static int s_tabX[NUM_EDITOR_TABS];
    static int s_tabY[NUM_EDITOR_TABS];
    static int s_tabW[NUM_EDITOR_TABS];
    static int s_tabH[NUM_EDITOR_TABS];
    static int s_tabTX[NUM_EDITOR_TABS];   /* text X offset from tab top-left */
    static int s_tabTY[NUM_EDITOR_TABS];   /* text Y offset from tab top-left */
    static void* s_tabCel[NUM_EDITOR_TABS] = { NULL };
    static int s_tabCelLoaded = -1;

    INI_HOT_RELOAD_GUARD(s_loaded);
    if (!s_loaded) {
        s_loaded = 1;
        char ini[MAX_PATH]; GetArchDir(ini, MAX_PATH); strcat(ini, "d2arch.ini");
        s_barX = GetPrivateProfileIntA("EditorTabs", "BarX",  43,  ini);
        s_barY = GetPrivateProfileIntA("EditorTabs", "BarY",  38,  ini);
        s_barW = GetPrivateProfileIntA("EditorTabs", "BarW",  614, ini);
        s_barH = GetPrivateProfileIntA("EditorTabs", "BarH",  22,  ini);

        int iniLayout = GetPrivateProfileIntA("EditorTabs", "LayoutVersion", 1, ini);
        BOOL useStored = (iniLayout >= EDITOR_TAB_LAYOUT_VERSION);

        /* Auto-layout: share BarW out by label length rather than in equal slices, so "Overview" gets room and "AP" does not hog it. */
        int visCount = 0;
        int visMap[NUM_EDITOR_TABS];
        int units = 0;
        const int PAD_UNITS = 2;                 /* padding, in label characters */
        for (int t = 0; t < NUM_EDITOR_TABS; t++) {
            if (!Editor_TabVisible(t)) continue;
            visMap[visCount++] = t;
            units += (int)wcslen(g_tabs[t].label) + PAD_UNITS;
        }
        if (units <= 0) units = 1;

        /* Walk the visible tabs left to right, handing each its share and carrying the running edge so rounding never opens a gap or an overlap between two neighbours. */
        int autoX[NUM_EDITOR_TABS] = {0}, autoW[NUM_EDITOR_TABS] = {0};
        int edge = s_barX, used = 0;
        for (int v = 0; v < visCount; v++) {
            int t = visMap[v];
            used += (int)wcslen(g_tabs[t].label) + PAD_UNITS;
            int nextEdge = (v == visCount - 1) ? (s_barX + s_barW)
                                               : (s_barX + (s_barW * used) / units);
            autoX[t] = edge;
            autoW[t] = nextEdge - edge;
            edge = nextEdge;
        }

        for (int t = 0; t < NUM_EDITOR_TABS; t++) {
            int defX = autoX[t] ? autoX[t] : s_barX;
            int defY = s_barY;
            int defW = autoW[t] ? autoW[t] : (s_barW / (visCount ? visCount : 1));
            int defH = s_barH;
            int defTX = defW / 2;       /* centered relative to W */
            int defTY = defH - 6;       /* baseline near bottom edge */

            if (!useStored) {
                s_tabX[t] = defX; s_tabY[t] = defY;
                s_tabW[t] = defW; s_tabH[t] = defH;
                s_tabTX[t] = defTX; s_tabTY[t] = defTY;
                continue;
            }
            char k[64];
            sprintf(k, "%sX",        g_tabs[t].iniName); s_tabX[t]  = GetPrivateProfileIntA("EditorTabs", k, defX,  ini);
            sprintf(k, "%sY",        g_tabs[t].iniName); s_tabY[t]  = GetPrivateProfileIntA("EditorTabs", k, defY,  ini);
            sprintf(k, "%sW",        g_tabs[t].iniName); s_tabW[t]  = GetPrivateProfileIntA("EditorTabs", k, defW,  ini);
            sprintf(k, "%sH",        g_tabs[t].iniName); s_tabH[t]  = GetPrivateProfileIntA("EditorTabs", k, defH,  ini);
            sprintf(k, "%sTextOffX", g_tabs[t].iniName); s_tabTX[t] = GetPrivateProfileIntA("EditorTabs", k, defTX, ini);
            sprintf(k, "%sTextOffY", g_tabs[t].iniName); s_tabTY[t] = GetPrivateProfileIntA("EditorTabs", k, defTY, ini);
        }
        Log("EDITOR TABS: %d visible, layout=%s (ini v%d, code v%d), row %d..%d y=%d\n",
            visCount, useStored ? "from INI" : "auto (INI too old)",
            iniLayout, EDITOR_TAB_LAYOUT_VERSION, s_barX, s_barX + s_barW, s_barY);
    }
    if (!fnRect || !fnText || !fnFont) return -1;

    if (s_tabCelLoaded != g_celSessionGen) {
        s_tabCelLoaded = g_celSessionGen;
        for (int t = 0; t < NUM_EDITOR_TABS; t++) EdCelFree(&s_tabCel[t]);
        if (fnCelLoad) {
            /* the tab's WORD is painted into its DC6; there is no text drawn over it. */
            char exeDir[MAX_PATH] = {0};
            GetModuleFileNameA(NULL, exeDir, MAX_PATH);
            { char* sl = strrchr(exeDir, '\\'); if (sl) *(sl + 1) = 0; }
            for (int t = 0; t < NUM_EDITOR_TABS; t++) {
                char rel[128], full[MAX_PATH];
                sprintf(rel, "data\\global\\ui\\SPELLS\\%s", g_tabs[t].celName);
                _snprintf(full, sizeof(full), "%s%s.DC6", exeDir, rel);
                full[sizeof(full) - 1] = 0;
                if (GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES) {
                    s_tabCel[t] = NULL;
                    Log("EDITOR TABS: no art for '%s' — drawing its name as text\n",
                        g_tabs[t].celName);
                    continue;
                }
                s_tabCel[t] = fnCelLoad(rel, 0);
            }
        }
    }

    int currentTab = Editor_TabFromPage(g_editorPage);
    int clickedTab = -1;

    fnFont(6);
    for (int t = 0; t < NUM_EDITOR_TABS; t++) {
        if (!Editor_TabVisible(t)) continue;
        int tx  = s_tabX[t];
        int ty  = s_tabY[t];
        int tw  = s_tabW[t];
        int th  = s_tabH[t];
        int tx2 = tx + tw - 2;
        BOOL isActive = (t == currentTab);
        BOOL hover    = InRect(mx, my, tx, ty, tw - 2, th);

        BOOL usedTabArt = FALSE;
        if (s_tabCel[t]) {
            __try {
                DrawCel(s_tabCel[t], isActive ? 1 : 0, tx, ty + th);
                usedTabArt = TRUE;
            } __except(1) {
                usedTabArt = FALSE;
            }
        }

        if (!usedTabArt) {
            /* Fallback: keep the book visible if the DC6 tab skin is missing. */
            if (isActive) {
                for (int k = 0; k < 3; k++)
                    fnRect(tx, ty, tx2, ty + th, 4, 5);
            } else {
                if (hover)
                    fnRect(tx, ty + th - 3, tx2, ty + th, 7, 5);
            }
        } else if (hover && !isActive) {
            fnRect(tx + 2, ty + 2, tx2 - 2, ty + th - 2, 7, 5);
        }

        /* Border: gold on active, tan on inactive. */
        int bc = isActive ? 4 : 7;
        fnRect(tx,      ty,         tx2,    ty + 1,  bc, 5);
        fnRect(tx,      ty,         tx + 1, ty + th, bc, 5);
        fnRect(tx2 - 1, ty,         tx2,    ty + th, bc, 5);
        if (!isActive)
            fnRect(tx, ty + th - 1, tx2,    ty + th, bc, 5);

        /* a tab with no DC6 gets its name written on it. */
        if (!usedTabArt) {
            fnFont(6);
            fnText((wchar_t*)g_tabs[t].label, tx + s_tabTX[t], ty + s_tabTY[t],
                   isActive ? 4 : 0, 1);
        }

        if (clicked && hover) {
            static DWORD _tabClk = 0; DWORD _tn = GetTickCount();
            if (_tn - _tabClk > 250) { _tabClk = _tn; clickedTab = t; }
        }
    }
    return clickedTab;
}

/* OVERVIEW PAGE (g_editorPage == PAGE_OVERVIEW) Logbook-style summary of every check category. */
static void Editor_RenderOverviewPage(int mx, int my, BOOL clicked) {
    if (!fnRect || !fnText || !fnFont) return;
    (void)mx; (void)my; (void)clicked;

    static int s_loaded = 0;
    static int s_titleX = 400, s_titleY = 60;
    static int s_lpX = 70,  s_lpY = 100;
    static int s_rpX = 410, s_rpY = 100;
    static int s_valOffX = 230;       /* X offset of right-aligned value column from rowX */
    static int s_rowH = 18;
    INI_HOT_RELOAD_GUARD(s_loaded);
    if (!s_loaded) {
        s_loaded = 1;
        char ini[MAX_PATH]; GetArchDir(ini, MAX_PATH); strcat(ini, "d2arch.ini");
        s_titleX  = GetPrivateProfileIntA("EditorOverview", "TitleX",   400, ini);
        s_titleY  = GetPrivateProfileIntA("EditorOverview", "TitleY",    60, ini);
        s_lpX     = GetPrivateProfileIntA("EditorOverview", "LeftX",     70, ini);
        s_lpY     = GetPrivateProfileIntA("EditorOverview", "LeftY",    100, ini);
        s_rpX     = GetPrivateProfileIntA("EditorOverview", "RightX",   410, ini);
        s_rpY     = GetPrivateProfileIntA("EditorOverview", "RightY",   100, ini);
        s_valOffX = GetPrivateProfileIntA("EditorOverview", "ValueOffX",230, ini);
        s_rowH    = GetPrivateProfileIntA("EditorOverview", "RowH",      18, ini);
    }

    fnFont(0);
    fnText(L"Overview", s_titleX, s_titleY, 4, 1);

    /* Helper macro: draw a section header in gold, bump y. */
    #define OV_HEADER(x, y, text) do { fnFont(0); fnText((wchar_t*)(text), (x), (y), 4, 0); (y) += s_rowH + 2; } while (0)
    /* Helper macro: draw a label + right-aligned "X / Y" value, bump y. */
    #define OV_ROW(x, y, label, gotV, totV, doneColor) do { \
            wchar_t _wb[64]; wsprintfW(_wb, L"%s", (label)); \
            fnFont(6); \
            fnText(_wb, (x) + 8, (y), 0, 0); \
            wsprintfW(_wb, L"%d / %d", (gotV), (totV)); \
            int _vc = ((gotV) >= (totV) && (totV) > 0) ? 2 : (doneColor); \
            fnText(_wb, (x) + s_valOffX, (y), _vc, 0); \
            (y) += s_rowH; \
        } while (0)

    extern int  Bonus_GetSlotCount(int cat, int diff);
    extern int  Bonus_GetQuota(int cat);
    extern BOOL Bonus_IsCategoryEnabled(int cat);
    extern int  Bonus_CountFiredSetPickups(void);
    extern BOOL Bonus_IsGoldMilestoneFired(int idx);

    int totalDone = 0, totalAll = 0;

    /* LEFT PAGE: Quests / Skills / Bonus Checks */
    int lx = s_lpX, ly = s_lpY;

    /* Quests section — only show enabled subtypes individually. */
    if (g_apQuestStory || g_apQuestHunting || g_apQuestKillZones ||
        g_apQuestExploration || g_apQuestWaypoints || g_apQuestLevelMilestones)
    {
        OV_HEADER(lx, ly, L"QUESTS");
        int qd = CompletedQuests();
        int qa = TotalQuests();
        OV_ROW(lx, ly, L"All quests", qd, qa, 0);
        totalDone += qd; totalAll += qa;
        ly += 4;
    }

    /* Skills (only meaningful if Skill Hunting is on). */
    if (g_skillHuntingOn && g_poolCount > 0) {
        int unlocked = 0;
        for (int i = 0; i < g_poolCount; i++) if (g_pool[i].unlocked) unlocked++;
        OV_HEADER(lx, ly, L"SKILLS");
        OV_ROW(lx, ly, L"Unlocked", unlocked, g_poolCount, 0);
        totalDone += unlocked; totalAll += g_poolCount;
        ly += 4;
    }

    /* Bonus checks — only show categories that are enabled. */
    {
        BOOL anyBonus = FALSE;
        for (int c = 0; c < 6; c++) if (Bonus_IsCategoryEnabled(c)) { anyBonus = TRUE; break; }
        if (anyBonus) {
            OV_HEADER(lx, ly, L"BONUS CHECKS");
            static const wchar_t* bxNames[6] = {
                L"Shrines", L"Urns", L"Barrels", L"Chests",
                L"Set Pickups", L"Gold Milestones"
            };
            int diffsEnabled = g_apDiffScope + 1;
            if (diffsEnabled < 1) diffsEnabled = 1;
            if (diffsEnabled > 3) diffsEnabled = 3;
            for (int c = 0; c < 4; c++) {
                if (!Bonus_IsCategoryEnabled(c)) continue;
                int got = 0;
                for (int d = 0; d < diffsEnabled; d++) got += Bonus_GetSlotCount(c, d);
                int max = Bonus_GetQuota(c) * diffsEnabled;
                OV_ROW(lx, ly, bxNames[c], got, max, 0);
                totalDone += got; totalAll += max;
            }
            if (Bonus_IsCategoryEnabled(4)) {
                int got = Bonus_CountFiredSetPickups();
                OV_ROW(lx, ly, bxNames[4], got, 127, 0);
                totalDone += got; totalAll += 127;
            }
            if (Bonus_IsCategoryEnabled(5)) {
                /* Per-difficulty milestone counts: Normal=7, NM=5, Hell=5. */
                static const int gmPerDiff[3] = { 7, 5, 5 };
                int gmMax = 0;
                for (int d = 0; d < diffsEnabled; d++) gmMax += gmPerDiff[d];
                int got = 0;
                for (int i = 0; i < gmMax; i++) if (Bonus_IsGoldMilestoneFired(i)) got++;
                OV_ROW(lx, ly, bxNames[5], got, gmMax, 0);
                totalDone += got; totalAll += gmMax;
            }
        }
    }

    /* EXTRA CHECKS section. */
    {
        extern BOOL Extra_IsCategoryEnabled(int cat);
        extern int  Extra_GetSlotCount(int cat);
        extern int  Extra_CountFiredCategory(int cat);
        BOOL anyExtra = FALSE;
        for (int c = 0; c < 6; c++) if (Extra_IsCategoryEnabled(c)) { anyExtra = TRUE; break; }
        if (anyExtra) {
            OV_HEADER(lx, ly, L"EXTRA CHECKS");
            static const wchar_t* exNames[6] = {
                L"Cow Level", L"Mercenary", L"HF + Runes",
                L"NPC Talk",  L"Runewords", L"Cube Recipes"
            };
            for (int c = 0; c < 6; c++) {
                if (!Extra_IsCategoryEnabled(c)) continue;
                int got = Extra_CountFiredCategory(c);
                int max = Extra_GetSlotCount(c);
                OV_ROW(lx, ly, exNames[c], got, max, 0);
                totalDone += got; totalAll += max;
            }
        }
    }

    /* RIGHT PAGE: Collection / Zones / Totals */
    int rx = s_rpX, ry = s_rpY;

    /* Collection — pulled from Coll module. */
    {
        OV_HEADER(rx, ry, L"COLLECTION");
        int setsGot   = Coll_CountCollectedInRange(COLL_SLOT_SETS_BASE,
                            COLL_SLOT_SETS_BASE + COLL_NUM_SET_PIECES - 1);
        int runesGot  = Coll_CountCollectedInRange(COLL_SLOT_RUNES_BASE,
                            COLL_SLOT_RUNES_BASE + COLL_NUM_RUNES - 1);
        int gemsGot   = Coll_CountCollectedInRange(COLL_SLOT_GEMS_BASE,
                            COLL_SLOT_GEMS_BASE + COLL_NUM_GEMS - 1);
        int specGot   = Coll_CountCollectedInRange(COLL_SLOT_SPECIALS_BASE,
                            COLL_SLOT_SPECIALS_BASE + COLL_NUM_SPECIALS - 1);
        OV_ROW(rx, ry, L"Set pieces", setsGot,  COLL_NUM_SET_PIECES, 0);
        OV_ROW(rx, ry, L"Runes",      runesGot, COLL_NUM_RUNES,      0);
        OV_ROW(rx, ry, L"Gems",       gemsGot,  COLL_NUM_GEMS,       0);
        OV_ROW(rx, ry, L"Specials",   specGot,  COLL_NUM_SPECIALS,   0);
        ry += 4;
        totalDone += setsGot + runesGot + gemsGot + specGot;
        totalAll  += COLL_NUM_SET_PIECES + COLL_NUM_RUNES + COLL_NUM_GEMS + COLL_NUM_SPECIALS;
    }

    /* Zones — only when zone-locking is on. */
    if (g_zoneLockingOn) {
        OV_HEADER(rx, ry, L"ZONES");
        static const wchar_t* dNames[3] = { L"Normal", L"Nightmare", L"Hell" };
        int zoneTotalDone = 0, zoneTotalAll = 0;
        for (int d = 0; d <= g_apDiffScope && d < 3; d++) {
            int killed = 0, total = 0;
            int maxAct = GetMaxActForGoal();
            for (int act = 1; act <= maxAct; act++) {
                int numGates = g_actRegions[act - 1].num_gates;
                total += numGates;
                for (int gi = 0; gi < numGates; gi++) {
                    int slot = GateKey_SlotFromActGate(act, gi);
                    if (slot >= 0 && g_gateBossKilled[d][slot]) killed++;
                }
            }
            OV_ROW(rx, ry, dNames[d], killed, total, 0);
            zoneTotalDone += killed; zoneTotalAll += total;
        }
        ry += 4;
        totalDone += zoneTotalDone; totalAll += zoneTotalAll;
    }

    /* Totals at the bottom of the right page. */
    {
        OV_HEADER(rx, ry, L"TOTAL");
        wchar_t wb[64];
        wsprintfW(wb, L"%d / %d", totalDone, totalAll);
        fnFont(0);
        fnText(L"Checks", rx + 8, ry, 0, 0);
        fnText(wb, rx + s_valOffX, ry,
               (totalAll > 0 && totalDone == totalAll) ? 2 : 4, 0);
        ry += s_rowH + 2;
        wsprintfW(wb, L"%s",
                  g_apGoalComplete ? L"GOAL COMPLETE!" : L"Goal: In Progress");
        fnText(wb, rx + 8, ry, g_apGoalComplete ? 2 : 9, 0);
    }

    #undef OV_HEADER
    #undef OV_ROW
}

/* GetUnitStat behind a guard: a stat id the build does not know about, or a unit mid-teardown, must not take the whole book down with it. */
static int SafeStat(void* pUnit, int statId) {
    int v = 0;
    __try { v = fnGetStat(pUnit, statId, 0); } __except(EXCEPTION_EXECUTE_HANDLER) { v = 0; }
    return v;
}

/* HIDDEN STATS PAGE (g_editorPage == PAGE_STATS) The character screen shows a curated handful of numbers. */
static void Editor_RenderStatsPage(int mx, int my, BOOL clicked) {
    if (!fnRect || !fnText || !fnFont) return;
    (void)mx; (void)my; (void)clicked;

    static int s_loaded = 0;
    static int s_titleX = 400, s_titleY = 60;
    static int s_lpX = 70,  s_lpY = 100;
    static int s_rpX = 410, s_rpY = 100;
    static int s_valOffX = 210;
    static int s_rowH = 17;
    static int s_footY = 400;      /* footnote baseline, under the right column */
    INI_HOT_RELOAD_GUARD(s_loaded);
    if (!s_loaded) {
        s_loaded = 1;
        char ini[MAX_PATH]; GetArchDir(ini, MAX_PATH); strcat(ini, "d2arch.ini");
        s_titleX  = GetPrivateProfileIntA("EditorStats", "TitleX",   400, ini);
        s_titleY  = GetPrivateProfileIntA("EditorStats", "TitleY",    60, ini);
        s_lpX     = GetPrivateProfileIntA("EditorStats", "LeftX",     70, ini);
        s_lpY     = GetPrivateProfileIntA("EditorStats", "LeftY",    100, ini);
        s_rpX     = GetPrivateProfileIntA("EditorStats", "RightX",   410, ini);
        s_rpY     = GetPrivateProfileIntA("EditorStats", "RightY",   100, ini);
        s_valOffX = GetPrivateProfileIntA("EditorStats", "ValueOffX",210, ini);
        s_rowH    = GetPrivateProfileIntA("EditorStats", "RowH",      17, ini);
        s_footY   = GetPrivateProfileIntA("EditorStats", "FootY",    400, ini);
    }

    /* no page title. It was drawn at the same height as the tab row and ran across Runes and Gems. The tab you are standing on already says where you are, and both columns carry their own headings, so the line was paying for itself with a collision. */
    (void)s_titleX; (void)s_titleY;

    void* p = Player();
    if (!p || !fnGetStat) {
        fnFont(6);
        fnText(L"No character loaded.", s_lpX + 8, s_lpY, 1, 0);
        return;
    }

    #define ST(id) (SafeStat(p, (id)))
    #define SP_HEADER(x, y, text) do { \
            fnFont(0); fnText((wchar_t*)(text), (x), (y), 4, 0); (y) += s_rowH + 2; \
        } while (0)
    #define SP_ROW(x, y, label, fmt, val, col) do { \
            wchar_t _wb[80]; \
            fnFont(6); fnText((wchar_t*)(label), (x) + 8, (y), 0, 0); \
            wsprintfW(_wb, (fmt), (val)); \
            fnText(_wb, (x) + s_valOffX, (y), (col), 0); \
            (y) += s_rowH; \
        } while (0)
    #define SP_ROW2(x, y, label, fmt, a, b, col) do { \
            wchar_t _wb[80]; \
            fnFont(6); fnText((wchar_t*)(label), (x) + 8, (y), 0, 0); \
            wsprintfW(_wb, (fmt), (a), (b)); \
            fnText(_wb, (x) + s_valOffX, (y), (col), 0); \
            (y) += s_rowH; \
        } while (0)

    /* LEFT PAGE */
    int lx = s_lpX, ly = s_lpY;

    SP_HEADER(lx, ly, L"Finding things");
    {
        int mf = ST(80), gf = ST(79);
        SP_ROW(lx, ly, L"Magic Find", L"%d%%", mf, mf > 0 ? 2 : 0);
        SP_ROW(lx, ly, L"Gold Find",  L"%d%%", gf, gf > 0 ? 2 : 0);
    }
    ly += 6;

    /* The character screen already subtracts the difficulty penalty, so it answers "what do I have here" and never "how much headroom will Hell need". */
    SP_HEADER(lx, ly, L"Resistance   raw -> now / cap");
    {
        int pen = (g_currentDifficulty == 1) ? 40 : (g_currentDifficulty == 2) ? 100 : 0;
        struct { const wchar_t* name; int rid; int mid; } R[4] = {
            { L"Fire", 39, 40 }, { L"Lightning", 41, 42 },
            { L"Cold", 43, 44 }, { L"Poison",    45, 46 },
        };
        for (int i = 0; i < 4; i++) {
            int raw = ST(R[i].rid);
            int cap = 75 + ST(R[i].mid);
            int eff = raw - pen;
            if (eff > cap) eff = cap;
            wchar_t wb[80];
            fnFont(6);
            fnText((wchar_t*)R[i].name, lx + 8, ly, 0, 0);
            wsprintfW(wb, L"%d -> %d / %d", raw, eff, cap);
            fnText(wb, lx + s_valOffX - 30, ly, eff >= cap ? 2 : (eff < 0 ? 1 : 0), 0);
            ly += s_rowH;
        }
        wchar_t pb[64];
        fnFont(6);
        wsprintfW(pb, L"difficulty penalty  -%d", pen);
        fnText(pb, lx + 8, ly, 5, 0);
        ly += s_rowH + 6;
    }

    SP_HEADER(lx, ly, L"Damage taken");
    SP_ROW(lx, ly, L"Physical reduced", L"%d%%", ST(36), ST(36) > 0 ? 2 : 0);
    SP_ROW(lx, ly, L"Magical reduced",  L"%d%%", ST(37), ST(37) > 0 ? 2 : 0);
    ly += 6;

    /* Speed lives on the LEFT page. */
    SP_HEADER(lx, ly, L"Speed");
    SP_ROW(lx, ly, L"Faster Cast",         L"%d%%", ST(105), ST(105) > 0 ? 2 : 0);
    SP_ROW(lx, ly, L"Faster Hit Recovery", L"%d%%", ST(99),  ST(99)  > 0 ? 2 : 0);
    SP_ROW(lx, ly, L"Faster Block",        L"%d%%", ST(102), ST(102) > 0 ? 2 : 0);
    SP_ROW(lx, ly, L"Faster Run/Walk",     L"%d%%", ST(96),  ST(96)  > 0 ? 2 : 0);

    /* RIGHT PAGE */
    int rx = s_rpX, ry = s_rpY;

    SP_HEADER(rx, ry, L"Damage you add");
    SP_ROW2(rx, ry, L"Fire",      L"%d - %d", ST(48), ST(49), ST(49) > 0 ? 2 : 0);
    SP_ROW2(rx, ry, L"Lightning", L"%d - %d", ST(50), ST(51), ST(51) > 0 ? 2 : 0);
    SP_ROW2(rx, ry, L"Cold",      L"%d - %d", ST(54), ST(55), ST(55) > 0 ? 2 : 0);
    SP_ROW(rx, ry,  L"Poison",    L"%d",      ST(58), ST(58) > 0 ? 2 : 0);
    SP_ROW(rx, ry,  L"Enhanced Damage", L"%d%%", ST(111), ST(111) > 0 ? 2 : 0);
    ry += 6;

    SP_HEADER(rx, ry, L"Hitting things");
    SP_ROW(rx, ry, L"Attack Rating", L"%d",   ST(19),  0);
    SP_ROW(rx, ry, L"Bonus to Hit",  L"%d%%", ST(119), ST(119) > 0 ? 2 : 0);
    SP_ROW(rx, ry, L"Crushing Blow", L"%d%%", ST(136), ST(136) > 0 ? 2 : 0);
    SP_ROW(rx, ry, L"Deadly Strike", L"%d%%", ST(141), ST(141) > 0 ? 2 : 0);
    SP_ROW(rx, ry, L"Open Wounds",   L"%d%%", ST(135), ST(135) > 0 ? 2 : 0);
    SP_ROW(rx, ry, L"Pierce",        L"%d%%", ST(156), ST(156) > 0 ? 2 : 0);
    ry += 6;

    SP_HEADER(rx, ry, L"Misc");
    SP_ROW(rx, ry, L"+ All Skills", L"%d", ST(127), ST(127) > 0 ? 2 : 0);

    /* The footnote goes under the RIGHT column, which ends at y=373 and leaves room. */
    fnFont(6);
    fnText(L"Totals include worn gear and carried charms.",
           s_rpX + 8, s_footY, 5, 0);

    #undef ST
    #undef SP_HEADER
    #undef SP_ROW
    #undef SP_ROW2
}

/* ZONES PAGE (g_editorPage == PAGE_ZONES) Mirrors the Quest page layout: left page = scrollable list of gates for the selected (act, difficulty); right page = detail panel for the gate the user clicked. */
static void Editor_RenderZonesPage(int mx, int my, BOOL clicked) {
    if (!fnRect || !fnText || !fnFont) return;

    /* INI-tunable layout */
    static int s_loaded = 0;
    static int s_actTabX = 45,  s_actTabY = 50,  s_actTabW = 55,  s_actTabH = 22;
    static int s_diffTabX = 400, s_diffTabY = 50, s_diffTabW = 70, s_diffTabH = 22;
    static int s_listX = 55,  s_listY = 85,  s_listW = 270, s_listH = 320;
    static int s_detailX = 380, s_detailY = 85, s_detailW = 300, s_detailH = 320;
    static int s_rowH = 22;
    INI_HOT_RELOAD_GUARD(s_loaded);
    if (!s_loaded) {
        s_loaded = 1;
        char ini[MAX_PATH]; GetArchDir(ini, MAX_PATH); strcat(ini, "d2arch.ini");
        s_actTabX  = GetPrivateProfileIntA("EditorZonesPage", "ActTabX",  45,  ini);
        s_actTabY  = GetPrivateProfileIntA("EditorZonesPage", "ActTabY",  50,  ini);
        s_actTabW  = GetPrivateProfileIntA("EditorZonesPage", "ActTabW",  55,  ini);
        s_actTabH  = GetPrivateProfileIntA("EditorZonesPage", "ActTabH",  22,  ini);
        s_diffTabX = GetPrivateProfileIntA("EditorZonesPage", "DiffTabX", 400, ini);
        s_diffTabY = GetPrivateProfileIntA("EditorZonesPage", "DiffTabY", 50,  ini);
        s_diffTabW = GetPrivateProfileIntA("EditorZonesPage", "DiffTabW", 70,  ini);
        s_diffTabH = GetPrivateProfileIntA("EditorZonesPage", "DiffTabH", 22,  ini);
        s_listX    = GetPrivateProfileIntA("EditorZonesPage", "ListX",    55,  ini);
        s_listY    = GetPrivateProfileIntA("EditorZonesPage", "ListY",    85,  ini);
        s_listW    = GetPrivateProfileIntA("EditorZonesPage", "ListW",    270, ini);
        s_listH    = GetPrivateProfileIntA("EditorZonesPage", "ListH",    320, ini);
        s_detailX  = GetPrivateProfileIntA("EditorZonesPage", "DetailX",  380, ini);
        s_detailY  = GetPrivateProfileIntA("EditorZonesPage", "DetailY",  85,  ini);
        s_detailW  = GetPrivateProfileIntA("EditorZonesPage", "DetailW",  300, ini);
        s_detailH  = GetPrivateProfileIntA("EditorZonesPage", "DetailH",  320, ini);
        s_rowH     = GetPrivateProfileIntA("EditorZonesPage", "RowH",     22,  ini);
        if (s_rowH < 14) s_rowH = 14;
    }

    /* Friendly fallback when zone-locking is off */
    if (!g_zoneLockingOn) {
        fnFont(0);
        fnText(L"Zone Locking is disabled in settings.",
               s_listX, s_listY + 40, 7, 0);
        fnFont(6);
        fnText(L"Enable Zone Locking on the title screen to use this tracker.",
               s_listX, s_listY + 64, 7, 0);
        return;
    }

    /* Page state */
    static int s_zPageAct    = 1;     /* 1..5 */
    static int s_zDifficulty = 0;     /* 0..2 */
    static int s_zSelectedGate = -1;  /* index in current act's gate list */

    int maxAct = GetMaxActForGoal();
    if (maxAct < 1) maxAct = 1;
    if (maxAct > 5) maxAct = 5;
    if (s_zPageAct > maxAct) s_zPageAct = 1;
    int numDiffs = g_apDiffScope + 1;
    if (numDiffs < 1) numDiffs = 1;
    if (numDiffs > 3) numDiffs = 3;
    if (s_zDifficulty >= numDiffs) s_zDifficulty = 0;

    /* Act tabs (top-left) */
    static void* s_zBtnActCel[5] = {NULL,NULL,NULL,NULL,NULL};
    static void* s_zBtnDiffCel[3] = {NULL,NULL,NULL};
    static int s_zBtnLoaded = -1;
    if (s_zBtnLoaded != g_celSessionGen) {
        s_zBtnLoaded = g_celSessionGen;
        for (int fi = 0; fi < 5; fi++) EdCelFree(&s_zBtnActCel[fi]);
        for (int fi = 0; fi < 3; fi++) EdCelFree(&s_zBtnDiffCel[fi]);
        if (fnCelLoad) {
            s_zBtnActCel[0] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_act1", 0);
            s_zBtnActCel[1] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_act2", 0);
            s_zBtnActCel[2] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_act3", 0);
            s_zBtnActCel[3] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_act4", 0);
            s_zBtnActCel[4] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_act5", 0);
            s_zBtnDiffCel[0] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_normal", 0);
            s_zBtnDiffCel[1] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_nightmare", 0);
            s_zBtnDiffCel[2] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_hell", 0);
        }
    }

    fnFont(6);
    for (int act = 1; act <= maxAct; act++) {
        int tx = s_actTabX + (act - 1) * (s_actTabW + 4);
        int ty = s_actTabY;
        BOOL isActive = (s_zPageAct == act);
        BOOL hover = InRect(mx, my, tx, ty, s_actTabW, s_actTabH);

        if (s_zBtnActCel[act - 1]) {
            __try { DrawCel(s_zBtnActCel[act - 1], 0, tx, ty + s_actTabH); } __except(1) {}
            if (isActive && fnRect) {
                fnRect(tx, ty, tx + s_actTabW, ty + 1, 4, 5);
                fnRect(tx, ty + s_actTabH - 1, tx + s_actTabW, ty + s_actTabH, 4, 5);
                fnRect(tx, ty, tx + 1, ty + s_actTabH, 4, 5);
                fnRect(tx + s_actTabW - 1, ty, tx + s_actTabW, ty + s_actTabH, 4, 5);
            }
        } else {
            if (isActive) {
                for (int k = 0; k < 3; k++)
                    fnRect(tx, ty, tx + s_actTabW, ty + s_actTabH, 4, 5);
            } else {
                for (int k = 0; k < 5; k++)
                    fnRect(tx, ty, tx + s_actTabW, ty + s_actTabH, 0, 5);
                if (hover) fnRect(tx, ty, tx + s_actTabW, ty + s_actTabH, 7, 5);
            }
            int bc = isActive ? 4 : 7;
            fnRect(tx, ty,                tx + s_actTabW, ty + 1,        bc, 5);
            fnRect(tx, ty,                tx + 1,         ty + s_actTabH, bc, 5);
            fnRect(tx + s_actTabW - 1, ty, tx + s_actTabW, ty + s_actTabH, bc, 5);
            if (!isActive)
                fnRect(tx, ty + s_actTabH - 1, tx + s_actTabW, ty + s_actTabH, bc, 5);

            wchar_t lbl[16]; wsprintfW(lbl, L"Act %d", act);
            int textColor = isActive ? 0 : (hover ? 4 : 7);
            fnText(lbl, tx + s_actTabW / 2, ty + 16, textColor, 1);
        }

        if (clicked && hover) {
            static DWORD _atc = 0; DWORD _atn = GetTickCount();
            if (_atn - _atc > 200) {
                _atc = _atn;
                s_zPageAct = act;
                s_zSelectedGate = -1;
                g_zoneTrackerScroll = 0;
            }
        }
    }

    /* Difficulty tabs (top-right) */
    static const wchar_t* diffNames[3] = { L"Normal", L"Nightmare", L"Hell" };
    for (int d = 0; d < numDiffs; d++) {
        int tx = s_diffTabX + d * (s_diffTabW + 4);
        int ty = s_diffTabY;
        BOOL isActive = (s_zDifficulty == d);
        BOOL hover = InRect(mx, my, tx, ty, s_diffTabW, s_diffTabH);

        if (s_zBtnDiffCel[d]) {
            __try { DrawCel(s_zBtnDiffCel[d], 0, tx, ty + s_diffTabH); } __except(1) {}
            if (isActive && fnRect) {
                fnRect(tx, ty, tx + s_diffTabW, ty + 1, 4, 5);
                fnRect(tx, ty + s_diffTabH - 1, tx + s_diffTabW, ty + s_diffTabH, 4, 5);
                fnRect(tx, ty, tx + 1, ty + s_diffTabH, 4, 5);
                fnRect(tx + s_diffTabW - 1, ty, tx + s_diffTabW, ty + s_diffTabH, 4, 5);
            }
        } else {
            if (isActive) {
                for (int k = 0; k < 3; k++)
                    fnRect(tx, ty, tx + s_diffTabW, ty + s_diffTabH, 4, 5);
            } else {
                for (int k = 0; k < 5; k++)
                    fnRect(tx, ty, tx + s_diffTabW, ty + s_diffTabH, 0, 5);
                if (hover) fnRect(tx, ty, tx + s_diffTabW, ty + s_diffTabH, 7, 5);
            }
            int bc = isActive ? 4 : 7;
            fnRect(tx, ty,                 tx + s_diffTabW, ty + 1,         bc, 5);
            fnRect(tx, ty,                 tx + 1,          ty + s_diffTabH, bc, 5);
            fnRect(tx + s_diffTabW - 1, ty, tx + s_diffTabW, ty + s_diffTabH, bc, 5);
            if (!isActive)
                fnRect(tx, ty + s_diffTabH - 1, tx + s_diffTabW, ty + s_diffTabH, bc, 5);

            int textColor = isActive ? 0 : (hover ? 4 : 7);
            fnText((wchar_t*)diffNames[d], tx + s_diffTabW / 2, ty + 16, textColor, 1);
        }

        if (clicked && hover) {
            static DWORD _dtc = 0; DWORD _dtn = GetTickCount();
            if (_dtn - _dtc > 200) {
                _dtc = _dtn;
                s_zDifficulty = d;
                s_zSelectedGate = -1;
                g_zoneTrackerScroll = 0;
            }
        }
    }

    /* Find current target gate (highlight the next non-killed) */
    int numGates = g_actRegions[s_zPageAct - 1].num_gates;
    int curTargetGate = -1;
    for (int g = 0; g < numGates; g++) {
        int slot = GateKey_SlotFromActGate(s_zPageAct, g);
        if (slot >= 0 && !g_gateBossKilled[s_zDifficulty][slot]) {
            curTargetGate = g;
            break;
        }
    }

    /* Per-act progress counter.

       This used to count gates whose CONDITION was met and stopped there, so
       an act with every condition done but a key still missing announced
       itself as fully cleared while a quarter of it was locked — which is
       exactly the state that leaves you unable to reach the act boss. A gate
       counts as open only when both halves are true. */
    int openInAct = 0, waitingForKey = 0;
    for (int g = 0; g < numGates; g++) {
        int slot = GateKey_SlotFromActGate(s_zPageAct, g);
        if (slot < 0) continue;
        BOOL cond = g_gateBossKilled[s_zDifficulty][slot];
        BOOL key  = g_gateKeyReceived[s_zDifficulty][slot];
        if (cond && key)      openInAct++;
        else if (cond && !key) waitingForKey++;
    }
    {
        wchar_t hdr[96];
        static const wchar_t* actNames[6] = {
            L"", L"Rogue Encampment", L"Lut Gholein",
            L"Kurast Docks", L"Pandemonium Fortress", L"Harrogath"
        };
        if (waitingForKey > 0) {
            swprintf(hdr, 96, L"Act %d - %s   (%d / %d open - %d waiting for a key)",
                     s_zPageAct, actNames[s_zPageAct], openInAct, numGates,
                     waitingForKey);
        } else {
            swprintf(hdr, 96, L"Act %d - %s   (%d / %d open)",
                     s_zPageAct, actNames[s_zPageAct], openInAct, numGates);
        }
        fnFont(0);
        fnText(hdr, s_listX, s_listY - 5, waitingForKey > 0 ? 8 : 4, 0);
    }

    /* Scroll math (uses shared g_zoneTrackerScroll) */
    int contentH = numGates * s_rowH;
    int maxScroll = contentH - s_listH;
    if (maxScroll < 0) maxScroll = 0;
    if (g_zoneTrackerScroll > maxScroll) g_zoneTrackerScroll = maxScroll;
    if (g_zoneTrackerScroll < 0) g_zoneTrackerScroll = 0;

    /* Gate rows (Logbook-style plain text) */
    int preload_id = g_actPreload[s_zPageAct - 1][s_zDifficulty];
    fnFont(6);
    for (int g = 0; g < numGates; g++) {
        int rowY = s_listY + g * s_rowH - g_zoneTrackerScroll;
        if (rowY + s_rowH < s_listY || rowY > s_listY + s_listH) continue;

        int slot = GateKey_SlotFromActGate(s_zPageAct, g);
        BOOL killed = (slot >= 0 && g_gateBossKilled[s_zDifficulty][slot]);
        BOOL keyOk  = (slot >= 0 && g_gateKeyReceived[s_zDifficulty][slot]);
        BOOL isCurrent = (g == curTargetGate);
        BOOL selected = (s_zSelectedGate == g);
        BOOL hover = InRect(mx, my, s_listX, rowY, s_listW, s_rowH);

        const wchar_t* badge;
        int badgeColor;
        /* added "OPEN" state for the case where the key has been received via AP but the boss hasn't been killed yet. */
        if (killed && keyOk)         { badge = L"[DONE]"; badgeColor = 2; }
        else if (killed && !keyOk)   { badge = L"[PEND]"; badgeColor = 8; }
        else if (keyOk)              { badge = L"[OPEN]"; badgeColor = 9; }
        else if (isCurrent)          { badge = L"[NOW ]"; badgeColor = 1; }
        else                         { badge = L"[----]"; badgeColor = 7; }

        /* "G4  Key: " eats 9 of the label, and the row is 270px wide at the
           small font — so the location gets what is left, truncated rather
           than wrapped. */
        #define GATE_KEYLOC_W 52
        wchar_t glabel[96];
        {
            /* 2.x — in combined mode (zone-lock + entrance-shuffle) the gate is cleared by a CONDITION, not a boss kill, and the boss has been shuffled away, so show the condition + live progress instead of the (now meaningless) boss name. */
            extern int  KeyCond_Active(void);
            extern const char* KeyCond_TypeName(int diff, int slot);
            extern unsigned KeyCond_GetThreshold(int diff, int slot);
            extern unsigned KeyCond_GetProgress(int diff, int slot);
            extern const char* Zones_GetGateKeyLocation(int, int);
            const char* keyWhere = (slot >= 0)
                ? Zones_GetGateKeyLocation(s_zDifficulty, slot) : "";

            if (killed && !keyOk && slot >= 0) {
                /* The condition is finished, so its "1500/1500" is noise —
                   what the player actually needs is where the key is. This
                   row is the whole reason a player sits stuck in an act. */
                if (keyWhere[0]) {
                    wchar_t w[GATE_KEYLOC_W];
                    int wi;
                    for (wi = 0; wi < GATE_KEYLOC_W - 1 && keyWhere[wi]; wi++)
                        w[wi] = (wchar_t)(unsigned char)keyWhere[wi];
                    w[wi] = 0;
                    wsprintfW(glabel, L"G%d  Key: %s", g + 1, w);
                } else {
                    wsprintfW(glabel, L"G%d  Waiting for the key", g + 1);
                }
            } else if (KeyCond_Active() && slot >= 0) {
                unsigned prog = KeyCond_GetProgress(s_zDifficulty, slot);
                unsigned thr  = KeyCond_GetThreshold(s_zDifficulty, slot);
                if (prog > thr) prog = thr;
                wsprintfW(glabel, L"G%d  %S %u/%u",
                          g + 1, KeyCond_TypeName(s_zDifficulty, slot), prog, thr);
            } else {
                const GateSpawnDef* gd = Preload_GetGate(s_zPageAct, preload_id, g);
                const char* boss = gd ? Preload_SuperUniqueName(gd->base_su, gd->label) : "Boss";
                wchar_t bossW[32];
                int bi; for (bi = 0; bi < 31 && boss[bi]; bi++) bossW[bi] = (wchar_t)boss[bi];
                bossW[bi] = 0;
                wsprintfW(glabel, L"G%d  %s", g + 1, bossW);
            }
        }

        int textColor = killed ? 2 : (selected ? 4 : (hover ? 0 : (isCurrent ? 0 : 7)));
        int textY = rowY + s_rowH / 2 + 4;
        fnText((wchar_t*)badge, s_listX + 5,  textY, badgeColor, 0);
        fnText(glabel,          s_listX + 60, textY, textColor, 0);

        if (hover && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            static DWORD _zlc = 0; DWORD _zln = GetTickCount();
            if (_zln - _zlc > 200) { _zlc = _zln; s_zSelectedGate = g; }
        }
    }

    /* Scrollbar (left of list) */
    if (maxScroll > 0) {
        int slX = s_listX - 12, slW = 10;
        int thumbH = (s_listH * s_listH) / contentH;
        if (thumbH < 20) thumbH = 20;
        fnRect(slX, s_listY, slX + slW, s_listY + s_listH, 0, 5);
        int thumbY = s_listY;
        if (maxScroll > 0)
            thumbY = s_listY + (g_zoneTrackerScroll * (s_listH - thumbH)) / maxScroll;
        fnRect(slX, thumbY, slX + slW, thumbY + thumbH, 7, 5);
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) &&
            InRect(mx, my, slX - 4, s_listY, slW + 8, s_listH)) {
            int relY = my - s_listY - thumbH / 2;
            if (s_listH - thumbH > 0)
                g_zoneTrackerScroll = (relY * maxScroll) / (s_listH - thumbH);
            if (g_zoneTrackerScroll < 0) g_zoneTrackerScroll = 0;
            if (g_zoneTrackerScroll > maxScroll) g_zoneTrackerScroll = maxScroll;
        }
    }

    /* Detail panel (right page) — only when a gate is selected */
    if (s_zSelectedGate >= 0 && s_zSelectedGate < numGates) {
        int g = s_zSelectedGate;
        int slot = GateKey_SlotFromActGate(s_zPageAct, g);
        BOOL killed = (slot >= 0 && g_gateBossKilled[s_zDifficulty][slot]);
        BOOL keyOk  = (slot >= 0 && g_gateKeyReceived[s_zDifficulty][slot]);
        BOOL isCurrent = (g == curTargetGate);

        const GateSpawnDef* gd = Preload_GetGate(s_zPageAct, preload_id, g);
        const char* boss = gd ? Preload_SuperUniqueName(gd->base_su, gd->label) : "Unknown";
        const char* zone = (gd && Preload_ZoneName(gd->spawn_zone))
                           ? Preload_ZoneName(gd->spawn_zone) : "Unknown";

        wchar_t bossW[40], zoneW[64];
        int bi; for (bi = 0; bi < 39 && boss[bi]; bi++) bossW[bi] = (wchar_t)boss[bi];
        bossW[bi] = 0;
        int zi; for (zi = 0; zi < 63 && zone[zi]; zi++) zoneW[zi] = (wchar_t)zone[zi];
        zoneW[zi] = 0;

        int dy = s_detailY;
        wchar_t title[32]; wsprintfW(title, L"Gate %d", g + 1);
        fnFont(0); fnText(title, s_detailX, dy + 15, 4, 0);
        dy += 35;

        fnFont(6);
        const wchar_t* statusText;
        int statusColor;
        if (killed && keyOk)        { statusText = L"COMPLETE";       statusColor = 2; }
        else if (killed && !keyOk)  { statusText = L"Key Pending";    statusColor = 8; }
        else if (keyOk)             { statusText = L"Open (kill boss)"; statusColor = 9; }
        else if (isCurrent)         { statusText = L"Current Target"; statusColor = 1; }
        else                        { statusText = L"Locked";         statusColor = 7; }
        fnText(L"Status:", s_detailX, dy, 7, 0);
        fnText((wchar_t*)statusText, s_detailX + 70, dy, statusColor, 0);
        dy += 18;

        {
            /* 2.x — combined mode: show the unlock CONDITION + progress instead of the shuffled-away boss/zone (forward-declared; keycond.c is included after editor.c in the unity TU). */
            extern int  KeyCond_Active(void);
            extern const char* KeyCond_TypeName(int diff, int slot);
            extern unsigned KeyCond_GetThreshold(int diff, int slot);
            extern unsigned KeyCond_GetProgress(int diff, int slot);
            if (KeyCond_Active() && slot >= 0) {
                unsigned prog = KeyCond_GetProgress(s_zDifficulty, slot);
                unsigned thr  = KeyCond_GetThreshold(s_zDifficulty, slot);
                if (prog > thr) prog = thr;
                wchar_t condW[48];
                wsprintfW(condW, L"%S  %u / %u",
                          KeyCond_TypeName(s_zDifficulty, slot), prog, thr);
                fnText(L"Condition:", s_detailX, dy, 7, 0);
                fnText(condW,         s_detailX + 70, dy, killed ? 2 : 0, 0);
                dy += 18;
            } else {
                fnText(L"Boss:",   s_detailX, dy, 7, 0);
                fnText(bossW,      s_detailX + 70, dy, killed ? 2 : 0, 0);
                dy += 18;

                fnText(L"Zone:",   s_detailX, dy, 7, 0);
                fnText(zoneW,      s_detailX + 70, dy, killed ? 2 : 0, 0);
                dy += 18;
            }
        }

        /* Next gate indicator */
        int isLast = (g == numGates - 1);
        wchar_t nextW[24];
        if (isLast) wsprintfW(nextW, L"act boss");
        else        wsprintfW(nextW, L"R%d", g + 2);
        fnText(L"Leads to:", s_detailX, dy, 7, 0);
        fnText(nextW, s_detailX + 70, dy, killed ? 2 : 7, 0);
        dy += 18;

        /* AP key location overlay (only when AP-mode + key not yet received + bridge has scouted location data for this slot). */
        if (g_apConnected && slot >= 0) {
            extern const char* Quests_GetGateKeyLocStr(int, int);
            const char* loc = Quests_GetGateKeyLocStr(s_zDifficulty, slot);
            if (loc && loc[0]) {
                wchar_t locW[80];
                int li; for (li = 0; li < 79 && loc[li]; li++)
                    locW[li] = (wchar_t)(unsigned char)loc[li];
                locW[li] = 0;
                dy += 6;
                fnText(L"Key at:", s_detailX, dy, 7, 0);
                fnText(locW,       s_detailX + 70, dy, keyOk ? 2 : 8, 0);
                dy += 18;
            }
        }

        /* Difficulty footer */
        dy += 12;
        fnText(diffNames[s_zDifficulty], s_detailX, dy, 9, 0);
    } else {
        /* Hint when nothing selected */
        fnFont(6);
        fnText(L"(click a gate on the left to see details)",
               s_detailX, s_detailY + 30, 7, 0);
    }
}

/* free a cached D2Win cel handle and NULL the slot. */
static void EdCelFree(void** pSlot) {
    if (!pSlot || !*pSlot) return;
    if (fnCelFree) { __try { fnCelFree(*pSlot); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
    *pSlot = NULL;
}

static void RenderEditor(void) {
    if (!g_editorOpen) return;

    /* never render during/after game teardown. */
    if (!Player()) { g_editorOpen = FALSE; g_celSessionGen++; return; }

    /* 2.x (F1-corruption ROOT FIX) — UI cels are STATIC (identical for every character) and are now loaded ONCE for the whole process: the per-block "if (s_resetThisFrame) { ...Cel = NULL; }" reloads were REMOVED. */
    static BOOL s_resetThisFrame = FALSE;
    s_resetThisFrame = g_editorNeedsReset;
    if (g_editorNeedsReset) {
        g_editorNeedsReset = FALSE;
        g_edDragActive = FALSE;
        g_edDragPoolIdx = -1;
        Log("EDITOR: character change — data/pointer reset (UI cels are load-once)\n");
    }

    /* === BOOK PANELS (always visible on all pages) === */

    /* Load 24x24 icons for right-side slots (once) */
    if (!g_icons28Loaded) {
        g_icons28Loaded = TRUE;
        char dc6p[MAX_PATH];
        GetArchDir(dc6p, MAX_PATH);
        strcat(dc6p, "ArchIcons24.DC6");
        g_icons28 = LoadDC6FromDisk(dc6p);
        Log("EDITOR: loaded ArchIcons25 = %p\n", g_icons28);
    }

    /* === NEW SKILL EDITOR: two-page panel (left + right, like an open book) === */
    {
        static void* s_edTopCel = NULL;
        static void* s_edBotCel = NULL;
        static void* s_edLeftTopCel = NULL;
        static void* s_edLeftBotCel = NULL;
        static void* s_edRightTopCel = NULL;
        static void* s_edRightBotCel = NULL;
        static int s_edLoaded = -1;   /* gen of last load (was BOOL load-once) */
        /* Left page */
        static int s_edLTopX = 100, s_edLTopY = 300;
        static int s_edLBotX = 100, s_edLBotY = 515;
        /* Right page */
        static int s_edRTopX = 420, s_edRTopY = 300;
        static int s_edRBotX = 420, s_edRBotY = 515;

        if (s_edLoaded != g_celSessionGen) {
            s_edLoaded = g_celSessionGen;
            /* RELEASE the old handles before loading new ones. */
            if (s_edLeftTopCel  == s_edTopCel) s_edLeftTopCel  = NULL;
            if (s_edLeftBotCel  == s_edBotCel) s_edLeftBotCel  = NULL;
            if (s_edRightTopCel == s_edTopCel) s_edRightTopCel = NULL;
            if (s_edRightBotCel == s_edBotCel) s_edRightBotCel = NULL;
            EdCelFree(&s_edLeftTopCel);
            EdCelFree(&s_edLeftBotCel);
            EdCelFree(&s_edRightTopCel);
            EdCelFree(&s_edRightBotCel);
            EdCelFree(&s_edTopCel);
            EdCelFree(&s_edBotCel);
            {
                char dc6Path[MAX_PATH];
                if (BuildDC6Path(dc6Path, MAX_PATH, "editor_top.DC6"))
                    s_edTopCel = LoadDC6FromDisk(dc6Path);
                if (BuildDC6Path(dc6Path, MAX_PATH, "editor_bot.DC6"))
                    s_edBotCel = LoadDC6FromDisk(dc6Path);
                if (BuildDC6Path(dc6Path, MAX_PATH, "editor_left_top.DC6"))
                    s_edLeftTopCel = LoadDC6FromDisk(dc6Path);
                if (BuildDC6Path(dc6Path, MAX_PATH, "editor_left_bot.DC6"))
                    s_edLeftBotCel = LoadDC6FromDisk(dc6Path);
                if (BuildDC6Path(dc6Path, MAX_PATH, "editor_right_top.DC6"))
                    s_edRightTopCel = LoadDC6FromDisk(dc6Path);
                if (BuildDC6Path(dc6Path, MAX_PATH, "editor_right_bot.DC6"))
                    s_edRightBotCel = LoadDC6FromDisk(dc6Path);
                if (!s_edLeftTopCel) s_edLeftTopCel = s_edTopCel;
                if (!s_edLeftBotCel) s_edLeftBotCel = s_edBotCel;
                if (!s_edRightTopCel) s_edRightTopCel = s_edTopCel;
                if (!s_edRightBotCel) s_edRightBotCel = s_edBotCel;
                Log("EDITOR: book disk left=%p/%p right=%p/%p legacy=%p/%p\n",
                    s_edLeftTopCel, s_edLeftBotCel, s_edRightTopCel, s_edRightBotCel,
                    s_edTopCel, s_edBotCel);
            }
            /* Read positions from INI */
            char ini[MAX_PATH], buf[32];
            GetArchDir(ini, MAX_PATH);
            strcat(ini, "d2arch.ini");
            /* Left page */
            GetPrivateProfileStringA("EditorPanel", "LTopX", "100", buf, 31, ini);
            s_edLTopX = atoi(buf);
            GetPrivateProfileStringA("EditorPanel", "LTopY", "300", buf, 31, ini);
            s_edLTopY = atoi(buf);
            GetPrivateProfileStringA("EditorPanel", "LBotX", "100", buf, 31, ini);
            s_edLBotX = atoi(buf);
            GetPrivateProfileStringA("EditorPanel", "LBotY", "515", buf, 31, ini);
            s_edLBotY = atoi(buf);
            /* Right page */
            GetPrivateProfileStringA("EditorPanel", "RTopX", "420", buf, 31, ini);
            s_edRTopX = atoi(buf);
            GetPrivateProfileStringA("EditorPanel", "RTopY", "300", buf, 31, ini);
            s_edRTopY = atoi(buf);
            GetPrivateProfileStringA("EditorPanel", "RBotX", "420", buf, 31, ini);
            s_edRBotX = atoi(buf);
            GetPrivateProfileStringA("EditorPanel", "RBotY", "515", buf, 31, ini);
            s_edRBotY = atoi(buf);
            Log("EDITOR POS: L top=(%d,%d) bot=(%d,%d) R top=(%d,%d) bot=(%d,%d)\n",
                s_edLTopX, s_edLTopY, s_edLBotX, s_edLBotY,
                s_edRTopX, s_edRTopY, s_edRBotX, s_edRBotY);
        }

        /* Draw side-specific pages, so the open book can have a real center spine. */
        {
            if (s_edLeftTopCel)  { DrawCel(s_edLeftTopCel,  0, s_edLTopX,       s_edLTopY); DrawCel(s_edLeftTopCel,  1, s_edLTopX + 256, s_edLTopY); }
            if (s_edLeftBotCel)  { DrawCel(s_edLeftBotCel,  0, s_edLBotX,       s_edLBotY); DrawCel(s_edLeftBotCel,  1, s_edLBotX + 256, s_edLBotY); }
            if (s_edRightTopCel) { DrawCel(s_edRightTopCel, 0, s_edRTopX,       s_edRTopY); DrawCel(s_edRightTopCel, 1, s_edRTopX + 256, s_edRTopY); }
            if (s_edRightBotCel) { DrawCel(s_edRightBotCel, 0, s_edRBotX,       s_edRBotY); DrawCel(s_edRightBotCel, 1, s_edRBotX + 256, s_edRBotY); }
        }
    }

    /* === Top tab bar (1.9.0 — replaces Next/Back as primary nav) === */
    {
        BOOL tabClicked = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        int newTab = Editor_DrawTabBar(MouseX(), MouseY(), tabClicked);
        if (newTab >= 0 && newTab < NUM_EDITOR_TABS) {
            int newPage = g_tabs[newTab].rawPageIdx;
            if (newPage != g_editorPage) {
                g_editorPage = newPage;
                g_apPageFocus = -1;
                g_questPageScroll = 0;
            }
        }
    }

    /* === PAGE 0: Skill Editor content === */
    if (g_editorPage == 0) {

    /* === Colored squares: 10 blue, 10 orange, 10 green === */
    {
        static void* s_blueCel = NULL;
        static void* s_orangeCel = NULL;
        static void* s_greenCel = NULL;
        static int s_sqLoaded = -1;   /* gen of last load */
        /* Positions for each square, read from INI */
        static int s_blueX[10], s_blueY[10];
        static int s_orangeX[10], s_orangeY[10];
        static int s_greenX[10], s_greenY[10];

        if (s_sqLoaded != g_celSessionGen) {
            s_sqLoaded = g_celSessionGen;
            EdCelFree(&s_blueCel); EdCelFree(&s_orangeCel); EdCelFree(&s_greenCel);
            if (fnCelLoad) {
                s_blueCel   = fnCelLoad("data\\global\\ui\\SPELLS\\blue_square", 0);
                s_orangeCel = fnCelLoad("data\\global\\ui\\SPELLS\\orange_square", 0);
                s_greenCel  = fnCelLoad("data\\global\\ui\\SPELLS\\green_square", 0);
                Log("EDITOR SQUARES: blue=%p orange=%p green=%p\n",
                    s_blueCel, s_orangeCel, s_greenCel);
            }
            /* Read positions from INI — [EditorBlue], [EditorOrange], [EditorGreen] */
            char ini[MAX_PATH], buf[32], key[16];
            GetArchDir(ini, MAX_PATH);
            strcat(ini, "d2arch.ini");
            for (int i = 0; i < 10; i++) {
                sprintf(key, "X%d", i + 1);
                GetPrivateProfileStringA("EditorBlue", key, "0", buf, 31, ini);
                s_blueX[i] = atoi(buf);
                sprintf(key, "Y%d", i + 1);
                GetPrivateProfileStringA("EditorBlue", key, "0", buf, 31, ini);
                s_blueY[i] = atoi(buf);

                sprintf(key, "X%d", i + 1);
                GetPrivateProfileStringA("EditorOrange", key, "0", buf, 31, ini);
                s_orangeX[i] = atoi(buf);
                sprintf(key, "Y%d", i + 1);
                GetPrivateProfileStringA("EditorOrange", key, "0", buf, 31, ini);
                s_orangeY[i] = atoi(buf);

                sprintf(key, "X%d", i + 1);
                GetPrivateProfileStringA("EditorGreen", key, "0", buf, 31, ini);
                s_greenX[i] = atoi(buf);
                sprintf(key, "Y%d", i + 1);
                GetPrivateProfileStringA("EditorGreen", key, "0", buf, 31, ini);
                s_greenY[i] = atoi(buf);
            }
        }

        /* Tier labels */
        if (fnText && fnFont) {
            static int s_lblGX = 40, s_lblGY = 65;
            static int s_lblBX = 40, s_lblBY = 125;
            static int s_lblOX = 40, s_lblOY = 185;
            static BOOL s_lblLoaded = FALSE;
            if (!s_lblLoaded) {
                s_lblLoaded = TRUE;
                char ini[MAX_PATH], buf[32];
                GetArchDir(ini, MAX_PATH);
                strcat(ini, "d2arch.ini");
                GetPrivateProfileStringA("EditorLabels", "Tier1X", "40", buf, 31, ini);
                s_lblGX = atoi(buf);
                GetPrivateProfileStringA("EditorLabels", "Tier1Y", "65", buf, 31, ini);
                s_lblGY = atoi(buf);
                GetPrivateProfileStringA("EditorLabels", "Tier2X", "40", buf, 31, ini);
                s_lblBX = atoi(buf);
                GetPrivateProfileStringA("EditorLabels", "Tier2Y", "125", buf, 31, ini);
                s_lblBY = atoi(buf);
                GetPrivateProfileStringA("EditorLabels", "Tier3X", "40", buf, 31, ini);
                s_lblOX = atoi(buf);
                GetPrivateProfileStringA("EditorLabels", "Tier3Y", "185", buf, 31, ini);
                s_lblOY = atoi(buf);
            }
            fnFont(0);
            fnText(L"Tier 1", s_lblGX, s_lblGY, 2, 1);  /* Green, centered */
            fnText(L"Tier 2", s_lblBX, s_lblBY, 3, 1);  /* Blue, centered */
            fnText(L"Tier 3", s_lblOX, s_lblOY, 8, 1);  /* Orange, centered */
        }

        /* Draw right-side tier slots + assigned skill icons + drop detection */
        {
            int mx2 = MouseX(), my2 = MouseY();
            BOOL lmbUp = !(GetAsyncKeyState(VK_LBUTTON) & 0x8000) && g_edDragActive;
            int playerCls = GetPlayerClass();
            if (playerCls < 0) playerCls = 0;

            /* Build tier->slot mapping: find which (tab,slot) pairs belong to each tier */
            /* We iterate all 30 slots and group them by tier */
            /* Simple mapping: tab 0 = Tier 1 (green), tab 1 = Tier 2 (blue), tab 2 = Tier 3 (orange) */
            int tierSlotTab[3][10], tierSlotIdx[3][10], tierCount[3] = {0,0,0};
            for (int t = 0; t < 3; t++) {
                for (int s = 0; s < 10; s++) {
                    tierSlotTab[t][tierCount[t]] = t;
                    tierSlotIdx[t][tierCount[t]] = s;
                    tierCount[t]++;
                }
            }

            /* Draw each tier's slots with icons if assigned */
            struct { void* cel; int* xArr; int* yArr; int tier; } tierGroups[3] = {
                { s_greenCel,  s_greenX,  s_greenY,  1 },
                { s_blueCel,   s_blueX,   s_blueY,   2 },
                { s_orangeCel, s_orangeX, s_orangeY,  3 },
            };

            if (fnCelDraw) {
                __try {
                    for (int ti = 0; ti < 3; ti++) {
                        void* slotCel = tierGroups[ti].cel;
                        int* xArr = tierGroups[ti].xArr;
                        int* yArr = tierGroups[ti].yArr;
                        int tier = tierGroups[ti].tier;

                        for (int i = 0; i < 10; i++) {
                            if (!(xArr[i] || yArr[i])) continue;

                            /* Draw slot background (30x30) */
                            if (slotCel)
                                fnCelDraw(slotCel, xArr[i], yArr[i], 0, 5, -1);

                            /* Find assignment for this visual slot */
                            int assignedPoolIdx = -1;
                            if (i < tierCount[ti]) {
                                int tab = tierSlotTab[ti][i];
                                int slot = tierSlotIdx[ti][i];
                                assignedPoolIdx = g_tabSlots[tab][slot];
                            }

                            /* Draw skill icon (28x28) if assigned */
                            if (assignedPoolIdx >= 0 && g_icons28) {
                                int dbIdx = g_pool[assignedPoolIdx].dbIndex;
                                /* Center 24x24 in 30x30 slot: exactly 3px border all sides */
                                DrawCel(g_icons28, dbIdx, xArr[i] + 3, yArr[i] - 3);
                            }

                            /* Drop detection: if dragging and LMB released over this slot */
                            if (lmbUp && g_edDragPoolIdx >= 0 && i < tierCount[ti]) {
                                /* Hit test: slot is 30x30, drawn at (xArr[i], yArr[i]) bottom-left */
                                int slotX = xArr[i];
                                int slotY = yArr[i] - 30; /* top of slot */
                                if (InRect(mx2, my2, slotX, slotY, 30, 30)) {
                                    int dragTier = g_skillDB[g_pool[g_edDragPoolIdx].dbIndex].tier;
                                    if (dragTier == tier) {
                                        int tab = tierSlotTab[ti][i];
                                        int slot = tierSlotIdx[ti][i];
                                        if (g_tabSlots[tab][slot] == -1) {
                                            /* Empty slot — assign directly */
                                            AssignSkill(g_edDragPoolIdx, tab, slot);
                                            Log("EDITOR DROP: assigned pool %d to tab %d slot %d\n",
                                                g_edDragPoolIdx, tab, slot);
                                        } else if (g_resetPoints >= 1) {
                                            /* Occupied — swap (costs 1 reset point) */
                                            /* 2.x (refund-on-swap) — mark this slot so the next ResetD2SFile refunds the OVERWRITTEN skill's invested points back to the player's pool (to re-spend freely) instead of carrying them onto the new skill. */
                                            if (tab * 10 + slot >= 0 && tab * 10 + slot < 30)
                                                g_slotSwapRefund[tab * 10 + slot] = 1;
                                            RemoveSkill(tab, slot);
                                            AssignSkill(g_edDragPoolIdx, tab, slot);
                                            g_resetPoints--;
                                            Log("EDITOR SWAP: replaced tab %d slot %d (cost 1 reset)\n", tab, slot);
                                            /* persist Reset Point decrement immediately */
                                            extern void MarkStateDirty(void);
                                            MarkStateDirty();
                                        } else {
                                            ShowNotify("Need 1 Reset Point to swap!");
                                        }
                                    } else {
                                        ShowNotify("Wrong tier! Check skill color.");
                                    }
                                    g_edDragActive = FALSE;
                                    g_edDragPoolIdx = -1;
                                }
                            }

                            /* Tooltip on hover for assigned skill */
                            if (assignedPoolIdx >= 0 && !g_edDragActive) {
                                int slotX = xArr[i];
                                int slotY = yArr[i] - 30;
                                if (InRect(mx2, my2, slotX, slotY, 30, 30)) {
                                    g_tooltipSkillId = g_skillDB[g_pool[assignedPoolIdx].dbIndex].id;
                                    g_tooltipMX = mx2;
                                    g_tooltipMY = my2;
                                }
                            }
                        }
                    }
                } __except(1) {
                    Log("EDITOR: right side draw CRASHED\n");
                }
            }

            /* Cancel drag if LMB released outside any slot */
            if (lmbUp) {
                g_edDragActive = FALSE;
                g_edDragPoolIdx = -1;
            }
        }
    }

    /* === Scrollable skill pool list (left page) === */
    {
        static void* s_redCel = NULL;      /* locked/unavailable */
        static void* s_greenCel41 = NULL;  /* unlocked tier 1 */
        static void* s_blueCel41 = NULL;   /* unlocked tier 2 */
        static void* s_orangeCel41 = NULL; /* unlocked tier 3 */
        static void* s_cbWhiteCel41 = NULL; /* colourblind: tier 1 (white) */
        static void* s_cbGreyCel41 = NULL;  /* colourblind: locked (grey) */
        static void* s_trackCel = NULL;
        static void* s_thumbCel = NULL;
        static int s_rlLoaded = -1;   /* gen of last load */
        /* Box position and size (from INI) */
        static int s_boxX = 40, s_boxY = 80;
        static int s_boxW = 280, s_boxH = 305;
        static int s_sliderW = 12;

        /* Layout: 6 columns, 41x41 squares with 4px gap = 45px per cell */
        #define RL_COLS 6
        #define RL_CELL 45
        #define RL_SQ   41
        #define RL_TOTAL 210

        if (s_rlLoaded != g_celSessionGen) {
            s_rlLoaded = g_celSessionGen;
            EdCelFree(&s_redCel); EdCelFree(&s_greenCel41); EdCelFree(&s_blueCel41);
            EdCelFree(&s_orangeCel41); EdCelFree(&s_trackCel); EdCelFree(&s_thumbCel);
            EdCelFree(&s_cbWhiteCel41); EdCelFree(&s_cbGreyCel41);
            if (fnCelLoad) {
                s_redCel = fnCelLoad("data\\global\\ui\\SPELLS\\red_square", 0);
                s_greenCel41 = fnCelLoad("data\\global\\ui\\SPELLS\\green_square_41", 0);
                s_cbWhiteCel41 = fnCelLoad("data\\global\\ui\\SPELLS\\cb_white_square_41", 0);
                s_cbGreyCel41 = fnCelLoad("data\\global\\ui\\SPELLS\\cb_grey_square_41", 0);
                s_blueCel41 = fnCelLoad("data\\global\\ui\\SPELLS\\blue_square_41", 0);
                s_orangeCel41 = fnCelLoad("data\\global\\ui\\SPELLS\\orange_square_41", 0);
                s_trackCel = fnCelLoad("data\\global\\ui\\SPELLS\\slider_track", 0);
                s_thumbCel = fnCelLoad("data\\global\\ui\\SPELLS\\slider_thumb", 0);
                Log("EDITOR POOL: red=%p green41=%p blue41=%p orange41=%p track=%p thumb=%p\n",
                    s_redCel, s_greenCel41, s_blueCel41, s_orangeCel41, s_trackCel, s_thumbCel);
            }
            char ini[MAX_PATH], buf[32];
            GetArchDir(ini, MAX_PATH);
            strcat(ini, "d2arch.ini");
            GetPrivateProfileStringA("EditorRedList", "BoxX", "40", buf, 31, ini);
            s_boxX = atoi(buf);
            GetPrivateProfileStringA("EditorRedList", "BoxY", "80", buf, 31, ini);
            s_boxY = atoi(buf);
            GetPrivateProfileStringA("EditorRedList", "BoxW", "280", buf, 31, ini);
            s_boxW = atoi(buf);
            GetPrivateProfileStringA("EditorRedList", "BoxH", "305", buf, 31, ini);
            s_boxH = atoi(buf);
            GetPrivateProfileStringA("EditorRedList", "SliderW", "12", buf, 31, ini);
            s_sliderW = atoi(buf);
        }

        int mx = MouseX();
        int my = MouseY();

        /* Build sorted display order: unlocked first (newest=highest index first), then assigned (dimmed), then locked last */
        /* Sized to the pool, not to a number from before the pool grew. */
        static int s_sortedIdx[210];
        static int s_sortedCount = 0;
        {
            int pos = 0;
            const int cap = (int)(sizeof(s_sortedIdx) / sizeof(s_sortedIdx[0]));
            /* Pass 1: unlocked & NOT assigned (available to drag) — newest first */
            for (int j = g_poolCount - 1; j >= 0 && pos < cap; j--) {
                if (g_pool[j].unlocked && !g_pool[j].assigned)
                    s_sortedIdx[pos++] = j;
            }
            /* Pass 2: skip assigned skills — they no longer appear in the pool. */
            /* Pass 3: locked — skipped entirely while the Hide Locked toggle is on, so the list shows only what the player can actually use. */
            if (!g_edHideLocked) {
                for (int j = 0; j < g_poolCount && pos < cap; j++) {
                    if (!g_pool[j].unlocked)
                        s_sortedIdx[pos++] = j;
                }
            }
            s_sortedCount = pos;
        }

        /* Calculate total content height */
        int totalRows = (s_sortedCount + RL_COLS - 1) / RL_COLS;
        int contentH = totalRows * RL_CELL;
        int maxScroll = contentH - s_boxH;
        if (maxScroll < 0) maxScroll = 0;

        /* Clamp scroll */
        if (g_editorScroll < 0) g_editorScroll = 0;
        if (g_editorScroll > maxScroll) g_editorScroll = maxScroll;

        /* Slider drag handling (process before drawing so it's responsive) */
        int slX = s_boxX;  /* LEFT side of box */
        int slX2 = s_boxX + s_sliderW;
        if (maxScroll > 0) {
            int thumbH = (s_boxH * s_boxH) / contentH;
            if (thumbH < 30) thumbH = 30;
            BOOL lmbDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (lmbDown && InRect(mx, my, slX - 4, s_boxY, s_sliderW + 8, s_boxH)) {
                int relY = my - s_boxY - thumbH / 2;
                if (s_boxH - thumbH > 0) {
                    g_editorScroll = (relY * maxScroll) / (s_boxH - thumbH);
                    if (g_editorScroll < 0) g_editorScroll = 0;
                    if (g_editorScroll > maxScroll) g_editorScroll = maxScroll;
                }
            }
        }

        /* === RED SQUARES + SKILL ICONS (clipped to box) === */
        {
            int contentX = s_boxX + s_sliderW + 2;

            /* ArchIcons35 chain (35x35, centered on 41x41 red = 3px border). */
            static void* s_icons35 = NULL;
            static BOOL s_icons35Loaded = FALSE;
            static void* s_icons35_ptr = NULL;
            if (!s_icons35Loaded) {
                s_icons35Loaded = TRUE;
                char dc6p[MAX_PATH];
                GetArchDir(dc6p, MAX_PATH);
                strcat(dc6p, "ArchIcons35.DC6");
                s_icons35 = LoadDC6FromDisk(dc6p);
                Log("EDITOR: loaded ArchIcons35 = %p\n", s_icons35);
            }

            __try {
                for (int i = 0; i < s_sortedCount; i++) {
                    int pi = s_sortedIdx[i]; /* actual pool index */
                    int col = i % RL_COLS;
                    int row = i / RL_COLS;
                    int sqX = contentX + col * RL_CELL;
                    int sqY = s_boxY + row * RL_CELL - g_editorScroll;

                    /* Only draw if square is FULLY within the box bounds */
                    if (sqY >= s_boxY && sqY + RL_SQ <= s_boxY + s_boxH) {
                        /* Choose background color based on unlock status + tier */
                        /* Locked = red; colourblind mode swaps it for grey so no red/green pair exists anywhere in that palette. */
                        void* bgCel = (g_edColorblind && s_cbGreyCel41) ? s_cbGreyCel41 : s_redCel;
                        int   sqTier = 0;
                        if (g_pool[pi].unlocked) {
                            sqTier = g_skillDB[g_pool[pi].dbIndex].tier;
                            /* Normal: tier 1 green, 2 blue, 3 orange. */
                            if      (sqTier == 2 && s_blueCel41)   bgCel = s_blueCel41;
                            else if (sqTier == 3 && s_orangeCel41) bgCel = s_orangeCel41;
                            else if (g_edColorblind && s_cbWhiteCel41) bgCel = s_cbWhiteCel41;
                            else if (s_greenCel41)                 bgCel = s_greenCel41;
                        }

                        /* Background square (41x41) */
                        if (bgCel && fnCelDraw)
                            fnCelDraw(bgCel, sqX, sqY + RL_SQ, 0, 5, -1);

                        /* Skill icon (35x35) — use pool's dbIndex to get correct frame */
                        if (s_icons35) {
                            int dbIdx = g_pool[pi].dbIndex;
                            /* REVERTED from 3.3.1. That version made the book follow the right-click selector, on the theory that the two disagreeing was the bug. The book was the side that was RIGHT: it draws each skill's own artwork, and following the selector replaced good icons with the selector's recycled slot art. The selector is the side that cannot show the true icon — its cel field is one byte, so it cannot address a frame above 254, and 238 of the 283 new icons live above that. Fixing it needs the icon drawn over the panel, not a different frame number. */
                            DrawCel(s_icons35, dbIdx, sqX + 3, sqY + RL_SQ - 3);
                        }

                        /* Tier digit + locked dash — ALWAYS on. */
                        if (fnText && fnFont) {
                            wchar_t mark[2];
                            if (!g_pool[pi].unlocked)      mark[0] = L'-';
                            else if (sqTier >= 1 && sqTier <= 3) mark[0] = (wchar_t)(L'0' + sqTier);
                            else                           mark[0] = L'*';
                            mark[1] = 0;
                            fnFont(0);
                            fnText(mark, sqX + 2, sqY + 11, 1, 0);   /* 1 = white/grey */
                        }

                        /* Hover detection — show tooltip with correct skill ID */
                        if (InRect(mx, my, sqX, sqY, RL_SQ, RL_SQ)) {
                            g_tooltipSkillId = g_skillDB[g_pool[pi].dbIndex].id;
                            g_tooltipMX = mx;
                            g_tooltipMY = my;

                            /* Start drag on LMB press — only unlocked & not assigned */
                            BOOL lmbDown2 = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                            if (lmbDown2 && !g_edDragActive &&
                                g_pool[pi].unlocked && !g_pool[pi].assigned) {
                                g_edDragActive = TRUE;
                                g_edDragPoolIdx = pi;
                            }
                        }
                    }
                }
            } __except(1) {
                Log("EDITOR: red list draw CRASHED\n");
            }
        }

        /* === SLIDER (left edge, drawn AFTER squares using fnCelDraw) === */
        if (fnCelDraw) {
            __try {
                /* Track — dark DC6 bar (same as backup) */
                if (s_trackCel)
                    fnCelDraw(s_trackCel, slX, s_boxY + s_boxH, 0, 5, -1);

                /* Thumb — movable DC6 indicator */
                if (s_thumbCel && maxScroll > 0) {
                    int thumbH = 40;
                    int thumbY = s_boxY + (g_editorScroll * (s_boxH - thumbH)) / maxScroll;
                    fnCelDraw(s_thumbCel, slX, thumbY + thumbH, 0, 5, -1);
                }
            } __except(1) {
                Log("EDITOR: slider draw CRASHED\n");
            }
        }

        /* Hide Locked / Colourblind toggles Text-only buttons: no new art, and they sit under the reward box where the list they affect is. */
        if (fnText && fnFont) {
            EdLoadViewToggles();
            int tbY = s_boxY + s_boxH + 8;
            int tbW = s_boxW / 2 - 4;
            struct { const wchar_t* on; const wchar_t* off; int* flag; const char* key; int x; }
            toggles[2] = {
                { L"Show Locked",   L"Hide Locked",   &g_edHideLocked,  "HideLocked", s_boxX },
                { L"Colourblind*",  L"Colourblind",   &g_edColorblind,  "Colorblind", s_boxX + tbW + 8 },
            };
            static BOOL s_tgLmbWas = FALSE;
            BOOL tgNow  = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            BOOL tgEdge = tgNow && !s_tgLmbWas;
            s_tgLmbWas = tgNow;

            for (int t = 0; t < 2; t++) {
                BOOL hov = InRect(mx, my, toggles[t].x, tbY, tbW, 16);
                fnFont(0);
                fnText(*toggles[t].flag ? toggles[t].on : toggles[t].off,
                       toggles[t].x, tbY + 13, hov ? 2 : (*toggles[t].flag ? 9 : 1), 0);
                if (tgEdge && hov) {
                    *toggles[t].flag = !*toggles[t].flag;
                    EdSaveViewToggle(toggles[t].key, *toggles[t].flag);
                    if (toggles[t].flag == &g_edHideLocked) g_editorScroll = 0;
                }
            }
        }
    }

    /* === Instructions + Reset Points on right page === */
    if (fnText && fnFont) {
        static int s_instrX = 0, s_instrY = 0;
        static int s_resetX = 0, s_resetY = 0;
        static int s_boxIX = 0, s_boxIY = 0, s_boxIW = 0, s_boxIH = 0;
        static BOOL s_instrLoaded = FALSE;
        if (s_resetThisFrame) s_instrLoaded = FALSE;
        if (!s_instrLoaded) {
            s_instrLoaded = TRUE;
            char ini5[MAX_PATH], buf5[32];
            GetArchDir(ini5, MAX_PATH); strcat(ini5, "d2arch.ini");
            GetPrivateProfileStringA("EditorInfo", "InstrX", "490", buf5, 31, ini5);
            s_instrX = atoi(buf5);
            GetPrivateProfileStringA("EditorInfo", "InstrY", "430", buf5, 31, ini5);
            s_instrY = atoi(buf5);
            GetPrivateProfileStringA("EditorInfo", "ResetX", "490", buf5, 31, ini5);
            s_resetX = atoi(buf5);
            GetPrivateProfileStringA("EditorInfo", "ResetY", "500", buf5, 31, ini5);
            s_resetY = atoi(buf5);
            GetPrivateProfileStringA("EditorInfo", "BoxX", "0", buf5, 31, ini5);
            s_boxIX = atoi(buf5);
            GetPrivateProfileStringA("EditorInfo", "BoxY", "0", buf5, 31, ini5);
            s_boxIY = atoi(buf5);
            GetPrivateProfileStringA("EditorInfo", "BoxW", "0", buf5, 31, ini5);
            s_boxIW = atoi(buf5);
            GetPrivateProfileStringA("EditorInfo", "BoxH", "0", buf5, 31, ini5);
            s_boxIH = atoi(buf5);
        }

        /* Instructions */
        /* Dark background box removed — dark book background is enough */

        fnFont(6);
        int iy = s_instrY;
        fnText(L"Drag skills into matching tier slots.", s_instrX, iy, 4, 0); iy += 14;
        fnText(L"Green=T1   Blue=T2   Orange=T3.", s_instrX, iy, 4, 0); iy += 14;
        fnText(L"Replace filled slot: costs 1 point.", s_instrX, iy, 1, 0);

        /* Reset Points counter */
        {
            wchar_t rpBuf[64];
            wsprintfW(rpBuf, L"Reset Points: %d", g_resetPoints);
            /* Reset text is inside the main box */
            fnFont(0);
            fnText(rpBuf, s_resetX, s_resetY, g_resetPoints > 0 ? 2 : 1, 0);
        }

        /* 2.x — "Reset Skill Points" button. */
        {
            int mx = MouseX(), my = MouseY();
            int bX = s_resetX, bY = s_resetY + 16, bW = 190, bH = 22;
            BOOL bHover = InRect(mx, my, bX, bY, bW, bH);
            static void* s_resetBtnCel = NULL;
            static int s_resetBtnLoaded = -1;
            /* Local left-click rising-edge (same GetAsyncKeyState pattern the rest of the editor uses); self-contained so it can't double-fire other handlers. */
            static BOOL s_rsLmbWas = FALSE;
            BOOL lmbNow  = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            BOOL lmbEdge = lmbNow && !s_rsLmbWas;
            s_rsLmbWas = lmbNow;

            /* Lapse an idle arm so the player sees it expire. */
            if (g_resetAllPending && GetTickCount() > g_resetAllDeadline) {
                g_resetAllPending = 0;
                g_resetAllDeadline = 0;
                ShowNotify("Skill point reset cancelled (no relog in time)");
            }

            if (s_resetBtnLoaded != g_celSessionGen) {
                s_resetBtnLoaded = g_celSessionGen;
                EdCelFree(&s_resetBtnCel);
                if (fnCelLoad)
                    s_resetBtnCel = fnCelLoad("data\\global\\ui\\SPELLS\\btn_reset_skills", 0);
            }

            if (fnCelDraw && s_resetBtnCel) {
                __try { fnCelDraw(s_resetBtnCel, bX, bY + bH, 0, 5, -1); } __except(1) {}
                if (bHover && !g_resetAllPending && fnRect)
                    fnRect(bX + 3, bY + 3, bX + bW - 3, bY + bH - 3, 7, 5);
            } else {
                for (int i = 0; i < 3; i++) fnRect(bX, bY, bX + bW, bY + bH, 0, 5);
                fnRect(bX, bY, bX + bW, bY + 1, 7, 5);
                fnRect(bX, bY + bH - 1, bX + bW, bY + bH, 7, 5);
            }
            fnFont(0);
            if (g_resetAllPending) {
                int secs = (int)(((long)g_resetAllDeadline - (long)GetTickCount()) / 1000) + 1;
                if (secs < 0) secs = 0;
                wchar_t ab[64];
                swprintf(ab, 64, L"Relog within %ds to reset!", secs);
                fnText(ab, bX + bW / 2, bY + 14, 1, 1);
            } else if (!s_resetBtnCel) {
                fnText(L"Reset Skill Points (3)", bX + 8, bY + 13,
                       (g_resetPoints >= RESET_ALL_COST) ? (bHover ? 2 : 9) : 1, 0);
            }

            if (lmbEdge && bHover && !g_resetAllPending) {
                if (g_resetPoints >= RESET_ALL_COST) {
                    g_resetAllPending  = 1;
                    g_resetAllDeadline = GetTickCount() + 60000;
                    ShowNotify("Log out and back in within 1 minute to reset all skill points");
                } else {
                    ShowNotify("Need 3 Reset Points to reset all skill points");
                }
            }
        }
    }

    } /* === END PAGE 0: Skill Editor === */

    /* QUEST SCANNER — runs EVERY frame regardless of which page is open. */
    {
        static BOOL s_qfPtrFound = FALSE;
        static DWORD s_lastRefresh = 0;

        if (s_resetThisFrame) {
            g_questScanQF = NULL;
            s_qfPtrFound = FALSE;
            s_lastRefresh = 0;
        }

        /* Find the client quest record pointer */
        if (!s_qfPtrFound && fnGetQuestState && hD2Client && Player()) {
            __try {
                void* ptr = *(void**)((DWORD)hD2Client + 0x115D13);
                if (ptr && (DWORD)ptr > 0x10000 && (DWORD)ptr < 0x7FFF0000) {
                    g_questScanQF = ptr;
                    s_qfPtrFound = TRUE;
                    Log("QUEST PTR: client record at D2Client+0x115D13 = %08X\n", (DWORD)ptr);
                }
            } __except(1) {}
        }

        /* Force-refresh the client quest record from server data every 500ms. */
        if (s_qfPtrFound && g_questScanQF && g_cachedPGame) {
            DWORD now = GetTickCount();
            if (now - s_lastRefresh > 500) {
                s_lastRefresh = now;
                __try {
                    DWORD pQC = *(DWORD*)(g_cachedPGame + 0x10F4);
                    if (pQC) {
                        void* pServerQF = *(void**)(pQC + 0x0C);
                        if (pServerQF) {
                            /* Get the raw buffer from server quest record */
                            typedef void (__stdcall *CopyRecordToBuffer_t)(void* pQR, void* pBuf, unsigned short nSize, int unused);
                            typedef void (__stdcall *CopyBufferToRecord_t)(void* pQR, void* pBuf, unsigned short nSize, BOOL bReset);
                            static CopyRecordToBuffer_t fnCopyOut = NULL;
                            static CopyBufferToRecord_t fnCopyIn = NULL;
                            if (!fnCopyOut && hD2Common) {
                                fnCopyOut = (CopyRecordToBuffer_t)GetProcAddress(hD2Common, (LPCSTR)11114);
                                fnCopyIn  = (CopyBufferToRecord_t)GetProcAddress(hD2Common, (LPCSTR)11113);
                            }
                            if (fnCopyOut && fnCopyIn) {
                                unsigned char buf[96]; /* 0x60 bytes = 48 quests * 2 bytes each */
                                fnCopyOut(pServerQF, buf, 0x60, 0);
                                fnCopyIn(g_questScanQF, buf, 0x60, 0); /* bReset=FALSE to preserve states */
                            }
                        }
                    }
                } __except(1) {}
            }
        }
    }

    /* === PAGE 1: Quest page (scrollable list of ALL quests) === */
    if (g_editorPage == 1) {
        /* === STATE === */
        static void* s_qSocketCel = NULL;  /* kept for future use */
        static void* s_qDoneCel = NULL;    /* kept for future use */
        static void* s_qImgCel[5][6];
        static int s_qGfxLoaded = -1;   /* gen of last load */
        static int s_qPageAct = 0;
        static int s_qSelectedQuest = -1;
        static DWORD s_qBurnStart[5][6];
        static BOOL s_qBurnPlayed[5][6];
        static int s_qDifficulty = 0;
        static int s_qFilterTab = 0; /* 0=Story, 1=Hunt, 2=Kill, 3=Area, 4=WP */

        /* the difficulty tab now FOLLOWS the difficulty you are actually playing, until you click a tab yourself. */
        static BOOL s_qDiffUserPicked = FALSE;
        /* The manual-pick latch is per CHARACTER, not per process: without a reset, one tab click froze the quest log's difficulty for every later character until game restart. */
        static char s_qDiffChar[32] = {0};
        if (strncmp(s_qDiffChar, g_charName, 31) != 0) {
            strncpy(s_qDiffChar, g_charName, 31); s_qDiffChar[31] = 0;
            s_qDiffUserPicked = FALSE;
        }
        if (!s_qDiffUserPicked && g_currentDifficulty >= 0 && g_currentDifficulty < 3)
            s_qDifficulty = g_currentDifficulty;

        /* D2 quest flag IDs (same as scanner above) */
        static const int d2QF[5][6] = {
            { 1, 2, 3, 4, 5, 6 }, { 9, 10, 11, 12, 13, 14 },
            { 17, 18, 19, 20, 21, 22 }, { 25, 27, 26, -1, -1, -1 },
            { 35, 36, 37, 38, 39, 40 },
        };

        if (s_resetThisFrame) {
            /* 2.x — quest images (s_qImgCel / s_qSocketCel / s_qDoneCel) are static: load-once, NOT reloaded here (leak). */
            s_qSelectedQuest = -1;
            memset(s_qBurnStart, 0, sizeof(s_qBurnStart));
            memset(s_qBurnPlayed, 0, sizeof(s_qBurnPlayed));
        }

        /* Positions from INI */
        static int s_qActTabX = 40, s_qActTabY = 50, s_qActTabW = 55, s_qActTabH = 22;
        static int s_qDiffTabX = 400, s_qDiffTabY = 50, s_qDiffTabW = 70, s_qDiffTabH = 22;
        static int s_qGridX = 50, s_qGridY = 85, s_qCellW = 90, s_qCellH = 115;
        static int s_qNameOffX = 40;  /* X offset for quest name (centered on socket) */
        static int s_qNameOffY = 108; /* Y offset for quest name below socket */
        static int s_qDetailX = 400, s_qDetailY = 85, s_qDetailW = 270, s_qDetailH = 180;
        static BOOL s_qPosLoaded = FALSE;

        if (s_resetThisFrame) s_qPosLoaded = FALSE;
        INI_HOT_RELOAD_GUARD(s_qPosLoaded);

        if (s_qGfxLoaded != g_celSessionGen) {
            s_qGfxLoaded = g_celSessionGen;
            EdCelFree(&s_qSocketCel); EdCelFree(&s_qDoneCel);
            for (int fa = 0; fa < 5; fa++)
                for (int fq = 0; fq < 6; fq++) EdCelFree(&s_qImgCel[fa][fq]);
            if (fnCelLoad) {
                s_qSocketCel = fnCelLoad("data\\global\\ui\\SPELLS\\questsockets", 0);
                s_qDoneCel = fnCelLoad("data\\global\\ui\\SPELLS\\questdone", 0);
            }
            /* Load per-act per-quest images (27 frames each: 0=grey, 1=active, 2-26=burn) */
            {
                static const char* qImgNames[5][6] = {
                    {"a1q1","a1q2","a1q3","a1q4","a1q5","a1q6"},
                    {"a2q1","a2q2","a2q3","a2q4","a2q5","a2q6"},
                    {"a3q1","a3q2","a3q3","a3q4","a3q5","a3q6"},
                    {"a4q1","a4q2","a4q3",NULL,NULL,NULL},
                    {"a5q1","a5q2","a5q3","a5q4","a5q5","a5q6"},
                };
                for (int a = 0; a < 5; a++) {
                    for (int q = 0; q < 6; q++) {
                        if (!qImgNames[a][q]) { s_qImgCel[a][q] = NULL; continue; }
                        char p[128];
                        sprintf(p, "data\\global\\ui\\SPELLS\\%s", qImgNames[a][q]);
                        s_qImgCel[a][q] = fnCelLoad ? fnCelLoad(p, 0) : NULL;
                    }
                }
            }
            Log("QUEST PAGE: socket=%p a1q1=%p a5q6=%p\n", s_qSocketCel, s_qImgCel[0][0], s_qImgCel[4][5]);
        }

        if (!s_qPosLoaded) {
            s_qPosLoaded = TRUE;
            char qi2[MAX_PATH], qb[32];
            GetArchDir(qi2, MAX_PATH); strcat(qi2, "d2arch.ini");
            GetPrivateProfileStringA("QuestPage", "ActTabX", "40", qb, 31, qi2); s_qActTabX = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "ActTabY", "50", qb, 31, qi2); s_qActTabY = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "ActTabW", "55", qb, 31, qi2); s_qActTabW = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "ActTabH", "22", qb, 31, qi2); s_qActTabH = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "DiffTabX", "400", qb, 31, qi2); s_qDiffTabX = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "DiffTabY", "50", qb, 31, qi2); s_qDiffTabY = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "DiffTabW", "70", qb, 31, qi2); s_qDiffTabW = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "DiffTabH", "22", qb, 31, qi2); s_qDiffTabH = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "GridX", "50", qb, 31, qi2); s_qGridX = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "GridY", "85", qb, 31, qi2); s_qGridY = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "CellW", "90", qb, 31, qi2); s_qCellW = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "CellH", "115", qb, 31, qi2); s_qCellH = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "NameOffX", "40", qb, 31, qi2); s_qNameOffX = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "NameOffY", "108", qb, 31, qi2); s_qNameOffY = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "DetailX", "400", qb, 31, qi2); s_qDetailX = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "DetailY", "85", qb, 31, qi2); s_qDetailY = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "DetailW", "270", qb, 31, qi2); s_qDetailW = atoi(qb);
            GetPrivateProfileStringA("QuestPage", "DetailH", "180", qb, 31, qi2); s_qDetailH = atoi(qb);
        }

        int qmx = MouseX(), qmy = MouseY();

        /* ACT TABS: Act 1-5 with DC6 graphics */
        {
            static void* s_btnActCel[5] = {NULL,NULL,NULL,NULL,NULL};
            static int s_btnActLoaded = -1;   /* gen of last load */
            if (s_btnActLoaded != g_celSessionGen) {
                s_btnActLoaded = g_celSessionGen;
                for (int fi = 0; fi < 5; fi++) EdCelFree(&s_btnActCel[fi]);
                if (fnCelLoad) {
                    s_btnActCel[0] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_act1", 0);
                    s_btnActCel[1] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_act2", 0);
                    s_btnActCel[2] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_act3", 0);
                    s_btnActCel[3] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_act4", 0);
                    s_btnActCel[4] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_act5", 0);
                }
            }
            int atX = s_qActTabX, atY = s_qActTabY, atW = s_qActTabW, atH = s_qActTabH;
            for (int ai = 0; ai < 5; ai++) {
                int ax = atX + ai * (atW + 3);
                BOOL aActive = (s_qPageAct == ai);
                BOOL aHover = InRect(qmx, qmy, ax, atY, atW, atH);
                if (s_btnActCel[ai]) {
                    __try { DrawCel(s_btnActCel[ai], 0, ax, atY + atH); } __except(1) {}
                    if (aActive && fnRect) {
                        fnRect(ax, atY, ax + atW, atY + 1, 4, 5);
                        fnRect(ax, atY + atH - 1, ax + atW, atY + atH, 4, 5);
                        fnRect(ax, atY, ax + 1, atY + atH, 4, 5);
                        fnRect(ax + atW - 1, atY, ax + atW, atY + atH, 4, 5);
                    }
                } else {
                    if (fnRect) {
                        fnRect(ax, atY, ax + atW, atY + atH, aActive ? 4 : (aHover ? 7 : 0), 5);
                        fnRect(ax, atY, ax + atW, atY + 1, 7, 5);
                        fnRect(ax, atY + atH - 1, ax + atW, atY + atH, 7, 5);
                        fnRect(ax, atY, ax + 1, atY + atH, 7, 5);
                        fnRect(ax + atW - 1, atY, ax + atW, atY + atH, 7, 5);
                    }
                    if (fnText && fnFont) {
                        wchar_t atxt[16]; wsprintfW(atxt, L"Act %d", ai + 1);
                        fnFont(6); fnText(atxt, ax + atW/2, atY + 15, aActive ? 0 : 7, 1);
                    }
                }
                if (aHover && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    static DWORD _atclk2 = 0; DWORD _atn2 = GetTickCount();
                    if (_atn2 - _atclk2 > 300) { _atclk2 = _atn2; s_qPageAct = ai; s_qSelectedQuest = -1; }
                }
            }
        }

        /* FILTER TABS (above quest list): Story, Hunt, Kill, Area, WP DC6 buttons for first 4, text fallback for WP */
        {
            static void* s_btnFilterCel[5] = {NULL,NULL,NULL,NULL,NULL};
            static int s_btnFilterLoaded = -1;   /* gen of last load */
            static const wchar_t* filterNames[] = { L"Story", L"Hunt", L"Kill", L"Area", L"WP" };

            if (s_btnFilterLoaded != g_celSessionGen) {
                s_btnFilterLoaded = g_celSessionGen;
                for (int fi = 0; fi < 5; fi++) EdCelFree(&s_btnFilterCel[fi]);
                if (fnCelLoad) {
                    s_btnFilterCel[0] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_story", 0);
                    s_btnFilterCel[1] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_hunt", 0);
                    s_btnFilterCel[2] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_kill", 0);
                    s_btnFilterCel[3] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_area", 0);
                    s_btnFilterCel[4] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_wp", 0);
                }
            }

            static int ftX = 40, ftY = 76, ftW = 50, ftH = 22;
            static BOOL s_ftPosLoaded = FALSE;
            if (s_resetThisFrame) s_ftPosLoaded = FALSE;
            INI_HOT_RELOAD_GUARD(s_ftPosLoaded);
            if (!s_ftPosLoaded) {
                s_ftPosLoaded = TRUE;
                char qi4[MAX_PATH], qb4[32];
                GetArchDir(qi4, MAX_PATH); strcat(qi4, "d2arch.ini");
                GetPrivateProfileStringA("QuestPage", "FilterX", "40", qb4, 31, qi4); ftX = atoi(qb4);
                GetPrivateProfileStringA("QuestPage", "FilterY", "76", qb4, 31, qi4); ftY = atoi(qb4);
                GetPrivateProfileStringA("QuestPage", "FilterW", "50", qb4, 31, qi4); ftW = atoi(qb4);
                GetPrivateProfileStringA("QuestPage", "FilterH", "22", qb4, 31, qi4); ftH = atoi(qb4);
            }
            for (int fi = 0; fi < 5; fi++) {
                int fx = ftX + fi * (ftW + 3);
                BOOL fActive = (s_qFilterTab == fi);
                BOOL fHover = InRect(qmx, qmy, fx, ftY, ftW, ftH);

                /* Draw DC6 button if available, otherwise rect+text fallback */
                if (fi < 5 && s_btnFilterCel[fi]) {
                    __try { DrawCel(s_btnFilterCel[fi], 0, fx, ftY + ftH); } __except(1) {}
                    /* Gold border highlight when active */
                    if (fActive && fnRect) {
                        fnRect(fx, ftY, fx + ftW, ftY + 1, 4, 5);
                        fnRect(fx, ftY + ftH - 1, fx + ftW, ftY + ftH, 4, 5);
                        fnRect(fx, ftY, fx + 1, ftY + ftH, 4, 5);
                        fnRect(fx + ftW - 1, ftY, fx + ftW, ftY + ftH, 4, 5);
                    }
                } else {
                    /* Text fallback (WP tab or if DC6 missing) */
                    if (fnRect) {
                        fnRect(fx, ftY, fx + ftW, ftY + ftH, fActive ? 4 : (fHover ? 7 : 0), 5);
                        fnRect(fx, ftY, fx + ftW, ftY + 1, 7, 5);
                        fnRect(fx, ftY + ftH - 1, fx + ftW, ftY + ftH, 7, 5);
                        fnRect(fx, ftY, fx + 1, ftY + ftH, 7, 5);
                        fnRect(fx + ftW - 1, ftY, fx + ftW, ftY + ftH, 7, 5);
                    }
                    if (fnText && fnFont) {
                        fnFont(6);
                        fnText(filterNames[fi], fx + ftW / 2, ftY + 15, fActive ? 0 : 7, 1);
                    }
                }

                if (fHover && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    static DWORD _ftclk = 0; DWORD _ftn = GetTickCount();
                    if (_ftn - _ftclk > 300) { _ftclk = _ftn; s_qFilterTab = fi; g_questPageScroll = 0; s_qSelectedQuest = -1; }
                }
            }
        }

        /* DIFFICULTY TABS (right side): Normal, Nightmare, Hell */
        {
            static void* s_btnDiffCel[3] = {NULL,NULL,NULL};
            static int s_btnDiffLoaded = -1;   /* gen of last load */
            if (s_btnDiffLoaded != g_celSessionGen) {
                s_btnDiffLoaded = g_celSessionGen;
                for (int fi = 0; fi < 3; fi++) EdCelFree(&s_btnDiffCel[fi]);
                if (fnCelLoad) {
                    s_btnDiffCel[0] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_normal", 0);
                    s_btnDiffCel[1] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_nightmare", 0);
                    s_btnDiffCel[2] = fnCelLoad("data\\global\\ui\\SPELLS\\btn_hell", 0);
                }
            }
            int dtX = s_qDiffTabX, dtY = s_qDiffTabY, dtW = s_qDiffTabW, dtH = s_qDiffTabH;
            for (int di = 0; di < 3; di++) {
                int dx = dtX + di * (dtW + 3);
                BOOL dActive = (s_qDifficulty == di);
                BOOL dHover = InRect(qmx, qmy, dx, dtY, dtW, dtH);
                if (s_btnDiffCel[di]) {
                    __try { DrawCel(s_btnDiffCel[di], 0, dx, dtY + dtH); } __except(1) {}
                    if (dActive && fnRect) {
                        fnRect(dx, dtY, dx + dtW, dtY + 1, 4, 5);
                        fnRect(dx, dtY + dtH - 1, dx + dtW, dtY + dtH, 4, 5);
                        fnRect(dx, dtY, dx + 1, dtY + dtH, 4, 5);
                        fnRect(dx + dtW - 1, dtY, dx + dtW, dtY + dtH, 4, 5);
                    }
                } else {
                    static const wchar_t* diffNames[] = { L"Normal", L"Nightmare", L"Hell" };
                    if (fnRect) {
                        fnRect(dx, dtY, dx + dtW, dtY + dtH, dActive ? 4 : (dHover ? 7 : 0), 5);
                        fnRect(dx, dtY, dx + dtW, dtY + 1, 7, 5);
                        fnRect(dx, dtY + dtH - 1, dx + dtW, dtY + dtH, 7, 5);
                        fnRect(dx, dtY, dx + 1, dtY + dtH, 7, 5);
                        fnRect(dx + dtW - 1, dtY, dx + dtW, dtY + dtH, 7, 5);
                    }
                    if (fnText && fnFont) { fnFont(6); fnText(diffNames[di], dx + dtW/2, dtY + 15, dActive ? 0 : 7, 1); }
                }
                if (dHover && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    static DWORD _dtclk2 = 0; DWORD _dtn2 = GetTickCount();
                    if (_dtn2 - _dtclk2 > 300) { _dtclk2 = _dtn2; s_qDifficulty = di;
                                                 s_qDiffUserPicked = TRUE; /* stop auto-following */ }
                }
            }
        }

        /* Filter type mapping */
        int filterQType = -1;
        switch (s_qFilterTab) {
            case 0: filterQType = QTYPE_QUESTFLAG; break;   /* Story */
            case 1: filterQType = QTYPE_SUPERUNIQUE; break; /* Hunt */
            case 2: filterQType = QTYPE_KILL; break;        /* Kill */
            case 3: filterQType = QTYPE_AREA; break;        /* Area */
            case 4: filterQType = QTYPE_WAYPOINT; break;    /* WP */
        }

        /* respect quest-type toggles (Hunting/KillZn/Explore/Waypnt). */
        BOOL filterActive = IsQuestTypeActive(filterQType);

        /* SCROLLABLE QUEST LIST — Logbook-style plain rows (1.9.0). */
        static int s_qListX = 55, s_qListY = 100, s_qListW = 250, s_qListH = 304;
        static int s_qRowH = 22;
        static BOOL s_qListPosLoaded = FALSE;

        if (s_resetThisFrame) {
            g_questPageScroll = 0;
            s_qListPosLoaded = FALSE;
        }
        INI_HOT_RELOAD_GUARD(s_qListPosLoaded);

        if (!s_qListPosLoaded) {
            s_qListPosLoaded = TRUE;
            char qi3[MAX_PATH], qb3[32];
            GetArchDir(qi3, MAX_PATH); strcat(qi3, "d2arch.ini");
            GetPrivateProfileStringA("QuestPage", "ListX", "55", qb3, 31, qi3); s_qListX = atoi(qb3);
            GetPrivateProfileStringA("QuestPage", "ListY", "100", qb3, 31, qi3); s_qListY = atoi(qb3);
            GetPrivateProfileStringA("QuestPage", "ListW", "250", qb3, 31, qi3); s_qListW = atoi(qb3);
            GetPrivateProfileStringA("QuestPage", "ListH", "304", qb3, 31, qi3); s_qListH = atoi(qb3);
            GetPrivateProfileStringA("QuestPage", "RowH",  "22",  qb3, 31, qi3); s_qRowH  = atoi(qb3);
            if (s_qRowH < 14) s_qRowH = 14;
        }
        #define QUEST_ROW_H s_qRowH

        int numQ = g_acts[s_qPageAct].num;
        int diff = s_qDifficulty;
        if (diff < 0 || diff > 2) diff = 0;

        /* Count filtered quests */
        int numFiltered = 0;
        if (filterActive) {
            for (int ci = 0; ci < numQ; ci++) {
                Quest* cq = &g_acts[s_qPageAct].quests[ci];
                if (cq->id > 0 && (int)cq->type == filterQType) numFiltered++;
            }
        }
        int contentH = numFiltered * QUEST_ROW_H;
        int maxScroll = contentH - s_qListH;
        if (maxScroll < 0) maxScroll = 0;
        if (g_questPageScroll < 0) g_questPageScroll = 0;
        if (g_questPageScroll > maxScroll) g_questPageScroll = maxScroll;

        /* Reset scroll on act/filter change */
        {
            static int s_lastAct2 = -1, s_lastFilter = -1;
            if (s_qPageAct != s_lastAct2 || s_qFilterTab != s_lastFilter) {
                g_questPageScroll = 0; s_qSelectedQuest = -1;
                s_lastAct2 = s_qPageAct; s_lastFilter = s_qFilterTab;
            }
        }

        /* Slider drag */
        {
            int slX = s_qListX - 12, slW = 10;
            if (maxScroll > 0) {
                int thumbH = (s_qListH * s_qListH) / contentH;
                if (thumbH < 20) thumbH = 20;
                BOOL lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (lmb && InRect(qmx, qmy, slX - 4, s_qListY, slW + 8, s_qListH)) {
                    int relY = qmy - s_qListY - thumbH / 2;
                    if (s_qListH - thumbH > 0)
                        g_questPageScroll = (relY * maxScroll) / (s_qListH - thumbH);
                    if (g_questPageScroll < 0) g_questPageScroll = 0;
                    if (g_questPageScroll > maxScroll) g_questPageScroll = maxScroll;
                }
            }
        }

        /* Quest rows (filtered). */
        if (!filterActive && fnText && fnFont) {
            fnFont(6);
            fnText(L"This quest type is disabled in settings.",
                   s_qListX + 5, s_qListY + 24, 7, 0);
        }
        if (filterActive)
        {
            int visIdx = 0;
            for (int qi = 0; qi < numQ; qi++) {
                Quest* q = &g_acts[s_qPageAct].quests[qi];
                if (q->id <= 0) continue;
                if ((int)q->type != filterQType) continue;

                int rowY = s_qListY + visIdx * QUEST_ROW_H - g_questPageScroll;
                visIdx++;

                if (rowY < s_qListY || rowY + QUEST_ROW_H > s_qListY + s_qListH) continue;

                BOOL done = (q->id < MAX_QUEST_ID) ? g_questCompleted[diff][q->id] : FALSE;
                BOOL selected = (s_qSelectedQuest == qi);
                BOOL hover = InRect(qmx, qmy, s_qListX, rowY, s_qListW, QUEST_ROW_H);

                if (fnText && fnFont) {
                    fnFont(6);
                    wchar_t wname[64];
                    MultiByteToWideChar(CP_ACP, 0, q->name, -1, wname, 64);
                    /* Logbook-style colors: done=green(2), selected=gold(4), hover=brighter white(0), default=muted(5). */
                    int color = done ? 2 : (selected ? 4 : (hover ? 0 : 7));
                    int textY = rowY + (QUEST_ROW_H / 2) + 4;
                    fnText(wname, s_qListX + 5, textY, color, 0);

                    /* Right-aligned status (matches Logbook value column). */
                    if (q->type == QTYPE_KILL && q->killReq > 0 && q->id < MAX_QUEST_ID) {
                        wchar_t wp[16];
                        int kills = g_questKillCount[diff][q->id];
                        wsprintfW(wp, L"%d / %d", kills, q->killReq);
                        int pcolor = (kills >= q->killReq) ? 2 : (selected ? 4 : 0);
                        fnText(wp, s_qListX + s_qListW - 60, textY, pcolor, 0);
                    } else {
                        const wchar_t* status = done ? L"Done" : L"-";
                        int pcolor = done ? 2 : (selected ? 4 : 7);
                        fnText((wchar_t*)status, s_qListX + s_qListW - 30, textY, pcolor, 0);
                    }
                }

                if (hover && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    static DWORD _qlclk = 0; DWORD _qln = GetTickCount();
                    if (_qln - _qlclk > 200) { _qlclk = _qln; s_qSelectedQuest = qi; }
                }
            }
        }

        /* Slider draw (on top of rows) */
        {
            int slX = s_qListX - 12, slW = 10;
            if (maxScroll > 0 && fnRect) {
                int thumbH = (s_qListH * s_qListH) / contentH;
                if (thumbH < 20) thumbH = 20;
                fnRect(slX, s_qListY, slX + slW, s_qListY + s_qListH, 0, 5);
                int thumbY = s_qListY + (g_questPageScroll * (s_qListH - thumbH)) / maxScroll;
                fnRect(slX, thumbY, slX + slW, thumbY + thumbH, 7, 5);
            }
        }

        /* Progress counter */
        {
            static int s_progX = 55, s_progY = 410;
            static BOOL s_progLoaded = FALSE;
            if (s_resetThisFrame) s_progLoaded = FALSE;
            INI_HOT_RELOAD_GUARD(s_progLoaded);
            if (!s_progLoaded) {
                s_progLoaded = TRUE;
                char qi5[MAX_PATH], qb5[32];
                GetArchDir(qi5, MAX_PATH); strcat(qi5, "d2arch.ini");
                GetPrivateProfileStringA("QuestPage", "ProgressX", "55", qb5, 31, qi5); s_progX = atoi(qb5);
                GetPrivateProfileStringA("QuestPage", "ProgressY", "410", qb5, 31, qi5); s_progY = atoi(qb5);
            }
            if (fnText && fnFont) {
                /* only count quests whose type is enabled, so the progress line matches what the user actually sees in the list and tracker. */
                int totalAct = 0, doneAct = 0;
                for (int ci = 0; ci < g_acts[s_qPageAct].num; ci++) {
                    Quest* cq = &g_acts[s_qPageAct].quests[ci];
                    if (cq->id <= 0) continue;
                    if (!IsQuestTypeActive((int)cq->type)) continue;
                    totalAct++;
                    if (cq->id < MAX_QUEST_ID && g_questCompleted[diff][cq->id]) doneAct++;
                }
                wchar_t wProg[48];
                wsprintfW(wProg, L"Progress: %d / %d", doneAct, totalAct);
                fnFont(6);
                fnText(wProg, s_progX, s_progY,
                       (totalAct > 0 && doneAct == totalAct) ? 2 : 0, 0);
            }
        }

        /* Level milestone labels */
        {
            static int s_lvlX = 55, s_lvlY = 426;
            static BOOL s_lvlLoaded = FALSE;
            if (s_resetThisFrame) s_lvlLoaded = FALSE;
            INI_HOT_RELOAD_GUARD(s_lvlLoaded);
            if (!s_lvlLoaded) {
                s_lvlLoaded = TRUE;
                char qi6[MAX_PATH], qb6[32];
                GetArchDir(qi6, MAX_PATH); strcat(qi6, "d2arch.ini");
                GetPrivateProfileStringA("QuestPage", "LevelX", "55", qb6, 31, qi6); s_lvlX = atoi(qb6);
                GetPrivateProfileStringA("QuestPage", "LevelY", "426", qb6, 31, qi6); s_lvlY = atoi(qb6);
            }
        if (fnText && fnFont) {
            /* Show level milestones in 3 rows: Normal / NM / Hell */
            static const wchar_t* diffLabel[] = { L"Norm:", L"NM:", L"Hell:" };
            static const int diffRanges[][2] = { {5,30}, {35,55}, {60,75} };
            fnFont(6);
            /* V8 (T7) — heading so players know these numbers ARE checks. */
            fnText(L"Level checks:", s_lvlX, s_lvlY, 4, 0);
            int lblY = s_lvlY;
            for (int dRow = 0; dRow < 3; dRow++) {
                int lblX = s_lvlX;
                fnText(diffLabel[dRow], lblX, lblY + 12, 7, 0);
                lblX += 38;
                for (int ai = 0; ai < 5; ai++) {
                    for (int qi = 0; qi < g_acts[ai].num; qi++) {
                        Quest* q = &g_acts[ai].quests[qi];
                        if (q->id <= 0 || q->type != QTYPE_LEVEL) continue;
                        if (q->param < diffRanges[dRow][0] || q->param > diffRanges[dRow][1]) continue;
                        BOOL lvlDone = (q->id < MAX_QUEST_ID) ? g_questCompleted[diff][q->id] : FALSE;
                        wchar_t lvlTxt[16];
                        wsprintfW(lvlTxt, L"%d", q->param);
                        fnText(lvlTxt, lblX, lblY + 12, lvlDone ? 2 : 0, 0);
                        if (InRect(qmx, qmy, lblX, lblY, 25, 16) && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                            static DWORD _lvclk2 = 0; DWORD _lvn2 = GetTickCount();
                            if (_lvn2 - _lvclk2 > 200) { _lvclk2 = _lvn2; s_qSelectedQuest = qi; s_qPageAct = ai; }
                        }
                        lblX += 28;
                    }
                }
                lblY += 16;
            }
        }
        }

        /* Detail panel (right side) */
        if (s_qSelectedQuest >= 0 && s_qSelectedQuest < numQ) {
            Quest* sq = &g_acts[s_qPageAct].quests[s_qSelectedQuest];
            if (sq->id > 0) {
                int detX = s_qDetailX, detY = s_qDetailY;
                BOOL done = (sq->id < MAX_QUEST_ID) ? g_questCompleted[diff][sq->id] : FALSE;

                if (fnText && fnFont) {
                    wchar_t wn2[64];
                    MultiByteToWideChar(CP_ACP, 0, sq->name, -1, wn2, 64);
                    fnFont(0); fnText(wn2, detX, detY + 15, 4, 0);

                    fnFont(6);
                    fnText(done ? L"Completed" : L"Not Started", detX, detY + 35, done ? 2 : 7, 0);

                    wchar_t wdesc[128];
                    MultiByteToWideChar(CP_ACP, 0, sq->desc, -1, wdesc, 128);
                    fnText(wdesc, detX, detY + 55, 0, 0);

                    /* Type name */
                    static const wchar_t* typeNames[] = {
                        L"Boss Hunt", L"Area Discovery", L"Kill Zone",
                        L"Waypoint", L"Story Quest", L"Super Unique Hunt", L"Level Milestone"
                    };
                    int tt = (int)sq->type;
                    if (tt < 0 || tt > 6) tt = 0;
                    fnText(typeNames[tt], detX, detY + 75, 7, 0);

                    /* WHERE. The page named a target and stopped there, which is fine until the name lies: three players spent an evening hunting Pitspawn Fouldog in the Pit, where he has never been (Jail Level 2). The area comes from the same region map zone locking gates on, so the book now says out loud what the logic already believed — which also means a wrong entry is visible to players instead of only to the generator. (That is how the Pitspawn one was found.) */
                    {
                        int qz = QuestZone_For(sq->id);

                        /* The quest's own zone used to be suppressed for hunts
                         * (qz = -1) because the hunt area table was not trusted.
                         * It is now: all 47 hunts carry a verified zone (the
                         * Pitspawn = Jail 2 report is fixed at the source), so a
                         * hunt with the shuffle OFF shows its real spawn.
                         *
                         * With the shuffle ON the identity has MOVED: hunt X's
                         * hcIdx now stands on some other boss's spot. That other
                         * boss keeps its own spawn point, so its zone is where X
                         * is actually found. SUShuffle_HostFor names that host,
                         * and the shuffle only ever swaps within one act/region
                         * bucket, so the host always has a hunt of its own to
                         * read the zone from. */
                        const char* moved = NULL;
                        if (sq->type == QTYPE_SUPERUNIQUE) {
                            moved = SUShuffle_HostFor(sq->param);
                            if (moved && moved[0]) {
                                for (int am = 0; am < 5; am++) {
                                    for (int qm = 0; qm < g_acts[am].num; qm++) {
                                        Quest* hq = &g_acts[am].quests[qm];
                                        if (hq->type != QTYPE_SUPERUNIQUE) continue;
                                        if (!strstr(hq->name, moved)) continue;
                                        int hz = QuestZone_For(hq->id);
                                        if (hz >= 1) qz = hz;
                                        am = 5; break;
                                    }
                                }
                            }
                        }

                        /* g_zoneNames is indexed by area id DIRECTLY — entry 1
                         * is area 1 (Rogue Encampment). The old qz-1 read here
                         * named the area one id below the true one for every
                         * quest on this page, which is very likely why the hunt
                         * zones were written off as untrustworthy: the table
                         * was right, the read was off by one. */
                        const char* zn = Preload_ZoneName(qz);
                        if (zn && zn[0]) {
                            wchar_t wz[128];
                            char buf[128];
                            /* Say the name the player will read over its head.
                             * "at X's spot" was a place; this is a target. */
                            const char* killName = (sq->type == QTYPE_SUPERUNIQUE)
                                ? SUShuffle_KillNameFor(sq->param) : NULL;
                            if (killName && killName[0])
                                _snprintf(buf, sizeof(buf),
                                          "In %s - kill %s", zn, killName);
                            else
                                _snprintf(buf, sizeof(buf), "Found in: %s", zn);
                            buf[sizeof(buf) - 1] = 0;
                            MultiByteToWideChar(CP_ACP, 0, buf, -1, wz, 128);
                            fnText(wz, detX, detY + 95, 8, 0);   /* orange */
                        }
                    }

                    /* Kill progress for QTYPE_KILL */
                    if (sq->type == QTYPE_KILL && sq->killReq > 0 && sq->id < MAX_QUEST_ID) {
                        wchar_t wk[32];
                        int kills = g_questKillCount[diff][sq->id];
                        wsprintfW(wk, L"Kills: %d / %d", kills, sq->killReq);
                        fnText(wk, detX, detY + 95, kills >= sq->killReq ? 2 : 0, 0);
                    }

                    /* Reward: Progression or Filler */
                    fnText(sq->reward == REWARD_SKILL ? L"Progression" : L"Filler",
                        detX, detY + 115, 9, 0);

                    /* Act + difficulty */
                    wchar_t wa2[32];
                    wsprintfW(wa2, L"Act %d - %s", s_qPageAct + 1,
                        diff == 0 ? L"Normal" : diff == 1 ? L"Nightmare" : L"Hell");
                    fnText(wa2, detX, detY + 135, 7, 0);
                }
            }
        }
    }

    /* === PAGE 2: Archipelago Connection === */
    if (g_editorPage == 2) {
        int mx2 = MouseX(), my2 = MouseY();

        /* g_apPageFocus declared globally for WndProc keyboard access */

        if (fnText && fnFont) {
            /* LEFT SIDE: Connection info + editable fields */
            fnFont(0);
            fnText(L"Archipelago", 180, 82, 4, 1);

            /* the Multiworld-Launcher owns the AP connection; hide the in-game login panel (status + Server/Slot/Pass + Connect/Reconnect) when launcher-controlled. */
            if (!g_launcherHidesTitleUI) {
            fnFont(6);
            /* Status */
            fnText(g_apConnected ? L"Connected" : L"Disconnected", 50, 85, g_apConnected ? 2 : 1, 0);
            {
                wchar_t wSt[64]; MultiByteToWideChar(CP_ACP, 0, g_apStatus, -1, wSt, 64);
                fnText(wSt, 160, 85, 7, 0);
            }
            /* 1.9.5 Bug C3 fix — show last error reason in red beneath the status line when the connection is in error/refused. */
            extern char g_apErrorMsg[128];
            if (!g_apConnected && g_apErrorMsg[0]) {
                wchar_t wEm[160];
                MultiByteToWideChar(CP_ACP, 0, g_apErrorMsg, -1, wEm, 160);
                fnText(wEm, 50, 100, 11 /* red */, 0);
            }

            /* Editable fields: Server, Slot, Password */
            {
                static const wchar_t* labels[] = { L"Server:", L"Slot:", L"Pass:" };
                char* fields[] = { g_apIP, g_apSlot, g_apPassword };
                int maxLens[] = { 62, 30, 30 };
                int fy = 105;

                for (int fi = 0; fi < 3; fi++) {
                    int fx = 50, fw = 230, fh = 20;
                    BOOL focused = (g_apPageFocus == fi);
                    BOOL hover = InRect(mx2, my2, fx + 55, fy, fw - 55, fh);

                    /* Label */
                    fnText(labels[fi], fx, fy + 14, 7, 0);

                    /* Input background (semi-transparent) */
                    if (fnRect) {
                        fnRect(fx+55, fy, fx+fw, fy+fh, 0, 2);
                        /* Border: gold if focused */
                        int bc = focused ? 4 : 7;
                        fnRect(fx+55,fy,fx+fw,fy+1,bc,5); fnRect(fx+55,fy+fh-1,fx+fw,fy+fh,bc,5);
                        fnRect(fx+55,fy,fx+56,fy+fh,bc,5); fnRect(fx+fw-1,fy,fx+fw,fy+fh,bc,5);
                    }

                    /* Field text (mask password) */
                    wchar_t wField[80];
                    if (fi == 2 && fields[fi][0]) {
                        /* Show asterisks for password */
                        int plen = (int)strlen(fields[fi]);
                        for (int pi = 0; pi < plen && pi < 79; pi++) wField[pi] = L'*';
                        wField[plen < 79 ? plen : 79] = 0;
                    } else {
                        MultiByteToWideChar(CP_ACP, 0, fields[fi], -1, wField, 80);
                    }
                    /* Add cursor blink when focused */
                    if (focused && (GetTickCount() / 500) % 2 == 0) wcscat(wField, L"|");
                    fnText(wField, fx + 60, fy + 14, focused ? 0 : 4, 0);

                    /* Click to focus */
                    if (hover && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                        g_apPageFocus = fi;
                    }

                    fy += 26;
                }

                /* Keyboard input for focused field */
                /* (handled via g_apPageKeyPending set from WndProc) */
            }

            /* Reconnect / Disconnect button */
            {
                int bx = 50, by = 190, bw = 140, bh = 24;
                BOOL bHov = InRect(mx2, my2, bx, by, bw, bh);
                if (fnRect) {
                    fnRect(bx, by, bx+bw, by+bh, bHov ? 4 : 0, 5);
                    fnRect(bx,by,bx+bw,by+1,7,5); fnRect(bx,by+bh-1,bx+bw,by+bh,7,5);
                    fnRect(bx,by,bx+1,by+bh,7,5); fnRect(bx+bw-1,by,bx+bw,by+bh,7,5);
                }
                fnText(g_apConnected ? L"Disconnect" : L"Connect", bx+bw/2, by+17, bHov ? 0 : 7, 1);
                if (bHov && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    static DWORD _rc2 = 0; if (GetTickCount() - _rc2 > 1000) { _rc2 = GetTickCount();
                        if (g_apConnected) {
                            WriteAPCommand("disconnect");
                            g_apConnected = FALSE;
                            strcpy(g_apStatus, "Disconnecting...");
                        } else if (g_apIP[0] && g_apSlot[0]) {
                            /* 1.9.5 Gap 4 — slot-change collision check. */
                            CheckSlotChangeOnConnect();
                            StartAPBridge();
                            WriteAPCommand("connect");
                            /* set g_apPolling (NOT g_apMode); g_apMode is now gated on actual auth in PollAPStatus. */
                            g_apPolling = TRUE;
                            strcpy(g_apStatus, "Connecting...");
                        }
                    }
                }
            }

            /* 1.9.5 Gap 5 — Force Reconnect button. */
            if (g_apPolling && !g_apConnected
                && strcmp(g_apStatus, "Disconnected")    != 0
                && strcmp(g_apStatus, "Disconnecting...") != 0) {
                int bx = 200, by = 190, bw = 140, bh = 24;
                BOOL bHov = InRect(mx2, my2, bx, by, bw, bh);
                if (fnRect) {
                    fnRect(bx, by, bx+bw, by+bh, bHov ? 4 : 0, 5);
                    fnRect(bx,by,bx+bw,by+1,7,5); fnRect(bx,by+bh-1,bx+bw,by+bh,7,5);
                    fnRect(bx,by,bx+1,by+bh,7,5); fnRect(bx+bw-1,by,bx+bw,by+bh,7,5);
                }
                fnText(L"Force Reconnect", bx+bw/2, by+17, bHov ? 0 : 7, 1);
                if (bHov && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    static DWORD _rc3 = 0;
                    if (GetTickCount() - _rc3 > 1000) {
                        _rc3 = GetTickCount();
                        /* Force a fresh connect attempt by writing the command again. */
                        Log("AP: Force Reconnect clicked\n");
                        ShowNotify("AP: Forcing reconnect...");
                        /* 1.9.5 Gap 7 fix — Force Reconnect must run the same slot-change wipe protection as the normal Connect path. */
                        CheckSlotChangeOnConnect();
                        StartAPBridge();
                        WriteAPCommand("connect");
                        strcpy(g_apStatus, "Force reconnecting...");
                    }
                }
            }
            }   /* end: launcher-hide AP login panel */

            /* Character + mode + settings */
            {
                wchar_t wChar[40]; MultiByteToWideChar(CP_ACP, 0, g_charName, -1, wChar, 40);
                fnText(L"Character:", 50, 228, 7, 0);
                fnText(wChar, 140, 228, 0, 0);

                int sy = 248;
                {
                    const wchar_t* modeLabel;
                    if (g_skillHuntingOn && g_zoneLockingOn)      modeLabel = L"Mode: Skill Hunt + Zone Lock";
                    else if (g_zoneLockingOn)                      modeLabel = L"Mode: Zone Locking";
                    else if (g_skillHuntingOn)                     modeLabel = L"Mode: Skill Hunting";
                    else                                            modeLabel = L"Mode: (none)";
                    fnText((wchar_t*)modeLabel, 50, sy, 9, 0); sy += 16;
                }

                wchar_t ws[64];
                wsprintfW(ws, L"XP +%d00%%", g_xpMultiplier);  /* 0-based: +N00% bonus */
                fnText(ws, 50, sy, 0, 0);
                /* show the EFFECTIVE in-game pool (g_poolCount, after the class-locked native-only/trap skills are excluded) rather than the raw 210 — the removed class-locked skills aren't in the pool. */
                {
                    int poolShown = g_poolCount;
                    if (g_apMode && g_apSkillPoolSize > 0 && g_apSkillPoolSize < g_poolCount)
                        poolShown = g_apSkillPoolSize;
                    wsprintfW(ws, L"Pool: %d", poolShown);
                }
                fnText(ws, 145, sy, 0, 0);
                wsprintfW(ws, L"Start: %d", g_apStartingSkills);
                fnText(ws, 235, sy, 0, 0);
                sy += 16;

                fnText(L"Quests:", 50, sy, 7, 0);
                int qx = 110;
                if (g_apQuestStory) { fnText(L"Story", qx, sy, 2, 0); qx += 45; }
                if (g_apQuestHunting) { fnText(L"Hunt", qx, sy, 2, 0); qx += 40; }
                if (g_apQuestKillZones) { fnText(L"Kill", qx, sy, 2, 0); qx += 35; }
                if (g_apQuestExploration) { fnText(L"Area", qx, sy, 2, 0); qx += 38; }
                if (g_apQuestWaypoints) { fnText(L"WP", qx, sy, 2, 0); qx += 25; }
                if (g_apQuestLevelMilestones) { fnText(L"Lvl", qx, sy, 2, 0); }
                sy += 16;

                fnText(L"Shuffle:", 50, sy, 7, 0);
                int shx = 110;
                fnText(g_monsterShuffleEnabled ? L"Mon:ON" : L"Mon:OFF", shx, sy, g_monsterShuffleEnabled ? 2 : 1, 0); shx += 72;
                fnText(g_bossShuffleEnabled ? L"Boss:ON" : L"Boss:OFF", shx, sy, g_bossShuffleEnabled ? 2 : 1, 0);
                sy += 16;

                /* Goal — 1.8.5: g_apGoal is 0..2 (full-game scope per difficulty) since 1.8.x. */
                {
                    static const wchar_t* dNames[] = {L"Normal", L"Nightmare", L"Hell"};
                    int gd = (g_apGoal >= 0 && g_apGoal <= 2) ? g_apGoal : 2;
                    wsprintfW(ws, L"Goal: Full Game (%s)", dNames[gd]);
                    fnText(ws, 50, sy, 9, 0);
                }
            }

            /* Click outside fields = unfocus */
            if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) && g_apPageFocus >= 0) {
                if (!InRect(mx2, my2, 105, 105, 175, 78))
                    g_apPageFocus = -1;
            }

            /* RIGHT SIDE: Item log + per-category check counts. */
            static int s_itLogLoaded = 0;
            static int s_itLogHdrX = 420, s_itLogHdrY = 60;
            static int s_itLogBodyX = 420, s_itLogBodyY = 90;
            static int s_itLogRowH  = 18;
            if (s_resetThisFrame) s_itLogLoaded = 0;
            INI_HOT_RELOAD_GUARD(s_itLogLoaded);
            if (!s_itLogLoaded) {
                s_itLogLoaded = 1;
                char ini7[MAX_PATH]; GetArchDir(ini7, MAX_PATH); strcat(ini7, "d2arch.ini");
                s_itLogHdrX  = GetPrivateProfileIntA("EditorItemLog", "HeaderX", 420, ini7);
                s_itLogHdrY  = GetPrivateProfileIntA("EditorItemLog", "HeaderY",  60, ini7);
                s_itLogBodyX = GetPrivateProfileIntA("EditorItemLog", "BodyX",   420, ini7);
                s_itLogBodyY = GetPrivateProfileIntA("EditorItemLog", "BodyY",    90, ini7);
                s_itLogRowH  = GetPrivateProfileIntA("EditorItemLog", "RowH",     18, ini7);
            }
            fnFont(0);
            fnText(L"Item Log", s_itLogHdrX, s_itLogHdrY, 4, 1);

            fnFont(6);
            fnText(L"Checks (enabled categories):", s_itLogBodyX, s_itLogBodyY, 7, 0);

            {
                int ry = s_itLogBodyY + s_itLogRowH;
                wchar_t wb[80];
                int doneTotal = 0, allTotal = 0;

                /* Quest categories — collapsed under "Quests" since CompletedQuests / TotalQuests already aggregate across the enabled subset (see TotalQuests in d2arch_quests.c). */
                if (g_apQuestStory || g_apQuestHunting || g_apQuestKillZones ||
                    g_apQuestExploration || g_apQuestWaypoints || g_apQuestLevelMilestones) {
                    int qd = CompletedQuests();
                    int qa = TotalQuests();
                    wsprintfW(wb, L"Quests: %d / %d", qd, qa);
                    fnText(wb, s_itLogBodyX, ry, 0, 0); ry += s_itLogRowH;
                    doneTotal += qd; allTotal += qa;
                }

                /* Skills — only when Skill Hunting is enabled (otherwise the pool is class-only and not really a "check"). */
                if (g_skillHuntingOn && g_poolCount > 0) {
                    int unlocked = 0;
                    for (int i = 0; i < g_poolCount; i++) if (g_pool[i].unlocked) unlocked++;
                    wsprintfW(wb, L"Skills: %d / %d", unlocked, g_poolCount);
                    fnText(wb, s_itLogBodyX, ry, 0, 0); ry += s_itLogRowH;
                    doneTotal += unlocked; allTotal += g_poolCount;
                }

                /* Bonus check categories — only show enabled ones. */
                extern BOOL Bonus_IsCategoryEnabled(int cat);
                extern int  Bonus_GetSlotCount(int cat, int diff);
                extern int  Bonus_GetQuota(int cat);
                extern int  Bonus_CountFiredSetPickups(void);
                extern BOOL Bonus_IsGoldMilestoneFired(int idx);
                static const wchar_t* bxNames[6] = {
                    L"Shrines", L"Urns", L"Barrels", L"Chests",
                    L"Set Pickups", L"Gold Milestones"
                };
                int diffsEnabled = g_apDiffScope + 1;
                if (diffsEnabled < 1) diffsEnabled = 1;
                if (diffsEnabled > 3) diffsEnabled = 3;
                for (int c = 0; c < 4; c++) {
                    if (!Bonus_IsCategoryEnabled(c)) continue;
                    int got = 0;
                    for (int d = 0; d < diffsEnabled; d++) got += Bonus_GetSlotCount(c, d);
                    int max = Bonus_GetQuota(c) * diffsEnabled;
                    wsprintfW(wb, L"%s: %d / %d", bxNames[c], got, max);
                    fnText(wb, s_itLogBodyX, ry, 0, 0); ry += s_itLogRowH;
                    doneTotal += got; allTotal += max;
                }
                if (Bonus_IsCategoryEnabled(4)) {
                    int got = Bonus_CountFiredSetPickups();
                    wsprintfW(wb, L"%s: %d / 127", bxNames[4], got);
                    fnText(wb, s_itLogBodyX, ry, 0, 0); ry += s_itLogRowH;
                    doneTotal += got; allTotal += 127;
                }
                if (Bonus_IsCategoryEnabled(5)) {
                    static const int gmPerDiff[3] = { 7, 5, 5 };
                    int gmMax = 0;
                    for (int d = 0; d < diffsEnabled; d++) gmMax += gmPerDiff[d];
                    int got = 0;
                    for (int i = 0; i < gmMax; i++) if (Bonus_IsGoldMilestoneFired(i)) got++;
                    wsprintfW(wb, L"%s: %d / %d", bxNames[5], got, gmMax);
                    fnText(wb, s_itLogBodyX, ry, 0, 0); ry += s_itLogRowH;
                    doneTotal += got; allTotal += gmMax;
                }

                /* extra-check categories appended to the same Logbook section. */
                {
                    extern BOOL Extra_IsCategoryEnabled(int cat);
                    extern int  Extra_GetSlotCount(int cat);
                    extern int  Extra_CountFiredCategory(int cat);
                    static const wchar_t* exNames[6] = {
                        L"Cow Level",  L"Mercenary",   L"HF + Runes",
                        L"NPC Talk",   L"Runewords",   L"Cube Recipes"
                    };
                    for (int c = 0; c < 6; c++) {
                        if (!Extra_IsCategoryEnabled(c)) continue;
                        int got = Extra_CountFiredCategory(c);
                        int max = Extra_GetSlotCount(c);
                        wsprintfW(wb, L"%s: %d / %d", exNames[c], got, max);
                        fnText(wb, s_itLogBodyX, ry, 0, 0); ry += s_itLogRowH;
                        doneTotal += got; allTotal += max;
                    }
                }

                /* Collection + Zones appended so the Item Log "Total Checks" matches the F1 Overview total (which already includes both categories). */
                {
                    int setsGot   = Coll_CountCollectedInRange(COLL_SLOT_SETS_BASE,
                                        COLL_SLOT_SETS_BASE + COLL_NUM_SET_PIECES - 1);
                    int runesGot  = Coll_CountCollectedInRange(COLL_SLOT_RUNES_BASE,
                                        COLL_SLOT_RUNES_BASE + COLL_NUM_RUNES - 1);
                    int gemsGot   = Coll_CountCollectedInRange(COLL_SLOT_GEMS_BASE,
                                        COLL_SLOT_GEMS_BASE + COLL_NUM_GEMS - 1);
                    int specGot   = Coll_CountCollectedInRange(COLL_SLOT_SPECIALS_BASE,
                                        COLL_SLOT_SPECIALS_BASE + COLL_NUM_SPECIALS - 1);
                    int collGot = setsGot + runesGot + gemsGot + specGot;
                    int collMax = COLL_NUM_SET_PIECES + COLL_NUM_RUNES +
                                  COLL_NUM_GEMS + COLL_NUM_SPECIALS;
                    wsprintfW(wb, L"Collection: %d / %d", collGot, collMax);
                    fnText(wb, s_itLogBodyX, ry, 0, 0); ry += s_itLogRowH;
                    doneTotal += collGot; allTotal += collMax;
                }
                if (g_zoneLockingOn) {
                    int zoneTotalDone = 0, zoneTotalAll = 0;
                    int maxAct = GetMaxActForGoal();
                    for (int d = 0; d <= g_apDiffScope && d < 3; d++) {
                        for (int act = 1; act <= maxAct; act++) {
                            int numGates = g_actRegions[act - 1].num_gates;
                            zoneTotalAll += numGates;
                            for (int gi = 0; gi < numGates; gi++) {
                                int slot = GateKey_SlotFromActGate(act, gi);
                                if (slot >= 0 && g_gateBossKilled[d][slot])
                                    zoneTotalDone++;
                            }
                        }
                    }
                    wsprintfW(wb, L"Zones: %d / %d", zoneTotalDone, zoneTotalAll);
                    fnText(wb, s_itLogBodyX, ry, 0, 0); ry += s_itLogRowH;
                    doneTotal += zoneTotalDone; allTotal += zoneTotalAll;
                }

                /* Combined total + status. */
                ry += 4;
                wsprintfW(wb, L"Total Checks: %d / %d", doneTotal, allTotal);
                fnText(wb, s_itLogBodyX, ry, 4, 0); ry += s_itLogRowH;
                fnText(g_apGoalComplete ? L"GOAL COMPLETE!" : L"Goal: In Progress",
                       s_itLogBodyX, ry, g_apGoalComplete ? 2 : 9, 0);
                ry += s_itLogRowH + 6;

                /* Item Log per-event list below summary stats */
                if (ItemLogCount() > 0) {
                    fnFont(6);
                    fnText(L"---", s_itLogBodyX, ry, 7, 0); ry += s_itLogRowH;
                    ItemLogDraw(s_itLogBodyX, ry, 10);
                }
            }
        }
    }

    /* === Next / Back buttons (always visible on all pages) === */
    {
        static void* s_btnNextCel = NULL;
        static void* s_btnBackCel = NULL;
        static int s_navLoaded = -1;   /* gen of last load (was load-once BOOL) */
        static int s_nextX = 0, s_nextY = 0;
        static int s_backX = 0, s_backY = 0;
        /* was load-once, which DANGLED after save&quit: D2 frees the DC6 archive, so the cached btn_next/btn_back handles went stale and drawing them asserted in CelCmp.cpp:1485 -> hard-lock. */
        INI_HOT_RELOAD_GUARD(s_navLoaded);
        if (s_navLoaded != g_celSessionGen) {
            s_navLoaded = g_celSessionGen;
            EdCelFree(&s_btnNextCel);
            EdCelFree(&s_btnBackCel);
            if (fnCelLoad) {
                s_btnNextCel = fnCelLoad("data\\global\\ui\\SPELLS\\btn_next", 0);
                s_btnBackCel = fnCelLoad("data\\global\\ui\\SPELLS\\btn_back", 0);
            }
            char ini6[MAX_PATH], buf6[32];
            GetArchDir(ini6, MAX_PATH); strcat(ini6, "d2arch.ini");
            GetPrivateProfileStringA("EditorButtons", "NextX", "500", buf6, 31, ini6);
            s_nextX = atoi(buf6);
            GetPrivateProfileStringA("EditorButtons", "NextY", "450", buf6, 31, ini6);
            s_nextY = atoi(buf6);
            GetPrivateProfileStringA("EditorButtons", "BackX", "400", buf6, 31, ini6);
            s_backX = atoi(buf6);
            GetPrivateProfileStringA("EditorButtons", "BackY", "450", buf6, 31, ini6);
            s_backY = atoi(buf6);
        }
        /* === PAGES 3..7: F1 Collection (1.9.0 NEW) === */
        if (g_editorPage >= PAGE_SETS_1 && g_editorPage <= PAGE_SPECIALS) {
            extern void Coll_RenderF1Page(int mouseX, int mouseY, BOOL clicked);
            BOOL clicked = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            Coll_RenderF1Page(MouseX(), MouseY(), clicked);
        }

        /* === PAGE 9: Overview (1.9.0 NEW) === */
        if (g_editorPage == PAGE_OVERVIEW) {
            BOOL clicked = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            Editor_RenderOverviewPage(MouseX(), MouseY(), clicked);
        }

        /* === PAGE 11: Hidden stats (3.4.1 NEW) === */
        if (g_editorPage == PAGE_STATS) {
            BOOL clicked = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            Editor_RenderStatsPage(MouseX(), MouseY(), clicked);
        }

        /* === PAGE 10: Zones — folded F4 zone tracker (1.9.0 NEW) === */
        if (g_editorPage == PAGE_ZONES) {
            BOOL clicked = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            Editor_RenderZonesPage(MouseX(), MouseY(), clicked);
        }

        /* === PAGE 8: Statistics / Logbook (1.9.0 NEW) */
        if (g_editorPage == 8) {
            extern void Stats_RenderF1Page(int mouseX, int mouseY, BOOL clicked);
            extern void Stats_HandleScroll(int side, int delta);
            BOOL clicked = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            Stats_RenderF1Page(MouseX(), MouseY(), clicked);

            /* Mouse-wheel handling lives in WndProc (WM_MOUSEWHEEL) in d2arch_main.c — see the g_editorPage==8 branch there. */

            /* Page-Up / Page-Down keys scroll the column under the mouse cursor (left page if mx<350, right if mx>=350). */
            {
                static DWORD _pgC = 0;
                DWORD _pgN = GetTickCount();
                if ((GetAsyncKeyState(VK_PRIOR) & 0x8000) && (_pgN - _pgC) > 100) {
                    _pgC = _pgN;
                    int side = (MouseX() < 350) ? 0 : 1;
                    Stats_HandleScroll(side, +3);
                }
                if ((GetAsyncKeyState(VK_NEXT) & 0x8000) && (_pgN - _pgC) > 100) {
                    _pgC = _pgN;
                    int side = (MouseX() < 350) ? 0 : 1;
                    Stats_HandleScroll(side, -3);
                }
            }
        }

        /* Draw Next/Back buttons — kept as a fallback to the top tab bar. */
        {
            int nmx = MouseX(), nmy = MouseY();
            int curTab = Editor_TabFromPage(g_editorPage);
            if (curTab < 0) curTab = 0;
            int firstVis = curTab; for (int t = 0; t < NUM_EDITOR_TABS; t++) {
                if (Editor_TabVisible(t)) { firstVis = t; break; }
            }
            int lastVis = curTab; for (int t = NUM_EDITOR_TABS - 1; t >= 0; t--) {
                if (Editor_TabVisible(t)) { lastVis = t; break; }
            }

            if (fnCelDraw && s_btnNextCel && s_nextX > 0 && curTab != lastVis) {
                fnCelDraw(s_btnNextCel, s_nextX, s_nextY, 0, 5, -1);
                if (InRect(nmx, nmy, s_nextX, s_nextY - 22, 50, 22) &&
                    (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    static DWORD _nc = 0; DWORD _nn = GetTickCount();
                    if (_nn - _nc > 300) {
                        _nc = _nn;
                        int nextTab = Editor_NextVisibleTab(curTab, +1);
                        g_editorPage = g_tabs[nextTab].rawPageIdx;
                        g_apPageFocus = -1;
                        g_questPageScroll = 0;
                    }
                }
            }

            if (fnCelDraw && s_btnBackCel && s_backX > 0 && curTab != firstVis) {
                fnCelDraw(s_btnBackCel, s_backX, s_backY, 0, 5, -1);
                if (InRect(nmx, nmy, s_backX, s_backY - 22, 50, 22) &&
                    (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    static DWORD _bc = 0; DWORD _bn = GetTickCount();
                    if (_bn - _bc > 300) {
                        _bc = _bn;
                        int prevTab = Editor_NextVisibleTab(curTab, -1);
                        g_editorPage = g_tabs[prevTab].rawPageIdx;
                        g_apPageFocus = -1;
                        g_questPageScroll = 0;
                    }
                }
            }
        }
    }

    /* Draw dragged skill icon at cursor */
    if (g_edDragActive && g_edDragPoolIdx >= 0 && g_edDragPoolIdx < g_poolCount) {
        int dmx = MouseX(), dmy = MouseY();
        int dbIdx = g_pool[g_edDragPoolIdx].dbIndex;
        __try {
            if (g_icons28)
                DrawCel(g_icons28, dbIdx, dmx - 12, dmy + 12); /* centered on cursor */
        } __except(1) {}
    }

    /* Draw tooltip LAST (on top of everything) */
    if (g_tooltipSkillId >= 0) {
        DrawSkillTooltip(g_tooltipSkillId, g_tooltipMX, g_tooltipMY);
        g_tooltipSkillId = -1;
    }

    return; /* Rest of old editor code below - disabled */
    if (0) /* unreachable - keeps old code compilable */
    if (!fnRect || !fnText || !fnFont) return;
    if (!g_poolInitialized) {
        DWORD edSeed = GetTickCount();
        InitSkillPool(edSeed);
        AssignAllRewards(edSeed);
    }

    int mx = MouseX();
    int my = MouseY();
    BOOL clicked = MouseClicked();

    /* If confirmation dialog is open, render it on top and block other clicks */
    if (g_confirmOpen) {
        RenderConfirmDialog(mx, my, clicked);
        return;
    }

    /* === LAYOUT === Available panel: left side (x=6, w=170) Slots panel: center (x=182, w=260) Info panel: right (x=448, w=140) */
    /* Scroll via mouse wheel — poll g_mouseWheel which WndProc sets */
    /* Also support scroll buttons: up/down arrows at top/bottom of scrollbar */
    /* g_editorScroll is clamped in the render sections below */

    int AX = 4, AY = 48, AW = 140, AH = 520;   /* Available (narrower, below menu) */
    int SX = 150, SY = 48, SW = 310, SH = 350;   /* Slots (starts after available) */
    int IX = 466, IY = 68, IW = 326, IH = 140;    /* Info (moved down, fits within 800px: 466+326=792) */

    /* SLOTS PANEL (center) */
    /* Background: multi-pass fnRect + silver borders */
    for (int i = 0; i < 6; i++)
        fnRect(SX, SY, SX + SW, SY + SH, 0, 5);
    fnRect(SX, SY, SX + SW, SY + 1, 7, 5);           /* top */
    fnRect(SX, SY, SX + 1, SY + SH, 7, 5);           /* left */
    fnRect(SX + SW - 1, SY, SX + SW, SY + SH, 7, 5); /* right */
    fnRect(SX, SY + SH - 1, SX + SW, SY + SH, 7, 5); /* bottom */

    /* Header highlight */
    fnRect(SX + 1, SY + 1, SX + SW - 1, SY + 22, 7, 5);
    fnRect(SX + 1, SY + 1, SX + SW - 1, SY + 22, 7, 5);
    fnFont(1);
    fnText(L"Skill Editor", SX + SW/2, SY + 16, 9, 1);

    /* Close button — inside the header, left-aligned text */
    int closeX = SX + SW - 20;
    if (InRect(mx, my, closeX, SY + 2, 18, 18)) {
        fnFont(0);
        fnText(L"X", closeX + 4, SY + 15, 0, 0);
    } else {
        fnFont(0);
        fnText(L"X", closeX + 4, SY + 15, 1, 0);
    }
    if (clicked && InRect(mx, my, closeX, SY + 2, 20, 20)) {
        g_editorOpen = FALSE;
        g_apPageFocus = -1;
        return;
    }

    /* Tab bar */
    int tabY = SY + 24;
    int tabW = SW / 4;
    const char* tabNames[] = { "Tab 1", "Tab 2", "Tab 3", "Lock" };

    for (int t = 0; t < 4; t++) {
        int tx = SX + t * tabW;
        if (g_editorTab == t) {
            fnRect(tx + 1, tabY + 1, tx + tabW - 1, tabY + 20, 7, 5);
            fnRect(tx + 1, tabY + 1, tx + tabW - 1, tabY + 20, 7, 5);
        }
        fnFont(0);
        {
            wchar_t wbuf[16];
            MultiByteToWideChar(CP_ACP, 0, tabNames[t], -1, wbuf, 16);
            int hover = InRect(mx, my, tx, tabY, tabW, 20);
            fnText(wbuf, tx + 8, tabY + 15, g_editorTab == t ? 2 : (hover ? 0 : 7), 0);
        }
        if (clicked && InRect(mx, my, tx, tabY, tabW, 20)) {
            g_editorTab = t;
            g_editorScroll = 0;
            g_availScroll = 0;
        }
    }
    /* Separator after tabs */
    fnRect(SX, tabY + 20, SX + SW, tabY + 21, 7, 5);

    int contentY = tabY + 24;

    /* Reset Points — own box at bottom of slots panel, INSIDE the border */
    {
        int rpBx = SX + 4;
        int rpBy = SY + SH - 24;
        int rpBw = SW - 8;
        int rpBh = 20;
        /* Dark background + silver borders */
        for (int i = 0; i < 3; i++)
            fnRect(rpBx, rpBy, rpBx + rpBw, rpBy + rpBh, 0, 5);
        fnRect(rpBx, rpBy, rpBx + rpBw, rpBy + 1, 7, 5);
        fnRect(rpBx, rpBy + rpBh - 1, rpBx + rpBw, rpBy + rpBh, 7, 5);
        fnFont(0);
        wchar_t wbuf[64];
        /* Reset points + tier requirements in one clean box */
        /* 1.9.13 audit fix (2026-06-08): guard both the function pointer AND the unit it returns — fnGetPlayer can be unresolved, and returns NULL whenever no character is in-game (menus, loading, exit), which fnGetStat does not itself null-check. */
        void* lvlUnit = Player();
        int playerLvl = (fnGetStat && lvlUnit) ? fnGetStat(lvlUnit, 12, 0) : 1;
        swprintf(wbuf, 64, L"Resets: %d", g_resetPoints);
        fnText(wbuf, rpBx + 8, rpBy + 13, g_resetPoints > 0 ? 9 : 7, 0);
        /* Tier requirements — right side of same line */
        fnText(L"T1:1", rpBx + rpBw - 130, rpBy + 13, playerLvl >= 1 ? 2 : 1, 0);
        fnText(L"T2:20", rpBx + rpBw - 85, rpBy + 13, playerLvl >= 20 ? 2 : 1, 0);
        fnText(L"T3:40", rpBx + rpBw - 40, rpBy + 13, playerLvl >= 40 ? 2 : 1, 0);
    }

    if (g_editorTab < 3) {
        int tab = g_editorTab;
        fnFont(0);

        /* Draw 10 slots */
        for (int s = 0; s < 10; s++) {
            int sy = contentY + s * 26;
            /* Slot background: single dark pass */
            fnRect(SX + 4, sy, SX + SW - 4, sy + 24, 0, 5);

            wchar_t wlabel[16];
            int cls_ft = GetPlayerClass(); if (cls_ft < 0) cls_ft = 0;
            int tier = SlotTier(cls_ft, tab, s);
            swprintf(wlabel, 16, L"%d [T%d]", s + 1, tier);
            fnText(wlabel, SX + 8, sy + 15, (tier==1)?2:(tier==2)?9:1, 0);

            int poolIdx = g_tabSlots[tab][s];
            if (poolIdx >= 0) {
                const SkillEntry* sk = &g_skillDB[g_pool[poolIdx].dbIndex];
                {
                    wchar_t wname[128];
                    MultiByteToWideChar(CP_ACP, 0, sk->name, -1, wname, 128);
                    fnText(wname, SX + 60, sy + 15, 0, 0);
                }
                {
                    char cls[8]; sprintf(cls, "(%s)", sk->classCode);
                    wchar_t wcls[16];
                    MultiByteToWideChar(CP_ACP, 0, cls, -1, wcls, 16);
                    fnText(wcls, SX + 190, sy + 15, 7, 0);
                }

                /* [X] remove button -> opens confirm dialog */
                int rx = SX + SW - 24;
                {
                    wchar_t wx[] = L"X";
                    BOOL rxHover = InRect(mx, my, rx, sy + 2, 20, 20);
                    fnText(wx, rx + 10, sy + 15, rxHover ? 0 : 1, 1);
                }
                if (clicked && InRect(mx, my, rx, sy + 2, 20, 20)) {
                    g_confirmOpen = TRUE;
                    g_confirmAction = 1;
                    g_confirmTab = tab;
                    g_confirmSlot = s;
                    strncpy(g_confirmName, sk->name, 63);
                }

                if (InRect(mx, my, SX + 60, sy, 130, 24))
                    g_infoIdx = g_pool[poolIdx].dbIndex;
            } else {
                fnText(L"< Empty >", SX + 60, sy + 15, 7, 0);
            }
        }

    } else {
        /* Locked tab -- item-based scroll (exact copy of old code pattern) */
        fnFont(0);
        BOOL wasLDown2 = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        int lockedCount = 0;
        int poolLimit = (g_apMode && g_apSkillPoolSize < g_poolCount) ? g_apSkillPoolSize : g_poolCount;
        for (int i = 0; i < poolLimit; i++) {
            if (!g_pool[i].unlocked) lockedCount++;
        }

        {
            wchar_t wbuf[64];
            swprintf(wbuf, 64, L"Locked Skills: %d", lockedCount);
            fnText(wbuf, SX + 8, contentY + 12, 7, 0);
        }
        int lCY = contentY + 20;
        int lBottom = SY + SH - 28; /* Stop ABOVE reset points box */
        int lROW = 16;
        int lStartY = lCY;

        int lShown = 0;
        for (int tier = 1; tier <= 3 && lCY < lBottom; tier++) {
            for (int i = 0; i < g_poolCount && lCY < lBottom; i++) {
                if (g_pool[i].unlocked) continue;
                if (g_skillDB[g_pool[i].dbIndex].tier != tier) continue;

                /* In AP mode, only show skills that are in the AP pool (first g_apSkillPoolSize entries) */
                if (g_apMode && i >= g_apSkillPoolSize) continue;

                if (lShown < g_editorScroll) { lShown++; continue; }

                const SkillEntry* sk = &g_skillDB[g_pool[i].dbIndex];
                char entry[64];
                sprintf(entry, "[T%d] %s (%s)", sk->tier, sk->name, sk->classCode);
                int lockCol = (tier == 1) ? 2 : (tier == 2) ? 9 : 1;
                BOOL hover = InRect(mx, my, SX + 4, lCY - 2, SW - 16, lROW);
                {
                    wchar_t wbuf[128];
                    MultiByteToWideChar(CP_ACP, 0, entry, -1, wbuf, 128);
                    fnText(wbuf, SX + 8, lCY + 10, hover ? 0 : lockCol, 0);
                }
                if (hover) g_infoIdx = g_pool[i].dbIndex;
                lCY += lROW; lShown++;
            }
        }
        if (lockedCount == 0) fnText(L"All skills unlocked!", SX + 8, contentY + 20, 2, 0);

        /* Scrollbar */
        {
            int visibleRows = (lBottom - lStartY) / lROW;
            if (visibleRows < 1) visibleRows = 1;
            int maxScr = lockedCount - visibleRows;
            if (maxScr < 0) maxScr = 0;
            if (g_editorScroll > maxScr) g_editorScroll = maxScr;

            if (maxScr > 0) {
                int sbX = SX + SW - 10;
                int sbTop = lStartY;
                int sbH = lBottom - sbTop;
                fnRect(sbX, sbTop, sbX + 8, sbTop + sbH, 0, 5);
                int thumbH = (sbH * visibleRows) / lockedCount;
                if (thumbH < 16) thumbH = 16;
                int thumbY = sbTop + (g_editorScroll * (sbH - thumbH)) / maxScr;
                fnRect(sbX, thumbY, sbX + 8, thumbY + thumbH, 7, 5);

                if (InRect(mx, my, sbX - 4, sbTop, 16, sbH) && wasLDown2) {
                    int relY = my - sbTop - thumbH / 2;
                    g_editorScroll = (relY * maxScr) / (sbH - thumbH);
                    if (g_editorScroll < 0) g_editorScroll = 0;
                    if (g_editorScroll > maxScr) g_editorScroll = maxScr;
                }
            }
        }
    }

    /* AVAILABLE SKILLS PANEL (left side) */
    /* Background: multi-pass fnRect + silver borders */
    for (int i = 0; i < 6; i++)
        fnRect(AX, AY, AX + AW, AY + AH, 0, 5);
    fnRect(AX, AY, AX + AW, AY + 1, 7, 5);           /* top */
    fnRect(AX, AY, AX + 1, AY + AH, 7, 5);           /* left */
    fnRect(AX + AW - 1, AY, AX + AW, AY + AH, 7, 5); /* right */
    fnRect(AX, AY + AH - 1, AX + AW, AY + AH, 7, 5); /* bottom */

    /* Header highlight */
    fnRect(AX + 1, AY + 1, AX + AW - 1, AY + 20, 7, 5);
    fnRect(AX + 1, AY + 1, AX + AW - 1, AY + 20, 7, 5);
    fnFont(0);
    fnText(L"Available", AX + 8, AY + 14, 9, 0);

    {
        int avCY = AY + 24;
        int avBottom = AY + AH - 4;
        int ROW = 16;
        int tab = (g_editorTab < 3) ? g_editorTab : 0;
        BOOL wasLDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        int availStartY = avCY;
        fnFont(0);

        /* Count total available (unassigned + unlocked) */
        int totalAvail = 0;
        for (int i = 0; i < g_poolCount; i++) {
            if (g_pool[i].unlocked && !g_pool[i].assigned) totalAvail++;
        }

        /* Render available skills sorted by tier, with item-based scroll */
        int shown = 0;
        for (int tier = 1; tier <= 3; tier++) {
            for (int i = 0; i < g_poolCount && avCY < avBottom; i++) {
                if (!g_pool[i].unlocked) continue;
                if (g_pool[i].assigned) continue;
                if (g_skillDB[g_pool[i].dbIndex].tier != tier) continue;

                if (shown < g_availScroll) { shown++; continue; }

                const SkillEntry* sk = &g_skillDB[g_pool[i].dbIndex];
                char entry[48];
                sprintf(entry, "[T%d] %s", sk->tier, sk->name);
                int skillCol = (tier == 1) ? 2 : (tier == 2) ? 9 : 1;
                BOOL hover = InRect(mx, my, AX + 2, avCY - 2, AW - 14, ROW);

                if (hover) {
                    /* Highlight: silver rect on hover */
                    fnRect(AX + 2, avCY - 2, AX + AW - 12, avCY - 2 + ROW, 7, 5);
                    g_infoIdx = g_pool[i].dbIndex;

                    if (clicked && g_editorTab < 3) {
                        int es = -1;
                        /* Find first empty slot matching the skill's tier */
                        int cls_ft2 = GetPlayerClass(); if (cls_ft2 < 0) cls_ft2 = 0;
                        for (int s = 0; s < 10; s++) {
                            if (g_tabSlots[tab][s] == -1) {
                                int reqTier = SlotTier(cls_ft2, tab, s);
                                if (reqTier == 0 || sk->tier == reqTier) {
                                    es = s; break;
                                }
                            }
                        }
                        if (es >= 0) {
                            /* Level gate: T2 requires level 20, T3 requires level 40 */
                            /* 1.9.13 audit fix (2026-06-08): guard both the function pointer AND the unit it returns — fnGetPlayer can be unresolved, and returns NULL whenever no character is in-game (menus, loading, exit), which fnGetStat does not itself null-check. */
                            void* lvlUnit = Player();
                            int playerLvl = (fnGetStat && lvlUnit) ? fnGetStat(lvlUnit, 12, 0) : 1;
                            if (sk->tier == 2 && playerLvl < 20) {
                                ShowNotify("Requires Level 20 for Tier 2!");
                            } else if (sk->tier == 3 && playerLvl < 40) {
                                ShowNotify("Requires Level 40 for Tier 3!");
                            } else {
                                AssignSkill(i, tab, es);
                            }
                        } else {
                            /* No matching tier slot available */
                            char noSlotMsg[64];
                            sprintf(noSlotMsg, "No Tier %d slot available!", sk->tier);
                            ShowNotify(noSlotMsg);
                        }
                    }
                }

                {
                    wchar_t wbuf[128];
                    MultiByteToWideChar(CP_ACP, 0, entry, -1, wbuf, 128);
                    fnText(wbuf, AX + 4, avCY + 10, hover ? 0 : skillCol, 0);
                }
                avCY += ROW; shown++;
            }
        }

        /* Scrollbar (exact copy of old code pattern) */
        {
            int visibleRows = (avBottom - availStartY) / ROW;
            if (visibleRows < 1) visibleRows = 1;
            int maxScr = totalAvail - visibleRows;
            if (maxScr < 0) maxScr = 0;
            if (g_availScroll > maxScr) g_availScroll = maxScr;

            if (maxScr > 0) {
                int sbX = AX + AW - 10;
                int sbTop = availStartY;
                int sbH = avBottom - sbTop;
                fnRect(sbX, sbTop, sbX + 8, sbTop + sbH, 0, 5);
                int thumbH = (sbH * visibleRows) / totalAvail;
                if (thumbH < 16) thumbH = 16;
                int thumbY = sbTop + (g_availScroll * (sbH - thumbH)) / maxScr;
                fnRect(sbX, thumbY, sbX + 8, thumbY + thumbH, 7, 5);

                if (InRect(mx, my, sbX - 4, sbTop, 16, sbH) && wasLDown) {
                    int relY = my - sbTop - thumbH / 2;
                    g_availScroll = (relY * maxScr) / (sbH - thumbH);
                    if (g_availScroll < 0) g_availScroll = 0;
                    if (g_availScroll > maxScr) g_availScroll = maxScr;
                }
            }
        }
    }

    /* INFO PANEL (right side, wider) */
    if (g_infoIdx >= 0 && g_infoIdx < (int)SKILL_DB_COUNT) {
        int ipW = IW, ipH = IH;
        int ipX = IX, ipY = IY;

        /* Background: multi-pass fnRect + silver borders */
        for (int i = 0; i < 6; i++)
            fnRect(ipX, ipY, ipX + ipW, ipY + ipH, 0, 5);
        fnRect(ipX, ipY, ipX + ipW, ipY + 1, 7, 5);             /* top */
        fnRect(ipX, ipY, ipX + 1, ipY + ipH, 7, 5);             /* left */
        fnRect(ipX + ipW - 1, ipY, ipX + ipW, ipY + ipH, 7, 5); /* right */
        fnRect(ipX, ipY + ipH - 1, ipX + ipW, ipY + ipH, 7, 5); /* bottom */

        /* Header highlight */
        fnRect(ipX + 1, ipY + 1, ipX + ipW - 1, ipY + 22, 7, 5);
        fnRect(ipX + 1, ipY + 1, ipX + ipW - 1, ipY + 22, 7, 5);

        /* Skill name header */
        fnFont(1);
        {
            wchar_t wbuf[128];
            MultiByteToWideChar(CP_ACP, 0, g_skillDB[g_infoIdx].name, -1, wbuf, 128);
            fnText(wbuf, ipX + ipW/2, ipY + 16, 9, 1);
        }

        fnFont(0);
        int ly = ipY + 28;

        /* Class (full name) */
        {
            char buf[64];
            wchar_t wbuf[128];
            sprintf(buf, "Class: %s", FullClassName(g_skillDB[g_infoIdx].classCode));
            MultiByteToWideChar(CP_ACP, 0, buf, -1, wbuf, 128);
            fnText(wbuf, ipX + 6, ly, 0, 0);
            ly += 14;
        }

        /* Tier (in tier color) */
        {
            int tier = g_skillDB[g_infoIdx].tier;
            int tc = (tier == 1) ? 2 : (tier == 2) ? 9 : 1;
            wchar_t wbuf[32];
            swprintf(wbuf, 32, L"Tier: %d", tier);
            fnText(wbuf, ipX + 6, ly, tc, 0);
            ly += 14;
        }

        /* Extra info from Skills.txt */
        {
            int sid = g_skillDB[g_infoIdx].id;
            if (sid >= 0 && sid < 400 && g_skillExtraLoaded) {
                SkillExtraInfo* ex = &g_skillExtra[sid];

                if (ex->reqlevel > 0) {
                    wchar_t wbuf[64];
                    swprintf(wbuf, 64, L"Required Level: %d", ex->reqlevel);
                    fnText(wbuf, ipX + 6, ly, 0, 0);
                    ly += 14;
                }

                if (ex->manaCost > 0) {
                    wchar_t wbuf[64];
                    swprintf(wbuf, 64, L"Mana Cost: %d", ex->manaCost);
                    fnText(wbuf, ipX + 6, ly, 7, 0);
                    ly += 14;
                }

                if (ex->elemType[0]) {
                    /* Element type with color */
                    int ec = 0; /* default white */
                    if (strcmp(ex->elemType, "fire") == 0) ec = 1;
                    else if (strcmp(ex->elemType, "ltng") == 0) ec = 9;
                    else if (strcmp(ex->elemType, "cold") == 0) ec = 7;
                    else if (strcmp(ex->elemType, "pois") == 0) ec = 2;
                    else if (strcmp(ex->elemType, "mag") == 0) ec = 7;

                    {
                        char buf[32];
                        wchar_t wbuf[64];
                        sprintf(buf, "Element: %s", ex->elemType);
                        MultiByteToWideChar(CP_ACP, 0, buf, -1, wbuf, 64);
                        fnText(wbuf, ipX + 6, ly, ec, 0);
                        ly += 14;
                    }

                    if (ex->eMin > 0 || ex->eMax > 0) {
                        wchar_t wbuf[64];
                        swprintf(wbuf, 64, L"Damage: %d-%d", ex->eMin, ex->eMax);
                        fnText(wbuf, ipX + 6, ly, ec, 0);
                        ly += 14;
                    }
                }
            }
        }
    }
}
