/* ================================================================
 * d2arch_hooks.h — Logbook event hooks (1.9.0)
 * ================================================================
 * Tiny detour utility + two hooks into D2Game:
 *   - sub_6FC47470 (item use)             → potions/scrolls/tomes
 *   - OBJECTS_OperateHandler              → waypoints/chests/doors/barrels
 *
 * Both wrappers are no-throw guarded (__try) so a bad pointer in
 * the classification path can never crash the game — worst case is
 * a missed counter increment.
 * ================================================================ */
#ifndef D2ARCH_HOOKS_H
#define D2ARCH_HOOKS_H

#include <windows.h>

/* Install the two Logbook hooks. Idempotent — calling it more than
 * once is a no-op after the first successful install. Resolves
 * SUNIT_GetServerUnit (D2Game+0x8BB00) and ITEMS_GetItemType
 * (D2Common ord 10751) lazily. Should be called once per game session
 * from the gameloop tick, gated by a static-flag the caller manages. */
void Hooks_InstallLogbookHooks(HMODULE hD2Game);

#endif /* D2ARCH_HOOKS_H */
