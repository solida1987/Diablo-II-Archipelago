/* ================================================================
 * D2 FUNCTION TYPEDEFS
 * ================================================================ */

/* D2Win drawing */
typedef void  (__fastcall *DrawText_t)(const wchar_t* text, int x, int y, int color, int centered);
typedef void  (__fastcall *SetFont_t)(int fontIdx);

/* D2Gfx drawing */
typedef void  (__stdcall *DrawRect_t)(int left, int top, int right, int bottom, int color, int drawMode);
typedef void  (__stdcall *DrawLine_t)(int x1, int y1, int x2, int y2, int color, int unk);

/* D2Win cell file loading + drawing (high-level, handles CelContext internally) */
typedef void* (__fastcall *WinLoadCellFile_t)(const char* path, int type);
typedef void  (__fastcall *WinDrawCellFile_t)(void* cellFile, int x, int y, int div, int trans, int color);

/* D2Common stats */
typedef int   (__stdcall *GetUnitStat_t)(void* pUnit, int statId, int layer);
typedef void  (__stdcall *SetUnitStat_t)(void* pUnit, int statId, int value, int layer);
typedef void  (__stdcall *AddUnitStat_t)(void* pUnit, int statId, int value, int layer);

/* D2Net CLIENT_Send — sends client-to-server packet (ordinal 10005) */
typedef int   (__stdcall *ClientSend_t)(int unused, const unsigned char* buf, int size);

/* D2Game gold function — works in single-player (same process) */
typedef void  (__fastcall *AddGold_t)(void* pUnit, int statId, int value);

/* Stat IDs */
#define STAT_STATPTS    4
#define STAT_NEWSKILLS  5
#define STAT_GOLD      14
#define STAT_GOLDBANK  15

/* D2Win control struct macros (used by title screen code) */
#define CTRL_TYPE(p)   (*(int*)((BYTE*)(p) + 0x00))
#define CTRL_X(p)      (*(int*)((BYTE*)(p) + 0x0C))
#define CTRL_Y(p)      (*(int*)((BYTE*)(p) + 0x10))
#define CTRL_W(p)      (*(int*)((BYTE*)(p) + 0x14))
#define CTRL_H(p)      (*(int*)((BYTE*)(p) + 0x18))
#define CTRL_NEXT(p)   (*(void**)((BYTE*)(p) + 0x3C))
#define D2WIN_BUTTON 6
static void** g_ppControlList = NULL;

/* Title screen settings - forward declarations */
static int ts_Goal, ts_XPMultiplier_idx;
static int ts_MonsterShuffle, ts_BossShuffle, ts_ShopShuffle; /* 1.8.0: ts_TreasureCows extracted */
static int ts_EntranceShuffle; /* 1.9.0 NEW: System 1 — dead-end cave entrance shuffle */
static int ts_TrapsEnabled = 1; /* 1.8.4: toggle to disable trap fillers entirely */
/* 1.9.0 — Bonus check toggles (default OFF — opt-in). Saved/loaded
 * via d2arch.ini for standalone-only persistence; AP slot_data
 * overrides at character creation (handled by Bonus_ApplyToggles
 * called from d2arch_ap.c LoadAPSettings). */
static int ts_CheckShrines        = 0;
static int ts_CheckUrns           = 0;
static int ts_CheckBarrels        = 0;
static int ts_CheckChests         = 0;
static int ts_CheckSetPickups     = 0;
static int ts_CheckGoldMilestones = 0;
/* 1.8.0: game mode dropdown removed — replaced by two independent ON/OFF toggles */
static int ts_SkillHuntingOn, ts_ZoneLockingOn;

/* Cycle button options */
static const wchar_t* goalOptions[] = {
    L"Act1 Norm", L"Act2 Norm", L"Act3 Norm", L"Act4 Norm", L"Full Norm",
    L"Act1 NM", L"Act2 NM", L"Act3 NM", L"Act4 NM", L"Full NM",
    L"Act1 Hell", L"Act2 Hell", L"Act3 Hell", L"Act4 Hell", L"Full Hell",
    L"Collection"  /* 1.9.0 NEW — index 15 maps to internal g_apGoal=3 */
};
#define GOAL_COUNT 16

/* 1.9.0 — Collection sub-target toggles. State mirrored to d2arch.ini under
 * [settings] keys CollGoalSets / CollGoalRunes / CollGoalGems / CollGoalSpecials.
 * Default ON (strictest = collect everything to win). Read by the DLL at
 * character load and applied as overrides to g_collGoal. */
static int ts_CollGoalSets     = 1;
static int ts_CollGoalRunes    = 1;
static int ts_CollGoalGems     = 1;
static int ts_CollGoalSpecials = 1;
static const wchar_t* xpOptions[] = { L"1x", L"2x", L"3x", L"4x", L"5x", L"6x", L"7x", L"8x", L"9x", L"10x" };
#define XP_COUNT 10

/* Cycle button handles */
static void* g_btnGoal = NULL;
static void* g_btnXP = NULL;
static int ts_QuestStory, ts_QuestHunting, ts_QuestKillZones, ts_QuestExploration;
static int ts_QuestWaypoints, ts_QuestLevelMilestones, ts_XPMultiplier;
static int ts_ClassFilter, ts_ClsEnabled[7], ts_IPlayAssassin;
static char ts_iniPath[MAX_PATH];
static void TitleSettings_Load(void);
static void TitleSettings_Save(void);
static void TitleSettings_CreateButtons(void);

/* AP connection strings — declared early for title screen callback access */
static char  g_apIP[64]       = "localhost:38281";
static char  g_apSlot[32]     = "";
static char  g_apPassword[32] = "";

/* AP page 2 focused field (-1=none, 0=server, 1=slot, 2=pass) */
static int g_apPageFocus = -1;

/* AP editbox control pointers (set during title screen creation) */
static void* g_apEdServer = NULL;
static void* g_apEdSlot = NULL;
static void* g_apEdPass = NULL;

/* Forward declarations for AP bridge (used in title screen callbacks) */
static void WriteAPCommand(const char* action);
static void StartAPBridge(void);
static void SaveAPConfig(void);

/* D2Win BUTTON_Create (ordinal 10107) - creates persistent buttons */
typedef void* (__fastcall *ButtonCreate_t)(
    int nX, int nY, int nWidth, int nHeight,
    void* pCellFile,
    int (__stdcall* pCallback)(void*),
    int nVKey, int nBaseFrame, unsigned int nFlags,
    unsigned short nStringId,
    int (__stdcall* pfHover)(void*)
);
typedef void (__fastcall *ButtonSetText_t)(void* pBtn, int eFont, const wchar_t* wszText, int nColor);
typedef void* (__fastcall *CellFileLoad_t)(const char* path, int type);
static ButtonCreate_t  fnButtonCreate  = NULL;
static ButtonSetText_t fnButtonSetText = NULL;
static CellFileLoad_t  fnCellFileLoad  = NULL;
static void* g_btnCellFile = NULL;      /* 130x35 yellow button graphic */
static void* g_btnCellFileRed = NULL;   /* 130x35 red button graphic */
/* 1.8.2: green Connect-button variant — used by PollAPStatus to indicate
 * an authenticated AP session. Disconnected state remains red. */
static void* g_btnCellFileGreen = NULL; /* 130x35 green button graphic */
/* 1.8.0: 260x35 wide variants for longer labels like "Skill Hunting" */
static void* g_btnCellFileWide    = NULL;
static void* g_btnCellFileWideRed = NULL;

/* 1.8.0: Connect button tracked so the tick loop can swap cellfile
 * (red=disconnected / yellow=connected) based on g_apConnected. */
static void* g_btnConnectBtn = NULL;

/* Track which buttons are class buttons (indices 1-8: Amazon through Assassin) */
#define CLASS_BTN_START 1
#define CLASS_BTN_END 8

/* Track our created buttons for cleanup */
#define MAX_TITLE_BTNS 64
static void* g_titleBtns[MAX_TITLE_BTNS] = {0};
static int*  g_titleBtnVals[MAX_TITLE_BTNS] = {0}; /* pointer to the setting value */
static const wchar_t* g_titleBtnLabels[MAX_TITLE_BTNS] = {0};
static int   g_titleBtnCount = 0;

/* ================================================================
 * GLOBAL STATE
 * ================================================================ */

/* Function pointers - resolved at init */
static DrawText_t     fnText     = NULL;
static SetFont_t      fnFont     = NULL;
static DrawRect_t     fnRect     = NULL;
static DrawLine_t     fnLine     = NULL;
static WinLoadCellFile_t fnCelLoad = NULL;
static WinDrawCellFile_t fnCelDraw = NULL;

/* D2Client GetUIVar — queries panel open/close state.
 * Internal calling convention: varno in EAX, returns DWORD.
 * Offset 0xBE400 from D2Client base (1.10f). */
static DWORD g_getUIVarAddr = 0;
static DWORD __declspec(naked) __fastcall CallGetUIVar(DWORD varno) {
    __asm {
        mov eax, ecx       /* varno from fastcall ecx -> eax */
        jmp g_getUIVarAddr  /* jump to D2Client GetUIVar */
    }
}
static DWORD (*g_fnGetUIVar)(DWORD) = NULL;
static GetUnitStat_t  fnGetStat  = NULL;
static SetUnitStat_t  fnSetStat  = NULL;
static AddUnitStat_t  fnAddStat  = NULL;  /* ordinal 10518 — REAL stat add with callbacks */
static ClientSend_t   fnClientSend = NULL;
static AddGold_t      fnAddGold  = NULL;  /* D2Game PLRTRADE_AddGold — proper gold */

/* ================================================================
 * TITLE SCREEN SETTINGS - Implementation using REAL D2Win buttons
 * ================================================================ */
static void TitleSettings_Load(void) {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* sl2 = strrchr(exePath, '\\');
    if (sl2) *(sl2 + 1) = 0;
    strcat(exePath, "Archipelago\\d2arch.ini");
    strcpy(ts_iniPath, exePath);
    ts_Goal = GetPrivateProfileIntA("settings", "Goal", 4, ts_iniPath);
    /* 1.8.0: two independent toggles. If the user has an old INI with
     * GameMode=N and no new keys yet, seed defaults from the legacy value. */
    {
        int legacyMode = GetPrivateProfileIntA("settings", "GameMode", -1, ts_iniPath);
        ts_SkillHuntingOn = GetPrivateProfileIntA("settings", "SkillHunting",
                                                   (legacyMode == 1) ? 0 : 1, ts_iniPath);
        ts_ZoneLockingOn  = GetPrivateProfileIntA("settings", "ZoneLocking",
                                                   (legacyMode == 1) ? 1 : 0, ts_iniPath);
    }
    ts_XPMultiplier_idx = GetPrivateProfileIntA("settings", "XPMultiplier", 1, ts_iniPath) - 1;
    if (ts_XPMultiplier_idx < 0) ts_XPMultiplier_idx = 0;
    if (ts_XPMultiplier_idx >= XP_COUNT) ts_XPMultiplier_idx = XP_COUNT - 1;
    if (ts_Goal < 0 || ts_Goal >= GOAL_COUNT) ts_Goal = 4;
    ts_MonsterShuffle = GetPrivateProfileIntA("settings", "MonsterShuffle", 0, ts_iniPath);
    ts_BossShuffle = GetPrivateProfileIntA("settings", "BossShuffle", 0, ts_iniPath);
    ts_ShopShuffle = GetPrivateProfileIntA("settings", "ShopShuffle", 0, ts_iniPath);
    ts_EntranceShuffle = GetPrivateProfileIntA("settings", "EntranceShuffle", 0, ts_iniPath); /* 1.9.0 */
    ts_TrapsEnabled = GetPrivateProfileIntA("settings", "TrapsEnabled", 1, ts_iniPath); /* 1.8.4 */
    /* 1.9.0 — bonus check toggles (default 0 = OFF) */
    ts_CheckShrines        = GetPrivateProfileIntA("settings", "CheckShrines",        0, ts_iniPath);
    ts_CheckUrns           = GetPrivateProfileIntA("settings", "CheckUrns",           0, ts_iniPath);
    ts_CheckBarrels        = GetPrivateProfileIntA("settings", "CheckBarrels",        0, ts_iniPath);
    ts_CheckChests         = GetPrivateProfileIntA("settings", "CheckChests",         0, ts_iniPath);
    ts_CheckSetPickups     = GetPrivateProfileIntA("settings", "CheckSetPickups",     0, ts_iniPath);
    ts_CheckGoldMilestones = GetPrivateProfileIntA("settings", "CheckGoldMilestones", 0, ts_iniPath);
    /* 1.9.0 — Collection-goal sub-targets (only effective when Goal=15 / Collection) */
    ts_CollGoalSets     = GetPrivateProfileIntA("settings", "CollGoalSets",     1, ts_iniPath);
    ts_CollGoalRunes    = GetPrivateProfileIntA("settings", "CollGoalRunes",    1, ts_iniPath);
    ts_CollGoalGems     = GetPrivateProfileIntA("settings", "CollGoalGems",     1, ts_iniPath);
    ts_CollGoalSpecials = GetPrivateProfileIntA("settings", "CollGoalSpecials", 1, ts_iniPath);
    /* 1.8.0 cleanup: TreasureCows INI load extracted to pending_reimplementation/ */
    ts_QuestStory = GetPrivateProfileIntA("settings", "QuestStory", 1, ts_iniPath);
    ts_QuestHunting = GetPrivateProfileIntA("settings", "QuestHunting", 1, ts_iniPath);
    ts_QuestKillZones = GetPrivateProfileIntA("settings", "QuestKillZones", 1, ts_iniPath);
    ts_QuestExploration = GetPrivateProfileIntA("settings", "QuestExploration", 1, ts_iniPath);
    ts_QuestWaypoints = GetPrivateProfileIntA("settings", "QuestWaypoints", 1, ts_iniPath);
    ts_QuestLevelMilestones = GetPrivateProfileIntA("settings", "QuestLevelMilestones", 1, ts_iniPath);
    ts_XPMultiplier = GetPrivateProfileIntA("settings", "XPMultiplier", 1, ts_iniPath);
    ts_ClassFilter = GetPrivateProfileIntA("settings", "ClassFilter", 0, ts_iniPath);
    ts_ClsEnabled[0] = GetPrivateProfileIntA("settings", "ClsAmazon", 1, ts_iniPath);
    ts_ClsEnabled[1] = GetPrivateProfileIntA("settings", "ClsSorceress", 1, ts_iniPath);
    ts_ClsEnabled[2] = GetPrivateProfileIntA("settings", "ClsNecromancer", 1, ts_iniPath);
    ts_ClsEnabled[3] = GetPrivateProfileIntA("settings", "ClsPaladin", 1, ts_iniPath);
    ts_ClsEnabled[4] = GetPrivateProfileIntA("settings", "ClsBarbarian", 1, ts_iniPath);
    ts_ClsEnabled[5] = GetPrivateProfileIntA("settings", "ClsDruid", 1, ts_iniPath);
    ts_ClsEnabled[6] = GetPrivateProfileIntA("settings", "ClsAssassin", 1, ts_iniPath);
    ts_IPlayAssassin = GetPrivateProfileIntA("settings", "IPlayAssassin", 0, ts_iniPath);
}
static void TitleSettings_Save(void) {
    char buf[16];
    #define SAVE_INT(sec, key, val) _snprintf(buf, 15, "%d", val); WritePrivateProfileStringA(sec, key, buf, ts_iniPath)
    SAVE_INT("settings", "Goal", ts_Goal);
    SAVE_INT("settings", "SkillHunting", ts_SkillHuntingOn);
    SAVE_INT("settings", "ZoneLocking", ts_ZoneLockingOn);
    SAVE_INT("settings", "XPMultiplier", ts_XPMultiplier_idx + 1);
    SAVE_INT("settings", "MonsterShuffle", ts_MonsterShuffle);
    SAVE_INT("settings", "BossShuffle", ts_BossShuffle);
    SAVE_INT("settings", "ShopShuffle", ts_ShopShuffle);
    SAVE_INT("settings", "EntranceShuffle", ts_EntranceShuffle); /* 1.9.0 */
    SAVE_INT("settings", "TrapsEnabled", ts_TrapsEnabled); /* 1.8.4 */
    /* 1.9.0 — bonus check toggles */
    SAVE_INT("settings", "CheckShrines",        ts_CheckShrines);
    SAVE_INT("settings", "CheckUrns",           ts_CheckUrns);
    SAVE_INT("settings", "CheckBarrels",        ts_CheckBarrels);
    SAVE_INT("settings", "CheckChests",         ts_CheckChests);
    SAVE_INT("settings", "CheckSetPickups",     ts_CheckSetPickups);
    SAVE_INT("settings", "CheckGoldMilestones", ts_CheckGoldMilestones);
    SAVE_INT("settings", "CollGoalSets",     ts_CollGoalSets);     /* 1.9.0 */
    SAVE_INT("settings", "CollGoalRunes",    ts_CollGoalRunes);    /* 1.9.0 */
    SAVE_INT("settings", "CollGoalGems",     ts_CollGoalGems);     /* 1.9.0 */
    SAVE_INT("settings", "CollGoalSpecials", ts_CollGoalSpecials); /* 1.9.0 */
    /* 1.8.0 cleanup: TreasureCows INI save extracted */
    SAVE_INT("settings", "QuestStory", ts_QuestStory);
    SAVE_INT("settings", "QuestHunting", ts_QuestHunting);
    SAVE_INT("settings", "QuestKillZones", ts_QuestKillZones);
    SAVE_INT("settings", "QuestExploration", ts_QuestExploration);
    SAVE_INT("settings", "QuestWaypoints", ts_QuestWaypoints);
    SAVE_INT("settings", "QuestLevelMilestones", ts_QuestLevelMilestones);
    /* 1.8.2: removed duplicate SAVE_INT("XPMultiplier", ts_XPMultiplier).
     * The picker callbacks (OnXPPickN) only update ts_XPMultiplier_idx — the
     * old ts_XPMultiplier global is never refreshed, so this duplicate save
     * was overwriting the correct ts_XPMultiplier_idx+1 value (line 207)
     * with a stale 1, silently undoing every XP-multiplier change. */
    SAVE_INT("settings", "ClassFilter", ts_ClassFilter);
    SAVE_INT("settings", "ClsAmazon", ts_ClsEnabled[0]);
    SAVE_INT("settings", "ClsSorceress", ts_ClsEnabled[1]);
    SAVE_INT("settings", "ClsNecromancer", ts_ClsEnabled[2]);
    SAVE_INT("settings", "ClsPaladin", ts_ClsEnabled[3]);
    SAVE_INT("settings", "ClsBarbarian", ts_ClsEnabled[4]);
    SAVE_INT("settings", "ClsDruid", ts_ClsEnabled[5]);
    SAVE_INT("settings", "ClsAssassin", ts_ClsEnabled[6]);
    SAVE_INT("settings", "IPlayAssassin", ts_IPlayAssassin);
    #undef SAVE_INT
}

/* Set button cellfile to red or normal based on value.
 * 1.8.0: buttons created with the wide (260px) cellfile get the wide
 * red/yellow pair; others get the default 130px pair. We detect which
 * by looking at the button's current cellfile pointer — if it matches
 * either wide variant, swap to the other wide. Otherwise use default. */
static void SetBtnColor(void* btn, int isOn) {
    if (!btn) return;
    /* Cellfile is at control offset +0x04 */
    void* current = *(void**)((BYTE*)btn + 0x04);
    BOOL isWide = (current == g_btnCellFileWide || current == g_btnCellFileWideRed);
    void* cell;
    if (isWide) {
        cell = isOn ? g_btnCellFileWide : g_btnCellFileWideRed;
    } else {
        cell = isOn ? g_btnCellFile : g_btnCellFileRed;
    }
    if (cell) *(void**)((BYTE*)btn + 0x04) = cell;
}

/* Update all class button colors based on filter state */
static void UpdateClassColors(void) {
    if (!g_btnCellFileRed) return;
    for (int i = CLASS_BTN_START; i <= CLASS_BTN_END && i < g_titleBtnCount; i++) {
        if (!g_titleBtns[i]) continue;
        if (ts_ClassFilter) {
            /* Filter ON: each class shows its own color (red=off, yellow=on) */
            SetBtnColor(g_titleBtns[i], *g_titleBtnVals[i]);
        } else {
            /* Filter OFF: all classes are red (disabled/irrelevant) */
            *(void**)((BYTE*)g_titleBtns[i] + 0x04) = g_btnCellFileRed;
        }
    }
}

/* ============================================================
 * DROPDOWN SYSTEM - hidden buttons shown/hidden below trigger
 * ============================================================ */
#define DROP_MAX 15
static void* g_goalDropBtns[GOAL_COUNT] = {0};
static void* g_xpDropBtns[XP_COUNT] = {0};
static int g_goalDropYs[GOAL_COUNT] = {0};
static int g_xpDropYs[XP_COUNT] = {0};
static int g_goalDropOpen = 0, g_xpDropOpen = 0;

static void HideDropBtns(void** btns, int count) {
    for (int i = 0; i < count; i++)
        if (btns[i]) *(int*)((BYTE*)btns[i] + 0x10) = -5000;
}
static void ShowDropBtns(void** btns, int* ys, int count) {
    for (int i = 0; i < count; i++)
        if (btns[i]) *(int*)((BYTE*)btns[i] + 0x10) = ys[i];
}
static void CloseAllDropdowns(void) {
    HideDropBtns(g_goalDropBtns, GOAL_COUNT); g_goalDropOpen = 0;
    HideDropBtns(g_xpDropBtns, XP_COUNT); g_xpDropOpen = 0;
}

/* Goal pick callbacks */
#define MAKE_GOAL_PICK(n) static int __stdcall OnGoalPick##n(void* p) { \
    (void)p; ts_Goal = n; \
    if (g_btnGoal && fnButtonSetText) fnButtonSetText(g_btnGoal, 1, goalOptions[n], 0); \
    CloseAllDropdowns(); TitleSettings_Save(); return 1; }
MAKE_GOAL_PICK(0)  MAKE_GOAL_PICK(1)  MAKE_GOAL_PICK(2)  MAKE_GOAL_PICK(3)
MAKE_GOAL_PICK(4)  MAKE_GOAL_PICK(5)  MAKE_GOAL_PICK(6)  MAKE_GOAL_PICK(7)
MAKE_GOAL_PICK(8)  MAKE_GOAL_PICK(9)  MAKE_GOAL_PICK(10) MAKE_GOAL_PICK(11)
MAKE_GOAL_PICK(12) MAKE_GOAL_PICK(13) MAKE_GOAL_PICK(14) MAKE_GOAL_PICK(15)
static int (__stdcall *goalPickCbs[16])(void*) = {
    OnGoalPick0,OnGoalPick1,OnGoalPick2,OnGoalPick3,OnGoalPick4,
    OnGoalPick5,OnGoalPick6,OnGoalPick7,OnGoalPick8,OnGoalPick9,
    OnGoalPick10,OnGoalPick11,OnGoalPick12,OnGoalPick13,OnGoalPick14,
    OnGoalPick15  /* 1.9.0 — Collection */
};

/* 1.8.0 removed: OnModePick0 / OnModePick1 — mode dropdown replaced by
 * two independent toggles (Skill Hunting + Zone Locking) created via
 * the standard CreateToggleBtn path in TitleSettings_CreateButtons. */

/* XP pick callbacks */
#define MAKE_XP_PICK(n) static int __stdcall OnXPPick##n(void* p) { \
    (void)p; ts_XPMultiplier_idx = n; \
    if (g_btnXP && fnButtonSetText) fnButtonSetText(g_btnXP, 1, xpOptions[n], 0); \
    CloseAllDropdowns(); TitleSettings_Save(); return 1; }
MAKE_XP_PICK(0) MAKE_XP_PICK(1) MAKE_XP_PICK(2) MAKE_XP_PICK(3) MAKE_XP_PICK(4)
MAKE_XP_PICK(5) MAKE_XP_PICK(6) MAKE_XP_PICK(7) MAKE_XP_PICK(8) MAKE_XP_PICK(9)
static int (__stdcall *xpPickCbs[10])(void*) = {
    OnXPPick0,OnXPPick1,OnXPPick2,OnXPPick3,OnXPPick4,
    OnXPPick5,OnXPPick6,OnXPPick7,OnXPPick8,OnXPPick9
};

/* AP Connect button callback — reads editbox fields, starts bridge, connects */
extern BOOL g_apMode;     /* defined in d2arch_input.c (included later in unity build) */
extern BOOL g_apPolling;  /* 1.9.0: gating flag for PollAPStatus, set on click */
static int __stdcall OnConnectClick(void* p) {
    (void)p;
    /* Debounce */
    static DWORD s_lastClick = 0;
    DWORD now = GetTickCount();
    if (now - s_lastClick < 1000) return 1;
    s_lastClick = now;

    /* Read editbox text (wchar at offset 0x5C) into g_apIP/g_apSlot/g_apPassword */
    if (g_apEdServer) {
        wchar_t* ws = (wchar_t*)((BYTE*)g_apEdServer + 0x5C);
        WideCharToMultiByte(CP_ACP, 0, ws, -1, g_apIP, 63, NULL, NULL);
    }
    if (g_apEdSlot) {
        wchar_t* ws = (wchar_t*)((BYTE*)g_apEdSlot + 0x5C);
        WideCharToMultiByte(CP_ACP, 0, ws, -1, g_apSlot, 31, NULL, NULL);
    }
    if (g_apEdPass) {
        wchar_t* ws = (wchar_t*)((BYTE*)g_apEdPass + 0x5C);
        WideCharToMultiByte(CP_ACP, 0, ws, -1, g_apPassword, 31, NULL, NULL);
    }

    Log("AP CONNECT: IP=%s Slot=%s\n", g_apIP, g_apSlot);
    if (g_apIP[0] && g_apSlot[0]) {
        StartAPBridge();
        WriteAPCommand("connect");
        /* 1.9.0 — set g_apPolling (NOT g_apMode) so PollAPStatus starts
         * reading ap_status.dat. g_apMode stays FALSE until PollAPStatus
         * actually sees status=authenticated. This way a click that never
         * connects does NOT make any subsequently-loaded character bake
         * AP settings or default to the AP stash tab.
         *
         * (1.8.3 originally set g_apMode here to break a chicken-and-egg
         * with PollAPStatus's early-return — that's now solved by the
         * separate g_apPolling flag, which gates polling without claiming
         * "we are in AP mode".) */
        g_apPolling = TRUE;
        Log("AP CONNECT: g_apPolling set TRUE (waiting for auth before flipping g_apMode)\n");
    } else {
        Log("AP CONNECT: missing IP or Slot\n");
    }
    return 1;
}

/* Forward extern for AP-locked dropdowns. Same pattern as toggle buttons:
 * when AP is connected, slot_data is the source of truth and the dropdowns
 * are read-only — opening them would let the user change a value that
 * LoadAPSettings would just overwrite, so block the click and show why. */
extern BOOL g_apConnected;
static void ShowNotify(const char* text);

/* Trigger button callbacks - toggle dropdown open/close */
static int __stdcall OnGoalClick(void* pMsg) {
    if (g_apConnected) {
        ShowNotify("AP connected - settings locked (managed by server)");
        return 1;
    }
    if (g_goalDropOpen) { CloseAllDropdowns(); }
    else { CloseAllDropdowns(); ShowDropBtns(g_goalDropBtns, g_goalDropYs, GOAL_COUNT); g_goalDropOpen=1; }
    return 1;
}
static int __stdcall OnXPClick(void* pMsg) {
    if (g_apConnected) {
        ShowNotify("AP connected - settings locked (managed by server)");
        return 1;
    }
    if (g_xpDropOpen) { CloseAllDropdowns(); }
    else { CloseAllDropdowns(); ShowDropBtns(g_xpDropBtns, g_xpDropYs, XP_COUNT); g_xpDropOpen=1; }
    return 1;
}

/* Forward declarations — g_apConnected and g_apMode live in d2arch_input.c,
 * ShowNotify in d2arch_render.c, all #included after this file but part of
 * the same translation unit. */
extern BOOL g_apConnected;
extern BOOL g_apMode;
static void ShowNotify(const char* text);
static void ShowBigWarning(const char* text);

/* Toggle callback */
static int __stdcall OnToggleClick(void* pMsg) {
    void* pClickedBtn = *(void**)pMsg;

    /* 1.8.0: When AP is connected, title-screen toggles are ignored because
     * LoadAPSettings() overwrites every setting from ap_settings.dat. Block
     * clicks entirely and notify the user so they don't think their toggle
     * had effect. Standalone mode remains fully interactive. */
    if (g_apConnected) {
        ShowNotify("AP connected - settings locked (managed by server)");
        return 1;
    }

    for (int i = 0; i < g_titleBtnCount; i++) {
        if (g_titleBtns[i] == pClickedBtn && g_titleBtnVals[i]) {
            *g_titleBtnVals[i] = !*g_titleBtnVals[i];
            /* Update text */
            if (fnButtonSetText) {
                wchar_t txt[64];
                _snwprintf(txt, 63, L"%s:%s",
                    g_titleBtnLabels[i],
                    *g_titleBtnVals[i] ? L"ON" : L"OFF");
                fnButtonSetText(pClickedBtn, 1, txt, 0);
            }
            /* Update color: ON=yellow, OFF=red */
            SetBtnColor(pClickedBtn, *g_titleBtnVals[i]);
            /* If filter button toggled, update all class button colors */
            if (i == 0) UpdateClassColors(); /* index 0 = Filter */
            TitleSettings_Save();
            return 1;
        }
    }
    return 0;
}

/* Create a single toggle button with custom width, font index, and cellfile.
 * Pass cellFileOverride=NULL to use the default (130x35) g_btnCellFile. */
static void* CreateToggleBtnWFC(int x, int y, int width, int fontIdx,
                                void* cellFileOverride,
                                const wchar_t* label, int* valuePtr) {
    if (!fnButtonCreate || g_titleBtnCount >= MAX_TITLE_BTNS) return NULL;
    wchar_t txt[64];
    _snwprintf(txt, 63, L"%s:%s", label, *valuePtr ? L"ON" : L"OFF");

    void* cell = cellFileOverride ? cellFileOverride : g_btnCellFile;
    void* btn = fnButtonCreate(x, y, width, 35, cell,
        OnToggleClick, 0, 0, 0, 0, NULL);
    if (btn && fnButtonSetText) {
        fnButtonSetText(btn, fontIdx, txt, *valuePtr ? 1 : 3);
    }
    if (btn) {
        g_titleBtns[g_titleBtnCount] = btn;
        g_titleBtnVals[g_titleBtnCount] = valuePtr;
        g_titleBtnLabels[g_titleBtnCount] = label;
        g_titleBtnCount++;
    }
    return btn;
}

/* Default-width (130px) + default font (1 = medium) wrapper */
static void* CreateToggleBtn(int x, int y, const wchar_t* label, int* valuePtr) {
    return CreateToggleBtnWFC(x, y, 130, 1, NULL, label, valuePtr);
}
/* Width-only customisation (font stays at 1, default cellfile) */
static void* CreateToggleBtnW(int x, int y, int width, const wchar_t* label, int* valuePtr) {
    return CreateToggleBtnWFC(x, y, width, 1, NULL, label, valuePtr);
}
/* Width + font customisation (default cellfile) */
static void* CreateToggleBtnWF(int x, int y, int width, int fontIdx,
                               const wchar_t* label, int* valuePtr) {
    return CreateToggleBtnWFC(x, y, width, fontIdx, NULL, label, valuePtr);
}

/* Check if main menu is active by looking for SINGLE PLAYER button */
/* Check if a specific button exists in D2Win control list */
static BOOL ButtonExists(int x, int y, int w, int h) {
    if (!g_ppControlList) return FALSE;
    __try {
        void* pCtrl = *g_ppControlList;
        while (pCtrl) {
            if (CTRL_TYPE(pCtrl) == D2WIN_BUTTON &&
                CTRL_X(pCtrl) == x && CTRL_Y(pCtrl) == y &&
                CTRL_W(pCtrl) == w && CTRL_H(pCtrl) == h) {
                return TRUE;
            }
            pCtrl = CTRL_NEXT(pCtrl);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return FALSE;
}

/* Main menu = SINGLE PLAYER (264,490) + OTHER MULTI (264,528) + EXIT (264,568) all exist */
static BOOL IsMainMenuActive(void) {
    return ButtonExists(264, 490, 272, 35) &&
           ButtonExists(264, 528, 272, 35) &&
           ButtonExists(264, 568, 272, 35);
}

/* Show/hide our buttons by moving Y on/offscreen */
static BOOL g_apButtonsVisible = FALSE;
static int  g_apButtonOrigY[MAX_TITLE_BTNS] = {0};

static void SetButtonsVisible(BOOL bShow) {
    if (bShow == g_apButtonsVisible) return;
    g_apButtonsVisible = bShow;
    for (int i = 0; i < g_titleBtnCount; i++) {
        if (!g_titleBtns[i]) continue;
        int* pY = (int*)((BYTE*)g_titleBtns[i] + 0x10);
        if (bShow) {
            *pY = g_apButtonOrigY[i];
            /* Restore cellfile pointer in case D2Win cleared it */
            void** ppCell = (void**)((BYTE*)g_titleBtns[i] + 0x04);
            int type = *(int*)((BYTE*)g_titleBtns[i] + 0x00);
            if (type == D2WIN_BUTTON && *ppCell == NULL) {
                /* Restore appropriate cellfile */
                if (g_titleBtnVals[i] != NULL) {
                    /* Toggle button - restore based on ON/OFF state */
                    *ppCell = *g_titleBtnVals[i] ? g_btnCellFile : g_btnCellFileRed;
                } else {
                    *ppCell = g_btnCellFile;
                }
            }
        } else {
            g_apButtonOrigY[i] = *pY;
            *pY = -5000;
        }
    }
}

/* Create all settings buttons - positioned on right side */
static void TitleSettings_CreateButtons(void) {
    if (!fnButtonCreate) return;
    g_titleBtnCount = 0;

    /* Layout from d2arch.ini [layout] section - each group has own position */
    int col1 = GetPrivateProfileIntA("layout", "Col1X", 5, ts_iniPath);
    int col2 = GetPrivateProfileIntA("layout", "Col2X", 140, ts_iniPath);
    int col3 = GetPrivateProfileIntA("layout", "Col3X", 280, ts_iniPath);
    int sp = GetPrivateProfileIntA("layout", "Spacing", 30, ts_iniPath);
    int classY = GetPrivateProfileIntA("layout", "ClassY", 310, ts_iniPath);
    int shuffleY = GetPrivateProfileIntA("layout", "ShuffleY", 310, ts_iniPath);
    int questY = GetPrivateProfileIntA("layout", "QuestY", 310, ts_iniPath);
    int row;

    /* Column 1: Classes */
    row = classY;
    CreateToggleBtn(col1, row, L"Filter", &ts_ClassFilter); row += sp;
    CreateToggleBtn(col1, row, L"Amazon", &ts_ClsEnabled[0]); row += sp;
    CreateToggleBtn(col1, row, L"Sorc", &ts_ClsEnabled[1]); row += sp;
    CreateToggleBtn(col1, row, L"Necro", &ts_ClsEnabled[2]); row += sp;
    CreateToggleBtn(col1, row, L"Paladin", &ts_ClsEnabled[3]); row += sp;
    CreateToggleBtn(col1, row, L"Barb", &ts_ClsEnabled[4]); row += sp;
    CreateToggleBtn(col1, row, L"Druid", &ts_ClsEnabled[5]); row += sp;
    CreateToggleBtn(col1, row, L"Assassin", &ts_ClsEnabled[6]);
    /* 1.8.0 removed: "PlayAsn" toggle — legacy 1.7.0 trap-filter was removed
     * in 1.7.1, flag only acknowledged in apworld. No reader exists. */

    /* Column 2: Shuffle (Monster, Boss, Entrance) + Traps toggle (1.8.4) */
    row = shuffleY;
    CreateToggleBtn(col2, row, L"Monster", &ts_MonsterShuffle); row += sp;
    CreateToggleBtn(col2, row, L"Boss", &ts_BossShuffle); row += sp;
    CreateToggleBtn(col2, row, L"Entrance", &ts_EntranceShuffle); row += sp; /* 1.9.0 */
    CreateToggleBtn(col2, row, L"Traps", &ts_TrapsEnabled);

    /* 1.9.0 — Bonus check toggles. Each button has its own X/Y INI keys
     * so the user can place them anywhere on the title screen. Defaults
     * push them to a 4th column further right of the Collection toggles
     * to avoid overlapping the class column when Class Filter is OFF.
     * INI section [layout]:
     *   ShrinesX/Y, UrnsX/Y, BarrelsX/Y, ChestsX/Y, SetPickupsX/Y, GoldMSX/Y
     * Also a BonusY base + sp stacking — only used if individual key absent.
     */
    {
        int bonusBaseX = GetPrivateProfileIntA("layout", "BonusX", col2 + 145, ts_iniPath);
        int bonusBaseY = GetPrivateProfileIntA("layout", "BonusY", shuffleY,    ts_iniPath);
        int bonusSp    = GetPrivateProfileIntA("layout", "BonusSpacing", sp,    ts_iniPath);

        int sx = GetPrivateProfileIntA("layout", "ShrinesX", bonusBaseX, ts_iniPath);
        int sy = GetPrivateProfileIntA("layout", "ShrinesY", bonusBaseY + 0 * bonusSp, ts_iniPath);
        CreateToggleBtn(sx, sy, L"Shrines", &ts_CheckShrines);

        sx = GetPrivateProfileIntA("layout", "UrnsX", bonusBaseX, ts_iniPath);
        sy = GetPrivateProfileIntA("layout", "UrnsY", bonusBaseY + 1 * bonusSp, ts_iniPath);
        CreateToggleBtn(sx, sy, L"Urns", &ts_CheckUrns);

        sx = GetPrivateProfileIntA("layout", "BarrelsX", bonusBaseX, ts_iniPath);
        sy = GetPrivateProfileIntA("layout", "BarrelsY", bonusBaseY + 2 * bonusSp, ts_iniPath);
        CreateToggleBtn(sx, sy, L"Barrels", &ts_CheckBarrels);

        sx = GetPrivateProfileIntA("layout", "ChestsX", bonusBaseX, ts_iniPath);
        sy = GetPrivateProfileIntA("layout", "ChestsY", bonusBaseY + 3 * bonusSp, ts_iniPath);
        CreateToggleBtn(sx, sy, L"Chests", &ts_CheckChests);

        sx = GetPrivateProfileIntA("layout", "SetPickupsX", bonusBaseX, ts_iniPath);
        sy = GetPrivateProfileIntA("layout", "SetPickupsY", bonusBaseY + 4 * bonusSp, ts_iniPath);
        CreateToggleBtn(sx, sy, L"Set Pickups", &ts_CheckSetPickups);

        sx = GetPrivateProfileIntA("layout", "GoldMSX", bonusBaseX, ts_iniPath);
        sy = GetPrivateProfileIntA("layout", "GoldMSY", bonusBaseY + 5 * bonusSp, ts_iniPath);
        CreateToggleBtn(sx, sy, L"Gold MS", &ts_CheckGoldMilestones);
    }
    /* 1.9.0 — Collection-goal sub-target toggles. Placed in their own
     * column (CollX, CollY) under the right-side quest-toggle stack.
     * Right-column flow with default questY=180, sp=30:
     *   Skill Hunting Y=180, Zone Locking Y=210, +sp/2 gap →
     *   Hunting Y=255, KillZn Y=285, Explore Y=315, Waypnt Y=345,
     *   Levels Y=375. After-Levels position = Y=405.
     * Default CollY=420 leaves a sp/2 (=15) breathing gap between
     * Levels and Coll Set, matching the gap style used between the
     * top game-mode group and the quest-toggle group above. INI
     * override available via [layout] CollX / CollY.
     * Effective only when Goal=Collection (idx 15); otherwise the
     * flags are stored but ignored. */
    {
        /* Master fallback positions — only used when a per-button
         * key is absent. Per-button CollSetX/Y, CollRunesX/Y,
         * CollGemsX/Y, CollSpecX/Y always win when present. Same
         * pattern as the bonus check buttons above. */
        int collX = GetPrivateProfileIntA("layout", "CollX", col3, ts_iniPath);
        int collY = GetPrivateProfileIntA("layout", "CollY", 420, ts_iniPath);

        int sx, sy;
        sx = GetPrivateProfileIntA("layout", "CollSetX",   collX,           ts_iniPath);
        sy = GetPrivateProfileIntA("layout", "CollSetY",   collY + 0 * sp,  ts_iniPath);
        CreateToggleBtn(sx, sy, L"Coll Set",   &ts_CollGoalSets);

        sx = GetPrivateProfileIntA("layout", "CollRunesX", collX,           ts_iniPath);
        sy = GetPrivateProfileIntA("layout", "CollRunesY", collY + 1 * sp,  ts_iniPath);
        CreateToggleBtn(sx, sy, L"Coll Runes", &ts_CollGoalRunes);

        sx = GetPrivateProfileIntA("layout", "CollGemsX",  collX,           ts_iniPath);
        sy = GetPrivateProfileIntA("layout", "CollGemsY",  collY + 2 * sp,  ts_iniPath);
        CreateToggleBtn(sx, sy, L"Coll Gems",  &ts_CollGoalGems);

        sx = GetPrivateProfileIntA("layout", "CollSpecX",  collX,           ts_iniPath);
        sy = GetPrivateProfileIntA("layout", "CollSpecY",  collY + 3 * sp,  ts_iniPath);
        CreateToggleBtn(sx, sy, L"Coll Spec",  &ts_CollGoalSpecials);
    }
    /* 1.8.0 removed: "Shop" toggle — flag is parsed from slot_data but no
     * shop-shuffle logic exists yet. Pending implementation. */

    /* Dropdown buttons: Goal, XP (individual XY from ini).
     * 1.8.0: Mode dropdown removed — replaced by two ON/OFF toggles
     * ("Skill Hunting" and "Zone Locking") created later in this function
     * alongside the quest-type toggles. */
    {
        int goalX = GetPrivateProfileIntA("layout", "GoalX", 140, ts_iniPath);
        int goalY = GetPrivateProfileIntA("layout", "GoalY", 180, ts_iniPath);
        int xpX = GetPrivateProfileIntA("layout", "XPX", 420, ts_iniPath);
        int xpY = GetPrivateProfileIntA("layout", "XPY", 180, ts_iniPath);

        g_btnGoal = fnButtonCreate(goalX, goalY, 130, 35, g_btnCellFile,
            OnGoalClick, 0, 0, 0, 0, NULL);
        if (g_btnGoal && fnButtonSetText)
            fnButtonSetText(g_btnGoal, 1, goalOptions[ts_Goal], 0);

        g_btnXP = fnButtonCreate(xpX, xpY, 130, 35, g_btnCellFile,
            OnXPClick, 0, 0, 0, 0, NULL);
        if (g_btnXP && fnButtonSetText)
            fnButtonSetText(g_btnXP, 1, xpOptions[ts_XPMultiplier_idx], 0);

        /* Track for visibility */
        if (g_btnGoal && g_titleBtnCount < MAX_TITLE_BTNS) {
            g_titleBtns[g_titleBtnCount] = g_btnGoal; g_titleBtnVals[g_titleBtnCount] = NULL;
            g_titleBtnLabels[g_titleBtnCount] = L"Goal"; g_titleBtnCount++;
        }
        if (g_btnXP && g_titleBtnCount < MAX_TITLE_BTNS) {
            g_titleBtns[g_titleBtnCount] = g_btnXP; g_titleBtnVals[g_titleBtnCount] = NULL;
            g_titleBtnLabels[g_titleBtnCount] = L"XP"; g_titleBtnCount++;
        }

        /* Create dropdown option buttons (hidden by default at Y=-5000) */
        {
            int dropSp = 26; /* smaller spacing for dropdown items */
            int baseY;
            int i;

            /* Goal dropdown (15 options below Goal button) */
            baseY = goalY + 38;
            for (i = 0; i < GOAL_COUNT; i++) {
                g_goalDropBtns[i] = fnButtonCreate(goalX, -5000, 130, 35, g_btnCellFile,
                    goalPickCbs[i], 0, 0, 0, 0, NULL);
                if (g_goalDropBtns[i] && fnButtonSetText)
                    fnButtonSetText(g_goalDropBtns[i], 1, goalOptions[i], 0);
                g_goalDropYs[i] = baseY + i * dropSp;
                /* Track for visibility management */
                if (g_titleBtnCount < MAX_TITLE_BTNS) {
                    g_titleBtns[g_titleBtnCount] = g_goalDropBtns[i];
                    g_titleBtnVals[g_titleBtnCount] = NULL;
                    g_titleBtnLabels[g_titleBtnCount] = L"GoalDrop";
                    g_titleBtnCount++;
                }
            }

            /* XP dropdown (10 options below XP button) */
            baseY = xpY + 38;
            for (i = 0; i < XP_COUNT; i++) {
                g_xpDropBtns[i] = fnButtonCreate(xpX, -5000, 130, 35, g_btnCellFile,
                    xpPickCbs[i], 0, 0, 0, 0, NULL);
                if (g_xpDropBtns[i] && fnButtonSetText)
                    fnButtonSetText(g_xpDropBtns[i], 1, xpOptions[i], 0);
                g_xpDropYs[i] = baseY + i * dropSp;
                if (g_titleBtnCount < MAX_TITLE_BTNS) {
                    g_titleBtns[g_titleBtnCount] = g_xpDropBtns[i];
                    g_titleBtnVals[g_titleBtnCount] = NULL;
                    g_titleBtnLabels[g_titleBtnCount] = L"XPDrop";
                    g_titleBtnCount++;
                }
            }
        }
    }

    /* AP Connection fields: Label + Editbox + Connect button */
    {
        int apX = GetPrivateProfileIntA("layout", "APX", 300, ts_iniPath);
        int apY = GetPrivateProfileIntA("layout", "APY", 440, ts_iniPath);
        int rowH = 28; /* row height */

        typedef void* (__stdcall *ControlCreate_t)(void*);
        HMODULE hW2 = GetModuleHandleA("D2Win.dll");
        ControlCreate_t fnCtrlCreate = hW2 ? (ControlCreate_t)GetProcAddress(hW2, (LPCSTR)10017) : NULL;

        /* Load editbox cellfiles */
        void* cellLabel = fnCellFileLoad ? fnCellFileLoad("data\\global\\ui\\CharSelect\\editbox_label", 0) : NULL;
        /* Grey input box background (130x35) */
        void* cellInput = fnCellFileLoad ? fnCellFileLoad("data\\global\\ui\\CharSelect\\editbox_input", 0) : NULL;
        if (!cellInput) cellInput = g_btnCellFile; /* fallback to yellow toggle */

        /* Helper to create label + input pair */
        #define MAKE_AP_FIELD(labelText, iniKey, iniDefault, yOff) \
        { \
            /* Label (non-editable, just shows text) */ \
            void* lbl = fnButtonCreate(apX, apY + yOff, 55, 22, cellLabel, NULL, 0, 0, 0, 0, NULL); \
            if (lbl && fnButtonSetText) fnButtonSetText(lbl, 1, labelText, 0); \
            if (lbl && g_titleBtnCount < MAX_TITLE_BTNS) { \
                g_titleBtns[g_titleBtnCount] = lbl; g_titleBtnVals[g_titleBtnCount] = NULL; \
                g_titleBtnLabels[g_titleBtnCount] = labelText; g_titleBtnCount++; } \
            /* Input editbox */ \
            if (fnCtrlCreate) { \
                typedef struct { int nType; int nX; int nY; int nW; int nH; \
                    int f14; int f18; void* f1C; void* f20; void* f24; void* f28; void* f2C; } CI; \
                CI ci = {1, apX+60, apY+yOff, 200, 22, 0x08, 4, (void*)&cellInput, NULL, NULL, NULL, 0}; \
                void* ed = fnCtrlCreate(&ci); \
                if (ed) { \
                    char valA[64]; wchar_t valW[64]; \
                    GetPrivateProfileStringA("ap", iniKey, iniDefault, valA, 63, ts_iniPath); \
                    MultiByteToWideChar(CP_ACP, 0, valA, -1, valW, 63); \
                    wcscpy((wchar_t*)((BYTE*)ed + 0x5C), valW); \
                    int sl2 = (int)wcslen(valW); \
                    *(int*)((BYTE*)ed+0x58)=sl2; *(int*)((BYTE*)ed+0x54)=sl2; *(int*)((BYTE*)ed+0x50)=sl2; \
                    if (g_titleBtnCount < MAX_TITLE_BTNS) { \
                        g_titleBtns[g_titleBtnCount]=ed; g_titleBtnVals[g_titleBtnCount]=NULL; \
                        g_titleBtnLabels[g_titleBtnCount]=labelText; g_titleBtnCount++; } \
                } \
            } \
        }

        MAKE_AP_FIELD(L"Server:", "ServerIP", "localhost:38281", 0)
        g_apEdServer = g_titleBtns[g_titleBtnCount - 1]; /* last added = editbox */
        MAKE_AP_FIELD(L"Slot:", "SlotName", "", rowH)
        g_apEdSlot = g_titleBtns[g_titleBtnCount - 1];
        MAKE_AP_FIELD(L"Pass:", "Password", "", rowH*2)
        g_apEdPass = g_titleBtns[g_titleBtnCount - 1];
        #undef MAKE_AP_FIELD

        /* Connect button — starts in the colour matching current AP state:
         *   green  = authenticated (slot_data received, char will use it)
         *   red    = disconnected
         * PollAPStatus swaps the cellfile on transition (see d2arch_ap.c).
         * Initial colour matters for the title-rebuild path: when player
         * returns to title from char-select with AP still connected, the
         * button needs to come back GREEN immediately, not red-then-green. */
        void* connectCell = (g_apConnected && g_btnCellFileGreen)
                                ? g_btnCellFileGreen
                                : g_btnCellFileRed;
        void* btnConnect = fnButtonCreate(apX, apY + rowH*3 + 10, 130, 35,
            connectCell, OnConnectClick, 0, 0, 0, 0, NULL);
        if (btnConnect && fnButtonSetText)
            fnButtonSetText(btnConnect, 1, L"CONNECT", 0);
        if (btnConnect && g_titleBtnCount < MAX_TITLE_BTNS) {
            g_titleBtns[g_titleBtnCount] = btnConnect;
            g_titleBtnVals[g_titleBtnCount] = NULL;
            g_titleBtnLabels[g_titleBtnCount] = L"Connect";
            g_titleBtnCount++;
            g_btnConnectBtn = btnConnect;
        }
    }

    /* Column 3: Game Modes + Quests.
     * 1.8.0: Game modes (Skill Hunting + Zone Locking) sit ABOVE the quest
     * toggles. They're now independent ON/OFF so both can be active at
     * the same time. Quest toggles follow below — same column. */
    row = questY;
    /* 1.8.0: 250x35 WIDE cellfile for "Skill Hunting" / "Zone Locking".
     * X is right-aligned with the quest column below — quest toggles are
     * 130 wide at col3, ending at col3+130. The wide (250) game-mode
     * buttons share that right edge: X = col3 + 130 - 250. Configurable
     * via d2arch.ini [layout] GameModeX if needed. */
    int gameModeX = GetPrivateProfileIntA("layout", "GameModeX",
                                          col3 + 130 - 250, ts_iniPath);
    CreateToggleBtnWFC(gameModeX, row, 250, 1,
        ts_SkillHuntingOn ? g_btnCellFileWide : g_btnCellFileWideRed,
        L"Skill Hunting", &ts_SkillHuntingOn); row += sp;
    CreateToggleBtnWFC(gameModeX, row, 250, 1,
        ts_ZoneLockingOn  ? g_btnCellFileWide : g_btnCellFileWideRed,
        L"Zone Locking",  &ts_ZoneLockingOn);  row += sp;
    /* Small visual gap between game-mode toggles and quest toggles */
    row += sp / 2;
    /* 1.8.0 removed: "Story" toggle — D2's native main-story quests are
     * always required by the game engine; the toggle only hid them from
     * the UI which was misleading. Story quests stay always-ON internally
     * (forced to 1 in LoadAPSettings). */
    CreateToggleBtn(col3, row, L"Hunting", &ts_QuestHunting); row += sp;
    CreateToggleBtn(col3, row, L"KillZn", &ts_QuestKillZones); row += sp;
    CreateToggleBtn(col3, row, L"Explore", &ts_QuestExploration); row += sp;
    CreateToggleBtn(col3, row, L"Waypnt", &ts_QuestWaypoints); row += sp;
    CreateToggleBtn(col3, row, L"Levels", &ts_QuestLevelMilestones); row += sp;
    /* 1.8.0 cleanup: "Cows" toggle button extracted to pending_reimplementation/ */

    /* Save original Y positions for visibility toggling */
    for (int i = 0; i < g_titleBtnCount; i++) {
        if (g_titleBtns[i])
            g_apButtonOrigY[i] = *(int*)((BYTE*)g_titleBtns[i] + 0x10);
    }
    g_apButtonsVisible = TRUE;

    /* Set initial colors: OFF buttons = red, ON buttons = yellow */
    for (int i = 0; i < g_titleBtnCount; i++) {
        if (g_titleBtns[i] && g_titleBtnVals[i])
            SetBtnColor(g_titleBtns[i], *g_titleBtnVals[i]);
    }
    /* Apply filter logic to class buttons */
    UpdateClassColors();
}

/* Waypoint API */
typedef int (__stdcall *WaypointIsActivated_t)(void* pWaypointData, unsigned short wField);
static WaypointIsActivated_t fnWaypointIsActivated = NULL; /* D2Common ordinal 11146 */

/* Quest state API — checks D2's native quest completion flags */
typedef BOOL (__stdcall *QuestRecordGetState_t)(void* pQuestRecord, int nQuest, int nState);
static QuestRecordGetState_t fnGetQuestState = NULL; /* D2Common ordinal 11107 */

/* Quest state IAT hook */
typedef void (__stdcall *QuestRecordSetState_t)(void* pQuestRecord, int nQuest, int nState);
static QuestRecordSetState_t g_origSetQuestState = NULL;
#define MAX_D2_QUESTS 48
static volatile BOOL g_serverQuestComplete[3][MAX_D2_QUESTS];
static void __stdcall SetQuestState_IATHook(void* pQuestRecord, int nQuest, int nState);

/* Game module handles */
static HMODULE hD2Client = NULL;
static HMODULE hD2Common = NULL;
static HMODULE hD2Win    = NULL;
static HMODULE hD2Gfx   = NULL;
static HMODULE hD2Game   = NULL;
static HMODULE hD2Net    = NULL;

/* ================================================================
 * D2Win CONTROL SYSTEM — for removing/adding title screen buttons
 * ================================================================ */

/* D2WinControlStrc layout (from D2WinControlHeader.h):
 *   +0x00: nType (1=editbox, 2=image, 4=textbox, 6=button, ...)
 *   +0x0C: nImageX
 *   +0x10: nImageY
 *   +0x14: nWidth
 *   +0x18: nHeight
 *   +0x1C: pfDraw
 *   +0x3C: pNext (linked list) */
/* CTRL_ macros and D2WIN_BUTTON defined earlier in file */

/* D2Win control functions */
typedef void* (__stdcall *ControlDestroy_t)(void** ppControl);
typedef int   (__stdcall *ControlGetType_t)(void* pControl);
static ControlDestroy_t  fnControlDestroy = NULL;

/* g_ppControlList declared earlier in file */

/* Flag: title screen cleanup done */
static int g_titleCleanupDone = 0;

/* Timer ID for delayed title screen cleanup */
#define TITLE_CLEANUP_TIMER_ID 0xD2A1

/* Buttons to KEEP on title screen (identified by Y position and type):
 * We keep SINGLE PLAYER and EXIT DIABLO II, remove everything else. */
static int ShouldKeepControl(void* pCtrl) {
    int type = CTRL_TYPE(pCtrl);
    if (type != D2WIN_BUTTON) return 1; /* keep all non-buttons */

    int y = CTRL_Y(pCtrl);
    int w = CTRL_W(pCtrl);
    int h = CTRL_H(pCtrl);

    /* Log every button for diagnostics */
    Log("  TITLE BTN: type=%d x=%d y=%d w=%d h=%d\n",
        type, CTRL_X(pCtrl), y, w, h);

    /* Strategy: remove buttons in the MIDDLE of the screen.
     * Keep buttons at extreme top (SINGLE PLAYER is first) and bottom (EXIT is last).
     * We identify by checking if there are other buttons - the first and last
     * button type=6 in the list are kept. */
    return 1; /* Initially keep all — we'll refine after seeing the log */
}

/* Walk gpControlList and remove unwanted buttons */
static void CleanupTitleButtons(void) {
    /* DISABLED: cleanup was removing our own Archipelago buttons.
     * Vanilla buttons we don't want are covered by our buttons anyway. */
    g_titleCleanupDone = 1;
    return;
    if (!g_ppControlList || !fnControlDestroy) return;
    if (g_titleCleanupDone) return;

    void* pCtrl = *g_ppControlList;
    if (!pCtrl) {
        Log("TITLE CLEANUP: control list is empty, waiting...\n");
        return;
    }

    /* First pass: count and log all type=6 buttons */
    int btnCount = 0;
    void* firstBtn = NULL;
    void* lastBtn = NULL;
    int minY = 99999, maxY = -1;

    Log("TITLE CLEANUP: Walking control list...\n");
    void* p = pCtrl;
    while (p) {
        int type = CTRL_TYPE(p);
        if (type == D2WIN_BUTTON) {
            int y = CTRL_Y(p);
            Log("  BTN #%d: x=%d y=%d w=%d h=%d\n",
                btnCount, CTRL_X(p), y, CTRL_W(p), CTRL_H(p));
            if (!firstBtn) firstBtn = p;
            lastBtn = p;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
            btnCount++;
        }
        p = CTRL_NEXT(p);
    }

    Log("TITLE CLEANUP: Found %d buttons (Y range: %d-%d)\n", btnCount, minY, maxY);

    if (btnCount < 3) {
        Log("TITLE CLEANUP: Too few buttons, skipping cleanup\n");
        g_titleCleanupDone = 1;
        return;
    }

    /* Second pass: destroy vanilla buttons EXCEPT the 3 that
     * IsMainMenuActive() checks (y=490, 528, 568) and any
     * buttons at y=-5000 (our hidden dropdown buttons) or y=9999. */
    p = *g_ppControlList;
    int removed = 0;
    while (p) {
        void* next = CTRL_NEXT(p);
        int type = CTRL_TYPE(p);
        if (type == D2WIN_BUTTON) {
            int y = CTRL_Y(p);
            BOOL keep = (y == 490 || y == 528 || y == 568 ||
                         y <= -5000 || y >= 9999);
            if (!keep) {
                Log("  REMOVING button at y=%d\n", y);
                void* pTemp = p;
                fnControlDestroy(&pTemp);
                removed++;
            } else {
                Log("  KEEPING button at y=%d\n", y);
            }
        }
        p = next;
    }

    Log("TITLE CLEANUP: Removed %d buttons, kept 2\n", removed);
    g_titleCleanupDone = 1;
}

/* Player detection */
typedef void* (__stdcall *GetPlayer_t)(void);
static GetPlayer_t fnGetPlayer = NULL;

/* Mouse position (read from D2Client memory) */
static int* pMouseX = NULL;
static int* pMouseY = NULL;

/* D2Client SendGamePacket function pointer */
typedef void (__fastcall *SendPacket_t)(DWORD dwLen, BYTE* pPacket);
static DWORD g_sendPacketAddr = 0;  /* D2Client + 0x143E0 */

/* D2Common skill functions (ordinals) */
typedef void* (__stdcall *GetUnitSkill_t)(void* pUnit);
static GetUnitSkill_t fnGetRightSkill = NULL;  /* D2Common @10322 */
static GetUnitSkill_t fnGetLeftSkill  = NULL;  /* D2Common @10321 */

/* D2Client base address for reading skill memory directly */
static DWORD g_d2clientBase = 0;

/* Forward declare g_gameHwnd (defined later with other globals) */
static HWND g_gameHwnd;

/* Packet logging for debugging */
static BOOL g_packetLogEnabled = FALSE;
static DWORD g_packetLogStart = 0;
#define PACKET_LOG_DURATION 30000 /* log for 30 seconds */

/* Send a raw game packet to the server */
static void __declspec(noinline) SendGamePacket(DWORD len, BYTE* packet) {
    if (!g_sendPacketAddr) return;

    /* Log packet if logging enabled */
    if (g_packetLogEnabled && len >= 1 && len <= 256) {
        char hex[768];
        int pos = 0;
        for (DWORD i = 0; i < len && i < 32 && pos < 700; i++)
            pos += sprintf(hex + pos, "%02X ", packet[i]);
        hex[pos] = 0;
        Log("PKT OUT [0x%02X] len=%d: %s\n", packet[0], len, hex);
    }

    /* D2Client SendPacket: length in EDI, packet pointer pushed */
    __asm {
        push edi
        mov edi, len
        push packet
        call g_sendPacketAddr
        pop edi
    }
}

/* Inline hook on D2Client's SendPacket to log ALL outgoing packets.
 * D2Client+0x143E0 is __fastcall: EDI=length, [ESP+4]=packet */
static BYTE g_sendPatchBackup[5] = {0};
static DWORD g_sendPatchTrampoline = 0;
static BOOL g_sendHooked = FALSE;

static void __cdecl LogPacketFromHook(BYTE* packet, DWORD len) {
    if (!g_packetLogEnabled) return;
    if (GetTickCount() - g_packetLogStart > PACKET_LOG_DURATION) {
        g_packetLogEnabled = FALSE;
        Log("PACKET LOG: disabled (timeout)\n");
        return;
    }
    if (!packet || len < 1 || len > 512) return;
    __try {
        char hex[200];
        int pos = 0;
        for (DWORD i = 0; i < len && i < 40 && pos < 190; i++)
            pos += sprintf(hex + pos, "%02X ", packet[i]);
        hex[pos] = 0;
        Log("PKT [0x%02X] len=%d: %s\n", packet[0], len, hex);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void __declspec(naked) HookedSendPacket(void) {
    /* EDI = length, [ESP+4] = packet pointer (caller pushed it) */
    __asm {
        /* Save all registers */
        pushad
        /* Get packet from stack: 8 regs * 4 = 32, + return addr = 36, + packet = 40 */
        mov eax, [esp+36]  /* packet pointer */
        push edi           /* length */
        push eax           /* packet */
        call LogPacketFromHook
        add esp, 8
        popad
        /* Execute original first 5 bytes (saved in trampoline) */
        jmp g_sendPatchTrampoline
    }
}

/* Original D2Net SendPacket for trampoline */
static ClientSend_t g_origClientSend = NULL;

/* Last seen waypoint GUID — captured passively from normal gameplay */
static DWORD g_lastWaypointGUID = 0;

static int __stdcall HookedClientSend(int unused, const unsigned char* buf, int size) {
    /* Passively capture waypoint GUID from interact packets (0x13 type=2) */
    if (buf && size == 9 && buf[0] == 0x13 && buf[1] == 0x02) {
        g_lastWaypointGUID = *(DWORD*)&buf[5];
        Log("Captured waypoint GUID: %d (0x%X)\n", g_lastWaypointGUID, g_lastWaypointGUID);
    }

    /* Log the packet if logging enabled */
    if (g_packetLogEnabled && buf && size >= 1 && size <= 512) {
        __try {
            char hex[200];
            int pos = 0;
            for (int i = 0; i < size && i < 40 && pos < 190; i++)
                pos += sprintf(hex + pos, "%02X ", buf[i]);
            hex[pos] = 0;
            Log("PKT [0x%02X] len=%d: %s\n", buf[0], size, hex);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    /* Call original */
    return g_origClientSend(unused, buf, size);
}

static void InstallSendPacketHook(void) {
    if (g_sendHooked || !fnClientSend) return;

    /* Hook D2Net ordinal 10005 — the REAL client→server send function.
     * This is __stdcall so we can safely wrap it with a C function. */
    HMODULE hD2Net = GetModuleHandleA("D2Net.dll");
    if (!hD2Net) return;
    FARPROC fnOrig = GetProcAddress(hD2Net, (LPCSTR)10005);
    if (!fnOrig) return;

    /* Save original and patch the IAT/function pointer */
    g_origClientSend = (ClientSend_t)fnOrig;

    /* Inline hook: overwrite first 5 bytes with JMP to our wrapper */
    DWORD addr = (DWORD)fnOrig;
    g_sendPatchTrampoline = (DWORD)VirtualAlloc(NULL, 16, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!g_sendPatchTrampoline) return;

    /* Trampoline: original 5 bytes + JMP back to original+5 */
    memcpy((void*)g_sendPatchTrampoline, (void*)addr, 5);
    memcpy(g_sendPatchBackup, (void*)addr, 5);
    BYTE* tramp = (BYTE*)g_sendPatchTrampoline;
    tramp[5] = 0xE9;
    *(DWORD*)(tramp + 6) = (addr + 5) - (g_sendPatchTrampoline + 10);
    g_origClientSend = (ClientSend_t)(void*)g_sendPatchTrampoline;

    /* Patch original: JMP to our hook */
    DWORD oldProt;
    VirtualProtect((void*)addr, 5, PAGE_EXECUTE_READWRITE, &oldProt);
    BYTE* orig = (BYTE*)addr;
    orig[0] = 0xE9;
    *(DWORD*)(orig + 1) = (DWORD)HookedClientSend - (addr + 5);
    VirtualProtect((void*)addr, 5, oldProt, &oldProt);

    /* Update fnClientSend to use our hook (so our own sends also get logged) */
    fnClientSend = HookedClientSend;

    g_sendHooked = TRUE;
    Log("D2Net SendPacket hook installed at %08X, trampoline at %08X\n", addr, g_sendPatchTrampoline);
}

/* Get the skill ID from a D2SkillStrc pointer */
