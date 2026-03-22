/*
 * Diablo II Archipelago - Launcher
 * Multi-page launcher: Main Menu, New Character, Continue.
 * Creates .d2s files from templates, generates skill randomizer state files,
 * isolates characters, and launches Game.exe with -direct -txt.
 */
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

/* Templates are now copied from Archipelago/acc Temp/ folder as real files */

/* ================================================================
 * CONSTANTS
 * ================================================================ */
#define WND_W       500
#define WND_H       500
#define HEADER_H    55
#define PAD         24

#define PAGE_MAIN       0
#define PAGE_NEWCHAR    1
#define PAGE_CONTINUE   2

/* Control IDs */
#define ID_BTN_NEWCHAR      101
#define ID_BTN_CONTINUE     102
#define ID_BTN_QUIT         103
#define ID_EDIT_NAME        110
#define ID_COMBO_CLASS      111
#define ID_EDIT_SEED        112
#define ID_RADIO_WIN_NEW    113
#define ID_RADIO_FS_NEW     114
#define ID_BTN_CREATE       115
#define ID_BTN_BACK_NEW     116
#define ID_LIST_CHARS       120
#define ID_RADIO_WIN_CONT   121
#define ID_RADIO_FS_CONT    122
#define ID_BTN_PLAY         123
#define ID_BTN_BACK_CONT    124
#define ID_BTN_DELETE       125

/* ================================================================
 * SKILL DATABASE — all 210 skills
 * ================================================================ */
typedef struct {
    int id;
    const char *name;
    const char *cls;
    int tier;
} SkillEntry;

static const SkillEntry ALL_SKILLS[210] = {
    /* Amazon T1 (10) */
    {  6, "Magic Arrow",      "ama", 1}, {  7, "Fire Arrow",       "ama", 1},
    {  8, "Inner Sight",      "ama", 1}, {  9, "Critical Strike",  "ama", 1},
    { 10, "Jab",              "ama", 1}, { 11, "Cold Arrow",       "ama", 1},
    { 12, "Multiple Shot",    "ama", 1}, { 13, "Dodge",            "ama", 1},
    { 14, "Power Strike",     "ama", 1}, { 15, "Poison Javelin",   "ama", 1},
    /* Amazon T2 (10) */
    { 16, "Exploding Arrow",  "ama", 2}, { 17, "Slow Missiles",    "ama", 2},
    { 18, "Avoid",            "ama", 2}, { 19, "Impale",           "ama", 2},
    { 20, "Lightning Bolt",   "ama", 2}, { 21, "Ice Arrow",        "ama", 2},
    { 22, "Guided Arrow",     "ama", 2}, { 23, "Penetrate",        "ama", 2},
    { 24, "Charged Strike",   "ama", 2}, { 25, "Plague Javelin",   "ama", 2},
    /* Amazon T3 (10) */
    { 26, "Strafe",           "ama", 3}, { 27, "Immolation Arrow", "ama", 3},
    { 28, "Decoy",            "ama", 3}, { 29, "Evade",            "ama", 3},
    { 30, "Fend",             "ama", 3}, { 31, "Freezing Arrow",   "ama", 3},
    { 32, "Valkyrie",         "ama", 3}, { 33, "Pierce",           "ama", 3},
    { 34, "Lightning Strike", "ama", 3}, { 35, "Lightning Fury",   "ama", 3},
    /* Sorceress T1 (10) */
    { 36, "Fire Bolt",        "sor", 1}, { 37, "Warmth",           "sor", 1},
    { 38, "Charged Bolt",     "sor", 1}, { 39, "Ice Bolt",         "sor", 1},
    { 40, "Frozen Armor",     "sor", 1}, { 41, "Inferno",          "sor", 1},
    { 42, "Static Field",     "sor", 1}, { 43, "Telekinesis",      "sor", 1},
    { 44, "Frost Nova",       "sor", 1}, { 45, "Ice Blast",        "sor", 1},
    /* Sorceress T2 (10) */
    { 46, "Blaze",            "sor", 2}, { 47, "Fire Ball",        "sor", 2},
    { 48, "Nova",             "sor", 2}, { 49, "Lightning",        "sor", 2},
    { 50, "Shiver Armor",     "sor", 2}, { 51, "Fire Wall",        "sor", 2},
    { 52, "Enchant",          "sor", 2}, { 53, "Chain Lightning",  "sor", 2},
    { 54, "Teleport",         "sor", 2}, { 55, "Glacial Spike",    "sor", 2},
    /* Sorceress T3 (10) */
    { 56, "Meteor",           "sor", 3}, { 57, "Thunder Storm",    "sor", 3},
    { 58, "Energy Shield",    "sor", 3}, { 59, "Blizzard",         "sor", 3},
    { 60, "Chilling Armor",   "sor", 3}, { 61, "Fire Mastery",     "sor", 3},
    { 62, "Hydra",            "sor", 3}, { 63, "Lightning Mastery","sor", 3},
    { 64, "Frozen Orb",       "sor", 3}, { 65, "Cold Mastery",     "sor", 3},
    /* Necromancer T1 (10) */
    { 66, "Amplify Damage",   "nec", 1}, { 67, "Teeth",            "nec", 1},
    { 68, "Bone Armor",       "nec", 1}, { 69, "Skeleton Mastery", "nec", 1},
    { 70, "Raise Skeleton",   "nec", 1}, { 71, "Dim Vision",       "nec", 1},
    { 72, "Weaken",           "nec", 1}, { 73, "Poison Dagger",    "nec", 1},
    { 74, "Corpse Explosion", "nec", 1}, { 75, "Clay Golem",       "nec", 1},
    /* Necromancer T2 (10) */
    { 76, "Iron Maiden",      "nec", 2}, { 77, "Terror",           "nec", 2},
    { 78, "Bone Wall",        "nec", 2}, { 79, "Golem Mastery",    "nec", 2},
    { 80, "Raise Skeletal Mage","nec",2}, { 81, "Confuse",          "nec", 2},
    { 82, "Life Tap",         "nec", 2}, { 83, "Poison Explosion", "nec", 2},
    { 84, "Bone Spear",       "nec", 2}, { 85, "Blood Golem",      "nec", 2},
    /* Necromancer T3 (10) */
    { 86, "Attract",          "nec", 3}, { 87, "Decrepify",        "nec", 3},
    { 88, "Bone Prison",      "nec", 3}, { 89, "Summon Resist",    "nec", 3},
    { 90, "Iron Golem",       "nec", 3}, { 91, "Lower Resist",     "nec", 3},
    { 92, "Poison Nova",      "nec", 3}, { 93, "Bone Spirit",      "nec", 3},
    { 94, "Fire Golem",       "nec", 3}, { 95, "Revive",           "nec", 3},
    /* Paladin T1 (10) */
    { 96, "Sacrifice",        "pal", 1}, { 97, "Smite",            "pal", 1},
    { 98, "Might",            "pal", 1}, { 99, "Prayer",           "pal", 1},
    {100, "Resist Fire",      "pal", 1}, {101, "Holy Bolt",        "pal", 1},
    {102, "Holy Fire",        "pal", 1}, {103, "Thorns",           "pal", 1},
    {104, "Defiance",         "pal", 1}, {105, "Resist Cold",      "pal", 1},
    /* Paladin T2 (10) */
    {106, "Zeal",             "pal", 2}, {107, "Charge",           "pal", 2},
    {108, "Blessed Aim",      "pal", 2}, {109, "Cleansing",        "pal", 2},
    {110, "Resist Lightning", "pal", 2}, {111, "Vengeance",        "pal", 2},
    {112, "Blessed Hammer",   "pal", 2}, {113, "Concentration",    "pal", 2},
    {114, "Holy Freeze",      "pal", 2}, {115, "Vigor",            "pal", 2},
    /* Paladin T3 (10) */
    {116, "Conversion",       "pal", 3}, {117, "Holy Shield",      "pal", 3},
    {118, "Holy Shock",       "pal", 3}, {119, "Sanctuary",        "pal", 3},
    {120, "Meditation",       "pal", 3}, {121, "Fist of the Heavens","pal",3},
    {122, "Fanaticism",       "pal", 3}, {123, "Conviction",       "pal", 3},
    {124, "Redemption",       "pal", 3}, {125, "Salvation",        "pal", 3},
    /* Barbarian T1 (13) */
    {126, "Bash",             "bar", 1}, {127, "Sword Mastery",    "bar", 1},
    {128, "Axe Mastery",      "bar", 1}, {129, "Mace Mastery",     "bar", 1},
    {130, "Howl",             "bar", 1}, {131, "Find Potion",      "bar", 1},
    {132, "Leap",             "bar", 1}, {133, "Double Swing",     "bar", 1},
    {134, "Pole Arm Mastery", "bar", 1}, {135, "Throwing Mastery", "bar", 1},
    {136, "Spear Mastery",    "bar", 1}, {137, "Taunt",            "bar", 1},
    {138, "Shout",            "bar", 1},
    /* Barbarian T2 (8) */
    {139, "Stun",             "bar", 2}, {140, "Double Throw",     "bar", 2},
    {141, "Increased Stamina","bar", 2}, {142, "Find Item",        "bar", 2},
    {143, "Leap Attack",      "bar", 2}, {144, "Concentrate",      "bar", 2},
    {145, "Iron Skin",        "bar", 2}, {146, "Battle Cry",       "bar", 2},
    /* Barbarian T3 (9) */
    {147, "Frenzy",           "bar", 3}, {148, "Increased Speed",  "bar", 3},
    {149, "Battle Orders",    "bar", 3}, {150, "Grim Ward",        "bar", 3},
    {151, "Whirlwind",        "bar", 3}, {152, "Berserk",          "bar", 3},
    {153, "Natural Resistance","bar",3}, {154, "War Cry",          "bar", 3},
    {155, "Battle Command",   "bar", 3},
    /* Druid T1 (10) */
    {221, "Raven",            "dru", 1}, {222, "Poison Creeper",   "dru", 1},
    {223, "Werewolf",         "dru", 1}, {224, "Lycanthropy",      "dru", 1},
    {225, "Firestorm",        "dru", 1}, {226, "Oak Sage",         "dru", 1},
    {227, "Summon Spirit Wolf","dru",1}, {228, "Werebear",         "dru", 1},
    {229, "Molten Boulder",   "dru", 1}, {230, "Arctic Blast",     "dru", 1},
    /* Druid T2 (10) */
    {231, "Carrion Vine",     "dru", 2}, {232, "Feral Rage",       "dru", 2},
    {233, "Maul",             "dru", 2}, {234, "Fissure",          "dru", 2},
    {235, "Cyclone Armor",    "dru", 2}, {236, "Heart of Wolverine","dru",2},
    {237, "Summon Dire Wolf",  "dru",2}, {238, "Rabies",           "dru", 2},
    {239, "Fire Claws",       "dru", 2}, {240, "Twister",          "dru", 2},
    /* Druid T3 (10) */
    {241, "Solar Creeper",    "dru", 3}, {242, "Hunger",           "dru", 3},
    {243, "Shock Wave",       "dru", 3}, {244, "Volcano",          "dru", 3},
    {245, "Tornado",          "dru", 3}, {246, "Spirit of Barbs",  "dru", 3},
    {247, "Summon Grizzly",   "dru", 3}, {248, "Fury",             "dru", 3},
    {249, "Armageddon",       "dru", 3}, {250, "Hurricane",        "dru", 3},
    /* Assassin T1 (10) */
    {251, "Fire Blast",       "ass", 1}, {252, "Claw Mastery",     "ass", 1},
    {253, "Psychic Hammer",   "ass", 1}, {254, "Tiger Strike",     "ass", 1},
    {255, "Dragon Talon",     "ass", 1}, {256, "Shock Web",        "ass", 1},
    {257, "Blade Sentinel",   "ass", 1}, {258, "Burst of Speed",   "ass", 1},
    {259, "Fists of Fire",    "ass", 1}, {260, "Dragon Claw",      "ass", 1},
    /* Assassin T2 (10) */
    {261, "Charged Bolt Sentry","ass",2},{262, "Wake of Fire",     "ass", 2},
    {263, "Weapon Block",     "ass", 2}, {264, "Cloak of Shadows", "ass", 2},
    {265, "Cobra Strike",     "ass", 2}, {266, "Blade Fury",       "ass", 2},
    {267, "Fade",             "ass", 2}, {268, "Shadow Warrior",   "ass", 2},
    {269, "Claws of Thunder", "ass", 2}, {270, "Dragon Tail",      "ass", 2},
    /* Assassin T3 (10) */
    {271, "Lightning Sentry", "ass", 3}, {272, "Inferno Sentry",   "ass", 3},
    {273, "Mind Blast",       "ass", 3}, {274, "Blades of Ice",    "ass", 3},
    {275, "Dragon Flight",    "ass", 3}, {276, "Death Sentry",     "ass", 3},
    {277, "Blade Shield",     "ass", 3}, {278, "Venom",            "ass", 3},
    {279, "Shadow Master",    "ass", 3}, {280, "Phoenix Strike",   "ass", 3},
};

static const char *CLASS_NAMES[] = {
    "Amazon", "Sorceress", "Necromancer", "Paladin",
    "Barbarian", "Druid", "Assassin"
};

static const char *CLASS_CODES[] = {
    "ama", "sor", "nec", "pal", "bar", "dru", "ass"
};

static const char *CLASS_TEMPLATES[] = {
    "Amazon", "Sorceress", "Necromancer", "Paladin",
    "Barbarian", "Druid", "Assassin"
};

/* ================================================================
 * GLOBALS
 * ================================================================ */
static HINSTANCE g_hInst;
static HWND g_hWnd;
static HFONT g_hFont;
static HFONT g_hFontBold;
static char g_gameRoot[MAX_PATH] = "";
static char g_saveDir[MAX_PATH] = "";
static char g_lastChar[64] = "";
static int g_displayMode = 0; /* 0=windowed, 1=fullscreen */
static int g_currentPage = PAGE_MAIN;

/* Page 0 controls */
static HWND g_hBtnNewChar, g_hBtnContinue, g_hBtnQuit;

/* Page 1 controls */
static HWND g_hLblNewChar;
static HWND g_hLblName, g_hEditName;
static HWND g_hLblClass, g_hComboClass;
static HWND g_hLblSeed, g_hEditSeed;
static HWND g_hRadioWinNew, g_hRadioFsNew;
static HWND g_hRadioStandalone, g_hRadioAP;
static HWND g_hLblServer, g_hEditServer;
static HWND g_hLblSlot, g_hEditSlot;
static HWND g_hLblPassword, g_hEditPassword;
static HWND g_hBtnCreate, g_hBtnBackNew;
static int g_apMode = 0; /* 0=standalone, 1=archipelago */
static char g_apServer[128] = "archipelago.gg:38281";
static char g_apSlot[64] = "";
static char g_apPassword[64] = "";

/* Page 2 controls */
static HWND g_hLblContinue;
static HWND g_hListChars;
static HWND g_hRadioWinCont, g_hRadioFsCont;
static HWND g_hBtnPlay, g_hBtnBackCont, g_hBtnDelete;

/* ================================================================
 * FORWARD DECLARATIONS
 * ================================================================ */
static void ShowPage(int page);
static void PopulateCharList(void);
static void RestoreHiddenChars(void);
static void IsolateCharacter(const char *charName);

/* ================================================================
 * CONFIG — Archipelago/launcher.cfg
 * ================================================================ */
static void GetConfigPath(char *out) {
    snprintf(out, MAX_PATH, "%sArchipelago\\launcher.cfg", g_gameRoot);
}

static void LoadConfig(void) {
    char path[MAX_PATH];
    GetConfigPath(path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_PATH + 32];
    while (fgets(line, sizeof(line), f)) {
        int val;
        if (sscanf(line, "display=%d", &val) == 1) {
            g_displayMode = val;
        }
        if (strncmp(line, "savedir=", 8) == 0) {
            char *p = line + 8;
            char *nl = strchr(p, '\n'); if (nl) *nl = 0;
            char *cr = strchr(p, '\r'); if (cr) *cr = 0;
            if (strlen(p) > 0) strncpy(g_saveDir, p, MAX_PATH - 1);
        }
        if (strncmp(line, "last_char=", 10) == 0) {
            char *p = line + 10;
            char *nl = strchr(p, '\n'); if (nl) *nl = 0;
            char *cr = strchr(p, '\r'); if (cr) *cr = 0;
            if (strlen(p) > 0) strncpy(g_lastChar, p, sizeof(g_lastChar) - 1);
        }
    }
    fclose(f);
}

static void SaveConfig(void) {
    char path[MAX_PATH];
    GetConfigPath(path);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "display=%d\n", g_displayMode);
    if (g_saveDir[0]) fprintf(f, "savedir=%s\n", g_saveDir);
    if (g_lastChar[0]) fprintf(f, "last_char=%s\n", g_lastChar);
    fclose(f);
}

/* ================================================================
 * DDRAW.INI — update windowed/fullscreen
 * ================================================================ */
static void UpdateDdrawIni(void) {
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "%sddraw.ini", g_gameRoot);

    FILE *f = fopen(path, "r");
    char lines[256][256];
    int numLines = 0;
    if (f) {
        while (numLines < 256 && fgets(lines[numLines], 256, f)) numLines++;
        fclose(f);
    }

    BOOL foundWindowed = FALSE, foundFullscreen = FALSE;
    for (int i = 0; i < numLines; i++) {
        if (strncmp(lines[i], "windowed=", 9) == 0) {
            snprintf(lines[i], 256, "windowed=%s\n", g_displayMode == 0 ? "true" : "false");
            foundWindowed = TRUE;
        }
        if (strncmp(lines[i], "fullscreen=", 11) == 0) {
            snprintf(lines[i], 256, "fullscreen=%s\n", g_displayMode == 1 ? "true" : "false");
            foundFullscreen = TRUE;
        }
    }

    if (numLines == 0) {
        snprintf(lines[numLines++], 256, "[ddraw]\n");
        snprintf(lines[numLines++], 256, "windowed=%s\n", g_displayMode == 0 ? "true" : "false");
        snprintf(lines[numLines++], 256, "fullscreen=%s\n", g_displayMode == 1 ? "true" : "false");
    } else {
        if (!foundWindowed && numLines < 255)
            snprintf(lines[numLines++], 256, "windowed=%s\n", g_displayMode == 0 ? "true" : "false");
        if (!foundFullscreen && numLines < 255)
            snprintf(lines[numLines++], 256, "fullscreen=%s\n", g_displayMode == 1 ? "true" : "false");
    }

    f = fopen(path, "w");
    if (f) {
        for (int i = 0; i < numLines; i++) fputs(lines[i], f);
        fclose(f);
    }
}

/* ================================================================
 * DETECT SAVE DIR
 * ================================================================ */
static void DetectSaveDir(void) {
    if (g_saveDir[0]) return;

    char test[MAX_PATH];
    snprintf(test, MAX_PATH, "C:\\Program Files (x86)\\Diablo II\\Save");
    if (GetFileAttributesA(test) != INVALID_FILE_ATTRIBUTES) {
        strncpy(g_saveDir, test, MAX_PATH - 1);
        return;
    }

    char *profile = getenv("USERPROFILE");
    if (profile) {
        snprintf(test, MAX_PATH, "%s\\Saved Games\\Diablo II", profile);
        if (GetFileAttributesA(test) != INVALID_FILE_ATTRIBUTES) {
            strncpy(g_saveDir, test, MAX_PATH - 1);
            return;
        }
    }

    /* Fallback to first found */
    snprintf(g_saveDir, MAX_PATH, "C:\\Program Files (x86)\\Diablo II\\Save");
}

/* ================================================================
 * DETECT GAME ROOT
 * ================================================================ */
static void DetectGameRoot(void) {
    GetModuleFileNameA(NULL, g_gameRoot, MAX_PATH);
    char *s = strrchr(g_gameRoot, '\\');
    if (s) *(s + 1) = 0;

    /* Check if Game.exe is in our directory */
    char test[MAX_PATH];
    snprintf(test, MAX_PATH, "%sGame.exe", g_gameRoot);
    if (GetFileAttributesA(test) != INVALID_FILE_ATTRIBUTES) return;

    /* Go up two levels from Archipelago/build/ */
    char *p = g_gameRoot + strlen(g_gameRoot) - 2;
    while (p > g_gameRoot && *p != '\\') p--;
    if (p > g_gameRoot) { p--; while (p > g_gameRoot && *p != '\\') p--; }
    if (p > g_gameRoot) *(p + 1) = 0;
}

/* ================================================================
 * CHARACTER ISOLATION
 * ================================================================ */
static void EnsureHiddenDir(void) {
    char dir[MAX_PATH];
    snprintf(dir, MAX_PATH, "%s\\_arch_hidden", g_saveDir);
    CreateDirectoryA(dir, NULL);
}

static void RestoreHiddenChars(void) {
    char pattern[MAX_PATH];
    snprintf(pattern, MAX_PATH, "%s\\_arch_hidden\\*", g_saveDir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char src[MAX_PATH], dst[MAX_PATH];
        snprintf(src, MAX_PATH, "%s\\_arch_hidden\\%s", g_saveDir, fd.cFileName);
        snprintf(dst, MAX_PATH, "%s\\%s", g_saveDir, fd.cFileName);
        MoveFileA(src, dst);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static void IsolateCharacter(const char *charName) {
    EnsureHiddenDir();

    /* Move all .d2s, .key, .ma0, .map files EXCEPT selected character */
    const char *exts[] = {"d2s", "key", "ma0", "map", NULL};
    for (int e = 0; exts[e]; e++) {
        char pattern[MAX_PATH];
        snprintf(pattern, MAX_PATH, "%s\\*.%s", g_saveDir, exts[e]);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            /* Check if this file belongs to our character */
            char nameOnly[MAX_PATH];
            strncpy(nameOnly, fd.cFileName, MAX_PATH - 1);
            nameOnly[MAX_PATH - 1] = 0;
            char *dot = strrchr(nameOnly, '.');
            if (dot) *dot = 0;

            if (_stricmp(nameOnly, charName) == 0) continue; /* keep this one */

            char src[MAX_PATH], dst[MAX_PATH];
            snprintf(src, MAX_PATH, "%s\\%s", g_saveDir, fd.cFileName);
            snprintf(dst, MAX_PATH, "%s\\_arch_hidden\\%s", g_saveDir, fd.cFileName);
            MoveFileA(src, dst);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
}

/* ================================================================
 * D2S FILE CREATION (template-based)
 * ================================================================ */
static const char *CLASS_TEMPLATE_NAMES[] = {
    "Amazon", "Sorceress", "Necromancer", "Paladin",
    "Barbarian", "Druid", "Assassin"
};

static BOOL CreateD2sFile(const char *name, int classId, unsigned int seed) {
    if (classId < 0 || classId > 6) return FALSE;

    CreateDirectoryA(g_saveDir, NULL);

    /* Template files are in Archipelago/acc Temp/ */
    const char *tplName = CLASS_TEMPLATE_NAMES[classId];
    const char *exts[] = {".d2s", ".key", ".ma0", ".map", NULL};

    for (int e = 0; exts[e]; e++) {
        char src[MAX_PATH], dst[MAX_PATH];
        snprintf(src, MAX_PATH, "%sArchipelago\\acc Temp\\%s%s", g_gameRoot, tplName, exts[e]);
        snprintf(dst, MAX_PATH, "%s\\%s%s", g_saveDir, name, exts[e]);
        CopyFileA(src, dst, FALSE);
    }

    /* Patch name inside .d2s (offset 0x14) + fix checksum */
    char d2sPath[MAX_PATH];
    snprintf(d2sPath, MAX_PATH, "%s\\%s.d2s", g_saveDir, name);

    FILE *f = fopen(d2sPath, "rb");
    if (!f) return FALSE;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc(sz);
    fread(buf, 1, sz, f);
    fclose(f);

    /* Set character name */
    memset(buf + 0x14, 0, 16);
    strncpy((char *)(buf + 0x14), name, 15);

    /* Recalculate checksum */
    buf[0x0C] = buf[0x0D] = buf[0x0E] = buf[0x0F] = 0;
    unsigned int ck = 0;
    for (long i = 0; i < sz; i++) {
        ck = ((ck << 1) | (ck >> 31)) + buf[i];
    }
    buf[0x0C] = (unsigned char)(ck & 0xFF);
    buf[0x0D] = (unsigned char)((ck >> 8) & 0xFF);
    buf[0x0E] = (unsigned char)((ck >> 16) & 0xFF);
    buf[0x0F] = (unsigned char)((ck >> 24) & 0xFF);

    f = fopen(d2sPath, "wb");
    if (f) { fwrite(buf, 1, sz, f); fclose(f); }
    free(buf);
    return TRUE;
}

/* ================================================================
 * STATE FILE CREATION (skill randomizer)
 * ================================================================ */
static void CreateStateFile(const char *name, unsigned int seed) {
    srand(seed);

    /* Skill pool: pick 60 random skills (20 T1, 20 T2, 20 T3).
     * Shuffle each tier, take first 20 from each.
     * Unlock order: T1 first, then T2, then T3.
     * First 6 T1 are starting skills (unlocked from start). */
    #define POOL_T1 20
    #define POOL_T2 20
    #define POOL_T3 20
    #define POOL_TOTAL (POOL_T1 + POOL_T2 + POOL_T3)
    #define NUM_STARTING 6

    int t1[210], t2[210], t3[210];
    int t1n = 0, t2n = 0, t3n = 0;

    for (int i = 0; i < 210; i++) {
        if (ALL_SKILLS[i].tier == 1) t1[t1n++] = i;
        else if (ALL_SKILLS[i].tier == 2) t2[t2n++] = i;
        else t3[t3n++] = i;
    }

    /* Fisher-Yates shuffle each tier */
    for (int i = t1n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = t1[i]; t1[i] = t1[j]; t1[j] = tmp;
    }
    for (int i = t2n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = t2[i]; t2[i] = t2[j]; t2[j] = tmp;
    }
    for (int i = t3n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = t3[i]; t3[i] = t3[j]; t3[j] = tmp;
    }

    /* Build the 60-skill pool: first 20 from each tier */
    int pool[POOL_TOTAL];
    int poolN = 0;
    int pickT1 = (POOL_T1 < t1n) ? POOL_T1 : t1n;
    int pickT2 = (POOL_T2 < t2n) ? POOL_T2 : t2n;
    int pickT3 = (POOL_T3 < t3n) ? POOL_T3 : t3n;

    /* T1 first (unlock order: early game) */
    for (int i = 0; i < pickT1; i++) pool[poolN++] = t1[i];
    /* T2 next (mid game) */
    for (int i = 0; i < pickT2; i++) pool[poolN++] = t2[i];
    /* T3 last (late game) */
    for (int i = 0; i < pickT3; i++) pool[poolN++] = t3[i];

    /* First 6 T1 skills are starting skills (unlocked) */
    BOOL unlocked[210] = {0};
    for (int i = 0; i < NUM_STARTING && i < pickT1; i++) unlocked[t1[i]] = TRUE;

    /* Ensure Archipelago dir exists */
    char archDir[MAX_PATH];
    snprintf(archDir, MAX_PATH, "%sArchipelago", g_gameRoot);
    CreateDirectoryA(archDir, NULL);

    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "%sArchipelago\\d2arch_state_%s.dat", g_gameRoot, name);
    FILE *f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "seed=%u\n", seed);
    fprintf(f, "num_starting=%d\n", NUM_STARTING);
    fprintf(f, "total_skills=%d\n", poolN);

    /* starting_skills line */
    fprintf(f, "starting_skills=");
    for (int i = 0; i < NUM_STARTING; i++) {
        if (i > 0) fprintf(f, ",");
        fprintf(f, "%s", ALL_SKILLS[t1[i]].name);
    }
    fprintf(f, "\n");

    /* Write ONLY the 60 skills in the pool (not all 210).
     * Order = unlock order: T1 shuffled, then T2 shuffled, then T3 shuffled. */
    fprintf(f, "assignments=\n");
    for (int i = 0; i < poolN; i++) {
        int idx = pool[i];
        fprintf(f, "%s,%s,%d,%d\n",
            ALL_SKILLS[idx].name,
            ALL_SKILLS[idx].cls,
            unlocked[idx] ? 1 : 0,
            ALL_SKILLS[idx].id);
    }

    /* Save AP connection info if in Archipelago mode */
    if (g_apMode && g_apServer[0] && g_apSlot[0]) {
        fprintf(f, "ap_server=%s\n", g_apServer);
        fprintf(f, "ap_slot=%s\n", g_apSlot);
        fprintf(f, "ap_password=%s\n", g_apPassword);
    }

    fclose(f);
}

/* ================================================================
 * CLEANUP ORPHANED STATE/SLOT FILES
 * Remove d2arch_state_X.dat and d2arch_slots_X.dat files
 * where no matching X.d2s exists in the save directory.
 * ================================================================ */
static void CleanupOrphanedFiles(void) {
    char archDir[MAX_PATH];
    snprintf(archDir, MAX_PATH, "%sArchipelago\\", g_gameRoot);

    /* Delete legacy global files (no char name — from older versions) */
    {
        const char *legacy[] = { "d2arch_state.dat", "d2arch_slots.dat",
                                  "d2arch_checks.dat", "ap_status.dat", NULL };
        for (int i = 0; legacy[i]; i++) {
            char path[MAX_PATH];
            snprintf(path, MAX_PATH, "%s%s", archDir, legacy[i]);
            DeleteFileA(path);
        }
    }

    /* Scan for d2arch_state_*.dat and d2arch_slots_*.dat */
    const char *prefixes[] = { "d2arch_state_", "d2arch_slots_", "d2arch_checks_", NULL };
    for (int p = 0; prefixes[p]; p++) {
        char pattern[MAX_PATH];
        snprintf(pattern, MAX_PATH, "%s%s*.dat", archDir, prefixes[p]);

        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;

        int prefixLen = (int)strlen(prefixes[p]);
        do {
            /* Extract character name from filename */
            char *nameStart = fd.cFileName + prefixLen;
            char *dot = strstr(nameStart, ".dat");
            if (!dot) continue;

            char charName[17] = {0};
            int nameLen = (int)(dot - nameStart);
            if (nameLen <= 0 || nameLen > 15) continue;
            strncpy(charName, nameStart, nameLen);

            /* Check if matching .d2s exists in save dir */
            char d2sPath[MAX_PATH];
            snprintf(d2sPath, MAX_PATH, "%s\\%s.d2s", g_saveDir, charName);
            if (GetFileAttributesA(d2sPath) == INVALID_FILE_ATTRIBUTES) {
                /* No .d2s — delete orphaned file */
                char fullPath[MAX_PATH];
                snprintf(fullPath, MAX_PATH, "%s%s", archDir, fd.cFileName);
                DeleteFileA(fullPath);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
}

/* Delete a character and all associated files */
static void DeleteCharacter(const char *charName) {
    /* Delete .d2s and related save files */
    const char *exts[] = { ".d2s", ".d2x", ".key", ".ma0", ".map", NULL };
    for (int e = 0; exts[e]; e++) {
        char path[MAX_PATH];
        snprintf(path, MAX_PATH, "%s\\%s%s", g_saveDir, charName, exts[e]);
        DeleteFileA(path);
    }

    /* Also check _arch_hidden/ */
    for (int e = 0; exts[e]; e++) {
        char path[MAX_PATH];
        snprintf(path, MAX_PATH, "%s\\_arch_hidden\\%s%s", g_saveDir, charName, exts[e]);
        DeleteFileA(path);
    }

    /* Delete our state/slot/checks files */
    char archDir[MAX_PATH];
    snprintf(archDir, MAX_PATH, "%sArchipelago\\", g_gameRoot);
    const char *prefixes[] = { "d2arch_state_", "d2arch_slots_", "d2arch_checks_", NULL };
    for (int p = 0; prefixes[p]; p++) {
        char path[MAX_PATH];
        snprintf(path, MAX_PATH, "%s%s%s.dat", archDir, prefixes[p], charName);
        DeleteFileA(path);
    }
}

/* ================================================================
 * POPULATE CHARACTER LIST
 * ================================================================ */
static void PopulateCharList(void) {
    SendMessage(g_hListChars, LB_RESETCONTENT, 0, 0);

    char pattern[MAX_PATH];
    snprintf(pattern, MAX_PATH, "%s\\*.d2s", g_saveDir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        char filepath[MAX_PATH];
        snprintf(filepath, MAX_PATH, "%s\\%s", g_saveDir, fd.cFileName);

        FILE *f = fopen(filepath, "rb");
        if (!f) continue;

        unsigned char header[0x2C];
        if (fread(header, 1, sizeof(header), f) < sizeof(header)) {
            fclose(f);
            continue;
        }
        fclose(f);

        /* Name at 0x14 (16 bytes), class at 0x28 (1 byte), level at 0x2B (1 byte) */
        char charName[17] = {0};
        memcpy(charName, header + 0x14, 16);
        int classId = header[0x28];
        int level = header[0x2B];

        const char *className = "Unknown";
        if (classId >= 0 && classId <= 6) className = CLASS_NAMES[classId];

        char display[128];
        snprintf(display, sizeof(display), "%s - Lv%d %s", charName, level, className);
        SendMessageA(g_hListChars, LB_ADDSTRING, 0, (LPARAM)display);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/* ================================================================
 * LAUNCH GAME
 * ================================================================ */
static void LaunchGame(const char *charName) {
    strncpy(g_lastChar, charName, sizeof(g_lastChar) - 1);
    SaveConfig();
    UpdateDdrawIni();

    /* Isolate character */
    IsolateCharacter(charName);

    /* Run d2skillreset.exe */
    char resetExe[MAX_PATH];
    snprintf(resetExe, MAX_PATH, "%sArchipelago\\build\\d2skillreset.exe", g_gameRoot);
    if (GetFileAttributesA(resetExe) != INVALID_FILE_ATTRIBUTES) {
        char cmd[MAX_PATH * 2];
        snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", resetExe, g_saveDir);
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, g_gameRoot, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 10000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }

    /* Launch AP bridge — check UI fields first, then fall back to state file */
    if (g_apMode) {
        /* Read fresh from UI (New Character page) */
        GetWindowTextA(g_hEditServer, g_apServer, sizeof(g_apServer));
        GetWindowTextA(g_hEditSlot, g_apSlot, sizeof(g_apSlot));
        GetWindowTextA(g_hEditPassword, g_apPassword, sizeof(g_apPassword));
    } else {
        /* Continue page — try to load AP info from state file */
        char statePath[MAX_PATH];
        snprintf(statePath, MAX_PATH, "%sArchipelago\\d2arch_state_%s.dat",
                 g_gameRoot, charName);
        FILE *sf = fopen(statePath, "r");
        if (sf) {
            char line[256];
            while (fgets(line, sizeof(line), sf)) {
                char *nl = strchr(line, '\n'); if (nl) *nl = 0;
                char *cr = strchr(line, '\r'); if (cr) *cr = 0;
                if (strncmp(line, "ap_server=", 10) == 0) {
                    strncpy(g_apServer, line + 10, sizeof(g_apServer) - 1);
                    g_apMode = 1;
                }
                else if (strncmp(line, "ap_slot=", 8) == 0)
                    strncpy(g_apSlot, line + 8, sizeof(g_apSlot) - 1);
                else if (strncmp(line, "ap_password=", 12) == 0)
                    strncpy(g_apPassword, line + 12, sizeof(g_apPassword) - 1);
            }
            fclose(sf);
        }
    }

    if (g_apMode && g_apServer[0] && g_apSlot[0]) {
        /* Find Python executable — try several common locations */
        char pythonExe[MAX_PATH] = "";
        const char *pyCandidates[] = { "python.exe", "py.exe", "python3.exe", NULL };
        for (int i = 0; pyCandidates[i]; i++) {
            char found[MAX_PATH];
            if (SearchPathA(NULL, pyCandidates[i], NULL, MAX_PATH, found, NULL)) {
                strncpy(pythonExe, found, MAX_PATH - 1);
                break;
            }
        }
        if (!pythonExe[0]) {
            MessageBoxA(g_hWnd,
                "Python not found on PATH.\n\n"
                "The AP bridge requires Python 3.x to be installed.\n"
                "Install from python.org and ensure it is on your PATH.",
                "Archipelago Bridge Error", MB_OK | MB_ICONERROR);
        } else {
            /* Create log file for bridge output */
            char logFile[MAX_PATH];
            snprintf(logFile, sizeof(logFile),
                "%sArchipelago\\ap_bridge_log.txt", g_gameRoot);
            SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
            HANDLE hLog = CreateFileA(logFile, GENERIC_WRITE, FILE_SHARE_READ,
                &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

            /* Write launch info to log */
            if (hLog != INVALID_HANDLE_VALUE) {
                char info[1024];
                int n = snprintf(info, sizeof(info),
                    "Python: %s\nServer: %s\nSlot: %s\nChar: %s\nGameDir: %s\n---\n",
                    pythonExe, g_apServer, g_apSlot, charName, g_gameRoot);
                DWORD written;
                WriteFile(hLog, info, n, &written, NULL);
            }

            char bridgeCmd[MAX_PATH * 4];
            snprintf(bridgeCmd, sizeof(bridgeCmd),
                "\"%s\" -u \"%sArchipelago\\src\\ap_bridge_gui.py\" "
                "--server \"%s\" --slot \"%s\" --password \"%s\" "
                "--char \"%s\" --gamedir \"%s\"",
                pythonExe, g_gameRoot, g_apServer, g_apSlot, g_apPassword,
                charName, g_gameRoot);

            STARTUPINFOA siAP = {sizeof(siAP)};
            PROCESS_INFORMATION piAP;
            siAP.dwFlags = STARTF_USESTDHANDLES;
            if (hLog != INVALID_HANDLE_VALUE) {
                siAP.hStdOutput = hLog;
                siAP.hStdError = hLog;
            }
            BOOL ok = CreateProcessA(NULL, bridgeCmd, NULL, NULL, TRUE,
                0, NULL, g_gameRoot, &siAP, &piAP);
            if (ok) {
                CloseHandle(piAP.hProcess);
                CloseHandle(piAP.hThread);
            } else {
                DWORD err = GetLastError();
                char errMsg[512];
                snprintf(errMsg, sizeof(errMsg),
                    "Failed to start AP bridge (error %lu).\n\n"
                    "Python: %s\nCommand: %.200s...",
                    err, pythonExe, bridgeCmd);
                MessageBoxA(g_hWnd, errMsg,
                    "Archipelago Bridge Error", MB_OK | MB_ICONERROR);
            }
            if (hLog != INVALID_HANDLE_VALUE)
                CloseHandle(hLog);
        }
    }

    /* Launch Game.exe */
    char cmdLine[MAX_PATH * 2];
    snprintf(cmdLine, sizeof(cmdLine), "\"%sGame.exe\" -direct -txt", g_gameRoot);
    STARTUPINFOA si2 = {sizeof(si2)};
    PROCESS_INFORMATION pi2;
    CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, g_gameRoot, &si2, &pi2);
    CloseHandle(pi2.hProcess);
    CloseHandle(pi2.hThread);

    DestroyWindow(g_hWnd);
}

/* ================================================================
 * UI HELPERS
 * ================================================================ */
static void SetControlFont(HWND hCtrl) {
    SendMessage(hCtrl, WM_SETFONT, (WPARAM)g_hFont, TRUE);
}

static void ShowCtrl(HWND h, BOOL show) {
    ShowWindow(h, show ? SW_SHOW : SW_HIDE);
    EnableWindow(h, show);
}

static void ShowPage(int page) {
    g_currentPage = page;

    /* Hide all page controls */
    ShowCtrl(g_hBtnNewChar, FALSE);
    ShowCtrl(g_hBtnContinue, FALSE);
    ShowCtrl(g_hBtnQuit, FALSE);

    ShowCtrl(g_hLblNewChar, FALSE);
    ShowCtrl(g_hLblName, FALSE);
    ShowCtrl(g_hEditName, FALSE);
    ShowCtrl(g_hLblClass, FALSE);
    ShowCtrl(g_hComboClass, FALSE);
    ShowCtrl(g_hLblSeed, FALSE);
    ShowCtrl(g_hEditSeed, FALSE);
    ShowCtrl(g_hRadioWinNew, FALSE);
    ShowCtrl(g_hRadioFsNew, FALSE);
    ShowCtrl(g_hBtnCreate, FALSE);
    ShowCtrl(g_hBtnBackNew, FALSE);

    ShowCtrl(g_hLblContinue, FALSE);
    ShowCtrl(g_hListChars, FALSE);
    ShowCtrl(g_hRadioWinCont, FALSE);
    ShowCtrl(g_hRadioFsCont, FALSE);
    ShowCtrl(g_hBtnPlay, FALSE);
    ShowCtrl(g_hBtnBackCont, FALSE);
    ShowCtrl(g_hBtnDelete, FALSE);

    switch (page) {
    case PAGE_MAIN:
        ShowCtrl(g_hBtnNewChar, TRUE);
        ShowCtrl(g_hBtnContinue, TRUE);
        ShowCtrl(g_hBtnQuit, TRUE);
        break;

    case PAGE_NEWCHAR: {
        ShowCtrl(g_hLblNewChar, TRUE);
        ShowCtrl(g_hLblName, TRUE);
        ShowCtrl(g_hEditName, TRUE);
        ShowCtrl(g_hLblClass, TRUE);
        ShowCtrl(g_hComboClass, TRUE);
        ShowCtrl(g_hLblSeed, TRUE);
        ShowCtrl(g_hEditSeed, TRUE);
        ShowCtrl(g_hRadioWinNew, TRUE);
        ShowCtrl(g_hRadioFsNew, TRUE);
        ShowCtrl(g_hRadioStandalone, TRUE);
        ShowCtrl(g_hRadioAP, TRUE);
        /* AP fields shown/hidden based on mode */
        {
            BOOL ap = (SendMessage(g_hRadioAP, BM_GETCHECK, 0, 0) == BST_CHECKED);
            ShowCtrl(g_hLblServer, ap); ShowCtrl(g_hEditServer, ap);
            ShowCtrl(g_hLblSlot, ap); ShowCtrl(g_hEditSlot, ap);
            ShowCtrl(g_hLblPassword, ap); ShowCtrl(g_hEditPassword, ap);
        }
        ShowCtrl(g_hBtnCreate, TRUE);
        ShowCtrl(g_hBtnBackNew, TRUE);

        /* Auto-fill seed */
        char seedStr[32];
        snprintf(seedStr, sizeof(seedStr), "%u", (unsigned int)GetTickCount());
        SetWindowTextA(g_hEditSeed, seedStr);
        SetWindowTextA(g_hEditName, "");
        SendMessage(g_hComboClass, CB_SETCURSEL, 0, 0);

        /* Set display radio */
        SendMessage(g_hRadioWinNew, BM_SETCHECK, g_displayMode == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessage(g_hRadioFsNew, BM_SETCHECK, g_displayMode == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
        break;
    }

    case PAGE_CONTINUE:
        ShowCtrl(g_hLblContinue, TRUE);
        ShowCtrl(g_hListChars, TRUE);
        ShowCtrl(g_hRadioWinCont, TRUE);
        ShowCtrl(g_hRadioFsCont, TRUE);
        ShowCtrl(g_hBtnPlay, TRUE);
        ShowCtrl(g_hBtnBackCont, TRUE);
        ShowCtrl(g_hBtnDelete, TRUE);

        CleanupOrphanedFiles();
        SendMessage(g_hRadioWinCont, BM_SETCHECK, g_displayMode == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessage(g_hRadioFsCont, BM_SETCHECK, g_displayMode == 1 ? BST_CHECKED : BST_UNCHECKED, 0);

        PopulateCharList();
        break;
    }

    InvalidateRect(g_hWnd, NULL, TRUE);
}

/* ================================================================
 * WINDOW PROC
 * ================================================================ */
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        /* Dark header bar */
        RECT rc = {0, 0, WND_W, HEADER_H};
        HBRUSH hbr = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);

        /* Gold title text */
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(200, 170, 80));
        HFONT hfTitle = CreateFontA(24, 0, 0, 0, FW_BOLD, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(hdc, hfTitle);
        rc.left = PAD; rc.top = 14;
        DrawTextA(hdc, "Diablo II Archipelago", -1, &rc, DT_LEFT | DT_NOCLIP);
        SelectObject(hdc, oldFont);
        DeleteObject(hfTitle);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetBkMode(hdcStatic, TRANSPARENT);
        SetTextColor(hdcStatic, RGB(40, 40, 40));
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int notif = HIWORD(wParam);

        switch (id) {
        /* Page 0: Main Menu */
        case ID_BTN_NEWCHAR:
            ShowPage(PAGE_NEWCHAR);
            break;
        case ID_BTN_CONTINUE:
            ShowPage(PAGE_CONTINUE);
            break;
        case ID_BTN_QUIT:
            SaveConfig();
            DestroyWindow(hWnd);
            break;

        /* Page 1: New Character radio buttons */
        case ID_RADIO_WIN_NEW:
            g_displayMode = 0;
            break;
        case ID_RADIO_FS_NEW:
            g_displayMode = 1;
            break;

        /* Mode toggle: show/hide AP fields */
        case 2001: /* Standalone */
            g_apMode = 0;
            ShowCtrl(g_hLblServer, FALSE); ShowCtrl(g_hEditServer, FALSE);
            ShowCtrl(g_hLblSlot, FALSE); ShowCtrl(g_hEditSlot, FALSE);
            ShowCtrl(g_hLblPassword, FALSE); ShowCtrl(g_hEditPassword, FALSE);
            break;
        case 2002: /* Archipelago */
            g_apMode = 1;
            ShowCtrl(g_hLblServer, TRUE); ShowCtrl(g_hEditServer, TRUE);
            ShowCtrl(g_hLblSlot, TRUE); ShowCtrl(g_hEditSlot, TRUE);
            ShowCtrl(g_hLblPassword, TRUE); ShowCtrl(g_hEditPassword, TRUE);
            break;

        /* Page 1: Create & Play */
        case ID_BTN_CREATE: {
            char name[17] = {0};
            GetWindowTextA(g_hEditName, name, 16);
            if (strlen(name) == 0) {
                MessageBoxA(hWnd, "Please enter a character name.", "Error", MB_OK | MB_ICONERROR);
                break;
            }
            /* Validate name: alphanumeric and underscores only */
            for (int i = 0; name[i]; i++) {
                char c = name[i];
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-')) {
                    MessageBoxA(hWnd, "Name must contain only letters, numbers, hyphens, and underscores.",
                        "Error", MB_OK | MB_ICONERROR);
                    goto done_create;
                }
            }

            int classId = (int)SendMessage(g_hComboClass, CB_GETCURSEL, 0, 0);
            if (classId == CB_ERR) classId = 0;

            char seedStr[32];
            GetWindowTextA(g_hEditSeed, seedStr, sizeof(seedStr));
            unsigned int seed = (unsigned int)strtoul(seedStr, NULL, 10);
            if (seed == 0) seed = (unsigned int)GetTickCount();

            /* Check display mode from radio */
            if (SendMessage(g_hRadioFsNew, BM_GETCHECK, 0, 0) == BST_CHECKED)
                g_displayMode = 1;
            else
                g_displayMode = 0;

            /* Check if file already exists */
            char checkPath[MAX_PATH];
            snprintf(checkPath, MAX_PATH, "%s\\%s.d2s", g_saveDir, name);
            if (GetFileAttributesA(checkPath) != INVALID_FILE_ATTRIBUTES) {
                MessageBoxA(hWnd, "A character with that name already exists.", "Error", MB_OK | MB_ICONERROR);
                break;
            }

            if (!CreateD2sFile(name, classId, seed)) {
                MessageBoxA(hWnd, "Failed to create character file.", "Error", MB_OK | MB_ICONERROR);
                break;
            }

            /* Read AP fields from UI before creating state file */
            if (g_apMode) {
                GetWindowTextA(g_hEditServer, g_apServer, sizeof(g_apServer));
                GetWindowTextA(g_hEditSlot, g_apSlot, sizeof(g_apSlot));
                GetWindowTextA(g_hEditPassword, g_apPassword, sizeof(g_apPassword));
            }
            CreateStateFile(name, seed);
            LaunchGame(name);
            done_create:
            break;
        }

        case ID_BTN_BACK_NEW:
            ShowPage(PAGE_MAIN);
            break;

        /* Page 2: Continue radio buttons */
        case ID_RADIO_WIN_CONT:
            g_displayMode = 0;
            break;
        case ID_RADIO_FS_CONT:
            g_displayMode = 1;
            break;

        /* Page 2: Play */
        case ID_BTN_PLAY: {
            int sel = (int)SendMessage(g_hListChars, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) {
                MessageBoxA(hWnd, "Please select a character.", "Error", MB_OK | MB_ICONERROR);
                break;
            }

            /* Check display mode from radio */
            if (SendMessage(g_hRadioFsCont, BM_GETCHECK, 0, 0) == BST_CHECKED)
                g_displayMode = 1;
            else
                g_displayMode = 0;

            char display[128];
            SendMessageA(g_hListChars, LB_GETTEXT, sel, (LPARAM)display);

            /* Extract character name (everything before " - ") */
            char charName[17] = {0};
            char *dash = strstr(display, " - ");
            if (dash) {
                int len = (int)(dash - display);
                if (len > 15) len = 15;
                strncpy(charName, display, len);
            } else {
                strncpy(charName, display, 15);
            }

            LaunchGame(charName);
            break;
        }

        case ID_BTN_BACK_CONT:
            ShowPage(PAGE_MAIN);
            break;

        case ID_BTN_DELETE: {
            int sel = (int)SendMessage(g_hListChars, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) {
                MessageBoxA(hWnd, "Please select a character to delete.", "Error", MB_OK | MB_ICONERROR);
                break;
            }
            char display[128];
            SendMessageA(g_hListChars, LB_GETTEXT, sel, (LPARAM)display);
            char charName[17] = {0};
            char *dash = strstr(display, " - ");
            if (dash) {
                int len = (int)(dash - display);
                if (len > 15) len = 15;
                strncpy(charName, display, len);
            } else {
                strncpy(charName, display, 15);
            }

            char msg[128];
            snprintf(msg, sizeof(msg), "Delete character '%s' and all save data?\n\nThis cannot be undone.", charName);
            if (MessageBoxA(hWnd, msg, "Confirm Delete", MB_YESNO | MB_ICONWARNING) == IDYES) {
                DeleteCharacter(charName);
                PopulateCharList();
            }
            break;
        }

        /* Handle listbox double-click */
        case ID_LIST_CHARS:
            if (notif == LBN_DBLCLK) {
                SendMessage(hWnd, WM_COMMAND, MAKEWPARAM(ID_BTN_PLAY, BN_CLICKED), 0);
            }
            break;
        }
        return 0;
    }

    case WM_DESTROY:
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

/* ================================================================
 * CREATE ALL CONTROLS
 * ================================================================ */
static void CreateControls(void) {
    int centerX = WND_W / 2;
    int btnW = 200, btnH = 36;

    /* ---- Page 0: Main Menu ---- */
    int startY = HEADER_H + 80;
    int spacing = 50;

    g_hBtnNewChar = CreateWindowA("BUTTON", "New Character",
        WS_CHILD | BS_PUSHBUTTON,
        centerX - btnW/2, startY, btnW, btnH,
        g_hWnd, (HMENU)(INT_PTR)ID_BTN_NEWCHAR, g_hInst, NULL);

    g_hBtnContinue = CreateWindowA("BUTTON", "Continue",
        WS_CHILD | BS_PUSHBUTTON,
        centerX - btnW/2, startY + spacing, btnW, btnH,
        g_hWnd, (HMENU)(INT_PTR)ID_BTN_CONTINUE, g_hInst, NULL);

    g_hBtnQuit = CreateWindowA("BUTTON", "Quit",
        WS_CHILD | BS_PUSHBUTTON,
        centerX - btnW/2, startY + spacing * 2, btnW, btnH,
        g_hWnd, (HMENU)(INT_PTR)ID_BTN_QUIT, g_hInst, NULL);

    /* ---- Page 1: New Character ---- */
    int y = HEADER_H + 16;
    int lblW = 60, editW = 280, editX = PAD + lblW + 8;

    g_hLblNewChar = CreateWindowA("STATIC", "Create New Character",
        WS_CHILD | SS_LEFT,
        PAD, y, 300, 22, g_hWnd, NULL, g_hInst, NULL);
    y += 36;

    g_hLblName = CreateWindowA("STATIC", "Name:",
        WS_CHILD | SS_RIGHT,
        PAD, y + 2, lblW, 20, g_hWnd, NULL, g_hInst, NULL);
    g_hEditName = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | ES_AUTOHSCROLL,
        editX, y, editW, 24, g_hWnd, (HMENU)(INT_PTR)ID_EDIT_NAME, g_hInst, NULL);
    SendMessageA(g_hEditName, EM_SETLIMITTEXT, 15, 0);
    y += 36;

    g_hLblClass = CreateWindowA("STATIC", "Class:",
        WS_CHILD | SS_RIGHT,
        PAD, y + 2, lblW, 20, g_hWnd, NULL, g_hInst, NULL);
    g_hComboClass = CreateWindowA("COMBOBOX", "",
        WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        editX, y, editW, 200, g_hWnd, (HMENU)(INT_PTR)ID_COMBO_CLASS, g_hInst, NULL);
    for (int i = 0; i < 7; i++)
        SendMessageA(g_hComboClass, CB_ADDSTRING, 0, (LPARAM)CLASS_NAMES[i]);
    SendMessage(g_hComboClass, CB_SETCURSEL, 0, 0);
    y += 36;

    g_hLblSeed = CreateWindowA("STATIC", "Seed:",
        WS_CHILD | SS_RIGHT,
        PAD, y + 2, lblW, 20, g_hWnd, NULL, g_hInst, NULL);
    g_hEditSeed = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER,
        editX, y, editW, 24, g_hWnd, (HMENU)(INT_PTR)ID_EDIT_SEED, g_hInst, NULL);
    y += 44;

    /* Display radio buttons */
    g_hRadioWinNew = CreateWindowA("BUTTON", "Windowed",
        WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
        editX, y, 120, 22, g_hWnd, (HMENU)(INT_PTR)ID_RADIO_WIN_NEW, g_hInst, NULL);
    g_hRadioFsNew = CreateWindowA("BUTTON", "Fullscreen",
        WS_CHILD | BS_AUTORADIOBUTTON,
        editX + 130, y, 120, 22, g_hWnd, (HMENU)(INT_PTR)ID_RADIO_FS_NEW, g_hInst, NULL);
    y += 34;

    /* Mode: Standalone / Archipelago */
    g_hRadioStandalone = CreateWindowA("BUTTON", "Standalone",
        WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
        editX, y, 120, 22, g_hWnd, (HMENU)2001, g_hInst, NULL);
    g_hRadioAP = CreateWindowA("BUTTON", "Archipelago",
        WS_CHILD | BS_AUTORADIOBUTTON,
        editX + 130, y, 120, 22, g_hWnd, (HMENU)2002, g_hInst, NULL);
    SendMessage(g_hRadioStandalone, BM_SETCHECK, BST_CHECKED, 0);
    y += 30;

    /* AP fields */
    g_hLblServer = CreateWindowA("STATIC", "Server:", WS_CHILD, PAD, y+2, 60, 20, g_hWnd, NULL, g_hInst, NULL);
    g_hEditServer = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "archipelago.gg:38281",
        WS_CHILD | ES_AUTOHSCROLL, editX, y, editW, 22, g_hWnd, NULL, g_hInst, NULL);
    y += 26;
    g_hLblSlot = CreateWindowA("STATIC", "Slot:", WS_CHILD, PAD, y+2, 60, 20, g_hWnd, NULL, g_hInst, NULL);
    g_hEditSlot = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | ES_AUTOHSCROLL, editX, y, editW, 22, g_hWnd, NULL, g_hInst, NULL);
    y += 26;
    g_hLblPassword = CreateWindowA("STATIC", "Password:", WS_CHILD, PAD, y+2, 60, 20, g_hWnd, NULL, g_hInst, NULL);
    g_hEditPassword = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | ES_AUTOHSCROLL | ES_PASSWORD, editX, y, editW, 22, g_hWnd, NULL, g_hInst, NULL);
    y += 34;

    /* Buttons */
    g_hBtnCreate = CreateWindowA("BUTTON", "Create && Play",
        WS_CHILD | BS_DEFPUSHBUTTON,
        editX, y, 130, 32, g_hWnd, (HMENU)(INT_PTR)ID_BTN_CREATE, g_hInst, NULL);
    g_hBtnBackNew = CreateWindowA("BUTTON", "Back",
        WS_CHILD | BS_PUSHBUTTON,
        editX + 140, y, 80, 32, g_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK_NEW, g_hInst, NULL);

    /* ---- Page 2: Continue ---- */
    y = HEADER_H + 16;

    g_hLblContinue = CreateWindowA("STATIC", "Select Character",
        WS_CHILD | SS_LEFT,
        PAD, y, 300, 22, g_hWnd, NULL, g_hInst, NULL);
    y += 32;

    g_hListChars = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        PAD, y, WND_W - PAD * 2, 260,
        g_hWnd, (HMENU)(INT_PTR)ID_LIST_CHARS, g_hInst, NULL);
    y += 270;

    /* Display radio buttons */
    g_hRadioWinCont = CreateWindowA("BUTTON", "Windowed",
        WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
        PAD, y, 120, 22, g_hWnd, (HMENU)(INT_PTR)ID_RADIO_WIN_CONT, g_hInst, NULL);
    g_hRadioFsCont = CreateWindowA("BUTTON", "Fullscreen",
        WS_CHILD | BS_AUTORADIOBUTTON,
        PAD + 130, y, 120, 22, g_hWnd, (HMENU)(INT_PTR)ID_RADIO_FS_CONT, g_hInst, NULL);
    y += 36;

    /* Buttons */
    g_hBtnPlay = CreateWindowA("BUTTON", "Play",
        WS_CHILD | BS_DEFPUSHBUTTON,
        PAD, y, 100, 32, g_hWnd, (HMENU)(INT_PTR)ID_BTN_PLAY, g_hInst, NULL);
    g_hBtnBackCont = CreateWindowA("BUTTON", "Back",
        WS_CHILD | BS_PUSHBUTTON,
        PAD + 110, y, 80, 32, g_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK_CONT, g_hInst, NULL);
    g_hBtnDelete = CreateWindowA("BUTTON", "Delete",
        WS_CHILD | BS_PUSHBUTTON,
        WND_W - PAD - 80, y, 80, 32, g_hWnd, (HMENU)(INT_PTR)ID_BTN_DELETE, g_hInst, NULL);

    /* ---- Apply fonts to ALL controls ---- */
    HWND allCtrls[] = {
        g_hBtnNewChar, g_hBtnContinue, g_hBtnQuit,
        g_hLblNewChar, g_hLblName, g_hEditName,
        g_hLblClass, g_hComboClass, g_hLblSeed, g_hEditSeed,
        g_hRadioWinNew, g_hRadioFsNew,
        g_hRadioStandalone, g_hRadioAP,
        g_hLblServer, g_hEditServer, g_hLblSlot, g_hEditSlot,
        g_hLblPassword, g_hEditPassword,
        g_hBtnCreate, g_hBtnBackNew,
        g_hLblContinue, g_hListChars,
        g_hRadioWinCont, g_hRadioFsCont, g_hBtnPlay, g_hBtnBackCont, g_hBtnDelete,
    };
    for (int i = 0; i < (int)(sizeof(allCtrls)/sizeof(allCtrls[0])); i++) {
        SetControlFont(allCtrls[i]);
    }

    /* Bold font for section labels */
    SendMessage(g_hLblNewChar, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
    SendMessage(g_hLblContinue, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
}

/* ================================================================
 * ENTRY POINT
 * ================================================================ */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow) {
    (void)hPrev; (void)cmdLine; (void)nShow;
    g_hInst = hInst;

    DetectGameRoot();
    LoadConfig();
    DetectSaveDir();

    /* Restore any hidden characters from previous session */
    RestoreHiddenChars();

    InitCommonControls();

    /* Create fonts */
    g_hFont = CreateFontA(15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_hFontBold = CreateFontA(17, 0, 0, 0, FW_BOLD, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

    /* Register window class */
    WNDCLASSEXA wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "D2ArchLauncher";
    wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(1));
    RegisterClassExA(&wc);

    /* Size window for 500x500 client area */
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT wr = {0, 0, WND_W, WND_H};
    AdjustWindowRect(&wr, style, FALSE);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);

    g_hWnd = CreateWindowExA(0, "D2ArchLauncher", "Diablo II Archipelago",
        style, (sx - ww) / 2, (sy - wh) / 2, ww, wh,
        NULL, NULL, hInst, NULL);

    CreateControls();
    ShowPage(PAGE_MAIN);

    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (IsDialogMessage(g_hWnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
