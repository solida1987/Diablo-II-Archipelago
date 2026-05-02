/* ================================================================
 * D2Archipelago - Stash UI (Phase 2: tab bar + click handling)
 *
 * Minimal overlay on top of the storage + logic layers. No DC6 art
 * is used here — the tab bar and slot grid are drawn with plain
 * fnRect (filled / outlined boxes) and fnText. This keeps the
 * module self-contained while the real D2 stash visuals are
 * investigated (see d2arch_ap.c / d2arch_drawall.c for future DC6
 * references).
 *
 * LAYOUT (800x600 reference; scaled via g_screenW/g_screenH):
 *   - The stash overlay is anchored to a 520x420 window centered
 *     on screen.
 *   - Tab bar: up to 20 buttons in two rows (10 AP + 10 shared)
 *     across the top. Only visible tabs are drawn; hidden tabs
 *     collapse out of the bar.
 *   - Below the tab bar: 6x8 slot grid (48 slots, 30x30 each).
 *
 * VISIBILITY:
 *   For phase 2 there is no reliable native-stash-open signal wired
 *   up yet, so we toggle `g_stashOpen` via F8 (VK_F8 = 0x77). The
 *   hotkey is intentionally outside the existing F1-F4 range used
 *   by the skill editor / quest log / tracker / zone map so it
 *   can't clash. A proper detection pass (reading D2's stash UI
 *   flag) is TODO — see StashUIUpdateOpenFromD2().
 *
 * ACCESS CONTROL:
 *   Each tab's visuals reflect StashGetAccess(globalIndex):
 *     READWRITE -> normal / active colors
 *     LOCKED    -> grey outline + "[LOCKED]" label
 *     READONLY  -> orange highlight (future: disable clicks)
 *     NONE      -> not drawn, not clickable
 *
 * BUILD NOTE — unity-build integration:
 * Included by d2arch.c AFTER d2arch_stash.c and d2arch_stashlogic.c
 * (see d2arch.c). Uses fnRect / fnText / fnFont / Log / MouseX /
 * MouseY directly — all visible because this file is compiled in
 * the same TU later than their defining modules.
 * ================================================================ */

/* ----------------------------------------------------------------
 * CONSTANTS
 * ---------------------------------------------------------------- */

/* Hotkey: F8 toggles the overlay. Using 0x77 (VK_F8) so we don't
 * shadow any of the existing F1..F4 bindings in d2arch_input.c. */
#define STASHUI_HOTKEY_VK       0x77

/* Overall panel size + placement. Values are in D2 "logical" coords
 * (800x600 base); the actual on-screen pixels are mapped by the
 * game's upscaler. */
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

/* ----------------------------------------------------------------
 * LOCAL STATE
 * ---------------------------------------------------------------- */

/* Anchor of the stash panel (top-left corner in logical coords). */
static int s_stashPanelX = 0;
static int s_stashPanelY = 0;

/* Cached tab-bar hit rectangles. Recomputed each frame during
 * render so the click handler can read them without recomputing.
 *
 * Only tabs whose StashGetAccess() != NONE are populated;
 * s_tabRectGlobal[i] == -1 means the row index has no mapped tab. */
typedef struct {
    int x, y, w, h;
    int globalIndex;   /* 0..STASH_MAX_TABS-1, or -1 when unused */
} StashUITabRect;

#define STASHUI_MAX_TAB_RECTS   STASH_MAX_TABS
static StashUITabRect s_tabRects[STASHUI_MAX_TAB_RECTS];
static int            s_tabRectCount = 0;

/* Edge-trigger state for the F8 hotkey (GetAsyncKeyState polling
 * fallback, in case the WndProc dispatcher doesn't fire — belt and
 * suspenders). */
static BOOL s_stashHotkeyWasDown = FALSE;

/* ----------------------------------------------------------------
 * INTERNAL HELPERS
 * ---------------------------------------------------------------- */

/* Recompute panel anchor based on current screen size. Keeps the
 * overlay centered even if resolution changes mid-session. */
static void StashUIRecomputeAnchor(void) {
    s_stashPanelX = (g_screenW - STASHUI_PANEL_W) / 2;
    s_stashPanelY = (g_screenH - STASHUI_PANEL_H) / 2;
    if (s_stashPanelX < 0) s_stashPanelX = 0;
    if (s_stashPanelY < 0) s_stashPanelY = 0;
}

/* Point-in-rect test matching d2arch_helpers.c::InRect (same module
 * is visible here, but we rewrite it locally to avoid the guarantee
 * that InRect was included before this file). */
static BOOL StashUIPointInRect(int px, int py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px < rx + rw && py >= ry && py < ry + rh) ? TRUE : FALSE;
}

/* Draw a 1-pixel rectangle outline using four thin fnRect strokes.
 * color indices follow the project convention (see drawall.c): 0
 * white, 2 green, 4 gold, 5 grey, 7 orange, 9 blue, 11 red. */
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

/* Compute tab positions + fill s_tabRects[]. Two rows: AP tabs in
 * the top row, shared tabs in the second row. Hidden tabs (access
 * == NONE) are skipped and don't consume a slot in the bar. */
static void StashUIComputeTabLayout(void) {
    int rowY;
    int perRow = STASH_NUM_AP_TABS;   /* 10 tabs wide is the design */
    int rowW   = perRow * STASHUI_TAB_W + (perRow - 1) * STASHUI_TAB_GAP;
    /* Position tab strip at top-center of screen (not centered on an
     * overlay panel). Leave ~40px for version text. */
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
        /* AP tabs labeled AP1-AP10, shared tabs S1-S10.
         * Keeps labels short enough to fit in narrow tab buttons. */
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

/* Draw the 6x8 slot grid for the currently active tab. Item
 * rendering is Phase 5 and out of scope here — virtual stacks show
 * their count, real items show a small "I" placeholder, empty slots
 * are just outlined. */
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
    /* Semi-transparent black fill (color 1 == dark, but any works
     * with trans=3 here — mirrors menu boxes elsewhere). */
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

/* Diagnostic: scan UIVars 0..31 for changes, log when any flips. This
 * tells us which UIVar index corresponds to stash. Runs once per frame
 * at low cost (32 cheap calls). */
static DWORD s_lastUIVar[32] = {0};
static BOOL  s_uiVarInitialized = FALSE;

/* D2Client_GetUIState is __stdcall (arg on stack). The mod's existing
 * wrapper in api.c used "mov eax, ecx; jmp" which only works if the
 * target is some weird register-EAX ABI — doesn't match D2 actually.
 * Resolve via ordinal 10006 in D2Client (the real GetUIState export).
 * Falls back to D2Client + 0xBE400 if ordinal lookup fails. */
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

/* D2 1.10f doesn't expose GetUIState via ordinal, and the internal
 * function at 0xBE400 has an unknown ABI (all calls return 0).
 * Pragmatic fallback: show the tab bar whenever a character is
 * loaded. User can still click tabs to switch active tab — when the
 * native stash chest opens, the tab bar is already visible. */
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

    /* Every 60 frames (~1 sec) dump a snapshot of ALL non-zero UIVars
     * so we can see them when the user opens/closes the stash */
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

/* ----------------------------------------------------------------
 * PUBLIC API
 * ---------------------------------------------------------------- */

void StashUIInit(void) {
    memset(s_tabRects, 0, sizeof(s_tabRects));
    s_tabRectCount = 0;
    s_stashHotkeyWasDown = FALSE;
    StashUIRecomputeAnchor();
    Log("StashUI: initialized (panel %dx%d anchor=%d,%d — follows UIVar[14] native stash)\n",
        STASHUI_PANEL_W, STASHUI_PANEL_H,
        s_stashPanelX, s_stashPanelY);
}

/* Phase 1 tab buttons.
 *
 * Layout: 20 small square buttons in a vertical column on the LEFT
 * side of D2's native stash panel. The native stash grid (Ultimate
 * Inventory mod, 10x10) renders at approximately screen-x 16..302 and
 * y 82..371. Our button column is placed just to the left of that,
 * at x=0..18, y=100..500 (50 px gap between rows).
 *
 * Rows 0..9 = AP (private) tabs, rows 10..19 = Shared tabs.
 *
 * Visibility: buttons only render when player is in a TOWN area (the
 * only places with a physical stash chest). This sidesteps the
 * unreliable UIVar/stash-open detection while matching the real
 * usage pattern (players only open stash in town).
 *
 * Interaction: click a button -> g_activeStashTab updates to that
 * global index. Locked/hidden access modes grey out the button and
 * swallow clicks without switching. Phase 1 does NOT yet swap stash
 * grid contents when the tab changes; that's Phase 2 (hook into D2
 * inventory grid render and replace pGrids[STASH] with our shadow). */

/* Compact button column on left edge. Height increased so font 6 text
 * fits fully inside with vertical padding. */
#define TAB_COL_X          145
#define TAB_BTN_W          32
#define TAB_BTN_H          22
#define TAB_BTN_GAP         1
#define TAB_ROW_Y_AP_0     55
#define TAB_ROW_Y_SH_0    (TAB_ROW_Y_AP_0 + STASH_NUM_AP_TABS * (TAB_BTN_H + TAB_BTN_GAP) + 8)
/* 1.9.0 — second column (STK tabs). Sits immediately right of column 1
 * with a small gap so the buttons don't visually fuse together. */
#define TAB_COL2_X         (TAB_COL_X + TAB_BTN_W + 4)

/* Hardcoded town area IDs (5 acts). */
static BOOL StashUI_InTown(void) {
    extern int GetCurrentArea(void);
    int a = GetCurrentArea();
    return (a == 1 || a == 40 || a == 75 || a == 103 || a == 109) ? TRUE : FALSE;
}

/* D2Client 1.10f — live UI-panel state array.
 *
 * Reverse-engineered from D2Client.dll disassembly:
 *
 *   GetUIState @ 6fb23230:
 *     cmp ecx, 0x26                       ; bounds (NUM_UI = 38)
 *     ...
 *     mov eax, [ecx*4 + 0x6fbba6a8]       ; eax = gpUIState[id]
 *     ret
 *
 *   ToggleUI @ 6fb23260:
 *     ... cmp esi, 0x26 / cmp ebx, 0x2 ...
 *     mov eax, [esi*4 + 0x6fbba6a8]       ; read current state
 *     ...
 *     mov [esi*4 + 0x6fbba6a8], edx       ; write new state
 *
 * Image base = 0x6FAA0000, so RVA(gpUIState) = 0x11A6A8.
 * Array: 38 DWORDs indexed by D2C_UiPanel (see D2MOO D2Constants.h).
 * UI_STASH = 0x19 ⇒ stash state at gpUIState[0x19].
 *
 * Non-zero DWORD = panel currently open. Unlike the shared
 * `screen_shift` flag (used before), this array holds ONE entry per
 * panel, so character (UI_CHARACTER=0x02) and stash (UI_STASH=0x19)
 * do not collide. */
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

    /* Log first call + any transition, to confirm this offset actually
     * flips when the user opens/closes the stash. */
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
        /* 1.9.0 — Column 2, STK_AP rows (20..22). Top of column = next
         * to the AP-side P button. Mirrors AP rows 0..2 vertically. */
        colX  = TAB_COL2_X;
        baseY = TAB_ROW_Y_AP_0;
        local = globalIdx - STASH_STK_AP_BASE;
    } else {
        /* 1.9.0 — Column 2, STK_SH rows (23..25). Mirrors SH rows 0..2. */
        colX  = TAB_COL2_X;
        baseY = TAB_ROW_Y_SH_0;
        local = globalIdx - STASH_STK_SH_BASE;
    }

    *x = colX;
    *y = baseY + local * (TAB_BTN_H + TAB_BTN_GAP);
    *w = TAB_BTN_W;
    *h = TAB_BTN_H;
}

/* 1.9.0 — forward decl so StashUIRender can call it before it's defined
 * later in this file. */
static void StashUIRenderStkOverlay(int activeTabGlobal);

void StashUIRender(void) {
    int i;
    static int s_renderLogCounter = 0;
    s_renderLogCounter++;

    if (!fnRect || !fnText || !fnFont) {
        if (s_renderLogCounter % 120 == 0) {
            Log("StashUIRender: fnRect=%p fnText=%p fnFont=%p (missing function pointer)\n",
                fnRect, fnText, fnFont);
        }
        return;
    }

    BOOL isOpen = StashUI_IsStashOpen();
    static BOOL s_prevOpen = FALSE;
    static int  s_openFrames = 0;
    if (s_renderLogCounter % 120 == 0) {
        Log("StashUIRender: called, isStashOpen=%d screenW=%d\n", (int)isOpen, g_screenW);
    }

    /* Auto-swap back to Personal tab when stash closes. This keeps
     * pInventory's stash items consistent with "P" so that at char
     * save, D2's .d2s has the Personal tab's contents (and other tabs
     * live in shadow_*.dat files). Without this, whichever tab was
     * active at save becomes mislabeled as P on next load.
     *
     * Debounce: only fire after stash was open for >= 3 consecutive
     * frames, to avoid screen_shift probe flicker. */
    if (isOpen) {
        s_openFrames++;
    } else {
        if (s_prevOpen && s_openFrames >= 3) {
            int personalTab = g_apMode ? 0 : STASH_NUM_AP_TABS;
            if (g_activeStashTab != personalTab) {
                /* 1.9.0: kill switch for auto-swap-on-close.
                 * D2 1.10f's ITEMS_SerializeItemToBitstream may not preserve
                 * socketed items reliably for runeword items, causing runes
                 * to vanish on respawn. Set [Debug] DisableStashAutoSwap=1
                 * in d2arch.ini to skip the auto-swap and keep items live in
                 * pInventory across stash open/close. The user must then
                 * explicitly click the Personal tab before logging out, or
                 * the .d2s save will record whichever tab was active. */
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
                } else {
                    Log("StashUI: stash closing (openFrames=%d) auto-swap tab %d → P (%d)\n",
                        s_openFrames, g_activeStashTab, personalTab);
                    StashSwapPage(g_activeStashTab, personalTab);
                    g_activeStashTab = personalTab;
                }
            }
        }
        s_openFrames = 0;
    }
    s_prevOpen = isOpen;

    if (!isOpen) return;

    /* Ensure the currently-active tab is actually visible for this character
     * mode. If not (e.g. fresh non-AP char starts at tab 0 which is an AP
     * tab = hidden), snap to the first visible tab in display order. This
     * also runs every frame so a state change (goal complete -> shared
     * unlocks) can't leave us pointing at a hidden tab. */
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
            Log("StashUIRender: STASH OPEN — drawing buttons starting at (%d,%d) size %dx%d, "
                "STASH_MAX_TABS=%d apMode=%d apGoalComplete=%d\n",
                x, y, w, h, STASH_MAX_TABS, (int)g_apMode, (int)g_apGoalComplete);
            /* 1.9.0 — list every tab's access + position so we can confirm
             * STK buttons are actually being included in the draw loop. */
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

    /* Visibility matrix (see StashGetAccess in d2arch_stash.c):
     *   non-AP char:      only SH1..SH10 (orange) visible; AP hidden.
     *   AP char pre-goal: only AP1..AP10 (red)    visible; SH hidden.
     *   AP char post-goal: all 20 visible.
     * STASH_ACCESS_NONE tabs are skipped entirely. */
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

        /* Color scheme:
         *   AP tabs (red 11) / SH tabs (orange 7) / STK tabs (gold 4)
         *   Active tab highlights in gold (4).
         *   LOCKED greys out. */
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
            /* 1.9.0 — STK tabs (both AP and SH variants): yellow/gold */
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

        /* Labels. Tab 0 (first AP) and tab STASH_NUM_AP_TABS (first SH)
         * are labeled "P" for "personal" — their contents are saved in
         * the character's own .d2s via pInventory, not in the cross-
         * character shared_stash_ser.dat. The remaining AP2..AP10 and
         * SH2..SH10 share their pool across characters (AP tabs via
         * ap_stash_ser_<char>.dat which is per-char; SH tabs via the
         * account-wide shared_stash_ser.dat). */
        fnFont(6);
        if (i == 0 || i == STASH_NUM_AP_TABS) {
            _snwprintf(label, 15, L"P");
        } else if (i < STASH_NUM_AP_TABS) {
            _snwprintf(label, 15, L"AP%d", i + 1);
        } else if (i < STASH_NUM_AP_TABS + STASH_NUM_SHARED_TABS) {
            _snwprintf(label, 15, L"SH%d", i - STASH_NUM_AP_TABS + 1);
        } else if (i < STASH_STK_AP_BASE + STASH_NUM_STK_TABS) {
            /* 1.9.0 — STK_AP: 3-letter category code */
            int cat = i - STASH_STK_AP_BASE;
            const wchar_t* cats[] = { L"CON", L"RUN", L"GEM" };
            _snwprintf(label, 15, L"%ls", cats[cat]);
        } else {
            /* 1.9.0 — STK_SH: same labels (different scope) */
            int cat = i - STASH_STK_SH_BASE;
            const wchar_t* cats[] = { L"CON", L"RUN", L"GEM" };
            _snwprintf(label, 15, L"%ls", cats[cat]);
        }
        label[15] = 0;
        /* Text anchored to LEFT edge of button with small padding.
         * "centered" flag 0 = left-aligned, x is left edge of text.
         * y is BASELINE (text extends upward), so add ~2/3 of box height
         * to keep glyph inside the box. Box h=22 -> y+13 puts text
         * roughly in the lower-middle of the button. */
        fnText(label, x + 3, y + 13, text, 0);
    }

    /* 1.9.0 — render the STK ghost-image overlay AFTER tab buttons so
     * it draws on top of any vanilla chest grid pixels that would
     * otherwise show through. No-op when the active tab is not STK. */
    StashUIRenderStkOverlay(g_activeStashTab);
}

/* ----------------------------------------------------------------
 * 1.9.0 — STK ghost-image overlay
 * ----------------------------------------------------------------
 * When the active tab is one of the STK tabs (global index >=
 * STASH_STK_AP_BASE) the underlying pInventory has no items in the
 * stash slots (StashSwapPage's STK branch leaves them empty). We
 * draw the layout's ghost icons + counters directly over the chest
 * grid here.
 *
 * Grid screen coordinates are tunable via d2arch.ini [Stash] keys
 * StkGridX / StkGridY / StkCellW / StkCellH. Defaults match the
 * 1.10f Ultimate Inventory mod's Big Bank Page 1 layout (gridLeft=16,
 * gridTop=82 within the stash window) but can be shifted at runtime
 * if the actual on-screen grid sits elsewhere on the user's setup. */

/* Validate a loaded cel pointer's DC6 version magic. Returns TRUE if
 * the pointer looks like a real DC6 file (version field = 6). Drawing
 * a bad pointer triggers a D2 assertion (D2CMP\\SRC\\CelCmp.cpp:1485). */
static BOOL StkCelLooksValid(void* pCel) {
    if (!pCel) return FALSE;
    DWORD version = 0;
    __try { version = *(DWORD*)pCel; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return FALSE; }
    return (version == 6) ? TRUE : FALSE;
}

/* Resolve the cel for a layout cell.
 *
 * Tries TWO paths (low cost, no per-frame lag):
 *   1. data\global\items\<invFile>     — path used for our 5 custom
 *      loose Pandemonium DC6 files (invbrnz / inveye / invfang /
 *      invmph / invtrch). Confirmed present in repo.
 *   2. data\global\items\inv\<invFile> — standard D2 path, but in
 *      1.10f's MPQ search context fnCelLoad does NOT have access to
 *      d2data.mpq's item icons (returns garbage). So this path will
 *      fail for hp1/mp1/runes/gems unless the user has extracted
 *      them as loose files at this location.
 *
 * Cached as (void*)-1 sentinel on miss so we don't thrash the loader
 * every render frame. */
static void* StkResolveCel(int categoryIdx, int cellIdx) {
    if (categoryIdx < 0 || categoryIdx >= STASH_NUM_STK_TABS) return NULL;
    if (cellIdx    < 0 || cellIdx    >= STASH_SLOTS_PER_TAB) return NULL;
    StkLayoutEntry* e = &g_stkLayout[categoryIdx][cellIdx];
    if (!e->invFile)                return NULL;
    if (e->pCachedCel == (void*)-1) return NULL;
    if (e->pCachedCel)              return e->pCachedCel;
    if (!fnCelLoad)                 return NULL;

    /* Try 3 paths in order:
     *   1. data\global\items\stk\<file>  — files we placed there for STK
     *   2. data\global\items\<file>      — Pandemonium-event loose files
     *      (invmph, invfang, invbrnz, inveye, invtrch — used by vanilla
     *      rendering for our custom event items)
     *   3. data\global\items\inv\<file>  — standard MPQ path (rarely
     *      works in our hook context but tried as last resort) */
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
        /* No per-cell logging on miss — we expect most standard items
         * to fail until DC6s are extracted. Logged once via summary. */
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

    /* Grid screen coords — INI tunable so the user can shift if needed.
     * Defaults match the empirically-calibrated stash grid origin used by
     * StashQuickMoveToStash (see d2arch_stashlogic.c ~line 1574, calibrated
     * for 1068x600 with the Ultimate Inventory mod). */
    int gridX = StkIniInt("StkGridX", 232);
    int gridY = StkIniInt("StkGridY", 143);
    int cellW = StkIniInt("StkCellW", 29);
    int cellH = StkIniInt("StkCellH", 29);

    /* Diagnostic: log first time per session-tab so we can verify the
     * overlay actually runs and confirm grid coords. Throttled to once
     * per (tab) so it doesn't spam every frame. */
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

            /* 1.9.0 — multi-cell items: only the origin cell renders.
             * Sub-cells skip their own draw so the icon (which may be
             * smaller than the slot) doesn't fight with itself. */
            if (!layout->isOriginCell) continue;

            /* 1.9.0 — visual indicator of multi-cell footprint: thin
             * gold border around the FULL multi-cell area. No backdrop
             * fill — the icon (which may be smaller than the slot)
             * sits centered against vanilla's empty cell background.
             * Border alone is enough to show the slot is reserved. */
            if (layout->cellsW > 1 || layout->cellsH > 1) {
                int fullW = layout->cellsW * cellW;
                int fullH = layout->cellsH * cellH;
                fnRect(sx,             sy,             sx + fullW, sy + 1,     4, 5);
                fnRect(sx,             sy + fullH - 1, sx + fullW, sy + fullH, 4, 5);
                fnRect(sx,             sy,             sx + 1,     sy + fullH, 4, 5);
                fnRect(sx + fullW - 1, sy,             sx + fullW, sy + fullH, 4, 5);
            }

            /* 1.9.0 — render the actual item icon as a translucent
             * ghost. StkResolveCel validates the DC6 version after
             * loading so we can never pass a bad pointer to fnCelDraw
             * (the previous CelCmp assertion crash is gated by that
             * check now). If the cel doesn't load (missing file or
             * format mismatch) we fall back to a small text label so
             * the cell isn't completely empty. */
            void* pCel = StkResolveCel(categoryIdx, cellIdx);

            StkSlot* slot = &dataTab->slots[cellIdx];

            if (pCel && fnCelDraw) {
                /* fnCelDraw signature: (cel, x, y, gamma, drawMode, palette).
                 * D2 inv icons anchor on the BOTTOM-LEFT and extend
                 * UPWARD by the cel's height. For multi-cell items
                 * (cellsW × cellsH > 1), the cel was authored at the
                 * full multi-cell pixel size, so we anchor at the
                 * BOTTOM-LEFT of the FULL multi-cell area, not just
                 * the origin cell. drawMode=2 trans50, 5 solid. */
                int iconX = sx + 1;
                int iconY = sy + (layout->cellsH * cellH) - 1;
                fnCelDraw(pCel, iconX, iconY, 0, 2, -1);
                if (slot->count > 0) {
                    fnCelDraw(pCel, iconX, iconY, 0, 5, -1);
                }
            } else {
                /* Cel didn't load — draw the 3-char code as a fallback
                 * so the cell still indicates what item belongs there. */
                if (fnText && fnFont) {
                    wchar_t lbl[8] = {0};
                    lbl[0] = (wchar_t)(layout->dwCode & 0xFF);
                    lbl[1] = (wchar_t)((layout->dwCode >> 8) & 0xFF);
                    lbl[2] = (wchar_t)((layout->dwCode >> 16) & 0xFF);
                    fnFont(6);
                    /* Dim grey so it reads as a placeholder, not a real
                     * rendered item. */
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

            /* 1.9.0 Phase 4 — red-flash overlay when this cell was the
             * target of a recent rejected deposit (wrong-type or stack
             * full). 2-px red border around the cell, visible for
             * STK_BAD_DROP_FLASH_MS (300ms). fnRect mode 5 = solid fill,
             * color 1 = bright red. Drawn AFTER the icon so it overlays. */
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

    /* 1.9.0 Phase 4 — tooltip on hover. After all cells are drawn,
     * find which (if any) cell the mouse is currently over and render
     * a tooltip box near the cursor showing the item name + count.
     * Only shown when the cell is BOTH a valid layout cell (dwCode != 0)
     * AND the slot is populated (count > 0). Empty cells silently
     * skip — no point telling the user "nothing here".
     *
     * Drawn LAST so it overlays everything else in the stash UI. */
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
                /* ASCII → wide for fnText */
                wchar_t ttW[96];
                int ttLen = 0;
                for (; ttLen < 95 && ttBuf[ttLen]; ttLen++) {
                    ttW[ttLen] = (wchar_t)(unsigned char)ttBuf[ttLen];
                }
                ttW[ttLen] = 0;

                /* Estimate width — D2 fonts average ~7px/char at font(0).
                 * Pad +20 for the box edges + breathing room. */
                int boxW = ttLen * 7 + 20;
                if (boxW < 90) boxW = 90;
                int boxH = 22;

                /* Position near cursor, clamp to screen bounds.
                 * g_screenW / g_screenH are file-scope from input.c. */
                int boxX = mx + 14;
                int boxY = my - boxH - 6;
                if (boxX + boxW > g_screenW) boxX = g_screenW - boxW - 4;
                if (boxY < 0) boxY = my + 14;  /* if no room above, drop below */
                if (boxX < 0) boxX = 0;

                /* Backdrop: 5x solid black + gold border (matches Coll
                 * tooltip style). */
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

/* Forward decl — StkClickPickup is defined in d2arch_stashlogic.c which
 * is also part of the unity TU but appears AFTER this point in the
 * source. Declaring as extern here is technically a no-op for unity
 * builds (everything is one TU) but keeps intent explicit. */
extern BOOL StkClickPickup(int mx, int my);

/* Mouse click handler. Returns TRUE if the click hit a tab button
 * and should be swallowed. */
BOOL StashUIHandleClick(int ignoredMx, int ignoredMy) {
    int i;
    int mx, my;
    (void)ignoredMx; (void)ignoredMy;
    if (!StashUI_IsStashOpen()) return FALSE;

    /* Use the same mouse source the rest of the mod uses (MouseX/MouseY in
     * d2arch_helpers.c). It applies proper letterbox-aware scaling that
     * the WndProc coord conversion misses, so clicks hit the same rects
     * that were drawn in 800x600 logical space. */
    mx = MouseX();
    my = MouseY();

    /* 1.9.0 — STK click-pickup. Run BEFORE the tab-button loop so a
     * click on the chest grid (while STK tab is active) gets handled
     * here. Returns TRUE if click hit an STK cell or layout-defined
     * black cell (we swallow it either way); FALSE if click is outside
     * the grid (lets the tab-button loop run normally). */
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
            if (num)
                Log("StashUI: switched tab %d -> %d (%s%d)\n", prev, i, label, num);
            else
                Log("StashUI: switched tab %d -> %d (%s — personal)\n", prev, i, label);
            /* Move D2's live stash items into shadow[prev], then relink
             * shadow[i] items into D2's stash. Done before updating
             * g_activeStashTab so Drain reads the old tab as "currently
             * native" and Fill replaces with the new one. */
            StashSwapPage(prev, i);
            g_activeStashTab = i;
        }
        return TRUE;
    }
    return FALSE;
}

/* Old tab-rect click path below is unreachable (new logic above returns). */
static BOOL StashUIHandleClick_OLD(int mx, int my) {
    int i;
    if (!g_stashOpen) return FALSE;

    StashUIRecomputeAnchor();
    StashUIComputeTabLayout();

    for (i = 0; i < s_tabRectCount; i++) {
        StashUITabRect* rc = &s_tabRects[i];
        if (StashUIPointInRect(mx, my, rc->x, rc->y, rc->w, rc->h)) {
            StashAccess acc = StashGetAccess(rc->globalIndex);
            if (acc == STASH_ACCESS_NONE || acc == STASH_ACCESS_LOCKED) {
                Log("StashUI: click on locked/invisible tab %d (acc=%d)\n",
                    rc->globalIndex, (int)acc);
                return TRUE;
            }
            g_activeStashTab = rc->globalIndex;
            Log("StashUI: active tab -> %d\n", g_activeStashTab);
            return TRUE;
        }
    }

    /* Click inside the panel frame but not on a tab — still consume
     * it so the player doesn't accidentally command their character
     * through the overlay. */
    if (StashUIPointInRect(mx, my, s_stashPanelX, s_stashPanelY,
                           STASHUI_PANEL_W, STASHUI_PANEL_H)) {
        return TRUE;
    }

    return FALSE;
}

/* Keyboard hook. 1.8.0: hotkey removed per user request. Only ESC is
 * handled — lets the native stash close propagate to our overlay
 * without waiting for the next UIVar poll. */
void StashUIHandleKey(int vk) {
    if (vk == VK_ESCAPE && g_stashOpen) {
        /* D2 itself also closes its stash on ESC, so UIVar will
         * flip to 0 on next frame anyway. We pre-empt so the overlay
         * vanishes immediately with no one-frame lag. */
        g_stashOpen = FALSE;
        Log("StashUI: ESC -> CLOSED\n");
    }
}
