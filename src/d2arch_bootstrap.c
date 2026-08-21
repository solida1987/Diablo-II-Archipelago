/* D2Arch Bootstrap — produces Game/D2Arch_Launcher.exe Silent bootstrap invoked by the C# launcher (launcher/Diablo II Archipelago.exe): 1. */
#include <windows.h>
#include <stdio.h>
#include <tlhelp32.h>
#include "d2arch_version.h"

/* 1.9.12 helpers for B18 fix */

/* Print a human-readable interpretation of a Windows error code. */
static const char* InjectErrorHint(DWORD err) {
    switch (err) {
        case 5:    /* ERROR_ACCESS_DENIED */
            return "Access denied — almost always antivirus blocking. "
                   "Add Game.exe AND D2Arch_Launcher.exe to your AV's exception list. "
                   "On Windows Defender: Settings -> Virus & threat protection -> "
                   "Manage settings -> Exclusions -> Add or remove exclusions -> "
                   "Add an exclusion (File) -> select both .exe files.";
        case 87:   /* ERROR_INVALID_PARAMETER */
            return "Invalid parameter — typically antivirus interfering with memory "
                   "allocation in the target process, or a stale Game.exe from a "
                   "previous failed launch. Try the AV exclusion fix (see error 5) "
                   "and also kill any leftover Game.exe in Task Manager before retrying.";
        case 8:    /* ERROR_NOT_ENOUGH_MEMORY */
            return "Out of memory — close other applications and retry.";
        case 1450: /* ERROR_NO_SYSTEM_RESOURCES */
            return "System resources exhausted — restart your computer.";
        case 998:  /* ERROR_NOACCESS */
            return "Invalid memory access — typically antivirus interference. "
                   "Add Game.exe and D2Arch_Launcher.exe to AV exceptions.";
        default:
            return "Unknown error. Most common causes: antivirus blocking "
                   "(add Game.exe + D2Arch_Launcher.exe to exceptions) or a "
                   "stale Game.exe process (check Task Manager).";
    }
}

/* Find Game.exe process ID — first match wins */
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

/* kill ALL stale Game.exe instances before we start D2.Detours. */
static int KillStaleGameProcesses(void) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe = {0};
    pe.dwSize = sizeof(pe);
    int killed = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "Game.exe") == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    if (TerminateProcess(hProc, 1)) {
                        killed++;
                        printf("  Killed stale Game.exe (PID %lu)\n", pe.th32ProcessID);
                    }
                    CloseHandle(hProc);
                }
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    if (killed > 0) {
        /* Give Windows a moment to release file handles + descriptors. */
        Sleep(500);
    }
    return killed;
}

/* InjectDLL with per-step error reporting. */
static int InjectDLL(DWORD pid, const char* dllPath, DWORD* pOutErr) {
    *pOutErr = 0;

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) {
        *pOutErr = GetLastError();
        return 1;
    }

    SIZE_T pathLen = strlen(dllPath) + 1;
    void* remoteMem = VirtualAllocEx(hProc, NULL, pathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) {
        *pOutErr = GetLastError();
        CloseHandle(hProc);
        return 2;
    }

    /* WriteProcessMemory return value check. */
    SIZE_T written = 0;
    BOOL wpmOk = WriteProcessMemory(hProc, remoteMem, dllPath, pathLen, &written);
    if (!wpmOk || written != pathLen) {
        *pOutErr = GetLastError();
        VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return 3;
    }

    FARPROC pLoadLib = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    if (!pLoadLib) {
        *pOutErr = GetLastError();
        VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return 4;
    }

    HANDLE hThread = CreateRemoteThread(hProc, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLib, remoteMem, 0, NULL);
    if (!hThread) {
        *pOutErr = GetLastError();
        VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return 5;
    }

    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProc);
    return 0;
}

int main(int argc, char* argv[]) {
    /* Build DLL path */
    char dllPath[MAX_PATH];
    GetModuleFileNameA(NULL, dllPath, MAX_PATH);
    char* sl = strrchr(dllPath, '\\');
    if (sl) strcpy(sl + 1, "D2Archipelago.dll");

    /* pin D2's save path to <exe_dir>\save\ in HKCU before Game.exe starts. */
    {
        char saveDir[MAX_PATH];
        /* the Multiworld-Launcher sets D2ARCH_SAVE_PATH to a per-seed folder (Game\save\seed_<id>\) so each standalone seed keeps its own isolated character list — D2's character-select reads HKCU Save Path, so pointing it at the seed folder shows only that seed's characters. */
        DWORD envLen = GetEnvironmentVariableA("D2ARCH_SAVE_PATH", saveDir, MAX_PATH);
        if (envLen == 0 || envLen >= MAX_PATH) {
            GetModuleFileNameA(NULL, saveDir, MAX_PATH);
            char* lastSlash = strrchr(saveDir, '\\');
            if (lastSlash) strcpy(lastSlash + 1, "save\\");
        } else {
            size_t sl = strlen(saveDir);     /* normalize trailing backslash */
            if (sl > 0 && saveDir[sl - 1] != '\\' && sl + 1 < MAX_PATH) {
                saveDir[sl] = '\\'; saveDir[sl + 1] = 0;
            }
        }
        CreateDirectoryA(saveDir, NULL); /* ensure it exists */

        HKEY hKey;
        if (RegCreateKeyExA(HKEY_CURRENT_USER,
                "SOFTWARE\\Blizzard Entertainment\\Diablo II",
                0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                NULL, &hKey, NULL) == ERROR_SUCCESS) {
            DWORD len = (DWORD)(strlen(saveDir) + 1);
            /* Write both legacy ("Save Path") and 1.13+-era ("NewSavePath") values to cover every D2 build a user might have lingering registry data from. */
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

    /* kill stale Game.exe BEFORE launching D2.Detours. */
    {
        int killed = KillStaleGameProcesses();
        if (killed > 0) {
            printf("Cleaned up %d stale Game.exe instance(s) from previous session\n", killed);
        }
    }

    /* Set DIABLO2_PATCH env var for D2.Detours */
    char patchPath[MAX_PATH];
    GetModuleFileNameA(NULL, patchPath, MAX_PATH);
    sl = strrchr(patchPath, '\\');
    if (sl) strcpy(sl + 1, "patch");
    SetEnvironmentVariableA("DIABLO2_PATCH", patchPath);

    /* remove a leftover D2MOO D2Common before D2.Detours scans the patch folder. */
    {
        char staleCommon[MAX_PATH];
        _snprintf(staleCommon, sizeof(staleCommon), "%s\\D2Common.dll", patchPath);
        staleCommon[sizeof(staleCommon) - 1] = 0;
        if (GetFileAttributesA(staleCommon) != INVALID_FILE_ATTRIBUTES) {
            SetFileAttributesA(staleCommon, FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileA(staleCommon))
                printf("Removed stale patch\\D2Common.dll (blocked monster spawns)\n");
            else
                printf("WARNING: could not remove patch\\D2Common.dll (error %lu) -"
                       " monsters may be sparse\n", GetLastError());
        }
    }

    /* Launch via D2.Detours */
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        printf("ERROR: Could not start D2.DetoursLauncher (error %lu)\n", err);
        printf("\nHint: %s\n", InjectErrorHint(err));
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
        printf("\nHint: D2.DetoursLauncher may have been blocked by antivirus, "
               "OR Game.exe crashed during init. Check Task Manager for any "
               "leftover processes and add Game.exe + D2Arch_Launcher.exe + "
               "D2.DetoursLauncher.exe to your antivirus exception list.\n");
        return 1;
    }

    /* Wait a bit for Game.exe to initialize DLLs */
    Sleep(1000);

    /* retry with backoff. A single inject failure is often transient (AV scanning the DLL, target process not yet stable). Three attempts spread 500ms apart catch most transient cases without making the user wait long on a persistent failure. */
    DWORD lastErr = 0;
    int   lastStep = 0;
    BOOL  injected = FALSE;
    for (int attempt = 1; attempt <= 3 && !injected; attempt++) {
        DWORD err = 0;
        int step = InjectDLL(pid, dllPath, &err);
        if (step == 0) {
            injected = TRUE;
            printf("Injected %s into Game.exe (PID %lu, attempt %d)\n",
                   dllPath, pid, attempt);
            break;
        }
        lastErr  = err;
        lastStep = step;
        const char* stepName = "?";
        switch (step) {
            case 1: stepName = "OpenProcess";        break;
            case 2: stepName = "VirtualAllocEx";     break;
            case 3: stepName = "WriteProcessMemory"; break;
            case 4: stepName = "GetProcAddress";     break;
            case 5: stepName = "CreateRemoteThread"; break;
        }
        printf("  Attempt %d/3 failed at step %s (Windows error %lu)\n",
               attempt, stepName, err);
        if (attempt < 3) Sleep(500);
    }

    if (!injected) {
        const char* stepName = "?";
        switch (lastStep) {
            case 1: stepName = "OpenProcess";        break;
            case 2: stepName = "VirtualAllocEx";     break;
            case 3: stepName = "WriteProcessMemory"; break;
            case 4: stepName = "GetProcAddress";     break;
            case 5: stepName = "CreateRemoteThread"; break;
        }
        printf("\n");
        printf("ERROR: Failed to inject D2Archipelago.dll into Game.exe after 3 attempts.\n");
        printf("  Last failing step:  %s\n", stepName);
        printf("  Windows error code: %lu\n", lastErr);
        printf("\n");
        printf("Diagnosis: %s\n", InjectErrorHint(lastErr));
        printf("\n");
        printf("If the AV exception fix does not help, please post the EXACT output of\n");
        printf("this window in the project Discord so we can help diagnose further.\n");
        return 1;
    }

    return 0;
}
