/*
 * d2_gameapi.h — Function pointers to D2 game functions.
 * Resolved at runtime via GetProcAddress with ordinal numbers.
 *
 * D2Common.dll uses ordinal exports (no names).
 * D2Client.dll uses a mix of ordinals and direct offsets.
 * D2Gfx.dll and D2Win.dll use ordinals for drawing.
 */
#ifndef D2_GAMEAPI_H
#define D2_GAMEAPI_H

#include <windows.h>
#include "d2_addresses.h"

/* ===== D2Common.dll Function Types ===== */

/* SetStat: Set a stat value on a unit's stat list */
/* Ordinal 10519 in D2Common.dll 1.10f */
/* __fastcall: pStatList in ECX, nStat in EDX, nValue on stack */
typedef void (__fastcall *D2COMMON_SetStat_t)(DWORD pStatList, DWORD nStat, int nValue, DWORD nLayer);

/* GetStat: Get a stat value from a unit */
/* Ordinal 10520 in D2Common.dll 1.10f */
typedef int (__fastcall *D2COMMON_GetStat_t)(DWORD pUnit, DWORD nStat, DWORD nLayer);

/* GetUnitStat: Alternative stat getter */
/* Ordinal 10264 */
typedef int (__stdcall *D2COMMON_GetUnitStat_t)(DWORD pUnit, DWORD nStat, DWORD nLayer);

/* ===== D2Win.dll Function Types (UI Drawing) ===== */

/* DrawText: Draw text string at position */
/* Ordinal 10117 */
typedef void (__fastcall *D2WIN_DrawText_t)(const wchar_t* text, int x, int y, DWORD color, DWORD unknown);

/* SetFont: Set the current font for text drawing */
/* Ordinal 10127 */
typedef DWORD (__fastcall *D2WIN_SetFont_t)(DWORD fontId);

/* GetTextWidth: Get pixel width of text string */
/* Ordinal 10121 */
typedef DWORD (__fastcall *D2WIN_GetTextWidth_t)(const wchar_t* text);

/* ===== D2Gfx.dll Function Types ===== */

/* DrawRectangle: Draw a filled rectangle */
/* Ordinal 10055 */
typedef void (__stdcall *D2GFX_DrawRectangle_t)(int x1, int y1, int x2, int y2, DWORD color, DWORD drawMode);

/* ===== D2Client.dll Function Types ===== */

/* PrintGameString: Show message in game chat area */
typedef void (__fastcall *D2CLIENT_PrintGameString_t)(const wchar_t* msg, DWORD color);

/* ===== D2 Font IDs ===== */
#define D2FONT_8        0
#define D2FONT_16       1
#define D2FONT_30       2
#define D2FONT_42       3
#define D2FONT_FORMAL10 4
#define D2FONT_FORMAL12 5
#define D2FONT_6        6
#define D2FONT_24       7
#define D2FONT_EXOCET10 9
#define D2FONT_EXOCET8  11
#define D2FONT_INGAMECHAT 13

/* ===== D2 Text Colors ===== */
#define D2COLOR_WHITE       0
#define D2COLOR_RED         1
#define D2COLOR_LIGHTGREEN  2
#define D2COLOR_BLUE        3
#define D2COLOR_DARKGOLD    4
#define D2COLOR_GREY        5
#define D2COLOR_BLACK       6
#define D2COLOR_GOLD        7
#define D2COLOR_ORANGE      8
#define D2COLOR_YELLOW      9
#define D2COLOR_DARKGREEN   10
#define D2COLOR_PURPLE      11
#define D2COLOR_GREEN       12

/* ===== D2Common Skill Functions ===== */

/* STATLIST_SetUnitStat: Set a stat on a unit (with layer for skill ID)
 * Ordinal 10517 — THE KEY FUNCTION for granting skills cross-class.
 * Use stat 97 (ITEM_NONCLASSSKILL) with layer=skillId, value=skillLevel.
 * This is how items like Enigma grant Teleport to all classes. */
typedef void (__stdcall *D2COMMON_SetUnitStat_t)(DWORD pUnit, int nStatId, int nValue, WORD nLayer);

/* SKILLS_AddSkill: Add a skill to unit's skill list
 * Ordinal 10952 */
typedef DWORD (__stdcall *D2COMMON_AddSkill_t)(DWORD pUnit, int nSkillId);

/* SKILLS_AssignSkill: Initialize a skill on unit
 * Ordinal 10953 */
typedef void (__stdcall *D2COMMON_AssignSkill_t)(DWORD pUnit, int nSkillId, int nSkillLevel, BOOL bRemove, const char* szFile, int nLine);

/* SKILLS_RefreshSkill: Refresh passive/aura state
 * Ordinal 10940 */
typedef void (__stdcall *D2COMMON_RefreshSkill_t)(DWORD pUnit, int nSkillId);

/* ===== Global Function Pointers ===== */
/* Call D2API_Init() once at DLL load to resolve these */

static D2COMMON_GetUnitStat_t   D2COMMON_GetUnitStat   = NULL;
static D2COMMON_SetUnitStat_t   D2COMMON_SetUnitStat   = NULL;
static D2COMMON_AddSkill_t      D2COMMON_AddSkill      = NULL;
static D2COMMON_AssignSkill_t   D2COMMON_AssignSkill   = NULL;
static D2COMMON_RefreshSkill_t  D2COMMON_RefreshSkill  = NULL;
static D2WIN_DrawText_t         D2WIN_DrawText          = NULL;
static D2WIN_SetFont_t          D2WIN_SetFont           = NULL;
static D2WIN_GetTextWidth_t     D2WIN_GetTextWidth      = NULL;
static D2GFX_DrawRectangle_t    D2GFX_DrawRectangle     = NULL;

static inline BOOL D2API_Init(void)
{
    HMODULE hCommon = GetModuleHandleA("D2Common.dll");
    HMODULE hWin    = GetModuleHandleA("D2Win.dll");
    HMODULE hGfx    = GetModuleHandleA("D2gfx.dll");
    HMODULE hClient = GetModuleHandleA("D2Client.dll");

    if (!hCommon || !hWin || !hGfx || !hClient)
        return FALSE;

    /* D2Common - ordinal exports (from D2MOO STATLIST functions) */
    /* 10519 = STATLIST_UnitGetStatValue(pUnit, statId, layer) */
    D2COMMON_GetUnitStat  = (D2COMMON_GetUnitStat_t) GetProcAddress(hCommon, (LPCSTR)10519);
    D2COMMON_SetUnitStat  = (D2COMMON_SetUnitStat_t) GetProcAddress(hCommon, (LPCSTR)10517);
    D2COMMON_AddSkill     = (D2COMMON_AddSkill_t)   GetProcAddress(hCommon, (LPCSTR)10952);
    D2COMMON_AssignSkill  = (D2COMMON_AssignSkill_t) GetProcAddress(hCommon, (LPCSTR)10953);
    D2COMMON_RefreshSkill = (D2COMMON_RefreshSkill_t)GetProcAddress(hCommon, (LPCSTR)10940);

    /* D2Win - ordinal exports */
    D2WIN_DrawText     = (D2WIN_DrawText_t)    GetProcAddress(hWin, (LPCSTR)10117);
    D2WIN_SetFont      = (D2WIN_SetFont_t)     GetProcAddress(hWin, (LPCSTR)10127);
    D2WIN_GetTextWidth = (D2WIN_GetTextWidth_t) GetProcAddress(hWin, (LPCSTR)10121);

    /* D2Gfx - ordinal exports */
    D2GFX_DrawRectangle = (D2GFX_DrawRectangle_t)GetProcAddress(hGfx, (LPCSTR)10055);

    return TRUE;
}

#endif /* D2_GAMEAPI_H */
