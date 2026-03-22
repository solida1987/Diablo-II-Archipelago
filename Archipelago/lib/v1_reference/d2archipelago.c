/*
 * D2Archipelago.dll - Diablo II 1.10f Archipelago Mod (Core Plugin)
 *
 * Features:
 * - Kill tracking via monster hash table scanning
 * - Area change detection (6+ simultaneous disappearances)
 * - Check/reward system (kill milestones → skill points, gold)
 * - In-game top bar with info panels
 * - Persistent state saved to file
 *
 * Loaded by ddraw_proxy.dll via LoadLibrary.
 * Hooks into D2Client's UI render loop for drawing.
 */
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#include "d2_addresses.h"
#include "d2_hooks.h"
#include "d2_gameapi.h"

/* ========================================================================
 * Logging
 * ======================================================================== */
static FILE* g_log = NULL;
static void Log(const char* fmt, ...) {
    if (!g_log) {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        char* slash = strrchr(path, '\\');
        if (slash) strcpy(slash + 1, "d2archipelago.log");
        g_log = fopen(path, "w");
        if (!g_log) return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fflush(g_log);
}

/* ========================================================================
 * Safe memory reads (SEH for invalid addresses)
 * ======================================================================== */
static DWORD SafeRead(DWORD addr) {
    DWORD val = 0;
    __try { val = *(DWORD*)addr; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return val;
}

/* ========================================================================
 * Kill tracking
 * ======================================================================== */
#define MAX_TRACKED 4096
typedef struct {
    DWORD unitId;
    DWORD txtId;
    BOOL  gone;
    DWORD mode;
    DWORD prevMode;
    int   seenCount;
    int   missCount;
    BOOL  wasInCombat;
} TrackedUnit;

static TrackedUnit g_tracked[MAX_TRACKED];
static int g_trackedCount = 0;
static volatile BOOL g_running = TRUE;
static CRITICAL_SECTION g_cs;

#define MAX_MONSTER_TYPES 700
static int g_killsByType[MAX_MONSTER_TYPES];
static int g_totalKills = 0;

/* ========================================================================
 * Quest definitions
 * ======================================================================== */
typedef struct {
    int txtId;
    int required;
    const char* name;
} QuestReq;

typedef struct {
    const char* areaName;
    int numReqs;
    QuestReq reqs[5];
    BOOL completed;
} AreaQuest;

/* Kill requirements per area (txt IDs found via in-game scanning):
 * txt=5  = Zombie
 * txt=19 = Carver (Fallen variant)
 * txt=63 = Quill Rat */
#define NUM_AREAS 3
static AreaQuest g_quests[NUM_AREAS] = {
    { "Blood Moor", 3, {
        {  5, 10, "Zombie" },
        { 19,  8, "Carver" },
        { 63,  5, "Quill Rat" },
    }, FALSE },
    { "Cold Plains", 3, {
        { 19, 15, "Carver" },
        {  5, 15, "Zombie" },
        { 63, 10, "Quill Rat" },
    }, FALSE },
    { "Stony Field", 2, {
        { 19, 20, "Carver" },
        {  5, 20, "Zombie" },
    }, FALSE },
};

static void CheckQuestCompletion(void) {
    for (int i = 0; i < NUM_AREAS; i++) {
        if (g_quests[i].completed) continue;
        BOOL allDone = TRUE;
        for (int j = 0; j < g_quests[i].numReqs; j++) {
            int tid = g_quests[i].reqs[j].txtId;
            if (g_killsByType[tid] < g_quests[i].reqs[j].required) {
                allDone = FALSE;
                break;
            }
        }
        if (allDone) {
            g_quests[i].completed = TRUE;
            Log("QUEST COMPLETE: %s\n", g_quests[i].areaName);
        }
    }
}

/* ========================================================================
 * Monster scanning - hash table at D2CLIENT_BASE + MONSTER_HASH_OFF
 * 128 buckets, linked list via unit+UNIT_NEXT_OFF
 * Contains combat monsters in current area
 * ======================================================================== */

/* Town area IDs - never count kills in town */
static BOOL IsTownArea(DWORD areaId) {
    return (areaId == 1 || areaId == 40 || areaId == 75 ||
            areaId == 103 || areaId == 109);
}

static void ScanMonsters(void) {
    static int scanNum = 0;
    scanNum++;

    /* Skip tracking entirely in town areas */
    DWORD currentArea = CURRENT_AREA_ID;
    if (IsTownArea(currentArea)) {
        return;
    }

    EnterCriticalSection(&g_cs);

    BOOL seen[MAX_TRACKED];
    memset(seen, 0, sizeof(seen));

    int aliveCount = 0;

    /* Walk the monster hash table (128 buckets) */
    for (int bucket = 0; bucket < HASH_TABLE_SIZE; bucket++) {
        DWORD unit = SafeRead(MONSTER_HASH_TABLE + bucket * 4);
        int chain = 0;
        while (unit && chain++ < 50) {
            DWORD type = SafeRead(unit + UNIT_TYPE_OFF);

            if (type == 1) { /* monster */
                DWORD txt  = SafeRead(unit + UNIT_TXTID_OFF);
                DWORD id   = SafeRead(unit + UNIT_UNITID_OFF);
                DWORD mode = SafeRead(unit + UNIT_MODE_OFF);

                if (id != 0 && txt < MAX_MONSTER_TYPES) {
                    aliveCount++;

                    /* Find or create tracking entry */
                    int found = -1;
                    for (int t = 0; t < g_trackedCount; t++) {
                        if (g_tracked[t].unitId == id) { found = t; break; }
                    }

                    if (found == -1 && g_trackedCount < MAX_TRACKED) {
                        found = g_trackedCount++;
                        g_tracked[found].unitId = id;
                        g_tracked[found].txtId = txt;
                        g_tracked[found].gone = FALSE;
                        g_tracked[found].mode = mode;
                        g_tracked[found].prevMode = mode;
                        g_tracked[found].seenCount = 0;
                        g_tracked[found].missCount = 0;
                        g_tracked[found].wasInCombat = FALSE;

                        if (scanNum > 20) {
                            Log("NEW: txt=%d id=%08X mode=%d\n", txt, id, mode);
                        }
                    }

                    if (found >= 0) {
                        seen[found] = TRUE;
                        g_tracked[found].seenCount++;
                        g_tracked[found].missCount = 0;
                        g_tracked[found].prevMode = g_tracked[found].mode;
                        g_tracked[found].mode = mode;

                        /* Track if unit was ever in combat (attack modes) */
                        if (mode == MODE_ATTACK1 || mode == MODE_ATTACK2 ||
                            mode == MODE_WALK || mode == MODE_DEATH || mode == MODE_DEAD) {
                            g_tracked[found].wasInCombat = TRUE;
                        }
                    }
                }
            }
            unit = SafeRead(unit + UNIT_NEXT_OFF);
        }
    }

    /* ---- Disappear detection with area-change filter ---- */
    int goneThisScan = 0;
    for (int t = 0; t < g_trackedCount; t++) {
        if (g_tracked[t].gone) continue;
        if (!seen[t]) {
            g_tracked[t].missCount++;
            if (g_tracked[t].missCount >= 3 && g_tracked[t].seenCount >= 2) {
                goneThisScan++;
            }
        }
    }

    /* If 6+ units disappear at once, it's an area transition, not kills */
    BOOL isAreaChange = (goneThisScan >= 6);
    if (isAreaChange && goneThisScan > 0) {
        Log("AREA CHANGE: %d units gone at once (not counting as kills)\n", goneThisScan);
    }

    /* Mark gone and count kills */
    for (int t = 0; t < g_trackedCount; t++) {
        if (g_tracked[t].gone) continue;
        if (!seen[t] && g_tracked[t].missCount >= 3) {
            DWORD txt = g_tracked[t].txtId;

            if (g_tracked[t].seenCount < 2) {
                g_tracked[t].gone = TRUE; /* Barely visible, ignore */
            } else if (isAreaChange) {
                g_tracked[t].gone = TRUE; /* Area transition, not a kill */
            } else if (!g_tracked[t].wasInCombat) {
                g_tracked[t].gone = TRUE; /* NPC or non-hostile, ignore */
            } else {
                /* Real kill! */
                g_tracked[t].gone = TRUE;
                if (txt < MAX_MONSTER_TYPES) {
                    g_killsByType[txt]++;
                }
                g_totalKills++;
                Log("KILL #%d: txt=%d id=%08X (seen=%d)\n",
                    g_totalKills, txt, g_tracked[t].unitId,
                    g_tracked[t].seenCount);
            }
        }
    }

    /* Compact tracked array periodically */
    if (g_trackedCount > MAX_TRACKED / 2) {
        int write = 0;
        for (int read = 0; read < g_trackedCount; read++) {
            if (!g_tracked[read].gone) {
                if (write != read)
                    g_tracked[write] = g_tracked[read];
                write++;
            }
        }
        if (write < g_trackedCount) {
            g_trackedCount = write;
        }
    }

    CheckQuestCompletion();

    /* Periodic status */
    if (scanNum % 100 == 0) {
        Log("SCAN #%d: kills=%d tracked=%d alive=%d area=%d\n",
            scanNum, g_totalKills, g_trackedCount, aliveCount,
            CURRENT_AREA_ID);
    }

    LeaveCriticalSection(&g_cs);
}

/* ========================================================================
 * Save/load kill state
 * ======================================================================== */
static void SaveKillState(void) {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) strcpy(slash + 1, "d2arch_kills.dat");
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(&g_totalKills, sizeof(int), 1, f);
        fwrite(g_killsByType, sizeof(int), MAX_MONSTER_TYPES, f);
        for (int i = 0; i < NUM_AREAS; i++)
            fwrite(&g_quests[i].completed, sizeof(BOOL), 1, f);
        fclose(f);
    }
}

static void LoadKillState(void) {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) strcpy(slash + 1, "d2arch_kills.dat");
    FILE* f = fopen(path, "rb");
    if (f) {
        fread(&g_totalKills, sizeof(int), 1, f);
        fread(g_killsByType, sizeof(int), MAX_MONSTER_TYPES, f);
        for (int i = 0; i < NUM_AREAS; i++)
            fread(&g_quests[i].completed, sizeof(BOOL), 1, f);
        fclose(f);
        Log("Kill state loaded (total=%d)\n", g_totalKills);
    }
}

/* ========================================================================
 * Public accessors (for other DLLs to query kill data)
 * ======================================================================== */
__declspec(dllexport) int __stdcall D2Arch_GetTotalKills(void) {
    return g_totalKills;
}

__declspec(dllexport) int __stdcall D2Arch_GetKillsByType(int txtId) {
    if (txtId < 0 || txtId >= MAX_MONSTER_TYPES) return 0;
    return g_killsByType[txtId];
}

__declspec(dllexport) int __stdcall D2Arch_GetQuestCount(void) {
    return NUM_AREAS;
}

__declspec(dllexport) BOOL __stdcall D2Arch_IsQuestComplete(int index) {
    if (index < 0 || index >= NUM_AREAS) return FALSE;
    return g_quests[index].completed;
}

/* ========================================================================
 * UI Constants (shared between skill tree and top bar)
 * ======================================================================== */
#define BAR_HEIGHT 20
#define BAR_Y 0

/* ========================================================================
 * Custom Skill Tree System
 * ======================================================================== */
#define MAX_ARCH_SKILLS 210
#define MAX_ACTIVE_SLOTS 30
#define SKILLS_PER_PAGE 18  /* How many skill rows fit on screen */

typedef struct {
    char name[64];
    int  skillId;        /* Row index in Skills.txt (used as skill ID) */
    BOOL unlocked;       /* Available via check/starting */
    BOOL selected;       /* Player chose this for active use */
    int  pointsInvested; /* Skill points put in */
} ArchSkill;

/* Currently selected skill for info display (-1 = none) */
static int g_selectedSkillInfo = -1;

/* Internal skill point tracking (independent of D2's system) */
static int g_archSkillPoints = 3;   /* Start with 3 points to spend */
static int g_lastKnownLevel = 0;    /* 0 = not yet synced */

/* Pending skill grants — UI thread adds, scanner thread applies */
#define MAX_PENDING_SKILLS 32
typedef struct {
    int skillId;
    int level;
} PendingSkill;
static PendingSkill g_pendingSkills[MAX_PENDING_SKILLS];
static volatile int g_numPendingSkills = 0;

static ArchSkill g_archSkills[MAX_ARCH_SKILLS];
static int g_numArchSkills = 0;
static int g_maxActiveSlots = 6;   /* Start with 6, increase via checks */
static int g_skillTab = 0;         /* 0=Active, 1=Pool, 2=Locked */
static int g_skillScroll = 0;      /* Scroll offset for Tab 2/3 */
static BOOL g_skillTreeOpen = FALSE;
static BOOL g_skillStateLoaded = FALSE;

/* Count helpers */
static int CountSelected(void) {
    int n = 0;
    for (int i = 0; i < g_numArchSkills; i++)
        if (g_archSkills[i].selected) n++;
    return n;
}
static int CountUnlocked(void) {
    int n = 0;
    for (int i = 0; i < g_numArchSkills; i++)
        if (g_archSkills[i].unlocked) n++;
    return n;
}
static int CountLocked(void) {
    return g_numArchSkills - CountUnlocked();
}

/* Load skill data from d2arch_state.dat */
static void LoadSkillState(void) {
    if (g_skillStateLoaded) return;

    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) strcpy(slash + 1, "Archipelago\\d2arch_state.dat");

    FILE* f = fopen(path, "r");
    if (!f) {
        Log("[WARN] Cannot open d2arch_state.dat\n");
        return;
    }

    char line[256];
    int numStarting = 6;
    BOOL inAssignments = FALSE;

    while (fgets(line, sizeof(line), f)) {
        /* Remove newline */
        char* nl = strchr(line, '\n'); if (nl) *nl = '\0';
        char* cr = strchr(line, '\r'); if (cr) *cr = '\0';

        if (strncmp(line, "num_starting=", 13) == 0) {
            numStarting = atoi(line + 13);
        } else if (strcmp(line, "assignments=") == 0) {
            inAssignments = TRUE;
        } else if (inAssignments && line[0] && g_numArchSkills < MAX_ARCH_SKILLS) {
            /* Format: SkillName,classCode,isStarting,skillRowIndex */
            char* comma1 = strchr(line, ',');
            if (!comma1) continue;
            char* comma2 = strchr(comma1 + 1, ',');
            if (!comma2) continue;

            *comma1 = '\0';
            int isStarting = atoi(comma2 + 1);

            /* Parse skill row index (4th field, optional) */
            int skillRow = g_numArchSkills; /* fallback */
            char* comma3 = strchr(comma2 + 1, ',');
            if (comma3) {
                skillRow = atoi(comma3 + 1);
            }

            strncpy(g_archSkills[g_numArchSkills].name, line, 63);
            g_archSkills[g_numArchSkills].name[63] = '\0';
            g_archSkills[g_numArchSkills].skillId = skillRow;
            g_archSkills[g_numArchSkills].unlocked = isStarting;
            g_archSkills[g_numArchSkills].selected = isStarting;
            g_archSkills[g_numArchSkills].pointsInvested = 0;
            g_numArchSkills++;
        }
    }
    fclose(f);

    g_skillStateLoaded = TRUE;
    Log("Loaded %d skills (%d starting, %d unlocked)\n",
        g_numArchSkills, numStarting, CountUnlocked());
}

/* Render custom skill tree panel */
static void RenderCustomSkillTree(void) {
    if (!D2GFX_DrawRectangle || !D2WIN_DrawText || !D2WIN_SetFont)
        return;

    /* Skill tree panel position.
     * D2 skill tree panel in 640x480: x=320..640, y=0..432
     * In 800x600: x=480..800, y=0..532 (same panel, offset by 160,120)
     * We position our panel to exactly overlay the vanilla tree area. */
    int stScreenW = 640, stScreenH = 480;
    {
        HMODULE hCli2 = GetModuleHandleA("D2Client.dll");
        if (hCli2) {
            DWORD sw = SafeRead((DWORD)hCli2 + RESOLUTION_X_OFF);
            if (sw == 800) { stScreenW = 800; stScreenH = 600; }
        }
    }
    /* Panel matches vanilla tree position exactly */
    int px = stScreenW / 2;           /* 320 at 640, 400 at 800 */
    int py = 0;                        /* Start from very top */
    int pw = stScreenW - px;           /* Fill to right edge */
    int ph = stScreenH - 48;           /* Leave room for belt/action bar at bottom */

    /* ---- Full dark background covering vanilla tree ---- */
    D2GFX_DrawRectangle(px, py, px + pw, py + ph, 0, 5);
    D2GFX_DrawRectangle(px, py, px + pw, py + ph, 0, 5);
    D2GFX_DrawRectangle(px, py, px + pw, py + ph, 0, 5);
    D2GFX_DrawRectangle(px, py, px + pw, py + ph, 0, 5);

    /* ---- Tab bar with colored background ---- */
    int tabW = pw / 3;
    int tabY = py;
    int tabH = 22;
    const char* tabNames[] = {"Active", "Pool", "Locked"};
    int mx = GetMouseX(), my = GetMouseY();
    BOOL lDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    static BOOL wasLDown = FALSE;
    BOOL clicked = (lDown && !wasLDown);
    wasLDown = lDown;

    /* Tab bar background - darker blue/grey tint */
    D2GFX_DrawRectangle(px, tabY, px + pw, tabY + tabH, 4, 5);
    D2GFX_DrawRectangle(px, tabY, px + pw, tabY + tabH, 4, 5);

    D2WIN_SetFont(D2FONT_16);
    for (int t = 0; t < 3; t++) {
        int tx1 = px + t * tabW;
        int tx2 = tx1 + tabW;
        BOOL hover = (mx >= tx1 && mx < tx2 && my >= tabY && my < tabY + tabH);
        BOOL active = (g_skillTab == t);

        /* Active tab = brighter background */
        if (active) {
            D2GFX_DrawRectangle(tx1, tabY, tx2, tabY + tabH, 7, 5);
            D2GFX_DrawRectangle(tx1, tabY, tx2, tabY + tabH, 7, 5);
        } else if (hover) {
            D2GFX_DrawRectangle(tx1, tabY, tx2, tabY + tabH, 5, 5);
        }

        wchar_t wname[32];
        mbstowcs(wname, tabNames[t], 32);
        D2WIN_DrawText(wname, tx1 + 10, tabY + tabH - 4,
            active ? D2COLOR_GOLD : D2COLOR_GREY, 0);

        if (hover && clicked) g_skillTab = t;
    }

    /* ---- Separator line between tabs and content ---- */
    D2GFX_DrawRectangle(px, tabY + tabH, px + pw, tabY + tabH + 2, 7, 5);

    /* ---- Content area (well below tab bar + separator) ---- */
    int contentY = tabY + tabH + 20;
    int contentH = ph - tabH - 20;
    int rowH = 14;
    int maxRows = contentH / rowH;

    D2WIN_SetFont(D2FONT_8);

    if (g_skillTab == 0) {
        /* ======== TAB 1: ACTIVE SKILLS ======== */
        int selected = CountSelected();
        wchar_t header[64];
        swprintf(header, 64, L"Active Skills: %d/%d", selected, g_maxActiveSlots);
        D2WIN_DrawText(header, px + 8, contentY, D2COLOR_GOLD, 0);
        contentY += rowH + 2;

        /* Track skill points internally.
         * Read level by scanning D2Common ordinals to find the right one.
         * For now, just use our internal counter — skill points come from
         * the check/reward system, not from leveling. */
        int skillPts = g_archSkillPoints;

        int row = 0;
        for (int i = 0; i < g_numArchSkills && row < maxRows - 2; i++) {
            if (!g_archSkills[i].selected) continue;

            int ry = contentY + row * rowH;
            BOOL rowHover = (mx >= px + 4 && mx < px + pw - 4 &&
                            my >= ry - rowH + 2 && my < ry + 2);

            /* Skill name */
            wchar_t wname[80];
            swprintf(wname, 80, L"%hs", g_archSkills[i].name);
            D2WIN_DrawText(wname, px + 10, ry,
                rowHover ? D2COLOR_WHITE : D2COLOR_LIGHTGREEN, 0);

            /* Points display */
            wchar_t pts[32];
            swprintf(pts, 32, L"[%d]", g_archSkills[i].pointsInvested);
            D2WIN_DrawText(pts, px + pw - 60, ry, D2COLOR_GOLD, 0);

            /* + button */
            int btnX = px + pw - 30;
            BOOL btnHover = (mx >= btnX && mx < btnX + 14 &&
                           my >= ry - rowH + 2 && my < ry + 2);
            D2WIN_DrawText(L"+", btnX, ry,
                btnHover ? D2COLOR_GREEN : D2COLOR_GREY, 0);

            if (btnHover && clicked && g_archSkillPoints > 0) {
                g_archSkills[i].pointsInvested++;
                g_archSkillPoints--;
                skillPts = g_archSkillPoints;

                /* Queue skill grant — will be applied from scanner thread */
                if (g_numPendingSkills < MAX_PENDING_SKILLS) {
                    g_pendingSkills[g_numPendingSkills].skillId = g_archSkills[i].skillId;
                    g_pendingSkills[g_numPendingSkills].level = g_archSkills[i].pointsInvested;
                    g_numPendingSkills++;
                    Log("SKILL QUEUED: id=%d lvl=%d\n",
                        g_archSkills[i].skillId, g_archSkills[i].pointsInvested);
                }

                Log("SKILL: +1 in %s (id=%d, now %d, remaining=%d)\n",
                    g_archSkills[i].name, g_archSkills[i].skillId,
                    g_archSkills[i].pointsInvested, g_archSkillPoints);
            }

            /* Click on skill name = show info */
            if (rowHover && clicked && !btnHover) {
                g_selectedSkillInfo = (g_selectedSkillInfo == i) ? -1 : i;
            }

            row++;
        }

        /* Show available points at bottom */
        wchar_t ptsLine[64];
        swprintf(ptsLine, 64, L"Skill Points Available: %d", skillPts);
        D2WIN_DrawText(ptsLine, px + 8, py + ph - 18, D2COLOR_YELLOW, 0);

        /* Skill info panel — shown when a skill is clicked */
        if (g_selectedSkillInfo >= 0 && g_selectedSkillInfo < g_numArchSkills) {
            ArchSkill* sk = &g_archSkills[g_selectedSkillInfo];
            /* Draw info box on the LEFT side of screen */
            int ix = 10, iy = 60, iw = px - 20, ih = 160;
            D2GFX_DrawRectangle(ix, iy, ix + iw, iy + ih, 0, 5);
            D2GFX_DrawRectangle(ix, iy, ix + iw, iy + ih, 0, 5);
            D2GFX_DrawRectangle(ix, iy, ix + iw, iy + ih, 0, 5);

            D2WIN_SetFont(D2FONT_16);
            wchar_t wname[80];
            swprintf(wname, 80, L"%hs", sk->name);
            D2WIN_DrawText(wname, ix + 10, iy + 22, D2COLOR_GOLD, 0);

            D2WIN_SetFont(D2FONT_8);
            int infoY = iy + 40;
            wchar_t line[128];

            swprintf(line, 128, L"Skill ID: %d", sk->skillId);
            D2WIN_DrawText(line, ix + 10, infoY, D2COLOR_WHITE, 0);
            infoY += 14;

            swprintf(line, 128, L"Points Invested: %d", sk->pointsInvested);
            D2WIN_DrawText(line, ix + 10, infoY, D2COLOR_WHITE, 0);
            infoY += 14;

            swprintf(line, 128, L"Status: %hs",
                sk->unlocked ? "Unlocked" : "Locked");
            D2WIN_DrawText(line, ix + 10, infoY,
                sk->unlocked ? D2COLOR_GREEN : D2COLOR_RED, 0);
            infoY += 14;

            swprintf(line, 128, L"Selected: %hs",
                sk->selected ? "Yes (Active)" : "No");
            D2WIN_DrawText(line, ix + 10, infoY, D2COLOR_WHITE, 0);
            infoY += 20;

            D2WIN_DrawText(L"(Click skill name again to close)",
                ix + 10, infoY, D2COLOR_GREY, 0);

            D2WIN_SetFont(D2FONT_8);
        }

    } else if (g_skillTab == 1) {
        /* ======== TAB 2: SKILL POOL (unlocked) ======== */
        int selected = CountSelected();
        int unlocked = CountUnlocked();
        wchar_t header[64];
        swprintf(header, 64, L"Unlocked: %d  Selected: %d/%d",
            unlocked, selected, g_maxActiveSlots);
        D2WIN_DrawText(header, px + 8, contentY, D2COLOR_GOLD, 0);
        contentY += rowH + 2;

        /* Build list of unlocked skills */
        int shown = 0, idx = 0;
        for (int i = 0; i < g_numArchSkills; i++) {
            if (!g_archSkills[i].unlocked) continue;
            if (idx < g_skillScroll) { idx++; continue; }
            if (shown >= maxRows - 3) break;

            int ry = contentY + shown * rowH;
            BOOL rowHover = (mx >= px + 4 && mx < px + pw - 4 &&
                            my >= ry - rowH + 2 && my < ry + 2);

            /* Selected marker */
            wchar_t marker[4] = L"  ";
            if (g_archSkills[i].selected) wcscpy(marker, L"> ");

            /* Skill name */
            wchar_t wname[80];
            swprintf(wname, 80, L"%ls%hs", marker, g_archSkills[i].name);

            int color = g_archSkills[i].selected ? D2COLOR_GREEN :
                       (rowHover ? D2COLOR_WHITE : D2COLOR_GREY);
            D2WIN_DrawText(wname, px + 10, ry, color, 0);

            /* Click to toggle selection */
            if (rowHover && clicked) {
                if (g_archSkills[i].selected) {
                    g_archSkills[i].selected = FALSE;
                    g_archSkills[i].pointsInvested = 0;
                    Log("SKILL: Deselected %s\n", g_archSkills[i].name);
                } else if (selected < g_maxActiveSlots) {
                    g_archSkills[i].selected = TRUE;
                    Log("SKILL: Selected %s\n", g_archSkills[i].name);
                }
            }

            shown++;
            idx++;
        }

        /* Scroll buttons */
        int scrollY = py + ph - 18;
        BOOL upHover = (mx >= px + pw - 60 && mx < px + pw - 40 &&
                       my >= scrollY - 12 && my < scrollY);
        BOOL downHover = (mx >= px + pw - 30 && mx < px + pw - 10 &&
                         my >= scrollY - 12 && my < scrollY);
        D2WIN_DrawText(L"[Up]", px + pw - 60, scrollY,
            upHover ? D2COLOR_WHITE : D2COLOR_GREY, 0);
        D2WIN_DrawText(L"[Dn]", px + pw - 30, scrollY,
            downHover ? D2COLOR_WHITE : D2COLOR_GREY, 0);

        if (upHover && clicked && g_skillScroll > 0) g_skillScroll--;
        if (downHover && clicked) g_skillScroll++;

    } else if (g_skillTab == 2) {
        /* ======== TAB 3: LOCKED SKILLS ======== */
        int locked = CountLocked();
        wchar_t header[64];
        swprintf(header, 64, L"Locked Skills: %d", locked);
        D2WIN_DrawText(header, px + 8, contentY, D2COLOR_GOLD, 0);
        contentY += rowH + 2;

        int shown = 0, idx = 0;
        for (int i = 0; i < g_numArchSkills; i++) {
            if (g_archSkills[i].unlocked) continue;
            if (idx < g_skillScroll) { idx++; continue; }
            if (shown >= maxRows - 3) break;

            int ry = contentY + shown * rowH;
            wchar_t wname[80];
            swprintf(wname, 80, L"  %hs", g_archSkills[i].name);
            D2WIN_DrawText(wname, px + 10, ry, D2COLOR_RED, 0);

            shown++;
            idx++;
        }

        /* Scroll buttons */
        int scrollY = py + ph - 18;
        BOOL upHover = (mx >= px + pw - 60 && mx < px + pw - 40 &&
                       my >= scrollY - 12 && my < scrollY);
        BOOL downHover = (mx >= px + pw - 30 && mx < px + pw - 10 &&
                         my >= scrollY - 12 && my < scrollY);
        D2WIN_DrawText(L"[Up]", px + pw - 60, scrollY,
            upHover ? D2COLOR_WHITE : D2COLOR_GREY, 0);
        D2WIN_DrawText(L"[Dn]", px + pw - 30, scrollY,
            downHover ? D2COLOR_WHITE : D2COLOR_GREY, 0);

        if (upHover && clicked && g_skillScroll > 0) g_skillScroll--;
        if (downHover && clicked) g_skillScroll++;
    }

    /* Reset scroll when switching tabs */
    static int lastTab = 0;
    if (g_skillTab != lastTab) { g_skillScroll = 0; lastTab = g_skillTab; }
}

/* Skill tree is rendered from DrawTopBar via F8 hotkey toggle.
 * No separate hook needed — drawn over the game UI from DrawGameUI hook. */

/* ========================================================================
 * Check/Reward System
 * ======================================================================== */
typedef enum {
    REWARD_SKILL_POINT,
    REWARD_GOLD,
    REWARD_LEVEL_UP,
} RewardType;

typedef struct {
    const char* name;
    int         requiredKills;  /* total kills needed */
    RewardType  reward;
    int         rewardAmount;
    BOOL        completed;
} Check;

#define NUM_CHECKS 8
static Check g_checks[NUM_CHECKS] = {
    { "First Blood",       1,    REWARD_GOLD,        500,  FALSE },
    { "Monster Slayer",    10,   REWARD_SKILL_POINT,  1,   FALSE },
    { "Warrior",           25,   REWARD_GOLD,        1000, FALSE },
    { "Veteran",           50,   REWARD_SKILL_POINT,  1,   FALSE },
    { "Champion",          100,  REWARD_GOLD,        2500, FALSE },
    { "Hero",              200,  REWARD_SKILL_POINT,  1,   FALSE },
    { "Legend",            500,  REWARD_LEVEL_UP,     1,   FALSE },
    { "Godlike",           1000, REWARD_SKILL_POINT,  2,   FALSE },
};

static int g_checksCompleted = 0;
static char g_lastRewardMsg[128] = "";
static DWORD g_rewardMsgTime = 0;

static void ApplyReward(Check* chk) {
    DWORD player = PLAYER_UNIT_PTR;
    if (!player || !D2COMMON_GetUnitStat) return;

    switch (chk->reward) {
    case REWARD_SKILL_POINT: {
        g_archSkillPoints += chk->rewardAmount;
        Log("REWARD: +%d Skill Point(s) [%s] (total=%d)\n",
            chk->rewardAmount, chk->name, g_archSkillPoints);
        break;
    }
    case REWARD_GOLD: {
        Log("REWARD: +%d Gold [%s]\n", chk->rewardAmount, chk->name);
        break;
    }
    case REWARD_LEVEL_UP: {
        Log("REWARD: +%d Level(s) [%s]\n", chk->rewardAmount, chk->name);
        break;
    }
    }

    snprintf(g_lastRewardMsg, sizeof(g_lastRewardMsg),
             "Check: %s! (+%d %s)",
             chk->name, chk->rewardAmount,
             chk->reward == REWARD_SKILL_POINT ? "Skill Points" :
             chk->reward == REWARD_GOLD ? "Gold" : "Level");
    g_rewardMsgTime = GetTickCount();
}

static void EvaluateChecks(void) {
    for (int i = 0; i < NUM_CHECKS; i++) {
        if (g_checks[i].completed) continue;
        if (g_totalKills >= g_checks[i].requiredKills) {
            g_checks[i].completed = TRUE;
            g_checksCompleted++;
            ApplyReward(&g_checks[i]);
            Log("CHECK COMPLETE: %s (kills=%d)\n", g_checks[i].name, g_totalKills);
        }
    }
}

/* ========================================================================
 * Top Bar UI — drawn in D2's render loop
 * ======================================================================== */
static BOOL g_apiReady = FALSE;
static BOOL g_hooksInstalled = FALSE;
static int g_activePanel = -1;  /* -1 = none, 0-4 = button index */

/* Mouse state */
static BOOL g_wasLButtonDown = FALSE;

/* Get mouse position using D2Client functions */
static int GetMouseX(void) {
    typedef DWORD (__stdcall *fn_t)(void);
    HMODULE h = GetModuleHandleA("D2Client.dll");
    if (!h) return 0;
    fn_t f = (fn_t)((DWORD)h + MOUSE_X_OFF);
    __try { return (int)f(); } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int GetMouseY(void) {
    typedef DWORD (__stdcall *fn_t)(void);
    HMODULE h = GetModuleHandleA("D2Client.dll");
    if (!h) return 0;
    fn_t f = (fn_t)((DWORD)h + MOUSE_Y_OFF);
    __try { return (int)f(); } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* Button definitions */
#define NUM_BUTTONS 5

static const char* g_buttonLabels[NUM_BUTTONS] = {
    "Missions", "Checks", "Skills", "Difficulty", "Settings"
};

/* Menu state */
static BOOL g_menuOpen = FALSE;
static int g_persistentHud = -1;  /* -1=none, 0=missions, 1=checks, 2=settings */

/* Check if mouse is over any of our UI elements */
static BOOL IsMouseOverOurUI(int mx, int my) {
    /* Menu button */
    if (mx >= 2 && mx < 52 && my >= 2 && my < 18) return TRUE;
    /* Menu panel */
    if (g_menuOpen && mx >= 2 && mx < 180 && my >= 18 && my < 120) return TRUE;
    /* Skill tree panel (right half) */
    if (g_skillTreeOpen) {
        int sw = 640;
        HMODULE h = GetModuleHandleA("D2Client.dll");
        if (h) { DWORD r = SafeRead((DWORD)h + RESOLUTION_X_OFF); if (r == 800) sw = 800; }
        if (mx >= sw / 2 && my >= 0) return TRUE;
    }
    return FALSE;
}

/* Draw the menu button, dropdown, and persistent HUD */
static void DrawTopBar(void) {
    if (!D2GFX_DrawRectangle || !D2WIN_DrawText || !D2WIN_SetFont)
        return;
    if (!PLAYER_UNIT_PTR)
        return;

    int mx = GetMouseX();
    int my = GetMouseY();
    BOOL lButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    BOOL clicked = (lButtonDown && !g_wasLButtonDown);
    g_wasLButtonDown = lButtonDown;

    /* ---- Small "Menu" button in top-left ---- */
    int mbx = 2, mby = 2, mbw = 50, mbh = 16;
    BOOL menuBtnHover = (mx >= mbx && mx < mbx + mbw && my >= mby && my < mby + mbh);

    D2GFX_DrawRectangle(mbx, mby, mbx + mbw, mby + mbh, 0, 5);
    D2GFX_DrawRectangle(mbx, mby, mbx + mbw, mby + mbh, 0, 5);
    D2WIN_SetFont(D2FONT_8);
    D2WIN_DrawText(L"Menu", mbx + 4, mby + mbh - 2,
        menuBtnHover ? D2COLOR_GOLD : D2COLOR_GREY, 0);

    if (menuBtnHover && clicked) {
        g_menuOpen = !g_menuOpen;
    }

    /* ---- Menu dropdown ---- */
    if (g_menuOpen) {
        int mpx = 2, mpy = mby + mbh + 2, mpw = 175, mph = 58;
        D2GFX_DrawRectangle(mpx, mpy, mpx + mpw, mpy + mph, 0, 5);
        D2GFX_DrawRectangle(mpx, mpy, mpx + mpw, mpy + mph, 0, 5);
        D2GFX_DrawRectangle(mpx, mpy, mpx + mpw, mpy + mph, 0, 5);

        D2WIN_SetFont(D2FONT_8);
        const char* items[] = {"Missions", "Checks", "Settings"};
        int itemY = mpy + 6;

        for (int mi = 0; mi < 3; mi++) {
            int iy = itemY + mi * 16;
            BOOL itemHover = (mx >= mpx + 4 && mx < mpx + mpw - 4 &&
                             my >= iy && my < iy + 14);

            BOOL isActive = (g_persistentHud == mi);
            wchar_t witem[64];
            if (mi == 1)
                swprintf(witem, 64, L"Checks %d/%d", g_checksCompleted, NUM_CHECKS);
            else
                mbstowcs(witem, items[mi], 64);

            D2WIN_DrawText(witem, mpx + 10, iy + 12,
                isActive ? D2COLOR_GREEN : (itemHover ? D2COLOR_GOLD : D2COLOR_WHITE), 0);

            if (itemHover && clicked) {
                /* Toggle persistent HUD for this item */
                g_persistentHud = isActive ? -1 : mi;
                g_menuOpen = FALSE; /* close menu */
            }
        }
    }

    /* ---- Persistent HUD (transparent, no box, stays on screen) ---- */
    if (g_persistentHud >= 0 && !g_menuOpen) {
        D2WIN_SetFont(D2FONT_8);
        int hy = 32;  /* Below menu button */

        if (g_persistentHud == 0) { /* Missions */
            for (int q = 0; q < NUM_AREAS; q++) {
                wchar_t line[128];
                swprintf(line, 128, L"%hs: %hs",
                    g_quests[q].areaName,
                    g_quests[q].completed ? "DONE" : "...");
                D2WIN_DrawText(line, 6, hy,
                    g_quests[q].completed ? D2COLOR_GREEN : D2COLOR_DARKGOLD, 0);
                hy += 12;
            }
        } else if (g_persistentHud == 1) { /* Checks */
            for (int c = 0; c < NUM_CHECKS; c++) {
                wchar_t line[128];
                int prog = g_totalKills > g_checks[c].requiredKills ?
                    g_checks[c].requiredKills : g_totalKills;
                swprintf(line, 128, L"%hs %d/%d",
                    g_checks[c].name, prog, g_checks[c].requiredKills);
                D2WIN_DrawText(line, 6, hy,
                    g_checks[c].completed ? D2COLOR_GREEN : D2COLOR_DARKGOLD, 0);
                hy += 12;
            }
        } else if (g_persistentHud == 2) { /* Settings */
            wchar_t line[128];
            swprintf(line, 128, L"Archipelago v0.2 | Kills: %d | Area: %d",
                g_totalKills, CURRENT_AREA_ID);
            D2WIN_DrawText(line, 6, hy, D2COLOR_DARKGOLD, 0);
        }
    }

    /* Keyboard shortcuts for skill tree */
    {
        /* T = toggle our skill tree */
        static BOOL wasTDown = FALSE;
        BOOL tDown = (GetAsyncKeyState('T') & 0x8000) != 0;
        if (tDown && !wasTDown) {
            g_skillTreeOpen = !g_skillTreeOpen;
            g_skillScroll = 0;
        }
        wasTDown = tDown;

        /* Red + button (fixed position, never shifts) = open skill tree */
        if (clicked) {
            int cmx = GetMouseX(), cmy = GetMouseY();
            if (cmx >= 568 && cmx <= 592 && cmy >= 568 && cmy <= 592) {
                g_skillTreeOpen = TRUE;
                g_skillScroll = 0;
            }
        }

        /* Escape = close skill tree (and info panels) */
        static BOOL wasEscDown = FALSE;
        BOOL escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        if (escDown && !wasEscDown) {
            if (g_skillTreeOpen) {
                g_skillTreeOpen = FALSE;
            }
            if (g_activePanel >= 0) {
                g_activePanel = -1;
            }
        }
        wasEscDown = escDown;
    }

    /* Draw custom skill tree if open */
    if (g_skillTreeOpen) {
        RenderCustomSkillTree();
    }

    /* Draw reward notification (fades after 5 seconds) */
    if (g_lastRewardMsg[0] && (GetTickCount() - g_rewardMsgTime) < 5000) {
        D2WIN_SetFont(D2FONT_16);
        wchar_t wmsg[128];
        mbstowcs(wmsg, g_lastRewardMsg, 128);
        D2WIN_DrawText(wmsg, 100, 60, D2COLOR_GOLD, 0);
    }

    /* ---- Click-blocking: prevent character movement when clicking our UI ---- */
    if (lButtonDown && IsMouseOverOurUI(mx, my)) {
        HMODULE hc = GetModuleHandleA("D2Client.dll");
        if (hc) {
            /* Write D2's mouse offset to (0,0) so click doesn't move character.
             * D2Client + 0x1119C8 = mouse offset POINT struct */
            DWORD mAddr = (DWORD)hc + 0x1119C8;
            DWORD oldProt;
            if (VirtualProtect((void*)mAddr, 8, PAGE_READWRITE, &oldProt)) {
                *(DWORD*)mAddr = 0;
                *(DWORD*)(mAddr + 4) = 0;
                VirtualProtect((void*)mAddr, 8, oldProt, &oldProt);
            }
        }
    }
}

/* Hook target: called from D2Client's DrawGameUI routine.
 * Uses a proper detour with executable trampoline buffer. */
static DWORD g_drawUIAddr = 0;

/* Trampoline: executable buffer containing original 5 bytes + JMP back.
 * Must be in executable memory (VirtualAlloc with EXECUTE). */
static BYTE* g_trampoline = NULL;

/* Wrapper that safely draws our UI */
static void SafeDrawTopBar(void) {
    __try {
        DrawTopBar();
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* Our hook: call original via trampoline, then draw our stuff on top */
static void __declspec(naked) DrawGameUIHook(void) {
    __asm {
        /* Call original function via trampoline */
        call g_trampoline

        /* Now draw our UI on top */
        pushad
        pushfd
        call SafeDrawTopBar
        popfd
        popad
        ret
    }
}

static void InstallDrawHook(void) {
    if (g_hooksInstalled) return;

    HMODULE hClient = GetModuleHandleA("D2Client.dll");
    if (!hClient) return;

    /* Initialize D2 API function pointers */
    if (!D2API_Init()) {
        Log("[WARN] D2API_Init failed - draw functions not available\n");
        return;
    }
    g_apiReady = TRUE;
    Log("D2 API initialized (DrawText=%p, DrawRect=%p)\n",
        D2WIN_DrawText, D2GFX_DrawRectangle);

    g_drawUIAddr = (DWORD)hClient + DRAW_GAME_UI_OFF;

    /* Log first 16 bytes for instruction analysis */
    Log("DrawGameUI at %08X, bytes:", g_drawUIAddr);
    for (int i = 0; i < 16; i++) Log(" %02X", ((BYTE*)g_drawUIAddr)[i]);
    Log("\n");

    /* Instruction analysis of DrawGameUI start:
     * 83 EC 48       = sub esp, 0x48   (3 bytes)
     * B9 xx xx xx xx = mov ecx, imm32  (5 bytes)
     * Total: 8 bytes needed for clean instruction boundary.
     * We must copy at least 8 bytes to the trampoline. */
    #define HOOK_COPY_LEN 8

    /* Allocate executable trampoline buffer:
     * [8 bytes: original instructions] [5 bytes: JMP back to original+8] */
    g_trampoline = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE,
                                        PAGE_EXECUTE_READWRITE);
    if (!g_trampoline) {
        Log("[ERROR] VirtualAlloc for trampoline failed\n");
        return;
    }

    /* Copy original 8 bytes to trampoline */
    memcpy(g_trampoline, (void*)g_drawUIAddr, HOOK_COPY_LEN);

    /* Add JMP back to original function + 8 */
    g_trampoline[HOOK_COPY_LEN] = 0xE9; /* JMP rel32 */
    *(DWORD*)(g_trampoline + HOOK_COPY_LEN + 1) =
        (g_drawUIAddr + HOOK_COPY_LEN) - ((DWORD)g_trampoline + HOOK_COPY_LEN + 5);

    /* Patch original: overwrite first 5 bytes with JMP, NOP remaining 3 */
    PatchJMP(g_drawUIAddr, (DWORD)DrawGameUIHook);
    PatchNOP(g_drawUIAddr + 5, HOOK_COPY_LEN - 5); /* NOP bytes 5-7 */

    g_hooksInstalled = TRUE;
    Log("DrawGameUI hook installed (trampoline at %p)\n", g_trampoline);
}

/* ========================================================================
 * Main thread - background scanner
 * ======================================================================== */
static DWORD WINAPI MainThread(LPVOID param) {
    (void)param;
    Log("=== Main thread started ===\n");

    /* Wait for D2Client.dll to be loaded */
    while (g_running && !GetModuleHandleA("D2Client.dll")) Sleep(200);
    if (!g_running) return 0;
    Log("D2Client at %08X\n", (DWORD)GetModuleHandleA("D2Client.dll"));

    /* Load skill state from d2arch_state.dat */
    LoadSkillState();

    /* Install drawing hooks */
    InstallDrawHook();

    /* Skill tree uses F8 hotkey, rendered from DrawGameUI hook */

    /* Wait for player unit to exist */
    while (g_running && !PLAYER_UNIT_PTR) Sleep(200);
    if (!g_running) return 0;
    Log("Player: %08X\n", PLAYER_UNIT_PTR);

    /* Start fresh - delete old kill state */
    {
        char kpath[MAX_PATH];
        GetModuleFileNameA(NULL, kpath, MAX_PATH);
        char* ks = strrchr(kpath, '\\');
        if (ks) strcpy(ks + 1, "d2arch_kills.dat");
        DeleteFileA(kpath);
        Log("Kill state reset (fresh start)\n");
    }

    /* Main scan loop - 20 scans/sec for responsive kill detection */
    int scanNum = 0;
    while (g_running) {
        __try {
            if (PLAYER_UNIT_PTR) {
                ScanMonsters();
                EvaluateChecks();
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}

        /* Save periodically (every ~25 seconds) */
        scanNum++;
        if (scanNum % 500 == 0) {
            SaveKillState();
        }

        Sleep(50);
    }

    SaveKillState();
    return 0;
}

/* ========================================================================
 * D2Mod plugin exports
 * ======================================================================== */
__declspec(dllexport) void __stdcall _Init(void* param) {
    (void)param;
    Log("D2Archipelago _Init (D2Mod plugin)\n");
}

__declspec(dllexport) void __stdcall _Release(void) {
    Log("D2Archipelago _Release\n");
    g_running = FALSE;
}

/* ========================================================================
 * DLL Entry
 * ======================================================================== */
static HANDLE g_thread = NULL;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InitializeCriticalSection(&g_cs);
        memset(g_killsByType, 0, sizeof(g_killsByType));
        memset(g_tracked, 0, sizeof(g_tracked));
        Log("=== D2Archipelago.dll loaded ===\n");

        /* Note: -direct -txt flags are provided via "Play Archipelago.bat"
         * which launches Game.exe with the required flags. */

        g_thread = CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_running = FALSE;
        if (g_thread) { WaitForSingleObject(g_thread, 3000); CloseHandle(g_thread); }
        Log("=== Unloaded. Kills: %d ===\n", g_totalKills);
        if (g_log) fclose(g_log);
        DeleteCriticalSection(&g_cs);
    }
    return TRUE;
}
