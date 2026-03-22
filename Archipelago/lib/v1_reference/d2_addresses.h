/*
 * d2_addresses.h — Fixed memory addresses for Diablo II 1.10f
 * No ASLR on D2 1.10, all DLL bases are constant.
 */
#ifndef D2_ADDRESSES_H
#define D2_ADDRESSES_H

#include <windows.h>

/* ===== DLL Base Addresses ===== */
#define D2CLIENT_BASE   0x6FAA0000
#define D2COMMON_BASE   0x6FD50000
#define D2GAME_BASE     0x6FC40000
#define D2LAUNCH_BASE   0x6FA40000
#define D2GFX_BASE      0x6FA70000
#define D2WIN_BASE      0x6F8F0000
#define D2LANG_BASE     0x6FC00000
#define D2NET_BASE      0x6FBF0000
#define FOG_BASE        0x6FF50000
#define STORM_BASE      0x6FBB0000

/* ===== D2Client Offsets ===== */
#define PLAYER_UNIT_OFF     0x11C200   /* *(DWORD*)(D2CLIENT_BASE + off) = player UnitNode* */
#define MONSTER_HASH_OFF    0x11AC00   /* 128-bucket hash table for monsters */
#define AREA_ID_OFF         0x11C1B4   /* Current area/level ID */
#define AUTOMAP_DRAW_OFF    0x2EF10    /* AUTOMAP_Draw function (confirmed 1.10f) */
#define DRAW_GAME_UI_OFF    0x5E650    /* DrawGameUI function (main UI render) */
#define SKILLTREE_RENDER_OFF 0x1F7E0   /* Skill tree rendering function */
#define DISPLAY_MSG_OFF     0x7E120    /* Display message function */
#define DRAW_FILLED_RECT_OFF 0xBB0F0  /* DrawFilledRectangle (D2Client native) */
#define RESOLUTION_X_OFF    0xD40EC    /* Screen width variable (640/800) */
#define AUTOMAP_ON_OFF      0x11A6D0   /* Automap visibility flag */
#define MOUSE_X_OFF         0xB7BC0    /* GetMouseXCoord function */
#define MOUSE_Y_OFF         0xB7BD0    /* GetMouseYCoord function */
#define SCREEN_W_OFF        0xDBC48    /* Screen width (640) */
#define SCREEN_H_OFF        0xDBC4C    /* Screen height (480) */

/* ===== D2Common Offsets ===== */
#define SGPT_DATA_TABLES_OFF 0x96A20   /* sgptDataTables pointer */

/* ===== Convenience Macros ===== */
#define PLAYER_UNIT_PTR     (*(DWORD*)(D2CLIENT_BASE + PLAYER_UNIT_OFF))
#define MONSTER_HASH_TABLE  (D2CLIENT_BASE + MONSTER_HASH_OFF)
#define CURRENT_AREA_ID     (*(DWORD*)(D2CLIENT_BASE + AREA_ID_OFF))

/* ===== Unit Struct Offsets ===== */
#define UNIT_TYPE_OFF       0x00    /* DWORD: 0=player, 1=monster, 2=object, 3=missile, 4=item */
#define UNIT_TXTID_OFF      0x04    /* DWORD: txtFileNo */
#define UNIT_UNITID_OFF     0x0C    /* DWORD: unique unit ID */
#define UNIT_MODE_OFF       0x10    /* DWORD: current animation mode */
#define UNIT_PPATH_OFF      0x2C    /* DWORD: pPath pointer */
#define UNIT_STATLIST_OFF   0x5C    /* DWORD: pStatList pointer */
#define UNIT_NEXT_OFF       0xE8    /* DWORD: next unit in hash chain */

/* ===== Stat IDs ===== */
#define STAT_STRENGTH       0
#define STAT_ENERGY         1
#define STAT_DEXTERITY      2
#define STAT_VITALITY       3
#define STAT_NEWSKILLS      4      /* Unspent skill points */
#define STAT_NEWSTATS       5      /* Unspent stat points */
#define STAT_LEVEL          6
#define STAT_EXPERIENCE     7
#define STAT_GOLD           14     /* Gold on person */
#define STAT_GOLDBANK       15     /* Gold in stash */
#define STAT_HITPOINTS      6      /* Current HP (shifted <<8) */
#define STAT_MAXHP          7      /* Max HP (shifted <<8) */
#define STAT_MANA           8      /* Current Mana (shifted <<8) */
#define STAT_MAXMANA        9      /* Max Mana (shifted <<8) */

/* ===== Monster Mode Values ===== */
#define MODE_DEATH          0
#define MODE_NEUTRAL        1
#define MODE_WALK           2
#define MODE_ATTACK1        4
#define MODE_ATTACK2        5
#define MODE_DEAD           12

/* ===== Hash Table Config ===== */
#define HASH_TABLE_SIZE     128

/* ===== Act Boss TxtIDs ===== */
#define BOSS_ANDARIEL       156
#define BOSS_DURIEL         211
#define BOSS_MEPHISTO       242
#define BOSS_DIABLO         243
#define BOSS_BAAL           544

#endif /* D2_ADDRESSES_H */
