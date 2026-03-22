/*
 * Build Universal Skill Icon DC6
 *
 * Extracts ALL 210 skill icons from the 7 class DC6 files,
 * builds ONE universal DC6 containing all icons (1 frame per skill = 210 frames).
 * Each skill gets a fixed index (0-209) regardless of class.
 * The same file is written as all 7 class DC6 files.
 *
 * Also writes a mapping file (skill_icon_map.dat) that maps skillId -> frame index.
 *
 * Usage: build_universal_icons.exe [game_dir]
 */
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SFMPQ */
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

static const char* CLASS_DC6[] = {
    "AmSkillicon", "SoSkillicon", "NeSkillicon", "PaSkillicon",
    "BaSkillicon", "DrSkillicon", "AsSkillicon"
};
static const char* MPQ_NAMES[] = { "patch_d2.mpq", "d2exp.mpq", "d2data.mpq" };

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

/* All 210 class skills: {skillId, classIndex, nIconCel} */
typedef struct { int id; int cls; int cel; } SkillIcon;

/* Skills ordered by class, sorted by ID.
 * nIconCel values: 0, 2, 4, 6, ... (even numbers = normal frame) */
static SkillIcon SKILLS[] = {
    /* Amazon (class 0, skills 6-35) */
    {6,0,0},{7,0,2},{8,0,4},{9,0,6},{10,0,8},{11,0,10},{12,0,12},{13,0,14},{14,0,16},{15,0,18},
    {16,0,20},{17,0,22},{18,0,24},{19,0,26},{20,0,28},{21,0,30},{22,0,32},{23,0,34},{24,0,36},{25,0,38},
    {26,0,40},{27,0,42},{28,0,44},{29,0,46},{30,0,48},{31,0,50},{32,0,52},{33,0,54},{34,0,56},{35,0,58},
    /* Sorceress (class 1, skills 36-65) */
    {36,1,0},{37,1,2},{38,1,4},{39,1,6},{40,1,8},{41,1,10},{42,1,12},{43,1,14},{44,1,16},{45,1,18},
    {46,1,20},{47,1,22},{48,1,24},{49,1,26},{50,1,28},{51,1,30},{52,1,32},{53,1,34},{54,1,36},{55,1,38},
    {56,1,40},{57,1,42},{58,1,44},{59,1,46},{60,1,48},{61,1,50},{62,1,52},{63,1,54},{64,1,56},{65,1,58},
    /* Necromancer (class 2, skills 66-95) */
    {66,2,0},{67,2,2},{68,2,4},{69,2,6},{70,2,8},{71,2,10},{72,2,12},{73,2,14},{74,2,16},{75,2,18},
    {76,2,20},{77,2,22},{78,2,24},{79,2,26},{80,2,28},{81,2,30},{82,2,32},{83,2,34},{84,2,36},{85,2,38},
    {86,2,40},{87,2,42},{88,2,44},{89,2,46},{90,2,48},{91,2,50},{92,2,52},{93,2,54},{94,2,56},{95,2,58},
    /* Paladin (class 3, skills 96-125) */
    {96,3,0},{97,3,2},{98,3,4},{99,3,6},{100,3,8},{101,3,10},{102,3,12},{103,3,14},{104,3,16},{105,3,18},
    {106,3,20},{107,3,22},{108,3,24},{109,3,26},{110,3,28},{111,3,30},{112,3,32},{113,3,34},{114,3,36},{115,3,38},
    {116,3,40},{117,3,42},{118,3,44},{119,3,46},{120,3,48},{121,3,50},{122,3,52},{123,3,54},{124,3,56},{125,3,58},
    /* Barbarian (class 4, skills 126-155) */
    {126,4,0},{127,4,2},{128,4,4},{129,4,6},{130,4,8},{131,4,10},{132,4,12},{133,4,14},{134,4,16},{135,4,18},
    {136,4,20},{137,4,22},{138,4,24},{139,4,26},{140,4,28},{141,4,30},{142,4,32},{143,4,34},{144,4,36},{145,4,38},
    {146,4,40},{147,4,42},{148,4,44},{149,4,46},{150,4,48},{151,4,50},{152,4,52},{153,4,54},{154,4,56},{155,4,58},
    /* Druid (class 5, skills 221-250) */
    {221,5,0},{222,5,2},{223,5,4},{224,5,6},{225,5,8},{226,5,10},{227,5,12},{228,5,14},{229,5,16},{230,5,18},
    {231,5,20},{232,5,22},{233,5,24},{234,5,26},{235,5,28},{236,5,30},{237,5,32},{238,5,34},{239,5,36},{240,5,38},
    {241,5,40},{242,5,42},{243,5,44},{244,5,46},{245,5,48},{246,5,50},{247,5,52},{248,5,54},{249,5,56},{250,5,58},
    /* Assassin (class 6, skills 251-280) */
    {251,6,0},{252,6,2},{253,6,4},{254,6,6},{255,6,8},{256,6,10},{257,6,12},{258,6,14},{259,6,16},{260,6,18},
    {261,6,20},{262,6,22},{263,6,24},{264,6,26},{265,6,28},{266,6,30},{267,6,32},{268,6,34},{269,6,36},{270,6,38},
    {271,6,40},{272,6,42},{273,6,44},{274,6,46},{275,6,48},{276,6,50},{277,6,52},{278,6,54},{279,6,56},{280,6,58},
};
#define NUM_SKILLS 210

static unsigned char* ExtractFile(const char* gameDir, const char* mpqFile,
                                   const char* arcName, unsigned int* outSize) {
    char mpqPath[MAX_PATH];
    sprintf(mpqPath, "%s\\%s", gameDir, mpqFile);
    HANDLE hMpq = NULL;
    if (!pOpen(mpqPath, 0, 0, &hMpq) || !hMpq) return NULL;
    char arcPath[256];
    sprintf(arcPath, "data\\global\\ui\\SPELLS\\%s.DC6", arcName);
    HANDLE hFile = NULL;
    if (!pOpenFile(hMpq, arcPath, 0, &hFile) || !hFile) { pClose(hMpq); return NULL; }
    DWORD size = pGetSize(hFile, NULL);
    if (size == 0 || size > 10000000) { pCloseFile(hFile); pClose(hMpq); return NULL; }
    unsigned char* buf = (unsigned char*)malloc(size);
    DWORD rd = 0;
    pReadFile(hFile, buf, size, &rd, NULL);
    pCloseFile(hFile); pClose(hMpq);
    if (rd == 0) { free(buf); return NULL; }
    *outSize = rd;
    return buf;
}

/* Get a frame's raw data block from a DC6 file */
static int GetFrameBlock(unsigned char* dc6, unsigned int dc6Size, int frameIdx,
                          unsigned char** outData, unsigned int* outSize) {
    DC6Header* h = (DC6Header*)dc6;
    int nf = h->directions * h->framesPerDir;
    if (frameIdx < 0 || frameIdx >= nf) return 0;
    unsigned int* ptrs = (unsigned int*)(dc6 + 24);
    unsigned int off = ptrs[frameIdx];
    if (off + 32 > dc6Size) return 0;
    unsigned int length = *(unsigned int*)(dc6 + off + 28);
    unsigned int blockSize = 32 + length + 3;
    if (off + blockSize > dc6Size) return 0;
    *outData = dc6 + off;
    *outSize = blockSize;
    return 1;
}

int main(int argc, char* argv[]) {
    char gameDir[MAX_PATH];
    if (argc > 1) {
        strcpy(gameDir, argv[1]);
    } else {
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
    if (!hDll) { sprintf(dllPath, "%s\\SFMPQ.dll", gameDir); hDll = LoadLibraryA(dllPath); }
    if (!hDll) { printf("ERROR: Cannot load SFMPQ.dll\n"); return 1; }

    pOpen      = (SFileOpenArchive_t)GetProcAddress(hDll, "SFileOpenArchive");
    pClose     = (SFileCloseArchive_t)GetProcAddress(hDll, "SFileCloseArchive");
    pOpenFile  = (SFileOpenFileEx_t)GetProcAddress(hDll, "SFileOpenFileEx");
    pCloseFile = (SFileCloseFile_t)GetProcAddress(hDll, "SFileCloseFile");
    pGetSize   = (SFileGetFileSize_t)GetProcAddress(hDll, "SFileGetFileSize");
    pReadFile  = (SFileReadFile_t)GetProcAddress(hDll, "SFileReadFile");

    /* Extract all 7 class DC6 files from MPQ */
    unsigned char* dc6Data[7] = {0};
    unsigned int dc6Size[7] = {0};
    for (int c = 0; c < 7; c++) {
        printf("Extracting %s... ", CLASS_DC6[c]);
        for (int m = 0; m < 3; m++) {
            dc6Data[c] = ExtractFile(gameDir, MPQ_NAMES[m], CLASS_DC6[c], &dc6Size[c]);
            if (dc6Data[c]) { printf("OK (%u bytes)\n", dc6Size[c]); break; }
        }
        if (!dc6Data[c]) printf("FAILED\n");
    }

    /* Collect the normal frame (not highlight) for each of the 210 skills */
    unsigned char* frameData[NUM_SKILLS] = {0};
    unsigned int frameSizes[NUM_SKILLS] = {0};
    int collected = 0;

    for (int i = 0; i < NUM_SKILLS; i++) {
        int cls = SKILLS[i].cls;
        int cel = SKILLS[i].cel; /* even number = normal frame index in DC6 */
        if (!dc6Data[cls]) continue;
        if (GetFrameBlock(dc6Data[cls], dc6Size[cls], cel, &frameData[i], &frameSizes[i])) {
            collected++;
        } else {
            printf("WARNING: Failed to get frame %d for skill %d (class %d)\n",
                   cel, SKILLS[i].id, cls);
        }
    }
    printf("Collected %d/%d skill icons\n", collected, NUM_SKILLS);

    /* Build the universal DC6: 210 frames (1 per skill) */
    unsigned int hdrSize = 24;
    unsigned int ptrSize = NUM_SKILLS * 4;
    unsigned int dataSize = 0;
    for (int i = 0; i < NUM_SKILLS; i++) dataSize += frameSizes[i];
    unsigned int totalSize = hdrSize + ptrSize + dataSize;

    unsigned char* universal = (unsigned char*)calloc(1, totalSize);

    /* Header */
    DC6Header* oh = (DC6Header*)universal;
    DC6Header* srcH = NULL;
    for (int c = 0; c < 7; c++) if (dc6Data[c]) { srcH = (DC6Header*)dc6Data[c]; break; }
    if (srcH) *oh = *srcH;
    oh->directions = 1;
    oh->framesPerDir = NUM_SKILLS;

    /* Frame pointers + data */
    unsigned int* outPtrs = (unsigned int*)(universal + hdrSize);
    unsigned int dataOff = hdrSize + ptrSize;
    for (int i = 0; i < NUM_SKILLS; i++) {
        outPtrs[i] = dataOff;
        if (frameData[i] && frameSizes[i] > 0) {
            memcpy(universal + dataOff, frameData[i], frameSizes[i]);
            dataOff += frameSizes[i];
        }
    }

    /* Create output directory */
    char outDir[MAX_PATH], tmp[MAX_PATH];
    sprintf(outDir, "%s\\data\\global\\ui\\SPELLS", gameDir);
    sprintf(tmp, "%s\\data", gameDir); CreateDirectoryA(tmp, NULL);
    sprintf(tmp, "%s\\data\\global", gameDir); CreateDirectoryA(tmp, NULL);
    sprintf(tmp, "%s\\data\\global\\ui", gameDir); CreateDirectoryA(tmp, NULL);
    CreateDirectoryA(outDir, NULL);

    /* Write the SAME file as all 7 class DC6 files */
    for (int c = 0; c < 7; c++) {
        char outPath[MAX_PATH];
        sprintf(outPath, "%s\\%s.DC6", outDir, CLASS_DC6[c]);
        FILE* f = fopen(outPath, "wb");
        if (f) {
            fwrite(universal, 1, totalSize, f);
            fclose(f);
            printf("Written: %s (%u bytes, %d frames)\n", outPath, totalSize, NUM_SKILLS);
        }
    }

    /* Write skill_icon_map.dat: skillId -> frame index in universal DC6 */
    char mapPath[MAX_PATH];
    sprintf(mapPath, "%s\\Archipelago\\skill_icon_map.dat", gameDir);
    FILE* mf = fopen(mapPath, "w");
    if (mf) {
        for (int i = 0; i < NUM_SKILLS; i++) {
            fprintf(mf, "%d=%d\n", SKILLS[i].id, i);
        }
        fclose(mf);
        printf("Written: %s (%d mappings)\n", mapPath, NUM_SKILLS);
    }

    /* Cleanup */
    free(universal);
    for (int c = 0; c < 7; c++) free(dc6Data[c]);
    FreeLibrary(hDll);

    printf("\nDone! All 7 class DC6 files contain all 210 skill icons.\n");
    printf("Use skill_icon_map.dat to set nIconCel for each skill.\n");
    return 0;
}
