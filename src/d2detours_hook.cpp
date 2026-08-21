/* ================================================================
 * D2.Detours Hook Registration
 * This file registers our callbacks with D2.Detours
 * ================================================================ */
#include <windows.h>

/* Our exported functions from d2arch.c */
extern "C" {
    void __cdecl D2Arch_Init(void);
    void __cdecl D2Arch_OnDrawGameUI(void);
    void __cdecl D2Arch_OnUnload(void);
}

/* D2.Detours hook types */
typedef void(__fastcall* D2Client_DrawGameUI_t)(void);
static D2Client_DrawGameUI_t Real_DrawGameUI = NULL;

/* Our replacement DrawGameUI */
void __fastcall Hook_DrawGameUI(void) {
    /* Call original first */
    if (Real_DrawGameUI) Real_DrawGameUI();
    /* Then draw our stuff on top */
    D2Arch_OnDrawGameUI();
}

/* ================================================================
 * D2.Detours DllPreLoadHook / DllPostLoadHook
 * Called by D2.Detours when DLLs are loaded
 * ================================================================ */
extern "C" {

/* Called after D2Client.dll is loaded and patched */
__declspec(dllexport) void __cdecl DllPostLoadHook(const char* dllName, int dllId) {
    /* dllId for D2Client = 1 */
    if (dllId == 1) {
        D2Arch_Init();

        /* Hook DrawGameUI at D2Client + 0x5E650 */
        HMODULE hClient = GetModuleHandleA("D2Client.dll");
        if (hClient) {
            DWORD hookAddr = (DWORD)hClient + 0x5E650;

            /* Create trampoline: save original bytes, patch JMP */
            static BYTE trampoline[16];
            DWORD oldProt;

            /* Copy original 8 bytes */
            if (!VirtualProtect((void*)hookAddr, 8, PAGE_EXECUTE_READWRITE, &oldProt))
                return;
            memcpy(trampoline, (void*)hookAddr, 8);

            /* JMP back to hookAddr+8 */
            trampoline[8] = 0xE9;
            DWORD jmpBack = hookAddr + 8 - (DWORD)&trampoline[8] - 5;
            memcpy(&trampoline[9], &jmpBack, 4);

            /* Make trampoline executable */
            DWORD tp;
            VirtualProtect(trampoline, 16, PAGE_EXECUTE_READWRITE, &tp);
            Real_DrawGameUI = (D2Client_DrawGameUI_t)(void*)trampoline;

            /* Patch original with JMP to our hook */
            BYTE patch[8];
            patch[0] = 0xE9;
            DWORD jmpTo = (DWORD)Hook_DrawGameUI - hookAddr - 5;
            memcpy(&patch[1], &jmpTo, 4);
            /* NOP remaining bytes */
            for (int i = 5; i < 8; i++) patch[i] = 0x90;
            memcpy((void*)hookAddr, patch, 8);

            VirtualProtect((void*)hookAddr, 8, oldProt, &oldProt);
        }
    }
}

} /* extern "C" */
