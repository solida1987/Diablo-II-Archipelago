/*
 * D2MpqGen.exe — Generates a patch_d2.mpq with randomized skill data.
 * Reads vanilla Skills.txt, SkillDesc.txt, CharStats.txt, randomizes them,
 * and packs the modified files into an MPQ archive using SFMPQ.dll.
 *
 * Usage: D2MpqGen.exe [--seed N] [--skills N] [--mpq path]
 *   --seed N     Random seed (0 = random)
 *   --skills N   Number of starting skills (default: 6)
 *   --mpq path   Output MPQ path (default: patch_d2.mpq in game dir)
 *
 * Build: cl /nologo /MT /O2 d2mpqgen.c /Fe:D2MpqGen.exe /link kernel32.lib
 * Note: Must be compiled as 32-bit (x86) since SFMPQ.dll is 32-bit.
 */
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ===== Configuration ===== */
#define MAX_COLS        300
#define MAX_ROWS        400
#define MAX_CELL_LEN    256
#define MAX_SKILLS      210
#define SKILLS_PER_CLASS 30
#define NUM_CLASSES     7

/* ===== Tab-Delimited Table ===== */
typedef struct {
    char headers[MAX_COLS][MAX_CELL_LEN];
    char cells[MAX_ROWS][MAX_COLS][MAX_CELL_LEN];
    int numCols;
    int numRows;
} D2Table;

static int FindColumn(D2Table* t, const char* name)
{
    for (int i = 0; i < t->numCols; i++) {
        if (_stricmp(t->headers[i], name) == 0)
            return i;
    }
    return -1;
}

static const char* GetCell(D2Table* t, int row, const char* col)
{
    int c = FindColumn(t, col);
    if (c < 0 || row < 0 || row >= t->numRows) return "";
    return t->cells[row][c];
}

static void SetCell(D2Table* t, int row, const char* col, const char* value)
{
    int c = FindColumn(t, col);
    if (c < 0 || row < 0 || row >= t->numRows) return;
    strncpy(t->cells[row][c], value, MAX_CELL_LEN - 1);
    t->cells[row][c][MAX_CELL_LEN - 1] = '\0';
}

static BOOL LoadTable(D2Table* t, const char* filepath)
{
    FILE* f = fopen(filepath, "r");
    if (!f) {
        printf("  [ERROR] Cannot open: %s\n", filepath);
        return FALSE;
    }

    char line[65536];
    t->numCols = 0;
    t->numRows = 0;

    /* Read header line */
    if (!fgets(line, sizeof(line), f)) { fclose(f); return FALSE; }

    /* Remove trailing newline/CR */
    int len = (int)strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';

    /* Split by tabs */
    char* tok = line;
    char* tab;
    while ((tab = strchr(tok, '\t')) != NULL && t->numCols < MAX_COLS) {
        *tab = '\0';
        strncpy(t->headers[t->numCols], tok, MAX_CELL_LEN - 1);
        t->numCols++;
        tok = tab + 1;
    }
    if (t->numCols < MAX_COLS) {
        strncpy(t->headers[t->numCols], tok, MAX_CELL_LEN - 1);
        t->numCols++;
    }

    /* Read data rows */
    while (fgets(line, sizeof(line), f) && t->numRows < MAX_ROWS) {
        len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';

        tok = line;
        int col = 0;
        while ((tab = strchr(tok, '\t')) != NULL && col < t->numCols) {
            *tab = '\0';
            strncpy(t->cells[t->numRows][col], tok, MAX_CELL_LEN - 1);
            col++;
            tok = tab + 1;
        }
        if (col < t->numCols) {
            strncpy(t->cells[t->numRows][col], tok, MAX_CELL_LEN - 1);
            col++;
        }
        /* Fill remaining columns with empty */
        while (col < t->numCols) {
            t->cells[t->numRows][col][0] = '\0';
            col++;
        }
        t->numRows++;
    }

    fclose(f);
    return TRUE;
}

static BOOL SaveTable(D2Table* t, const char* filepath)
{
    FILE* f = fopen(filepath, "wb"); /* binary mode for exact \r\n */
    if (!f) return FALSE;

    /* Write headers */
    for (int c = 0; c < t->numCols; c++) {
        if (c > 0) fputc('\t', f);
        fputs(t->headers[c], f);
    }
    fputs("\r\n", f);

    /* Write rows */
    for (int r = 0; r < t->numRows; r++) {
        for (int c = 0; c < t->numCols; c++) {
            if (c > 0) fputc('\t', f);
            fputs(t->cells[r][c], f);
        }
        fputs("\r\n", f);
    }

    fclose(f);
    return TRUE;
}

/* ===== Skill Randomizer ===== */

static const char* CLASS_CODES[NUM_CLASSES] = {"ama", "sor", "nec", "pal", "bar", "dru", "ass"};
static const char* CLASS_NAMES[NUM_CLASSES] = {"Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin"};

typedef struct {
    int row;         /* Row index in Skills table */
    int classIdx;    /* Original class index (0-6) */
    char name[64];   /* Skill name */
    char desc[64];   /* SkillDesc name */
} SkillInfo;

static SkillInfo g_allSkills[MAX_SKILLS];
static int g_numSkills = 0;
static int g_indices[MAX_SKILLS]; /* Shuffled skill indices, set by RandomizeSkills */

/* Fisher-Yates shuffle */
static void Shuffle(int* arr, int n, unsigned int* seed)
{
    for (int i = n - 1; i > 0; i--) {
        *seed = *seed * 1103515245 + 12345;
        int j = (int)((*seed >> 16) % (i + 1));
        int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

static void RandomizeSkills(D2Table* skills, D2Table* skilldesc, D2Table* charstats,
                            int numStarting, unsigned int seed)
{
    int classCol = FindColumn(skills, "charclass");
    int skillCol = FindColumn(skills, "skill");
    int descCol  = FindColumn(skills, "skilldesc");

    if (classCol < 0 || skillCol < 0) {
        printf("  [ERROR] Skills.txt missing required columns\n");
        return;
    }

    /* Collect all class skills */
    g_numSkills = 0;
    for (int r = 0; r < skills->numRows; r++) {
        const char* cc = skills->cells[r][classCol];
        if (cc[0] == '\0') continue;

        for (int ci = 0; ci < NUM_CLASSES; ci++) {
            if (_stricmp(cc, CLASS_CODES[ci]) == 0) {
                g_allSkills[g_numSkills].row = r;
                g_allSkills[g_numSkills].classIdx = ci;
                strncpy(g_allSkills[g_numSkills].name, skills->cells[r][skillCol], 63);
                if (descCol >= 0)
                    strncpy(g_allSkills[g_numSkills].desc, skills->cells[r][descCol], 63);
                g_numSkills++;
                break;
            }
        }
    }

    printf("  Found %d class skills\n", g_numSkills);

    /* Create shuffled indices (stored in global for state file) */
    for (int i = 0; i < g_numSkills; i++) g_indices[i] = i;
    Shuffle(g_indices, g_numSkills, &seed);

    /* First 'numStarting' are the starting skills (assigned to ALL classes) */
    /* All 210 skills get redistributed: 30 per class */
    /* Each class gets the same set of 30 skills (just reassigned charclass) */
    printf("  Starting skills (%d):\n", numStarting);
    for (int i = 0; i < numStarting && i < g_numSkills; i++) {
        printf("    %d. %s\n", i + 1, g_allSkills[g_indices[i]].name);
    }

    /* KEEP original charclass — preserves correct icons per class.
     * Skills are granted at runtime via stat 97 (ITEM_NONCLASSSKILL)
     * which works cross-class like Enigma's Teleport.
     * Clear prerequisites, set reqlevel=1, enable left-click. */
    for (int i = 0; i < g_numSkills; i++) {
        int skillIdx = g_indices[i];
        int row = g_allSkills[skillIdx].row;

        /* DO NOT change charclass — keep original for correct icons */

        /* Remove all prerequisites and level requirements */
        SetCell(skills, row, "reqlevel", "1");
        SetCell(skills, row, "reqskill1", "");
        SetCell(skills, row, "reqskill2", "");
        SetCell(skills, row, "reqskill3", "");

        /* Enable left-click for all skills */
        SetCell(skills, row, "leftskill", "1");
    }

    /* Assign SkillDesc grid positions */
    /* Each class gets 30 skills: 3 pages x 10 skills */
    /* Positions: Page 1-3, Row 1-6, Col 1-3 (10 per page) */
    int sdDescCol = FindColumn(skilldesc, "skilldesc");
    int sdPageCol = FindColumn(skilldesc, "SkillPage");
    int sdRowCol  = FindColumn(skilldesc, "SkillRow");
    int sdColCol  = FindColumn(skilldesc, "SkillColumn");
    int sdListCol = FindColumn(skilldesc, "ListRow");
    int sdPoolCol = FindColumn(skilldesc, "ListPool");

    if (sdDescCol >= 0 && sdPageCol >= 0 && sdRowCol >= 0 && sdColCol >= 0) {
        /* Pre-compute grid positions: 10 per page, 3 pages */
        int gridPage[30], gridRow[30], gridCol[30];
        int pos = 0;
        for (int page = 1; page <= 3; page++) {
            for (int row = 1; row <= 6 && pos < 30; row++) {
                for (int col = 1; col <= 3 && pos < 30; col++) {
                    gridPage[pos] = page;
                    gridRow[pos] = row;
                    gridCol[pos] = col;
                    pos++;
                    if (pos % 10 == 0) goto nextPage;
                }
            }
            nextPage:;
        }

        /* For each class, find its 30 skills and assign grid positions */
        for (int ci = 0; ci < NUM_CLASSES; ci++) {
            int classSkills[SKILLS_PER_CLASS];
            int count = 0;

            /* Find all skills assigned to this class */
            for (int i = 0; i < g_numSkills; i++) {
                int skillIdx = g_indices[i];
                int targetClass = i / SKILLS_PER_CLASS;
                if (targetClass == ci) {
                    classSkills[count++] = skillIdx;
                    if (count >= SKILLS_PER_CLASS) break;
                }
            }

            /* Hide ALL skills from vanilla skill tree by setting SkillPage=0.
             * Our custom skill tree panel handles display instead. */
            for (int s = 0; s < count; s++) {
                const char* descName = g_allSkills[classSkills[s]].desc;
                if (descName[0] == '\0') continue;

                for (int sdr = 0; sdr < skilldesc->numRows; sdr++) {
                    if (_stricmp(skilldesc->cells[sdr][sdDescCol], descName) == 0) {
                        strncpy(skilldesc->cells[sdr][sdPageCol], "0", MAX_CELL_LEN - 1);
                        strncpy(skilldesc->cells[sdr][sdRowCol], "0", MAX_CELL_LEN - 1);
                        strncpy(skilldesc->cells[sdr][sdColCol], "0", MAX_CELL_LEN - 1);
                        if (sdListCol >= 0)
                            strncpy(skilldesc->cells[sdr][sdListCol], "0", MAX_CELL_LEN - 1);
                        if (sdPoolCol >= 0)
                            strncpy(skilldesc->cells[sdr][sdPoolCol], "0", MAX_CELL_LEN - 1);
                        break;
                    }
                }
            }
        }
    }

    /* Update CharStats.txt - set starting skills for each class */
    int csClassCol = FindColumn(charstats, "class");
    if (csClassCol >= 0) {
        for (int ci = 0; ci < NUM_CLASSES; ci++) {
            /* Find CharStats row for this class */
            for (int r = 0; r < charstats->numRows; r++) {
                if (_stricmp(charstats->cells[r][csClassCol], CLASS_NAMES[ci]) == 0) {
                    /* Clear StartSkill - no skills at game start */
                    SetCell(charstats, r, "StartSkill", "");

                    /* NO class skills at start - only basic utility.
                     * Skills are unlocked via the check/reward system. */
                    int written = 0;
                    const char* utility[] = {"Throw", "Kick", "Book of Townportal", "Book of Identify"};
                    for (int u = 0; u < 4; u++, written++) {
                        char key[16];
                        sprintf(key, "Skill %d", written + 1);
                        SetCell(charstats, r, key, utility[u]);
                    }
                    /* Clear remaining skill slots */
                    for (; written < 10; written++) {
                        char key[16];
                        sprintf(key, "Skill %d", written + 1);
                        SetCell(charstats, r, key, "");
                    }
                    break;
                }
            }
        }
    }

    printf("  Skills randomized successfully (seed=%u)\n", seed);
}

/* ===== Main ===== */

int main(int argc, char* argv[])
{
    unsigned int seed = 0;
    int numSkills = 6;
    char gameDir[MAX_PATH] = "";
    char vanillaDir[MAX_PATH] = "";

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== D2MpqGen - Diablo II Archipelago Skill Randomizer ===\n\n");

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (unsigned int)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--skills") == 0 && i + 1 < argc) {
            numSkills = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--gamedir") == 0 && i + 1 < argc) {
            strncpy(gameDir, argv[++i], MAX_PATH - 1);
        } else if (strcmp(argv[i], "--vanilla") == 0 && i + 1 < argc) {
            strncpy(vanillaDir, argv[++i], MAX_PATH - 1);
        }
    }

    /* Default seed = time-based */
    if (seed == 0) {
        seed = (unsigned int)time(NULL);
    }

    /* Default paths - relative to exe location */
    if (vanillaDir[0] == '\0') {
        GetModuleFileNameA(NULL, vanillaDir, MAX_PATH);
        char* lastSlash = strrchr(vanillaDir, '\\');
        if (lastSlash) *lastSlash = '\0';
        strncat(vanillaDir, "\\launcher\\data\\vanilla_txt", MAX_PATH - strlen(vanillaDir) - 1);
    }

    if (gameDir[0] == '\0') {
        /* Default: game directory = parent of Archipelago folder */
        GetModuleFileNameA(NULL, gameDir, MAX_PATH);
        char* lastSlash = strrchr(gameDir, '\\');
        if (lastSlash) *lastSlash = '\0';
        lastSlash = strrchr(gameDir, '\\');
        if (lastSlash) *lastSlash = '\0';
    }

    /* Create output directory: data/global/excel/ in game dir */
    char excelDir[MAX_PATH];
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "%s\\data", gameDir);
    CreateDirectoryA(path, NULL);
    snprintf(path, MAX_PATH, "%s\\data\\global", gameDir);
    CreateDirectoryA(path, NULL);
    snprintf(excelDir, MAX_PATH, "%s\\data\\global\\excel", gameDir);
    CreateDirectoryA(excelDir, NULL);

    printf("  Seed: %u\n", seed);
    printf("  Starting skills: %d\n", numSkills);
    printf("  Vanilla dir: %s\n", vanillaDir);
    printf("  Output dir: %s\n", excelDir);
    printf("\n");

    /* Load vanilla tables */
    static D2Table skills, skilldesc, charstats;

    snprintf(path, MAX_PATH, "%s\\Skills.txt", vanillaDir);
    printf("  Loading Skills.txt...\n");
    if (!LoadTable(&skills, path)) return 1;
    printf("  Loaded: %d rows x %d cols\n", skills.numRows, skills.numCols);

    snprintf(path, MAX_PATH, "%s\\SkillDesc.txt", vanillaDir);
    printf("  Loading SkillDesc.txt...\n");
    if (!LoadTable(&skilldesc, path)) return 1;
    printf("  Loaded: %d rows x %d cols\n", skilldesc.numRows, skilldesc.numCols);

    snprintf(path, MAX_PATH, "%s\\CharStats.txt", vanillaDir);
    printf("  Loading CharStats.txt...\n");
    if (!LoadTable(&charstats, path)) return 1;
    printf("  Loaded: %d rows x %d cols\n", charstats.numRows, charstats.numCols);

    printf("\n  Randomizing skills...\n");
    RandomizeSkills(&skills, &skilldesc, &charstats, numSkills, seed);

    /* Save modified tables directly to data/global/excel/
     * NewTxt.dll (D2Mod plugin) makes the game load these automatically */
    printf("\n  Writing modified txt files...\n");
    const char* txtFiles[] = {"Skills.txt", "SkillDesc.txt", "CharStats.txt"};
    D2Table* tables[] = {&skills, &skilldesc, &charstats};

    for (int i = 0; i < 3; i++) {
        snprintf(path, MAX_PATH, "%s\\%s", excelDir, txtFiles[i]);
        if (SaveTable(tables[i], path)) {
            printf("  [OK] %s\n", txtFiles[i]);
        } else {
            printf("  [ERROR] Failed to write %s\n", txtFiles[i]);
        }
    }

    /* Save state file for in-game DLL */
    snprintf(path, MAX_PATH, "%s\\Archipelago\\d2arch_state.dat", gameDir);
    FILE* sf = fopen(path, "w");
    if (sf) {
        fprintf(sf, "seed=%u\n", seed);
        fprintf(sf, "num_starting=%d\n", numSkills);
        fprintf(sf, "total_skills=%d\n", g_numSkills);
        fprintf(sf, "starting_skills=");
        for (int i = 0; i < numSkills && i < g_numSkills; i++) {
            if (i > 0) fprintf(sf, ",");
            fprintf(sf, "%s", g_allSkills[g_indices[i]].name);
        }
        fprintf(sf, "\n");
        fprintf(sf, "assignments=\n");
        for (int i = 0; i < g_numSkills; i++) {
            int si = g_indices[i];
            /* Use ORIGINAL class (for correct icons), not reassigned class */
            int origClass = g_allSkills[si].classIdx;
            /* Format: name,originalClass,isStarting,skillId */
            fprintf(sf, "%s,%s,%d,%d\n",
                g_allSkills[si].name,
                CLASS_CODES[origClass],
                i < numSkills ? 1 : 0,
                g_allSkills[si].row);
        }
        fclose(sf);
        printf("\n  State saved to: %s\n", path);
    }

    printf("\n  === SUCCESS ===\n");
    printf("  Modified txt files in: %s\n", excelDir);
    printf("  NewTxt.dll will load them automatically (no -direct -txt needed).\n");
    printf("  Seed: %u\n", seed);
    printf("  Start Game.exe to play with randomized skills.\n");

    return 0;
}
