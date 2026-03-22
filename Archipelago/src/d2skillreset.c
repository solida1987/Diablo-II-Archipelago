/* d2skillreset.exe — Pre-game tool to reset skill allocations in .d2s files.
 *
 * Usage: d2skillreset.exe "C:\Program Files (x86)\Diablo II\Save"
 *
 * For each .d2s file:
 *  1. Find "if" section (30 skill bytes) and "gf" section (bitpacked stats)
 *  2. Count total = sum(30 skill bytes) + current NEWSKILLS pool
 *  3. Zero all 30 skill bytes
 *  4. Write total into NEWSKILLS in the bitpacked stats (insert if missing)
 *  5. Recalculate d2s checksum
 *  6. Save modified .d2s
 *
 * No .reset file needed — everything is done in the .d2s itself.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* ============================================================
 * BIT READING/WRITING helpers for D2's bitpacked stats
 * ============================================================ */
static int read_bits(const unsigned char *data, int dataLen, int bitOff, int numBits) {
    int result = 0;
    for (int i = 0; i < numBits; i++) {
        int byteIdx = (bitOff + i) / 8;
        int bitIdx  = (bitOff + i) % 8;
        if (byteIdx < dataLen) {
            if (data[byteIdx] & (1 << bitIdx))
                result |= (1 << i);
        }
    }
    return result;
}

static void write_bits(unsigned char *data, int dataLen, int bitOff, int numBits, int value) {
    for (int i = 0; i < numBits; i++) {
        int byteIdx = (bitOff + i) / 8;
        int bitIdx  = (bitOff + i) % 8;
        if (byteIdx < dataLen) {
            if (value & (1 << i))
                data[byteIdx] |= (1 << bitIdx);
            else
                data[byteIdx] &= ~(1 << bitIdx);
        }
    }
}

/* CSvBits for stats 0-15 in D2 1.10f */
static int GetStatBits(int statId) {
    switch (statId) {
        case 0: case 1: case 2: case 3: case 4: return 10;
        case 5: return 8;   /* NEWSKILLS */
        case 6: case 7: case 8: case 9: case 10: case 11: return 21;
        case 12: return 7;
        case 13: return 32;
        case 14: case 15: return 25;
        default: return 0;  /* unknown */
    }
}

/* ============================================================
 * D2S checksum
 * ============================================================ */
static unsigned int CalcChecksum(unsigned char *data, int size) {
    unsigned int checksum = 0;
    for (int i = 0; i < size; i++) {
        checksum = (checksum << 1) | (checksum >> 31);
        checksum += data[i];
    }
    return checksum;
}

/* ============================================================
 * Parsed stat entry
 * ============================================================ */
typedef struct {
    int id;
    int value;
} StatEntry;

#define MAX_STATS 32

/* ============================================================
 * Process a single .d2s file
 * ============================================================ */
static int ProcessFile(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("  Cannot open: %s\n", path); return 0; }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize < 100) {
        printf("  File too small: %s\n", path);
        fclose(f);
        return 0;
    }

    unsigned char *data = (unsigned char*)malloc(fileSize + 64); /* extra space for insertion */
    if (!data) { fclose(f); return 0; }
    fread(data, 1, fileSize, f);
    fclose(f);

    /* Verify d2s signature */
    if (data[0] != 0x55 || data[1] != 0xAA || data[2] != 0x55 || data[3] != 0xAA) {
        printf("  Not a valid d2s: %s\n", path);
        free(data);
        return 0;
    }

    char charName[17] = {0};
    memcpy(charName, data + 0x14, 16);
    int charLevel = data[0x2B];

    /* Find "gf" stats section */
    int gfPos = -1;
    for (int i = 0; i < fileSize - 2; i++) {
        if (data[i] == 0x67 && data[i+1] == 0x66) {
            gfPos = i;
            break;
        }
    }
    if (gfPos < 0) {
        printf("  No stats section: %s\n", path);
        free(data);
        return 0;
    }

    /* Find "if" skills section */
    int ifPos = -1;
    for (int i = gfPos; i < fileSize - 32; i++) {
        if (data[i] == 0x69 && data[i+1] == 0x66) {
            ifPos = i;
            break;
        }
    }
    if (ifPos < 0) {
        printf("  No skills section: %s\n", path);
        free(data);
        return 0;
    }

    /* ---- Step 1: Count skill points in 30 bytes ---- */
    unsigned char *skills30 = data + ifPos + 2;
    unsigned char origSkills[30]; /* SAVE original values before zeroing */
    memcpy(origSkills, skills30, 30);
    int skillPoints = 0;
    for (int i = 0; i < 30; i++)
        skillPoints += skills30[i];

    /* ---- Step 2: Parse bitpacked stats to find NEWSKILLS ---- */
    StatEntry stats[MAX_STATS];
    int numStats = 0;
    int bitPos = (gfPos + 2) * 8; /* after "gf" header */

    while (numStats < MAX_STATS) {
        int statId = read_bits(data, fileSize, bitPos, 9);
        bitPos += 9;
        if (statId == 0x1FF) break; /* terminator */

        int bits = GetStatBits(statId);
        if (bits == 0) {
            printf("  Unknown stat %d at bit %d, aborting\n", statId, bitPos);
            free(data);
            return 0;
        }

        int value = read_bits(data, fileSize, bitPos, bits);
        bitPos += bits;

        stats[numStats].id = statId;
        stats[numStats].value = value;
        numStats++;
    }

    int statsSectionEndBit = bitPos; /* position of terminator's end (after 9 bits of 0x1FF) */

    /* Find current NEWSKILLS value */
    int poolValue = 0;
    int poolIdx = -1;
    for (int i = 0; i < numStats; i++) {
        if (stats[i].id == 5) {
            poolValue = stats[i].value;
            poolIdx = i;
            break;
        }
    }

    /* ---- Step 3: Calculate new pool ---- */
    int newPool = skillPoints + poolValue;

    printf("  %s (Lv%d): skills=%d + pool=%d = %d total\n",
           charName, charLevel, skillPoints, poolValue, newPool);

    if (newPool == poolValue && skillPoints == 0) {
        printf("  Nothing to reset\n");
        free(data);
        return 0;
    }

    /* ---- Step 4: Zero all 30 skill bytes ---- */
    memset(skills30, 0, 30);

    /* ---- Step 5: Write new NEWSKILLS into bitpacked stats ---- */
    if (newPool > 0) {
        if (poolIdx >= 0) {
            /* Stat 5 exists — just update the value in place */
            stats[poolIdx].value = newPool;
        } else {
            /* Stat 5 doesn't exist — insert it */
            /* Insert after stat 4 (statpts) or at the end */
            int insertAt = numStats; /* default: end */
            for (int i = 0; i < numStats; i++) {
                if (stats[i].id > 5) {
                    insertAt = i;
                    break;
                }
            }
            /* Shift stats after insertAt */
            for (int i = numStats; i > insertAt; i--)
                stats[i] = stats[i-1];
            stats[insertAt].id = 5;
            stats[insertAt].value = newPool;
            numStats++;
        }
    }
    /* If newPool == 0 and stat existed, remove it */
    if (newPool == 0 && poolIdx >= 0) {
        for (int i = poolIdx; i < numStats - 1; i++)
            stats[i] = stats[i+1];
        numStats--;
    }

    /* ---- Step 6: Re-encode the entire stats section ---- */
    /* Calculate new stats section size in bits */
    int newStatsBits = 0;
    for (int i = 0; i < numStats; i++) {
        newStatsBits += 9 + GetStatBits(stats[i].id);
    }
    newStatsBits += 9; /* terminator (0x1FF) */

    int newStatsSectionBytes = (newStatsBits + 7) / 8;
    int oldStatsSectionBytes = (statsSectionEndBit + 7) / 8 - (gfPos + 2);
    int byteDiff = newStatsSectionBytes - oldStatsSectionBytes;

    /* Create new file buffer */
    long newFileSize = fileSize + byteDiff;
    unsigned char *newData = (unsigned char*)calloc(newFileSize + 64, 1);
    if (!newData) { free(data); return 0; }

    /* Copy header up to stats section start (gfPos + 2) */
    int statsStart = gfPos + 2;
    memcpy(newData, data, statsStart);

    /* Write new bitpacked stats */
    int wBit = statsStart * 8;
    for (int i = 0; i < numStats; i++) {
        write_bits(newData, newFileSize, wBit, 9, stats[i].id);
        wBit += 9;
        int bits = GetStatBits(stats[i].id);
        write_bits(newData, newFileSize, wBit, bits, stats[i].value);
        wBit += bits;
    }
    /* Write terminator */
    write_bits(newData, newFileSize, wBit, 9, 0x1FF);
    wBit += 9;

    /* Pad remaining bits in last byte to 0 (already calloc'd) */
    int newStatsEnd = statsStart + newStatsSectionBytes;

    /* Copy everything after old stats section */
    int oldStatsEnd = statsStart + oldStatsSectionBytes;
    int remainingBytes = fileSize - oldStatsEnd;
    if (remainingBytes > 0) {
        memcpy(newData + newStatsEnd, data + oldStatsEnd, remainingBytes);
    }

    /* Update file size in d2s header (offset 0x08, little-endian DWORD) */
    newData[0x08] = (newFileSize) & 0xFF;
    newData[0x09] = (newFileSize >> 8) & 0xFF;
    newData[0x0A] = (newFileSize >> 16) & 0xFF;
    newData[0x0B] = (newFileSize >> 24) & 0xFF;

    /* Recalculate checksum */
    newData[0x0C] = 0; newData[0x0D] = 0; newData[0x0E] = 0; newData[0x0F] = 0;
    unsigned int ck = CalcChecksum(newData, newFileSize);
    newData[0x0C] = (ck) & 0xFF;
    newData[0x0D] = (ck >> 8) & 0xFF;
    newData[0x0E] = (ck >> 16) & 0xFF;
    newData[0x0F] = (ck >> 24) & 0xFF;

    /* Write to disk */
    f = fopen(path, "wb");
    if (!f) {
        printf("  Cannot write: %s\n", path);
        free(data); free(newData);
        return 0;
    }
    fwrite(newData, 1, newFileSize, f);
    fclose(f);

    printf("  Reset: %d points -> pool (was %d, now %d)\n", skillPoints, poolValue, newPool);

    /* ---- Step 7: Write restore file with slot positions ---- */
    if (skillPoints > 0) {
        /* Build restore file path: same dir, charactername.d2restore */
        char restorePath[MAX_PATH];
        strncpy(restorePath, path, MAX_PATH - 20);
        /* Replace .d2s with .d2restore */
        char *ext = strstr(restorePath, ".d2s");
        if (ext) strcpy(ext, ".d2restore");
        else strcat(restorePath, ".d2restore");

        FILE *rf = fopen(restorePath, "w");
        if (rf) {
            for (int i = 0; i < 30; i++) {
                /* Use ORIGINAL skill bytes (before we zeroed them) */
                int pts = origSkills[i]; /* saved BEFORE zeroing */
                if (pts > 0) {
                    fprintf(rf, "slot=%d,%d\n", i, pts);
                    printf("    slot %d: %d points\n", i, pts);
                }
            }
            fclose(rf);
            printf("  Wrote restore: %s\n", restorePath);
        }
    }

    free(data);
    free(newData);
    return skillPoints;
}

int main(int argc, char *argv[]) {
    const char *saveDir;
    if (argc >= 2) saveDir = argv[1];
    else saveDir = "C:\\Program Files (x86)\\Diablo II\\Save";

    printf("D2 Skill Reset Tool v2\n");
    printf("Save directory: %s\n\n", saveDir);

    char searchPath[MAX_PATH];
    snprintf(searchPath, MAX_PATH, "%s\\*.d2s", saveDir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("No .d2s files found in %s\n", saveDir);
        return 0;
    }

    int totalReset = 0, filesProcessed = 0;
    do {
        char fullPath[MAX_PATH];
        snprintf(fullPath, MAX_PATH, "%s\\%s", saveDir, fd.cFileName);
        int pts = ProcessFile(fullPath);
        totalReset += pts;
        filesProcessed++;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);

    printf("\nDone: %d files, %d skill points reset\n", filesProcessed, totalReset);
    return 0;
}
