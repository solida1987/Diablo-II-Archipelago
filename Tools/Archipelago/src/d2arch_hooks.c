/* ================================================================
 * d2arch_hooks.c — Logbook event hooks (1.9.0)
 * ================================================================ */
#ifndef D2ARCH_HOOKS_C
#define D2ARCH_HOOKS_C

#include "d2arch_hooks.h"
#include "d2arch_stats.h"

/* ----------------------------------------------------------------
 * Tiny detour utility
 *
 * Hooks a function by writing a 5-byte JMP at its entry. The
 * trampoline preserves the original prologue bytes so the wrapper
 * can call back into the original behavior.
 *
 * `savedLen` MUST be at least 5 AND must end on an instruction
 * boundary in the target. Safe values were determined per target
 * by reading the prologue bytes (see HOOK_PROBE log lines):
 *
 *   sub_6FC47470          → savedLen=5  (5 single-byte pushes)
 *   OBJECTS_OperateHandler→ savedLen=7  (sub esp,14 + mov eax,[esp+20])
 *
 * Wrong savedLen splits an instruction and crashes with PRIV_INSTRUCTION
 * on the next call (per the cube-hook lesson). Don't guess — check
 * the bytes first.
 * ---------------------------------------------------------------- */
typedef struct {
    void* target;
    void* wrapper;
    BYTE  saved[16];
    int   savedLen;
    BYTE* trampoline;
    BOOL  installed;
} Detour;

/* Install a 5-byte JMP detour at `target`. The trampoline is allocated
 * RWX, contains the saved bytes + a JMP back to (target + savedLen).
 * Returns TRUE on success. Idempotent — re-installing is a no-op. */
static BOOL Detour_Install(Detour* d, void* target, void* wrapper, int savedLen) {
    if (!d || !target || !wrapper || savedLen < 5 || savedLen > 16) return FALSE;
    if (d->installed) return TRUE;

    d->target   = target;
    d->wrapper  = wrapper;
    d->savedLen = savedLen;

    /* Trampoline = original bytes + JMP back. Sized generously. */
    d->trampoline = (BYTE*)VirtualAlloc(NULL, 64,
                                        MEM_COMMIT | MEM_RESERVE,
                                        PAGE_EXECUTE_READWRITE);
    if (!d->trampoline) return FALSE;

    /* Snapshot the original prologue BEFORE patching. */
    memcpy(d->saved, target, savedLen);
    memcpy(d->trampoline, d->saved, savedLen);

    /* Append a 5-byte JMP from the trampoline back to (target+savedLen).
     * E9 rel32 — rel32 = (dest - (next-instruction-address)). */
    BYTE* trampJmp = d->trampoline + savedLen;
    DWORD trampJmpEnd = (DWORD)trampJmp + 5;
    DWORD trampDest   = (DWORD)target + (DWORD)savedLen;
    trampJmp[0] = 0xE9;
    *(DWORD*)(trampJmp + 1) = trampDest - trampJmpEnd;

    /* Patch the target with a 5-byte JMP to our wrapper. NOP out any
     * leftover bytes (savedLen-5) to keep the disassembly tidy — those
     * bytes are unreachable since execution diverts at offset 0. */
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

/* ----------------------------------------------------------------
 * Resolved D2 helpers
 * ---------------------------------------------------------------- */
typedef void* (__fastcall *GetServerUnit_t)(void* pGame, int nUnitType, int nUnitGUID);
typedef DWORD (__stdcall  *GetItemType_t)(void* pItem);

static GetServerUnit_t  s_fnGetServerUnit = NULL;
static GetItemType_t    s_fnGetItemType   = NULL;

/* ----------------------------------------------------------------
 * Item-classification helper used by the two new packet hooks.
 * Called with a server item-GUID; resolves it to pItem, reads its
 * type, and bumps the matching CharStats counter. No-op if helpers
 * unresolved or item lookup fails — never throws. The `tag` is the
 * source label used in diagnostic log lines so we can tell which
 * packet triggered the increment.
 * ---------------------------------------------------------------- */
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
        /* ITEMTYPE_* values for OUR 1.10f install — verified against
         * Game/data/global/excel/ItemTypes.txt AND live runtime in
         * the game log (Minor Healing Potion → 76, Book of TP/ID →
         * 18, Scroll → 22).
         *
         * Note the runtime parser skips the "Expansion" divider row
         * at file row 60 in ItemTypes.txt, so:
         *   - rows BEFORE row 60 → runtime index = NR-2
         *   - rows AFTER  row 60 → runtime index = NR-3
         * That's why Book/Scroll match my naive NR-2 counting but
         * the potions (which are after the divider) are off-by-one.
         *
         *   18 = Book             (tbk = TP tome, ibk = ID tome)
         *   22 = Scroll           (tsc = TP scroll, isc = ID scroll)
         *   76 = Healing Potion   (hp1..hp5)
         *   77 = Mana Potion      (mp1..mp5)
         *   78 = Rejuv Potion     (rvs, rvl)
         *   79 = Stamina Potion   (vps)
         *   80 = Antidote Potion  (yps)
         *   81 = Thawing Potion   (wms) */
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

/* ----------------------------------------------------------------
 * Hook A: D2Game+0x56150 — packet 0x26 (UseBeltItem)
 *
 * Sent from client when player right-clicks a belt slot to drink
 * a potion / cast TP scroll / cast ID scroll directly. Packet
 * layout (D2GSPacketClt26, 0x0D bytes total):
 *   +0x00  uint8  nHeader
 *   +0x01  int32  nItemGUID         <-- the consumed item
 *   +0x05  uint32 bUseOnMerc
 *   +0x09  int32  unk
 * ---------------------------------------------------------------- */
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

/* ----------------------------------------------------------------
 * Hook B: D2Game+0x55B50 — packet 0x27 (UseItemAction)
 *
 * Sent when player uses one item ON another item (scroll of identify
 * on unidentified item, scroll on tome to combine, etc.). Packet
 * layout (D2GSPacketClt27, 0x09 bytes total):
 *   +0x00  uint8  nHeader
 *   +0x01  int32  nTargetItemGUID
 *   +0x05  int32  nUseItemGUID      <-- the consumed item
 * ---------------------------------------------------------------- */
static Detour s_pkt27Detour;

static int __fastcall Pkt27_UseItemActionHook(void* pGame, void* pUnit,
                                              void* pPacket, int nSize) {
    if (pPacket && nSize >= 9) {
        int nUseItemGUID = *(int*)((BYTE*)pPacket + 0x05);
        Hooks_ClassifyAndCount(pGame, nUseItemGUID, "PKT27_ACTION");
    }
    return ((PacketHandler_t)s_pkt27Detour.trampoline)(pGame, pUnit, pPacket, nSize);
}

/* ----------------------------------------------------------------
 * Hook C: D2Game+0x56AE0 — packet 0x32 (BuyItemFromNpcBuffer)
 *
 * Sent when the player buys an item from a vendor (Akara, Charsi,
 * Drognan, etc.). Gambling shares this packet — the difference is
 * encoded in nTransactionType (offset 0x0B).
 *
 * Packet layout (D2GSPacketClt32, 17 bytes):
 *   +0x00  uint8   nHeader
 *   +0x01  int32   dwNpcGUID
 *   +0x05  int32   dwItemGUID
 *   +0x09  uint16  nItemMode
 *   +0x0B  uint16  nTransactionType   (vendor=0, gamble != 0)
 *   +0x0D  int32   dwCost
 * (size 17 = 0x11 — checked by handler before dispatching)
 * ---------------------------------------------------------------- */
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
            if (doLog) Log("PKT32_BUY: txType=%u cost=%d\n",
                           (unsigned)txType, dwCost);
            /* We count every buy. The vendor-UI gold filter already
             * handles the spend in goldSpent (negative delta). For
             * gamble-vs-vendor distinction we use txType: vendor=0,
             * gamble has bits set per D2's transaction-type enum. */
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

/* ----------------------------------------------------------------
 * Hook D: D2Game+0x56B30 — packet 0x33 (SellItemToNpcBuffer)
 *
 * Sent when the player sells an item to a vendor. Increments
 * itemsSoldToVendor. Gold-from-sale is already counted in the
 * Coll_PollGoldDelta path (positive delta WITH vendor UI open is
 * skipped from goldCollected — but the sale revenue still increments
 * the sale counter here).
 *
 * Packet size 17 same as 0x32. Layout shifted (+1=GUID1, +5=GUID2,
 * +9=u16, +13=i32). For our counter we don't need the specifics.
 * ---------------------------------------------------------------- */
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

/* ----------------------------------------------------------------
 * Hook E: D2Game+0x61250 — sub_6FC91250 (TradeButton handler)
 *
 * This is the inner function called by packet 0x4F (ClickButton) that
 * processes ALL trade-window button clicks: stash close/withdraw/deposit
 * (buttons 18..21) and cube close/transmute (buttons 23..24). When
 * nButton == TRADEBTN_TRANSMUTE (24) the cube transmute fires for any
 * recipe (gem upgrades, runeword socket fills, rerolls, Pandemonium
 * recipes, vanilla Cow Portal — everything).
 *
 * Signature (D2MOO PlrTrade.cpp:1257):
 *   int __fastcall sub_6FC91250(D2GameStrc*, D2UnitStrc* pPlayer,
 *                               uint16_t nButton, int32_t nGoldAmount)
 * ---------------------------------------------------------------- */
typedef int (__fastcall *TradeBtnFn_t)(void*, void*, uint16_t, int);

static Detour s_tradeBtnDetour;

#define TRADEBTN_TRANSMUTE 24

static int __fastcall TradeBtn_Hook(void* pGame, void* pPlayer,
                                    uint16_t nButton, int nGoldAmount) {
    static int s_logCount = 0;
    BOOL doLog = (s_logCount < 6);
    if (doLog) s_logCount++;

    if (nButton == TRADEBTN_TRANSMUTE) {
        g_charStats.cubeTransmutes++;
        if (doLog) Log("TRADEBTN_HOOK: TRANSMUTE (cube) — count=%llu\n",
                       (unsigned long long)g_charStats.cubeTransmutes);
    } else if (doLog) {
        Log("TRADEBTN_HOOK: button=%u (not transmute, ignored)\n",
            (unsigned)nButton);
    }
    return ((TradeBtnFn_t)s_tradeBtnDetour.trampoline)(pGame, pPlayer,
                                                      nButton, nGoldAmount);
}

/* ----------------------------------------------------------------
 * Hook #2: OBJECTS_OperateHandler (every object interaction)
 *
 * Single dispatcher for waypoints, chests, barrels, doors, shrines,
 * wells, secret-doors, exploding chests, etc. We classify by reading
 * the object's nOperateFn from its D2ObjectsTxt record.
 *
 * Signature (D2MOO ObjMode.cpp:2942, D2Game+0x489C0):
 *   int __fastcall OBJECTS_OperateHandler(D2GameStrc*, D2UnitStrc* pPlayer,
 *                                         int nObjectType, int nObjectGUID,
 *                                         int* pResult)
 *
 * Object struct layout (1.10f, verified from D2MOO Object.h):
 *   pObject       + 0x14 = pObjectData     (D2ObjectDataStrc*, ~0x38 bytes)
 *   pObjectData   + 0x00 = pObjectsTxt     (D2ObjectsTxt*, the txt record)
 *   pObjectsTxt   + 0x1B3 = nOperateFn     (uint8_t, dispatch index 0..54+)
 * Earlier code read the byte at +0x1B3 of pObjectData itself, which is
 * past the end of that struct — got garbage memory that happened to be
 * 0 every time. Two-level deref now.
 * ---------------------------------------------------------------- */
typedef int (__fastcall *OperateHandlerFn_t)(void*, void*, int, int, int*);

static Detour s_operateDetour;

static int __fastcall OperateHandlerHook(void* pGame, void* pPlayer,
                                         int nObjectType, int nObjectGUID,
                                         int* pResult) {
    static int s_logCount = 0;
    BOOL doLog = (s_logCount < 8);
    if (doLog) s_logCount++;

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
                /* Two-level deref: pUnit+0x14 = pObjectData (small struct),
                 * pObjectData+0x00 = pObjectsTxt (the actual txt record). */
                void* pObjectData = *(void**)((BYTE*)pObject + 0x14);
                void* pObjectsTxt = pObjectData
                                    ? *(void**)((BYTE*)pObjectData + 0x00)
                                    : NULL;
                if (doLog) Log("OPERATE_HOOK:  pObjectData=%08X pObjectsTxt=%08X\n",
                               (DWORD)pObjectData, (DWORD)pObjectsTxt);
                if (pObjectsTxt) {
                    BYTE operateFn = *((BYTE*)pObjectsTxt + 0x1B3);
                    if (doLog) Log("OPERATE_HOOK:  operateFn=%d\n", (int)operateFn);
                    /* Full dispatch map from D2MOO ObjMode.cpp:79-156.
                     * Indices that aren't classified here are silently
                     * ignored (decorative torches, gibbets, books that
                     * have no obvious gameplay meaning, etc.). */
                    switch (operateFn) {
                        /* ----- Movement ----- */
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

                        /* ----- Loot containers ----- */
                        case 1:  /* Casket */
                        case 4:  /* Chest */
                        case 19: /* ArmorStand (drops armor) */
                        case 20: /* WeaponRack (drops weapons) */
                        case 26: /* BookShelf */
                        case 30: /* ExplodingChest */
                        case 51: /* JungleStash */
                        case 57: /* KhalimChest 1 */
                        case 58: /* KhalimChest 2 */
                        case 59: /* KhalimChest 3 */
                            g_charStats.chestsOpened++;
                            /* 1.9.0 — fire bonus AP check via escalating chance */
                            Bonus_OnChestOpened(g_currentDifficulty);
                            break;

                        /* ----- Breakables ----- */
                        case 3: /* Urn / Basket / Jar — separate from barrels */
                            g_charStats.jarsBroken++;
                            Bonus_OnUrnBroken(g_currentDifficulty);
                            break;
                        case 5: /* Barrel */
                        case 7: /* ExplodingBarrel */
                            g_charStats.barrelsBroken++;
                            Bonus_OnBarrelBroken(g_currentDifficulty);
                            break;

                        /* ----- Resource givers ----- */
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

                        /* ----- Quest-specific objects (single super-bucket) ----- */
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
                        case 49: /* HellForge */
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

    return ((OperateHandlerFn_t)s_operateDetour.trampoline)(pGame, pPlayer,
                                                            nObjectType, nObjectGUID,
                                                            pResult);
}

/* ----------------------------------------------------------------
 * Public install
 * ---------------------------------------------------------------- */
void Hooks_InstallLogbookHooks(HMODULE hD2Game) {
    if (!hD2Game) return;
    static BOOL s_installed = FALSE;
    if (s_installed) return;

    /* Resolve helpers. SUNIT_GetServerUnit is at a fixed offset (not
     * exported by ordinal in 1.10f) — D2MOO source places it at
     * D2Game.0x6FCBBB00 → offset 0x8BB00 from the 0x6FC30000 base. */
    s_fnGetServerUnit = (GetServerUnit_t)((DWORD)hD2Game + 0x8BB00);

    HMODULE hCommon = GetModuleHandleA("D2Common.dll");
    if (hCommon) {
        s_fnGetItemType = (GetItemType_t)GetProcAddress(hCommon, (LPCSTR)10751);
    }

    if (!s_fnGetServerUnit || !s_fnGetItemType) {
        Log("HOOKS: helper resolution failed (GetServerUnit=%08X GetItemType=%08X) — bailing\n",
            (DWORD)s_fnGetServerUnit, (DWORD)s_fnGetItemType);
        /* Don't install — the wrappers' classification would silently
         * no-op anyway, but bailing means we don't write any patches
         * we'd then have to undo. */
        return;
    }

    /* Probe prologue bytes of every hook target so the log shows the
     * decoded instructions if anything goes wrong. */
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

    /* Hook 1: operate handler (7-byte boundary at D2Game+0x489C0).
     * The 7-byte save is critical — `mov eax,[esp+20h]` straddles
     * the 5th byte and a naive 5-byte detour would corrupt EAX. */
    BOOL ok1 = Detour_Install(&s_operateDetour,
                              (void*)((DWORD)hD2Game + 0x489C0),
                              (void*)OperateHandlerHook,
                              7);

    /* Hook 2: packet 0x26 handler at D2Game+0x56150 (UseBeltItem).
     * Prologue (verified by probe):
     *   83 EC 08          sub esp, 8         ; 3 bytes
     *   8B 44 24 10       mov eax, [esp+10h] ; 4 bytes — straddles byte 5
     *   53 55 56          push ebx/ebp/esi   ; bytes 7-9
     *   ...
     * 5-byte JMP detour would split the mov instruction (same gotcha
     * as OperateHandler). savedLen=7 covers both prologue instructions
     * cleanly and JMPs back at byte 7 (the push ebx boundary). */
    BOOL ok2 = Detour_Install(&s_pkt26Detour,
                              (void*)((DWORD)hD2Game + 0x56150),
                              (void*)Pkt26_UseBeltItemHook,
                              7);

    /* Hook 3: packet 0x27 handler at D2Game+0x55B50 (UseItemAction).
     * Prologue:
     *   51                push ecx           ; 1 byte
     *   8B 44 24 0C       mov eax, [esp+0Ch] ; 4 bytes (bytes 1-4)
     *   53                push ebx           ; 1 byte (byte 5)
     * The mov is wholly within the saved 5 bytes, boundary at byte 5
     * is clean — savedLen=5 is correct. */
    BOOL ok3 = Detour_Install(&s_pkt27Detour,
                              (void*)((DWORD)hD2Game + 0x55B50),
                              (void*)Pkt27_UseItemActionHook,
                              5);

    /* Hook 4: packet 0x32 handler at D2Game+0x56AE0 (VendorBuy / Gamble).
     * Hook 5: packet 0x33 handler at D2Game+0x56B30 (VendorSell).
     *
     * Prologue (verified by probe — both have IDENTICAL stub layout):
     *   83 7C 24 08 11    cmp [esp+8], 11h     ; 5 bytes
     *   74 08             jz +8                ; 2 bytes (bytes 5-6, RELATIVE)
     *   B8 03 00 00 00    mov eax, 3           ; 5 bytes
     *   C2 08 00          ret 8                ; 3 bytes
     *
     * savedLen=7 would copy the `74 08` jz into the trampoline. That
     * jz uses an EIP-relative offset which becomes wrong once relocated —
     * it would jump to garbage 8 bytes past the trampoline's jz, missing
     * the original's body. savedLen=5 keeps the jz IN PLACE in the
     * original (since 5-byte JMP only overwrites bytes 0-4 = the cmp).
     * The trampoline replays just the cmp, then JMPs back to byte 5
     * which is the still-intact `74 08` instruction. ZF is preserved
     * across JMPs so the conditional branch resolves correctly. */
    BOOL ok4 = Detour_Install(&s_pkt32Detour,
                              (void*)((DWORD)hD2Game + 0x56AE0),
                              (void*)Pkt32_VendorBuyHook,
                              5);
    BOOL ok5 = Detour_Install(&s_pkt33Detour,
                              (void*)((DWORD)hD2Game + 0x56B30),
                              (void*)Pkt33_VendorSellHook,
                              5);

    /* Hook 6: sub_6FC91250 at D2Game+0x61250 (TradeButton handler).
     * Catches the TRANSMUTE button (id=24).
     *
     * Prologue (probe):
     *   83 EC 2C          sub esp, 2Ch         ; 3 bytes
     *   53 55 56          push ebx/ebp/esi     ; 3 single bytes
     *   8B F2             mov esi, edx         ; 2 bytes at offsets 6-7
     *
     * savedLen=7 SPLITS the `mov esi, edx` instruction at byte 6 —
     * trampoline's stray `8B` byte combines with the JMP-back's first
     * byte (E9) to form `mov ebp, ecx`, corrupting EBP. Use savedLen=5
     * which lands cleanly between push ebp and push esi (boundary at
     * byte 5, where push esi starts). */
    BOOL ok6 = Detour_Install(&s_tradeBtnDetour,
                              (void*)((DWORD)hD2Game + 0x61250),
                              (void*)TradeBtn_Hook,
                              5);

    Log("HOOKS: OperateHandler=%d Pkt26=%d Pkt27=%d Pkt32=%d Pkt33=%d TradeBtn=%d\n",
        ok1, ok2, ok3, ok4, ok5, ok6);

    s_installed = TRUE;
}

#endif /* D2ARCH_HOOKS_C */
