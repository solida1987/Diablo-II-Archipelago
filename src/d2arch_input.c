static WORD GetSkillIdFromStruct(void* pSkill) {
    if (!pSkill) return 0;
    void* pSkillsTxt = *(void**)pSkill;  /* First field = pSkillsTxt */
    if (!pSkillsTxt) return 0;
    return *(WORD*)pSkillsTxt;  /* First field of SkillsTxt = wSkillId */
}

/* Send packet 0x3C to switch the right-click skill */
static void SwitchRightSkill(WORD skillId) {
    BYTE pkt[9];
    pkt[0] = 0x3C;
    *(WORD*)&pkt[1] = skillId;
    pkt[3] = 0;
    pkt[4] = 0x00;  /* 0x00 = right hand */
    *(DWORD*)&pkt[5] = 0xFFFFFFFF;  /* no item */
    SendGamePacket(9, pkt);
}

/* Send packet 0x0C to cast right-click skill at mouse location */
static void CastRightSkillAtMouse(void) {
    if (!pMouseX || !pMouseY) return;
    BYTE pkt[5];
    pkt[0] = 0x0C;
    /* D2 uses world coordinates, but mouse position from D2Client is already world coords */
    *(WORD*)&pkt[1] = (WORD)(*pMouseX);
    *(WORD*)&pkt[3] = (WORD)(*pMouseY);
    SendGamePacket(5, pkt);
}

/* UI State */
static BOOL g_initialized  = FALSE;
static BOOL g_menuOpen     = FALSE;
static BOOL g_editorOpen   = FALSE;
static BOOL g_questLogOpen = FALSE;
static BOOL g_trackerOn    = TRUE;
static BOOL g_skillTreeOpen = FALSE;  /* ARCHIPELAGO: our custom 30-skill tree panel */
static BOOL g_skillPanelReset = FALSE; /* Set TRUE when player changes to force DC6 reload */
static BOOL g_stHideColors = TRUE;    /* Toggle: hide tier-colored backgrounds in skill tree (default: hidden) */

/* Global icon system: loaded once, used by all 30 buttons */
static void* g_archIcons = NULL;
static BOOL  g_archIconsLoaded = FALSE;
static int   g_icoMap[400]; /* skillId -> frame index */
static BOOL  g_icoMapLoaded = FALSE;
static int* g_pVanillaSkillTreeOpen = NULL; /* D2Client's native skill tree state (0x6FBB3664) */
static void* g_cellSkillPanel = NULL;  /* skltree_a_back.DC6 panel background */
static void* g_cellSkillIcons = NULL;  /* Skillicon.DC6 skill icons (48x48 each) */
static int   g_skillPanelLoaded = 0;
static BOOL  g_slotsDirty = TRUE;   /* Set TRUE when skill assignments change; ApplyAllSlots resets */
static BOOL  g_slotsApplied = FALSE; /* TRUE after first successful ApplyAllSlots */
static char g_charName[32] = {0};

/* Screen dimensions */
static int g_screenW = 800;
static int g_screenH = 600;

/* WndProc hook */
static WNDPROC g_origWndProc = NULL;

/* Forward declaration for keybinding system */
static void GetArchDir(char* buf, int bufSize);

/* KEYBINDING SYSTEM — configurable key mappings */
/* Custom AP keybindings (defaults) */
/* the panels live on 5/6/7/8, NOT F1-F4. */
static BYTE g_keySkillEditor = '5';
static BYTE g_keyQuestLog    = '6';
static BYTE g_keyTracker     = '7';
static BYTE g_keyZoneMap     = '8';

/* Mouse-wheel skill cycling — how many hotkey slots the wheel walks (F1..FN). */
static int  g_wheelSkillSlots = 8;
static int  g_wheelSkillCur   = 0;

/* Vanilla D2 key remapping: user's key -> original D2 key */
#define MAX_KEYREMAP 20
typedef struct { BYTE userKey; BYTE d2Key; } KeyRemap;
static KeyRemap g_keyRemaps[MAX_KEYREMAP];
static int g_keyRemapCount = 0;

/* Default vanilla D2 keys (what user can remap FROM) */
static BYTE g_keyInventory   = 'I';
static BYTE g_keyCharacter   = 'C';
static BYTE g_keyD2Quest     = 'Q';
static BYTE g_keySpellbook   = 'B';
static BYTE g_keySkillBar    = 'S';
static BYTE g_keyAutomap     = VK_TAB;
static BYTE g_keyShowItems   = VK_MENU; /* Alt */

/* Show-items toggle. `g_showItemsToggleOn` is the SETTING (latching behaviour on/off); `g_showItemsLatched` is the current state of the latch. */
static BOOL g_showItemsToggleOn = TRUE;
static BOOL g_showItemsLatched  = FALSE;

/* HOW THIS WORKS, and why the first attempt could not have. */
typedef SHORT (WINAPI *GetKeyState_fn)(int);
static GetKeyState_fn g_origGetKeyState      = NULL;
static GetKeyState_fn g_origGetAsyncKeyState = NULL;
static BOOL           g_showItemsHookOk      = FALSE;

/* VK_MENU is what the game asks for, but a remapped key could be anything, and some callers ask for the side-specific Alt. */
static BOOL ShowItems_IsOurKey(int vk) {
    if (vk == g_keyShowItems) return TRUE;
    if (g_keyShowItems == VK_MENU && (vk == VK_LMENU || vk == VK_RMENU)) return TRUE;
    return FALSE;
}

static SHORT WINAPI ShowItems_GetKeyState(int vk) {
    SHORT r = g_origGetKeyState ? g_origGetKeyState(vk) : 0;
    if (g_showItemsLatched && ShowItems_IsOurKey(vk)) r |= (SHORT)0x8000;
    return r;
}

static SHORT WINAPI ShowItems_GetAsyncKeyState(int vk) {
    SHORT r = g_origGetAsyncKeyState ? g_origGetAsyncKeyState(vk) : 0;
    if (g_showItemsLatched && ShowItems_IsOurKey(vk)) r |= (SHORT)0x8000;
    return r;
}

/* Redirect one imported function in `mod`'s import table.
 *
 * Matches by name when fnName is set, otherwise by ordinal. The D2 modules
 * export almost everything by ordinal only — D2Client pulls its text drawing
 * from D2Win as #10117 — so a name-only version reaches USER32 and nothing
 * inside the game. */
static BOOL PatchImportEx(HMODULE mod, const char* dllName, const char* fnName,
                          WORD ordinal, void* newFn, void** oldFn) {
    if (!mod) return FALSE;
    BYTE* base = (BYTE*)mod;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return FALSE;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return FALSE;

    IMAGE_DATA_DIRECTORY* dir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->VirtualAddress || !dir->Size) return FALSE;

    IMAGE_IMPORT_DESCRIPTOR* imp =
        (IMAGE_IMPORT_DESCRIPTOR*)(base + dir->VirtualAddress);
    for (; imp->Name; imp++) {
        const char* name = (const char*)(base + imp->Name);
        if (_stricmp(name, dllName) != 0) continue;
        /* OriginalFirstThunk carries the names, FirstThunk the live pointers. */
        if (!imp->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA* nameThunk = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA* addrThunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; nameThunk->u1.AddressOfData; nameThunk++, addrThunk++) {
            BOOL byOrdinal = (nameThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) != 0;
            if (fnName) {
                if (byOrdinal) continue;
                IMAGE_IMPORT_BY_NAME* ibn =
                    (IMAGE_IMPORT_BY_NAME*)(base + nameThunk->u1.AddressOfData);
                if (strcmp((const char*)ibn->Name, fnName) != 0) continue;
            } else {
                if (!byOrdinal) continue;
                if ((WORD)IMAGE_ORDINAL(nameThunk->u1.Ordinal) != ordinal) continue;
            }

            DWORD oldProt = 0;
            if (!VirtualProtect(&addrThunk->u1.Function, sizeof(void*),
                                PAGE_READWRITE, &oldProt))
                return FALSE;
            if (oldFn) *oldFn = (void*)addrThunk->u1.Function;
            addrThunk->u1.Function = (ULONG_PTR)newFn;
            VirtualProtect(&addrThunk->u1.Function, sizeof(void*), oldProt, &oldProt);
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL PatchImport(HMODULE mod, const char* dllName, const char* fnName,
                        void* newFn, void** oldFn) {
    return PatchImportEx(mod, dllName, fnName, 0, newFn, oldFn);
}

/* Install once, after D2Client is in memory. */
static void ShowItems_InstallHook(void) {
    if (g_showItemsHookOk) return;
    HMODULE d2c = GetModuleHandleA("D2Client.dll");
    if (!d2c) { Log("SHOWITEMS: D2Client.dll not loaded yet — latch unavailable\n"); return; }

    BOOL a = PatchImport(d2c, "USER32.dll", "GetKeyState",
                         (void*)ShowItems_GetKeyState, (void**)&g_origGetKeyState);
    BOOL b = PatchImport(d2c, "USER32.dll", "GetAsyncKeyState",
                         (void*)ShowItems_GetAsyncKeyState, (void**)&g_origGetAsyncKeyState);
    if (a || b) {
        g_showItemsHookOk = TRUE;
        Log("SHOWITEMS: hooked D2Client imports (GetKeyState=%d GetAsyncKeyState=%d)\n",
            a, b);
    } else {
        g_showItemsToggleOn = FALSE;
        Log("SHOWITEMS: could not hook D2Client's key-state imports — "
            "latch disabled, Alt behaves as stock hold-to-see\n");
    }
}

static void ShowItems_Toggle(void) {
    if (!g_showItemsHookOk) return;
    g_showItemsLatched = !g_showItemsLatched;
    Log("SHOWITEMS: latch %s\n", g_showItemsLatched ? "ON" : "OFF");
}

/* Drop the latch: focus loss, a panel opening, or leaving the game. */
static void ShowItems_ForceOff(void) {
    if (!g_showItemsLatched) return;
    g_showItemsLatched = FALSE;
    Log("SHOWITEMS: latch released\n");
}
static BYTE g_keyRunWalk     = 'R';
static BYTE g_keySwapWeapons = 'W';
static BYTE g_keyBelt1       = '1';
static BYTE g_keyBelt2       = '2';
static BYTE g_keyBelt3       = '3';
static BYTE g_keyBelt4       = '4';

/* Helper: convert key name string to VK code */
static BYTE KeyNameToVK(const char* name) {
    if (!name || !name[0]) return 0;
    /* Single character */
    if (name[1] == 0) {
        char c = name[0];
        if (c >= 'A' && c <= 'Z') return (BYTE)c;
        if (c >= 'a' && c <= 'z') return (BYTE)(c - 32);
        if (c >= '0' && c <= '9') return (BYTE)c;
        return 0;
    }
    /* Function keys */
    if (name[0] == 'F' || name[0] == 'f') {
        int n = atoi(name + 1);
        if (n >= 1 && n <= 12) return (BYTE)(VK_F1 + n - 1);
    }
    /* Named keys */
    if (_stricmp(name, "Tab") == 0) return VK_TAB;
    if (_stricmp(name, "Alt") == 0) return VK_MENU;
    if (_stricmp(name, "Shift") == 0) return VK_SHIFT;
    if (_stricmp(name, "Ctrl") == 0) return VK_CONTROL;
    if (_stricmp(name, "Space") == 0) return VK_SPACE;
    if (_stricmp(name, "Enter") == 0) return VK_RETURN;
    if (_stricmp(name, "Esc") == 0) return VK_ESCAPE;
    if (_stricmp(name, "Backspace") == 0) return VK_BACK;
    if (_stricmp(name, "Delete") == 0) return VK_DELETE;
    if (_stricmp(name, "Insert") == 0) return VK_INSERT;
    if (_stricmp(name, "Home") == 0) return VK_HOME;
    if (_stricmp(name, "End") == 0) return VK_END;
    if (_stricmp(name, "PageUp") == 0) return VK_PRIOR;
    if (_stricmp(name, "PageDown") == 0) return VK_NEXT;
    if (_stricmp(name, "Up") == 0) return VK_UP;
    if (_stricmp(name, "Down") == 0) return VK_DOWN;
    if (_stricmp(name, "Left") == 0) return VK_LEFT;
    if (_stricmp(name, "Right") == 0) return VK_RIGHT;
    if (_stricmp(name, "NumLock") == 0) return VK_NUMLOCK;
    if (_stricmp(name, "ScrollLock") == 0) return VK_SCROLL;
    if (_stricmp(name, ";") == 0) return VK_OEM_1;
    if (_stricmp(name, "/") == 0) return VK_OEM_2;
    if (_stricmp(name, "`") == 0) return VK_OEM_3;
    if (_stricmp(name, "[") == 0) return VK_OEM_4;
    if (_stricmp(name, "\\") == 0) return VK_OEM_5;
    if (_stricmp(name, "]") == 0) return VK_OEM_6;
    if (_stricmp(name, "'") == 0) return VK_OEM_7;
    if (_stricmp(name, ",") == 0) return VK_OEM_COMMA;
    if (_stricmp(name, ".") == 0) return VK_OEM_PERIOD;
    if (_stricmp(name, "-") == 0) return VK_OEM_MINUS;
    if (_stricmp(name, "=") == 0) return VK_OEM_PLUS;
    if (_stricmp(name, "Numpad0") == 0 || _stricmp(name, "Num0") == 0) return VK_NUMPAD0;
    if (_stricmp(name, "Numpad1") == 0 || _stricmp(name, "Num1") == 0) return VK_NUMPAD1;
    if (_stricmp(name, "Numpad2") == 0 || _stricmp(name, "Num2") == 0) return VK_NUMPAD2;
    if (_stricmp(name, "Numpad3") == 0 || _stricmp(name, "Num3") == 0) return VK_NUMPAD3;
    if (_stricmp(name, "Numpad4") == 0 || _stricmp(name, "Num4") == 0) return VK_NUMPAD4;
    if (_stricmp(name, "Numpad5") == 0 || _stricmp(name, "Num5") == 0) return VK_NUMPAD5;
    if (_stricmp(name, "Numpad6") == 0 || _stricmp(name, "Num6") == 0) return VK_NUMPAD6;
    if (_stricmp(name, "Numpad7") == 0 || _stricmp(name, "Num7") == 0) return VK_NUMPAD7;
    if (_stricmp(name, "Numpad8") == 0 || _stricmp(name, "Num8") == 0) return VK_NUMPAD8;
    if (_stricmp(name, "Numpad9") == 0 || _stricmp(name, "Num9") == 0) return VK_NUMPAD9;
    return 0;
}

/* Build remap table: if user changed a vanilla D2 key, add a remap entry */
static void BuildKeyRemaps(void) {
    g_keyRemapCount = 0;
    struct { BYTE* userKey; BYTE d2Default; } defs[] = {
        { &g_keyInventory,   'I' },
        { &g_keyCharacter,   'C' },
        { &g_keyD2Quest,     'Q' },
        { &g_keySpellbook,   'B' },
        { &g_keySkillBar,    'S' },
        { &g_keyAutomap,     VK_TAB },
        { &g_keyShowItems,   VK_MENU },
        { &g_keyRunWalk,     'R' },
        { &g_keySwapWeapons, 'W' },
        { &g_keyBelt1,       '1' },
        { &g_keyBelt2,       '2' },
        { &g_keyBelt3,       '3' },
        { &g_keyBelt4,       '4' },
    };
    for (int i = 0; i < 13; i++) {
        if (*defs[i].userKey != defs[i].d2Default && g_keyRemapCount < MAX_KEYREMAP) {
            g_keyRemaps[g_keyRemapCount].userKey = *defs[i].userKey;
            g_keyRemaps[g_keyRemapCount].d2Key = defs[i].d2Default;
            g_keyRemapCount++;
        }
    }
    Log("KeyRemaps: %d active remaps\n", g_keyRemapCount);
}

/* CONTROLLER SUPPORT — XInput gamepad Left stick = move cursor, buttons = clicks/keys */
static BOOL g_controllerEnabled = FALSE;
static XINPUT_STATE g_padState;
static WORD g_padPrevButtons = 0;
static float g_padSensitivity = 8.0f;
#define PAD_DEADZONE 7849

/* Check if button was JUST pressed (rising edge) */
#define PAD_PRESSED(btn) ((g_padState.Gamepad.wButtons & (btn)) && !(g_padPrevButtons & (btn)))
#define PAD_HELD(btn) (g_padState.Gamepad.wButtons & (btn))

void SimulateKey(BYTE vk, BOOL down) {
    INPUT inp = {0};
    inp.type = INPUT_KEYBOARD;
    inp.ki.wVk = vk;
    inp.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &inp, sizeof(INPUT));
}

static void SimulateClick(DWORD downFlag, DWORD upFlag) {
    INPUT inp[2] = {0};
    inp[0].type = INPUT_MOUSE;
    inp[0].mi.dwFlags = downFlag;
    inp[1].type = INPUT_MOUSE;
    inp[1].mi.dwFlags = upFlag;
    SendInput(2, inp, sizeof(INPUT));
}

static void ProcessController(void) {
    XINPUT_STATE state;
    DWORD result = XInputGetState(0, &state);
    if (result != ERROR_SUCCESS) return; /* No controller connected */

    g_padState = state;
    XINPUT_GAMEPAD *pad = &state.Gamepad;

    /* --- Left Stick -> Mouse Cursor --- */
    {
        float lx = (float)pad->sThumbLX;
        float ly = (float)pad->sThumbLY;

        /* Dead zone */
        if (lx > -PAD_DEADZONE && lx < PAD_DEADZONE) lx = 0;
        if (ly > -PAD_DEADZONE && ly < PAD_DEADZONE) ly = 0;

        if (lx != 0 || ly != 0) {
            /* Normalize to -1..1 */
            float nx = lx / 32767.0f;
            float ny = ly / 32767.0f;

            /* Apply sensitivity with acceleration (faster at edges) */
            float speed = g_padSensitivity * (1.0f + (nx*nx + ny*ny) * 2.0f);
            int dx = (int)(nx * speed);
            int dy = (int)(-ny * speed); /* Y is inverted */

            if (dx != 0 || dy != 0) {
                mouse_event(MOUSEEVENTF_MOVE, dx, dy, 0, 0);
            }
        }
    }

    /* --- Buttons (rising edge detection) --- */

    /* A = Left click (hold while button held for drag/assign support) */
    {
        static BOOL aWasDown = FALSE;
        BOOL aDown = PAD_HELD(XINPUT_GAMEPAD_A);
        if (aDown && !aWasDown) {
            INPUT inp = {0}; inp.type = INPUT_MOUSE; inp.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            SendInput(1, &inp, sizeof(INPUT));
        } else if (!aDown && aWasDown) {
            INPUT inp = {0}; inp.type = INPUT_MOUSE; inp.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &inp, sizeof(INPUT));
        }
        aWasDown = aDown;
    }

    /* B = Right click (hold while button held) */
    {
        static BOOL bWasDown = FALSE;
        BOOL bDown = PAD_HELD(XINPUT_GAMEPAD_B);
        if (bDown && !bWasDown) {
            INPUT inp = {0}; inp.type = INPUT_MOUSE; inp.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
            SendInput(1, &inp, sizeof(INPUT));
        } else if (!bDown && bWasDown) {
            INPUT inp = {0}; inp.type = INPUT_MOUSE; inp.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
            SendInput(1, &inp, sizeof(INPUT));
        }
        bWasDown = bDown;
    }

    /* X = Belt slot 1 */
    if (PAD_PRESSED(XINPUT_GAMEPAD_X)) {
        SimulateKey(g_keyBelt1, TRUE); Sleep(10); SimulateKey(g_keyBelt1, FALSE);
    }

    /* Y = Belt slot 2 */
    if (PAD_PRESSED(XINPUT_GAMEPAD_Y)) {
        SimulateKey(g_keyBelt2, TRUE); Sleep(10); SimulateKey(g_keyBelt2, FALSE);
    }

    /* LB = Belt slot 3 */
    if (PAD_PRESSED(XINPUT_GAMEPAD_LEFT_SHOULDER)) {
        SimulateKey(g_keyBelt3, TRUE); Sleep(10); SimulateKey(g_keyBelt3, FALSE);
    }

    /* RB = Belt slot 4 */
    if (PAD_PRESSED(XINPUT_GAMEPAD_RIGHT_SHOULDER)) {
        SimulateKey(g_keyBelt4, TRUE); Sleep(10); SimulateKey(g_keyBelt4, FALSE);
    }

    /* D-Pad Up = Inventory */
    if (PAD_PRESSED(XINPUT_GAMEPAD_DPAD_UP)) {
        SimulateKey(g_keyInventory, TRUE); Sleep(10); SimulateKey(g_keyInventory, FALSE);
    }

    /* D-Pad Down = Character screen */
    if (PAD_PRESSED(XINPUT_GAMEPAD_DPAD_DOWN)) {
        SimulateKey(g_keyCharacter, TRUE); Sleep(10); SimulateKey(g_keyCharacter, FALSE);
    }

    /* D-Pad Left = Skill Editor (our custom) */
    if (PAD_PRESSED(XINPUT_GAMEPAD_DPAD_LEFT)) {
        SimulateKey(g_keySkillEditor, TRUE); Sleep(10); SimulateKey(g_keySkillEditor, FALSE);
    }

    /* D-Pad Right = Quest Log (our custom) */
    if (PAD_PRESSED(XINPUT_GAMEPAD_DPAD_RIGHT)) {
        SimulateKey(g_keyQuestLog, TRUE); Sleep(10); SimulateKey(g_keyQuestLog, FALSE);
    }

    /* Start = ESC */
    if (PAD_PRESSED(XINPUT_GAMEPAD_START)) {
        SimulateKey(VK_ESCAPE, TRUE); Sleep(10); SimulateKey(VK_ESCAPE, FALSE);
    }

    /* Back = Automap (Tab) */
    if (PAD_PRESSED(XINPUT_GAMEPAD_BACK)) {
        SimulateKey(g_keyAutomap, TRUE); Sleep(10); SimulateKey(g_keyAutomap, FALSE);
    }

    /* Left Stick Click = Run/Walk */
    if (PAD_PRESSED(XINPUT_GAMEPAD_LEFT_THUMB)) {
        SimulateKey(g_keyRunWalk, TRUE); Sleep(10); SimulateKey(g_keyRunWalk, FALSE);
    }

    /* Right Stick Click = Swap weapons */
    if (PAD_PRESSED(XINPUT_GAMEPAD_RIGHT_THUMB)) {
        SimulateKey(g_keySwapWeapons, TRUE); Sleep(10); SimulateKey(g_keySwapWeapons, FALSE);
    }

    /* --- Triggers (held = modifier keys) --- */

    /* LT = Show items (Alt hold) */
    {
        static BOOL ltWasHeld = FALSE;
        BOOL ltHeld = (pad->bLeftTrigger > 30);
        if (ltHeld && !ltWasHeld) SimulateKey(VK_MENU, TRUE);   /* Alt down */
        if (!ltHeld && ltWasHeld) SimulateKey(VK_MENU, FALSE);  /* Alt up */
        ltWasHeld = ltHeld;
    }

    /* RT = Stand still (Shift hold) */
    {
        static BOOL rtWasHeld = FALSE;
        BOOL rtHeld = (pad->bRightTrigger > 30);
        if (rtHeld && !rtWasHeld) SimulateKey(VK_SHIFT, TRUE);
        if (!rtHeld && rtWasHeld) SimulateKey(VK_SHIFT, FALSE);
        rtWasHeld = rtHeld;
    }

    /* Save previous button state for next frame */
    g_padPrevButtons = pad->wButtons;
}

/* Load keybindings from d2arch.ini [keybindings] section */
static void LoadKeybindings(void) {
    char ini[MAX_PATH], dir[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(ini, "%sd2arch.ini", dir);

    char buf[32];
    /* Custom AP keys */
    GetPrivateProfileStringA("keybindings", "SkillEditor", "5", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keySkillEditor = v; }
    GetPrivateProfileStringA("keybindings", "QuestLog", "6", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keyQuestLog = v; }
    GetPrivateProfileStringA("keybindings", "Tracker", "7", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keyTracker = v; }
    GetPrivateProfileStringA("keybindings", "ZoneMap", "8", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keyZoneMap = v; }

    /* an existing install still has F1-F4 written into its ini from before the move. */
    {
        struct { BYTE* key; BYTE fkey; char repl; const char* name; } mig[4] = {
            { &g_keySkillEditor, VK_F1, '5', "SkillEditor" },
            { &g_keyQuestLog,    VK_F2, '6', "QuestLog"    },
            { &g_keyTracker,     VK_F3, '7', "Tracker"     },
            { &g_keyZoneMap,     VK_F4, '8', "ZoneMap"     },
        };
        for (int i = 0; i < 4; i++) {
            if (*mig[i].key == mig[i].fkey) {
                *mig[i].key = (BYTE)mig[i].repl;
                char one[2] = { mig[i].repl, 0 };
                WritePrivateProfileStringA("keybindings", mig[i].name, one, ini);
                Log("Keybind migrated: %s F%d -> %c (F-keys returned to the game)\n",
                    mig[i].name, i + 1, mig[i].repl);
            }
        }
    }

    /* ShowItemsToggle=1 (default) makes Alt latch; 0 restores the vanilla hold-to-see behaviour for anyone who prefers it. */
    g_showItemsToggleOn = GetPrivateProfileIntA("keybindings", "ShowItemsToggle", 1, ini) != 0;
    g_wheelSkillSlots = GetPrivateProfileIntA("keybindings", "WheelSkillSlots", 8, ini);
    if (g_wheelSkillSlots < 2)  g_wheelSkillSlots = 2;
    if (g_wheelSkillSlots > 12) g_wheelSkillSlots = 12;

    /* Vanilla D2 remaps */
    GetPrivateProfileStringA("keybindings", "Inventory", "I", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keyInventory = v; }
    GetPrivateProfileStringA("keybindings", "Character", "C", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keyCharacter = v; }
    GetPrivateProfileStringA("keybindings", "D2Quest", "Q", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keyD2Quest = v; }
    GetPrivateProfileStringA("keybindings", "Spellbook", "B", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keySpellbook = v; }
    GetPrivateProfileStringA("keybindings", "SkillBar", "S", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keySkillBar = v; }
    GetPrivateProfileStringA("keybindings", "Automap", "Tab", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keyAutomap = v; }
    GetPrivateProfileStringA("keybindings", "ShowItems", "Alt", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keyShowItems = v; }
    GetPrivateProfileStringA("keybindings", "RunWalk", "R", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keyRunWalk = v; }
    GetPrivateProfileStringA("keybindings", "SwapWeapons", "W", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keySwapWeapons = v; }
    GetPrivateProfileStringA("keybindings", "Belt1", "1", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keyBelt1 = v; }
    GetPrivateProfileStringA("keybindings", "Belt2", "2", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keyBelt2 = v; }
    GetPrivateProfileStringA("keybindings", "Belt3", "3", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keyBelt3 = v; }
    GetPrivateProfileStringA("keybindings", "Belt4", "4", buf, 32, ini);
    { BYTE v = KeyNameToVK(buf); if (v) g_keyBelt4 = v; }

    BuildKeyRemaps();
    Log("Keybindings loaded from %s\n", ini);
    Log("  SkillEditor=0x%02X QuestLog=0x%02X Tracker=0x%02X ZoneMap=0x%02X\n",
        g_keySkillEditor, g_keyQuestLog, g_keyTracker, g_keyZoneMap);
}

/* Hook state — DrawGameUI */
static DWORD g_hookAddr = 0;
static BYTE  g_trampoline[16] = {0};

/* Hook state — D2GFX_EndScene (for AP panel in menu) */
static DWORD g_endSceneHookAddr = 0;
static BYTE  g_endSceneTrampoline[16] = {0};
static DWORD g_endSceneTrampolinePtr = 0;
static BOOL  g_endSceneHooked = FALSE;
static DWORD g_lastDrawGameUITime = 0;

/* AP Connection Panel (strings declared at top of file) */
static int   g_apFocusField   = -1;
/* non-static so d2arch_api.c (earlier in include order) can use an `extern` forward declaration — still TU-private since the whole mod compiles as one translation unit via #include chain. */
BOOL g_apConnected    = FALSE;
static char  g_apStatus[64]   = "Not Connected";
static BOOL  g_apPanelVisible = TRUE;

/* 1.9.5 Bug C3 fix — last bridge error message. */
static char  g_apErrorMsg[128] = {0};

/* AP Settings (from slot_data via bridge) */
/* Goal simplified to 3 values (0=Full Normal, 1=Full NM, 2=Full Hell). */
static int  g_apGoal             = 0;   /* 0=Full Normal / 1=NM / 2=Hell */
#define GOAL_ACT_SCOPE  4                /* always full game (all 5 acts) */
#define GOAL_DIFF_SCOPE (g_apGoal)       /* direct: goal IS difficulty */
#define g_apGoalScope  GOAL_ACT_SCOPE
#define g_apDiffScope  GOAL_DIFF_SCOPE
static int  g_apStartingGold    = 0;
static BOOL g_apDeathLink       = FALSE;
static BOOL g_apGoalComplete    = FALSE;
static int  g_apSkillPoolSize   = 210; /* How many skills in AP pool */

/* Class filter codes (g_classFilter, g_clsEnabled declared at top) */
static const char* g_clsCodes[7] = {"ama","sor","nec","pal","bar","dru","ass"};

static BOOL IsClassEnabled(const char* classCode) {
    if (!g_classFilter) return TRUE; /* all classes mode */
    for (int i = 0; i < 7; i++) {
        if (strcmp(classCode, g_clsCodes[i]) == 0)
            return g_clsEnabled[i];
    }
    return TRUE;
}
static int  g_apStartingSkills = 6;   /* How many skills start unlocked */
static BOOL g_apQuestStory      = TRUE;
static BOOL g_apQuestHunting    = TRUE;
static BOOL g_apQuestKillZones  = TRUE;
static BOOL g_apQuestExploration = TRUE;
static BOOL g_apQuestWaypoints  = TRUE;
static BOOL g_apQuestLevelMilestones = TRUE;

/* Filler distribution percentages (used in standalone reward assignment) */
static int g_fillerGoldPct   = 25;
static int g_fillerStatPct   = 12;
static int g_fillerSkillPct  = 12;
static int g_fillerTrapPct   = 13;
static int g_fillerResetPct  = 20;
static int g_fillerLootPct   = 18;
BOOL g_apMode            = FALSE; /* TRUE when playing AP, FALSE for singleplayer 1.8.3: non-static so d2arch_api.c (included earlier in the unity build) can forward-decl via `extern BOOL g_apMode` and set it from the title-screen Connect button. */
BOOL g_apPolling         = FALSE; /* TRUE after the user clicks Connect (bridge has been started, status file is being written). */
/* game modes are now two independent ON/OFF toggles instead of a mutually-exclusive enum. */
static BOOL g_skillHuntingOn    = TRUE;  /* default ON (old behaviour) */
static BOOL g_zoneLockingOn     = FALSE; /* default OFF */

/* Skill investment level requirement toggle (Maegis #2). */
BOOL g_skillLevelReqs = TRUE;

/* Skill point cap + vanilla level escalation (tester request, Yllarius 2026-08-10). */
int g_skillMaxLevel = 20;

/* Tiered rewards land at a depth that matches their tier. Standalone applies
 * the bands directly in the reward roll; the AP side does it in pre_fill. */
BOOL g_tieredFillerDepth = TRUE;

/* Item investment level/STR/DEX requirement toggle (Maegis #2 second half). */
BOOL g_itemLevelReqs = TRUE;
/* 2.x — split from g_itemLevelReqs so LEVEL and STAT (str/dex) requirements toggle independently. */
BOOL g_itemStatsReqs = TRUE;

/* settings are "frozen at character creation" per user spec. */
static BOOL g_settingsFrozen    = FALSE;

/* Gated zone-locking preload selection per (act, difficulty). */
static int g_actPreload[5][3] = {
    {0, 0, 0},  /* Act 1: Normal/NM/Hell preload ID */
    {0, 0, 0},  /* Act 2 */
    {0, 0, 0},  /* Act 3 */
    {0, 0, 0},  /* Act 4 */
    {0, 0, 0}   /* Act 5 */
};

/* Goal quest IDs corresponding to goal_scope values */
static const int g_goalQuestIds[] = {
    6,    /* 0 = Andariel (quest id 6: Sisters to the Slaughter) */
    106,  /* 1 = Duriel (quest id 106: Seven Tombs) */
    206,  /* 2 = Mephisto (quest id 206: The Guardian) */
    303,  /* 3 = Diablo (quest id 303: Terror's End) */
    406   /* 4 = Baal (quest id 406: Eve of Destruction) */
};
static const char* g_goalBossNames[] = {
    "Andariel", "Duriel", "Mephisto", "Diablo", "Baal"
};

/* Scroll state */
static int g_editorScroll   = 0;  /* scroll for center panel (slots/locked) */
static int g_questPageScroll = 0; /* scroll for quest book page */
static int g_availScroll    = 0;  /* scroll for left available panel */
static int g_questLogScroll = 0;
static int g_mouseWheel     = 0;

/* Tooltip state (used by skill tree + editor) */
static int g_tooltipSkillId = -1;
static int g_tooltipMX = 0, g_tooltipMY = 0;

/* Editor tabs */
static int g_editorTab = 0;  /* 0=Tab1, 1=Tab2, 2=Tab3, 3=Locked */

/* Notification system */
static volatile wchar_t g_notifyText[256] = {0};
static DWORD   g_notifyTime = 0;

/* "Big warning" notification: large red centered text used to signal that something special has spawned in the current area (Treasure Cow, future custom-boss variants). */
static volatile wchar_t g_bigWarnText[256] = {0};
static DWORD   g_bigWarnTime = 0;

