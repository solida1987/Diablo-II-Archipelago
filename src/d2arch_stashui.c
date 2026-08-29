/* Stash UI (Phase 2: tab bar + click handling) Minimal overlay on top of the storage + logic layers. */

/* CONSTANTS */

/* Hotkey: F8 toggles the overlay. */
#define STASHUI_HOTKEY_VK       0x77

/* Overall panel size + placement. */
#define STASHUI_PANEL_W         520
#define STASHUI_PANEL_H         420

/* Tab bar row height; two rows (AP + shared) fit on top. */
#define STASHUI_TAB_W           48
#define STASHUI_TAB_H           22
#define STASHUI_TAB_GAP         2

/* Slot grid dimensions (6 cols x 8 rows, per STASH_GRID_COLS/ROWS). */
#define STASHUI_SLOT_W          30
#define STASHUI_SLOT_H          30
#define STASHUI_SLOT_GAP        2

/* LOCAL STATE */

/* Anchor of the stash panel (top-left corner in logical coords). */
static int s_stashPanelX = 0;
static int s_stashPanelY = 0;

/* Cached tab-bar hit rectangles. */
typedef struct {
    int x, y, w, h;
    int globalIndex;   /* 0..STASH_MAX_TABS-1, or -1 when unused */
} StashUITabRect;

#define STASHUI_MAX_TAB_RECTS   STASH_MAX_TABS
static StashUITabRect s_tabRects[STASHUI_MAX_TAB_RECTS];
static int            s_tabRectCount = 0;

/* Edge-trigger state for the F8 hotkey (GetAsyncKeyState polling fallback, in case the WndProc dispatcher doesn't fire — belt and suspenders). */
static BOOL s_stashHotkeyWasDown = FALSE;

/* INTERNAL HELPERS */

/* Recompute panel anchor based on current screen size. */
static void StashUIRecomputeAnchor(void) {
    s_stashPanelX = (g_screenW - STASHUI_PANEL_W) / 2;
    s_stashPanelY = (g_screenH - STASHUI_PANEL_H) / 2;
    if (s_stashPanelX < 0) s_stashPanelX = 0;
    if (s_stashPanelY < 0) s_stashPanelY = 0;
}

/* Point-in-rect test matching d2arch_helpers.c::InRect (same module is visible here, but we rewrite it locally to avoid the guarantee that InRect was included before this file). */
static BOOL StashUIPointInRect(int px, int py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px < rx + rw && py >= ry && py < ry + rh) ? TRUE : FALSE;
}

/* Draw a 1-pixel rectangle outline using four thin fnRect strokes. */
static void StashUIDrawOutline(int x, int y, int w, int h, int color) {
    if (!fnRect) return;
    fnRect(x,         y,         x + w,     y + 1,     color, 5);
    fnRect(x,         y + h - 1, x + w,     y + h,     color, 5);
    fnRect(x,         y,         x + 1,     y + h,     color, 5);
    fnRect(x + w - 1, y,         x + w,     y + h,     color, 5);
}

/* Fill a rectangle (filled box, transNum 5 == solid in D2Gfx). */
static void StashUIFillRect(int x, int y, int w, int h, int color, int trans) {
    if (!fnRect) return;
    fnRect(x, y, x + w, y + h, color, trans);
}

/* Choose the visual colors for a tab based on its access + active state. */
static void StashUIPickTabColors(int globalIndex,
                                 BOOL isActive,
                                 int* outFillColor,
                                 int* outFillTrans,
                                 int* outOutlineColor,
                                 int* outTextColor)
{
    StashAccess acc = StashGetAccess(globalIndex);

    /* Defaults (LOCKED/grey). */
    int fill    = 5;   /* grey */
    int trans   = 3;   /* semi-transparent background */
    int outline = 5;   /* grey border */
    int text    = 5;

    switch (acc) {
    case STASH_ACCESS_READWRITE:
        fill    = isActive ? 4 : 0;   /* gold when active, white idle */
        trans   = isActive ? 5 : 3;
        outline = 4;                  /* gold border */
        text    = isActive ? 0 : 4;
        break;
    case STASH_ACCESS_READONLY:
        fill    = 7;                  /* orange */
        trans   = 3;
        outline = 7;
        text    = 7;
        break;
    case STASH_ACCESS_LOCKED:
        fill    = 5;                  /* grey */
        trans   = 3;
        outline = 5;
        text    = 5;
        break;
    case STASH_ACCESS_NONE:
    default:
        /* caller should not draw — defaults fall through */
        break;
    }

    if (outFillColor)    *outFillColor    = fill;
    if (outFillTrans)    *outFillTrans    = trans;
    if (outOutlineColor) *outOutlineColor = outline;
    if (outTextColor)    *outTextColor    = text;
}

/* Compute tab positions + fill s_tabRects[]. */
static void StashUIComputeTabLayout(void) {
    int rowY;
    int perRow = STASH_NUM_AP_TABS;   /* 10 tabs wide is the design */
    int rowW   = perRow * STASHUI_TAB_W + (perRow - 1) * STASHUI_TAB_GAP;
    /* Position tab strip at top-center of screen (not centered on an overlay panel). */
    int baseX  = (g_screenW - rowW) / 2;
    int topY   = 40;
    if (baseX < 0) baseX = 0;

    s_tabRectCount = 0;

    /* Row 0: AP tabs (global index 0..9). */
    rowY = topY;
    {
        int col = 0;
        int i;
        for (i = 0; i < STASH_NUM_AP_TABS; i++) {
            if (!StashIsTabVisible(i)) continue;
            if (s_tabRectCount >= STASHUI_MAX_TAB_RECTS) break;
            s_tabRects[s_tabRectCount].x = baseX + col * (STASHUI_TAB_W + STASHUI_TAB_GAP);
            s_tabRects[s_tabRectCount].y = rowY;
            s_tabRects[s_tabRectCount].w = STASHUI_TAB_W;
            s_tabRects[s_tabRectCount].h = STASHUI_TAB_H;
            s_tabRects[s_tabRectCount].globalIndex = i;
            s_tabRectCount++;
            col++;
        }
    }

    /* Row 1: shared tabs (global index 10..19). */
    rowY = topY + STASHUI_TAB_H + STASHUI_TAB_GAP;
    {
        int col = 0;
        int i;
        for (i = STASH_NUM_AP_TABS; i < STASH_MAX_TABS; i++) {
            if (!StashIsTabVisible(i)) continue;
            if (s_tabRectCount >= STASHUI_MAX_TAB_RECTS) break;
            s_tabRects[s_tabRectCount].x = baseX + col * (STASHUI_TAB_W + STASHUI_TAB_GAP);
            s_tabRects[s_tabRectCount].y = rowY;
            s_tabRects[s_tabRectCount].w = STASHUI_TAB_W;
            s_tabRects[s_tabRectCount].h = STASHUI_TAB_H;
            s_tabRects[s_tabRectCount].globalIndex = i;
            s_tabRectCount++;
            col++;
        }
    }
}

/* Paint a single tab button at the cached location. */
static void StashUIDrawTab(const StashUITabRect* rc) {
    int fill, trans, outline, text;
    BOOL isActive = (rc->globalIndex == g_activeStashTab) ? TRUE : FALSE;
    StashAccess acc = StashGetAccess(rc->globalIndex);
    wchar_t label[32];

    StashUIPickTabColors(rc->globalIndex, isActive, &fill, &trans, &outline, &text);

    StashUIFillRect(rc->x, rc->y, rc->w, rc->h, fill, trans);
    StashUIDrawOutline(rc->x, rc->y, rc->w, rc->h, outline);

    if (fnText && fnFont) {
        fnFont(6);
        /* AP tabs labeled AP1-AP10, shared tabs S1-S10. */
        if (rc->globalIndex < STASH_NUM_AP_TABS) {
            _snwprintf(label, 31, acc == STASH_ACCESS_LOCKED ? L"AP%d X" : L"AP%d",
                       rc->globalIndex + 1);
        } else {
            _snwprintf(label, 31, acc == STASH_ACCESS_LOCKED ? L"S%d X" : L"S%d",
                       rc->globalIndex - STASH_NUM_AP_TABS + 1);
        }
        label[31] = 0;
        fnText(label, rc->x + rc->w / 2, rc->y + rc->h - 6, text, 1);
    }
}

/* Draw the 6x8 slot grid for the currently active tab. */
static void StashUIDrawSlots(void) {
    StashTab* tab = NULL;
    int gridTopY;
    int gridLeftX;
    int col, row;
    int gridW, gridH;

    if (g_activeStashTab < 0 || g_activeStashTab >= STASH_MAX_TABS) return;
    if (!StashIsTabVisible(g_activeStashTab)) return;

    if (g_activeStashTab < STASH_NUM_AP_TABS) {
        tab = &g_stashAP[g_activeStashTab];
    } else {
        tab = &g_stashShared[g_activeStashTab - STASH_NUM_AP_TABS];
    }

    gridW = STASH_GRID_COLS * STASHUI_SLOT_W + (STASH_GRID_COLS - 1) * STASHUI_SLOT_GAP;
    gridH = STASH_GRID_ROWS * STASHUI_SLOT_H + (STASH_GRID_ROWS - 1) * STASHUI_SLOT_GAP;
    gridLeftX = s_stashPanelX + (STASHUI_PANEL_W - gridW) / 2;
    gridTopY  = s_stashPanelY + 6 + 2 * (STASHUI_TAB_H + STASHUI_TAB_GAP) + 18;

    for (row = 0; row < STASH_GRID_ROWS; row++) {
        for (col = 0; col < STASH_GRID_COLS; col++) {
            int sx = gridLeftX + col * (STASHUI_SLOT_W + STASHUI_SLOT_GAP);
            int sy = gridTopY  + row * (STASHUI_SLOT_H + STASHUI_SLOT_GAP);
            int slotIdx = row * STASH_GRID_COLS + col;
            StashSlot* sl = &tab->slots[slotIdx];

            StashUIFillRect(sx, sy, STASHUI_SLOT_W, STASHUI_SLOT_H, 0, 3);
            StashUIDrawOutline(sx, sy, STASHUI_SLOT_W, STASHUI_SLOT_H, 5);

            if (sl->occupied == 2 && fnText && fnFont) {
                /* Virtual stack — render its count. */
                wchar_t buf[16];
                _snwprintf(buf, 15, L"%d", sl->stack.count);
                buf[15] = 0;
                fnFont(6);
                fnText(buf, sx + STASHUI_SLOT_W / 2, sy + STASHUI_SLOT_H - 8, 2, 1);
            } else if (sl->occupied == 1 && fnText && fnFont) {
                /* Phase 5 — real item rendering TODO. */
                fnFont(6);
                fnText(L"I", sx + STASHUI_SLOT_W / 2, sy + STASHUI_SLOT_H - 8, 4, 1);
            }
        }
    }
}

/* Draw the panel chrome: outer frame + title + close hint. */
static void StashUIDrawFrame(void) {
    if (!fnRect) return;
    /* Semi-transparent black fill (color 1 == dark, but any works with trans=3 here — mirrors menu boxes elsewhere). */
    StashUIFillRect(s_stashPanelX, s_stashPanelY,
                    STASHUI_PANEL_W, STASHUI_PANEL_H, 0, 3);
    /* Gold outline to match the vanilla stash visual tone. */
    StashUIDrawOutline(s_stashPanelX, s_stashPanelY,
                       STASHUI_PANEL_W, STASHUI_PANEL_H, 4);

    if (fnText && fnFont) {
        fnFont(6);
        fnText(L"Archipelago Multi-Tab Stash",
               s_stashPanelX + STASHUI_PANEL_W / 2,
               s_stashPanelY + STASHUI_PANEL_H - 8,
               4, 1);
    }
}

/* Diagnostic: scan UIVars 0..31 for changes, log when any flips. */
static DWORD s_lastUIVar[32] = {0};
static BOOL  s_uiVarInitialized = FALSE;

/* D2Client_GetUIState is __stdcall (arg on stack). */
typedef DWORD (__stdcall *GetUIState_t)(DWORD);
static GetUIState_t s_pGetUIState = NULL;

static void StashUIResolveGetUIState(void) {
    if (s_pGetUIState) return;
    HMODULE h = GetModuleHandleA("D2Client.dll");
    if (!h) return;
    s_pGetUIState = (GetUIState_t)GetProcAddress(h, (LPCSTR)10006);
    if (!s_pGetUIState) {
        /* Fall back to the offset the existing code used */
        s_pGetUIState = (GetUIState_t)((BYTE*)h + 0xBE400);
    }
    Log("StashUI: GetUIState resolved to %p (via %s)\n",
        s_pGetUIState,
        GetProcAddress(h, (LPCSTR)10006) ? "ordinal 10006" : "offset 0xBE400");
}

static DWORD s_debugFrameCount = 0;

/* D2 1.10f doesn't expose GetUIState via ordinal, and the internal function at 0xBE400 has an unknown ABI (all calls return 0). */
extern void* g_lastPlayerPtr;

static void StashUIDetectNativeStash(void) {
    BOOL nowOpen = (g_lastPlayerPtr != NULL);
    if (nowOpen != g_stashOpen) {
        g_stashOpen = nowOpen;
        Log("StashUI: overlay %s (player %s)\n",
            nowOpen ? "SHOWN" : "HIDDEN",
            nowOpen ? "loaded" : "gone");
    }
    /* Early return — skip the original UIState probe code */
    return;
    StashUIResolveGetUIState();
    if (!s_pGetUIState) {
        static int warned = 0;
        if (!warned) { Log("StashUI: GetUIState not resolved\n"); warned = 1; }
        return;
    }

    /* Every 60 frames (~1 sec) dump a snapshot of ALL non-zero UIVars so we can see them when the user opens/closes the stash */
    s_debugFrameCount++;
    BOOL dumpThisFrame = (s_debugFrameCount % 60 == 0);

    DWORD cur[32] = {0};
    for (int i = 0; i < 32; i++) {
        __try { cur[i] = s_pGetUIState((DWORD)i); } __except(EXCEPTION_EXECUTE_HANDLER) { cur[i] = 0; }
    }

    if (dumpThisFrame) {
        char buf[512]; buf[0] = 0;
        int bufLen = 0;
        int anyNonZero = 0;
        for (int i = 0; i < 32; i++) {
            if (cur[i] != 0) {
                bufLen += _snprintf(buf + bufLen, sizeof(buf) - bufLen - 1,
                                    "[%d]=%u ", i, (unsigned)cur[i]);
                anyNonZero = 1;
            }
        }
        if (anyNonZero) {
            Log("StashUI: UIVar snapshot (frame %u): %s\n", (unsigned)s_debugFrameCount, buf);
        } else if (s_debugFrameCount <= 300) {
            Log("StashUI: UIVar snapshot (frame %u): ALL ZERO — s_pGetUIState=%p\n",
                (unsigned)s_debugFrameCount, s_pGetUIState);
        }
    }

    /* Log ALL UIVars once at startup so we have a baseline. */
    if (!s_uiVarInitialized) {
        Log("StashUI: initial UIVar snapshot:\n");
        for (int i = 0; i < 32; i++) {
            if (cur[i] != 0) Log("  UIVar[%d] = %u\n", i, (unsigned)cur[i]);
        }
        memcpy(s_lastUIVar, cur, sizeof(cur));
        s_uiVarInitialized = TRUE;
        return;
    }

    /* Log any UIVar that CHANGED this frame. */
    for (int i = 0; i < 32; i++) {
        if (cur[i] != s_lastUIVar[i]) {
            Log("StashUI: UIVar[%d] changed %u -> %u\n", i,
                (unsigned)s_lastUIVar[i], (unsigned)cur[i]);
            s_lastUIVar[i] = cur[i];
        }
    }

    /* Legacy code path below is unreachable (we early-return above). */
}

/* PUBLIC API */

void StashUIInit(void) {
    memset(s_tabRects, 0, sizeof(s_tabRects));
    s_tabRectCount = 0;
    s_stashHotkeyWasDown = FALSE;
    StashUIRecomputeAnchor();
    Log("StashUI: initialized (panel %dx%d anchor=%d,%d — follows UIVar[14] native stash)\n",
        STASHUI_PANEL_W, STASHUI_PANEL_H,
        s_stashPanelX, s_stashPanelY);
}

/* Phase 1 tab buttons. Layout: 20 small square buttons in a vertical column on the LEFT side of D2's native stash panel. The native stash grid (Ultimate Inventory mod, 10x10) renders at approximately screen-x 16..302 and y 82..371. Our button column is placed just to the left of that, at x=0..18, y=100..500 (50 px gap between rows). Rows 0..9 = AP (private) tabs, rows 10..19 = Shared tabs. Visibility: buttons only render when player is in a TOWN area (the only places with a physical stash chest). This sidesteps the unreliable UIVar/stash-open detection while matching the real usage pattern (players only open stash in town). Interaction: click a button -> g_activeStashTab updates to that global index. Locked/hidden access modes grey out the button and swallow clicks without switching. Phase 1 does NOT yet swap stash grid contents when the tab changes; that's Phase 2 (hook into D2 inventory grid render and replace pGrids[STASH] with our shadow). */

/* Compact button column on left edge. */
#define TAB_COL_X          145
#define TAB_BTN_W          32
#define TAB_BTN_H          22
#define TAB_BTN_GAP         1
#define TAB_ROW_Y_AP_0     55
#define TAB_ROW_Y_SH_0    (TAB_ROW_Y_AP_0 + STASH_NUM_AP_TABS * (TAB_BTN_H + TAB_BTN_GAP) + 8)
/* second column (STK tabs). */
#define TAB_COL2_X         (TAB_COL_X + TAB_BTN_W + 4)

/* Hardcoded town area IDs (5 acts). */
static BOOL StashUI_InTown(void) {
    extern int GetCurrentArea(void);
    int a = GetCurrentArea();
    return (a == 1 || a == 40 || a == 75 || a == 103 || a == 109) ? TRUE : FALSE;
}

/* D2Client 1.10f — live UI-panel state array. */
#define D2_RVA_GPUISTATE   0x11A6A8
#define D2_UI_STASH        0x19

static BOOL StashUI_IsStashOpen(void) {
    HMODULE h = GetModuleHandleA("D2Client.dll");
    if (!h) return FALSE;

    DWORD* pUIState = (DWORD*)((BYTE*)h + D2_RVA_GPUISTATE);
    DWORD  stashVal = 0;
    __try {
        stashVal = pUIState[D2_UI_STASH];
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }

    /* Log first call + any transition, to confirm this offset actually flips when the user opens/closes the stash. */
    static DWORD s_prev = 0xFFFFFFFF;
    if (stashVal != s_prev) {
        Log("StashUI: gpUIState[UI_STASH]=%u (addr %p)\n",
            (unsigned)stashVal, &pUIState[D2_UI_STASH]);
        s_prev = stashVal;
    }

    return stashVal != 0 ? TRUE : FALSE;
}

static void StashUI_BtnRect(int globalIdx, int* x, int* y, int* w, int* h) {
    int baseY;
    int local;
    int colX = TAB_COL_X;

    if (globalIdx < STASH_NUM_AP_TABS) {
        /* Column 1, AP rows (0..9) */
        baseY = TAB_ROW_Y_AP_0;
        local = globalIdx;
    } else if (globalIdx < STASH_NUM_AP_TABS + STASH_NUM_SHARED_TABS) {
        /* Column 1, SH rows (10..19) */
        baseY = TAB_ROW_Y_SH_0;
        local = globalIdx - STASH_NUM_AP_TABS;
    } else if (globalIdx < STASH_STK_AP_BASE + STASH_NUM_STK_TABS) {
        /* Column 2, STK_AP rows (20..22). */
        colX  = TAB_COL2_X;
        baseY = TAB_ROW_Y_AP_0;
        local = globalIdx - STASH_STK_AP_BASE;
    } else {
        /* Column 2, STK_SH rows (23..25). */
        colX  = TAB_COL2_X;
        baseY = TAB_ROW_Y_SH_0;
        local = globalIdx - STASH_STK_SH_BASE;
    }

    *x = colX;
    *y = baseY + local * (TAB_BTN_H + TAB_BTN_GAP);
    *w = TAB_BTN_W;
    *h = TAB_BTN_H;
}

/* forward decl so StashUIRender can call it before it's defined later in this file. */
static void StashUIRenderStkOverlay(int activeTabGlobal);

void StashUIRender(void) {
    int i;
    static int s_renderLogCounter = 0;
    s_renderLogCounter++;

    if (!fnRect || !fnText || !fnFont) {
        if (s_renderLogCounter % 120 == 0) {
            if (g_verboseInput) Log("StashUIRender: fnRect=%p fnText=%p fnFont=%p (missing function pointer)\n",
                fnRect, fnText, fnFont);
        }
        return;
    }

    BOOL isOpen = StashUI_IsStashOpen();
    static BOOL s_prevOpen = FALSE;
    static int  s_openFrames = 0;
    static BOOL s_pendingCloseSwap = FALSE; /* close-swap deferred: cursor held an item */
    if (s_renderLogCounter % 120 == 0) {
        if (g_verboseInput) Log("StashUIRender: called, isStashOpen=%d screenW=%d\n", (int)isOpen, g_screenW);
    }

    /* Auto-swap back to Personal tab when stash closes. */
    if (isOpen) {
        s_openFrames++;
        /* Stash re-opened before a deferred close-swap ran: cancel it — the grid is live again and the normal close path will re-arm. */
        s_pendingCloseSwap = FALSE;
    } else {
        if ((s_prevOpen && s_openFrames >= 3) || s_pendingCloseSwap) {
            /* 2.x one-chest: the personal "P" tab is always tab STASH_NUM_AP_TABS (the Shared pool's first tab) now that the AP pool is retired — the old g_apMode?0 branch pointed at a now-hidden AP tab. */
            int personalTab = STASH_NUM_AP_TABS;
            if (g_activeStashTab != personalTab) {
                /* kill switch for auto-swap-on-close. */
                static int s_disableLoaded = 0;
                static int s_disableValue = 0;
                if (!s_disableLoaded) {
                    char ini[MAX_PATH], buf[8];
                    GetArchDir(ini, MAX_PATH);
                    strcat(ini, "d2arch.ini");
                    GetPrivateProfileStringA("Debug", "DisableStashAutoSwap",
                                             "0", buf, 7, ini);
                    s_disableValue = atoi(buf);
                    s_disableLoaded = 1;
                }
                if (s_disableValue != 0) {
                    Log("StashUI: stash closing — auto-swap SKIPPED via "
                        "[Debug] DisableStashAutoSwap=1, active tab=%d\n",
                        g_activeStashTab);
                    s_pendingCloseSwap = FALSE;
                } else {
                    /* ESC-close cursor guard (same destruction mechanism the 1.9.5 tab-click guard documents): if the player closed the stash (ESC) while HOLDING an item, swapping the grid under the cursor destroys the held item. */
                    void* pCliInv = StashSwap_GetPlayerInv();
                    if (pCliInv && StkFindCursorItem(pCliInv) != NULL) {
                        if (!s_pendingCloseSwap)
                            Log("StashUI: close auto-swap DEFERRED — cursor has an item (tab %d)\n",
                                g_activeStashTab);
                        s_pendingCloseSwap = TRUE;
                    } else {
                        Log("StashUI: stash closing (openFrames=%d) auto-swap tab %d → P (%d)%s\n",
                            s_openFrames, g_activeStashTab, personalTab,
                            s_pendingCloseSwap ? " [deferred]" : "");
                        StashSwapPage(g_activeStashTab, personalTab);
                        g_activeStashTab = personalTab;
                        s_pendingCloseSwap = FALSE;
                    }
                }
            } else {
                s_pendingCloseSwap = FALSE; /* already on P — nothing pending */
            }
        }
        s_openFrames = 0;
    }
    s_prevOpen = isOpen;

    if (!isOpen) return;

    /* Ensure the currently-active tab is actually visible for this character mode. */
    if (StashGetAccess(g_activeStashTab) == STASH_ACCESS_NONE) {
        int j;
        for (j = 0; j < STASH_MAX_TABS; j++) {
            if (StashGetAccess(j) != STASH_ACCESS_NONE &&
                StashGetAccess(j) != STASH_ACCESS_LOCKED) {
                Log("StashUI: active tab %d hidden for this char, snapping to %d\n",
                    g_activeStashTab, j);
                g_activeStashTab = j;
                break;
            }
        }
    }

    /* Verbose log first time we render when open */
    {
        static BOOL loggedFirstRender = FALSE;
        if (!loggedFirstRender) {
            int x, y, w, h;
            StashUI_BtnRect(0, &x, &y, &w, &h);
            if (g_verboseInput) Log("StashUIRender: STASH OPEN — drawing buttons starting at (%d,%d) size %dx%d, "
                "STASH_MAX_TABS=%d apMode=%d apGoalComplete=%d\n",
                x, y, w, h, STASH_MAX_TABS, (int)g_apMode, (int)g_apGoalComplete);
            /* list every tab's access + position so we can confirm STK buttons are actually being included in the draw loop. */
            for (int diag = 0; diag < STASH_MAX_TABS; diag++) {
                int dx, dy, dw, dh;
                StashAccess dacc = StashGetAccess(diag);
                StashUI_BtnRect(diag, &dx, &dy, &dw, &dh);
                Log("StashUI: tab %d access=%d rect=(%d,%d %dx%d)\n",
                    diag, (int)dacc, dx, dy, dw, dh);
            }
            loggedFirstRender = TRUE;
        }
    }

    /* Visibility matrix (see StashGetAccess in d2arch_stash.c): non-AP char: only SH1..SH10 (orange) visible; AP hidden. */
    for (i = 0; i < STASH_MAX_TABS; i++) {
        int x, y, w, h;
        StashAccess acc;
        BOOL isActive;
        int fill, trans, outline, text;
        wchar_t label[16];

        acc = StashGetAccess(i);
        if (acc == STASH_ACCESS_NONE) continue;   /* hidden — don't draw */

        StashUI_BtnRect(i, &x, &y, &w, &h);
        isActive = (i == g_activeStashTab);

        /* Color scheme: AP tabs (red 11) / SH tabs (orange 7) / STK tabs (gold 4) Active tab highlights in gold (4). */
        trans = 5;   /* opaque fill */
        text = 0;    /* white label */
        if (acc == STASH_ACCESS_LOCKED) {
            fill = 5; outline = 5; text = 5;
        } else if (isActive) {
            fill = 4; outline = 4;
        } else if (i < STASH_NUM_AP_TABS) {
            fill = 11; outline = 4;   /* AP: red */
        } else if (i < STASH_NUM_AP_TABS + STASH_NUM_SHARED_TABS) {
            fill = 7;  outline = 4;   /* SH: orange */
        } else {
            /* STK tabs (both AP and SH variants): yellow/gold */
            fill = 4;  outline = 4;   /* STK: gold */
            text = 1;                  /* dark text on gold so it reads */
        }

        /* Solid filled box */
        fnRect(x, y, x + w, y + h, fill, trans);
        /* 1px outline */
        fnRect(x,         y,         x + w,     y + 1,     outline, 5);
        fnRect(x,         y + h - 1, x + w,     y + h,     outline, 5);
        fnRect(x,         y,         x + 1,     y + h,     outline, 5);
        fnRect(x + w - 1, y,         x + w,     y + h,     outline, 5);

        /* Labels. Tab 0 (first AP) and tab STASH_NUM_AP_TABS (first SH) are labeled "P" for "personal" — their contents are saved in the character's own .d2s via pInventory, not in the cross- character shared_stash_ser.dat. The remaining AP2..AP10 and SH2..SH10 share their pool across characters (AP tabs via ap_stash_ser_<char>.dat which is per-char; SH tabs via the account-wide shared_stash_ser.dat). */
        fnFont(6);
        if (i == 0 || i == STASH_NUM_AP_TABS) {
            _snwprintf(label, 15, L"P");
        } else if (i < STASH_NUM_AP_TABS) {
            _snwprintf(label, 15, L"AP%d", i + 1);
        } else if (i < STASH_NUM_AP_TABS + STASH_NUM_SHARED_TABS) {
            /* 2.x one-chest: plain numbered tabs (no "SH" split). */
            _snwprintf(label, 15, L"%d", i - STASH_NUM_AP_TABS + 1);
        } else if (i < STASH_STK_AP_BASE + STASH_NUM_STK_TABS) {
            /* STK_AP: 3-letter category code */
            int cat = i - STASH_STK_AP_BASE;
            const wchar_t* cats[] = { L"CON", L"RUN", L"GEM" };
            _snwprintf(label, 15, L"%ls", cats[cat]);
        } else {
            /* STK_SH: same labels (different scope) */
            int cat = i - STASH_STK_SH_BASE;
            const wchar_t* cats[] = { L"CON", L"RUN", L"GEM" };
            _snwprintf(label, 15, L"%ls", cats[cat]);
        }
        label[15] = 0;
        /* Text anchored to LEFT edge of button with small padding. */
        fnText(label, x + 3, y + 13, text, 0);
    }

    /* render the STK ghost-image overlay AFTER tab buttons so it draws on top of any vanilla chest grid pixels that would otherwise show through. */
    StashUIRenderStkOverlay(g_activeStashTab);
}

/* STK ghost-image overlay When the active tab is one of the STK tabs (global index >= STASH_STK_AP_BASE) the underlying pInventory has no items in the stash slots (StashSwapPage's STK branch leaves them empty). */

/* Validate a loaded cel pointer's DC6 version magic. */
static BOOL StkCelLooksValid(void* pCel) {
    if (!pCel) return FALSE;
    DWORD version = 0;
    __try { version = *(DWORD*)pCel; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return FALSE; }
    return (version == 6) ? TRUE : FALSE;
}

/* Resolve the cel for a layout cell. */
static void* StkResolveCel(int categoryIdx, int cellIdx) {
    if (categoryIdx < 0 || categoryIdx >= STASH_NUM_STK_TABS) return NULL;
    if (cellIdx    < 0 || cellIdx    >= STASH_SLOTS_PER_TAB) return NULL;
    StkLayoutEntry* e = &g_stkLayout[categoryIdx][cellIdx];
    if (!e->invFile)                return NULL;
    if (e->pCachedCel == (void*)-1) return NULL;
    if (e->pCachedCel) {
        /* STK cels are load-once ("valid for the whole process"), but a save&quit DOES free the underlying DC6 archive, so a cached STK cel can dangle and assert in fnCelDraw (CelCmp.cpp:1485) -> hard-lock. */
        if (StkCelLooksValid(e->pCachedCel)) return e->pCachedCel;
        e->pCachedCel = NULL;   /* stale — fall through to reload */
    }
    if (!fnCelLoad)                 return NULL;

    /* Try 3 paths in order: 1. */
    char path1[160], path2[160], path3[160];
    _snprintf(path1, sizeof(path1) - 1, "data\\global\\items\\stk\\%s",  e->invFile);
    _snprintf(path2, sizeof(path2) - 1, "data\\global\\items\\%s",       e->invFile);
    _snprintf(path3, sizeof(path3) - 1, "data\\global\\items\\inv\\%s",  e->invFile);
    path1[sizeof(path1) - 1] = 0;
    path2[sizeof(path2) - 1] = 0;
    path3[sizeof(path3) - 1] = 0;

    void* pCel = NULL;
    void* a = NULL;
    __try { a = fnCelLoad(path1, 0); } __except(EXCEPTION_EXECUTE_HANDLER) { a = NULL; }
    if (StkCelLooksValid(a)) {
        pCel = a;
        Log("STK: cel OK '%s' via stk -> %p\n", e->invFile, pCel);
    } else {
        __try { a = fnCelLoad(path2, 0); } __except(EXCEPTION_EXECUTE_HANDLER) { a = NULL; }
        if (StkCelLooksValid(a)) {
            pCel = a;
            Log("STK: cel OK '%s' via root -> %p\n", e->invFile, pCel);
        } else {
            __try { a = fnCelLoad(path3, 0); } __except(EXCEPTION_EXECUTE_HANDLER) { a = NULL; }
            if (StkCelLooksValid(a)) {
                pCel = a;
                Log("STK: cel OK '%s' via inv -> %p\n", e->invFile, pCel);
            }
        }
    }

    if (!pCel) {
        e->pCachedCel = (void*)-1;
        /* No per-cell logging on miss — we expect most standard items to fail until DC6s are extracted. */
        return NULL;
    }
    e->pCachedCel = pCel;
    return pCel;
}

/* Read INI key with integer default. */
static int StkIniInt(const char* key, int defVal) {
    char ini[MAX_PATH];
    GetArchDir(ini, MAX_PATH);
    strcat(ini, "d2arch.ini");
    return GetPrivateProfileIntA("Stash", key, defVal, ini);
}

static void StashUIRenderStkOverlay(int activeTabGlobal) {
    if (activeTabGlobal < STASH_STK_AP_BASE) return;

    /* Identify scope (AP vs SH) and category (0..2). */
    BOOL useApScope    = (activeTabGlobal < STASH_STK_SH_BASE);
    int  categoryIdx   = useApScope ? (activeTabGlobal - STASH_STK_AP_BASE)
                                    : (activeTabGlobal - STASH_STK_SH_BASE);
    StkTab* dataTab    = useApScope ? &g_stashStkAp[categoryIdx]
                                    : &g_stashStkSh[categoryIdx];

    /* Grid screen coords — INI tunable so the user can shift if needed. */
    int gridX = StkIniInt("StkGridX", 232);
    int gridY = StkIniInt("StkGridY", 143);
    int cellW = StkIniInt("StkCellW", 29);
    int cellH = StkIniInt("StkCellH", 29);

    /* Diagnostic: log first time per session-tab so we can verify the overlay actually runs and confirm grid coords. */
    static int s_loggedTab[6] = {-1, -1, -1, -1, -1, -1};
    int slot = activeTabGlobal - STASH_STK_AP_BASE;
    if (slot >= 0 && slot < 6 && s_loggedTab[slot] != activeTabGlobal) {
        s_loggedTab[slot] = activeTabGlobal;
        int designatedCount = 0;
        for (int q = 0; q < STASH_SLOTS_PER_TAB; q++) {
            if (g_stkLayout[categoryIdx][q].dwCode != 0) designatedCount++;
        }
        Log("StkOverlay: tab=%d cat=%d scope=%s grid=(%d,%d %dx%d) "
            "cellsDesignated=%d\n",
            activeTabGlobal, categoryIdx, useApScope ? "AP" : "SH",
            gridX, gridY, cellW, cellH, designatedCount);
    }

    /* Iterate all 100 cells of the active layout. */
    for (int row = 0; row < STASH_GRID_ROWS; row++) {
        for (int col = 0; col < STASH_GRID_COLS; col++) {
            int cellIdx = row * STASH_GRID_COLS + col;
            int sx = gridX + col * cellW;
            int sy = gridY + row * cellH;

            StkLayoutEntry* layout = &g_stkLayout[categoryIdx][cellIdx];

            /* Black inactive cell. */
            if (layout->dwCode == 0) {
                fnRect(sx, sy, sx + cellW, sy + cellH, 0, 5);
                continue;
            }

            /* multi-cell items: only the origin cell renders. */
            if (!layout->isOriginCell) continue;

            /* visual indicator of multi-cell footprint: thin gold border around the FULL multi-cell area. */
            if (layout->cellsW > 1 || layout->cellsH > 1) {
                int fullW = layout->cellsW * cellW;
                int fullH = layout->cellsH * cellH;
                fnRect(sx,             sy,             sx + fullW, sy + 1,     4, 5);
                fnRect(sx,             sy + fullH - 1, sx + fullW, sy + fullH, 4, 5);
                fnRect(sx,             sy,             sx + 1,     sy + fullH, 4, 5);
                fnRect(sx + fullW - 1, sy,             sx + fullW, sy + fullH, 4, 5);
            }

            /* render the actual item icon as a translucent ghost. */
            void* pCel = StkResolveCel(categoryIdx, cellIdx);

            StkSlot* slot = &dataTab->slots[cellIdx];

            if (pCel && fnCelDraw) {
                /* fnCelDraw signature: (cel, x, y, gamma, drawMode, palette). */
                int iconX = sx + 1;
                int iconY = sy + (layout->cellsH * cellH) - 1;
                fnCelDraw(pCel, iconX, iconY, 0, 2, -1);
                if (slot->count > 0) {
                    fnCelDraw(pCel, iconX, iconY, 0, 5, -1);
                }
            } else {
                /* Cel didn't load — draw the 3-char code as a fallback so the cell still indicates what item belongs there. */
                if (fnText && fnFont) {
                    wchar_t lbl[8] = {0};
                    lbl[0] = (wchar_t)(layout->dwCode & 0xFF);
                    lbl[1] = (wchar_t)((layout->dwCode >> 8) & 0xFF);
                    lbl[2] = (wchar_t)((layout->dwCode >> 16) & 0xFF);
                    fnFont(6);
                    /* Dim grey so it reads as a placeholder, not a real rendered item. */
                    fnText(lbl, sx + 3, sy + cellH - 3, 5, 0);
                }
            }

            /* Count overlay (always drawn when the cell is occupied). */
            if (slot->count > 0 && fnText && fnFont) {
                wchar_t buf[16];
                _snwprintf(buf, 15, L"%u", (unsigned)slot->count);
                buf[15] = 0;
                int textColor = 0;             /* white < 100 */
                if (slot->count >= 999)        textColor = 4;  /* gold */
                else if (slot->count >= 100)   textColor = 9;  /* yellow */
                fnFont(6);
                fnText(buf, sx + cellW - 14, sy + cellH - 2, textColor, 0);
            }

            /* 1.9.0 Phase 4 — red-flash overlay when this cell was the target of a recent rejected deposit (wrong-type or stack full). */
            {
                extern int   g_stkBadDropTab;
                extern int   g_stkBadDropCell;
                extern DWORD g_stkBadDropTick;
                if (g_stkBadDropTab == categoryIdx &&
                    g_stkBadDropCell == cellIdx &&
                    (GetTickCount() - g_stkBadDropTick) < 300) {
                    fnRect(sx,            sy,             sx + cellW, sy + 2,         1, 5);
                    fnRect(sx,            sy + cellH - 2, sx + cellW, sy + cellH,     1, 5);
                    fnRect(sx,            sy,             sx + 2,     sy + cellH,     1, 5);
                    fnRect(sx + cellW - 2,sy,             sx + cellW, sy + cellH,     1, 5);
                }
            }
        }
    }

    /* 1.9.0 Phase 4 — tooltip on hover. */
    {
        int mx = MouseX();
        int my = MouseY();
        int hoverCell = -1;
        for (int row = 0; row < STASH_GRID_ROWS && hoverCell < 0; row++) {
            for (int col = 0; col < STASH_GRID_COLS; col++) {
                int sx = gridX + col * cellW;
                int sy = gridY + row * cellH;
                if (mx >= sx && mx < sx + cellW &&
                    my >= sy && my < sy + cellH) {
                    hoverCell = row * STASH_GRID_COLS + col;
                    break;
                }
            }
        }
        if (hoverCell >= 0) {
            StkLayoutEntry* lo = &g_stkLayout[categoryIdx][hoverCell];
            StkSlot*       sl = &dataTab->slots[hoverCell];
            if (lo->dwCode != 0 && sl->count > 0 && lo->displayName) {
                /* Compose: "<displayName> ×<count>" */
                char ttBuf[96];
                _snprintf(ttBuf, sizeof(ttBuf) - 1, "%s  x%u",
                          lo->displayName, (unsigned)sl->count);
                ttBuf[sizeof(ttBuf) - 1] = 0;
                /* ASCII -> wide for fnText */
                wchar_t ttW[96];
                int ttLen = 0;
                for (; ttLen < 95 && ttBuf[ttLen]; ttLen++) {
                    ttW[ttLen] = (wchar_t)(unsigned char)ttBuf[ttLen];
                }
                ttW[ttLen] = 0;

                /* Estimate width — D2 fonts average ~7px/char at font(0). */
                int boxW = ttLen * 7 + 20;
                if (boxW < 90) boxW = 90;
                int boxH = 22;

                /* Position near cursor, clamp to screen bounds. */
                int boxX = mx + 14;
                int boxY = my - boxH - 6;
                if (boxX + boxW > g_screenW) boxX = g_screenW - boxW - 4;
                if (boxY < 0) boxY = my + 14;  /* if no room above, drop below */
                if (boxX < 0) boxX = 0;

                /* Backdrop: 5x solid black + gold border (matches Coll tooltip style). */
                if (fnRect) {
                    for (int i = 0; i < 5; i++)
                        fnRect(boxX, boxY, boxX + boxW, boxY + boxH, 0, 5);
                    fnRect(boxX,            boxY,            boxX + boxW,     boxY + 1,        4, 5);
                    fnRect(boxX,            boxY + boxH - 1, boxX + boxW,     boxY + boxH,     4, 5);
                    fnRect(boxX,            boxY,            boxX + 1,        boxY + boxH,     4, 5);
                    fnRect(boxX + boxW - 1, boxY,            boxX + boxW,     boxY + boxH,     4, 5);
                }

                if (fnText && fnFont) {
                    fnFont(0);
                    fnText(ttW, boxX + 8, boxY + boxH - 6, 0, 0);
                }
            }
        }
    }
}

/* Forward decl — StkClickPickup is defined in d2arch_stashlogic.c which is also part of the unity TU but appears AFTER this point in the source. */
extern BOOL StkClickPickup(int mx, int my);

/* Mouse click handler. Returns TRUE if the click hit a tab button and should be swallowed. */
BOOL StashUIHandleClick(int ignoredMx, int ignoredMy) {
    int i;
    int mx, my;
    (void)ignoredMx; (void)ignoredMy;
    if (!StashUI_IsStashOpen()) return FALSE;

    /* Use the same mouse source the rest of the mod uses (MouseX/MouseY in d2arch_helpers.c). */
    mx = MouseX();
    my = MouseY();

    /* STK click-pickup. Run BEFORE the tab-button loop so a click on the chest grid (while STK tab is active) gets handled here. Returns TRUE if click hit an STK cell or layout-defined black cell (we swallow it either way); FALSE if click is outside the grid (lets the tab-button loop run normally). */
    if (g_activeStashTab >= STASH_STK_AP_BASE) {
        if (StkClickPickup(mx, my)) return TRUE;
    }

    for (i = 0; i < STASH_MAX_TABS; i++) {
        int x, y, w, h;
        StashAccess acc = StashGetAccess(i);
        if (acc == STASH_ACCESS_NONE) continue;   /* hidden — not clickable */

        StashUI_BtnRect(i, &x, &y, &w, &h);
        if (!StashUIPointInRect(mx, my, x, y, w, h)) continue;

        if (acc == STASH_ACCESS_LOCKED) {
            Log("StashUI: click on LOCKED tab %d\n", i);
            return TRUE;
        }
        if (g_activeStashTab != i) {
            int prev = g_activeStashTab;
            const char* label =
                (i == 0 || i == STASH_NUM_AP_TABS) ? "P" :
                (i < STASH_NUM_AP_TABS) ? "AP" : "SH";
            int num = (i == 0 || i == STASH_NUM_AP_TABS) ? 0 :
                      (i < STASH_NUM_AP_TABS) ? i + 1 : i - STASH_NUM_AP_TABS + 1;
            /* 1.9.5 Bug 5 fix — refuse the tab swap if the player is holding an item on cursor. */
            {
                void* pCliInv = StashSwap_GetPlayerInv();
                if (pCliInv && StkFindCursorItem(pCliInv) != NULL) {
                    Log("StashUI: tab click %d->%d IGNORED — cursor has an item\n",
                        prev, i);
                    ShowNotify("Drop item before switching stash tabs");
                    return TRUE;   /* swallow click, do NOT swap */
                }
            }
            if (num)
                Log("StashUI: switched tab %d -> %d (%s%d)\n", prev, i, label, num);
            else
                Log("StashUI: switched tab %d -> %d (%s — personal)\n", prev, i, label);
            /* Move D2's live stash items into shadow[prev], then relink shadow[i] items into D2's stash. */
            StashSwapPage(prev, i);
            g_activeStashTab = i;
        }
        return TRUE;
    }
    return FALSE;
}

/* Old tab-rect click path below is unreachable (new logic above returns). */
/* (StashUIHandleClick_OLD deleted 2026-08-29: defined but never called -- dead-code sweep, docs/BUILDPLAN_2026-08-29.md part 4) */


/* Keyboard hook. 1.8.0: hotkey removed per user request. Only ESC is handled — lets the native stash close propagate to our overlay without waiting for the next UIVar poll. */
void StashUIHandleKey(int vk) {
    if (vk == VK_ESCAPE && g_stashOpen) {
        /* D2 itself also closes its stash on ESC, so UIVar will flip to 0 on next frame anyway. */
        g_stashOpen = FALSE;
        Log("StashUI: ESC -> CLOSED\n");
    }
}
