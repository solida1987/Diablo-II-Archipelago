/*
 * d2_hooks.h — Memory patching / detour utilities for D2 1.10f
 * All functions use VirtualProtect for write access.
 */
#ifndef D2_HOOKS_H
#define D2_HOOKS_H

#include <windows.h>

/* Write a JMP instruction at 'address' that jumps to 'newFunc'.
 * Overwrites 5 bytes. Returns the original bytes for restoration.
 * Usage: PatchJMP(0x6FAA1234, MyHookFunction);
 */
static BYTE s_origBytes[5];

static inline BOOL PatchJMP(DWORD address, DWORD newFunc)
{
    DWORD oldProtect;
    if (!VirtualProtect((LPVOID)address, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return FALSE;

    /* Save original bytes */
    memcpy(s_origBytes, (LPVOID)address, 5);

    /* Write JMP rel32 */
    *(BYTE*)address = 0xE9;
    *(DWORD*)(address + 1) = newFunc - address - 5;

    VirtualProtect((LPVOID)address, 5, oldProtect, &oldProtect);
    return TRUE;
}

/* Write a CALL instruction at 'address' that calls 'newFunc'.
 * Overwrites 5 bytes.
 */
static inline BOOL PatchCALL(DWORD address, DWORD newFunc)
{
    DWORD oldProtect;
    if (!VirtualProtect((LPVOID)address, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return FALSE;

    *(BYTE*)address = 0xE8;
    *(DWORD*)(address + 1) = newFunc - address - 5;

    VirtualProtect((LPVOID)address, 5, oldProtect, &oldProtect);
    return TRUE;
}

/* Write arbitrary bytes at 'address'. */
static inline BOOL PatchBytes(DWORD address, const BYTE* bytes, int len)
{
    DWORD oldProtect;
    if (!VirtualProtect((LPVOID)address, len, PAGE_EXECUTE_READWRITE, &oldProtect))
        return FALSE;

    memcpy((LPVOID)address, bytes, len);

    VirtualProtect((LPVOID)address, len, oldProtect, &oldProtect);
    return TRUE;
}

/* Write a single DWORD at 'address'. */
static inline BOOL PatchDWORD(DWORD address, DWORD value)
{
    return PatchBytes(address, (const BYTE*)&value, 4);
}

/* NOP out 'count' bytes at 'address'. */
static inline BOOL PatchNOP(DWORD address, int count)
{
    DWORD oldProtect;
    if (!VirtualProtect((LPVOID)address, count, PAGE_EXECUTE_READWRITE, &oldProtect))
        return FALSE;

    memset((LPVOID)address, 0x90, count);

    VirtualProtect((LPVOID)address, count, oldProtect, &oldProtect);
    return TRUE;
}

#endif /* D2_HOOKS_H */
