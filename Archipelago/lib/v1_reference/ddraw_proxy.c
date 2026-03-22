/*
 * ddraw_proxy.c - Diablo II Archipelago Mod Loader
 *
 * This DLL masquerades as ddraw.dll. It:
 *   1. Loads ddraw_cnc.dll (the real cnc-ddraw graphics wrapper)
 *   2. Forwards all DirectDraw calls to it
 *   3. Loads our mod DLLs (PlugY.dll, D2Archipelago.dll)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

/* ========================================================================
 * Logging
 * ======================================================================== */
static FILE* g_proxyLog = NULL;

static void ProxyLog(const char* fmt, ...) {
    if (!g_proxyLog) {
        /* Use full path based on module location */
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        /* Replace exe name with log name */
        char* slash = strrchr(path, '\\');
        if (slash) {
            strcpy(slash + 1, "ddraw_proxy.log");
        } else {
            strcpy(path, "ddraw_proxy.log");
        }
        g_proxyLog = fopen(path, "w");
        if (!g_proxyLog) return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(g_proxyLog, fmt, args);
    va_end(args);
    fflush(g_proxyLog);
}

/* ========================================================================
 * cnc-ddraw forwarding - all 22 exports
 * ======================================================================== */

static HMODULE g_cncDDraw = NULL;

/* Function pointer storage for all cnc-ddraw exports */
static FARPROC g_fp[22] = {0};

/* Export indices */
#define FP_AcquireDDThreadLock       0
#define FP_CompleteCreateSysmemSurf  1
#define FP_D3DParseUnknownCommand    2
#define FP_DDEnableZoom              3
#define FP_DDGetProcAddress          4
#define FP_DDInternalLock            5
#define FP_DDInternalUnlock          6
#define FP_DDIsWindowed              7
#define FP_DirectDrawCreate          8
#define FP_DirectDrawCreateClipper   9
#define FP_DirectDrawCreateEx        10
#define FP_DirectDrawEnumerateA      11
#define FP_DirectDrawEnumerateExA    12
#define FP_DirectDrawEnumerateExW    13
#define FP_DirectDrawEnumerateW      14
#define FP_DirectInput8Create        15
#define FP_DirectInputCreateA        16
#define FP_DirectInputCreateEx       17
#define FP_DirectInputCreateW        18
#define FP_GameHandlesClose          19
#define FP_ReleaseDDThreadLock       20
#define FP_pvBmpBits                 21

static void LoadCncDDraw(void) {
    char path[MAX_PATH];
    char* slash;

    if (g_cncDDraw) return;

    /* Build full path to ddraw_cnc.dll in the same directory as this DLL */
    GetModuleFileNameA(NULL, path, MAX_PATH);
    slash = strrchr(path, '\\');
    if (slash) {
        strcpy(slash + 1, "ddraw_cnc.dll");
    } else {
        strcpy(path, "ddraw_cnc.dll");
    }

    ProxyLog("Loading cnc-ddraw from: %s\n", path);
    g_cncDDraw = LoadLibraryA(path);

    if (!g_cncDDraw) {
        ProxyLog("ERROR: Failed to load ddraw_cnc.dll (error %d)\n", GetLastError());
        /* Fall back to system ddraw */
        GetSystemDirectoryA(path, MAX_PATH);
        strcat(path, "\\ddraw.dll");
        ProxyLog("Falling back to system ddraw: %s\n", path);
        g_cncDDraw = LoadLibraryA(path);
        if (!g_cncDDraw) {
            ProxyLog("ERROR: System ddraw also failed (error %d)\n", GetLastError());
            return;
        }
    }

    ProxyLog("cnc-ddraw loaded at %p\n", g_cncDDraw);

    g_fp[FP_AcquireDDThreadLock]      = GetProcAddress(g_cncDDraw, "AcquireDDThreadLock");
    g_fp[FP_CompleteCreateSysmemSurf] = GetProcAddress(g_cncDDraw, "CompleteCreateSysmemSurface");
    g_fp[FP_D3DParseUnknownCommand]   = GetProcAddress(g_cncDDraw, "D3DParseUnknownCommand");
    g_fp[FP_DDEnableZoom]             = GetProcAddress(g_cncDDraw, "DDEnableZoom");
    g_fp[FP_DDGetProcAddress]         = GetProcAddress(g_cncDDraw, "DDGetProcAddress");
    g_fp[FP_DDInternalLock]           = GetProcAddress(g_cncDDraw, "DDInternalLock");
    g_fp[FP_DDInternalUnlock]         = GetProcAddress(g_cncDDraw, "DDInternalUnlock");
    g_fp[FP_DDIsWindowed]             = GetProcAddress(g_cncDDraw, "DDIsWindowed");
    g_fp[FP_DirectDrawCreate]         = GetProcAddress(g_cncDDraw, "DirectDrawCreate");
    g_fp[FP_DirectDrawCreateClipper]  = GetProcAddress(g_cncDDraw, "DirectDrawCreateClipper");
    g_fp[FP_DirectDrawCreateEx]       = GetProcAddress(g_cncDDraw, "DirectDrawCreateEx");
    g_fp[FP_DirectDrawEnumerateA]     = GetProcAddress(g_cncDDraw, "DirectDrawEnumerateA");
    g_fp[FP_DirectDrawEnumerateExA]   = GetProcAddress(g_cncDDraw, "DirectDrawEnumerateExA");
    g_fp[FP_DirectDrawEnumerateExW]   = GetProcAddress(g_cncDDraw, "DirectDrawEnumerateExW");
    g_fp[FP_DirectDrawEnumerateW]     = GetProcAddress(g_cncDDraw, "DirectDrawEnumerateW");
    g_fp[FP_DirectInput8Create]       = GetProcAddress(g_cncDDraw, "DirectInput8Create");
    g_fp[FP_DirectInputCreateA]       = GetProcAddress(g_cncDDraw, "DirectInputCreateA");
    g_fp[FP_DirectInputCreateEx]      = GetProcAddress(g_cncDDraw, "DirectInputCreateEx");
    g_fp[FP_DirectInputCreateW]       = GetProcAddress(g_cncDDraw, "DirectInputCreateW");
    g_fp[FP_GameHandlesClose]         = GetProcAddress(g_cncDDraw, "GameHandlesClose");
    g_fp[FP_ReleaseDDThreadLock]      = GetProcAddress(g_cncDDraw, "ReleaseDDThreadLock");
    g_fp[FP_pvBmpBits]               = GetProcAddress(g_cncDDraw, "pvBmpBits");

    ProxyLog("DirectDrawCreate at %p\n", g_fp[FP_DirectDrawCreate]);
    ProxyLog("DirectDrawCreateEx at %p\n", g_fp[FP_DirectDrawCreateEx]);
}

/* ========================================================================
 * Mod loading
 * ======================================================================== */

static HMODULE g_plugy = NULL;
static HMODULE g_newtxt = NULL;
static HMODULE g_d2arch = NULL;
static BOOL g_modsLoaded = FALSE;

static void LoadOneDll(const char* dir, const char* name, HMODULE* out) {
    char dllPath[MAX_PATH];
    strcpy(dllPath, dir);
    strcat(dllPath, name);
    *out = LoadLibraryA(dllPath);
    ProxyLog("  %s: %s (handle=%p, err=%lu)\n",
        name, *out ? "LOADED" : "FAILED", *out, *out ? 0 : GetLastError());
}

static void LoadMods(void) {
    char path[MAX_PATH];
    char* slash;

    if (g_modsLoaded) return;
    g_modsLoaded = TRUE;

    GetModuleFileNameA(NULL, path, MAX_PATH);
    slash = strrchr(path, '\\');
    if (slash) slash[1] = '\0';

    ProxyLog("Loading mods from: %s\n", path);

    /* Load each DLL directly - no D2Mod.dll dependency.
     * Order matters: PlugY first (stash hooks), then game patches, then our mod. */
    LoadOneDll(path, "PlugY.dll",         &g_plugy);
    LoadOneDll(path, "NewTxt.dll",        &g_newtxt);
    LoadOneDll(path, "D2Archipelago.dll", &g_d2arch);
}

/* ========================================================================
 * Naked asm trampolines for forwarding
 * ======================================================================== */

#define MAKE_TRAMPOLINE(name, index)                     \
    __declspec(naked) void __stdcall proxy_##name() {    \
        __asm { jmp dword ptr [g_fp + index * 4] }       \
    }

MAKE_TRAMPOLINE(AcquireDDThreadLock,      FP_AcquireDDThreadLock)
MAKE_TRAMPOLINE(CompleteCreateSysmemSurface, FP_CompleteCreateSysmemSurf)
MAKE_TRAMPOLINE(D3DParseUnknownCommand,   FP_D3DParseUnknownCommand)
MAKE_TRAMPOLINE(DDEnableZoom,             FP_DDEnableZoom)
MAKE_TRAMPOLINE(DDGetProcAddress,         FP_DDGetProcAddress)
MAKE_TRAMPOLINE(DDInternalLock,           FP_DDInternalLock)
MAKE_TRAMPOLINE(DDInternalUnlock,         FP_DDInternalUnlock)
MAKE_TRAMPOLINE(DDIsWindowed,             FP_DDIsWindowed)
MAKE_TRAMPOLINE(DirectDrawCreateClipper,  FP_DirectDrawCreateClipper)
MAKE_TRAMPOLINE(DirectDrawCreateEx,       FP_DirectDrawCreateEx)
MAKE_TRAMPOLINE(DirectDrawEnumerateA,     FP_DirectDrawEnumerateA)
MAKE_TRAMPOLINE(DirectDrawEnumerateExA,   FP_DirectDrawEnumerateExA)
MAKE_TRAMPOLINE(DirectDrawEnumerateExW,   FP_DirectDrawEnumerateExW)
MAKE_TRAMPOLINE(DirectDrawEnumerateW,     FP_DirectDrawEnumerateW)
MAKE_TRAMPOLINE(DirectInput8Create,       FP_DirectInput8Create)
MAKE_TRAMPOLINE(DirectInputCreateA,       FP_DirectInputCreateA)
MAKE_TRAMPOLINE(DirectInputCreateEx,      FP_DirectInputCreateEx)
MAKE_TRAMPOLINE(DirectInputCreateW,       FP_DirectInputCreateW)
MAKE_TRAMPOLINE(GameHandlesClose,         FP_GameHandlesClose)
MAKE_TRAMPOLINE(ReleaseDDThreadLock,      FP_ReleaseDDThreadLock)
MAKE_TRAMPOLINE(pvBmpBits,               FP_pvBmpBits)

/* DirectDrawCreate - loads mods on first call */
__declspec(naked) void __stdcall proxy_DirectDrawCreate() {
    __asm {
        pushad
        call LoadMods
        popad
        jmp dword ptr [g_fp + FP_DirectDrawCreate * 4]
    }
}

/* ========================================================================
 * DllMain
 * ======================================================================== */

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        ProxyLog("=== ddraw_proxy.dll loaded ===\n");
        ProxyLog("Process: ");
        {
            char exePath[MAX_PATH];
            GetModuleFileNameA(NULL, exePath, MAX_PATH);
            ProxyLog("%s\n", exePath);
        }

        LoadCncDDraw();

        /* Load mods */
        ProxyLog("Loading mods...\n");
        LoadMods();
    }
    else if (reason == DLL_PROCESS_DETACH) {
        ProxyLog("=== ddraw_proxy.dll unloading ===\n");

        if (g_d2arch)   FreeLibrary(g_d2arch);
        if (g_newtxt)   FreeLibrary(g_newtxt);
        if (g_plugy)    FreeLibrary(g_plugy);
        if (g_cncDDraw) FreeLibrary(g_cncDDraw);

        if (g_proxyLog) fclose(g_proxyLog);
    }

    return TRUE;
}
