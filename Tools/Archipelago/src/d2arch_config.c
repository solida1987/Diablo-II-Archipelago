/* Forward declarations for settings used in LoadConfig */
static int  g_classFilter;
static int  g_clsEnabled[7];
static int  g_iPlayAssassin;

/* ================================================================
 * CONFIG SYSTEM — reads paths from d2arch.ini
 * ================================================================ */
static char g_cfgSavePath[MAX_PATH] = {0};  /* Save dir from config */
static char g_cfgGamePath[MAX_PATH] = {0};  /* Original D2 install from config */
static char g_cfgServerIP[64] = "localhost:38281";
static char g_cfgSlotName[32] = {0};
static char g_cfgPassword[32] = {0};

/* Quickcast system removed — caused crashes, needs proper D2Client research before reimplementation */

/* Forward declaration — Log is defined after GetSaveDir but needed by it */
static void Log(const char* fmt, ...);

/* Helper: check if a directory exists */
static BOOL DirExists(const char* path) {
    if (!path || !path[0]) return FALSE;
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

/* Helper: ensure trailing backslash on a path */
static void EnsureTrailingSlash(char* path, int maxLen) {
    if (!path[0]) return;
    int slen = (int)strlen(path);
    if (slen > 0 && slen < maxLen - 1 && path[slen-1] != '\\') {
        path[slen] = '\\'; path[slen+1] = 0;
    }
}

static void LoadConfig(void) {
    char iniPath[MAX_PATH];
    GetModuleFileNameA(NULL, iniPath, MAX_PATH);
    char* sl = strrchr(iniPath, '\\');
    if (sl) *(sl + 1) = 0;
    strcat(iniPath, "Archipelago\\d2arch.ini");

    GetPrivateProfileStringA("paths", "SavePath", "", g_cfgSavePath, MAX_PATH, iniPath);
    GetPrivateProfileStringA("paths", "GamePath", "", g_cfgGamePath, MAX_PATH, iniPath);
    GetPrivateProfileStringA("ap", "ServerIP", "localhost:38281", g_cfgServerIP, 64, iniPath);
    GetPrivateProfileStringA("ap", "SlotName", "", g_cfgSlotName, 32, iniPath);
    GetPrivateProfileStringA("ap", "Password", "", g_cfgPassword, 32, iniPath);

    /* Class filter settings */
    g_classFilter    = GetPrivateProfileIntA("settings", "ClassFilter", 0, iniPath);
    g_clsEnabled[0]  = GetPrivateProfileIntA("settings", "ClsAmazon", 1, iniPath);
    g_clsEnabled[1]  = GetPrivateProfileIntA("settings", "ClsSorceress", 1, iniPath);
    g_clsEnabled[2]  = GetPrivateProfileIntA("settings", "ClsNecromancer", 1, iniPath);
    g_clsEnabled[3]  = GetPrivateProfileIntA("settings", "ClsPaladin", 1, iniPath);
    g_clsEnabled[4]  = GetPrivateProfileIntA("settings", "ClsBarbarian", 1, iniPath);
    g_clsEnabled[5]  = GetPrivateProfileIntA("settings", "ClsDruid", 1, iniPath);
    g_clsEnabled[6]  = GetPrivateProfileIntA("settings", "ClsAssassin", 1, iniPath);
    g_iPlayAssassin  = GetPrivateProfileIntA("settings", "IPlayAssassin", 0, iniPath);

    /* Ensure trailing backslash on paths */
    EnsureTrailingSlash(g_cfgSavePath, MAX_PATH);
    EnsureTrailingSlash(g_cfgGamePath, MAX_PATH);
}

/* Helper: is a directory path under (or equal to) the directory containing Game.exe?
 * Used to accept the registry path only when it points at the same install we're running
 * from — otherwise a stale HKCU entry from a previous retail D2 install would silently
 * redirect saves to C:\Program Files\... or similar. */
static BOOL IsPathUnderExeDir(const char* path) {
    if (!path || !path[0]) return FALSE;
    char exeDir[MAX_PATH];
    GetModuleFileNameA(NULL, exeDir, MAX_PATH);
    char* sl = strrchr(exeDir, '\\');
    if (sl) *(sl + 1) = 0;
    size_t exeLen = strlen(exeDir);
    if (exeLen == 0) return FALSE;
    return (_strnicmp(path, exeDir, (int)exeLen) == 0);
}

/* Helper: directory contains at least one .d2s file. */
static BOOL DirHasD2sFile(const char* dir) {
    if (!dir || !dir[0]) return FALSE;
    char pat[MAX_PATH];
    snprintf(pat, sizeof(pat), "%s*.d2s", dir);
    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(pat, &ffd);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    FindClose(h);
    return TRUE;
}

/* Helper: directory is writable (create/delete a temp test file). */
static BOOL DirIsWritable(const char* dir) {
    if (!dir || !dir[0]) return FALSE;
    char testPath[MAX_PATH];
    snprintf(testPath, sizeof(testPath), "%sd2arch_wtest_%lu.tmp", dir, GetCurrentProcessId());
    HANDLE h = CreateFileA(testPath, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle(h);
    return TRUE;
}

/* Validate a candidate save directory.
 * Must exist, contain at least one .d2s file OR live under the Game.exe folder,
 * and be writable. Path must end in a backslash. */
static BOOL ValidateSaveDir(const char* dir) {
    if (!dir || !dir[0]) return FALSE;
    if (GetFileAttributesA(dir) == INVALID_FILE_ATTRIBUTES) return FALSE;
    if (!DirHasD2sFile(dir) && !IsPathUnderExeDir(dir)) return FALSE;
    if (!DirIsWritable(dir)) return FALSE;
    return TRUE;
}

/* Resolve save directory: config → registry → Game.exe\save\ → Game.exe dir.
 * Every candidate is validated (exists, writable, has .d2s evidence or is under
 * the exe folder) before being accepted. Falls through to the next candidate
 * when validation fails rather than silently creating directories. */
static void GetSaveDir(char* out, int outSize) {
    out[0] = 0;
    const char* source = NULL;

    /* Priority 1: d2arch.ini config */
    if (g_cfgSavePath[0]) {
        char cand[MAX_PATH];
        strncpy(cand, g_cfgSavePath, sizeof(cand) - 1);
        cand[sizeof(cand) - 1] = 0;
        EnsureTrailingSlash(cand, sizeof(cand));
        if (ValidateSaveDir(cand)) {
            strncpy(out, cand, outSize - 1);
            out[outSize - 1] = 0;
            source = "INI";
        } else {
            Log("GetSaveDir: INI path rejected (missing/empty/readonly): %s\n", cand);
        }
    }

    /* Priority 2: Windows Registry (HKCU Blizzard Diablo II Save Path).
     * No longer creates the directory if absent — a stale HKCU entry from a
     * prior retail install would otherwise redirect saves outside our install. */
    if (!out[0]) {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
                "SOFTWARE\\Blizzard Entertainment\\Diablo II", 0,
                KEY_READ, &hKey) == ERROR_SUCCESS) {
            char cand[MAX_PATH] = {0};
            /* Leave one byte of headroom so we can always null-terminate even
             * if the value fills the buffer without a trailing NUL. */
            DWORD sz = (DWORD)sizeof(cand) - 1;
            DWORD regType = 0;
            if (RegQueryValueExA(hKey, "Save Path", NULL, &regType,
                                 (LPBYTE)cand, &sz) == ERROR_SUCCESS && cand[0]) {
                cand[sizeof(cand) - 1] = 0;
                EnsureTrailingSlash(cand, sizeof(cand));
                if (ValidateSaveDir(cand)) {
                    strncpy(out, cand, outSize - 1);
                    out[outSize - 1] = 0;
                    source = "Registry";
                } else {
                    Log("GetSaveDir: Registry path rejected (missing/empty/readonly/outside-exe): %s\n", cand);
                }
            }
            RegCloseKey(hKey);
        }
    }

    /* Priority 3: Game.exe directory + "save\" subfolder */
    char exeDir[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exeDir, MAX_PATH);
    {
        char* sl = strrchr(exeDir, '\\');
        if (sl) *(sl + 1) = 0;
    }
    if (!out[0]) {
        char cand[MAX_PATH];
        snprintf(cand, sizeof(cand), "%ssave\\", exeDir);
        if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES && DirIsWritable(cand)) {
            strncpy(out, cand, outSize - 1);
            out[outSize - 1] = 0;
            source = "exeDir/save";
        }
    }

    /* Priority 4: Game.exe directory itself */
    if (!out[0]) {
        strncpy(out, exeDir, outSize - 1);
        out[outSize - 1] = 0;
        EnsureTrailingSlash(out, outSize);
        source = "exeDir fallback";
    }

    Log("GetSaveDir: resolved path '%s' via %s\n", out, source ? source : "(none)");
}

/* ================================================================
 * LOGGING
 * ================================================================ */
static FILE* g_logFile = NULL;

static void Log(const char* fmt, ...) {
    if (!g_logFile) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_logFile, fmt, ap);
    va_end(ap);
    fflush(g_logFile);
}

/* ================================================================
 * FORWARD DECLARATIONS for functions defined in d2arch_save.c
 * These exist here (the first #include in d2arch.c) so consumers in
 * d2arch_gameloop.c / d2arch_editor.c / d2arch_drawall.c can call
 * them even though they come earlier in the include order.
 * ================================================================ */
#include <stddef.h>  /* for size_t */
static void RequestD2SReset(void);
static void GrantPendingReinvestAsFreePool(void);
static int  BeginReinvestConsume(char* outConsumingPath, size_t pathSize);
static void EndReinvestConsume(const char* consumingPath);

/* ================================================================
 * 1.9.0 — Per-character save directory helper
 *
 * All per-character files (state, slots, reinvest, skill caches,
 * collections, stats, stash, spoiler, etc.) live alongside D2's
 * native .d2s files in Game/Save/ instead of being mixed into the
 * shared Game/Archipelago/ folder. This helper is the single source
 * of truth for "where do per-char files go" so future moves are a
 * one-line change.
 *
 * Shared/global files (ap_settings.dat, ap_command.dat, ap_unlocks.dat,
 * shared_stash*.dat, etc.) keep using GetArchDir() and stay in
 * Game/Archipelago/.
 * ================================================================ */
/* GetArchDir lives in d2arch_helpers.c (included AFTER this file in the
 * unity build) — forward-declare so the fallback below resolves at link. */
static void GetArchDir(char* buf, int bufSize);

static void GetCharFileDir(char* out, int outSize) {
    /* GetSaveDir resolves to Game/Save/ via the cached d2arch.ini /
     * registry / fallback chain. Falls back to GetArchDir() if the
     * save dir lookup somehow returns empty (e.g. brand-new install
     * with no .d2s files yet — Validate would reject); keeps writes
     * succeeding even in that degenerate case. */
    if (out && outSize > 0) {
        out[0] = 0;
        GetSaveDir(out, outSize);
        if (!out[0]) {
            GetArchDir(out, outSize);
        }
    }
}

