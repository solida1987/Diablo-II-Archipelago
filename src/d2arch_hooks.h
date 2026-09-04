/* d2arch_hooks.h — Logbook event hooks (1.9.0) Tiny detour utility + two hooks into D2Game: - sub_6FC47470 (item use) -> potions/scrolls/tomes - OBJECTS_OperateHandler -> waypoints/chests/doors/barrels Both wrappers are no-throw guarded (__try) so a bad pointer in the classification path can never crash the game — worst case is a missed counter increment. */
#ifndef D2ARCH_HOOKS_H
#define D2ARCH_HOOKS_H

#include <windows.h>

/* Install the two Logbook hooks. */
void Hooks_InstallLogbookHooks(HMODULE hD2Game);

/* force a NAMED set/unique instead of letting the engine roll one of the many rows sharing the same base code. */
void Hooks_ArmForcedItemRow(int rowIdx, void* pGame, BOOL isUnique, int minItemLevel);
void Hooks_DisarmForcedItemRow(void);
int  Hooks_ForcedItemStampCount(void);

#endif /* D2ARCH_HOOKS_H */
