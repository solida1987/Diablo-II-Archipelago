/*
 * Extract all Skillicon DC6 files from D2 MPQ archives,
 * merge into one mega DC6 file, write to data/ directory.
 * Compile: cl /nologo /MT extract_icons.c /Fe:extract_icons.exe /link SFMPQ.lib
 */
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SFMPQ/StormLib function types */
typedef BOOL (__stdcall *SFileOpenArchive_t)(const char*, DWORD, DWORD, HANDLE*);
typedef BOOL (__stdcall *SFileCloseArchive_t)(HANDLE);
typedef BOOL (__stdcall *SFileOpenFileEx_t)(HANDLE, const char*, DWORD, HANDLE*);
typedef BOOL (__stdcall *SFileCloseFile_t)(HANDLE);
typedef DWORD (__stdcall *SFileGetFileSize_t)(HANDLE, DWORD*);
typedef BOOL (__stdcall *SFileReadFile_t)(HANDLE, void*, DWORD, DWORD*, void*);

static SFileOpenArchive_t  pOpen;
static SFileCloseArchive_t pClose;
static SFileOpenFileEx_t   pOpenFile;
static SFileCloseFile_t    pCloseFile;
static SFileGetFileSize_t  pGetSize;
static SFileReadFile_t     pReadFile;

static const char* CLASS_NAMES[] = {
    "AmSkillicon", "SoSkillicon", "NeSkillicon", "PaSkillicon",
    "BaSkillicon", "DrSkillicon", "AsSkillicon"
};
#define NUM_CLASSES 7

static const char* MPQ_NAMES[] = {
    "patch_d2.mpq", "d2exp.mpq", "d2data.mpq"
};
#define NUM_MPQS 3

/* DC6 header: 24 bytes */
#pragma pack(push, 1)
typedef struct {
    int version;
    unsigned int flags;
    unsigned int encoding;
    unsigned char termination[4];
    unsigned int directions;
    unsigned int framesPerDir;
} DC6Header;
#pragma pack(pop)

static unsigned char* ExtractFile(const char* gameDir, const char* mpqFile, const char* archiveName, unsigned int* outSize) {
    char mpqPath[MAX_PATH];
    sprintf(mpqPath, "%s\\%s", gameDir, mpqFile);

    HANDLE hMpq = NULL;
    if (!pOpen(mpqPath, 0, 0, &hMpq) || !hMpq) return NULL;

    char arcPath[256];
    sprintf(arcPath, "data\\global\\ui\\SPELLS\\%s.DC6", archiveName);

    HANDLE hFile = NULL;
    if (!pOpenFile(hMpq, arcPath, 0, &hFile) || !hFile) {
        pClose(hMpq);
        return NULL;
    }

    DWORD size = pGetSize(hFile, NULL);
    if (size == 0 || size > 10000000) {
        pCloseFile(hFile);
        pClose(hMpq);
        return NULL;
    }

    unsigned char* buf = (unsigned char*)malloc(size);
    DWORD read = 0;
    pReadFile(hFile, buf, size, &read, NULL);
    pCloseFile(hFile);
    pClose(hMpq);

    if (read == 0) { free(buf); return NULL; }
    *outSize = read;
    return buf;
}

int main(int argc, char* argv[]) {
    char gameDir[MAX_PATH];
    if (argc > 1) {
        strcpy(gameDir, argv[1]);
    } else {
        /* Default: parent of parent of this exe */
        GetModuleFileNameA(NULL, gameDir, MAX_PATH);
        char* s = strrchr(gameDir, '\\'); if (s) *s = 0;
        s = strrchr(gameDir, '\\'); if (s) *s = 0;
        s = strrchr(gameDir, '\\'); if (s) *s = 0;
    }
    printf("Game dir: %s\n", gameDir);

    /* Load SFMPQ.dll */
    char dllPath[MAX_PATH];
    sprintf(dllPath, "%s\\Archipelago\\build\\SFMPQ.dll", gameDir);
    HMODULE hDll = LoadLibraryA(dllPath);
    if (!hDll) {
        sprintf(dllPath, "%s\\Storm.dll", gameDir);
        hDll = LoadLibraryA(dllPath);
    }
    if (!hDll) {
        printf("ERROR: Cannot load SFMPQ.dll or Storm.dll\n");
        return 1;
    }
    printf("Loaded: %s\n", dllPath);

    pOpen      = (SFileOpenArchive_t)GetProcAddress(hDll, "SFileOpenArchive");
    pClose     = (SFileCloseArchive_t)GetProcAddress(hDll, "SFileCloseArchive");
    pOpenFile  = (SFileOpenFileEx_t)GetProcAddress(hDll, "SFileOpenFileEx");
    pCloseFile = (SFileCloseFile_t)GetProcAddress(hDll, "SFileCloseFile");
    pGetSize   = (SFileGetFileSize_t)GetProcAddress(hDll, "SFileGetFileSize");
    pReadFile  = (SFileReadFile_t)GetProcAddress(hDll, "SFileReadFile");

    if (!pOpen || !pClose || !pOpenFile || !pCloseFile || !pGetSize || !pReadFile) {
        printf("ERROR: Missing MPQ functions\n");
        FreeLibrary(hDll);
        return 1;
    }

    /* Extract all class DC6 files */
    unsigned char* dc6Data[NUM_CLASSES] = {0};
    unsigned int dc6Size[NUM_CLASSES] = {0};

    for (int c = 0; c < NUM_CLASSES; c++) {
        printf("Extracting %s... ", CLASS_NAMES[c]);
        for (int m = 0; m < NUM_MPQS; m++) {
            dc6Data[c] = ExtractFile(gameDir, MPQ_NAMES[m], CLASS_NAMES[c], &dc6Size[c]);
            if (dc6Data[c]) {
                printf("OK (%u bytes from %s)\n", dc6Size[c], MPQ_NAMES[m]);
                break;
            }
        }
        if (!dc6Data[c]) printf("FAILED\n");
    }

    /* Parse frame counts and build offset table */
    int frameCount[NUM_CLASSES] = {0};
    int frameOffset[NUM_CLASSES] = {0};
    int totalFrames = 0;

    for (int c = 0; c < NUM_CLASSES; c++) {
        if (!dc6Data[c] || dc6Size[c] < 24) continue;
        DC6Header* h = (DC6Header*)dc6Data[c];
        frameCount[c] = h->directions * h->framesPerDir;
        frameOffset[c] = totalFrames;
        totalFrames += frameCount[c];
        printf("  %s: %d frames (offset %d)\n", CLASS_NAMES[c], frameCount[c], frameOffset[c]);
    }

    printf("Total merged frames: %d\n", totalFrames);

    /* Build merged DC6 file:
     * Header (24 bytes) + frame pointer table (totalFrames * 4) + all frame data */

    /* First, collect all raw frame data blocks */
    unsigned char** frameBlocks = (unsigned char**)calloc(totalFrames, sizeof(unsigned char*));
    unsigned int* frameBlockSize = (unsigned int*)calloc(totalFrames, sizeof(unsigned int));
    int fi = 0;

    for (int c = 0; c < NUM_CLASSES; c++) {
        if (!dc6Data[c]) continue;
        DC6Header* h = (DC6Header*)dc6Data[c];
        int nf = h->directions * h->framesPerDir;
        unsigned int* ptrs = (unsigned int*)(dc6Data[c] + 24);

        for (int f = 0; f < nf; f++) {
            unsigned int off = ptrs[f];
            if (off + 32 > dc6Size[c]) continue;
            /* Frame: header(32) + data(length) + terminator(3) */
            unsigned int length = *(unsigned int*)(dc6Data[c] + off + 28);
            unsigned int blockSize = 32 + length + 3;
            if (off + blockSize > dc6Size[c]) blockSize = dc6Size[c] - off;
            frameBlocks[fi] = dc6Data[c] + off;
            frameBlockSize[fi] = blockSize;
            fi++;
        }
    }

    /* Calculate total output size */
    unsigned int headerSize = 24;
    unsigned int ptrTableSize = totalFrames * 4;
    unsigned int dataSize = 0;
    for (int i = 0; i < totalFrames; i++) dataSize += frameBlockSize[i];
    unsigned int totalSize = headerSize + ptrTableSize + dataSize;

    unsigned char* merged = (unsigned char*)calloc(1, totalSize);

    /* Write header */
    DC6Header* outH = (DC6Header*)merged;
    DC6Header* srcH = (DC6Header*)(dc6Data[0] ? dc6Data[0] : dc6Data[6]);
    *outH = *srcH;
    outH->directions = 1;
    outH->framesPerDir = totalFrames;

    /* Write frame pointers and data */
    unsigned int dataOff = headerSize + ptrTableSize;
    unsigned int* outPtrs = (unsigned int*)(merged + headerSize);

    for (int i = 0; i < totalFrames; i++) {
        outPtrs[i] = dataOff;
        memcpy(merged + dataOff, frameBlocks[i], frameBlockSize[i]);
        dataOff += frameBlockSize[i];
    }

    /* Create output directory */
    char outDir[MAX_PATH];
    sprintf(outDir, "%s\\data\\global\\ui\\SPELLS", gameDir);
    CreateDirectoryA(gameDir, NULL);
    char tmp[MAX_PATH];
    sprintf(tmp, "%s\\data", gameDir); CreateDirectoryA(tmp, NULL);
    sprintf(tmp, "%s\\data\\global", gameDir); CreateDirectoryA(tmp, NULL);
    sprintf(tmp, "%s\\data\\global\\ui", gameDir); CreateDirectoryA(tmp, NULL);
    CreateDirectoryA(outDir, NULL);

    /* Write per-class merged files: each class gets ITS OWN icons first,
     * then all other classes after. This way native nIconCel values still work. */
    char offPath[MAX_PATH];
    sprintf(offPath, "%s\\Archipelago\\icon_offsets.dat", gameDir);
    FILE* offFile = fopen(offPath, "w");

    for (int targetCls = 0; targetCls < NUM_CLASSES; targetCls++) {
        if (!dc6Data[targetCls]) continue;

        /* Build frame list: target class first, then others */
        unsigned char** orderedBlocks = (unsigned char**)calloc(totalFrames, sizeof(unsigned char*));
        unsigned int* orderedSizes = (unsigned int*)calloc(totalFrames, sizeof(unsigned int));
        int ofi = 0;

        /* First: target class frames */
        {
            DC6Header* h = (DC6Header*)dc6Data[targetCls];
            int nf = h->directions * h->framesPerDir;
            unsigned int* ptrs = (unsigned int*)(dc6Data[targetCls] + 24);
            for (int f = 0; f < nf; f++) {
                unsigned int off = ptrs[f];
                if (off + 32 > dc6Size[targetCls]) continue;
                unsigned int length = *(unsigned int*)(dc6Data[targetCls] + off + 28);
                unsigned int bs = 32 + length + 3;
                if (off + bs > dc6Size[targetCls]) bs = dc6Size[targetCls] - off;
                orderedBlocks[ofi] = dc6Data[targetCls] + off;
                orderedSizes[ofi] = bs;
                ofi++;
            }
        }
        int nativeCount = ofi; /* how many native frames */

        /* Then: all other classes */
        int classOffsetInFile[NUM_CLASSES];
        for (int c = 0; c < NUM_CLASSES; c++) classOffsetInFile[c] = -1;
        classOffsetInFile[targetCls] = 0; /* native class is at offset 0 */

        for (int c = 0; c < NUM_CLASSES; c++) {
            if (c == targetCls || !dc6Data[c]) continue;
            classOffsetInFile[c] = ofi;
            DC6Header* h = (DC6Header*)dc6Data[c];
            int nf = h->directions * h->framesPerDir;
            unsigned int* ptrs = (unsigned int*)(dc6Data[c] + 24);
            for (int f = 0; f < nf; f++) {
                unsigned int off = ptrs[f];
                if (off + 32 > dc6Size[c]) continue;
                unsigned int length = *(unsigned int*)(dc6Data[c] + off + 28);
                unsigned int bs = 32 + length + 3;
                if (off + bs > dc6Size[c]) bs = dc6Size[c] - off;
                orderedBlocks[ofi] = dc6Data[c] + off;
                orderedSizes[ofi] = bs;
                ofi++;
            }
        }

        /* Build DC6 file */
        unsigned int hdrSize = 24;
        unsigned int ptrSize = ofi * 4;
        unsigned int dSize = 0;
        for (int i = 0; i < ofi; i++) dSize += orderedSizes[i];
        unsigned int tSize = hdrSize + ptrSize + dSize;

        unsigned char* out = (unsigned char*)calloc(1, tSize);
        DC6Header* oh = (DC6Header*)out;
        DC6Header* sh = (DC6Header*)(dc6Data[0] ? dc6Data[0] : dc6Data[6]);
        *oh = *sh;
        oh->directions = 1;
        oh->framesPerDir = ofi;

        unsigned int dOff = hdrSize + ptrSize;
        unsigned int* oP = (unsigned int*)(out + hdrSize);
        for (int i = 0; i < ofi; i++) {
            oP[i] = dOff;
            memcpy(out + dOff, orderedBlocks[i], orderedSizes[i]);
            dOff += orderedSizes[i];
        }

        char outPath2[MAX_PATH];
        sprintf(outPath2, "%s\\%s.DC6", outDir, CLASS_NAMES[targetCls]);
        FILE* f = fopen(outPath2, "wb");
        if (f) { fwrite(out, 1, tSize, f); fclose(f); }
        printf("Written: %s (%u bytes, %d frames, native=%d)\n",
               outPath2, tSize, ofi, nativeCount);

        /* Write offset table for this class:
         * For targetCls file: native class is at offset 0,
         * other classes at their respective offsets */
        if (offFile) {
            fprintf(offFile, "target=%d\n", targetCls);
            for (int c = 0; c < NUM_CLASSES; c++) {
                if (classOffsetInFile[c] >= 0)
                    fprintf(offFile, "%d,%d\n", c, classOffsetInFile[c]);
            }
        }

        free(orderedBlocks);
        free(orderedSizes);
        free(out);
    }

    if (offFile) {
        fclose(offFile);
        printf("Written offset table: %s\n", offPath);
    }

    /* Cleanup */
    free(merged);
    free(frameBlocks);
    free(frameBlockSize);
    for (int c = 0; c < NUM_CLASSES; c++) free(dc6Data[c]);
    FreeLibrary(hDll);

    printf("\nDone! Run game with -direct flag to use merged icons.\n");
    printf("Runtime: adjust nIconCel = icon_offset[original_charclass] + original_nIconCel\n");
    return 0;
}
