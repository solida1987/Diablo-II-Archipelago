/* D2Arch Bootstrap — produces Game/D2Arch_Launcher.exe
 *
 * Silent bootstrap invoked by the C# launcher (launcher/Diablo II Archipelago.exe):
 *   1. Starts D2.DetoursLauncher.exe Game.exe -- <args>  (D2.Detours hooks)
 *   2. Waits for Game.exe process to appear (up to 15s)
 *   3. Calls LoadLibraryA via CreateRemoteThread to inject D2Archipelago.dll
 *      into the Game.exe process
 *
 * Has NO UI — runs with hidden window when spawned by the C# launcher.
 *
 * Build: _build_bootstrap.bat  →  Game/D2Arch_Launcher.exe
 *
 * History: source was formerly at Tools/_backup/Archipelago_backup_160/src/
 * injector.c (lost in the 1.6.0 → 1.7.0 src refactor). Promoted to active
 * src/ in 1.8.0 cleanup so the binary is never "orphaned" again.
 */
#include <windows.h>
#include <stdio.h>
#include <tlhelp32.h>
#include "d2arch_version.h"

/* Find Game.exe process ID */
static DWORD FindGamePID(void) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe = {0};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "Game.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

/* Inject DLL into a running process */
static BOOL InjectDLL(DWORD pid, const char* dllPath) {
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) return FALSE;

    SIZE_T pathLen = strlen(dllPath) + 1;
    void* remoteMem = VirtualAllocEx(hProc, NULL, pathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) { CloseHandle(hProc); return FALSE; }

    WriteProcessMemory(hProc, remoteMem, dllPath, pathLen, NULL);
    FARPROC pLoadLib = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    HANDLE hThread = CreateRemoteThread(hProc, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLib, remoteMem, 0, NULL);
    if (!hThread) { VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE); CloseHandle(hProc); return FALSE; }

    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProc);
    return TRUE;
}

int main(int argc, char* argv[]) {
    /* Build DLL path */
    char dllPath[MAX_PATH];
    GetModuleFileNameA(NULL, dllPath, MAX_PATH);
    char* sl = strrchr(dllPath, '\\');
    if (sl) strcpy(sl + 1, "D2Archipelago.dll");

    /* 1.9.1 fix — pin D2's save path to <exe_dir>\save\ in HKCU before
     * Game.exe starts. D2 1.10f reads the save path from
     * HKCU\Software\Blizzard Entertainment\Diablo II at startup; without
     * this, a stale registry entry from any prior install (the dev's
     * machine, a different mod folder, etc.) would silently redirect
     * character .d2s files outside this install. Symptom: a fresh install
     * in folder A creates characters that show up in folder B.
     *
     * Per-install pin written every launch — cheap, idempotent, and means
     * each install owns its own characters even when several installs
     * share a Windows user account. */
    {
        char saveDir[MAX_PATH];
        GetModuleFileNameA(NULL, saveDir, MAX_PATH);
        char* lastSlash = strrchr(saveDir, '\\');
        if (lastSlash) strcpy(lastSlash + 1, "save\\");
        CreateDirectoryA(saveDir, NULL); /* ensure it exists */

        HKEY hKey;
        if (RegCreateKeyExA(HKEY_CURRENT_USER,
                "SOFTWARE\\Blizzard Entertainment\\Diablo II",
                0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                NULL, &hKey, NULL) == ERROR_SUCCESS) {
            DWORD len = (DWORD)(strlen(saveDir) + 1);
            /* Write both legacy ("Save Path") and 1.13+-era
             * ("NewSavePath") values to cover every D2 build a user
             * might have lingering registry data from. */
            RegSetValueExA(hKey, "Save Path",    0, REG_SZ, (const BYTE*)saveDir, len);
            RegSetValueExA(hKey, "NewSavePath",  0, REG_SZ, (const BYTE*)saveDir, len);
            RegCloseKey(hKey);
            printf("Save path pinned to: %s\n", saveDir);
        }
    }

    /* Build D2.Detours command line */
    char cmdLine[1024];
    sprintf(cmdLine, "D2.DetoursLauncher.exe Game.exe --");
    for (int i = 1; i < argc; i++) {
        strcat(cmdLine, " ");
        strcat(cmdLine, argv[i]);
    }

    printf("============================================\n");
    printf("  Diablo II Archipelago - %s\n", D2ARCH_VERSION_DISPLAY);
    printf("============================================\n\n");

    /* Set DIABLO2_PATCH env var for D2.Detours */
    char patchPath[MAX_PATH];
    GetModuleFileNameA(NULL, patchPath, MAX_PATH);
    sl = strrchr(patchPath, '\\');
    if (sl) strcpy(sl + 1, "patch");
    SetEnvironmentVariableA("DIABLO2_PATCH", patchPath);

    /* Launch via D2.Detours */
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("ERROR: Could not start D2.DetoursLauncher (error %lu)\n", GetLastError());
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    printf("D2.Detours started. Waiting for Game.exe...\n");

    /* Wait for Game.exe to appear */
    DWORD pid = 0;
    for (int i = 0; i < 30; i++) { /* 15 seconds max */
        Sleep(500);
        pid = FindGamePID();
        if (pid) break;
    }

    if (!pid) {
        printf("ERROR: Game.exe not found after 15 seconds\n");
        return 1;
    }

    /* Wait a bit for Game.exe to initialize DLLs */
    Sleep(1000);

    /* Inject D2Archipelago.dll */
    if (InjectDLL(pid, dllPath)) {
        printf("Injected %s into Game.exe (PID %lu)\n", dllPath, pid);
    } else {
        printf("ERROR: Failed to inject (error %lu)\n", GetLastError());
    }

    return 0;
}
