/* d2arch_hooks.c — Logbook event hooks (1.9.0) */
#ifndef D2ARCH_HOOKS_C
#define D2ARCH_HOOKS_C

#include "d2arch_hooks.h"
#include "d2arch_stats.h"

/* Tiny detour utility Hooks a function by writing a 5-byte JMP at its entry. */
typedef struct {
    void* target;
    void* wrapper;
    BYTE  saved[16];
    int   savedLen;
    BYTE* trampoline;
    BOOL  installed;
} Detour;

/* Install a 5-byte JMP detour at `target`. */
static BOOL Detour_Install(Detour* d, void* target, void* wrapper, int savedLen) {
    if (!d || !target || !wrapper || savedLen < 5 || savedLen > 16) return FALSE;
    if (d->installed) return TRUE;

    d->target   = target;
    d->wrapper  = wrapper;
    d->savedLen = savedLen;

    /* Trampoline = original bytes + JMP back. */
    d->trampoline = (BYTE*)VirtualAlloc(NULL, 64,
                                        MEM_COMMIT | MEM_RESERVE,
                                        PAGE_EXECUTE_READWRITE);
    if (!d->trampoline) return FALSE;

    /* Snapshot the original prologue BEFORE patching. */
    memcpy(d->saved, target, savedLen);
    memcpy(d->trampoline, d->saved, savedLen);

    /* Append a 5-byte JMP from the trampoline back to (target+savedLen). */
    BYTE* trampJmp = d->trampoline + savedLen;
    DWORD trampJmpEnd = (DWORD)trampJmp + 5;
    DWORD trampDest   = (DWORD)target + (DWORD)savedLen;
    trampJmp[0] = 0xE9;
    *(DWORD*)(trampJmp + 1) = trampDest - trampJmpEnd;

    /* Patch the target with a 5-byte JMP to our wrapper. */
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, savedLen, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        VirtualFree(d->trampoline, 0, MEM_RELEASE);
        d->trampoline = NULL;
        return FALSE;
    }
    BYTE* t = (BYTE*)target;
    DWORD jmpEnd = (DWORD)t + 5;
    t[0] = 0xE9;
    *(DWORD*)(t + 1) = (DWORD)wrapper - jmpEnd;
    for (int i = 5; i < savedLen; i++) t[i] = 0x90;  /* NOP filler */
    VirtualProtect(target, savedLen, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, savedLen);

    d->installed = TRUE;
    return TRUE;
}

/* Resolved D2 helpers */
typedef void* (__fastcall *GetServerUnit_t)(void* pGame, int nUnitType, int nUnitGUID);
typedef DWORD (__stdcall  *GetItemType_t)(void* pItem);

static GetServerUnit_t  s_fnGetServerUnit = NULL;
static GetItemType_t    s_fnGetItemType   = NULL;

/* Item-classification helper used by the two new packet hooks. */
static void Hooks_ClassifyAndCount(void* pGame, int nItemGUID, const char* tag) {
    static int s_logCount = 0;
    BOOL doLog = (s_logCount < 8);
    if (doLog) s_logCount++;

    __try {
        if (!s_fnGetServerUnit || !s_fnGetItemType) {
            if (doLog) Log("%s: helpers not resolved\n", tag);
            return;
        }
        /* UNIT_ITEM = 4. */
        void* pItem = s_fnGetServerUnit(pGame, 4, nItemGUID);
        if (!pItem) {
            if (doLog) Log("%s: GUID=%d resolved NULL\n", tag, nItemGUID);
            return;
        }
        DWORD itemType = s_fnGetItemType(pItem);
        if (doLog) Log("%s: GUID=%d itemType=%u\n", tag, nItemGUID, itemType);
        /* ITEMTYPE_* values for OUR 1.10f install — verified against Game/data/global/excel/ItemTypes.txt AND live runtime in the game log (Minor Healing Potion -> 76, Book of TP/ID -> 18, Scroll -> 22). */
        if (itemType == 18) {
            g_charStats.tomesUsed++;
        } else if (itemType == 22) {
            g_charStats.scrollsUsed++;
        } else if (itemType >= 76 && itemType <= 81) {
            g_charStats.potionsConsumed++;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (doLog) Log("%s: EXCEPTION in classification\n", tag);
    }
}

/* Hook A: D2Game+0x56150 — packet 0x26 (UseBeltItem) Sent from client when player right-clicks a belt slot to drink a potion / cast TP scroll / cast ID scroll directly. */
typedef int (__fastcall *PacketHandler_t)(void*, void*, void*, int);

static Detour s_pkt26Detour;

static int __fastcall Pkt26_UseBeltItemHook(void* pGame, void* pUnit,
                                            void* pPacket, int nSize) {
    if (pPacket && nSize >= 5) {
        int nItemGUID = *(int*)((BYTE*)pPacket + 0x01);
        Hooks_ClassifyAndCount(pGame, nItemGUID, "PKT26_BELT");
    }
    return ((PacketHandler_t)s_pkt26Detour.trampoline)(pGame, pUnit, pPacket, nSize);
}

/* Hook B: D2Game+0x55B50 — packet 0x27 (UseItemAction) Sent when player uses one item ON another item (scroll of identify on unidentified item, scroll on tome to combine, etc.). */
static Detour s_pkt27Detour;

static int __fastcall Pkt27_UseItemActionHook(void* pGame, void* pUnit,
                                              void* pPacket, int nSize) {
    if (pPacket && nSize >= 9) {
        int nUseItemGUID = *(int*)((BYTE*)pPacket + 0x05);
        Hooks_ClassifyAndCount(pGame, nUseItemGUID, "PKT27_ACTION");
    }
    return ((PacketHandler_t)s_pkt27Detour.trampoline)(pGame, pUnit, pPacket, nSize);
}

/* Hook C: D2Game+0x56AE0 — packet 0x32 (BuyItemFromNpcBuffer) Sent when the player buys an item from a vendor (Akara, Charsi, Drognan, etc.). */
static Detour s_pkt32Detour;

static int __fastcall Pkt32_VendorBuyHook(void* pGame, void* pUnit,
                                          void* pPacket, int nSize) {
    static int s_logCount = 0;
    BOOL doLog = (s_logCount < 4);
    if (doLog) s_logCount++;

    if (pPacket && nSize >= 17) {
        __try {
            uint16_t txType = *(uint16_t*)((BYTE*)pPacket + 0x0B);
            int32_t  dwCost = *(int32_t*) ((BYTE*)pPacket + 0x0D);
            DWORD    itemGuid = *(DWORD*)((BYTE*)pPacket + 0x05);   /* 0x32: id, npcGuid, itemGuid, flags, cost */
            if (doLog) Log("PKT32_BUY: txType=%u cost=%d item=%08X\n",
                           (unsigned)txType, dwCost, itemGuid);
            /* Bought (and gambled) items count for the Collection book. */
            {
                extern void Coll_MarkPurchase(DWORD itemGuid);
                Coll_MarkPurchase(itemGuid);
            }
            /* We count every buy. The vendor-UI gold filter already handles the spend in goldSpent (negative delta). For gamble-vs-vendor distinction we use txType: vendor=0, gamble has bits set per D2's transaction-type enum. */
            if (txType == 0) {
                g_charStats.itemsBoughtFromVendor++;
            } else {
                g_charStats.gambledItems++;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            if (doLog) Log("PKT32_BUY: EXCEPTION\n");
        }
    }
    return ((PacketHandler_t)s_pkt32Detour.trampoline)(pGame, pUnit, pPacket, nSize);
}

/* Hook D: D2Game+0x56B30 — packet 0x33 (SellItemToNpcBuffer) Sent when the player sells an item to a vendor. */
static Detour s_pkt33Detour;

static int __fastcall Pkt33_VendorSellHook(void* pGame, void* pUnit,
                                           void* pPacket, int nSize) {
    static int s_logCount = 0;
    BOOL doLog = (s_logCount < 4);
    if (doLog) s_logCount++;

    if (pPacket && nSize >= 17) {
        if (doLog) Log("PKT33_SELL: fired (size=%d)\n", nSize);
        g_charStats.itemsSoldToVendor++;
    }
    return ((PacketHandler_t)s_pkt33Detour.trampoline)(pGame, pUnit, pPacket, nSize);
}

/* Hook E: D2Game+0x61250 — sub_6FC91250 (TradeButton handler) This is the inner function called by packet 0x4F (ClickButton) that processes ALL trade-window button clicks: stash close/withdraw/deposit (buttons 18..21) and cube close/transmute (buttons 23..24). */
typedef int (__fastcall *TradeBtnFn_t)(void*, void*, uint16_t, int);

static Detour s_tradeBtnDetour;

#define TRADEBTN_TRANSMUTE 24

static int __fastcall TradeBtn_Hook(void* pGame, void* pPlayer,
                                    uint16_t nButton, int nGoldAmount) {
    static int s_logCount = 0;
    BOOL doLog = (s_logCount < 6);
    if (doLog) s_logCount++;

    int rv = ((TradeBtnFn_t)s_tradeBtnDetour.trampoline)(pGame, pPlayer,
                                                          nButton, nGoldAmount);
    if (nButton == TRADEBTN_TRANSMUTE) {
        /* The trampoline return value is non-zero on a SUCCESSFUL transmute (D2's cube checks all return BOOL via EAX). */
        if (rv != 0) {
            g_charStats.cubeTransmutes++;
            if (doLog) Log("TRADEBTN_HOOK: TRANSMUTE OK — count=%llu\n",
                           (unsigned long long)g_charStats.cubeTransmutes);
            /* 1.9.2 Cat 6 — fire the next sequential cube-recipe AP slot. */
            extern void Extra_OnCubeRecipeAuto(void);
            Extra_OnCubeRecipeAuto();
        } else if (doLog) {
            Log("TRADEBTN_HOOK: TRANSMUTE failed (no recipe matched)\n");
        }
    } else if (doLog) {
        Log("TRADEBTN_HOOK: button=%u (not transmute, ignored)\n",
            (unsigned)nButton);
    }
    return rv;
}

/* Hook #2: OBJECTS_OperateHandler (every object interaction) Single dispatcher for waypoints, chests, barrels, doors, shrines, wells, secret-doors, exploding chests, etc. */
typedef int (__fastcall *OperateHandlerFn_t)(void*, void*, int, int, int*);

static Detour s_operateDetour;

/* IRON GOLEM SAVE-BRICK GUARD (EX) Symptom: "unable to enter game, bad hireable" — the character can never be played again. */
typedef int (__fastcall *GolemSectionFn_t)(void*, void*, BYTE**, BYTE*, DWORD);

static Detour s_golemWriteDetour;
static Detour s_golemReadDetour;

#define PLRSAVE2ERROR_BAD_IRONGOLEM 23

static int __fastcall GolemWriteHook(void* pGame, void* pPlayer,
                                     BYTE** ppSection, BYTE* pEnd, DWORD a5) {
    BYTE* before = NULL;
    int rc;

    __try { before = ppSection ? *ppSection : NULL; }
    __except(EXCEPTION_EXECUTE_HANDLER) { before = NULL; }

    rc = ((GolemSectionFn_t)s_golemWriteDetour.trampoline)(pGame, pPlayer,
                                                           ppSection, pEnd, a5);
    if (rc != 0 || !before) return rc;

    /* Original wrote 'kf' (0x6B 0x66) + hasGolem byte [+ item]. */
    __try {
        if (*ppSection > before + 2 &&
            before[0] == 0x6B && before[1] == 0x66 && before[2] != 0) {
            before[2] = 0;
            *ppSection = before + 3;
            Log("GOLEM GUARD: iron golem NOT persisted (save kept clean)\n");
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("GOLEM GUARD: exception while trimming golem section\n");
    }
    return rc;
}

static int __fastcall GolemReadHook(void* pGame, void* pPlayer,
                                    BYTE** ppSection, BYTE* pEnd, DWORD dwVersion) {
    int rc = ((GolemSectionFn_t)s_golemReadDetour.trampoline)(pGame, pPlayer,
                                                              ppSection, pEnd, dwVersion);
    if (rc == PLRSAVE2ERROR_BAD_IRONGOLEM) {
        /* Un-brick: this is the LAST section of the save, so skipping it costs nothing but the golem itself. */
        Log("GOLEM GUARD: suppressed BAD_IRONGOLEM (%d) — character loads without golem\n", rc);
        return 0;
    }
    return rc;
}

/* HOOK 9 — FORCED SET/UNIQUE ROW (V4c: "Mara's arrived as Metalgrid") ROOT CAUSE. */
typedef void* (__fastcall *CreateItemExFn_t)(void* pGame, void* pItemDrop, int bUseSeed);

static Detour s_createItemExDetour;
static volatile LONG s_forcedItemRow  = 0;   /* 1-based row; 0 = disarmed */
static volatile LONG s_forcedItemHits = 0;
/* Item-level floor stamped alongside the row. 0 = leave the request's level
 * alone. Needed for the two uniques whose UniqueItems.txt lvl is 110 (Hellfire
 * Torch, Annihilus): below that level the engine will not roll them at all. */
static volatile LONG s_forcedItemMinLvl = 0;

#define D2_ITEMDROP_OFF_NITEMLVL   0x0C
#define D2_ITEMDROP_OFF_BFORCE     0x2C
#define D2_ITEMDROP_OFF_NQUALITY   0x30
#define D2_ITEMDROP_OFF_NITEMINDEX 0x40
#define D2_ITEMQUAL_SET            5
#define D2_ITEMQUAL_UNIQUE         7

/* ONE UNIQUE PER GAME — why forcing needs this to be safe. */
#define D2_GAME_OFF_UNIQUEFLAGS 0x1B24

static void Hooks_ClearUniqueDroppedFlag(void* pGame, int rowIdx) {
    if (!pGame || rowIdx < 0 || rowIdx >= 128 * 32) return;
    __try {
        DWORD* flags = (DWORD*)((BYTE*)pGame + D2_GAME_OFF_UNIQUEFLAGS);
        flags[rowIdx >> 5] &= ~(1u << (rowIdx & 0x1F));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* Arm for the NEXT item creation(s) until disarmed. */
void Hooks_ArmForcedItemRow(int rowIdx, void* pGame, BOOL isUnique, int minItemLevel) {
    InterlockedExchange(&s_forcedItemHits, 0);
    InterlockedExchange(&s_forcedItemMinLvl, (minItemLevel > 0 && minItemLevel <= 127) ? (LONG)minItemLevel : 0);
    InterlockedExchange(&s_forcedItemRow, (rowIdx >= 0) ? (LONG)(rowIdx + 1) : 0);
    if (rowIdx >= 0 && isUnique) Hooks_ClearUniqueDroppedFlag(pGame, rowIdx);
}

void Hooks_DisarmForcedItemRow(void) {
    InterlockedExchange(&s_forcedItemRow, 0);
    InterlockedExchange(&s_forcedItemMinLvl, 0);
}

/* How many creations actually received the stamp since arming — used by the delivery log so a test run tells us whether the hook fired. */
int Hooks_ForcedItemStampCount(void) {
    return (int)InterlockedCompareExchange(&s_forcedItemHits, 0, 0);
}

static void* __fastcall CreateItemExHook(void* pGame, void* pItemDrop, int bUseSeed) {
    LONG row = InterlockedCompareExchange(&s_forcedItemRow, 0, 0);
    if (row > 0 && pItemDrop) {
        __try {
            int bForce  = *(int*)((BYTE*)pItemDrop + D2_ITEMDROP_OFF_BFORCE);
            int quality = *(int*)((BYTE*)pItemDrop + D2_ITEMDROP_OFF_NQUALITY);
            if (!bForce && (quality == D2_ITEMQUAL_SET || quality == D2_ITEMQUAL_UNIQUE)) {
                *(int*)((BYTE*)pItemDrop + D2_ITEMDROP_OFF_NITEMINDEX) = (int)row;
                /* Raise the level to the row's floor when the caller set one:
                 * the qlvl filter runs on THIS field, and a 110 row against a
                 * 99 request fails every time, whatever index is stamped. */
                LONG minLvl = InterlockedCompareExchange(&s_forcedItemMinLvl, 0, 0);
                if (minLvl > 0) {
                    int* pLvl = (int*)((BYTE*)pItemDrop + D2_ITEMDROP_OFF_NITEMLVL);
                    if (*pLvl < (int)minLvl) *pLvl = (int)minLvl;
                }
                InterlockedIncrement(&s_forcedItemHits);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return ((CreateItemExFn_t)s_createItemExDetour.trampoline)(pGame, pItemDrop, bUseSeed);
}

static int __fastcall OperateHandlerHook(void* pGame, void* pPlayer,
                                         int nObjectType, int nObjectGUID,
                                         int* pResult) {
    static int s_logCount = 0;
    BOOL doLog = (s_logCount < 8);
    if (doLog) s_logCount++;

    /* capture pre-trampoline state for B17 chest cheat fix. */
    void* pObjectForPost = NULL;
    BYTE  preOperateFn   = 0;
    DWORD preMode        = 0;

    __try {
        if (doLog) {
            Log("OPERATE_HOOK: fired pGame=%08X pPlayer=%08X nObjType=%d nGUID=%d "
                "fnGetServerUnit=%08X\n",
                (DWORD)pGame, (DWORD)pPlayer, nObjectType, nObjectGUID,
                (DWORD)s_fnGetServerUnit);
        }
        if (s_fnGetServerUnit) {
            void* pObject = s_fnGetServerUnit(pGame, nObjectType, nObjectGUID);
            if (doLog) Log("OPERATE_HOOK:  pObject=%08X\n", (DWORD)pObject);
            if (pObject) {
                pObjectForPost = pObject;
                preMode = *(DWORD*)((BYTE*)pObject + 0x10);
                /* Two-level deref: pUnit+0x14 = pObjectData (small struct), pObjectData+0x00 = pObjectsTxt (the actual txt record). */
                void* pObjectData = *(void**)((BYTE*)pObject + 0x14);
                void* pObjectsTxt = pObjectData
                                    ? *(void**)((BYTE*)pObjectData + 0x00)
                                    : NULL;
                if (doLog) Log("OPERATE_HOOK:  pObjectData=%08X pObjectsTxt=%08X\n",
                               (DWORD)pObjectData, (DWORD)pObjectsTxt);
                if (pObjectsTxt) {
                    BYTE operateFn = *((BYTE*)pObjectsTxt + 0x1B3);
                    preOperateFn = operateFn;
                    if (doLog) Log("OPERATE_HOOK:  operateFn=%d\n", (int)operateFn);
                    /* Full dispatch map from D2MOO ObjMode.cpp:79-156. */
                    switch (operateFn) {
                        /* Movement */
                        case 23: /* Waypoint */
                            g_charStats.waypointsUsed++;
                            break;
                        case 15: /* Portal */
                        case 27: /* TeleportPad */
                        case 34: /* ArcaneSanctuaryPortal */
                        case 43: /* DurielPortal */
                        case 44: /* SewerStairs (Travincal) */
                        case 46: /* HellGatePortal */
                        case 70: /* BaalPortal */
                        case 72: /* LastPortal (Worldstone) */
                        case 73: /* LastLastPortal */
                            g_charStats.portalsTaken++;
                            break;
                        case 8:  /* Door */
                        case 16: /* TrapDoor */
                        case 18: /* SecretDoor */
                        case 29: /* SlimeDoor */
                        case 47: /* Stair */
                        case 50: /* Stair (variant) */
                        case 71: /* SummitDoor */
                            g_charStats.doorsOpened++;
                            break;

                        /* Loot containers 1.9.4 expanded list — Maegis bug report: hidden stash, loose rocks, and dead rogue corpses weren't counted as chests. */
                        case 1:  /* Casket — also covers HiddenStash, dead rogue corpses */
                        case 4:  /* Chest */
                        case 19: /* ArmorStand (drops armor) */
                        case 20: /* WeaponRack (drops weapons) */
                        case 26: /* BookShelf */
                        case 30: /* ExplodingChest */
                        case 36: /* LooseRock (Maegis: was missing) */
                        case 51: /* JungleStash */
                        case 57: /* KhalimChest 1 */
                        case 58: /* KhalimChest 2 */
                        case 59: /* KhalimChest 3 */
                        case 60: /* TristramCoffin loot variant */
                            /* Intentionally NO-OP here — handled post-trampoline. */
                            break;

                        /* Breakables */
                        case 3: /* Urn / Basket / Jar — separate from barrels */
                            g_charStats.jarsBroken++;
                            Bonus_OnUrnBroken(g_currentDifficulty);
                            break;
                        case 5: /* Barrel */
                        case 7: /* ExplodingBarrel */
                            g_charStats.barrelsBroken++;
                            Bonus_OnBarrelBroken(g_currentDifficulty);
                            break;

                        /* Resource givers */
                        case 2: /* Shrine */
                            g_charStats.shrinesActivated++;
                            Bonus_OnShrineActivated(g_currentDifficulty);
                            break;
                        case 22: /* Well */
                            g_charStats.wellsDrunk++;
                            break;
                        case 14: /* Corpse loot (Fallen-style decorative dead bodies) */
                            g_charStats.corpsesLooted++;
                            break;
                        case 9: /* Monolith — the "stones" you flip in Cold Plains, etc. */
                            g_charStats.monolithsActivated++;
                            break;

                        /* Quest-specific objects (single super-bucket) */
                        case 10: /* CainGibbet (Tristram rescue) */
                        case 12: /* InifussTree (Cain quest) */
                        case 17: /* Obelisk (Viper Temple altar) */
                        case 21: /* HoradrimMalus (Charsi imbue) */
                        case 24: /* TaintedSunAltar */
                        case 25: /* StaffOrifice */
                        case 28: /* LamEsenTome (Khalim quest) */
                        case 31: /* GidbinnDecoy */
                        case 33: /* WirtsBody */
                        case 39: /* HoradricCubeChest */
                        case 40: /* HoradricScrollChest */
                        case 41: /* StaffOfKingsChest */
                        case 42: /* SanctuaryTome (Anya) */
                        case 45: /* SewerLever (Travincal) */
                        case 48: /* TrappedSoul (Anya alt) */
                            g_charStats.questObjectsInteracted++;
                            break;
                        case 49: /* HellForge — fires 1.9.2 Cat 3 (Hellforge use) 1.9.2 fix: split out from the cases above so Extra_OnHellforgeUsed only fires for actual Hellforge interactions. */
                            {
                                extern void Extra_OnHellforgeUsed(int diff);
                                Extra_OnHellforgeUsed(g_currentDifficulty);
                            }
                            g_charStats.questObjectsInteracted++;
                            break;
                        case 52: case 54: case 55: case 56: /* DiabloSeals */
                        case 53: /* CompellingOrb */
                        case 61: /* HarrogathMainGate */
                        case 65: /* AncientsAltar */
                        case 67: /* FrozenAnya */
                        case 68: /* EvilUrn (Anya act 5) */
                            g_charStats.questObjectsInteracted++;
                            break;

                        default: break;
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (doLog) Log("OPERATE_HOOK:  EXCEPTION in classification path\n");
    }

    int rc = ((OperateHandlerFn_t)s_operateDetour.trampoline)(pGame, pPlayer,
                                                              nObjectType, nObjectGUID,
                                                              pResult);

    /* POST-trampoline classification for loot containers. */
    if (pObjectForPost && preOperateFn != 0) {
        __try {
            DWORD postMode = *(DWORD*)((BYTE*)pObjectForPost + 0x10);
            BOOL  isLootContainer = FALSE;
            switch (preOperateFn) {
                case 1: case 4: case 19: case 20: case 26: case 30:
                case 36: case 51: case 57: case 58: case 59: case 60:
                    isLootContainer = TRUE;
                    break;
                default: break;
            }
            if (isLootContainer && postMode > preMode) {
                /* Mode advanced — chest opened. */
                g_charStats.chestsOpened++;
                Bonus_OnChestOpened(g_currentDifficulty);
            } else if (isLootContainer && doLog) {
                Log("OPERATE_HOOK: chest open SUPPRESSED (operateFn=%d preMode=%lu "
                    "postMode=%lu rc=%d) — locked w/o key or failed attempt\n",
                    (int)preOperateFn, preMode, postMode, rc);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            if (doLog) Log("OPERATE_HOOK: EXCEPTION in post-trampoline classify\n");
        }
    }

    return rc;
}

/* Public install */
void Hooks_InstallLogbookHooks(HMODULE hD2Game) {
    if (!hD2Game) return;
    static BOOL s_installed = FALSE;
    if (s_installed) return;

    /* Resolve helpers. SUNIT_GetServerUnit is at a fixed offset (not exported by ordinal in 1.10f) — D2MOO source places it at D2Game.0x6FCBBB00 -> offset 0x8BB00 from the 0x6FC30000 base. */
    s_fnGetServerUnit = (GetServerUnit_t)((DWORD)hD2Game + 0x8BB00);

    HMODULE hCommon = GetModuleHandleA("D2Common.dll");
    if (hCommon) {
        s_fnGetItemType = (GetItemType_t)GetProcAddress(hCommon, (LPCSTR)10751);
    }

    if (!s_fnGetServerUnit || !s_fnGetItemType) {
        Log("HOOKS: helper resolution failed (GetServerUnit=%08X GetItemType=%08X) — bailing\n",
            (DWORD)s_fnGetServerUnit, (DWORD)s_fnGetItemType);
        /* Don't install — the wrappers' classification would silently no-op anyway, but bailing means we don't write any patches we'd then have to undo. */
        return;
    }

    /* Probe prologue bytes of every hook target so the log shows the decoded instructions if anything goes wrong. */
    static const struct { DWORD off; const char* name; } probes[] = {
        { 0x489C0, "OBJECTS_OperateHandler" },
        { 0x56150, "Pkt0x26_UseBeltItem"    },
        { 0x55B50, "Pkt0x27_UseItemAction"  },
        { 0x56AE0, "Pkt0x32_VendorBuy"      },
        { 0x56B30, "Pkt0x33_VendorSell"     },
        { 0x61250, "TradeBtn_sub6FC91250"   },
    };
    for (int p = 0; p < (int)(sizeof(probes)/sizeof(probes[0])); p++) {
        BYTE* bp = (BYTE*)((DWORD)hD2Game + probes[p].off);
        char hex[96] = {0};
        __try {
            int off = 0;
            for (int i = 0; i < 16 && off < 92; i++) {
                off += _snprintf(hex + off, 92 - off, "%02X ", bp[i]);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            _snprintf(hex, sizeof(hex)-1, "(read failed)");
        }
        Log("HOOKS: probe %s @ +0x%05X = %s\n", probes[p].name, probes[p].off, hex);
    }

    /* Hook 1: operate handler (7-byte boundary at D2Game+0x489C0). */
    BOOL ok1 = Detour_Install(&s_operateDetour,
                              (void*)((DWORD)hD2Game + 0x489C0),
                              (void*)OperateHandlerHook,
                              7);

    /* Hook 2: packet 0x26 handler at D2Game+0x56150 (UseBeltItem). */
    BOOL ok2 = Detour_Install(&s_pkt26Detour,
                              (void*)((DWORD)hD2Game + 0x56150),
                              (void*)Pkt26_UseBeltItemHook,
                              7);

    /* Hook 3: packet 0x27 handler at D2Game+0x55B50 (UseItemAction). */
    BOOL ok3 = Detour_Install(&s_pkt27Detour,
                              (void*)((DWORD)hD2Game + 0x55B50),
                              (void*)Pkt27_UseItemActionHook,
                              5);

    /* Hook 4: packet 0x32 handler at D2Game+0x56AE0 (VendorBuy / Gamble). */
    BOOL ok4 = Detour_Install(&s_pkt32Detour,
                              (void*)((DWORD)hD2Game + 0x56AE0),
                              (void*)Pkt32_VendorBuyHook,
                              5);
    BOOL ok5 = Detour_Install(&s_pkt33Detour,
                              (void*)((DWORD)hD2Game + 0x56B30),
                              (void*)Pkt33_VendorSellHook,
                              5);

    /* Hook 6: sub_6FC91250 at D2Game+0x61250 (TradeButton handler). */
    BOOL ok6 = Detour_Install(&s_tradeBtnDetour,
                              (void*)((DWORD)hD2Game + 0x61250),
                              (void*)TradeBtn_Hook,
                              5);

    /* Hook 7: PLRSAVE2_WriteIronGolemSection at D2Game+0x5D880. */
    BOOL ok7 = FALSE, ok8 = FALSE;
    {
        static const BYTE kWritePro[] = { 0x83, 0xEC, 0x08, 0x8B, 0x44, 0x24, 0x0C };
        static const BYTE kReadPro[]  = { 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x0C };
        BYTE* pw = (BYTE*)((DWORD)hD2Game + 0x5D880);
        BYTE* pr = (BYTE*)((DWORD)hD2Game + 0x5EAB0);
        BOOL wOk = FALSE, rOk = FALSE;
        __try {
            wOk = (memcmp(pw, kWritePro, sizeof(kWritePro)) == 0);
            rOk = (memcmp(pr, kReadPro,  sizeof(kReadPro))  == 0);
        } __except(EXCEPTION_EXECUTE_HANDLER) { wOk = rOk = FALSE; }

        if (wOk) ok7 = Detour_Install(&s_golemWriteDetour, (void*)pw, (void*)GolemWriteHook, 7);
        else     Log("HOOKS: golem WRITE prologue mismatch — hook SKIPPED (unexpected D2Game build)\n");
        if (rOk) ok8 = Detour_Install(&s_golemReadDetour,  (void*)pr, (void*)GolemReadHook,  6);
        else     Log("HOOKS: golem READ prologue mismatch — hook SKIPPED (unexpected D2Game build)\n");
    }

    /* Hook 9: D2GAME_CreateItemEx at D2Game+0x1ED80. */
    BOOL ok9 = FALSE;
    {
        static const BYTE kCreatePro[] = { 0x51, 0x55, 0x8B, 0xE9, 0x56 };
        BYTE* pc = (BYTE*)((DWORD)hD2Game + 0x1ED80);
        BOOL cOk = FALSE;
        __try { cOk = (memcmp(pc, kCreatePro, sizeof(kCreatePro)) == 0); }
        __except(EXCEPTION_EXECUTE_HANDLER) { cOk = FALSE; }

        if (cOk) ok9 = Detour_Install(&s_createItemExDetour, (void*)pc,
                                      (void*)CreateItemExHook, 5);
        else     Log("HOOKS: CreateItemEx prologue mismatch — named set/unique forcing DISABLED\n");
    }

    Log("HOOKS: OperateHandler=%d Pkt26=%d Pkt27=%d Pkt32=%d Pkt33=%d TradeBtn=%d GolemWrite=%d GolemRead=%d CreateItemEx=%d\n",
        ok1, ok2, ok3, ok4, ok5, ok6, ok7, ok8, ok9);

    /* Shop Shuffle full-pool shelf fill (d2arch_shops.c) — prologue-verified, arms itself. */
    {
        extern void Shops_Install(HMODULE hGame);
        Shops_Install(hD2Game);
    }

    s_installed = TRUE;
}

#endif /* D2ARCH_HOOKS_C */
