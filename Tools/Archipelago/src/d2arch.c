/* ================================================================
 * D2Archipelago - Diablo II Archipelago Mod (D2MOO Edition)
 * Built from scratch for D2.Detours + D2MOO
 * ================================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#include <Xinput.h>
#pragma comment(lib, "Xinput9_1_0.lib")
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 1.8.0 — central version header (before any .c files that consume it) */
#include "d2arch_version.h"

/* ================================================================
 * MODULAR SOURCE SPLIT
 * Each #include below is a contiguous block from the original code.
 * Order matters — do NOT rearrange.
 * ================================================================ */

#include "d2arch_config.c"      /* Config loading, logging              (~140 lines) */
#include "d2arch_versionpatch.c"/* 1.8.0 NEW: in-memory version string patch     */
#include "d2arch_api.c"         /* D2 API types, title screen, packets  (~978 lines) */
#include "d2arch_itemlog.c"     /* 1.8.0 NEW: Item log ring buffer               */
#include "d2arch_input.c"       /* Keybindings, XInput controller       (~499 lines) */
#include "d2arch_skills.c"      /* Skill database (210), pool, tiers    (~322 lines) */
#include "d2arch_quests.c"      /* Quest data (108), rewards, cows      (~534 lines) */
#include "d2arch_preloads.c"    /* 1.8.0 NEW: Gated zone-locking preload tables */
#include "d2arch_zones.c"       /* Zone gating, tree positions          (~373 lines) */
#include "d2arch_shuffle.c"     /* Monster + boss shuffle               (~314 lines) */
#include "d2arch_skilltree.c"   /* Skill tree manipulation, reinvest    (~555 lines) */
#include "d2arch_customboss.c"  /* 1.8.0 NEW: Custom Boss spawn framework (Phase 1 MVP) */
#include "d2arch_levelshuffle.c"/* 1.9.0 NEW: System 1 entrance shuffle (teleport-based;
                                   needs GetCurrentArea from skilltree.c) */
#include "d2arch_ubers.c"       /* 1.9.0 NEW: Pandemonium uber recipes (cube hook + spawn).
                                   * 1.9.0 Phase 2: moved BEFORE gameloop.c so its
                                   * Ubers_OnUnitDeathScan + Ubers_DropHellfireTorch
                                   * are visible to the unit-death walk. ubers.c
                                   * itself only needs IsTown (zones.c) and
                                   * fnSpawnMonster (skilltree.c), both already
                                   * included above, so this position is safe. */
#include "d2arch_gameloop.c"    /* Game tick hook, quest detection      (~978 lines) */
#include "d2arch_treasurecow.c" /* 1.8.0 NEW: Archipelago Treasure Cow (Phases 5b-d)
                                   * must come AFTER gameloop.c so it can reuse
                                   * g_pendingLootDrop + fnDropTC infrastructure. */
#include "d2arch_helpers.c"     /* Mouse, drawing, resolution helpers   (~208 lines) */
#include "d2arch_save.c"        /* Save/load, D2S, character init       (~956 lines) */
#include "d2arch_stash.c"       /* 1.8.0 NEW: Multi-tab stash foundation          */
#include "d2arch_celhook.c"     /* 1.8.0 NEW: diagnostic hook on D2Win ord 10039 */
#include "d2arch_stashlogic.c"  /* 1.8.0 NEW: Stash stacking + auto-routing       */
#include "d2arch_stashui.c"     /* 1.8.0 NEW: Stash tab bar + click handling      */
#include "d2arch_render.c"      /* Notifications, menu, pool helpers    (~490 lines) */
#include "d2arch_collections.c" /* 1.9.0 NEW: F1 Collection page (must come AFTER
                                   render.c so g_editorPage static is in scope) */
#include "d2arch_stats.c"       /* 1.9.0 NEW: F1 Statistics / Logbook page
                                   (must come AFTER collections.c so it can
                                   share GetArchDir + Log helpers via
                                   the unity-build include order) */
#include "d2arch_bonuschecks.c" /* 1.9.0 NEW: Bonus check categories
                                   (shrines/urns/barrels/chests/set pickups/
                                   gold milestones). Must come AFTER
                                   stats.c + collections.c (uses Coll_*
                                   accessors) and BEFORE hooks.c (which
                                   calls Bonus_OnXBroken from the existing
                                   object-interact dispatch). */
#include "d2arch_extrachecks.c" /* 1.9.2 NEW: Six new check categories
                                   (Cow expansion / Merc milestones /
                                   Hellforge+High runes / Per-NPC dialogue /
                                   Runeword crafting / Cube recipes).
                                   Mirrors bonuschecks.c structure. Must
                                   come AFTER bonuschecks.c so the
                                   include-order placement (between
                                   bonuschecks.c and hooks.c) keeps the
                                   detection entry points visible to
                                   gameloop.c / save.c / ap.c / stats.c /
                                   editor.c which all #include after this. */
#include "d2arch_customgoal.c"  /* 1.9.2 NEW: AP-side custom goal
                                   (52-target OptionSet, AND completion).
                                   Must come AFTER bonuschecks.c +
                                   extrachecks.c since CustomGoal_PollBulkTargets
                                   externs Bonus_/Extra_ accessors. */
#include "d2arch_hooks.c"       /* 1.9.0 NEW: D2Game detours that drive
                                   item-use + object-interact counters in
                                   stats.c. Must come AFTER stats.c so
                                   the wrappers can write g_charStats. */
#include "d2arch_editor.c"      /* Skill editor panel (RenderEditor)   (~1789 lines) */
#include "d2arch_questlog.c"    /* Quest book + tracker UI              (~713 lines) */
#include "d2arch_ap.c"          /* AP bridge, connection, DC6 loading   (~747 lines) */
#include "d2arch_drawall.c"     /* Skill tree render, DrawAll()        (~2062 lines) */
#include "d2arch_main.c"        /* WndProc, InitAPI, DllMain           (~1111 lines) */
