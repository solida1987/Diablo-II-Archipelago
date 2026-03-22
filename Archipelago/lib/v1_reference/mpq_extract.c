#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

typedef HANDLE (WINAPI *fn_Open)(LPCSTR, DWORD);
typedef BOOL   (WINAPI *fn_Close)(HANDLE);
typedef BOOL   (WINAPI *fn_ReadFile)(HANDLE, LPCSTR, LPVOID, DWORD, LPDWORD);
typedef DWORD  (WINAPI *fn_FileSize)(HANDLE, LPCSTR);
typedef BOOL   (WINAPI *fn_HasFile)(HANDLE, LPCSTR);

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: mpq_extract.exe <mpq_file> <file_in_mpq> [output]\n");
        return 1;
    }

    HMODULE sfmpq = LoadLibraryA("SFMPQ.dll");
    if (!sfmpq) { printf("Cannot load SFMPQ.dll\n"); return 1; }

    fn_Open pOpen = (fn_Open)GetProcAddress(sfmpq, "SFileOpenArchive");
    fn_Close pClose = (fn_Close)GetProcAddress(sfmpq, "SFileCloseArchive");
    fn_ReadFile pRead = (fn_ReadFile)GetProcAddress(sfmpq, "SFileReadFile");
    fn_FileSize pSize = (fn_FileSize)GetProcAddress(sfmpq, "SFileGetFileSize");
    fn_HasFile pHas = (fn_HasFile)GetProcAddress(sfmpq, "SFileHasFile");

    if (!pOpen || !pClose) { printf("Missing SFMPQ functions\n"); return 1; }

    HANDLE hMpq = pOpen(argv[1], 0);
    if (!hMpq) { printf("Cannot open %s\n", argv[1]); return 1; }

    printf("Opened %s\n", argv[1]);

    /* Try to check if file exists */
    if (pHas && !pHas(hMpq, argv[2])) {
        printf("File not found: %s\n", argv[2]);
        pClose(hMpq);
        return 1;
    }

    /* Get file size and read */
    /* Use SFileOpenFileEx + SFileReadFile + SFileCloseFile pattern */
    typedef BOOL (WINAPI *fn_OpenFileEx)(HANDLE, LPCSTR, HANDLE*);
    typedef BOOL (WINAPI *fn_ReadFileEx)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
    typedef BOOL (WINAPI *fn_CloseFile)(HANDLE);
    typedef DWORD (WINAPI *fn_GetSize)(HANDLE, LPDWORD);

    fn_OpenFileEx pOpenFile = (fn_OpenFileEx)GetProcAddress(sfmpq, "SFileOpenFileEx");
    fn_ReadFileEx pReadFile = (fn_ReadFileEx)GetProcAddress(sfmpq, "SFileReadFile");
    fn_CloseFile pCloseFile = (fn_CloseFile)GetProcAddress(sfmpq, "SFileCloseFile");
    fn_GetSize pGetSize = (fn_GetSize)GetProcAddress(sfmpq, "SFileGetFileSize");

    if (!pOpenFile || !pReadFile || !pCloseFile || !pGetSize) {
        printf("Missing file functions\n");
        pClose(hMpq);
        return 1;
    }

    HANDLE hFile = NULL;
    if (!pOpenFile(hMpq, argv[2], &hFile) || !hFile) {
        printf("Cannot open file: %s\n", argv[2]);
        pClose(hMpq);
        return 1;
    }

    DWORD size = pGetSize(hFile, NULL);
    printf("File size: %d\n", size);

    BYTE* buf = (BYTE*)malloc(size + 1);
    DWORD read = 0;
    pReadFile(hFile, buf, size, &read, NULL);
    buf[size] = 0;

    pCloseFile(hFile);
    pClose(hMpq);

    /* Write to output */
    const char* outFile = argc > 3 ? argv[3] : "extracted.txt";
    FILE* f = fopen(outFile, "wb");
    if (f) {
        fwrite(buf, 1, read, f);
        fclose(f);
        printf("Extracted %d bytes to %s\n", read, outFile);
    }

    free(buf);
    FreeLibrary(sfmpq);
    return 0;
}
