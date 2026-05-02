/* ================================================================
 * d2arch_versionpatch.c — Replace D2's "v 1.10" with our mod version
 *
 * D2Client.dll and D2Launch.dll build the version string at runtime via
 *   sprintf(buf, "v %d.%02d", major, minor)
 * using format strings stored in each DLL's .rdata section. By overwriting
 * the format strings with a literal mod-version + null we make D2's own
 * sprintf produce our mod version — main menu, in-game HUD, tab-map
 * overlay — all from a single patch, no overdraw needed.
 *
 * The format strings take 10+ bytes; an 11-byte replacement (e.g.
 * "Beta 1.8.1\0") fits within the slot padding (padding NULLs follow).
 *
 * Discovered offsets (vanilla D2 1.10f shipped DLLs):
 *   D2Client.dll  file-offset 0xD7EA4  "v %d.%02d\0"
 *   D2Client.dll  file-offset 0xD7EB0  "v %d.%02d %d\0"
 *   D2Launch.dll  file-offset 0x27654  "v %d.%02d\0"
 *   D2Launch.dll  file-offset 0x27660  "v %d.%02d %d\0"
 *
 * We DON'T use the file offsets directly — we scan the loaded module's
 * memory for the byte pattern instead. More robust across PE layouts.
 *
 * Unity-build integration: this file MUST be #included in d2arch.c AFTER
 * d2arch_config.c (Log) and BEFORE d2arch_main.c (which calls VersionPatchApply).
 * ================================================================ */

#include "d2arch_version.h"

/* Pattern to find: "v %d.%02d" (ASCII, 9 bytes + null terminator).
 * We match this exact byte sequence. */
static const unsigned char s_versionPat[]  = { 'v', ' ', '%', 'd', '.', '%', '0', '2', 'd', 0x00 };
/* Longer variant: "v %d.%02d %d\0" (13 bytes) — also patched.
 * We match it first (longer/more specific) to avoid mis-matching the
 * shorter one inside the longer. */
static const unsigned char s_versionPatLong[] = { 'v', ' ', '%', 'd', '.', '%', '0', '2', 'd', ' ', '%', 'd', 0x00 };

/* Replacement: D2ARCH_VERSION_DISPLAY (e.g. "Beta 1.8.2") + null.
 * Fits within either 10-byte or 13-byte slot (padding NULLs follow) as long
 * as the display string stays at most ~12 chars. When D2 calls
 * sprintf(buf, "Beta 1.8.X", ...) the format string has no conversions so
 * the args are ignored and our literal is copied verbatim. */
static const unsigned char s_versionRepl[] = D2ARCH_VERSION_DISPLAY;

/* Scan a range of bytes for a pattern. Returns pointer inside range or NULL. */
static unsigned char* FindBytes(unsigned char* hay, SIZE_T hayLen,
                                const unsigned char* needle, SIZE_T needleLen) {
    if (needleLen == 0 || hayLen < needleLen) return NULL;
    SIZE_T last = hayLen - needleLen;
    for (SIZE_T i = 0; i <= last; i++) {
        if (hay[i] != needle[0]) continue;
        SIZE_T j = 1;
        while (j < needleLen && hay[i+j] == needle[j]) j++;
        if (j == needleLen) return hay + i;
    }
    return NULL;
}

/* Get module size by parsing PE header. Returns image size in bytes. */
static SIZE_T GetModuleSize(HMODULE hMod) {
    if (!hMod) return 0;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)hMod;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((unsigned char*)hMod + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    return nt->OptionalHeader.SizeOfImage;
}

/* Patch a single occurrence of pattern `pat` inside module `hMod`.
 * Returns number of bytes patched (0 if not found). Writes replacement `repl`
 * of length `replLen`. Uses VirtualProtect to make .rdata writable briefly. */
static int PatchOnePattern(HMODULE hMod, const char* modName,
                           const unsigned char* pat, SIZE_T patLen,
                           const unsigned char* repl, SIZE_T replLen) {
    if (!hMod) {
        Log("VersionPatch: module '%s' not loaded\n", modName);
        return 0;
    }
    SIZE_T modSize = GetModuleSize(hMod);
    if (modSize == 0) {
        Log("VersionPatch: cannot read PE size for '%s'\n", modName);
        return 0;
    }
    unsigned char* base = (unsigned char*)hMod;
    unsigned char* hit = FindBytes(base, modSize, pat, patLen);
    if (!hit) {
        Log("VersionPatch: pattern not found in '%s'\n", modName);
        return 0;
    }
    /* Enable write on the page containing hit + replLen bytes */
    DWORD oldProt = 0;
    if (!VirtualProtect(hit, replLen, PAGE_EXECUTE_READWRITE, &oldProt)) {
        Log("VersionPatch: VirtualProtect failed on '%s' @ %p (err=%d)\n",
            modName, hit, (int)GetLastError());
        return 0;
    }
    memcpy(hit, repl, replLen);
    DWORD tmp = 0;
    VirtualProtect(hit, replLen, oldProt, &tmp);
    /* FlushInstructionCache to be safe (we're patching data but conservative) */
    FlushInstructionCache(GetCurrentProcess(), hit, replLen);
    Log("VersionPatch: '%s' @ %p — patched %u bytes -> \"%.10s\"\n",
        modName, hit, (unsigned)replLen, (const char*)repl);
    return (int)replLen;
}

/* Patch ALL occurrences of pattern in a module (there are typically 2: short + long variant slots). */
static int PatchAllOccurrences(HMODULE hMod, const char* modName,
                               const unsigned char* pat, SIZE_T patLen,
                               const unsigned char* repl, SIZE_T replLen) {
    if (!hMod) {
        Log("VersionPatch: '%s' — GetModuleHandle returned NULL\n", modName);
        return 0;
    }
    SIZE_T modSize = GetModuleSize(hMod);
    Log("VersionPatch: '%s' base=%p size=%u patLen=%u\n",
        modName, hMod, (unsigned)modSize, (unsigned)patLen);
    if (modSize == 0) return 0;
    unsigned char* base = (unsigned char*)hMod;
    /* Sanity probe: log the first 16 bytes (should be MZ header) and a byte
     * inside .rdata where we expect the format string to live. */
    Log("VersionPatch: '%s' first bytes=%02x %02x %02x %02x  pat[0..4]=%02x %02x %02x %02x %02x\n",
        modName, base[0], base[1], base[2], base[3],
        pat[0], pat[1], pat[2], pat[3], pat[4]);
    /* Try VirtualQuery-based scan over each committed region — some sections
     * may have protection that crashes a naive byte scan. */
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char* cursor = base;
    unsigned char* endOfModule = base + modSize;
    int total = 0;
    int regionCount = 0;
    while (cursor < endOfModule) {
        if (VirtualQuery(cursor, &mbi, sizeof(mbi)) == 0) break;
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY | PAGE_WRITECOPY))) {
            unsigned char* regionBase = (unsigned char*)mbi.BaseAddress;
            SIZE_T regionSize = mbi.RegionSize;
            /* Clamp region to module bounds */
            if (regionBase < base) { regionSize -= (base - regionBase); regionBase = base; }
            if (regionBase + regionSize > endOfModule) regionSize = endOfModule - regionBase;
            regionCount++;
            /* Scan this region for all occurrences */
            unsigned char* scanAt = regionBase;
            SIZE_T scanLeft = regionSize;
            while (scanLeft >= patLen) {
                unsigned char* hit = FindBytes(scanAt, scanLeft, pat, patLen);
                if (!hit) break;
                DWORD oldProt = 0;
                if (VirtualProtect(hit, replLen, PAGE_EXECUTE_READWRITE, &oldProt)) {
                    memcpy(hit, repl, replLen);
                    DWORD tmp = 0;
                    VirtualProtect(hit, replLen, oldProt, &tmp);
                    FlushInstructionCache(GetCurrentProcess(), hit, replLen);
                    Log("VersionPatch: '%s' @ %p (rva=0x%x) — patched %u bytes\n",
                        modName, hit, (unsigned)((DWORD)hit - (DWORD)base), (unsigned)replLen);
                    total++;
                } else {
                    Log("VersionPatch: '%s' @ %p — VirtualProtect failed (err=%d)\n",
                        modName, hit, (int)GetLastError());
                }
                SIZE_T advance = (SIZE_T)(hit - scanAt) + patLen;
                scanAt += advance;
                scanLeft -= advance;
            }
        }
        cursor = (unsigned char*)mbi.BaseAddress + mbi.RegionSize;
    }
    Log("VersionPatch: '%s' — scanned %d regions, %d hits for this pattern\n",
        modName, regionCount, total);
    return total;
}

/* Diagnostic: read bytes at a specific module-relative offset and log them. */
static void LogBytesAt(HMODULE hMod, const char* modName, DWORD rva, int nBytes) {
    if (!hMod) return;
    unsigned char* p = (unsigned char*)hMod + rva;
    __try {
        char hexbuf[128]; hexbuf[0] = 0;
        char ascbuf[32];  ascbuf[0] = 0;
        for (int i = 0; i < nBytes && i < 20; i++) {
            char tmp[8];
            sprintf(tmp, "%02x ", p[i]);
            strcat(hexbuf, tmp);
            ascbuf[i] = (p[i] >= 0x20 && p[i] < 0x7F) ? (char)p[i] : '.';
            ascbuf[i+1] = 0;
        }
        Log("VersionPatch: %s rva=0x%x bytes: %s  ascii: %s\n", modName, rva, hexbuf, ascbuf);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("VersionPatch: %s rva=0x%x — READ FAILED (access violation)\n", modName, rva);
    }
}

/* Public entry point — call ONCE at DLL init after D2Client + D2Launch are loaded. */
static void VersionPatchApply(void) {
    Log("VersionPatch: applying \"" D2ARCH_VERSION_DISPLAY "\" patch to D2Client/D2Launch format strings\n");

    HMODULE hClient = GetModuleHandleA("D2Client.dll");
    HMODULE hLaunch = GetModuleHandleA("D2Launch.dll");

    /* Direct reads at known file offsets (RVA==file_offset for these DLLs per PE dump) */
    LogBytesAt(hClient, "D2Client.dll", 0xD7EA4, 16);
    LogBytesAt(hClient, "D2Client.dll", 0xD7EB0, 16);
    LogBytesAt(hLaunch, "D2Launch.dll", 0x27654, 16);
    LogBytesAt(hLaunch, "D2Launch.dll", 0x27660, 16);

    int total = 0;
    total += PatchAllOccurrences(hClient, "D2Client.dll",
                                  s_versionPatLong, sizeof(s_versionPatLong),
                                  s_versionRepl,    sizeof(s_versionRepl));
    total += PatchAllOccurrences(hClient, "D2Client.dll",
                                  s_versionPat,     sizeof(s_versionPat),
                                  s_versionRepl,    sizeof(s_versionRepl));
    total += PatchAllOccurrences(hLaunch, "D2Launch.dll",
                                  s_versionPatLong, sizeof(s_versionPatLong),
                                  s_versionRepl,    sizeof(s_versionRepl));
    total += PatchAllOccurrences(hLaunch, "D2Launch.dll",
                                  s_versionPat,     sizeof(s_versionPat),
                                  s_versionRepl,    sizeof(s_versionRepl));

    /* If scan found nothing, try the known RVAs directly as a fallback */
    if (total == 0) {
        Log("VersionPatch: scan failed — trying direct-RVA fallback\n");
        struct { HMODULE mod; const char* name; DWORD rva; } sites[] = {
            { hClient, "D2Client.dll", 0xD7EA4 },
            { hClient, "D2Client.dll", 0xD7EB0 },
            { hLaunch, "D2Launch.dll", 0x27654 },
            { hLaunch, "D2Launch.dll", 0x27660 },
        };
        for (int i = 0; i < 4; i++) {
            if (!sites[i].mod) continue;
            unsigned char* hit = (unsigned char*)sites[i].mod + sites[i].rva;
            DWORD oldProt = 0;
            if (VirtualProtect(hit, sizeof(s_versionRepl), PAGE_EXECUTE_READWRITE, &oldProt)) {
                memcpy(hit, s_versionRepl, sizeof(s_versionRepl));
                DWORD tmp = 0;
                VirtualProtect(hit, sizeof(s_versionRepl), oldProt, &tmp);
                FlushInstructionCache(GetCurrentProcess(), hit, sizeof(s_versionRepl));
                Log("VersionPatch: direct-RVA %s @ %p (rva=0x%x) — forced patch applied\n",
                    sites[i].name, hit, sites[i].rva);
                total++;
            }
        }
    }

    Log("VersionPatch: complete, total sites patched=%d\n", total);
}
