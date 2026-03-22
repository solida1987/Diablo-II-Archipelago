/*
 * Diablo II Archipelago - Installer
 * Win32 GUI installer with browse dialog, progress bar, and desktop shortcut.
 */
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

/* ================================================================
 * GLOBALS
 * ================================================================ */
static HINSTANCE g_hInst;
static HWND g_hWnd;
static HWND g_hPath, g_hBrowse, g_hNext, g_hBack, g_hCancel;
static HWND g_hProgress, g_hStatus, g_hShortcut;
static HWND g_hFinishLabel, g_hPlayBtn;
static char g_d2Path[MAX_PATH] = "";
static char g_modDir[MAX_PATH] = "";
static char g_saveDir[MAX_PATH] = "";
static int g_page = 0; /* 0=welcome, 1=browse, 2=installing, 3=done */

/* ================================================================
 * HELPERS
 * ================================================================ */
static void GetModDir(void) {
    GetModuleFileNameA(NULL, g_modDir, MAX_PATH);
    char *s = strrchr(g_modDir, '\\');
    if (s) *(s + 1) = 0;
}

static void SetStatus(const char *msg) {
    SetWindowTextA(g_hStatus, msg);
    UpdateWindow(g_hStatus);
}

static void SetProgress(int pct) {
    SendMessage(g_hProgress, PBM_SETPOS, pct, 0);
    UpdateWindow(g_hProgress);
}

static int CopyFiles(const char *srcPattern, const char *destDir) {
    WIN32_FIND_DATAA fd;
    char srcDir[MAX_PATH];
    strncpy(srcDir, srcPattern, MAX_PATH);
    char *slash = strrchr(srcDir, '\\');
    if (slash) *(slash + 1) = 0;

    HANDLE h = FindFirstFileA(srcPattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int count = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char src[MAX_PATH], dst[MAX_PATH];
        snprintf(src, MAX_PATH, "%s%s", srcDir, fd.cFileName);
        snprintf(dst, MAX_PATH, "%s%s", destDir, fd.cFileName);
        if (CopyFileA(src, dst, FALSE)) count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return count;
}

static void CopyDir(const char *srcDir, const char *dstDir) {
    CreateDirectoryA(dstDir, NULL);
    char pattern[MAX_PATH];
    snprintf(pattern, MAX_PATH, "%s*", srcDir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char src[MAX_PATH], dst[MAX_PATH];
        snprintf(src, MAX_PATH, "%s%s", srcDir, fd.cFileName);
        snprintf(dst, MAX_PATH, "%s%s", dstDir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char s2[MAX_PATH], d2[MAX_PATH];
            snprintf(s2, MAX_PATH, "%s%s\\", srcDir, fd.cFileName);
            snprintf(d2, MAX_PATH, "%s%s\\", dstDir, fd.cFileName);
            CopyDir(s2, d2);
        } else {
            CopyFileA(src, dst, FALSE);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static BOOL FileExists(const char *path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void CreateDesktopShortcut(void) {
    CoInitialize(NULL);
    IShellLinkA *psl;
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IShellLinkA, (void**)&psl))) {
        /* Point shortcut to D2Launcher.exe (has embedded icon) instead of bat */
        char launcherPath[MAX_PATH];
        snprintf(launcherPath, MAX_PATH, "%sArchipelago\\build\\D2Launcher.exe", g_modDir);
        psl->lpVtbl->SetPath(psl, launcherPath);
        psl->lpVtbl->SetWorkingDirectory(psl, g_modDir);
        psl->lpVtbl->SetDescription(psl, "Launch Diablo II Archipelago");
        /* Set custom icon */
        char icoPath[MAX_PATH];
        snprintf(icoPath, MAX_PATH, "%sArchipelago\\src\\app.ico", g_modDir);
        psl->lpVtbl->SetIconLocation(psl, icoPath, 0);

        IPersistFile *ppf;
        if (SUCCEEDED(psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (void**)&ppf))) {
            char desktop[MAX_PATH];
            SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop);
            char lnkPath[MAX_PATH];
            snprintf(lnkPath, MAX_PATH, "%s\\Diablo II Archipelago.lnk", desktop);
            WCHAR wPath[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, lnkPath, -1, wPath, MAX_PATH);
            ppf->lpVtbl->Save(ppf, wPath, TRUE);
            ppf->lpVtbl->Release(ppf);
        }
        psl->lpVtbl->Release(psl);
    }
    CoUninitialize();
}

/* ================================================================
 * INSTALLATION THREAD
 * ================================================================ */
static DWORD WINAPI InstallThread(LPVOID lp) {
    (void)lp;
    char buf[MAX_PATH];
    int step = 0, total = 10; /* steps: gameDLLs, MPQs, 1.10f, PlugY, mods, gfx+singling, archMod, MPQfix, data, launchScript */

    /* Step 1: Copy game DLLs */
    step++; SetProgress(step * 100 / total);
    SetStatus("Copying game files...");
    snprintf(buf, MAX_PATH, "%s\\*.dll", g_d2Path);
    CopyFiles(buf, g_modDir);
    snprintf(buf, MAX_PATH, "%s\\Game.exe", g_d2Path);
    if (FileExists(buf)) CopyFileA(buf, (snprintf(buf, MAX_PATH, "%sGame.exe", g_modDir), buf), FALSE);
    snprintf(buf, MAX_PATH, "%s\\Game.exe", g_d2Path);
    CopyFileA(buf, (snprintf(buf, MAX_PATH, "%sGame.exe", g_modDir), buf), FALSE);
    snprintf(buf, MAX_PATH, "%s\\Diablo II.exe", g_d2Path);
    if (FileExists(buf)) { char d[MAX_PATH]; snprintf(d, MAX_PATH, "%sDiablo II.exe", g_modDir); CopyFileA(buf, d, FALSE); }
    snprintf(buf, MAX_PATH, "%s\\D2VidTst.exe", g_d2Path);
    if (FileExists(buf)) { char d[MAX_PATH]; snprintf(d, MAX_PATH, "%sD2VidTst.exe", g_modDir); CopyFileA(buf, d, FALSE); }

    /* Step 2: Copy MPQs */
    step++; SetProgress(step * 100 / total);
    SetStatus("Copying MPQ archives (this may take a while)...");
    snprintf(buf, MAX_PATH, "%s\\*.mpq", g_d2Path);
    CopyFiles(buf, g_modDir);
    snprintf(buf, MAX_PATH, "%s\\D2.LNG", g_d2Path);
    if (FileExists(buf)) { char d[MAX_PATH]; snprintf(d, MAX_PATH, "%sD2.LNG", g_modDir); CopyFileA(buf, d, FALSE); }

    /* Step 3: Downgrade to 1.10f */
    step++; SetProgress(step * 100 / total);
    SetStatus("Downgrading to version 1.10f...");
    snprintf(buf, MAX_PATH, "%sArchipelago\\files\\1.10\\*", g_modDir);
    CopyFiles(buf, g_modDir);

    /* Step 4: Install PlugY */
    step++; SetProgress(step * 100 / total);
    SetStatus("Installing PlugY...");
    snprintf(buf, MAX_PATH, "%sArchipelago\\files\\plugy\\*", g_modDir);
    CopyFiles(buf, g_modDir);
    /* Copy PlugY subfolder */
    char plugySrc[MAX_PATH], plugyDst[MAX_PATH];
    snprintf(plugySrc, MAX_PATH, "%sArchipelago\\files\\plugy\\PlugY\\", g_modDir);
    snprintf(plugyDst, MAX_PATH, "%sPlugY\\", g_modDir);
    CopyDir(plugySrc, plugyDst);

    /* Step 5: Install mod files */
    step++; SetProgress(step * 100 / total);
    SetStatus("Installing mod files...");
    /* Only install the 3 mod DLLs that the working installation uses.
     * NewTxt.dll is NOT used and would interfere. */
    snprintf(buf, MAX_PATH, "%sArchipelago\\files\\modfiles\\CustomTbl.dll", g_modDir);
    if (FileExists(buf)) { char d[MAX_PATH]; snprintf(d, MAX_PATH, "%sCustomTbl.dll", g_modDir); CopyFileA(buf, d, FALSE); }
    snprintf(buf, MAX_PATH, "%sArchipelago\\files\\modfiles\\D2Respec.dll", g_modDir);
    if (FileExists(buf)) { char d[MAX_PATH]; snprintf(d, MAX_PATH, "%sD2Respec.dll", g_modDir); CopyFileA(buf, d, FALSE); }
    snprintf(buf, MAX_PATH, "%sArchipelago\\files\\modfiles\\Utility.dll", g_modDir);
    if (FileExists(buf)) { char d[MAX_PATH]; snprintf(d, MAX_PATH, "%sUtility.dll", g_modDir); CopyFileA(buf, d, FALSE); }

    /* Step 6: Install graphics + singling */
    step++; SetProgress(step * 100 / total);
    SetStatus("Installing graphics and patches...");
    snprintf(buf, MAX_PATH, "%sArchipelago\\files\\graphics\\ddraw.dll", g_modDir);
    if (FileExists(buf)) { char d[MAX_PATH]; snprintf(d, MAX_PATH, "%sddraw.dll", g_modDir); CopyFileA(buf, d, FALSE); }
    snprintf(buf, MAX_PATH, "%sArchipelago\\files\\graphics\\ddraw_cnc.dll", g_modDir);
    if (FileExists(buf)) { char d[MAX_PATH]; snprintf(d, MAX_PATH, "%sddraw_cnc.dll", g_modDir); CopyFileA(buf, d, FALSE); }
    snprintf(buf, MAX_PATH, "%sArchipelago\\files\\graphics\\ddraw.ini", g_modDir);
    if (FileExists(buf)) { char d[MAX_PATH]; snprintf(d, MAX_PATH, "%sddraw.ini", g_modDir); CopyFileA(buf, d, FALSE); }
    snprintf(buf, MAX_PATH, "%sArchipelago\\files\\singling\\*", g_modDir);
    CopyFiles(buf, g_modDir);

    /* Step 7: Install our mod */
    step++; SetProgress(step * 100 / total);
    SetStatus("Installing Archipelago mod...");
    snprintf(buf, MAX_PATH, "%sArchipelago\\build\\D2Archipelago.dll", g_modDir);
    if (FileExists(buf)) { char d[MAX_PATH]; snprintf(d, MAX_PATH, "%sD2Archipelago.dll", g_modDir); CopyFileA(buf, d, FALSE); }
    snprintf(buf, MAX_PATH, "%sArchipelago\\build\\d2skillreset.exe", g_modDir);
    if (FileExists(buf)) { char d[MAX_PATH]; snprintf(d, MAX_PATH, "%sd2skillreset.exe", g_modDir); CopyFileA(buf, d, FALSE); }
    snprintf(buf, MAX_PATH, "%sArchipelago\\build\\SFMPQ.dll", g_modDir);
    if (FileExists(buf)) { char d[MAX_PATH]; snprintf(d, MAX_PATH, "%sSFMPQ.dll", g_modDir); CopyFileA(buf, d, FALSE); }

    /* Step 8: Fix MPQs */
    step++; SetProgress(step * 100 / total);
    SetStatus("Fixing MPQ archives...");
    /* Try mpqfix.exe first, then WinMPQ */
    snprintf(buf, MAX_PATH, "%sArchipelago\\files\\mpqfixer\\mpqfix.exe", g_modDir);
    if (FileExists(buf)) {
        char cmd[MAX_PATH * 2];
        snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", buf, g_modDir);
        STARTUPINFOA si = {sizeof(si)}; PROCESS_INFORMATION pi;
        si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, g_modDir, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 30000);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        }
    }

    /* Step 9: Copy pre-generated game data (bin files + skill icons).
     * These are included in the release package — no generation needed. */
    step++; SetProgress(step * 100 / total);
    SetStatus("Setting up game data...");
    {
        char src[MAX_PATH], dst[MAX_PATH];

        /* Copy data/global/excel/ (bin files + 3 txt overrides) */
        snprintf(src, MAX_PATH, "%sdata\\global\\excel\\", g_modDir);
        if (!FileExists(src)) {
            /* data/ is pre-packaged in release — just verify it exists */
            SetStatus("Game data already in place.");
        }

        /* If data/ doesn't exist but Archipelago/data/ does, copy from there */
        snprintf(src, MAX_PATH, "%sdata\\global\\excel\\Skills.txt", g_modDir);
        if (!FileExists(src)) {
            SetStatus("Copying game data from Archipelago folder...");
            char tmp[MAX_PATH];
            snprintf(tmp, MAX_PATH, "%sdata", g_modDir); CreateDirectoryA(tmp, NULL);
            snprintf(tmp, MAX_PATH, "%sdata\\global", g_modDir); CreateDirectoryA(tmp, NULL);
            snprintf(tmp, MAX_PATH, "%sdata\\global\\excel", g_modDir); CreateDirectoryA(tmp, NULL);
            snprintf(tmp, MAX_PATH, "%sdata\\global\\ui", g_modDir); CreateDirectoryA(tmp, NULL);
            snprintf(tmp, MAX_PATH, "%sdata\\global\\ui\\spells", g_modDir); CreateDirectoryA(tmp, NULL);

            /* Copy the 3 txt overrides */
            snprintf(src, MAX_PATH, "%sArchipelago\\data\\vanilla_txt\\Skills.txt", g_modDir);
            snprintf(dst, MAX_PATH, "%sdata\\global\\excel\\Skills.txt", g_modDir);
            CopyFileA(src, dst, FALSE);
            snprintf(src, MAX_PATH, "%sArchipelago\\data\\vanilla_txt\\SkillDesc.txt", g_modDir);
            snprintf(dst, MAX_PATH, "%sdata\\global\\excel\\SkillDesc.txt", g_modDir);
            CopyFileA(src, dst, FALSE);
            snprintf(src, MAX_PATH, "%sArchipelago\\data\\vanilla_txt\\CharStats.txt", g_modDir);
            snprintf(dst, MAX_PATH, "%sdata\\global\\excel\\CharStats.txt", g_modDir);
            CopyFileA(src, dst, FALSE);
        }
    }

    /* Step 9b: Write correct D2Mod.ini (overwrite PlugY's template) */
    {
        char iniPath[MAX_PATH];
        snprintf(iniPath, MAX_PATH, "%sD2Mod.ini", g_modDir);
        FILE *ini = fopen(iniPath, "w");
        if (ini) {
            fprintf(ini, "[D2MOD]\r\n");
            fprintf(ini, "D2Mod=D2Mod.dll\r\n");
            fprintf(ini, "D2Archipelago=D2Archipelago.dll\r\n");
            fprintf(ini, "\r\n");
            fprintf(ini, "[Respec]\r\n");
            fprintf(ini, "pspell=14\r\n");
            fclose(ini);
        }
    }

    /* Step 10: Generate launch script */
    step++; SetProgress(step * 100 / total);
    SetStatus("Creating launch script...");

    /* Detect save directory */
    snprintf(g_saveDir, MAX_PATH, "%s\\Save", g_d2Path);
    if (!FileExists(g_saveDir)) {
        char alt[MAX_PATH];
        snprintf(alt, MAX_PATH, "%s\\Saved Games\\Diablo II", getenv("USERPROFILE") ? getenv("USERPROFILE") : "C:\\Users\\Default");
        if (FileExists(alt)) strncpy(g_saveDir, alt, MAX_PATH);
    }

    /* Copy D2Launcher.exe as "Play Archipelago.exe" in root */
    {
        char src[MAX_PATH], dst[MAX_PATH];
        snprintf(src, MAX_PATH, "%sArchipelago\\build\\D2Launcher.exe", g_modDir);
        snprintf(dst, MAX_PATH, "%sPlay Archipelago.exe", g_modDir);
        CopyFileA(src, dst, FALSE);
    }

    SetProgress(100);
    SetStatus("Installation complete!");

    /* Switch to done page */
    g_page = 3;
    PostMessage(g_hWnd, WM_USER + 1, 0, 0);
    return 0;
}

/* ================================================================
 * UI LAYOUT
 * ================================================================ */
#define W 520
#define H 430
#define PAD 20
#define BTN_W 90
#define BTN_H 30

static void HideAll(void) {
    ShowWindow(g_hPath, SW_HIDE);
    ShowWindow(g_hBrowse, SW_HIDE);
    ShowWindow(g_hProgress, SW_HIDE);
    ShowWindow(g_hStatus, SW_HIDE);
    ShowWindow(g_hShortcut, SW_HIDE);
    ShowWindow(g_hFinishLabel, SW_HIDE);
    ShowWindow(g_hPlayBtn, SW_HIDE);
}

static void ShowPage(void) {
    HideAll();

    /* Always show Cancel */
    EnableWindow(g_hBack, g_page > 0 && g_page < 2);
    ShowWindow(g_hBack, g_page < 3 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hCancel, g_page < 3 ? SW_SHOW : SW_HIDE);

    if (g_page == 0) {
        /* Welcome */
        SetWindowTextA(g_hNext, "Next >");
        EnableWindow(g_hNext, TRUE);
        ShowWindow(g_hNext, SW_SHOW);
    } else if (g_page == 1) {
        /* Browse for D2 */
        ShowWindow(g_hPath, SW_SHOW);
        ShowWindow(g_hBrowse, SW_SHOW);
        SetWindowTextA(g_hNext, "Install");
        EnableWindow(g_hNext, TRUE);
        ShowWindow(g_hNext, SW_SHOW);
    } else if (g_page == 2) {
        /* Installing */
        ShowWindow(g_hProgress, SW_SHOW);
        ShowWindow(g_hStatus, SW_SHOW);
        ShowWindow(g_hNext, SW_HIDE);
        EnableWindow(g_hBack, FALSE);
        EnableWindow(g_hCancel, FALSE);
    } else if (g_page == 3) {
        /* Done */
        ShowWindow(g_hShortcut, SW_SHOW);
        ShowWindow(g_hFinishLabel, SW_SHOW);
        ShowWindow(g_hPlayBtn, SW_SHOW);
        SetWindowTextA(g_hNext, "Finish");
        EnableWindow(g_hNext, TRUE);
        ShowWindow(g_hNext, SW_SHOW);
        ShowWindow(g_hBack, SW_HIDE);
        ShowWindow(g_hCancel, SW_HIDE);
    }

    InvalidateRect(g_hWnd, NULL, TRUE);
}

/* ================================================================
 * WINDOW PROC
 * ================================================================ */
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        /* Draw header bar */
        RECT rc = {0, 0, W, 60};
        HBRUSH hbr = CreateSolidBrush(RGB(40, 40, 40));
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(200, 170, 80));
        HFONT hf = CreateFontA(24, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        HFONT old = SelectObject(hdc, hf);
        rc.left = PAD; rc.top = 15;
        DrawTextA(hdc, "Diablo II Archipelago", -1, &rc, DT_LEFT | DT_NOCLIP);
        SelectObject(hdc, old);
        DeleteObject(hf);

        /* Draw page content text */
        SetTextColor(hdc, RGB(0, 0, 0));
        hf = CreateFontA(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        old = SelectObject(hdc, hf);

        if (g_page == 0) {
            rc.left = PAD; rc.top = 80; rc.right = W - PAD; rc.bottom = 260;
            DrawTextA(hdc,
                "Welcome to the Diablo II Archipelago installer.\r\n\r\n"
                "This will set up a modded copy of Diablo II in this folder.\r\n"
                "Your original Diablo II installation will NOT be modified.\r\n\r\n"
                "You will need:\r\n"
                "  - A Diablo II installation (any version)\r\n"
                "  - Lord of Destruction expansion\r\n\r\n"
                "Click Next to continue.",
                -1, &rc, DT_LEFT | DT_WORDBREAK);
        } else if (g_page == 1) {
            rc.left = PAD; rc.top = 80; rc.right = W - PAD; rc.bottom = 140;
            DrawTextA(hdc,
                "Select the folder where Diablo II is installed.\r\n"
                "This is the folder containing Game.exe and the MPQ files.",
                -1, &rc, DT_LEFT | DT_WORDBREAK);
        }

        SelectObject(hdc, old);
        DeleteObject(hf);

        /* Bottom separator line */
        HPEN hp = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
        HPEN oldp = SelectObject(hdc, hp);
        MoveToEx(hdc, 0, H - 50, NULL);
        LineTo(hdc, W, H - 50);
        SelectObject(hdc, oldp);
        DeleteObject(hp);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);

        if (id == 1001) { /* Browse */
            BROWSEINFOA bi = {0};
            bi.hwndOwner = hWnd;
            bi.lpszTitle = "Select Diablo II installation folder:";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
            if (pidl) {
                SHGetPathFromIDListA(pidl, g_d2Path);
                SetWindowTextA(g_hPath, g_d2Path);
                CoTaskMemFree(pidl);
            }
        }

        if (id == 1002) { /* Next */
            if (g_page == 0) {
                g_page = 1;
                ShowPage();
            } else if (g_page == 1) {
                /* Validate and start install */
                GetWindowTextA(g_hPath, g_d2Path, MAX_PATH);
                int len = (int)strlen(g_d2Path);
                if (len > 0 && g_d2Path[len-1] == '\\') g_d2Path[len-1] = 0;

                char test[MAX_PATH];
                snprintf(test, MAX_PATH, "%s\\Game.exe", g_d2Path);
                if (!FileExists(test)) {
                    snprintf(test, MAX_PATH, "%s\\Diablo II.exe", g_d2Path);
                    if (!FileExists(test)) {
                        MessageBoxA(hWnd, "Game.exe not found in that folder.\nPlease select the correct Diablo II folder.", "Error", MB_OK | MB_ICONERROR);
                        return 0;
                    }
                }
                snprintf(test, MAX_PATH, "%s\\d2data.mpq", g_d2Path);
                if (!FileExists(test)) {
                    MessageBoxA(hWnd, "d2data.mpq not found.\nThis is not a complete Diablo II installation.", "Error", MB_OK | MB_ICONERROR);
                    return 0;
                }

                g_page = 2;
                ShowPage();
                CreateThread(NULL, 0, InstallThread, NULL, 0, NULL);
            } else if (g_page == 3) {
                /* Finish — create shortcut if checked */
                if (SendMessage(g_hShortcut, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    CreateDesktopShortcut();
                }
                DestroyWindow(hWnd);
            }
        }

        if (id == 1003) { /* Back */
            if (g_page == 1) { g_page = 0; ShowPage(); }
        }

        if (id == 1004) { /* Cancel */
            if (MessageBoxA(hWnd, "Cancel installation?", "Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES)
                DestroyWindow(hWnd);
        }

        if (id == 1005) { /* Play Now */
            char exe[MAX_PATH];
            snprintf(exe, MAX_PATH, "%sPlay Archipelago.exe", g_modDir);
            ShellExecuteA(NULL, "open", exe, NULL, g_modDir, SW_SHOWNORMAL);
            DestroyWindow(hWnd);
        }

        return 0;
    }

    case WM_USER + 1:
        ShowPage();
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

/* ================================================================
 * ENTRY POINT
 * ================================================================ */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow) {
    (void)hPrev; (void)cmdLine; (void)nShow;
    g_hInst = hInst;
    GetModDir();

    InitCommonControls();

    WNDCLASSEXA wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "D2ArchInstaller";
    wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(1));
    RegisterClassExA(&wc);

    /* Center on screen — use AdjustWindowRect for correct client area */
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT wr = {0, 0, W, H};
    AdjustWindowRect(&wr, style, FALSE);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    int x = (sx - ww) / 2, y = (sy - wh) / 2;

    g_hWnd = CreateWindowExA(0, "D2ArchInstaller", "Diablo II Archipelago - Setup",
        style, x, y, ww, wh, NULL, NULL, hInst, NULL);

    /* Page 1: Path input + Browse */
    g_hPath = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "C:\\Program Files (x86)\\Diablo II",
        WS_CHILD | ES_AUTOHSCROLL, PAD, 140, W - PAD*2 - BTN_W - 10, 24, g_hWnd, NULL, hInst, NULL);
    g_hBrowse = CreateWindowA("BUTTON", "Browse...",
        WS_CHILD | BS_PUSHBUTTON, W - PAD - BTN_W, 138, BTN_W, 28, g_hWnd, (HMENU)1001, hInst, NULL);

    /* Progress bar + status */
    g_hProgress = CreateWindowA(PROGRESS_CLASSA, NULL,
        WS_CHILD | PBS_SMOOTH, PAD, 140, W - PAD*2, 24, g_hWnd, NULL, hInst, NULL);
    SendMessage(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    g_hStatus = CreateWindowA("STATIC", "Preparing...",
        WS_CHILD, PAD, 175, W - PAD*2, 20, g_hWnd, NULL, hInst, NULL);

    /* Done page */
    g_hFinishLabel = CreateWindowA("STATIC",
        "Installation complete!\r\n\r\nClick 'Play Now' to start the game, or 'Finish' to close.",
        WS_CHILD, PAD, 80, W - PAD*2, 80, g_hWnd, NULL, hInst, NULL);
    g_hShortcut = CreateWindowA("BUTTON", "Create desktop shortcut",
        WS_CHILD | BS_AUTOCHECKBOX, PAD, 180, 250, 24, g_hWnd, NULL, hInst, NULL);
    SendMessage(g_hShortcut, BM_SETCHECK, BST_CHECKED, 0);
    g_hPlayBtn = CreateWindowA("BUTTON", "Play Now",
        WS_CHILD | BS_PUSHBUTTON, PAD, 220, 120, 35, g_hWnd, (HMENU)1005, hInst, NULL);

    /* Bottom buttons */
    g_hBack = CreateWindowA("BUTTON", "< Back",
        WS_CHILD | BS_PUSHBUTTON, W - PAD - BTN_W*3 - 20, H - 42, BTN_W, BTN_H, g_hWnd, (HMENU)1003, hInst, NULL);
    g_hNext = CreateWindowA("BUTTON", "Next >",
        WS_CHILD | BS_DEFPUSHBUTTON, W - PAD - BTN_W*2 - 10, H - 42, BTN_W, BTN_H, g_hWnd, (HMENU)1002, hInst, NULL);
    g_hCancel = CreateWindowA("BUTTON", "Cancel",
        WS_CHILD | BS_PUSHBUTTON, W - PAD - BTN_W, H - 42, BTN_W, BTN_H, g_hWnd, (HMENU)1004, hInst, NULL);

    /* Set font on all controls */
    HFONT hFont = CreateFontA(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    SendMessage(g_hPath, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(g_hBrowse, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(g_hNext, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(g_hBack, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(g_hCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(g_hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(g_hShortcut, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(g_hFinishLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(g_hPlayBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_page = 0;
    ShowPage();
    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
