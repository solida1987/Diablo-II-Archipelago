/*
 * D2Archipelago v3.0 — Skill Editor
 * Centered UI, 3 green tabs + 1 blue locked tab
 * Slot 1-10 per tab with tier system (T1/T2/T3)
 * Memory-write to vanilla skill tree
 * Quest tracker placeholder, reset button, auto-save
 *
 * T-key does NOTHING. Open via Menu → Skill Editor.
 * ESC closes editor/menu.
 */
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * LOGGING
 * ================================================================ */
static FILE* g_log = NULL;
static void Log(const char* fmt, ...) {
    if (!g_log) {
        char p[MAX_PATH]; GetModuleFileNameA(NULL, p, MAX_PATH);
        char* s = strrchr(p, '\\'); if (s) strcpy(s+1, "d2archipelago.log");
        g_log = fopen(p, "w"); if (!g_log) return;
    }
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log);
}

/* ================================================================
 * D2 CONSTANTS
 * ================================================================ */
#define STAT_STRENGTH       0
#define STAT_ENERGY         1
#define STAT_DEXTERITY      2
#define STAT_VITALITY       3
#define STAT_NEWSTATS       4
#define STAT_NEWSKILLS      5
#define STAT_LEVEL          12
#define STAT_GOLD           14
#define STAT_GOLDBANK       15
#define STAT_NONCLASSSKILL  97

/* SkillsTxt struct (size 0x23C) */
#define SKT_CHARCLASS   0x0C
#define SKT_REQLEVEL    0x174
#define SKT_REQSKILL0   0x17E
#define SKT_REQSKILL1   0x180
#define SKT_REQSKILL2   0x182
#define SKT_SKILLDESC   0x194
#define SKT_SIZE        0x23C

/* SkillDescTxt struct (size 0x120) */
#define SDT_PAGE        0x02
#define SDT_ROW         0x03
#define SDT_COL         0x04
#define SDT_ICONCEL     0x07
#define SDT_SIZE        0x120

/* sgptDataTables */
#define DT_OFFSET       0x96A20
#define DT_SKILLDESC    0xB8C
#define DT_SKILLDESC_N  0xB94
#define DT_SKILLS       0xB98
#define DT_SKILLS_N     0xBA0

/* Skill system limits */
#define MAX_SKILLS 220
#define MAX_SLOTS  10
#define NUM_TABS   3

/* Forward declarations of skill data (defined later, used by PatchSkillForPlayer) */
typedef struct {
    char name[64];
    char cls[4];
    int  id;
    int  tier; /* 1=T1 (basic), 2=T2 (mid), 3=T3 (advanced) */
    BOOL unlocked;
} SkillInfo;
static SkillInfo g_skills[MAX_SKILLS];
static int g_numSkills = 0;
static int g_tabSlots[NUM_TABS][MAX_SLOTS];

/* Base stats per class: str, dex, vit, eng */
static const int CLASS_BASE[7][4] = {
    {20, 25, 20, 15}, /* 0 Amazon */
    {10, 25, 10, 35}, /* 1 Sorceress */
    {15, 25, 15, 25}, /* 2 Necromancer */
    {25, 20, 25, 15}, /* 3 Paladin */
    {30, 20, 25, 10}, /* 4 Barbarian */
    {15, 20, 25, 20}, /* 5 Druid */
    {20, 20, 20, 25}, /* 6 Assassin */
};

/* ================================================================
 * D2 API
 * ================================================================ */
static HMODULE hCli, hCom, hWin, hGfx;

typedef DWORD (__stdcall *GetUnitStat_t)(DWORD, int, WORD);
typedef void  (__stdcall *SetUnitStat_t)(DWORD, int, int, WORD);
typedef DWORD (__stdcall *AddSkill_t)(DWORD, int);
typedef DWORD (__stdcall *GetPlayer_t)(void);
typedef void  (__fastcall *DrawText_t)(const wchar_t*, int, int, int, int);
typedef int   (__fastcall *SetFont_t)(int);
typedef void  (__stdcall *DrawRect_t)(int, int, int, int, BYTE, int);

static GetUnitStat_t fnGetStat;
static SetUnitStat_t fnSetStat;
static AddSkill_t    fnAddSkill;
static GetPlayer_t   fnGetPlayer;
static DrawText_t    fnText;
static SetFont_t     fnFont;
static DrawRect_t    fnRect;

static BOOL InitAPI(void) {
    hCli = GetModuleHandleA("D2Client.dll");
    hCom = GetModuleHandleA("D2Common.dll");
    hWin = GetModuleHandleA("D2Win.dll");
    hGfx = GetModuleHandleA("D2Gfx.dll");
    if (!hCli || !hCom || !hWin || !hGfx) return FALSE;
    fnGetStat   = (GetUnitStat_t)GetProcAddress(hCom, (LPCSTR)10520);
    fnSetStat   = (SetUnitStat_t)GetProcAddress(hCom, (LPCSTR)10517);
    fnAddSkill  = (AddSkill_t)GetProcAddress(hCom, (LPCSTR)10952);
    fnGetPlayer = (GetPlayer_t)((DWORD)hCli + 0x883D0);
    fnText      = (DrawText_t)GetProcAddress(hWin, (LPCSTR)10117);
    fnFont      = (SetFont_t)GetProcAddress(hWin, (LPCSTR)10127);
    fnRect      = (DrawRect_t)GetProcAddress(hGfx, (LPCSTR)10055);

    return TRUE;
}

/* ================================================================
 * HELPERS
 * ================================================================ */
static DWORD Player(void) {
    DWORD p = 0;
    if (fnGetPlayer) { __try { p = (DWORD)fnGetPlayer(); } __except(1) {} }
    return p;
}

static int ReadStat(int id) {
    DWORD p = Player(); if (!p || !fnGetStat) return 0;
    int v = 0; __try { v = (int)fnGetStat(p, id, 0); } __except(1) {} return v;
}

static int MouseX(void) {
    int r = 0; __try { r = (int)((DWORD(__stdcall*)(void))((DWORD)hCli+0xB7BC0))(); } __except(1) {} return r;
}

static int MouseY(void) {
    int r = 0; __try { r = (int)((DWORD(__stdcall*)(void))((DWORD)hCli+0xB7BD0))(); } __except(1) {} return r;
}

static int GetPlayerClass(void) {
    DWORD p = Player(); if (!p) return 0;
    int c = 0; __try { c = (int)*(DWORD*)(p + 0x04); } __except(1) {} return c;
}

/* ================================================================
 * SKILLTXT / SKILLDESC MEMORY MANIPULATION
 * ================================================================ */
static DWORD GetSgptDT(void) {
    if (!hCom) return 0;
    DWORD dt = 0;
    __try { dt = *(DWORD*)((DWORD)hCom + DT_OFFSET); } __except(1) {}
    return dt;
}

static int GetSkillDescIdx(int skillId) {
    DWORD dt = GetSgptDT(); if (!dt) return -1;
    __try {
        DWORD arr = *(DWORD*)(dt + DT_SKILLS);
        int cnt = *(int*)(dt + DT_SKILLS_N);
        if (!arr || skillId < 0 || skillId >= cnt) return -1;
        return (int)*(WORD*)(arr + skillId * SKT_SIZE + SKT_SKILLDESC);
    } __except(1) {} return -1;
}

static void SetSkillTreePos(int descIdx, int page, int row, int col) {
    DWORD dt = GetSgptDT(); if (!dt) return;
    __try {
        DWORD arr = *(DWORD*)(dt + DT_SKILLDESC);
        int cnt = *(int*)(dt + DT_SKILLDESC_N);
        if (!arr || descIdx < 0 || descIdx >= cnt) return;
        DWORD rec = arr + descIdx * SDT_SIZE;
        DWORD op;
        VirtualProtect((void*)(rec + SDT_PAGE), 3, PAGE_READWRITE, &op);
        *(BYTE*)(rec + SDT_PAGE) = (BYTE)page;
        *(BYTE*)(rec + SDT_ROW)  = (BYTE)row;
        *(BYTE*)(rec + SDT_COL)  = (BYTE)col;
        VirtualProtect((void*)(rec + SDT_PAGE), 3, op, &op);
    } __except(1) {}
}

/* Forward declarations */
static void GetArchPath(char* out, const char* file);
static void SetSkillIcon(int skillId);
static int g_savedClass = -1;
static void SaveSlots(void);
static int g_skillPoints[400];

/* Cache of original charclass per skill (before we change it to player's class).
 * Used by RestoreOrigCharClass/SetPlayerCharClass for icon rendering. */
static BOOL g_origCacheInit = FALSE;
static short g_origIconCel[400];
static short g_origCharClass[400];

/* Patch a skill: set charclass to player's class, set reqlevel by tier,
 * set prereqs so T2 requires a T1 in same tab, T3 requires a T2.
 * tab/slot = -1 means no tier restrictions (clear prereqs). */
static void PatchSkillForPlayer(int skillId) {
    DWORD dt = GetSgptDT(); if (!dt) return;
    int pc = GetPlayerClass();
    if (pc < 0) pc = (g_savedClass > 0) ? g_savedClass : 0;
    __try {
        DWORD arr = *(DWORD*)(dt + DT_SKILLS);
        int cnt = *(int*)(dt + DT_SKILLS_N);
        if (!arr || skillId < 0 || skillId >= cnt) return;
        DWORD rec = arr + skillId * SKT_SIZE;
        DWORD op;

        /* Cache original charclass BEFORE changing it (for icon rendering) */
        if (!g_origCacheInit) {
            g_origCacheInit = TRUE;
            memset(g_origIconCel, 0xFF, sizeof(g_origIconCel));
            memset(g_origCharClass, 0xFF, sizeof(g_origCharClass));
        }
        if (g_origCharClass[skillId] == -1) {
            g_origCharClass[skillId] = (short)*(BYTE*)(rec + SKT_CHARCLASS);
        }

        /* Set charclass to player's class */
        VirtualProtect((void*)(rec + SKT_CHARCLASS), 1, PAGE_READWRITE, &op);
        *(BYTE*)(rec + SKT_CHARCLASS) = (BYTE)pc;
        VirtualProtect((void*)(rec + SKT_CHARCLASS), 1, op, &op);

        /* reqlevel and prereqs set later by SetSkillTierReqs() which has
         * access to TREE_POS. For now just clear them. */
        VirtualProtect((void*)(rec + SKT_REQLEVEL), 2, PAGE_READWRITE, &op);
        *(WORD*)(rec + SKT_REQLEVEL) = 1;
        VirtualProtect((void*)(rec + SKT_REQLEVEL), 2, op, &op);

        VirtualProtect((void*)(rec + SKT_REQSKILL0), 6, PAGE_READWRITE, &op);
        *(short*)(rec + SKT_REQSKILL0) = -1;
        *(short*)(rec + SKT_REQSKILL1) = -1;
        *(short*)(rec + SKT_REQSKILL2) = -1;
        VirtualProtect((void*)(rec + SKT_REQSKILL0), 6, op, &op);
    } __except(1) {}
}

static void PlaceSkillInTree(int skillId, int page, int row, int col) {
    int descIdx = GetSkillDescIdx(skillId);
    if (descIdx < 0) { Log("Place FAIL: no desc for id=%d\n", skillId); return; }

    SetSkillTreePos(descIdx, page, row, col);
    SetSkillIcon(skillId);
    PatchSkillForPlayer(skillId);
}

static void ClearSkillFromTree(int skillId) {
    int descIdx = GetSkillDescIdx(skillId);
    if (descIdx >= 0) SetSkillTreePos(descIdx, 0, 0, 0);
}

/* ================================================================
 * SKILL LIST HELPERS
 * ================================================================ */
static DWORD FindSkillInList(DWORD player, int skillId) {
    __try {
        DWORD pL = *(DWORD*)(player + 0xA8); if (!pL) return 0;
        DWORD pS = *(DWORD*)(pL + 0x04);
        int n = 0;
        while (pS && n < 300) {
            DWORD pT = *(DWORD*)pS;
            if (pT && *(short*)pT == skillId) return pS;
            pS = *(DWORD*)(pS + 0x04); n++;
        }
    } __except(1) {}
    return 0;
}

/* ================================================================
 * SKILL DATABASE
 * ================================================================ */
#define MAX_SKILLS 220
#define MAX_SLOTS  10
#define NUM_TABS   3

/* Per-class, per-tab skill tree positions: TREE_POS[class][tab][slot] = {row, col}
 * Each class tab has exactly 10 unique positions matching vanilla D2 layout.
 * Classes: 0=Amazon, 1=Sorceress, 2=Necromancer, 3=Paladin, 4=Barbarian, 5=Druid, 6=Assassin */
static const int TREE_POS[7][3][MAX_SLOTS][2] = {
    /* AMAZON */
    {
        {{1,2},{1,3},{2,1},{2,2},{3,3},{4,1},{4,2},{5,2},{5,3},{6,1}},
        {{1,1},{1,3},{2,2},{3,1},{3,2},{4,3},{5,1},{5,2},{6,1},{6,3}},
        {{1,1},{2,2},{2,3},{3,1},{3,3},{4,2},{4,3},{5,1},{6,2},{6,3}},
    },
    /* SORCERESS */
    {
        {{1,2},{1,3},{2,1},{3,1},{3,2},{4,1},{4,3},{5,2},{6,2},{6,3}},
        {{1,2},{2,1},{2,3},{3,1},{3,2},{4,2},{4,3},{5,1},{5,3},{6,2}},
        {{1,2},{1,3},{2,1},{2,2},{3,3},{4,2},{5,1},{5,3},{6,1},{6,2}},
    },
    /* NECROMANCER */
    {
        {{1,2},{2,1},{2,3},{3,2},{3,3},{4,1},{4,2},{5,1},{5,3},{6,2}},
        {{1,2},{1,3},{2,1},{2,2},{3,3},{4,1},{4,2},{5,3},{6,1},{6,2}},
        {{1,1},{1,3},{2,2},{3,1},{3,3},{4,2},{5,1},{5,2},{6,2},{6,3}},
    },
    /* PALADIN */
    {
        {{1,1},{1,3},{2,2},{3,1},{3,3},{4,1},{4,2},{5,1},{5,3},{6,2}},
        {{1,1},{2,2},{2,3},{3,1},{4,1},{4,2},{5,2},{5,3},{6,1},{6,3}},
        {{1,1},{1,3},{2,2},{2,3},{3,1},{3,3},{4,2},{5,1},{6,2},{6,3}},
    },
    /* BARBARIAN */
    {
        {{1,2},{2,1},{2,3},{3,2},{3,3},{4,1},{4,2},{5,3},{6,1},{6,2}},
        {{1,1},{1,2},{1,3},{2,1},{2,2},{2,3},{3,1},{4,3},{5,1},{6,3}},
        {{1,1},{1,3},{2,1},{2,2},{3,3},{4,1},{5,2},{5,3},{6,1},{6,2}},
    },
    /* DRUID */
    {
        {{1,2},{1,3},{2,1},{2,2},{3,3},{4,1},{4,2},{5,3},{6,1},{6,2}},
        {{1,1},{1,2},{2,3},{3,1},{3,3},{4,1},{4,2},{5,2},{5,3},{6,1}},
        {{1,1},{2,1},{2,3},{3,1},{3,3},{4,2},{5,1},{5,2},{6,1},{6,2}},
    },
    /* ASSASSIN */
    {
        {{1,2},{2,1},{2,3},{3,1},{3,2},{4,3},{5,1},{5,2},{6,1},{6,3}},
        {{1,2},{1,3},{2,1},{3,2},{3,3},{4,1},{4,2},{5,3},{6,1},{6,2}},
        {{1,2},{1,3},{2,1},{2,3},{3,2},{4,1},{4,3},{5,1},{5,3},{6,2}},
    },
};

/* Get tier name from row position */
static const char* RowTier(int row) {
    if (row <= 2) return "T1";
    if (row <= 4) return "T2";
    return "T3";
}

/* Get tier for a specific slot given class and tab */
static const char* SlotTier(int cls, int tab, int slot) {
    if (cls < 0 || cls > 6 || tab < 0 || tab > 2 || slot < 0 || slot >= MAX_SLOTS)
        return "?";
    return RowTier(TREE_POS[cls][tab][slot][0]);
}

/* Universal skill icon map: skillId -> frame index in the universal DC6.
 * All 7 class DC6 files are identical and contain all 210 skill icons.
 * -1 = no mapping (use original nIconCel). */
static int g_skillIconMap[400];
static BOOL g_iconMapLoaded = FALSE;

static void LoadIconMap(void) {
    memset(g_skillIconMap, 0xFF, sizeof(g_skillIconMap)); /* -1 = unmapped */
    char path[MAX_PATH];
    GetArchPath(path, "skill_icon_map.dat");
    FILE* f = fopen(path, "r");
    if (!f) { Log("No skill_icon_map.dat\n"); g_iconMapLoaded = TRUE; return; }
    char line[64];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        int id, frame;
        if (sscanf(line, "%d=%d", &id, &frame) == 2) {
            if (id >= 0 && id < 400 && frame >= 0 && frame <= 255) {
                g_skillIconMap[id] = frame;
                count++;
            }
        }
    }
    fclose(f);
    g_iconMapLoaded = TRUE;
    Log("Loaded %d icon mappings\n", count);
}

/* Set nIconCel for a skill using the universal icon map */
static void SetSkillIcon(int skillId) {
    if (!g_iconMapLoaded || skillId < 0 || skillId >= 400) return;
    int frame = g_skillIconMap[skillId];
    if (frame < 0 || frame > 255) return;

    int descIdx = GetSkillDescIdx(skillId);
    if (descIdx < 0) return;

    DWORD dt = GetSgptDT(); if (!dt) return;
    __try {
        DWORD descArr = *(DWORD*)(dt + DT_SKILLDESC);
        int descCnt = *(int*)(dt + DT_SKILLDESC_N);
        if (!descArr || descIdx >= descCnt) return;
        DWORD rec = descArr + descIdx * SDT_SIZE;

        DWORD op;
        VirtualProtect((void*)(rec + SDT_ICONCEL), 1, PAGE_READWRITE, &op);
        *(BYTE*)(rec + SDT_ICONCEL) = (BYTE)frame;
        VirtualProtect((void*)(rec + SDT_ICONCEL), 1, op, &op);
    } __except(1) {}
}

/* ================================================================
 * STATE
 * ================================================================ */
static BOOL g_editorOpen    = FALSE;
static BOOL g_menuOpen      = FALSE;
static BOOL g_questTracker  = FALSE;
static BOOL g_resetConfirm  = FALSE;
static int  g_activeTab     = 0;  /* 0,1,2 = green tabs, 3 = locked */
static int  g_scroll        = 0;
static int  g_infoIdx       = -1; /* skill index for info panel, -1 = none */
static BOOL g_applied       = FALSE;
static char g_charName[17]  = ""; /* current character name for per-char saves */

/* Click state */
static BOOL g_wasLDown = FALSE;
static BOOL g_click    = FALSE;

static void UpdateClick(void) {
    BOOL ld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    g_click = (ld && !g_wasLDown);
    g_wasLDown = ld;
}

static BOOL InRect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x+w && py >= y && py < y+h;
}

/* ================================================================
 * QUEST SYSTEM
 * ================================================================ */
/* Memory addresses for 1.10f */
#define MONSTER_HASH_OFF    0x11AC00
#define AREA_ID_OFF         0x11C1B4
#define HASH_TABLE_SIZE     128
#define UNIT_NEXT_OFF       0xE8

/* Monster animation modes */
#define MODE_DEATH   0
#define MODE_WALK    2
#define MODE_ATTACK1 4
#define MODE_ATTACK2 5
#define MODE_DEAD    12

typedef enum { QTYPE_BOSS, QTYPE_AREA, QTYPE_KILL } QuestType;

typedef enum { REWARD_SKILL, REWARD_GOLD } RewardType;

typedef struct {
    int         id;
    const char* name;
    const char* desc;
    QuestType   type;
    int         param;      /* boss txtId, area id, or area id for kills */
    int         killReq;    /* kills required (QTYPE_KILL only) */
    int         killCount;  /* current kill count (runtime) */
    BOOL        completed;
    RewardType  reward;     /* REWARD_SKILL = progression, REWARD_GOLD = filler */
    int         goldAmount; /* random gold reward (set at char creation, 100-10000) */
} Quest;

/* ---- Act 1 Normal quests ---- */
/*                id  name                        desc                                type        param req cnt done   reward        gold */
static Quest g_act1Quests[] = {
    /* Story quests — progression (unlock skills) */
    {  1, "Den of Evil",             "Enter the Den of Evil",                    QTYPE_AREA, 8,   0,0, FALSE, REWARD_SKILL, 0 },
    {  2, "Sisters' Burial Grounds", "Kill Blood Raven",                         QTYPE_BOSS, 267, 0,0, FALSE, REWARD_SKILL, 0 },
    {  3, "The Search for Cain",     "Reach Tristram",                           QTYPE_AREA, 38,  0,0, FALSE, REWARD_SKILL, 0 },
    {  4, "The Forgotten Tower",     "Reach Tower Cellar Level 5",               QTYPE_AREA, 20,  0,0, FALSE, REWARD_SKILL, 0 },
    {  5, "Tools of the Trade",      "Enter the Barracks",                       QTYPE_AREA, 28,  0,0, FALSE, REWARD_SKILL, 0 },
    {  6, "Sisters to the Slaughter","Kill Andariel",                            QTYPE_BOSS, 156, 0,0, FALSE, REWARD_SKILL, 0 },
    /* Per-area kill quests — filler (gold reward, randomized at char creation) */
    { 10, "Clear Blood Moor",           "Kill 25 monsters in Blood Moor",            QTYPE_KILL, 2,  25,0, FALSE, REWARD_GOLD, 0 },
    { 11, "Clear Cold Plains",          "Kill 25 monsters in Cold Plains",            QTYPE_KILL, 3,  25,0, FALSE, REWARD_GOLD, 0 },
    { 12, "Clear Stony Field",          "Kill 30 monsters in Stony Field",            QTYPE_KILL, 4,  30,0, FALSE, REWARD_GOLD, 0 },
    { 13, "Clear Dark Wood",            "Kill 30 monsters in Dark Wood",              QTYPE_KILL, 5,  30,0, FALSE, REWARD_GOLD, 0 },
    { 14, "Clear Black Marsh",          "Kill 30 monsters in Black Marsh",            QTYPE_KILL, 6,  30,0, FALSE, REWARD_GOLD, 0 },
    { 15, "Clear Tamoe Highland",       "Kill 30 monsters in Tamoe Highland",         QTYPE_KILL, 7,  30,0, FALSE, REWARD_GOLD, 0 },
    { 16, "Clear Den of Evil",          "Kill 20 monsters in the Den of Evil",        QTYPE_KILL, 8,  20,0, FALSE, REWARD_GOLD, 0 },
    { 17, "Clear Cave Level 1",         "Kill 20 monsters in Cave Level 1",           QTYPE_KILL, 9,  20,0, FALSE, REWARD_GOLD, 0 },
    { 18, "Clear Underground Passage",  "Kill 20 monsters in Underground Passage",    QTYPE_KILL, 10, 20,0, FALSE, REWARD_GOLD, 0 },
    { 19, "Clear Burial Grounds",       "Kill 15 monsters in Burial Grounds",         QTYPE_KILL, 17, 15,0, FALSE, REWARD_GOLD, 0 },
    { 20, "Clear The Crypt",            "Kill 20 monsters in the Crypt",              QTYPE_KILL, 18, 20,0, FALSE, REWARD_GOLD, 0 },
    { 21, "Clear Mausoleum",            "Kill 20 monsters in the Mausoleum",          QTYPE_KILL, 19, 20,0, FALSE, REWARD_GOLD, 0 },
    { 22, "Clear Tower Cellar L1",      "Kill 15 monsters in Tower Cellar Level 1",   QTYPE_KILL, 21, 15,0, FALSE, REWARD_GOLD, 0 },
    { 23, "Clear Tower Cellar L2",      "Kill 15 monsters in Tower Cellar Level 2",   QTYPE_KILL, 22, 15,0, FALSE, REWARD_GOLD, 0 },
    { 24, "Clear Tower Cellar L3",      "Kill 15 monsters in Tower Cellar Level 3",   QTYPE_KILL, 23, 15,0, FALSE, REWARD_GOLD, 0 },
    { 25, "Clear Tower Cellar L4",      "Kill 15 monsters in Tower Cellar Level 4",   QTYPE_KILL, 24, 15,0, FALSE, REWARD_GOLD, 0 },
    { 26, "Clear Tower Cellar L5",      "Kill 10 monsters in Tower Cellar Level 5",   QTYPE_KILL, 25, 10,0, FALSE, REWARD_GOLD, 0 },
    { 27, "Clear Monastery Gate",       "Kill 15 monsters at the Monastery Gate",     QTYPE_KILL, 26, 15,0, FALSE, REWARD_GOLD, 0 },
    { 28, "Clear Outer Cloister",       "Kill 15 monsters in Outer Cloister",         QTYPE_KILL, 27, 15,0, FALSE, REWARD_GOLD, 0 },
    { 29, "Clear Barracks",             "Kill 20 monsters in the Barracks",           QTYPE_KILL, 28, 20,0, FALSE, REWARD_GOLD, 0 },
    { 30, "Clear Jail Level 1",         "Kill 25 monsters in Jail Level 1",           QTYPE_KILL, 29, 25,0, FALSE, REWARD_GOLD, 0 },
    { 31, "Clear Jail Level 2",         "Kill 25 monsters in Jail Level 2",           QTYPE_KILL, 30, 25,0, FALSE, REWARD_GOLD, 0 },
    { 32, "Clear Jail Level 3",         "Kill 25 monsters in Jail Level 3",           QTYPE_KILL, 31, 25,0, FALSE, REWARD_GOLD, 0 },
    { 33, "Clear Cathedral",            "Kill 20 monsters in the Cathedral",          QTYPE_KILL, 33, 20,0, FALSE, REWARD_GOLD, 0 },
    { 34, "Clear Catacombs L1",         "Kill 30 monsters in Catacombs Level 1",      QTYPE_KILL, 34, 30,0, FALSE, REWARD_GOLD, 0 },
    { 35, "Clear Catacombs L2",         "Kill 35 monsters in Catacombs Level 2",      QTYPE_KILL, 35, 35,0, FALSE, REWARD_GOLD, 0 },
    { 36, "Clear Catacombs L3",         "Kill 40 monsters in Catacombs Level 3",      QTYPE_KILL, 36, 40,0, FALSE, REWARD_GOLD, 0 },
    { 37, "Clear Tristram",             "Kill 20 monsters in Tristram",               QTYPE_KILL, 38, 20,0, FALSE, REWARD_GOLD, 0 },
};
#define ACT1_NUM (sizeof(g_act1Quests)/sizeof(g_act1Quests[0]))

/* ---- Placeholder acts (quests added later) ---- */
/* ---- Act 2 quests ---- */
static Quest g_act2Quests[] = {
    /* Story quests */
    {101, "Radament's Lair",     "Kill Radament in the Sewers",        QTYPE_BOSS, 229, 0,0, FALSE, REWARD_SKILL, 0 },
    {102, "The Horadric Staff",  "Reach Tal Rasha's Tomb",             QTYPE_AREA, 66,  0,0, FALSE, REWARD_SKILL, 0 },
    {103, "Tainted Sun",         "Enter the Claw Viper Temple",        QTYPE_AREA, 58,  0,0, FALSE, REWARD_SKILL, 0 },
    {104, "Arcane Sanctuary",    "Reach the Arcane Sanctuary",         QTYPE_AREA, 74,  0,0, FALSE, REWARD_SKILL, 0 },
    {105, "The Summoner",        "Kill The Summoner",                  QTYPE_BOSS, 250, 0,0, FALSE, REWARD_SKILL, 0 },
    {106, "Seven Tombs",         "Kill Duriel",                        QTYPE_BOSS, 211, 0,0, FALSE, REWARD_SKILL, 0 },
    /* Kill quests */
    {110, "Clear Rocky Waste",         "Kill 25 monsters in Rocky Waste",         QTYPE_KILL, 41, 25,0, FALSE, REWARD_GOLD, 0 },
    {111, "Clear Dry Hills",           "Kill 25 monsters in Dry Hills",           QTYPE_KILL, 42, 25,0, FALSE, REWARD_GOLD, 0 },
    {112, "Clear Far Oasis",           "Kill 30 monsters in Far Oasis",           QTYPE_KILL, 43, 30,0, FALSE, REWARD_GOLD, 0 },
    {113, "Clear Lost City",           "Kill 30 monsters in Lost City",           QTYPE_KILL, 44, 30,0, FALSE, REWARD_GOLD, 0 },
    {114, "Clear Valley of Snakes",    "Kill 20 monsters in Valley of Snakes",    QTYPE_KILL, 45, 20,0, FALSE, REWARD_GOLD, 0 },
    {115, "Clear Sewers L1",           "Kill 15 monsters in Sewers Level 1",      QTYPE_KILL, 47, 15,0, FALSE, REWARD_GOLD, 0 },
    {116, "Clear Sewers L2",           "Kill 15 monsters in Sewers Level 2",      QTYPE_KILL, 48, 15,0, FALSE, REWARD_GOLD, 0 },
    {117, "Clear Halls of Dead L1",    "Kill 20 monsters in Halls of the Dead",   QTYPE_KILL, 56, 20,0, FALSE, REWARD_GOLD, 0 },
    {118, "Clear Halls of Dead L2",    "Kill 20 monsters in Halls of the Dead 2", QTYPE_KILL, 57, 20,0, FALSE, REWARD_GOLD, 0 },
    {119, "Clear Halls of Dead L3",    "Kill 15 monsters in Halls of the Dead 3", QTYPE_KILL, 60, 15,0, FALSE, REWARD_GOLD, 0 },
    {120, "Clear Maggot Lair L1",      "Kill 15 monsters in Maggot Lair",         QTYPE_KILL, 62, 15,0, FALSE, REWARD_GOLD, 0 },
    {121, "Clear Maggot Lair L2",      "Kill 15 monsters in Maggot Lair 2",       QTYPE_KILL, 63, 15,0, FALSE, REWARD_GOLD, 0 },
    {122, "Clear Maggot Lair L3",      "Kill 15 monsters in Maggot Lair 3",       QTYPE_KILL, 64, 15,0, FALSE, REWARD_GOLD, 0 },
    {123, "Clear Ancient Tunnels",     "Kill 25 monsters in Ancient Tunnels",     QTYPE_KILL, 65, 25,0, FALSE, REWARD_GOLD, 0 },
    {124, "Clear Arcane Sanctuary",    "Kill 30 monsters in Arcane Sanctuary",    QTYPE_KILL, 74, 30,0, FALSE, REWARD_GOLD, 0 },
    {125, "Clear Palace Cellar L1",    "Kill 15 monsters in Palace Cellar",       QTYPE_KILL, 52, 15,0, FALSE, REWARD_GOLD, 0 },
    {126, "Clear Palace Cellar L2",    "Kill 15 monsters in Palace Cellar 2",     QTYPE_KILL, 53, 15,0, FALSE, REWARD_GOLD, 0 },
    {127, "Clear Palace Cellar L3",    "Kill 15 monsters in Palace Cellar 3",     QTYPE_KILL, 54, 15,0, FALSE, REWARD_GOLD, 0 },
    {128, "Clear Canyon of Magi",      "Kill 20 monsters in Canyon of the Magi",  QTYPE_KILL, 46, 20,0, FALSE, REWARD_GOLD, 0 },
    {129, "Clear Stony Tomb",          "Kill 15 monsters in Stony Tomb",          QTYPE_KILL, 55, 15,0, FALSE, REWARD_GOLD, 0 },
};

/* ---- Act 3 quests ---- */
static Quest g_act3Quests[] = {
    /* Story quests */
    {201, "Spider Cavern",       "Enter the Spider Cavern",            QTYPE_AREA, 85,  0,0, FALSE, REWARD_SKILL, 0 },
    {202, "Blade of Old Religion","Reach the Flayer Jungle",           QTYPE_AREA, 78,  0,0, FALSE, REWARD_SKILL, 0 },
    {203, "Khalim's Will",       "Reach Travincal",                    QTYPE_AREA, 83,  0,0, FALSE, REWARD_SKILL, 0 },
    {204, "Lam Esen's Tome",     "Enter the Ruined Temple",           QTYPE_AREA, 94,  0,0, FALSE, REWARD_SKILL, 0 },
    {205, "The Blackened Temple","Enter Durance of Hate",              QTYPE_AREA, 100, 0,0, FALSE, REWARD_SKILL, 0 },
    {206, "The Guardian",        "Kill Mephisto",                      QTYPE_BOSS, 242, 0,0, FALSE, REWARD_SKILL, 0 },
    /* Kill quests */
    {210, "Clear Spider Forest",       "Kill 30 monsters in Spider Forest",       QTYPE_KILL, 76, 30,0, FALSE, REWARD_GOLD, 0 },
    {211, "Clear Great Marsh",         "Kill 30 monsters in Great Marsh",         QTYPE_KILL, 77, 30,0, FALSE, REWARD_GOLD, 0 },
    {212, "Clear Flayer Jungle",       "Kill 30 monsters in Flayer Jungle",       QTYPE_KILL, 78, 30,0, FALSE, REWARD_GOLD, 0 },
    {213, "Clear Lower Kurast",        "Kill 25 monsters in Lower Kurast",        QTYPE_KILL, 79, 25,0, FALSE, REWARD_GOLD, 0 },
    {214, "Clear Kurast Bazaar",       "Kill 25 monsters in Kurast Bazaar",       QTYPE_KILL, 80, 25,0, FALSE, REWARD_GOLD, 0 },
    {215, "Clear Upper Kurast",        "Kill 25 monsters in Upper Kurast",        QTYPE_KILL, 81, 25,0, FALSE, REWARD_GOLD, 0 },
    {216, "Clear Travincal",           "Kill 25 monsters in Travincal",           QTYPE_KILL, 83, 25,0, FALSE, REWARD_GOLD, 0 },
    {217, "Clear Spider Cave",         "Kill 15 monsters in Spider Cave",         QTYPE_KILL, 84, 15,0, FALSE, REWARD_GOLD, 0 },
    {218, "Clear Flayer Dungeon L1",   "Kill 20 monsters in Flayer Dungeon",      QTYPE_KILL, 88, 20,0, FALSE, REWARD_GOLD, 0 },
    {219, "Clear Flayer Dungeon L2",   "Kill 20 monsters in Flayer Dungeon 2",    QTYPE_KILL, 89, 20,0, FALSE, REWARD_GOLD, 0 },
    {220, "Clear Sewers L1",           "Kill 15 monsters in Kurast Sewers",       QTYPE_KILL, 92, 15,0, FALSE, REWARD_GOLD, 0 },
    {221, "Clear Sewers L2",           "Kill 15 monsters in Kurast Sewers 2",     QTYPE_KILL, 93, 15,0, FALSE, REWARD_GOLD, 0 },
    {222, "Clear Durance L1",          "Kill 25 monsters in Durance of Hate",     QTYPE_KILL, 100,25,0, FALSE, REWARD_GOLD, 0 },
    {223, "Clear Durance L2",          "Kill 30 monsters in Durance of Hate 2",   QTYPE_KILL, 101,30,0, FALSE, REWARD_GOLD, 0 },
    {224, "Clear Kurast Causeway",     "Kill 20 monsters in Kurast Causeway",     QTYPE_KILL, 82, 20,0, FALSE, REWARD_GOLD, 0 },
};

/* ---- Act 4 quests ---- */
static Quest g_act4Quests[] = {
    /* Story quests (Act 4 only has 3 real quests) */
    {301, "The Fallen Angel",    "Kill Izual",                         QTYPE_BOSS, 256, 0,0, FALSE, REWARD_SKILL, 0 },
    {302, "Hell's Forge",        "Reach the River of Flame",           QTYPE_AREA, 107, 0,0, FALSE, REWARD_SKILL, 0 },
    {303, "Terror's End",        "Kill Diablo",                        QTYPE_BOSS, 243, 0,0, FALSE, REWARD_SKILL, 0 },
    /* Kill quests */
    {310, "Clear Outer Steppes",       "Kill 25 monsters in Outer Steppes",       QTYPE_KILL, 104, 25,0, FALSE, REWARD_GOLD, 0 },
    {311, "Clear Plains of Despair",   "Kill 30 monsters in Plains of Despair",   QTYPE_KILL, 105, 30,0, FALSE, REWARD_GOLD, 0 },
    {312, "Clear City of the Damned",  "Kill 30 monsters in City of the Damned",  QTYPE_KILL, 106, 30,0, FALSE, REWARD_GOLD, 0 },
    {313, "Clear River of Flame",      "Kill 35 monsters in River of Flame",      QTYPE_KILL, 107, 35,0, FALSE, REWARD_GOLD, 0 },
    {314, "Clear Chaos Sanctuary",     "Kill 40 monsters in Chaos Sanctuary",     QTYPE_KILL, 108, 40,0, FALSE, REWARD_GOLD, 0 },
};

/* ---- Act 5 quests ---- */
static Quest g_act5Quests[] = {
    /* Story quests */
    {401, "Siege on Harrogath",  "Reach the Bloody Foothills",         QTYPE_AREA, 110, 0,0, FALSE, REWARD_SKILL, 0 },
    {402, "Rescue on Mt. Arreat","Reach the Arreat Plateau",           QTYPE_AREA, 112, 0,0, FALSE, REWARD_SKILL, 0 },
    {403, "Prison of Ice",       "Kill Nihlathak",                     QTYPE_BOSS, 514, 0,0, FALSE, REWARD_SKILL, 0 },
    {404, "Betrayal of Harrogath","Enter Nihlathak's Temple",          QTYPE_AREA, 121, 0,0, FALSE, REWARD_SKILL, 0 },
    {405, "Rite of Passage",     "Reach the Worldstone Keep",          QTYPE_AREA, 128, 0,0, FALSE, REWARD_SKILL, 0 },
    {406, "Eve of Destruction",  "Kill Baal",                          QTYPE_BOSS, 544, 0,0, FALSE, REWARD_SKILL, 0 },
    /* Kill quests */
    {410, "Clear Bloody Foothills",    "Kill 30 monsters in Bloody Foothills",    QTYPE_KILL, 110, 30,0, FALSE, REWARD_GOLD, 0 },
    {411, "Clear Frigid Highlands",    "Kill 30 monsters in Frigid Highlands",    QTYPE_KILL, 111, 30,0, FALSE, REWARD_GOLD, 0 },
    {412, "Clear Arreat Plateau",      "Kill 30 monsters in Arreat Plateau",      QTYPE_KILL, 112, 30,0, FALSE, REWARD_GOLD, 0 },
    {413, "Clear Crystalline Passage", "Kill 20 monsters in Crystalline Passage", QTYPE_KILL, 113, 20,0, FALSE, REWARD_GOLD, 0 },
    {414, "Clear Glacial Caves L1",    "Kill 20 monsters in Glacial Caves",       QTYPE_KILL, 118, 20,0, FALSE, REWARD_GOLD, 0 },
    {415, "Clear Glacial Caves L2",    "Kill 20 monsters in Glacial Caves 2",     QTYPE_KILL, 119, 20,0, FALSE, REWARD_GOLD, 0 },
    {416, "Clear Tundra Wastelands",   "Kill 25 monsters in Tundra Wastelands",   QTYPE_KILL, 117, 25,0, FALSE, REWARD_GOLD, 0 },
    {417, "Clear Halls of Anguish",    "Kill 20 monsters in Halls of Anguish",    QTYPE_KILL, 122, 20,0, FALSE, REWARD_GOLD, 0 },
    {418, "Clear Halls of Death",      "Kill 20 monsters in Halls of Death",      QTYPE_KILL, 123, 20,0, FALSE, REWARD_GOLD, 0 },
    {419, "Clear Halls of Vaught",     "Kill 20 monsters in Halls of Vaught",     QTYPE_KILL, 124, 20,0, FALSE, REWARD_GOLD, 0 },
    {420, "Clear Worldstone L1",       "Kill 30 monsters in Worldstone Keep 1",   QTYPE_KILL, 128, 30,0, FALSE, REWARD_GOLD, 0 },
    {421, "Clear Worldstone L2",       "Kill 35 monsters in Worldstone Keep 2",   QTYPE_KILL, 129, 35,0, FALSE, REWARD_GOLD, 0 },
    {422, "Clear Worldstone L3",       "Kill 40 monsters in Worldstone Keep 3",   QTYPE_KILL, 130, 40,0, FALSE, REWARD_GOLD, 0 },
    {423, "Clear Throne of Destruction","Kill 40 monsters in Throne of Destruction",QTYPE_KILL,131,40,0, FALSE, REWARD_GOLD, 0 },
};

typedef struct {
    const char* name;
    Quest*      quests;
    int         num;
} ActData;

static ActData g_acts[5] = {
    { "Act I",   g_act1Quests, sizeof(g_act1Quests)/sizeof(g_act1Quests[0]) },
    { "Act II",  g_act2Quests, sizeof(g_act2Quests)/sizeof(g_act2Quests[0]) },
    { "Act III", g_act3Quests, sizeof(g_act3Quests)/sizeof(g_act3Quests[0]) },
    { "Act IV",  g_act4Quests, sizeof(g_act4Quests)/sizeof(g_act4Quests[0]) },
    { "Act V",   g_act5Quests, sizeof(g_act5Quests)/sizeof(g_act5Quests[0]) },
};

/* Total quest count across all acts */
static int TotalQuests(void) {
    int n = 0;
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++)
            if (g_acts[a].quests[q].id > 0) n++;
    return n;
}
static int CompletedQuests(void) {
    int n = 0;
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++)
            if (g_acts[a].quests[q].id > 0 && g_acts[a].quests[q].completed) n++;
    return n;
}

/* Quest Log UI state */
static BOOL g_questLogOpen = FALSE;
static int  g_questLogAct  = 0;  /* 0-4 */
static int  g_questScroll  = 0;

/* Notification system */
static wchar_t g_notifyText[128] = L"";
static DWORD   g_notifyTime = 0;
#define NOTIFY_DURATION 4000

static void ShowNotify(const char* msg) {
    MultiByteToWideChar(CP_ACP, 0, msg, -1, g_notifyText, 128);
    g_notifyTime = GetTickCount();
}

/* Boss tracking via monster hash table */
#define MAX_BOSS_TRACK 32
typedef struct {
    DWORD unitId;
    DWORD txtId;
    int   seenCount;
    int   missCount;
    BOOL  wasInCombat;
    BOOL  gone;
} BossTrack;

static BossTrack g_bossTrack[MAX_BOSS_TRACK];
static int g_bossTrackCount = 0;

/* All boss txtIds we care about across all acts.
 * Cached for performance — called every scan for every monster. */
static BOOL IsTrackedBoss(DWORD txtId) {
    switch (txtId) {
        case 156: return TRUE;  /* Andariel */
        case 211: return TRUE;  /* Duriel */
        case 229: return TRUE;  /* Radament */
        case 242: return TRUE;  /* Mephisto */
        case 243: return TRUE;  /* Diablo */
        case 250: return TRUE;  /* The Summoner */
        case 256: return TRUE;  /* Izual */
        case 267: return TRUE;  /* Blood Raven */
        case 514: return TRUE;  /* Nihlathak */
        case 544: return TRUE;  /* Baal */
        default:  return FALSE;
    }
}

static DWORD SafeRead(DWORD addr) {
    DWORD val = 0;
    __try { val = *(DWORD*)addr; } __except(1) {}
    return val;
}

/* Get current area/level ID from player unit path chain:
 * Player(+0x2C) → pDynamicPath → (+0x1C) pRoom → (+0x10) pRoomEx → (+0x00) pRoomExNext...
 * In 1.10f the level number is stored differently. Use D2COMMON ordinal 10691:
 * int __stdcall D2COMMON_GetLevelNoFromRoom(Room* pRoom) */
typedef int (__stdcall *GetLevelFromRoom_t)(DWORD pRoom);
static GetLevelFromRoom_t fnGetLevelFromRoom = NULL;

static int GetCurrentArea(void) {
    DWORD p = Player(); if (!p) return 0;
    __try {
        /* Unit+0x2C = pDynamicPath
         * Path+0x1C = pRoom (D2ActiveRoomStrc)
         * Room+0x38 = pDrlgRoom (D2DrlgRoomStrc)
         * DrlgRoom+0x00 = pLevel (D2DrlgLevelStrc)
         * Level+0x04 = nLevelId */
        DWORD pPath = *(DWORD*)(p + 0x2C);
        if (!pPath) return 0;
        DWORD pRoom = *(DWORD*)(pPath + 0x1C);
        if (!pRoom) return 0;
        DWORD pDrlgRoom = *(DWORD*)(pRoom + 0x38);
        if (!pDrlgRoom) return 0;
        DWORD pLevel = *(DWORD*)(pDrlgRoom + 0x00);
        if (!pLevel) return 0;
        return *(int*)(pLevel + 0x04);
    } __except(1) {}
    return 0;
}

/* --- Forward declarations --- */
static void SaveStateFile(void);
static void WriteChecksFile(void);

static void OnQuestComplete(Quest* quest) {
    if (!quest || quest->completed || quest->id == 0) return;
    quest->completed = TRUE;
    Log("QUEST COMPLETE: [%d] %s (reward=%d)\n", quest->id, quest->name, quest->reward);

    if (quest->reward == REWARD_SKILL) {
        /* Progression: unlock next locked skill */
        for (int i = 0; i < g_numSkills; i++) {
            if (!g_skills[i].unlocked) {
                g_skills[i].unlocked = TRUE;
                char msg[128];
                snprintf(msg, sizeof(msg), "UNLOCKED: %s", g_skills[i].name);
                ShowNotify(msg);
                Log("AUTO-UNLOCK: %s (skill %d)\n", g_skills[i].name, g_skills[i].id);
                break;
            }
        }
    } else {
        /* Filler: give gold reward */
        int gold = quest->goldAmount;
        if (gold <= 0) gold = 500; /* fallback */
        DWORD p = Player();
        if (p && fnSetStat) {
            int curGold = ReadStat(STAT_GOLD);
            fnSetStat(p, STAT_GOLD, curGold + gold, 0);
        }
        char msg[128];
        snprintf(msg, sizeof(msg), "Reward: %d gold!", gold);
        ShowNotify(msg);
        Log("GOLD REWARD: %d gold\n", gold);
    }

    SaveStateFile();
    WriteChecksFile();
}

/* Per-area kill counts (indexed by area ID) */
#define MAX_AREA_ID 150
static int g_areaKills[MAX_AREA_ID];

/* Track which unit IDs we already counted as dead (avoid double-counting) */
#define MAX_DEAD_TRACKED 512
static DWORD g_deadIds[MAX_DEAD_TRACKED];
static int g_deadCount = 0;

/* Unit flags */
#define UNITFLAG_ISDEAD 0x00010000

/* Town areas — never count kills here */
static BOOL IsTown(DWORD area) {
    return (area == 0 || area == 1 || area == 40 || area == 75 || area == 103 || area == 109);
}

/* Scan dead monsters in current room by walking the unit linked list.
 * Room+0x2C = pUnitFirst, Unit+0xE8 = pRoomNext.
 * Check UNITFLAG_ISDEAD (0x10000) in dwFlags or dwAnimMode == 12 (DEAD). */
static void ScanMonsters(void) {
    DWORD p = Player(); if (!p) return;
    int currentArea = GetCurrentArea();
    if (IsTown(currentArea) || currentArea <= 0) return;

    __try {
        /* Get player's current room: Unit+0x2C → pPath, Path+0x1C → pRoom */
        DWORD pPath = *(DWORD*)(p + 0x2C); if (!pPath) return;
        DWORD pRoom = *(DWORD*)(pPath + 0x1C); if (!pRoom) return;

        /* Also scan nearby rooms — Room+0x24 = ppRoomList, Room+0x28 = nNumRooms */
        DWORD *ppRoomList = (DWORD*)SafeRead(pRoom + 0x24);
        int nNumRooms = (int)SafeRead(pRoom + 0x28);
        if (nNumRooms > 20) nNumRooms = 20; /* sanity cap */

        /* Rooms to scan: current + nearby */
        DWORD rooms[21];
        int roomCount = 0;
        rooms[roomCount++] = pRoom;
        if (ppRoomList && nNumRooms > 0) {
            for (int r = 0; r < nNumRooms && roomCount < 21; r++) {
                DWORD nr = SafeRead((DWORD)ppRoomList + r * 4);
                if (nr && nr != pRoom) rooms[roomCount++] = nr;
            }
        }

        for (int ri = 0; ri < roomCount; ri++) {
            /* Walk unit linked list: Room+0x2C = pUnitFirst */
            DWORD unit = SafeRead(rooms[ri] + 0x2C);
            int chain = 0;
            while (unit && chain++ < 200) {
                DWORD type   = SafeRead(unit + 0x00);  /* dwUnitType */
                DWORD txtId  = SafeRead(unit + 0x04);  /* dwClassId/txtFileNo */
                DWORD unitId = SafeRead(unit + 0x0C);  /* dwUnitId */
                DWORD mode   = SafeRead(unit + 0x10);  /* dwAnimMode */
                DWORD flags  = SafeRead(unit + 0x08);  /* dwFlags — actually at 0x08 in some builds */

                /* Try flags at multiple offsets — D2MOO says dwFlags varies */
                DWORD flags2 = SafeRead(unit + 0xC4);  /* another common flags offset */

                if (type == 1 && unitId != 0 && txtId < 700) {
                    /* Check if dead: mode 12 (DEAD) or mode 0 (DEATH animation) */
                    BOOL isDead = (mode == MODE_DEAD || mode == MODE_DEATH);

                    if (isDead) {
                        /* Check if we already counted this unit */
                        BOOL alreadyCounted = FALSE;
                        for (int d = 0; d < g_deadCount; d++) {
                            if (g_deadIds[d] == unitId) { alreadyCounted = TRUE; break; }
                        }

                        if (!alreadyCounted) {
                            /* New kill! */
                            if (g_deadCount < MAX_DEAD_TRACKED)
                                g_deadIds[g_deadCount++] = unitId;

                            if (currentArea < MAX_AREA_ID)
                                g_areaKills[currentArea]++;

                            Log("KILL: txt=%d id=%08X mode=%d area=%d total=%d\n",
                                txtId, unitId, mode, currentArea,
                                currentArea < MAX_AREA_ID ? g_areaKills[currentArea] : 0);

                            /* Check boss quests */
                            if (IsTrackedBoss(txtId)) {
                                Log("BOSS KILLED: txt=%d\n", txtId);
                                for (int a = 0; a < 5; a++)
                                    for (int q = 0; q < g_acts[a].num; q++)
                                        if (g_acts[a].quests[q].type == QTYPE_BOSS &&
                                            g_acts[a].quests[q].param == (int)txtId &&
                                            !g_acts[a].quests[q].completed)
                                            OnQuestComplete(&g_acts[a].quests[q]);
                            }
                        }
                    }
                }

                unit = SafeRead(unit + 0xE8); /* pRoomNext */
            }
        }
    } __except(1) {}

    /* Update kill quest counts */
    for (int a = 0; a < 5; a++) {
        for (int q = 0; q < g_acts[a].num; q++) {
            Quest *quest = &g_acts[a].quests[q];
            if (quest->type == QTYPE_KILL && !quest->completed) {
                int areaId = quest->param;
                if (areaId >= 0 && areaId < MAX_AREA_ID) {
                    quest->killCount = g_areaKills[areaId];
                    if (quest->killCount >= quest->killReq)
                        OnQuestComplete(quest);
                }
            }
        }
    }

    /* Compact dead IDs when full */
    if (g_deadCount >= MAX_DEAD_TRACKED)
        g_deadCount = 0;
}

/* Check if player entered a quest area */
static void CheckAreaReach(void) {
    static DWORD lastArea = 0;
    if (!hCli) return;
    DWORD area = GetCurrentArea();
    if (area != lastArea) {
        lastArea = area;
        for (int a = 0; a < 5; a++)
            for (int q = 0; q < g_acts[a].num; q++)
                if (g_acts[a].quests[q].type == QTYPE_AREA &&
                    g_acts[a].quests[q].param == (int)area &&
                    !g_acts[a].quests[q].completed)
                    OnQuestComplete(&g_acts[a].quests[q]);
    }
}

static void RunCheckDetection(void) {
    if (!Player()) return;
    static int runCount = 0;
    runCount++;
    if (runCount <= 3 || runCount % 100 == 0) {
        DWORD area = hCli ? GetCurrentArea() : 9999;
        Log("RunCheckDetection #%d: area=%d isTown=%d\n", runCount, area, IsTown(area));
    }
    ScanMonsters();
    CheckAreaReach();
}

/* ================================================================
 * SAVE / LOAD
 * ================================================================ */
static void GetArchPath(char* out, const char* file) {
    GetModuleFileNameA(NULL, out, MAX_PATH);
    char* s = strrchr(out, '\\');
    if (s) { strcpy(s+1, "Archipelago\\"); strcat(out, file); }
}

/* Skill tier based on original vanilla tree row position.
 * Row 1-2=T1 (basic), Row 3-4=T2 (intermediate), Row 5-6=T3 (advanced). */
static int GetSkillTier(int id) {
    switch(id) {
        /* Amazon T1 */ case 6: case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15: return 1;
        /* Amazon T2 */ case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: return 2;
        /* Amazon T3 */ case 26: case 27: case 28: case 29: case 30: case 31: case 32: case 33: case 34: case 35: return 3;
        /* Sorc T1 */   case 36: case 37: case 38: case 39: case 40: case 41: case 42: case 43: case 44: case 45: return 1;
        /* Sorc T2 */   case 46: case 47: case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55: return 2;
        /* Sorc T3 */   case 56: case 57: case 58: case 59: case 60: case 61: case 62: case 63: case 64: case 65: return 3;
        /* Necro T1 */  case 66: case 67: case 68: case 69: case 70: case 71: case 72: case 73: case 74: case 75: return 1;
        /* Necro T2 */  case 76: case 77: case 78: case 79: case 80: case 81: case 82: case 83: case 84: case 85: return 2;
        /* Necro T3 */  case 86: case 87: case 88: case 89: case 90: case 91: case 92: case 93: case 94: case 95: return 3;
        /* Pala T1 */   case 96: case 97: case 98: case 99: case 100: case 101: case 102: case 103: case 104: case 105: return 1;
        /* Pala T2 */   case 106: case 107: case 108: case 109: case 110: case 111: case 112: case 113: case 114: case 115: return 2;
        /* Pala T3 */   case 116: case 117: case 118: case 119: case 120: case 121: case 122: case 123: case 124: case 125: return 3;
        /* Barb T1 */   case 126: case 127: case 128: case 129: case 130: case 131: case 132: case 133: case 134: case 135: case 136: case 137: case 138: return 1;
        /* Barb T2 */   case 139: case 140: case 141: case 142: case 143: case 144: case 145: case 146: return 2;
        /* Barb T3 */   case 147: case 148: case 149: case 150: case 151: case 152: case 153: case 154: case 155: return 3;
        /* Druid T1 */  case 221: case 222: case 223: case 224: case 225: case 226: case 227: case 228: case 229: case 230: return 1;
        /* Druid T2 */  case 231: case 232: case 233: case 234: case 235: case 236: case 237: case 238: case 239: case 240: return 2;
        /* Druid T3 */  case 241: case 242: case 243: case 244: case 245: case 246: case 247: case 248: case 249: case 250: return 3;
        /* Asn T1 */    case 251: case 252: case 253: case 254: case 255: case 256: case 257: case 258: case 259: case 260: return 1;
        /* Asn T2 */    case 261: case 262: case 263: case 264: case 265: case 266: case 267: case 268: case 269: case 270: return 2;
        /* Asn T3 */    case 271: case 272: case 273: case 274: case 275: case 276: case 277: case 278: case 279: case 280: return 3;
        default: return 1;
    }
}

/* Skill descriptions — from D2Strings.h + manually for expansion */
static const char* GetSkillDesc(int id) {
    switch(id) {
    case 6: return "Creates a magical arrow that always hits";
    case 7: return "Enchants arrows with fire damage";
    case 8: return "Illuminates and weakens nearby enemies";
    case 9: return "Passive - chance of doing double damage";
    case 10: return "Rapid attacks with a thrusting weapon";
    case 11: return "Enchants arrows to slow targets with cold";
    case 12: return "Fires multiple arrows at once";
    case 13: return "Passive - dodges melee attacks while standing";
    case 14: return "Adds lightning damage to javelin attacks";
    case 15: return "Adds poison damage to javelin attacks";
    case 16: return "Enchants arrows to explode on impact";
    case 17: return "Slows ranged attacks of nearby enemies";
    case 18: return "Passive - dodges enemy missiles";
    case 19: return "Increases damage but degrades weapon";
    case 20: return "Transforms javelin into a lightning bolt";
    case 21: return "Enchants arrows to freeze enemies";
    case 22: return "Enchants arrows to seek nearest enemy";
    case 23: return "Passive - increases attack rating";
    case 24: return "Enchants thrusting weapons with lightning";
    case 25: return "Enchants javelins with deadly poison";
    case 26: return "Enchants arrows to strike multiple targets";
    case 27: return "Enchants arrows to burn enemies over time";
    case 28: return "Creates a duplicate of yourself as decoy";
    case 29: return "Passive - dodges attacks when moving";
    case 30: return "Attacks all adjacent targets at once";
    case 31: return "Enchants arrows to freeze multiple enemies";
    case 32: return "Summons a powerful Valkyrie warrior";
    case 33: return "Passive - missiles may continue through targets";
    case 34: return "Enchants thrusting weapons with chain lightning";
    case 35: return "Lightning bolt that splits on impact";
    case 36: return "Creates a missile of flame";
    case 37: return "Passive - increases mana recovery rate";
    case 38: return "Creates multiple deadly sparks";
    case 39: return "Creates a shard of ice that slows enemies";
    case 40: return "Improves defense and freezes attackers";
    case 41: return "Creates a jet of flame";
    case 42: return "Creates a field of deadly sparks";
    case 43: return "Moves objects with your mind";
    case 44: return "Creates a freezing ring around you";
    case 45: return "Creates an ice bolt that freezes targets";
    case 46: return "Creates fire in your wake as you run";
    case 47: return "Creates an explosive sphere of fire";
    case 48: return "Creates an electrically charged ring";
    case 49: return "Creates a powerful bolt of lightning";
    case 50: return "Freezes and damages attackers on hit";
    case 51: return "Creates a wall of flame";
    case 52: return "Enchants melee or ranged weapons with fire";
    case 53: return "Creates a bolt of lightning that chains";
    case 54: return "Instantly teleports to destination";
    case 55: return "Creates a freezing comet of ice";
    case 56: return "Summons a meteor from the heavens";
    case 57: return "Summons a powerful electrical tempest";
    case 58: return "Uses your mana as a damage shield";
    case 59: return "Summons a massive ice storm";
    case 60: return "Retaliates against ranged attacks with cold";
    case 61: return "Passive - increases fire spell damage";
    case 62: return "Summons multi-headed fire beasts";
    case 63: return "Passive - increases lightning spell damage";
    case 64: return "Creates a globe of frozen death";
    case 65: return "Passive - reduces enemy cold resistance";
    case 66: return "Curse - amplifies damage taken by enemies";
    case 67: return "Fires barbed bone teeth";
    case 68: return "Creates a damage absorbing bone armor";
    case 69: return "Passive - improves skeleton warriors and mages";
    case 70: return "Reanimates skeletal warriors from corpses";
    case 71: return "Curse - reduces vision of monsters";
    case 72: return "Curse - reduces damage done by enemies";
    case 73: return "Next dagger attack poisons the target";
    case 74: return "Turns a corpse into a devastating bomb";
    case 75: return "Creates a Clay Golem to fight for you";
    case 76: return "Curse - enemies damage themselves when attacking";
    case 77: return "Curse - monsters run away in fear";
    case 78: return "Creates an impassable wall of bone";
    case 79: return "Enhances speed and life of golems";
    case 80: return "Reanimates skeletal mages from corpses";
    case 81: return "Curse - confused monster attacks randomly";
    case 82: return "Curse - enemies return life to you on hit";
    case 83: return "Turns a corpse into a poison gas cloud";
    case 84: return "Summons a deadly spike of bone";
    case 85: return "A Blood Golem that shares life with you";
    case 86: return "Curse - enemy loses all resistances";
    case 87: return "Curse - greatly slows and weakens target";
    case 88: return "Creates a barrier of bone around target";
    case 89: return "Passive - summoned monsters gain resistances";
    case 90: return "Creates a golem from a metal item";
    case 91: return "Curse - lowers enemy elemental resistances";
    case 92: return "Emits an expanding ring of poison";
    case 93: return "Releases a homing undead spirit";
    case 94: return "Creates a Fire Golem that absorbs fire";
    case 95: return "Raises a dead monster to fight for you";
    case 96: return "Increased accuracy and damage with sacrifice";
    case 97: return "Bash enemies with your shield";
    case 98: return "Aura - increases damage for party";
    case 99: return "Aura - regenerates life for party";
    case 100: return "Aura - protects against fire damage";
    case 101: return "Divine energy that damages undead";
    case 102: return "Aura - flames damage nearby enemies";
    case 103: return "Aura - reflects damage back at attackers";
    case 104: return "Aura - increases defense for party";
    case 105: return "Aura - protects against cold damage";
    case 106: return "Attacks multiple adjacent enemies with zeal";
    case 107: return "Charge at and attack target enemy";
    case 108: return "Aura - increases attack rating for party";
    case 109: return "Aura - reduces poison and curse duration";
    case 110: return "Aura - protects against lightning damage";
    case 111: return "Attacks add elemental damage types";
    case 112: return "Summons a spiraling magic hammer";
    case 113: return "Aura - increases attack and damage";
    case 114: return "Aura - freezes nearby monsters";
    case 115: return "Aura - increases speed and stamina";
    case 116: return "Changes allegiance of monsters";
    case 117: return "Enhances your shield defense";
    case 118: return "Aura - lightning damages nearby enemies";
    case 119: return "Aura - repels and damages undead";
    case 120: return "Aura - increases mana recovery for party";
    case 121: return "Calls down holy lightning on enemies";
    case 122: return "Aura - increases attack speed for party";
    case 123: return "Aura - weakens enemy stats";
    case 124: return "Aura - redeems the dead for mana and life";
    case 125: return "Aura - protects against all elements";
    case 126: return "Powerful blow that increases with low life";
    case 127: return "Passive - improves sword fighting";
    case 128: return "Passive - improves axe fighting";
    case 129: return "Passive - improves mace fighting";
    case 130: return "Frightens nearby monsters with a howl";
    case 131: return "Searches corpses to find potions";
    case 132: return "Leaps over obstacles and enemies";
    case 133: return "Swings two weapons at once";
    case 134: return "Passive - improves pole arm skills";
    case 135: return "Passive - improves thrown weapon skills";
    case 136: return "Passive - improves spear fighting";
    case 137: return "Taunts a monster to attack you";
    case 138: return "War cry that boosts party defense";
    case 139: return "Stuns your target with a heavy blow";
    case 140: return "Throws two weapons at once";
    case 141: return "Passive - increases stamina";
    case 142: return "Searches corpses to find items";
    case 143: return "Leaps and attacks target enemy";
    case 144: return "Uninterruptible focused attack";
    case 145: return "Passive - improves defense rating";
    case 146: return "War cry that reduces enemy effectiveness";
    case 147: return "Frenzied double weapon attacks";
    case 148: return "Passive - increases walk and run speed";
    case 149: return "War cry that boosts life, mana and stamina";
    case 150: return "Creates a frightening totem from corpse";
    case 151: return "Whirling dance of death";
    case 152: return "Powerful but reckless magic-damage attack";
    case 153: return "Passive - increases all resistances";
    case 154: return "Powerful war cry that stuns nearby enemies";
    case 155: return "War cry that increases all skill levels";
    case 221: return "Summons ravens to peck at enemies";
    case 222: return "Summons a poisonous creeper vine";
    case 223: return "Transform into a fast werewolf";
    case 224: return "Passive - enhances shapeshifting duration";
    case 225: return "Creates a wave of fire on the ground";
    case 226: return "Summons an Oak Sage that boosts life";
    case 227: return "Summons spirit wolves to fight for you";
    case 228: return "Transform into a powerful werebear";
    case 229: return "Sends a rolling boulder of fire";
    case 230: return "Creates a blast of arctic air";
    case 231: return "Summons a vine that feeds on corpses";
    case 232: return "Werewolf attack with increased fury";
    case 233: return "Powerful werebear maul attack";
    case 234: return "Creates a volcanic fissure in the ground";
    case 235: return "Absorbs elemental damage around you";
    case 236: return "Summons Heart of Wolverine for damage";
    case 237: return "Summons dire wolves from spirit wolves";
    case 238: return "Infects enemies with spreading rabies";
    case 239: return "Attacks with fire-enchanted claws";
    case 240: return "Creates small twisters that damage enemies";
    case 241: return "Summons a vine that drains enemy mana";
    case 242: return "Devour enemy to restore your life";
    case 243: return "Powerful shockwave that stuns enemies";
    case 244: return "Creates an erupting volcano";
    case 245: return "Summons a powerful damaging tornado";
    case 246: return "Summons Spirit of Barbs for thorns";
    case 247: return "Summons a powerful grizzly bear";
    case 248: return "Furious multi-hit shapeshifting attack";
    case 249: return "Rains fire and meteors from the sky";
    case 250: return "Summons a devastating hurricane";
    case 251: return "Throws a fire bomb that explodes";
    case 252: return "Passive - improves claw combat skills";
    case 253: return "Invisible bolt that knocks enemies back";
    case 254: return "Charge-up: adds lightning on next finisher";
    case 255: return "A powerful kick attack";
    case 256: return "Lays a fire damage web trap on ground";
    case 257: return "Spinning blade that seeks out enemies";
    case 258: return "Passive - increases attack and cast speed";
    case 259: return "Charge-up: adds fire on next finisher";
    case 260: return "Dual claw finishing attack";
    case 261: return "Sentry trap that shoots charged bolts";
    case 262: return "Sentry trap that shoots fire waves";
    case 263: return "Passive - block attacks with dual claws";
    case 264: return "Hides in shadows, reducing enemy defense";
    case 265: return "Charge-up: steals life and mana on finisher";
    case 266: return "Throws spinning blades at enemies";
    case 267: return "Passive - reduces all damage taken";
    case 268: return "Summons a shadow warrior to fight alongside";
    case 269: return "Charge-up: adds chain lightning on finisher";
    case 270: return "Flaming kick that causes fire explosion";
    case 271: return "Sentry trap that shoots lightning";
    case 272: return "Sentry trap that shoots inferno";
    case 273: return "Psychic blast that stuns groups of enemies";
    case 274: return "Charge-up: adds cold damage on finisher";
    case 275: return "Teleporting kick to distant enemies";
    case 276: return "Sentry trap with corpse explosion";
    case 277: return "Spinning blades orbit and protect you";
    case 278: return "Adds deadly poison to all attacks";
    case 279: return "Summons a powerful shadow master";
    case 280: return "Finishing attack that cycles all elements";
    default: return "";
    }
}

/* Get the required tier for a specific slot position */
static int SlotRequiredTier(int cls, int tab, int slot) {
    if (cls < 0 || cls > 6 || tab < 0 || tab > 2 || slot < 0 || slot >= MAX_SLOTS) return 1;
    int row = TREE_POS[cls][tab][slot][0];
    if (row <= 2) return 1;
    if (row <= 4) return 2;
    return 3;
}

/* Set reqlevel and prerequisites for a skill based on its tree position.
 * T1 (row 1-2): reqlevel=1, no prereqs
 * T2 (row 3-4): reqlevel=10, requires 1 point in a T1 skill in same tab
 * T3 (row 5-6): reqlevel=20, requires 1 point in a T2 skill in same tab */
static void SetSkillTierReqs(int skillId, int cls, int tab, int slot) {
    DWORD dt = GetSgptDT(); if (!dt) return;
    __try {
        DWORD arr = *(DWORD*)(dt + DT_SKILLS);
        int cnt = *(int*)(dt + DT_SKILLS_N);
        if (!arr || skillId < 0 || skillId >= cnt) return;
        DWORD rec = arr + skillId * SKT_SIZE;
        DWORD op;

        int row = TREE_POS[cls][tab][slot][0];
        WORD reqLvl = 1;
        if (row >= 3 && row <= 4) reqLvl = 10;
        else if (row >= 5) reqLvl = 20;

        VirtualProtect((void*)(rec + SKT_REQLEVEL), 2, PAGE_READWRITE, &op);
        *(WORD*)(rec + SKT_REQLEVEL) = reqLvl;
        VirtualProtect((void*)(rec + SKT_REQLEVEL), 2, op, &op);

        /* Find prerequisite: T2 needs any T1, T3 needs any T2 in same tab */
        short prereq = -1;
        int needTier = 0;
        if (row >= 3 && row <= 4) needTier = 1;
        else if (row >= 5) needTier = 2;

        if (needTier > 0) {
            for (int s = 0; s < MAX_SLOTS; s++) {
                int idx = g_tabSlots[tab][s];
                if (idx < 0 || idx >= g_numSkills) continue;
                int sRow = TREE_POS[cls][tab][s][0];
                int sTier = (sRow <= 2) ? 1 : (sRow <= 4) ? 2 : 3;
                if (sTier == needTier) {
                    prereq = (short)g_skills[idx].id;
                    break;
                }
            }
        }

        VirtualProtect((void*)(rec + SKT_REQSKILL0), 6, PAGE_READWRITE, &op);
        *(short*)(rec + SKT_REQSKILL0) = prereq;
        *(short*)(rec + SKT_REQSKILL1) = -1;
        *(short*)(rec + SKT_REQSKILL2) = -1;
        VirtualProtect((void*)(rec + SKT_REQSKILL0), 6, op, &op);
    } __except(1) {}
}

/* Read character name from player unit */
static void UpdateCharName(void) {
    DWORD p = Player(); if (!p) return;
    __try {
        DWORD pData = *(DWORD*)(p + 0x14);
        if (pData) {
            for (int i = 0; i < 16; i++) {
                char c = *(char*)(pData + i);
                if (!c) { g_charName[i] = 0; break; }
                g_charName[i] = c;
            }
            g_charName[16] = 0;
        }
    } __except(1) {}
}

/* Get per-character slot file path */
static void GetCharSlotPath(char *out) {
    if (g_charName[0]) {
        char fname[MAX_PATH];
        snprintf(fname, MAX_PATH, "d2arch_slots_%s.dat", g_charName);
        GetArchPath(out, fname);
    } else {
        GetArchPath(out, "d2arch_slots.dat");
    }
}

static void LoadSkillDatabase(void) {
    char path[MAX_PATH];
    /* Try per-character state file first */
    if (g_charName[0]) {
        char fname[MAX_PATH];
        snprintf(fname, MAX_PATH, "d2arch_state_%s.dat", g_charName);
        GetArchPath(path, fname);
        FILE* tf = fopen(path, "r");
        if (tf) { fclose(tf); Log("Using per-char state: %s\n", path); goto load_file; }
    }
    /* Fall back to global state */
    GetArchPath(path,"d2arch_state.dat");
    load_file:;
    FILE* f = fopen(path, "r");
    if (!f) { Log("No state: %s\n", path); return; }

    char line[256];
    BOOL past = FALSE;
    g_numSkills = 0;

    while (fgets(line, sizeof(line), f)) {
        char* nl = strchr(line,'\n'); if(nl) *nl=0;
        char* cr = strchr(line,'\r'); if(cr) *cr=0;
        if (!strcmp(line, "assignments=")) { past = TRUE; continue; }
        if (!past || !line[0]) continue;
        if (g_numSkills >= MAX_SKILLS) break;

        /* Format: name,class,unlocked,skillId */
        char* c1 = strchr(line,','); if(!c1) continue; *c1=0;
        char* cls = c1+1;
        char* c2 = strchr(cls,','); if(!c2) continue; *c2=0;
        char* c3 = strchr(c2+1,','); if(!c3) continue;

        strncpy(g_skills[g_numSkills].name, line, 63);
        g_skills[g_numSkills].name[63] = 0;
        strncpy(g_skills[g_numSkills].cls, cls, 3);
        g_skills[g_numSkills].cls[3] = 0;
        g_skills[g_numSkills].unlocked = atoi(c2+1);
        g_skills[g_numSkills].id = atoi(c3+1);
        g_skills[g_numSkills].tier = GetSkillTier(g_skills[g_numSkills].id);
        g_numSkills++;
    }
    fclose(f);
    Log("Loaded %d skills\n", g_numSkills);
}

static void InitSlots(void) {
    for (int t = 0; t < NUM_TABS; t++)
        for (int s = 0; s < MAX_SLOTS; s++)
            g_tabSlots[t][s] = -1;
}

/* g_savedClass declared via forward decl above */

/* Skill point tracking: how many points the player has invested in each custom skill.
 * We track this ourselves because the .d2s save doesn't preserve cross-class skill points.
 * g_skillPoints[skillId] = number of points invested. */
/* g_skillPoints declared via forward decl above */
static BOOL g_skillPointsInit = FALSE;

static void SaveSlots(void) {
    char path[MAX_PATH];
    GetCharSlotPath(path);
    FILE* f = fopen(path, "w"); if (!f) return;
    /* First line: player class.
     * At DLL_PROCESS_DETACH the player is already gone, so
     * use g_savedClass as fallback to avoid writing class=0 */
    int cls = GetPlayerClass();
    if (cls > 0) g_savedClass = cls;  /* update whenever we have a valid class */
    if (cls <= 0 && g_savedClass > 0) cls = g_savedClass;
    fprintf(f, "class=%d\n", cls);
    for (int t = 0; t < NUM_TABS; t++)
        for (int s = 0; s < MAX_SLOTS; s++)
            if (g_tabSlots[t][s] >= 0)
                fprintf(f, "%d,%d,%d\n", t, s, g_skills[g_tabSlots[t][s]].id);

    /* Skill points are NOT saved here — players spend points in the vanilla
     * skill tree, and the game's .d2s save handles persistence natively. */
    fclose(f);
    Log("Saved slots cls=%d\n", cls);
}

static void LoadSlots(void) {
    InitSlots();
    g_savedClass = -1;
    if (!g_skillPointsInit) { memset(g_skillPoints, 0, sizeof(g_skillPoints)); g_skillPointsInit = TRUE; }
    char path[MAX_PATH];
    GetCharSlotPath(path);
    FILE* f = fopen(path, "r"); if (!f) return;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        char* nl = strchr(line,'\n'); if(nl) *nl=0;
        char* cr = strchr(line,'\r'); if(cr) *cr=0;
        /* Parse class line */
        int clsVal;
        if (sscanf(line, "class=%d", &clsVal) == 1) {
            g_savedClass = clsVal;
            continue;
        }
        /* Parse skill points */
        int spId, spLvl;
        if (sscanf(line, "sp=%d,%d", &spId, &spLvl) == 2) {
            if (spId >= 0 && spId < 400) g_skillPoints[spId] = spLvl;
            continue;
        }
        /* Skip marker line */
        if (!strcmp(line, "skillpoints=")) continue;
        int t, s, id;
        if (sscanf(line, "%d,%d,%d", &t, &s, &id) == 3) {
            if (t >= 0 && t < NUM_TABS && s >= 0 && s < MAX_SLOTS) {
                for (int i = 0; i < g_numSkills; i++) {
                    if (g_skills[i].id == id) {
                        g_tabSlots[t][s] = i;
                        break;
                    }
                }
            }
        }
    }
    fclose(f);
    Log("Loaded slots, savedClass=%d\n", g_savedClass);
}

/* Save state file with current skill unlock status + completed checks */
static void SaveStateFile(void) {
    if (!g_charName[0]) return;
    char path[MAX_PATH];
    char fname[MAX_PATH];
    snprintf(fname, MAX_PATH, "d2arch_state_%s.dat", g_charName);
    GetArchPath(path, fname);

    /* Read header lines (everything before assignments=) */
    char headerBuf[4096] = "";
    int headerLen = 0;
    FILE* rf = fopen(path, "r");
    if (rf) {
        char line[256];
        while (fgets(line, sizeof(line), rf)) {
            int n = (int)strlen(line);
            if (headerLen + n < (int)sizeof(headerBuf)) {
                memcpy(headerBuf + headerLen, line, n);
                headerLen += n;
            }
            if (strstr(line, "assignments=")) break;
        }
        fclose(rf);
    }

    FILE* f = fopen(path, "w");
    if (!f) return;
    if (headerLen > 0) fwrite(headerBuf, 1, headerLen, f);
    else fprintf(f, "assignments=\n");

    for (int i = 0; i < g_numSkills; i++) {
        fprintf(f, "%s,%s,%d,%d\n",
            g_skills[i].name, g_skills[i].cls,
            g_skills[i].unlocked ? 1 : 0, g_skills[i].id);
    }

    /* Save completed quests */
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++)
            if (g_acts[a].quests[q].id > 0 && g_acts[a].quests[q].completed)
                fprintf(f, "quest_%d=1\n", g_acts[a].quests[q].id);

    /* Save per-area kill counts */
    for (int i = 0; i < MAX_AREA_ID; i++)
        if (g_areaKills[i] > 0)
            fprintf(f, "areakills_%d=%d\n", i, g_areaKills[i]);

    /* Save gold reward amounts (so they're consistent across sessions) */
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++)
            if (g_acts[a].quests[q].reward == REWARD_GOLD && g_acts[a].quests[q].goldAmount > 0)
                fprintf(f, "questgold_%d=%d\n", g_acts[a].quests[q].id, g_acts[a].quests[q].goldAmount);

    fclose(f);
    Log("SaveStateFile: saved %d skills + quests to %s\n", g_numSkills, path);
}

/* Write checks file for AP bridge compatibility */
static void WriteChecksFile(void) {
    if (!g_charName[0]) return;
    char path[MAX_PATH], fname[MAX_PATH];
    snprintf(fname, MAX_PATH, "d2arch_checks_%s.dat", g_charName);
    GetArchPath(path, fname);

    FILE* f = fopen(path, "w");
    if (!f) return;
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++)
            if (g_acts[a].quests[q].id > 0 && g_acts[a].quests[q].completed)
                fprintf(f, "check=%d\n", g_acts[a].quests[q].id);
    fclose(f);
}

/* Load completed quests and area kills from state file */
static void LoadChecks(void) {
    /* Reset all quests and kill counts */
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++) {
            g_acts[a].quests[q].completed = FALSE;
            g_acts[a].quests[q].killCount = 0;
        }
    memset(g_areaKills, 0, sizeof(g_areaKills));

    if (!g_charName[0]) return;
    char path[MAX_PATH], fname[MAX_PATH];
    snprintf(fname, MAX_PATH, "d2arch_state_%s.dat", g_charName);
    GetArchPath(path, fname);

    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int qid;
        if (sscanf(line, "quest_%d=1", &qid) == 1) {
            for (int a = 0; a < 5; a++)
                for (int q = 0; q < g_acts[a].num; q++)
                    if (g_acts[a].quests[q].id == qid)
                        g_acts[a].quests[q].completed = TRUE;
        }
        int areaId, kills;
        if (sscanf(line, "areakills_%d=%d", &areaId, &kills) == 2) {
            if (areaId >= 0 && areaId < MAX_AREA_ID)
                g_areaKills[areaId] = kills;
        }
        /* Load gold rewards */
        int gqid, gamt;
        if (sscanf(line, "questgold_%d=%d", &gqid, &gamt) == 2) {
            for (int a = 0; a < 5; a++)
                for (int q = 0; q < g_acts[a].num; q++)
                    if (g_acts[a].quests[q].id == gqid)
                        g_acts[a].quests[q].goldAmount = gamt;
        }
        /* Read seed for gold randomization */
        unsigned int fileSeed = 0;
        if (sscanf(line, "seed=%u", &fileSeed) == 1 && fileSeed > 0) {
            /* Generate gold rewards for filler quests that don't have one yet */
            srand(fileSeed + 777);
        }
    }
    fclose(f);

    /* Generate gold rewards for any filler quest that doesn't have one saved */
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++)
            if (g_acts[a].quests[q].reward == REWARD_GOLD && g_acts[a].quests[q].goldAmount == 0)
                g_acts[a].quests[q].goldAmount = 100 + (rand() % 9901); /* 100-10000 */

    /* Update kill quest counts from loaded area kills */
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++)
            if (g_acts[a].quests[q].type == QTYPE_KILL) {
                int aid = g_acts[a].quests[q].param;
                if (aid >= 0 && aid < MAX_AREA_ID)
                    g_acts[a].quests[q].killCount = g_areaKills[aid];
            }

    Log("LoadChecks: %d/%d quests completed\n", CompletedQuests(), TotalQuests());
}

/* ================================================================
 * nClassSkillList MANIPULATION
 * Write skill IDs directly into the class skill lookup table.
 * This is what D2Client's skill tree renderer actually reads.
 * sgptDT + 0xBA4 = int* nClassSkillCount (per-class counts)
 * sgptDT + 0xBA8 = int nHighestClassSkillCount (stride)
 * sgptDT + 0xBAC = short* nClassSkillList (the array)
 * Access: list[position + classId * stride] = skillId
 * ================================================================ */
static void WriteClassSkillList(void) {
    DWORD dt = GetSgptDT(); if (!dt) return;
    int cls = GetPlayerClass();
    if (cls <= 0 && g_savedClass > 0) cls = g_savedClass;
    if (cls < 0 || cls > 6) return;

    /* Count custom skills first — if none, don't touch the list */
    int numCustom = 0;
    for (int t = 0; t < NUM_TABS; t++)
        for (int s = 0; s < MAX_SLOTS; s++)
            if (g_tabSlots[t][s] >= 0) numCustom++;

    if (numCustom == 0) {
        Log("WriteClassSkillList: no custom skills, leaving original list\n");
        return;
    }

    __try {
        int stride = *(int*)(dt + 0xBA8);
        short* list = *(short**)(dt + 0xBAC);
        int* counts = *(int**)(dt + 0xBA4);
        if (!list || !counts || stride <= 0) {
            Log("WriteClassSkillList: bad ptrs stride=%d list=%08X counts=%08X\n",
                stride, (DWORD)list, (DWORD)counts);
            return;
        }

        /* Save original count — we must NEVER go below it */
        int origCount = counts[cls];

        /* Write our custom skills at the START of the list,
         * keep remaining original skills after them */
        short* classStart = list + cls * stride;
        DWORD op;
        VirtualProtect(classStart, stride * sizeof(short), PAGE_READWRITE, &op);

        /* Collect original skills that are NOT in our custom list */
        short origSkills[30];
        int nOrig = 0;
        for (int i = 0; i < origCount && i < stride && nOrig < 30; i++) {
            short sid = classStart[i];
            BOOL isCustom = FALSE;
            for (int t = 0; t < NUM_TABS && !isCustom; t++)
                for (int s = 0; s < MAX_SLOTS && !isCustom; s++) {
                    int idx = g_tabSlots[t][s];
                    if (idx >= 0 && idx < g_numSkills && g_skills[idx].id == sid)
                        isCustom = TRUE;
                }
            if (!isCustom) origSkills[nOrig++] = sid;
        }

        /* Write: custom skills first, then remaining originals */
        int pos = 0;
        for (int t = 0; t < NUM_TABS; t++)
            for (int s = 0; s < MAX_SLOTS; s++) {
                int idx = g_tabSlots[t][s];
                if (idx < 0 || idx >= g_numSkills) continue;
                if (pos < stride)
                    classStart[pos++] = (short)g_skills[idx].id;
            }
        for (int i = 0; i < nOrig && pos < stride; i++)
            classStart[pos++] = origSkills[i];

        VirtualProtect(classStart, stride * sizeof(short), op, &op);

        /* Count must be at LEAST the original count */
        int newCount = pos > origCount ? pos : origCount;
        VirtualProtect(&counts[cls], sizeof(int), PAGE_READWRITE, &op);
        counts[cls] = newCount;
        VirtualProtect(&counts[cls], sizeof(int), op, &op);

        Log("WriteClassSkillList: cls=%d custom=%d orig=%d total=%d (origCount=%d)\n",
            cls, numCustom, nOrig, newCount, origCount);
    } __except(1) { Log("WriteClassSkillList CRASH\n"); }
}

/* ================================================================
 * APPLY SLOTS TO VANILLA TREE
 * ================================================================ */
static void ApplyAllSlots(void) {
    if (!Player()) return;

    /* Count total assigned skills — if none, don't touch ANYTHING */
    int totalAssigned = 0;
    for (int t = 0; t < NUM_TABS; t++)
        for (int s = 0; s < MAX_SLOTS; s++)
            if (g_tabSlots[t][s] >= 0) totalAssigned++;

    if (totalAssigned == 0) {
        Log("ApplyAllSlots: no skills assigned, leaving vanilla tree untouched\n");
        return;
    }

    /* Only clear skills we previously placed (not ALL 210) */
    for (int t = 0; t < NUM_TABS; t++)
        for (int s = 0; s < MAX_SLOTS; s++) {
            int idx = g_tabSlots[t][s];
            if (idx >= 0 && idx < g_numSkills)
                ClearSkillFromTree(g_skills[idx].id);
        }

    /* Place assigned skills using class-specific tree positions */
    int cls = GetPlayerClass();
    if (cls < 0 || cls > 6) cls = 0;
    for (int t = 0; t < NUM_TABS; t++) {
        int page = t + 1;
        for (int s = 0; s < MAX_SLOTS; s++) {
            int idx = g_tabSlots[t][s];
            if (idx < 0 || idx >= g_numSkills) continue;
            int row = TREE_POS[cls][t][s][0];
            int col = TREE_POS[cls][t][s][1];
            PlaceSkillInTree(g_skills[idx].id, page, row, col);
        }
    }

    /* Set tier-based level requirements and prerequisites */
    for (int t = 0; t < NUM_TABS; t++) {
        for (int s = 0; s < MAX_SLOTS; s++) {
            int idx = g_tabSlots[t][s];
            if (idx < 0 || idx >= g_numSkills) continue;
            SetSkillTierReqs(g_skills[idx].id, cls, t, s);
        }
    }

    /* Write our skills into the class skill list — this is what the
     * tree renderer ACTUALLY reads (not AddSkill/player skill list) */
    WriteClassSkillList();

    /* Skill point restore is handled separately — NOT here.
     * AddSkill causes action bar pollution. We only do tree placement here. */
    Log("Applied all slots\n");
}

/* ================================================================
 * ASSIGNMENT HELPERS
 * ================================================================ */
/* Returns tab (0-2) if assigned, -1 if not. Sets *outSlot if non-NULL. */
static int FindAssignment(int skillIdx, int* outSlot) {
    for (int t = 0; t < NUM_TABS; t++)
        for (int s = 0; s < MAX_SLOTS; s++)
            if (g_tabSlots[t][s] == skillIdx) {
                if (outSlot) *outSlot = s;
                return t;
            }
    return -1;
}

static int FirstFreeSlot(int tab) {
    for (int s = 0; s < MAX_SLOTS; s++)
        if (g_tabSlots[tab][s] < 0) return s;
    return -1;
}

static int CountAssigned(int tab) {
    int n = 0;
    for (int s = 0; s < MAX_SLOTS; s++)
        if (g_tabSlots[tab][s] >= 0) n++;
    return n;
}

/* ================================================================
 * UI: EDITOR
 * ================================================================ */
#define ED_W  380
#define ED_H  420
#define ED_X  20
#define ED_Y  ((600 - ED_H) / 2)
#define INFO_W 360
#define INFO_X (ED_X + ED_W + 8)
#define INFO_Y ED_Y
#define TAB_H  24
#define ROW_H  15

static void RenderEditor(void) {
    if (!fnRect || !fnText || !fnFont) return;
    int mx = MouseX(), my = MouseY();

    /* Dark background */
    for (int i = 0; i < 5; i++)
        fnRect(ED_X, ED_Y, ED_X+ED_W, ED_Y+ED_H, 0, 5);

    /* Border highlight */
    fnRect(ED_X, ED_Y, ED_X+ED_W, ED_Y+1, 7, 5);
    fnRect(ED_X, ED_Y, ED_X+1, ED_Y+ED_H, 7, 5);
    fnRect(ED_X+ED_W-1, ED_Y, ED_X+ED_W, ED_Y+ED_H, 7, 5);
    fnRect(ED_X, ED_Y+ED_H-1, ED_X+ED_W, ED_Y+ED_H, 7, 5);

    /* ---- TABS ---- */
    int tabW = ED_W / 4;
    fnFont(1);

    const wchar_t* tabName[] = {L"Tab 1", L"Tab 2", L"Tab 3", L"Locked"};
    for (int t = 0; t < 4; t++) {
        int tx = ED_X + t * tabW;
        BOOL active = (g_activeTab == t);
        BOOL hover = InRect(mx, my, tx, ED_Y, tabW, TAB_H);

        if (active) {
            fnRect(tx+1, ED_Y+1, tx+tabW-1, ED_Y+TAB_H, 7, 5);
            fnRect(tx+1, ED_Y+1, tx+tabW-1, ED_Y+TAB_H, 7, 5);
        }

        int color;
        if (active) color = (t < 3) ? 2 : 3;
        else if (hover) color = 0;
        else color = 5;
        fnText(tabName[t], tx+10, ED_Y+TAB_H-6, color, 0);

        if (hover && g_click) { g_activeTab = t; g_scroll = 0; }
    }

    /* Separator */
    fnRect(ED_X, ED_Y+TAB_H, ED_X+ED_W, ED_Y+TAB_H+2, 7, 5);

    /* Close [X] */
    fnFont(0);
    BOOL closeH = InRect(mx, my, ED_X+ED_W-24, ED_Y+2, 22, 20);
    fnText(L"X", ED_X+ED_W-16, ED_Y+16, closeH ? 1 : 5, 0);
    if (closeH && g_click) g_editorOpen = FALSE;

    /* ---- CONTENT ---- */
    int cy = ED_Y + TAB_H + 16;
    int bottom = ED_Y + ED_H - 68;
    fnFont(0);

    if (g_activeTab < 3) {
        /* ===== GREEN TAB ===== */
        int tab = g_activeTab;
        int assigned = CountAssigned(tab);

        int pool = ReadStat(STAT_NEWSKILLS);
        wchar_t hdr[80];
        swprintf(hdr, 80, L"Tab %d  (%d/10)", tab+1, assigned);
        fnText(hdr, ED_X+8, cy, 9, 0);
        cy += ROW_H + 2;

        /* Show 10 slots with dynamic tier labels based on class.
         * Tier colors: T1=green(2), T2=blue(3), T3=orange(8) */
        int cls = GetPlayerClass();
        if (cls < 0 || cls > 6) cls = 0;
        int lastTierNum = 0;
        for (int s = 0; s < MAX_SLOTS; s++) {
            int slotTier = SlotRequiredTier(cls, tab, s);
            if (slotTier != lastTierNum) {
                wchar_t tl[32];
                int tierColor = (slotTier == 1) ? 2 : (slotTier == 2) ? 3 : 8;
                swprintf(tl, 32, L"-- T%d --", slotTier);
                fnText(tl, ED_X+8, cy, tierColor, 0);
                cy += ROW_H;
                lastTierNum = slotTier;
            }

            int idx = g_tabSlots[tab][s];
            wchar_t row[80];
            int slotColor = (slotTier == 1) ? 2 : (slotTier == 2) ? 3 : 8;

            if (idx >= 0 && idx < g_numSkills) {
                swprintf(row, 80, L" %d: %hs", s+1, g_skills[idx].name);
                BOOL nameH = InRect(mx, my, ED_X+8, cy-11, ED_W-60, ROW_H);
                fnText(row, ED_X+12, cy, nameH ? 0 : slotColor, 0);

                /* [X] remove */
                BOOL xH = InRect(mx, my, ED_X+ED_W-40, cy-11, 30, ROW_H);
                fnText(L"[X]", ED_X+ED_W-38, cy, xH ? 1 : 5, 0);
                if (xH && g_click) {
                    g_tabSlots[tab][s] = -1;
                    SaveSlots();
                    ApplyAllSlots();
                }

                if (nameH && g_click && !xH) g_infoIdx = idx;
            } else {
                swprintf(row, 80, L" %d: (empty)", s+1);
                fnText(row, ED_X+12, cy, 5, 0);
            }
            cy += ROW_H;
        }

        /* Divider */
        cy += 4;
        fnRect(ED_X+8, cy, ED_X+ED_W-8, cy+1, 7, 5);
        cy += 6;

        /* Available skills */
        fnText(L"Available Skills:", ED_X+8, cy, 9, 0);
        cy += ROW_H;
        int availStartY = cy; /* remember where list starts for scrollbar */

        /* Render available skills sorted by tier: T1 first, T2, T3 */
        int shown = 0;
        for (int tier = 1; tier <= 3; tier++) {
            for (int i = 0; i < g_numSkills && cy < bottom; i++) {
                if (!g_skills[i].unlocked) continue;
                if (g_skills[i].tier != tier) continue;
                if (FindAssignment(i, NULL) >= 0) continue;

                if (shown < g_scroll) { shown++; continue; }

                wchar_t row[80];
                int skillCol = (tier == 1) ? 2 : (tier == 2) ? 3 : 8;
                swprintf(row, 80, L"  [T%d] %hs (%hs)", tier, g_skills[i].name, g_skills[i].cls);
                BOOL hover = InRect(mx, my, ED_X+8, cy-11, ED_W-24, ROW_H);
                fnText(row, ED_X+12, cy, hover ? 0 : skillCol, 0);

                if (hover && g_click) {
                    int pc = GetPlayerClass();
                    if (pc < 0 && g_savedClass > 0) pc = g_savedClass;
                    int free = -1;
                    for (int fs = 0; fs < MAX_SLOTS; fs++) {
                        if (g_tabSlots[tab][fs] < 0) {
                            int slotTier = SlotRequiredTier(pc, tab, fs);
                            if (slotTier == tier) { free = fs; break; }
                        }
                    }
                    if (free >= 0) {
                        g_tabSlots[tab][free] = i;
                        SaveSlots();
                        ApplyAllSlots();
                    }
                    g_infoIdx = i;
                }
                cy += ROW_H; shown++;
            }
        }

        /* Scrollbar for available skills */
        {
            int totalAvail = 0;
            for (int i = 0; i < g_numSkills; i++) {
                if (!g_skills[i].unlocked) continue;
                if (FindAssignment(i, NULL) >= 0) continue;
                totalAvail++;
            }
            int visibleRows = (bottom - availStartY) / ROW_H;
            if (visibleRows < 1) visibleRows = 1;
            int maxScr = totalAvail - visibleRows;
            if (maxScr < 0) maxScr = 0;
            if (g_scroll > maxScr) g_scroll = maxScr;

            if (maxScr > 0) {
                int sbX = ED_X + ED_W - 12;
                int sbTop = availStartY;
                int sbH = bottom - sbTop;
                fnRect(sbX, sbTop, sbX + 8, sbTop + sbH, 0, 5);
                int thumbH = (sbH * visibleRows) / totalAvail;
                if (thumbH < 16) thumbH = 16;
                int thumbY = sbTop + (g_scroll * (sbH - thumbH)) / maxScr;
                fnRect(sbX, thumbY, sbX + 8, thumbY + thumbH, 7, 5);

                if (InRect(mx, my, sbX - 4, sbTop, 16, sbH) && g_wasLDown) {
                    int relY = my - sbTop - thumbH / 2;
                    g_scroll = (relY * maxScr) / (sbH - thumbH);
                    if (g_scroll < 0) g_scroll = 0;
                    if (g_scroll > maxScr) g_scroll = maxScr;
                }
            }
        }

    } else {
        /* ===== BLUE TAB: LOCKED — sorted by tier ===== */
        int locked = 0;
        for (int i = 0; i < g_numSkills; i++)
            if (!g_skills[i].unlocked) locked++;

        wchar_t hdr[64];
        swprintf(hdr, 64, L"Locked Skills: %d", locked);
        fnText(hdr, ED_X+8, cy, 3, 0);
        cy += ROW_H + 2;
        int listStartY = cy;

        /* Render sorted by tier: T1 first, then T2, then T3 */
        int shown = 0;
        for (int tier = 1; tier <= 3 && cy < bottom; tier++) {
            for (int i = 0; i < g_numSkills && cy < bottom; i++) {
                if (g_skills[i].unlocked) continue;
                if (g_skills[i].tier != tier) continue;
                if (shown < g_scroll) { shown++; continue; }

                wchar_t row[80];
                int lockCol = (tier == 1) ? 2 : (tier == 2) ? 3 : 8;
                swprintf(row, 80, L"  [T%d] %hs (%hs)", tier, g_skills[i].name, g_skills[i].cls);
                BOOL hover = InRect(mx, my, ED_X+8, cy-11, ED_W-24, ROW_H);
                fnText(row, ED_X+12, cy, hover ? 0 : lockCol, 0);

                if (hover && g_click) g_infoIdx = i;
                cy += ROW_H; shown++;
            }
        }

        /* Scrollbar for locked skills */
        {
            int visibleRows = (bottom - listStartY) / ROW_H;
            if (visibleRows < 1) visibleRows = 1;
            int maxScr = locked - visibleRows;
            if (maxScr < 0) maxScr = 0;
            if (g_scroll > maxScr) g_scroll = maxScr;

            if (maxScr > 0) {
                int sbX = ED_X + ED_W - 12;
                int sbTop = listStartY;
                int sbH = bottom - sbTop;
                fnRect(sbX, sbTop, sbX + 8, sbTop + sbH, 0, 5);
                int thumbH = (sbH * visibleRows) / locked;
                if (thumbH < 16) thumbH = 16;
                int thumbY = sbTop + (g_scroll * (sbH - thumbH)) / maxScr;
                fnRect(sbX, thumbY, sbX + 8, thumbY + thumbH, 7, 5);

                if (InRect(mx, my, sbX - 4, sbTop, 16, sbH) && g_wasLDown) {
                    int relY = my - sbTop - thumbH / 2;
                    g_scroll = (relY * maxScr) / (sbH - thumbH);
                    if (g_scroll < 0) g_scroll = 0;
                    if (g_scroll > maxScr) g_scroll = maxScr;
                }
            }
        }
    }

    /* ---- INFO PANEL (right side) ---- */
    if (g_infoIdx >= 0 && g_infoIdx < g_numSkills) {
        int ix = INFO_X, iy = INFO_Y;
        int iw = INFO_W, ih = 180;

        /* Background */
        for (int i = 0; i < 6; i++)
            fnRect(ix, iy, ix+iw, iy+ih, 0, 5);
        fnRect(ix, iy, ix+iw, iy+1, 7, 5);
        fnRect(ix, iy, ix+1, iy+ih, 7, 5);
        fnRect(ix+iw-1, iy, ix+iw, iy+ih, 7, 5);
        fnRect(ix, iy+ih-1, ix+iw, iy+ih, 7, 5);

        SkillInfo* sk = &g_skills[g_infoIdx];

        const char* fullCls = "Unknown";
        if (!strcmp(sk->cls,"ama")) fullCls = "Amazon";
        else if (!strcmp(sk->cls,"sor")) fullCls = "Sorceress";
        else if (!strcmp(sk->cls,"nec")) fullCls = "Necromancer";
        else if (!strcmp(sk->cls,"pal")) fullCls = "Paladin";
        else if (!strcmp(sk->cls,"bar")) fullCls = "Barbarian";
        else if (!strcmp(sk->cls,"dru")) fullCls = "Druid";
        else if (!strcmp(sk->cls,"ass")) fullCls = "Assassin";

        int ty = iy + 8;
        fnFont(1);
        wchar_t info1[128];
        swprintf(info1, 128, L"%hs", sk->name);
        fnText(info1, ix+8, ty+12, 0, 0); ty += 20;

        fnFont(0);
        wchar_t info2[128];
        swprintf(info2, 128, L"Class: %hs", fullCls);
        fnText(info2, ix+8, ty+12, 7, 0); ty += 16;

        wchar_t info3[128];
        swprintf(info3, 128, L"Tier: %d  |  ID: %d", sk->tier, sk->id);
        fnText(info3, ix+8, ty+12, 9, 0); ty += 16;

        wchar_t info4[128];
        swprintf(info4, 128, L"Status: %hs", sk->unlocked ? "UNLOCKED" : "LOCKED");
        fnText(info4, ix+8, ty+12, sk->unlocked ? 2 : 1, 0); ty += 16;

        int aSlot;
        int aTab = FindAssignment(g_infoIdx, &aSlot);
        if (aTab >= 0) {
            wchar_t info5[128];
            swprintf(info5, 128, L"Assigned: Tab %d, Slot %d", aTab+1, aSlot+1);
            fnText(info5, ix+8, ty+12, 5, 0); ty += 16;
        } else {
            fnText(L"Not assigned", ix+8, ty+12, 5, 0); ty += 16;
        }

        /* Skill description */
        ty += 8;
        fnRect(ix+1, ty, ix+iw-1, ty+1, 7, 5); /* separator */
        ty += 8;
        const char* desc = GetSkillDesc(sk->id);
        if (desc[0]) {
            wchar_t wdesc[128];
            MultiByteToWideChar(CP_ACP, 0, desc, -1, wdesc, 128);
            fnText(wdesc, ix+8, ty+12, 0, 0);
        }
    }
}

/* ================================================================
 * UI: MENU
 * ================================================================ */
#define MNU_X  2
#define MNU_Y  2
#define MNU_W  52
#define MNU_H  16

static void RenderMenu(void) {
    if (!fnRect || !fnText) return;
    int mx = MouseX(), my = MouseY();

    /* Menu button */
    fnRect(MNU_X, MNU_Y, MNU_X+MNU_W, MNU_Y+MNU_H, 0, 5);
    fnRect(MNU_X, MNU_Y, MNU_X+MNU_W, MNU_Y+MNU_H, 0, 5);
    fnFont(0);
    BOOL mH = InRect(mx, my, MNU_X, MNU_Y, MNU_W, MNU_H);
    fnText(L"Menu", MNU_X+6, MNU_Y+MNU_H-2, mH ? 7 : 5, 0);
    if (mH && g_click) g_menuOpen = !g_menuOpen;

    if (!g_menuOpen) return;

    /* Dropdown */
    int dy = MNU_Y + MNU_H + 2;
    int dw = 160;
    int ih = 18;
    int dh = 4 * ih + 4;

    for (int i = 0; i < 4; i++)
        fnRect(MNU_X, dy, MNU_X+dw, dy+dh, 0, 5);

    /* Skill Editor */
    BOOL h1 = InRect(mx, my, MNU_X, dy+2, dw, ih);
    fnText(L"Skill Editor (F1)", MNU_X+8, dy+ih-4, h1 ? 2 : 0, 0);
    if (h1 && g_click) { g_editorOpen = !g_editorOpen; g_menuOpen = FALSE; }
    dy += ih;

    /* Quest Log */
    BOOL hQL = InRect(mx, my, MNU_X, dy, dw, ih);
    fnText(L"Quest Log (F2)", MNU_X+8, dy+ih-4, hQL ? 9 : 0, 0);
    if (hQL && g_click) { g_questLogOpen = !g_questLogOpen; g_menuOpen = FALSE; }
    dy += ih;

    /* Quest Tracker toggle */
    BOOL h2 = InRect(mx, my, MNU_X, dy, dw, ih);
    wchar_t qt[64];
    swprintf(qt, 64, L"Tracker (F3) %hs", g_questTracker ? "[ON]" : "[OFF]");
    fnText(qt, MNU_X+8, dy+ih-4, h2 ? 2 : 0, 0);
    if (h2 && g_click) { g_questTracker = !g_questTracker; g_menuOpen = FALSE; }
    dy += ih;

    /* Reset */
    BOOL h3 = InRect(mx, my, MNU_X, dy, dw, ih);
    /* Reset button removed — skills reset automatically via d2skillreset.exe */
}

/* ================================================================
 * UI: QUEST TRACKER (placeholder)
 * ================================================================ */
/* ================================================================
 * UI: QUEST TRACKER (small overlay, right side, no background)
 * ================================================================ */
static void RenderQuestTracker(void) {
    if (!g_questTracker || !fnText || !fnFont) return;

    int qx = 600;
    int ty = 100;
    int curArea = GetCurrentArea();

    /* Determine which act we're in based on area ID */
    int curAct = -1;
    if (curArea >= 1 && curArea <= 39) curAct = 0;
    else if (curArea >= 40 && curArea <= 74) curAct = 1;
    else if (curArea >= 75 && curArea <= 102) curAct = 2;
    else if (curArea >= 103 && curArea <= 108) curAct = 3;
    else if (curArea >= 109 && curArea <= 132) curAct = 4;

    if (curAct < 0) return;

    /* Act header with completion count for this act only */
    int actDone = 0, actTotal = 0;
    for (int q = 0; q < g_acts[curAct].num; q++) {
        if (g_acts[curAct].quests[q].id > 0) {
            actTotal++;
            if (g_acts[curAct].quests[q].completed) actDone++;
        }
    }

    fnFont(1);
    wchar_t actName[16];
    MultiByteToWideChar(CP_ACP, 0, g_acts[curAct].name, -1, actName, 16);
    wchar_t header[64];
    swprintf(header, 64, L"%s: %d / %d", actName, actDone, actTotal);
    fnText(header, qx, ty + 12, 9, 0); ty += 20;
    fnFont(0);

    /* In town: just show the act progress, no specific quest */
    if (IsTown(curArea)) return;

    /* Show kill quest for current area */
    for (int q = 0; q < g_acts[curAct].num; q++) {
        Quest *quest = &g_acts[curAct].quests[q];
        if (quest->type == QTYPE_KILL && quest->param == curArea) {
            wchar_t wname[80];
            MultiByteToWideChar(CP_ACP, 0, quest->name, -1, wname, 64);
            if (quest->completed) {
                wchar_t line[96];
                swprintf(line, 96, L"%s [DONE]", wname);
                fnText(line, qx, ty + 12, 2, 0);
            } else {
                wchar_t line[96];
                swprintf(line, 96, L"%s (%d/%d)", wname, quest->killCount, quest->killReq);
                fnText(line, qx, ty + 12, 0, 0);
            }
            ty += 16;
            break;
        }
    }

    /* Show area-reach or boss quest if current area matches */
    for (int q = 0; q < g_acts[curAct].num; q++) {
        Quest *quest = &g_acts[curAct].quests[q];
        if (quest->id == 0 || quest->completed) continue;
        if (quest->type == QTYPE_AREA && quest->param == curArea) {
            wchar_t wname[64];
            MultiByteToWideChar(CP_ACP, 0, quest->name, -1, wname, 64);
            fnText(wname, qx, ty + 12, 10, 0);
            ty += 16;
        }
    }

    /* Show next incomplete story quest for this act */
    for (int q = 0; q < g_acts[curAct].num; q++) {
        Quest *quest = &g_acts[curAct].quests[q];
        if (quest->id == 0 || quest->completed) continue;
        if (quest->type == QTYPE_KILL) continue;
        wchar_t wname[64];
        MultiByteToWideChar(CP_ACP, 0, quest->name, -1, wname, 64);
        wchar_t line[80];
        swprintf(line, 80, L"Goal: %s", wname);
        fnText(line, qx, ty + 12, 9, 0);
        ty += 16;
        break; /* only show next one */
    }
}

/* ================================================================
 * UI: QUEST LOG (full panel, centered, like Skill Editor)
 * ================================================================ */
#define QL_W  480
#define QL_H  420
#define QL_X  ((800 - QL_W) / 2)
#define QL_Y  ((600 - QL_H) / 2)
#define QL_TAB_H 24

static void RenderQuestLog(void) {
    if (!g_questLogOpen || !fnRect || !fnText || !fnFont) return;
    int mx = MouseX(), my = MouseY();

    /* Background */
    for (int i = 0; i < 6; i++)
        fnRect(QL_X, QL_Y, QL_X+QL_W, QL_Y+QL_H, 0, 5);
    fnRect(QL_X, QL_Y, QL_X+QL_W, QL_Y+1, 7, 5);
    fnRect(QL_X, QL_Y, QL_X+1, QL_Y+QL_H, 7, 5);
    fnRect(QL_X+QL_W-1, QL_Y, QL_X+QL_W, QL_Y+QL_H, 7, 5);
    fnRect(QL_X, QL_Y+QL_H-1, QL_X+QL_W, QL_Y+QL_H, 7, 5);

    /* Act tabs */
    fnFont(0);
    int tabW = QL_W / 5;
    for (int a = 0; a < 5; a++) {
        int tx = QL_X + a * tabW;
        BOOL hover = InRect(mx, my, tx, QL_Y, tabW, QL_TAB_H);
        if (a == g_questLogAct) {
            fnRect(tx+1, QL_Y+1, tx+tabW-1, QL_Y+QL_TAB_H, 7, 5);
            fnRect(tx+1, QL_Y+1, tx+tabW-1, QL_Y+QL_TAB_H, 7, 5);
        }
        if (hover && g_click && a != g_questLogAct) {
            g_questLogAct = a;
            g_questScroll = 0;
        }
        wchar_t tabName[16];
        MultiByteToWideChar(CP_ACP, 0, g_acts[a].name, -1, tabName, 16);
        int color = (a == g_questLogAct) ? 9 : (hover ? 0 : 5);
        fnText(tabName, tx + 14, QL_Y + QL_TAB_H - 6, color, 0);
    }
    fnRect(QL_X, QL_Y+QL_TAB_H, QL_X+QL_W, QL_Y+QL_TAB_H+2, 7, 5);

    /* Close button */
    BOOL closeH = InRect(mx, my, QL_X+QL_W-24, QL_Y+2, 22, 20);
    fnText(L"X", QL_X+QL_W-16, QL_Y+16, closeH ? 1 : 5, 0);
    if (closeH && g_click) { g_questLogOpen = FALSE; return; }

    /* Title */
    fnFont(1);
    wchar_t title[64];
    swprintf(title, 64, L"Quest Log - %d / %d Complete", CompletedQuests(), TotalQuests());
    fnText(title, QL_X + 8, QL_Y + QL_TAB_H + 22, 9, 0);
    fnFont(0);

    /* Quest list for selected act — with scroll */
    ActData *act = &g_acts[g_questLogAct];
    int contentTop = QL_Y + QL_TAB_H + 38;
    int contentBottom = QL_Y + QL_H - 10;
    int contentH = contentBottom - contentTop;
    int itemH = 44; /* height per quest entry */
    int totalH = act->num * itemH;
    int maxScroll = totalH - contentH;
    if (maxScroll < 0) maxScroll = 0;
    if (g_questScroll > maxScroll) g_questScroll = maxScroll;
    if (g_questScroll < 0) g_questScroll = 0;

    /* Scrollbar track (right side) */
    int sbX = QL_X + QL_W - 12;
    int sbTrackTop = contentTop;
    int sbTrackH = contentH;
    fnRect(sbX, sbTrackTop, sbX + 8, sbTrackTop + sbTrackH, 0, 5);

    /* Scrollbar thumb */
    if (totalH > contentH) {
        int thumbH = (contentH * contentH) / totalH;
        if (thumbH < 20) thumbH = 20;
        int thumbY = sbTrackTop + (g_questScroll * (sbTrackH - thumbH)) / maxScroll;
        fnRect(sbX, thumbY, sbX + 8, thumbY + thumbH, 7, 5);

        /* Drag scrollbar with mouse */
        if (InRect(mx, my, sbX - 4, sbTrackTop, 16, sbTrackH) && g_wasLDown) {
            int relY = my - sbTrackTop - thumbH / 2;
            g_questScroll = (relY * maxScroll) / (sbTrackH - thumbH);
            if (g_questScroll < 0) g_questScroll = 0;
            if (g_questScroll > maxScroll) g_questScroll = maxScroll;
        }
    }

    /* Render visible quests */
    int cy = contentTop;
    for (int q = 0; q < act->num; q++) {
        int itemY = contentTop + q * itemH - g_questScroll;
        /* Skip if above or below visible area */
        if (itemY + itemH < contentTop || itemY > contentBottom) continue;

        Quest *quest = &act->quests[q];
        if (quest->id == 0) {
            wchar_t wdesc[128];
            MultiByteToWideChar(CP_ACP, 0, quest->desc, -1, wdesc, 128);
            fnText(wdesc, QL_X + 12, itemY + 12, 5, 0);
            continue;
        }

        /* Quest name with status indicator */
        wchar_t wname[96];
        wchar_t nameA[64];
        MultiByteToWideChar(CP_ACP, 0, quest->name, -1, nameA, 64);
        if (quest->completed) {
            swprintf(wname, 96, L"[DONE] %s", nameA);
        } else if (quest->type == QTYPE_KILL) {
            swprintf(wname, 96, L"[%d/%d] %s", quest->killCount, quest->killReq, nameA);
        } else {
            swprintf(wname, 96, L"[    ] %s", nameA);
        }

        BOOL hover = InRect(mx, my, QL_X+8, itemY, QL_W-24, 30);
        int nameColor = quest->completed ? 2 : (hover ? 0 : 7);
        fnFont(1);
        fnText(wname, QL_X + 12, itemY + 14, nameColor, 0);

        /* Description + reward info */
        fnFont(0);
        wchar_t wdesc[160];
        MultiByteToWideChar(CP_ACP, 0, quest->desc, -1, wdesc, 128);
        fnText(wdesc, QL_X + 20, itemY + 28, 5, 0);

        /* Separator line */
        int sepY = itemY + 38;
        if (sepY > contentTop && sepY < contentBottom)
            fnRect(QL_X+8, sepY, QL_X+QL_W-16, sepY+1, 7, 5);
    }
}

/* ================================================================
 * UI: RESET DIALOG
 * ================================================================ */
static void RenderResetDialog(void) {
    if (!g_resetConfirm || !fnRect || !fnText) return;
    int mx = MouseX(), my = MouseY();

    int dx = 250, dy = 220, dw = 300, dh = 120;
    for (int i = 0; i < 6; i++)
        fnRect(dx, dy, dx+dw, dy+dh, 0, 5);
    fnRect(dx, dy, dx+dw, dy+1, 7, 5);
    fnRect(dx, dy, dx+1, dy+dh, 7, 5);
    fnRect(dx+dw-1, dy, dx+dw, dy+dh, 7, 5);
    fnRect(dx, dy+dh-1, dx+dw, dy+dh, 7, 5);

    fnFont(1);
    fnText(L"Reset all skills & stats?", dx+30, dy+30, 0, 0);
    fnFont(0);
    fnText(L"Cost: 10,000 gold", dx+30, dy+50, 9, 0);

    int gold = ReadStat(STAT_GOLD) + ReadStat(STAT_GOLDBANK);
    if (gold < 10000) {
        wchar_t ng[64];
        swprintf(ng, 64, L"Not enough gold! (%d)", gold);
        fnText(ng, dx+30, dy+68, 1, 0);
    }

    /* YES */
    BOOL yH = InRect(mx, my, dx+50, dy+80, 60, 24);
    fnText(L"[YES]", dx+50, dy+98, yH ? 2 : 5, 0);

    /* NO */
    BOOL nH = InRect(mx, my, dx+190, dy+80, 60, 24);
    fnText(L"[NO]", dx+190, dy+98, nH ? 1 : 5, 0);

    if (nH && g_click) { g_resetConfirm = FALSE; return; }

    if (yH && g_click && gold >= 10000) {
        DWORD p = Player();
        if (!p || !fnSetStat) { g_resetConfirm = FALSE; return; }

        /* Deduct gold */
        int inv = ReadStat(STAT_GOLD);
        if (inv >= 10000) {
            fnSetStat(p, STAT_GOLD, inv - 10000, 0);
        } else {
            fnSetStat(p, STAT_GOLD, 0, 0);
            int bank = ReadStat(STAT_GOLDBANK);
            fnSetStat(p, STAT_GOLDBANK, bank - (10000 - inv), 0);
        }

        /* Reset stats to base */
        int cls = GetPlayerClass();
        if (cls >= 0 && cls < 7) {
            int curStr = ReadStat(STAT_STRENGTH);
            int curDex = ReadStat(STAT_DEXTERITY);
            int curVit = ReadStat(STAT_VITALITY);
            int curEng = ReadStat(STAT_ENERGY);

            int invested = (curStr - CLASS_BASE[cls][0])
                         + (curDex - CLASS_BASE[cls][1])
                         + (curVit - CLASS_BASE[cls][2])
                         + (curEng - CLASS_BASE[cls][3]);
            if (invested < 0) invested = 0;

            fnSetStat(p, STAT_STRENGTH,  CLASS_BASE[cls][0], 0);
            fnSetStat(p, STAT_DEXTERITY, CLASS_BASE[cls][1], 0);
            fnSetStat(p, STAT_VITALITY,  CLASS_BASE[cls][2], 0);
            fnSetStat(p, STAT_ENERGY,    CLASS_BASE[cls][3], 0);

            int curNew = ReadStat(STAT_NEWSTATS);
            fnSetStat(p, STAT_NEWSTATS, curNew + invested, 0);
            Log("Reset stats: invested=%d newstats=%d\n", invested, curNew+invested);
        }

        /* Reset skill points: walk skill list, zero levels, count points */
        __try {
            DWORD pL = *(DWORD*)(p + 0xA8);
            if (pL) {
                int totalPts = 0;
                DWORD pS = *(DWORD*)(pL + 0x04);
                int n = 0;
                while (pS && n < 300) {
                    int lvl = *(int*)(pS + 0x28);
                    if (lvl > 0) totalPts += lvl;
                    DWORD op;
                    VirtualProtect((void*)(pS+0x28), 4, PAGE_READWRITE, &op);
                    *(int*)(pS+0x28) = 0;
                    VirtualProtect((void*)(pS+0x28), 4, op, &op);
                    pS = *(DWORD*)(pS + 0x04); n++;
                }
                int curSk = ReadStat(STAT_NEWSKILLS);
                fnSetStat(p, STAT_NEWSKILLS, curSk + totalPts, 0);
                Log("Reset skills: returned=%d newskills=%d\n",
                    totalPts, curSk + totalPts);
            }
        } __except(1) {}

        g_resetConfirm = FALSE;
    }
}

/* ================================================================
 * MASTER DRAW
 * ================================================================ */
static void DrawAll(void) {
    if (!Player()) return;

    /* ESC closes everything */
    static BOOL wasEsc = FALSE;
    BOOL escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escDown && !wasEsc) {
        if (g_resetConfirm) g_resetConfirm = FALSE;
        else if (g_questLogOpen) g_questLogOpen = FALSE;
        else if (g_editorOpen) g_editorOpen = FALSE;
        else if (g_menuOpen) g_menuOpen = FALSE;
    }
    wasEsc = escDown;

    /* F1/F2/F3 hotkeys */
    {
        static BOOL wasF1 = FALSE, wasF2 = FALSE, wasF3 = FALSE;
        BOOL f1 = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
        BOOL f2 = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
        BOOL f3 = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
        if (f1 && !wasF1) g_editorOpen = !g_editorOpen;
        if (f2 && !wasF2) g_questLogOpen = !g_questLogOpen;
        if (f3 && !wasF3) g_questTracker = !g_questTracker;
        wasF1 = f1; wasF2 = f2; wasF3 = f3;
    }

    UpdateClick();

    /* Keep g_savedClass updated while player exists */
    {
        int pc = GetPlayerClass();
        if (pc > 0) g_savedClass = pc;
    }

    /* FIRST-FRAME: Detect character, load per-char slots, apply tree */
    {
        static DWORD lastApplyPlayer = 0;
        DWORD p = Player();
        if (p && p != lastApplyPlayer) {
            lastApplyPlayer = p;
            /* Get character name for per-char save files */
            char oldName[17];
            strncpy(oldName, g_charName, 16); oldName[16] = 0;
            UpdateCharName();
            /* ALWAYS reload skills and slots when player is detected.
             * This ensures per-character data is used, never global. */
            Log("Player detected: '%s' (was '%s')\n", g_charName, oldName);
            LoadSkillDatabase();
            LoadSlots();
            LoadChecks();
            ApplyAllSlots();
            Log("FIRST-FRAME: char='%s' ApplyAllSlots done\n", g_charName);

        }
    }

    /* PERIODIC PATCHER: re-apply tree modifications.
     * First 30 sec: every 500ms. After: every 5 sec. */
    {
        static DWORD lastPatch = 0;
        static DWORD firstPatchTime = 0;
        DWORD now = GetTickCount();

        if (!firstPatchTime && Player()) firstPatchTime = now;
        DWORD interval = (firstPatchTime && (now - firstPatchTime) < 30000) ? 500 : 5000;

        if (now - lastPatch > interval) {
            lastPatch = now;
            if (Player()) {
                ApplyAllSlots();
                RunCheckDetection();
            }
        }

        /* Auto-save progress every 30 seconds */
        {
            static DWORD lastSave = 0;
            if (now - lastSave > 30000) {
                lastSave = now;
                if (Player() && g_charName[0]) {
                    SaveStateFile();
                }
            }
        }
    }

    /* Notification overlay */
    if (g_notifyText[0] && fnText && fnFont) {
        DWORD elapsed = GetTickCount() - g_notifyTime;
        if (elapsed < NOTIFY_DURATION) {
            fnFont(1);
            fnText(g_notifyText, 250, 100, 2, 0);
        } else {
            g_notifyText[0] = 0;
        }
    }

    /* Render layers */
    RenderMenu();
    if (g_questTracker && !g_editorOpen && !g_questLogOpen) RenderQuestTracker();
    if (g_questLogOpen) RenderQuestLog();
    if (g_editorOpen) RenderEditor();
    if (g_resetConfirm) RenderResetDialog();
}

static void SafeDraw(void) { __try { DrawAll(); } __except(1) {} }

/* ================================================================
 * WNDPROC HOOK — Block mouse clicks when editor/dialog is open
 * ================================================================ */
static WNDPROC g_origWndProc = NULL;
static HWND g_gameWnd = NULL;

static LRESULT CALLBACK GameWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    BOOL blocking = (g_editorOpen || g_resetConfirm || g_questLogOpen);

    /* Transition: just opened editor → force-release any held buttons */
    static BOOL wasBlocking = FALSE;
    if (blocking && !wasBlocking) {
        CallWindowProcA(g_origWndProc, hWnd, WM_LBUTTONUP, 0, 0);
        CallWindowProcA(g_origWndProc, hWnd, WM_RBUTTONUP, 0, 0);
    }
    wasBlocking = blocking;

    /* Capture mouse wheel for scrolling */
    if (msg == WM_MOUSEWHEEL) {
        short delta = (short)HIWORD(wParam);
        if (g_questLogOpen) {
            if (delta > 0) g_questScroll -= 3;
            if (delta < 0) g_questScroll += 3;
            if (g_questScroll < 0) g_questScroll = 0;
            return 0;
        }
        if (g_editorOpen) {
            if (delta > 0 && g_scroll > 0) g_scroll--;
            if (delta < 0) g_scroll++;
            return 0;
        }
    }

    if (blocking) {
        switch (msg) {
            /* Block button-DOWN → prevents new movement/attacks */
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONDBLCLK:
            case WM_MOUSEWHEEL: /* block scroll from game too */
                return 0;
            /* Let button-UP through → game releases current movement */
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
                return CallWindowProcA(g_origWndProc, hWnd, msg, wParam, lParam);
        }
    }
    return CallWindowProcA(g_origWndProc, hWnd, msg, wParam, lParam);
}

static void InstallWndProcHook(void) {
    /* Try common D2 window titles */
    g_gameWnd = FindWindowA("Diablo II", NULL);
    if (!g_gameWnd) g_gameWnd = FindWindowA(NULL, "Diablo II");
    if (g_gameWnd) {
        g_origWndProc = (WNDPROC)SetWindowLongA(g_gameWnd, GWL_WNDPROC, (LONG)GameWndProc);
        Log("WndProc hooked: wnd=%08X orig=%08X\n", (DWORD)g_gameWnd, (DWORD)g_origWndProc);
    } else {
        Log("WARNING: Could not find game window for input blocking\n");
    }
}

/* ================================================================
 * HOOK
 * ================================================================ */
static BYTE* g_tramp = NULL;
static DWORD g_hookAddr = 0;

static void PatchJMP(DWORD a, DWORD d) {
    DWORD op;
    VirtualProtect((void*)a, 5, PAGE_EXECUTE_READWRITE, &op);
    *(BYTE*)a = 0xE9;
    *(DWORD*)(a+1) = d - a - 5;
    VirtualProtect((void*)a, 5, op, &op);
}

static void __declspec(naked) UIHook(void) {
    __asm {
        call g_tramp
        pushad
        pushfd
        call SafeDraw
        popfd
        popad
        ret
    }
}

/* ================================================================
 * MAIN THREAD
 * ================================================================ */
static volatile BOOL g_run = TRUE;

static DWORD WINAPI MainThread(LPVOID lp) {
    (void)lp;
    Log("=== D2Arch v3.0 started ===\n");

    while (g_run && !GetModuleHandleA("D2Client.dll")) Sleep(200);
    if (!g_run) return 0;
    if (!InitAPI()) { Log("InitAPI failed\n"); return 0; }

    /* Do NOT load skills/slots here — g_charName is empty.
     * FIRST-FRAME block in DrawAll handles it after character is detected. */
    LoadIconMap();

    /* NO early patching. Let the game load the character normally.
     * All modifications happen AFTER player exists (in ApplyAllSlots
     * via the g_applied flag in DrawAll). This avoids corrupting saves. */

    /* Install hook at D2Client DrawGameUI */
    g_hookAddr = (DWORD)hCli + 0x5E650;
    g_tramp = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_tramp) { Log("VirtualAlloc fail\n"); return 0; }
    memcpy(g_tramp, (void*)g_hookAddr, 8);
    g_tramp[8] = 0xE9;
    *(DWORD*)(g_tramp+9) = (g_hookAddr+8) - ((DWORD)g_tramp+13);
    PatchJMP(g_hookAddr, (DWORD)UIHook);
    { DWORD op; VirtualProtect((void*)(g_hookAddr+5), 3, PAGE_EXECUTE_READWRITE, &op);
      memset((void*)(g_hookAddr+5), 0x90, 3);
      VirtualProtect((void*)(g_hookAddr+5), 3, op, &op); }
    Log("Hook at %08X\n", g_hookAddr);

    /* Hook WndProc to block mouse clicks when editor is open */
    InstallWndProcHook();

    while (g_run && !Player()) Sleep(200);
    Log("Player=%08X Lv=%d\n", Player(), ReadStat(STAT_LEVEL));

    while (g_run) Sleep(500);
    return 0;
}

/* ================================================================
 * DLL ENTRY
 * ================================================================ */
static HANDLE g_th = NULL;

extern "C" BOOL APIENTRY DllMain(HMODULE hMod, DWORD dwR, LPVOID lpR) {
    (void)lpR;
    if (dwR == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        Log("=== D2Archipelago v3.0 loaded ===\n");
        g_th = CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
    } else if (dwR == DLL_PROCESS_DETACH) {
        g_run = FALSE;
        SaveSlots();
        SaveStateFile();
        if (g_th) { WaitForSingleObject(g_th, 3000); CloseHandle(g_th); }
        Log("=== Unloaded ===\n");
        if (g_log) fclose(g_log);
    }
    return TRUE;
}
