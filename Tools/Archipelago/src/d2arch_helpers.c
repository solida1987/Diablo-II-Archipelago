/* ================================================================
 * HELPER: Get player unit pointer
 * ================================================================ */
static void* Player(void) {
    if (!fnGetPlayer) return NULL;
    __try { return fnGetPlayer(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}

/* ================================================================
 * 1.9.0 — INI HOT RELOAD
 *
 * Renderers cache d2arch.ini values in static locals to avoid
 * hitting the file on every paint. That's fast but means UI tweaks
 * to d2arch.ini only take effect after a character relog.
 *
 * Solution: poll the INI's last-write time once every 500 ms; when
 * it changes, bump g_iniGen. Each renderer stores the gen it last
 * loaded with, and re-reads the INI when its cached gen != global.
 *
 * Renderers opt in via the INI_HOT_RELOAD_GUARD(flagVar) macro
 * placed right before their `if (!s_loaded) { ... load INI ... }`
 * block — it clears the load flag on gen mismatch, so the existing
 * load path runs again.
 * ================================================================ */
int g_iniGen = 0;

static void IniHotReload_Tick(void) {
    static FILETIME s_lastWrite = {0};
    static DWORD s_lastCheck = 0;
    static int s_initialized = 0;

    DWORD now = GetTickCount();
    if (now - s_lastCheck < 500) return;
    s_lastCheck = now;

    char ini[MAX_PATH];
    GetArchDir(ini, MAX_PATH);
    strcat(ini, "d2arch.ini");

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(ini, GetFileExInfoStandard, &fad)) return;

    if (!s_initialized) {
        s_initialized = 1;
        s_lastWrite = fad.ftLastWriteTime;
        return;
    }
    if (CompareFileTime(&fad.ftLastWriteTime, &s_lastWrite) != 0) {
        s_lastWrite = fad.ftLastWriteTime;
        g_iniGen++;
        Log("INI HOT RELOAD: d2arch.ini changed -> g_iniGen=%d\n", g_iniGen);
    }
}

#define INI_HOT_RELOAD_GUARD(flagVar) do { \
    static int _ihGen = -1; \
    if (_ihGen != g_iniGen) { (flagVar) = 0; _ihGen = g_iniGen; } \
} while (0)

/* ================================================================
 * HELPER: Read mouse position
 * ================================================================ */
/* Convert window pixel coords to game-space coords.
 * Accounts for d2gl upscaling + SGD2FreeRes resolution + letterboxing. */
static void WindowToGame(int winX, int winY, int* gameX, int* gameY) {
    RECT rc;
    GetClientRect(g_gameHwnd, &rc);
    int cw = rc.right - rc.left;
    int ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) { *gameX = winX; *gameY = winY; return; }

    float scaleX = (float)cw / (float)g_screenW;
    float scaleY = (float)ch / (float)g_screenH;
    float scale = (scaleX < scaleY) ? scaleX : scaleY;
    int offX = (int)((cw - (float)g_screenW * scale) / 2.0f);
    int offY = (int)((ch - (float)g_screenH * scale) / 2.0f);

    *gameX = (scale > 0) ? (int)((winX - offX) / scale) : winX;
    *gameY = (scale > 0) ? (int)((winY - offY) / scale) : winY;
}

/* Mouse coordinate mapping.
 * pMouseX/pMouseY always report in D2's base 800x600 space.
 * When SGD2FreeRes widens the game (e.g. 1068x600), the mouse Y range
 * shrinks due to letterboxing: the visible game area is a sub-rectangle
 * of the 800x600 mouse space.
 *
 * Calculation (verified against measured values for 1068x600):
 *   visiblePixels = floor(baseSize * baseSize / gameSize)
 *   offset = (baseSize - visiblePixels) / 2
 *
 * For 1068x600:  visibleH = floor(600*800/1068) = 449
 *                offsetY  = (600 - 449) / 2 = 75
 *                Y range  = 75..523  (75 + 449 - 1)
 *                X range  = 0..799   (no change, game is wider not taller)
 */
static int g_mouseOffX = 0, g_mouseRangeX = 800;
static int g_mouseOffY = 0, g_mouseRangeY = 600;

static void CalcMouseMapping(void) {
    float gameAspect = (float)g_screenW / (float)g_screenH;
    float baseAspect = 800.0f / 600.0f;

    if (gameAspect > baseAspect) {
        /* Game is wider than 4:3 → Y gets letterboxed in mouse space */
        g_mouseRangeX = 800;
        g_mouseOffX = 0;
        g_mouseRangeY = 600 * 800 / g_screenW;  /* integer floor */
        g_mouseOffY = (600 - g_mouseRangeY) / 2;
    } else if (gameAspect < baseAspect) {
        /* Game is taller than 4:3 → X gets letterboxed in mouse space */
        g_mouseRangeY = 600;
        g_mouseOffY = 0;
        g_mouseRangeX = 800 * g_screenH / (g_screenW * 600 / 800);
        g_mouseOffX = (800 - g_mouseRangeX) / 2;
    } else {
        /* Same aspect ratio → no offset */
        g_mouseRangeX = 800;
        g_mouseOffX = 0;
        g_mouseRangeY = 600;
        g_mouseOffY = 0;
    }

    Log("MouseMap: X off=%d range=%d -> 0..%d, Y off=%d range=%d -> 0..%d\n",
        g_mouseOffX, g_mouseRangeX, g_screenW,
        g_mouseOffY, g_mouseRangeY, g_screenH);
}

static int MouseX(void) {
    if (g_gameHwnd) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(g_gameHwnd, &pt);
        int gx, gy;
        WindowToGame(pt.x, pt.y, &gx, &gy);
        return gx;
    }
    return pMouseX ? *pMouseX : 0;
}
static int MouseY(void) {
    if (g_gameHwnd) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(g_gameHwnd, &pt);
        int gx, gy;
        WindowToGame(pt.x, pt.y, &gx, &gy);
        return gy;
    }
    return pMouseY ? *pMouseY : 0;
}

/* ================================================================
 * HELPER: Check left mouse click (edge-triggered)
 * ================================================================ */
static BOOL g_wasLDown = FALSE;
static BOOL g_click    = FALSE;

static void UpdateClick(void) {
    BOOL ld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    g_click = (ld && !g_wasLDown);
    g_wasLDown = ld;
}

static BOOL MouseClicked(void) {
    return g_click;
}

/* ================================================================
 * HELPER: Check if point is inside rectangle
 * ================================================================ */
static BOOL InRect(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

/* ================================================================
 * HELPER: Draw text wrapper (ASCII -> wide)
 * ================================================================ */
static void DrawText2(const char* text, int x, int y, int color, int centered) {
    if (!fnText || !fnFont) return;
    wchar_t wbuf[256];
    MultiByteToWideChar(CP_ACP, 0, text, -1, wbuf, 256);
    fnText(wbuf, x, y, color, centered);
}

/* ================================================================
 * HELPER: Draw filled rectangle
 * ================================================================ */
static void DrawBox(int x, int y, int w, int h, int color, int alpha) {
    if (!fnRect) return;
    fnRect(x, y, x + w, y + h, color, alpha);
}

/* ================================================================
 * SAVE/LOAD SYSTEM
 * ================================================================ */

/* Get path to Archipelago data folder */
static void GetArchDir(char* buf, int bufSize) {
    GetModuleFileNameA(NULL, buf, bufSize);
    char* sl = strrchr(buf, '\\');
    if (sl) *(sl + 1) = 0;
    /* Safe append — check remaining space */
    if ((int)strlen(buf) + 14 < bufSize)
        strcat(buf, "Archipelago\\");
    CreateDirectoryA(buf, NULL);
}

/* Detect game resolution from SGD2FreeResolution.json.
 * Sets g_screenW/g_screenH to the in-game resolution.
 * Falls back to 800x600 if file not found or parse error. */
static void DetectResolution(void) {
    char jsonPath[MAX_PATH];
    GetModuleFileNameA(NULL, jsonPath, MAX_PATH);
    char* sl = strrchr(jsonPath, '\\');
    if (sl) *(sl + 1) = 0;
    strcat(jsonPath, "SGD2FreeResolution.json");

    FILE* f = fopen(jsonPath, "r");
    if (!f) {
        Log("DetectResolution: no SGD2FreeResolution.json, using %dx%d\n", g_screenW, g_screenH);
        return;
    }

    /* Read entire file (small JSON, <2KB) */
    char buf[2048];
    int len = (int)fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[len] = 0;

    /* Find "Ingame Resolutions": ["WxH"] — simple string search.
     * After key, skip to '[', then find first '"' inside the array. */
    char* key = strstr(buf, "Ingame Resolutions");
    if (!key) {
        Log("DetectResolution: no 'Ingame Resolutions' key, using %dx%d\n", g_screenW, g_screenH);
        return;
    }
    /* Find the '[' that starts the array */
    char* bracket = strchr(key, '[');
    if (!bracket) { Log("DetectResolution: no '[' after key\n"); return; }
    /* Find first quoted string inside array */
    char* q1 = strchr(bracket, '"');
    if (!q1) { Log("DetectResolution: no '\"' in array\n"); return; }
    q1++; /* skip opening quote */
    char* q2 = strchr(q1, '"');
    if (!q2) { Log("DetectResolution: no closing '\"'\n"); return; }
    /* Null-terminate the resolution string */
    *q2 = 0;

    /* Parse "WIDTHxHEIGHT" */
    int w = 0, h = 0;
    if (sscanf(q1, "%dx%d", &w, &h) == 2 && w >= 640 && h >= 480) {
        g_screenW = w;
        g_screenH = h;
        Log("DetectResolution: game resolution = %dx%d\n", g_screenW, g_screenH);
    } else {
        Log("DetectResolution: parse failed on '%s', using %dx%d\n", q1, g_screenW, g_screenH);
    }
}
