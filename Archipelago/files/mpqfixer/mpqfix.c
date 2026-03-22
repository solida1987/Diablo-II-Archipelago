/* mpqfix.c - Command-line MPQ attribute remover using SFMPQ.dll */
/* Removes "(attributes)" entries that break old D2 versions */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

/* SFMPQ function signatures (from SFmpqapi.h) */
typedef HANDLE (WINAPI *fn_MpqOpenArchiveForUpdate)(LPCSTR, DWORD, DWORD);
typedef DWORD  (WINAPI *fn_MpqCloseUpdatedArchive)(HANDLE, DWORD);
typedef BOOL   (WINAPI *fn_MpqDeleteFile)(HANDLE, LPCSTR);

int main(int argc, char *argv[]) {
    HMODULE sfmpq;
    fn_MpqOpenArchiveForUpdate pOpen;
    fn_MpqCloseUpdatedArchive  pClose;
    fn_MpqDeleteFile           pDelete;

    const char *mpqs[] = { "d2char.mpq", "d2data.mpq", "d2sfx.mpq" };
    const char *gamedir;
    char path[MAX_PATH];
    int i, fixed = 0;

    if (argc > 1) {
        gamedir = argv[1];
    } else {
        gamedir = "..";
    }

    /* Load SFMPQ.dll from same directory as this exe */
    sfmpq = LoadLibraryA("SFMPQ.dll");
    if (!sfmpq) {
        printf("  [ERROR] Cannot load SFMPQ.dll\n");
        return 1;
    }

    pOpen   = (fn_MpqOpenArchiveForUpdate)GetProcAddress(sfmpq, "MpqOpenArchiveForUpdate");
    pClose  = (fn_MpqCloseUpdatedArchive)GetProcAddress(sfmpq, "MpqCloseUpdatedArchive");
    pDelete = (fn_MpqDeleteFile)GetProcAddress(sfmpq, "MpqDeleteFile");

    if (!pOpen || !pClose || !pDelete) {
        printf("  [ERROR] SFMPQ.dll missing required functions\n");
        printf("    Open=%p Close=%p Delete=%p\n", pOpen, pClose, pDelete);
        FreeLibrary(sfmpq);
        return 1;
    }

    for (i = 0; i < 3; i++) {
        HANDLE hMpq;

        snprintf(path, MAX_PATH, "%s\\%s", gamedir, mpqs[i]);

        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
            printf("  [SKIP] %s not found\n", mpqs[i]);
            continue;
        }

        /* Open archive for update, flags=0x08 (don't create), max files=0 */
        hMpq = pOpen(path, 0x08, 0);
        if (hMpq && hMpq != INVALID_HANDLE_VALUE) {
            if (pDelete(hMpq, "(attributes)")) {
                printf("  [FIXED] %s - removed (attributes)\n", mpqs[i]);
                fixed++;
            } else {
                printf("  [OK] %s - clean (no attributes)\n", mpqs[i]);
            }
            pClose(hMpq, 0);
        } else {
            printf("  [ERROR] Cannot open %s\n", mpqs[i]);
        }
    }

    FreeLibrary(sfmpq);
    printf("  Done. %d file(s) fixed.\n", fixed);
    return 0;
}
