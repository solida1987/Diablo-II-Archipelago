/* ================================================================
 * d2arch_celhook.c — Diagnostic hook on D2Win ordinal 10039
 *
 * D2Win exports a cellfile loader at ordinal 10039: the function every
 * D2 subsystem calls to resolve "data\global\ui\..." paths to DC6
 * sprite handles. By hooking it we log every path D2 asks for,
 * including the splash (TitleScreen) and main menu (gameselectscreenEXP).
 *
 * This is purely a diagnostic — we log and call the original, we never
 * alter the outcome. Enable via the ON flag, rebuild, restart, then read
 * d2arch_log.txt for "CEL LOAD:" lines.
 *
 * Unity build: include AFTER d2arch_api.c (fnCelLoad) and AFTER
 * d2arch_config.c (Log).
 * ================================================================ */

#define CEL_HOOK_ENABLED 0   /* DISABLED — 5-byte prologue patch crashed game */

typedef void* (__fastcall *CelLoad_t)(const char* path, int type);

static CelLoad_t  g_origCelLoad = NULL;
static BYTE       g_celHookOrigBytes[5];
static DWORD      g_celHookTarget = 0;  /* original function address */
static BYTE       g_celHookTramp[16];   /* trampoline: 5 copied bytes + JMP back */
static int        g_celHookInstalled = 0;
static int        g_celHookLogCount = 0;

/* Our wrapper — called in place of the original. Must match __fastcall
 * calling convention: ECX = path (pointer), EDX = type (int). */
static void* __fastcall CelLoadWrapper(const char* path, int type) {
    /* Log the path — cap to avoid spam after 200 entries */
    if (g_celHookLogCount < 200) {
        __try {
            Log("CEL LOAD: '%s' type=%d\n", path ? path : "(null)", type);
            g_celHookLogCount++;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Log("CEL LOAD: <exception reading path>\n");
        }
    }
    /* Call the trampoline which executes original's first 5 bytes then
     * jumps back into the middle of the original function. */
    CelLoad_t tramp = (CelLoad_t)(void*)g_celHookTramp;
    void* result = NULL;
    __try {
        result = tramp(path, type);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("CEL LOAD: exception in original (path='%s')\n", path ? path : "(null)");
    }
    return result;
}

/* Install the 5-byte JMP hook at D2Win ordinal 10039.
 * Resolves the target itself from D2Win.dll so it can run early (before
 * InitAPI resolves fnCelLoad). Safe to call multiple times. */
static void CelHookInstall(void) {
    if (!CEL_HOOK_ENABLED) return;
    if (g_celHookInstalled) return;

    HMODULE hD2W = GetModuleHandleA("D2Win.dll");
    if (!hD2W) {
        Log("CelHook: D2Win.dll not loaded yet, skipping install\n");
        return;
    }
    CelLoad_t resolved = (CelLoad_t)GetProcAddress(hD2W, (LPCSTR)10039);
    if (!resolved) {
        Log("CelHook: D2Win ordinal 10039 GetProcAddress failed\n");
        return;
    }

    g_celHookTarget = (DWORD)resolved;
    BYTE* target = (BYTE*)g_celHookTarget;

    /* Save original first 5 bytes */
    __try {
        memcpy(g_celHookOrigBytes, target, 5);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("CelHook: cannot read target (access violation)\n");
        return;
    }

    /* Build trampoline: [5 original bytes] + [JMP target+5] */
    memcpy(g_celHookTramp, g_celHookOrigBytes, 5);
    g_celHookTramp[5] = 0xE9;  /* JMP rel32 */
    DWORD jmpBack = (g_celHookTarget + 5) - ((DWORD)&g_celHookTramp[5] + 5);
    memcpy(&g_celHookTramp[6], &jmpBack, 4);

    /* Make trampoline executable */
    DWORD oldProt;
    if (!VirtualProtect(g_celHookTramp, sizeof(g_celHookTramp),
                        PAGE_EXECUTE_READWRITE, &oldProt)) {
        Log("CelHook: VirtualProtect on trampoline failed\n");
        return;
    }

    /* Patch target: 5-byte JMP to wrapper */
    BYTE patch[5];
    patch[0] = 0xE9;
    DWORD jmpToWrapper = (DWORD)CelLoadWrapper - (g_celHookTarget + 5);
    memcpy(&patch[1], &jmpToWrapper, 4);

    if (!VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &oldProt)) {
        Log("CelHook: VirtualProtect on target failed (err=%d)\n",
            (int)GetLastError());
        return;
    }
    memcpy(target, patch, 5);
    DWORD tmp;
    VirtualProtect(target, 5, oldProt, &tmp);
    FlushInstructionCache(GetCurrentProcess(), target, 5);

    g_celHookInstalled = 1;
    Log("CelHook: installed at %p (D2Win ordinal 10039). Tramp at %p. Log capped at 200 entries.\n",
        target, g_celHookTramp);
}
