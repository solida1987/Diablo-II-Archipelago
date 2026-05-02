
static void LoadSkillTreeAssets(void) {
    char dc6Path[MAX_PATH];

    if (g_sklTreeLoaded) return;
    g_sklTreeLoaded = TRUE;

    /* Load skill tree background DC6 from disk using D2CMP_CelFileNormalize.
     * This is the same path D2Win's ARCHIVE_LoadCellFileWithFileSize uses:
     *   1. Read raw DC6 file into memory
     *   2. Call D2CMP ordinal 10024 (CelFileNormalize) to resolve pointers
     *   3. Use the resulting D2CellFileStrc with D2Gfx ordinal 10072 (CelDraw)
     *
     * Place DC6 files in: <Game.exe dir>\data\global\ui\SPELLS\
     */

    if (BuildDC6Path(dc6Path, MAX_PATH, "skltree_a_back.DC6")) {
        g_sklTreeBg = LoadDC6FromDisk(dc6Path);
        Log("SkillTree: bg=%p from disk\n", g_sklTreeBg);
    }

    if (BuildDC6Path(dc6Path, MAX_PATH, "Skillicon.DC6")) {
        g_sklIconFile = LoadDC6FromDisk(dc6Path);
        Log("SkillTree: icons=%p from disk\n", g_sklIconFile);
    }

    if (!g_sklTreeBg) {
        Log("SkillTree: WARNING - background DC6 not loaded, will use fallback rectangles\n");
    }
}

/* Draw a loaded cel file using D2Gfx Ordinal 10072 (CelDraw).
 * Uses the exact same struct pattern as vanilla D2Client code. */
static void DrawCel(void* pCelFile, int frame, int x, int y) {
    DWORD gfx[0x12];
    int i;
    /* Get D2Gfx CelDraw (ordinal 10072) */
    static void* s_fnGfxCelDraw = NULL;
    if (!s_fnGfxCelDraw) {
        HMODULE hGfx = GetModuleHandleA("D2Gfx.dll");
        if (hGfx) s_fnGfxCelDraw = (void*)GetProcAddress(hGfx, (LPCSTR)10072);
    }
    if (!s_fnGfxCelDraw || !pCelFile) return;

    /* Build D2GfxDataStrc exactly like vanilla D2Client does */
    for (i = 0; i < 0x12; i++) gfx[i] = 0;
    gfx[1] = (DWORD)pCelFile;  /* offset 0x04: pCellFile */
    gfx[2] = (DWORD)frame;     /* offset 0x08: nFrame */
    gfx[3] = 0;                /* offset 0x0C: nDirection */

    __try {
        /* __stdcall: CelDraw(pData, x, y, gamma, drawMode, palette) */
        typedef void (__stdcall *GfxCelDraw_t)(void* pData, int x, int y, DWORD gamma, int drawMode, int palette);
        ((GfxCelDraw_t)s_fnGfxCelDraw)(gfx, x, y, 0xFFFFFFFF, 5, 0);
    } __except(1) {
        Log("DrawCel: CRASHED frame=%d x=%d y=%d\n", frame, x, y);
    }
}

/* CustomSkillTreeRender: Draws our 5x6 skill grid over vanilla's panel area.
 * Called from DrawAll when vanilla's skill tree panel flag is set.
 * Vanilla's render calls are NOPed so only our content is visible. */
static void CustomSkillTreeRender(void) {
    int row, col, slotIdx, sx, sy, tab, slot;
    int poolIdx, skillPts;
    wchar_t wbuf[64];

    if (!fnText || !fnFont || !fnRect) return;

    /* Panel position inside vanilla's stone panel frame.
     * Vanilla skill tree panel: left edge at ~x=340 in 800x600, width ~320, height ~432.
     * The stone frame is still drawn by vanilla (we only NOPed content, not frame). */
    int PX = 350;   /* inside the gold border */
    int PY = 10;
    int PW = 300;   /* within the frame */
    int PH = 420;

    /* Grid layout */
    int COLS = 5, ROWS = 6;
    int SLOT_W = 48, SLOT_H = 48;
    int MARGIN_X = 20, MARGIN_TOP = 36;
    int GAP_X = (PW - 2*MARGIN_X - COLS*SLOT_W) / (COLS > 1 ? COLS-1 : 1);
    int GAP_Y = (PH - MARGIN_TOP - 50 - ROWS*SLOT_H) / (ROWS > 1 ? ROWS-1 : 1);
    if (GAP_X < 2) GAP_X = 2;
    if (GAP_Y < 2) GAP_Y = 2;

    /* === TITLE === */
    fnFont(1);
    fnText(L"TALENT TREE", PX + PW/2, PY + 22, 4, 1); /* Gold, centered */

    /* === SKILL SLOTS 5x6 GRID === */
    for (row = 0; row < ROWS; row++) {
        for (col = 0; col < COLS; col++) {
            slotIdx = row * COLS + col;
            sx = PX + MARGIN_X + col * (SLOT_W + GAP_X);
            sy = PY + MARGIN_TOP + row * (SLOT_H + GAP_Y);
            tab = slotIdx / 10;
            slot = slotIdx % 10;
            poolIdx = -1;
            if (tab < 3 && slot < 10) poolIdx = g_tabSlots[tab][slot];

            /* Draw slot background (darker square) */
            fnRect(sx, sy, sx+SLOT_W, sy+SLOT_H, 0x11, 5);

            if (poolIdx >= 0 && poolIdx < g_poolCount) {
                int dbIdx = g_pool[poolIdx].dbIndex;
                if (dbIdx < 0 || dbIdx >= (int)SKILL_DB_COUNT) continue;
                const SkillEntry* sk = &g_skillDB[dbIdx];

                /* Skill name centered in slot */
                if (sk->name && sk->name[0]) {
                    MultiByteToWideChar(0, 0, sk->name, -1, wbuf, 31);
                    wbuf[7] = 0; /* truncate for space */
                    fnFont(6);
                    fnText(wbuf, sx + SLOT_W/2, sy + SLOT_H/2 + 4, 0, 1); /* white, centered */
                }
            }

            /* Tier labels at row boundaries */
            if (col == COLS-1) {
                int tier = (row < 2) ? 1 : (row < 4) ? 2 : 3;
                wchar_t tlbl[16];
                wsprintfW(tlbl, L"T%d", tier);
                fnFont(6);
                fnText(tlbl, sx + SLOT_W + 3, sy + SLOT_H/2 + 4, 1, 0); /* red */
            }
        }
    }

    /* === BOTTOM BAR === */
    int barY = PY + PH - 28;
    skillPts = 0;
    if (fnGetStat && Player()) {
        __try { skillPts = fnGetStat(Player(), 5, 0); } __except(1) {}
    }
    wsprintfW(wbuf, L"Skill Points: %d", skillPts);
    fnFont(0);
    fnText(wbuf, PX + 10, barY, 4, 0); /* gold */
}

/* SkillTreeHook trampoline removed — we render from DrawAll instead */

/* Tooltip state — declared near top with other globals */

/* Skill data from skill_data.dat */
#define SFD_MAX_SYN 6   /* D2R 3.0 has skills with up to 5 synergies (Ice Bolt) */
typedef struct {
    int id;
    char name[40], cls[4], reqSkill[40], etype[8], synergy[256];
    int reqlevel, mana, lvlmana, minmana, manashift;
    int emin, eminlv[5], emax, emaxlv[5];
    int param8, hitshift;
    char desc[128];
    /* 1.9.0: per-synergy data parsed at load time. The new synergy
     * field format is "Name1:%1;Name2:%2;Name3:%3" — semicolon-
     * separated name:percent pairs. The legacy format
     * "(skill('X'.blvl)+...)*par8" still works (all synergies share
     * param8). When the new format is detected, synPct[i] holds the
     * per-synergy percentage; otherwise synPct[i] == 0 (caller falls
     * back to param8). */
    char synName[SFD_MAX_SYN][40];
    int  synPct[SFD_MAX_SYN];
    int  synCount;
} SkillFullData;
static SkillFullData g_sfd[400];
static BOOL g_sfdLoaded = FALSE;

static void LoadSkillFullData(void) {
    if (g_sfdLoaded) return;
    g_sfdLoaded = TRUE;
    memset(g_sfd, 0, sizeof(g_sfd));
    char path[MAX_PATH];
    GetArchDir(path, MAX_PATH);
    strcat(path, "skill_data.dat");
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[640];
    while (fgets(line, sizeof(line), f)) {
        int id; char nm[40]={0},cl[4]={0},rl[8]={0},rs[40]={0},mn[8]={0},lm[8]={0},mm[8]={0},ms[8]={0};
        char et[8]={0},em[8]={0},el1[8]={0},el2[8]={0},el3[8]={0},el4[8]={0},el5[8]={0};
        char ex[8]={0},xl1[8]={0},xl2[8]={0},xl3[8]={0},xl4[8]={0},xl5[8]={0};
        char syn[256]={0},p8[8]={0},hs[8]={0};
        if (sscanf(line, "%d|", &id) != 1) continue;
        if (id < 0 || id >= 400) continue;
        /* Parse pipe-separated fields */
        char* p = line; int field = 0;
        char* fields[27] = {0};
        fields[0] = p;
        for (int i = 0; p[i]; i++) {
            if (p[i] == '|') { p[i] = 0; field++; if (field < 27) fields[field] = p+i+1; }
            if (p[i] == '\n' || p[i] == '\r') p[i] = 0;
        }
        g_sfd[id].id = id;
        if (fields[1]) strncpy(g_sfd[id].name, fields[1], 39);
        if (fields[2]) strncpy(g_sfd[id].cls, fields[2], 3);
        if (fields[3]) g_sfd[id].reqlevel = atoi(fields[3]);
        if (fields[4]) strncpy(g_sfd[id].reqSkill, fields[4], 39);
        if (fields[5]) g_sfd[id].mana = atoi(fields[5]);
        if (fields[6]) g_sfd[id].lvlmana = atoi(fields[6]);
        if (fields[7]) g_sfd[id].minmana = atoi(fields[7]);
        if (fields[8]) g_sfd[id].manashift = atoi(fields[8]);
        if (fields[9]) strncpy(g_sfd[id].etype, fields[9], 7);
        if (fields[10]) g_sfd[id].emin = atoi(fields[10]);
        if (fields[11]) g_sfd[id].eminlv[0] = atoi(fields[11]);
        if (fields[12]) g_sfd[id].eminlv[1] = atoi(fields[12]);
        if (fields[13]) g_sfd[id].eminlv[2] = atoi(fields[13]);
        if (fields[14]) g_sfd[id].eminlv[3] = atoi(fields[14]);
        if (fields[15]) g_sfd[id].eminlv[4] = atoi(fields[15]);
        if (fields[16]) g_sfd[id].emax = atoi(fields[16]);
        if (fields[17]) g_sfd[id].emaxlv[0] = atoi(fields[17]);
        if (fields[18]) g_sfd[id].emaxlv[1] = atoi(fields[18]);
        if (fields[19]) g_sfd[id].emaxlv[2] = atoi(fields[19]);
        if (fields[20]) g_sfd[id].emaxlv[3] = atoi(fields[20]);
        if (fields[21]) g_sfd[id].emaxlv[4] = atoi(fields[21]);
        if (fields[22]) strncpy(g_sfd[id].synergy, fields[22], 255);
        if (fields[23]) g_sfd[id].param8 = atoi(fields[23]);
        if (fields[24]) g_sfd[id].hitshift = atoi(fields[24]);
        if (fields[25]) strncpy(g_sfd[id].desc, fields[25], 127);

        /* Parse synergy field. New format: "Name1:%1;Name2:%2;...".
         * Old format: "(skill('X'.blvl)+skill('Y'.blvl))*par8".
         * Detect new format by checking for ':' WITHOUT 'skill('. */
        g_sfd[id].synCount = 0;
        const char* synStr = g_sfd[id].synergy;
        if (synStr[0]) {
            BOOL isNew = (strchr(synStr, ':') != NULL && strstr(synStr, "skill(") == NULL);
            if (isNew) {
                /* Parse "Name:pct;Name:pct;..." */
                char buf[256];
                strncpy(buf, synStr, 255); buf[255] = 0;
                char* tok = buf;
                while (tok && *tok && g_sfd[id].synCount < SFD_MAX_SYN) {
                    char* sep = strchr(tok, ';');
                    if (sep) *sep = 0;
                    char* col = strchr(tok, ':');
                    if (col) {
                        *col = 0;
                        char* nm = tok;
                        /* trim leading whitespace */
                        while (*nm == ' ' || *nm == '\t') nm++;
                        char* end = nm + strlen(nm);
                        while (end > nm && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
                        if (*nm) {
                            strncpy(g_sfd[id].synName[g_sfd[id].synCount], nm, 39);
                            g_sfd[id].synName[g_sfd[id].synCount][39] = 0;
                            g_sfd[id].synPct[g_sfd[id].synCount] = atoi(col + 1);
                            g_sfd[id].synCount++;
                        }
                    }
                    tok = sep ? sep + 1 : NULL;
                }
            } else {
                /* Old format — extract names via skill('Name'.blvl) and
                 * leave pct=0 so renderer falls back to param8. */
                const char* p = synStr;
                while (g_sfd[id].synCount < SFD_MAX_SYN) {
                    const char* start = strstr(p, "skill('");
                    if (!start) break;
                    start += 7;
                    const char* end = strstr(start, "'");
                    if (!end || end - start >= 39) break;
                    int n = (int)(end - start);
                    memcpy(g_sfd[id].synName[g_sfd[id].synCount], start, n);
                    g_sfd[id].synName[g_sfd[id].synCount][n] = 0;
                    g_sfd[id].synPct[g_sfd[id].synCount] = 0;  /* fallback to param8 */
                    g_sfd[id].synCount++;
                    p = end + 1;
                }
            }
        }
    }
    fclose(f);
    Log("LoadSkillFullData: loaded from %s\n", path);
}

/* Calculate damage at specific level */
static void CalcSkillDamage(SkillFullData* sd, int level, int* outMin, int* outMax) {
    if (level <= 0) { *outMin = sd->emin; *outMax = sd->emax; return; }
    int mn = sd->emin, mx = sd->emax;
    for (int lv = 2; lv <= level; lv++) {
        int idx;
        if (lv <= 8) idx = 0;
        else if (lv <= 16) idx = 1;
        else if (lv <= 22) idx = 2;
        else if (lv <= 28) idx = 3;
        else idx = 4;
        mn += sd->eminlv[idx];
        mx += sd->emaxlv[idx];
    }
    *outMin = mn; *outMax = mx;
}

/* Parse synergy skill names from formula like "(skill('Fire Bolt'.blvl)+skill('Meteor'.blvl))*par8" */
static int ParseSynergyNames(const char* formula, char names[][40], int maxNames) {
    int count = 0;
    const char* p = formula;
    while (p && count < maxNames) {
        const char* start = strstr(p, "skill('");
        if (!start) break;
        start += 7;
        const char* end = strstr(start, "'");
        if (!end || end - start >= 39) break;
        memcpy(names[count], start, end - start);
        names[count][end - start] = 0;
        count++;
        p = end + 1;
    }
    return count;
}

static void DrawSkillTooltip(int skillId, int mx, int my) {
    if (!fnText || !fnFont || skillId < 0 || skillId >= 400) return;
    LoadSkillFullData();

    SkillFullData* sd = &g_sfd[skillId];
    if (!sd->name[0]) return;

    /* Find in our DB for tier */
    int tier = 0;
    for (int i = 0; i < SKILL_DB_COUNT; i++) {
        if (g_skillDB[i].id == skillId) { tier = g_skillDB[i].tier; break; }
    }

    /* Get current level from our save files */
    int level = 0;
    /* Get skill level from dynamic assignments (g_tabSlots) */
    if (g_charName[0]) {
        int slotIdx = 0;
        for (int t = 0; t < 3; t++) {
            for (int s = 0; s < 10; s++, slotIdx++) {
                int pi = g_tabSlots[t][s];
                if (pi >= 0 && g_skillDB[g_pool[pi].dbIndex].id == skillId) {
                    /* Found this skill in slot — read saved level */
                    char lvlPath[MAX_PATH], sfx[32];
                    GetCharFileDir(lvlPath, MAX_PATH);
                    if (slotIdx == 0) strcat(lvlPath, "d2arch_fireball_");
                    else { sprintf(sfx, "d2arch_skill%d_", slotIdx + 1); strcat(lvlPath, sfx); }
                    strcat(lvlPath, g_charName); strcat(lvlPath, ".dat");
                    FILE* lf = fopen(lvlPath, "r");
                    if (lf) { fscanf(lf, "%d", &level); fclose(lf); }
                    break;
                }
            }
            if (level > 0) break;
        }
    }

    /* Calculate scaled damage */
    int dMin = 0, dMax = 0;
    CalcSkillDamage(sd, level > 0 ? level : 1, &dMin, &dMax);
    /* Apply hitshift correction: D2 displays damage >> (8 - hitshift) */
    if (sd->hitshift > 0 && sd->hitshift < 8) {
        int shift = 8 - sd->hitshift;
        dMin >>= shift;
        dMax >>= shift;
    }

    /* Calculate mana at level */
    int manaAtLvl = sd->mana;
    if (level > 1 && sd->lvlmana != 0) manaAtLvl = sd->mana + (level - 1) * sd->lvlmana;
    if (manaAtLvl < sd->minmana) manaAtLvl = sd->minmana;

    /* Use pre-parsed per-synergy data if available; otherwise fall
     * back to extracting names from the legacy formula. */
    int synCount = sd->synCount;

    /* Count lines */
    int lines = 2; /* name + class */
    lines++; /* level line */
    if (sd->desc[0]) lines++;
    if (sd->etype[0] && (dMin > 0 || dMax > 0)) lines++;
    if (manaAtLvl > 0) lines++;
    if (sd->reqlevel > 0) lines++;
    if (sd->reqSkill[0]) lines++;
    if (synCount > 0) lines += synCount + 1;

    int tw = 300, th = 14 + lines * 18;
    /* Position tooltip near mouse, clamped to screen */
    int tx = mx - tw / 2;
    if (tx < 10) tx = 10;
    if (tx + tw > g_screenW - 10) tx = g_screenW - tw - 10;
    int ty = my - th - 10;
    if (ty < 10) ty = my + 30;
    if (ty + th > g_screenH - 10) ty = g_screenH - th - 10;

    /* Background */
    if (fnRect) fnRect(tx, ty, tx+tw, ty+th, 0, 1);

    int cy = ty+10, lx = tx+10;
    wchar_t wb[256]; char buf[128];

    /* Name (gold) */
    MultiByteToWideChar(0,0,sd->name,-1,wb,128);
    fnFont(0); fnText(wb, lx, cy, 4, 0); cy += 18;

    /* Class + tier */
    { const char* cn=sd->cls;
      if(!strcmp(cn,"ama"))cn="Amazon";else if(!strcmp(cn,"sor"))cn="Sorceress";
      else if(!strcmp(cn,"nec"))cn="Necromancer";else if(!strcmp(cn,"pal"))cn="Paladin";
      else if(!strcmp(cn,"bar"))cn="Barbarian";else if(!strcmp(cn,"dru"))cn="Druid";
      else if(!strcmp(cn,"ass"))cn="Assassin";
      sprintf(buf,"%s - Tier %d",cn,tier);
      MultiByteToWideChar(0,0,buf,-1,wb,128);
      fnFont(0); fnText(wb, lx, cy, 0, 0); cy += 18; }

    /* Level */
    if (level > 0) {
        wsprintfW(wb, L"Current Skill Level: %d", level);
        fnFont(0); fnText(wb, lx, cy, 2, 0); cy += 18;
    } else {
        fnFont(0); fnText(L"Not yet learned", lx, cy, 1, 0); cy += 18;
    }

    /* Description (for passives, summons, auras, etc.) */
    if (sd->desc[0]) {
        MultiByteToWideChar(0,0,sd->desc,-1,wb,256);
        fnFont(0); fnText(wb, lx, cy, 0, 0); cy += 18;
    }

    /* Elemental damage */
    if (sd->etype[0] && (dMin > 0 || dMax > 0)) {
        int ec=0; char en[16]={0};
        if(!strcmp(sd->etype,"fire")){ec=1;strcpy(en,"Fire");}
        else if(!strcmp(sd->etype,"ltng")){ec=9;strcpy(en,"Lightning");}
        else if(!strcmp(sd->etype,"cold")){ec=3;strcpy(en,"Cold");}
        else if(!strcmp(sd->etype,"pois")){ec=2;strcpy(en,"Poison");}
        else if(!strcmp(sd->etype,"mag")){ec=7;strcpy(en,"Magic");}
        else{ec=0;strncpy(en,sd->etype,15);}
        sprintf(buf, "%s Damage: %d-%d", en, dMin, dMax);
        MultiByteToWideChar(0,0,buf,-1,wb,128);
        fnFont(0); fnText(wb, lx, cy, ec, 0); cy += 18;
    }

    /* Mana cost */
    if (manaAtLvl > 0) {
        wsprintfW(wb, L"Mana Cost: %d", manaAtLvl);
        fnFont(0); fnText(wb, lx, cy, 7, 0); cy += 18;
    }

    /* Required level */
    if (sd->reqlevel > 0) {
        wsprintfW(wb, L"Required Level: %d", sd->reqlevel);
        fnFont(0); fnText(wb, lx, cy, 0, 0); cy += 18;
    }

    /* Required skill */
    if (sd->reqSkill[0]) {
        sprintf(buf, "Requires: %s", sd->reqSkill);
        MultiByteToWideChar(0,0,buf,-1,wb,128);
        fnFont(0); fnText(wb, lx, cy, 0, 0); cy += 18;
    }

    /* Synergies — show name + per-synergy % (D2R format).
     * If per-synergy pct is 0 (legacy data), fall back to param8. */
    if (synCount > 0) {
        fnFont(0); fnText(L"Receives bonuses from:", lx, cy, 4, 0); cy += 18;
        for (int s = 0; s < synCount; s++) {
            int pct = sd->synPct[s] > 0 ? sd->synPct[s] : sd->param8;
            if (pct > 0) {
                sprintf(buf, "  %s: +%d%% per Level", sd->synName[s], pct);
            } else {
                sprintf(buf, "  %s", sd->synName[s]);
            }
            MultiByteToWideChar(0,0,buf,-1,wb,256);
            fnFont(0); fnText(wb, lx, cy, 2, 0); cy += 18;
        }
    }
}

/* 1.9.1 — wheel-delta accumulator for the Ctrl+V dev menu's Loot tab
 * scrollable lists. WndProc (d2arch_main.c WM_MOUSEWHEEL) accumulates
 * raw delta here when g_cheatMenuOpen is true; the renderer drains it
 * each frame. Defined here (drawall.c is the LAST .c included by the
 * unity TU) so a single definition exists. */
int g_cheatMenuWheelDelta = 0;

static void DrawAll(void) {
    /* Auto-hide D2Debugger window on first frame (it starts visible).
     * We keep trying until we find it, since it may take a few frames to appear. */
    {
        static BOOL dbgHidden = FALSE;
        if (!dbgHidden) {
            HWND hDbg = FindWindowA(NULL, "D2Debugger");
            if (!hDbg) hDbg = FindWindowA("D2Debugger", NULL);
            if (hDbg) {
                ShowWindow(hDbg, SW_HIDE);
                dbgHidden = TRUE;
                Log("D2Debugger window auto-hidden (use Shift+0 to show)\n");
            }
        }
    }

    void* player = Player();
    if (!player) {
        /* Player gone — save our data, then reset .d2s on disk so next load
         * starts with refunded skill points ready to redistribute. The game
         * has already saved the .d2s before player pointer went NULL, so
         * modifying here is safe — next load sees zeroed skills and points
         * back in the pool. */
        if (g_lastPlayerPtr != NULL) {
            Log("Player lost — saving and resetting .d2s\n");
            SaveSlots();
            SaveStateFile();
            WriteChecksFile();
            ResetD2SFile(g_charName);
            SaveStateFile();
            g_lastPlayerPtr = NULL;
        }
        return;
    }

    /* Update click state */
    UpdateClick();

    /* Keep g_savedClass updated */
    {
        int pc = GetPlayerClass();
        if (pc >= 0) g_savedClass = pc;
    }

    /* FIRST-FRAME: Detect new/changed player by BOTH pointer AND name */
    {
        static DWORD lastApplyPlayer = 0;
        static char lastDetectedName[32] = {0};
        DWORD p = (DWORD)player;

        char curName[32] = {0};
        if (p) {
            __try {
                void* pData = *(void**)((BYTE*)player + 0x14);
                if (pData) {
                    char* name = (char*)((BYTE*)pData + 0x00);
                    if (name[0]) strncpy(curName, name, 31);
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }

        BOOL pointerChanged = (p && p != lastApplyPlayer);
        BOOL nameChanged = (curName[0] && strcmp(curName, lastDetectedName) != 0);

        if (pointerChanged || nameChanged) {
            lastApplyPlayer = p;
            if (curName[0]) {
                strncpy(lastDetectedName, curName, 31);
                lastDetectedName[31] = 0;
                Log("Player detected: '%s' (was '%s') [ptr=%s name=%s]\n",
                    curName, g_charName,
                    pointerChanged ? "changed" : "same",
                    nameChanged ? "changed" : "same");
                strncpy(g_charName, curName, 31);
                g_charName[31] = 0;
            }
            g_shuffleApplied = FALSE;
            g_bossShuffleApplied = FALSE;
            OnCharacterLoad();
        }
    }

    /* === QUEST FLAG SCANNER — read-only, logs changes to d2arch_questscan.txt === */
    {
        static BOOL s_scanInit = FALSE;
        static void* s_scanQF = NULL;
        static BOOL s_scanQFFound = FALSE;
        /* Previous state for ALL 41 quest flags × 5 states = detect changes */
        static BOOL s_prevState[41][16];  /* [questFlagId][stateIdx] — ALL 16 states */
        static const int stateNums[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
        static const char* stateNames[] = { "S0_REWARD", "S1", "S2_STARTED", "S3", "S4", "S5", "S6", "S7", "S8", "S9", "S10", "S11", "S12", "S13_PRIMARY", "S14_NOW", "S15_BEFORE" };

        if (!s_scanInit && Player()) {
            s_scanInit = TRUE;
            memset(s_prevState, 0, sizeof(s_prevState));
            /* Use the shared quest pointer (refreshed by scanner above) */
            if (fnGetQuestState && g_questScanQF) {
                s_scanQF = g_questScanQF;
                s_scanQFFound = TRUE;
            }
            /* Write header to scan log */
            {
                char scanPath[MAX_PATH];
                GetModuleFileNameA(NULL, scanPath, MAX_PATH);
                char* sl = strrchr(scanPath, '\\'); if (sl) *(sl+1) = 0;
                strcat(scanPath, "d2arch_questscan.txt");
                FILE* sf = fopen(scanPath, "w");
                if (sf) {
                    fprintf(sf, "=== QUEST FLAG SCANNER ===\n");
                    fprintf(sf, "QF pointer: %08X (found=%d)\n", (DWORD)s_scanQF, s_scanQFFound);
                    fprintf(sf, "Scanning states: 0=REWARD, 2=STARTED, 13=PRIMARY, 14=NOW, 15=BEFORE\n");
                    fprintf(sf, "Monitoring quest flags 1-40 for ANY changes\n\n");
                    fclose(sf);
                }
            }
        }

        /* Poll every 500ms for changes */
        if (s_scanQFFound && s_scanQF && fnGetQuestState) {
            static DWORD s_scanTick = 0;
            DWORD now = GetTickCount();
            if (now - s_scanTick > 500) {
                s_scanTick = now;
                for (int qf = 1; qf <= 40; qf++) {
                    for (int si = 0; si < 16; si++) {
                        BOOL cur = FALSE;
                        __try { cur = fnGetQuestState(s_scanQF, qf, stateNums[si]); } __except(1) {}
                        if (cur != s_prevState[qf][si]) {
                            s_prevState[qf][si] = cur;
                            /* Log change to file */
                            char scanPath[MAX_PATH];
                            GetModuleFileNameA(NULL, scanPath, MAX_PATH);
                            char* sl = strrchr(scanPath, '\\'); if (sl) *(sl+1) = 0;
                            strcat(scanPath, "d2arch_questscan.txt");
                            FILE* sf = fopen(scanPath, "a");
                            if (sf) {
                                fprintf(sf, "[%08X] QuestFlag %2d  %s = %d\n",
                                    now, qf, stateNames[si], cur);
                                fclose(sf);
                            }
                        }
                    }
                }
            }
        }
    }

    /* 1.9.0 — INI hot-reload poll. Throttled to once per 500 ms
     * inside the function. Bumps g_iniGen when d2arch.ini changes,
     * which makes any renderer using INI_HOT_RELOAD_GUARD pick up
     * the new values on its next paint without a relog. */
    IniHotReload_Tick();

    /* Render UI layers */
    RenderNotify();
    RenderBigWarning();   /* 1.8.2: Treasure Cow / special-spawn red banner */
    RenderTracker();
    RenderMenu();
    RenderEditor();
    RenderQuestLog();
    RenderZoneTracker();

    /* 1.8.0 — Stash overlay (tab bar + slot grid). Render itself
     * short-circuits when the panel is closed; the F8 hotkey poll
     * inside still runs every frame so the overlay can be opened. */
    StashUIRender();
    {
        /* Throttled insertion scanner — roughly every 10 frames
         * (matches task spec). Cheap hash compare; the real merge
         * logic is still a scaffold (see d2arch_stashlogic.c). */
        static int s_stashScanCounter = 0;
        if (++s_stashScanCounter >= 10) {
            s_stashScanCounter = 0;
            StashLogicScanForInsertions();
        }
    }

    /* 1.8.0 — Version string replacement is done via in-memory format-string patch
     * at DLL init (see d2arch_versionpatch.c). This changes "v %d.%02d" inside
     * D2Client.dll and D2Launch.dll to "Beta 1.8.0", so D2's own sprintf
     * produces our string everywhere it renders — main menu, HUD, tab map.
     * No overdraw needed. */

    /* Verify RET patches are still in place */
    {
        static int verifyCount = 0;
        if (verifyCount == 0 && g_d2clientBase) {
            verifyCount = 1;
            DWORD addrs[] = { 0x381F0, 0x382E0, 0x38310, 0x38920, 0x3A0D0, 0x3A350 };
            for (int vi = 0; vi < 6; vi++) {
                BYTE* p = (BYTE*)(g_d2clientBase + addrs[vi]);
                __try {
                    Log("VERIFY RET 0x%X: %02X\n", addrs[vi], p[0]);
                } __except(1) {}
            }
        }
    }

    /* Reset skill panel state when character changes */
    {
        static DWORD s_lastPlayerPtr = 0;
        DWORD curPlayer = (DWORD)Player();
        if (curPlayer != s_lastPlayerPtr) {
            s_lastPlayerPtr = curPlayer;
            /* Reset all DC6 loading flags so they reload for new session */
            g_skillPanelReset = TRUE;
            g_editorNeedsReset = TRUE;
        }
    }

    /* Draw our skill tree panel when vanilla's panel is open.
     * Load DC6 directly from disk (bypasses MPQ) using LoadDC6FromDisk. */
    {
        static void* s_panelCel = NULL;
        static BOOL s_loadAttempted = FALSE;

        int panelOpen = 0;
        __try { panelOpen = *(int*)0x6FBBA6B8; } __except(1) {}

        /* Reset DC6 state on character change */
        if (g_skillPanelReset) {
            s_loadAttempted = FALSE;
            s_panelCel = NULL;
        }

        /* === RE-APPLY ALL SKILLS — DISABLED: reinvest in HookD2DebugGame handles this.
         * This block was double-spending skill points on top of reinvest. === */
        if (0) {
            static BOOL s_reapplyLoaded = FALSE;
            static BOOL s_reapplyDone = FALSE;
            static DWORD s_reapplyStart = 0;
            static BOOL s_reapplyPatched = FALSE;
            static int s_reapplyLevels[30];
            static int s_reapplySkills[30];

            if (g_skillPanelReset) {
                s_reapplyLoaded = FALSE;
                s_reapplyDone = FALSE;
                s_reapplyPatched = FALSE;
                s_reapplyStart = 0;
            }

            /* Load saved levels for all 30 skills — DYNAMIC from g_tabSlots */
            if (!s_reapplyLoaded && g_charName[0]) {
                s_reapplyLoaded = TRUE;
                memset(s_reapplyLevels, 0, sizeof(s_reapplyLevels));
                memset(s_reapplySkills, -1, sizeof(s_reapplySkills));
                /* Build skill ID list from current assignments */
                int idx = 0;
                for (int t = 0; t < 3; t++) {
                    for (int s = 0; s < 10; s++, idx++) {
                        int pi = g_tabSlots[t][s];
                        s_reapplySkills[idx] = (pi >= 0) ? g_skillDB[g_pool[pi].dbIndex].id : -1;
                    }
                }
                /* Load saved levels per slot */
                for (int n = 0; n < 30; n++) {
                    if (s_reapplySkills[n] < 0) continue;
                    char sp[MAX_PATH], sfx[32];
                    GetCharFileDir(sp, MAX_PATH);
                    if (n == 0) { strcat(sp, "d2arch_fireball_"); }
                    else { sprintf(sfx, "d2arch_skill%d_", n + 1); strcat(sp, sfx); }
                    strcat(sp, g_charName); strcat(sp, ".dat");
                    FILE* f = fopen(sp, "r");
                    if (f) { fscanf(f, "%d", &s_reapplyLevels[n]); fclose(f); }
                }
            }

            /* Patch + invest (no need to open skill tree) */
            if (!s_reapplyDone && s_reapplyLoaded && Player()) {
                BOOL hasAny = FALSE;
                for (int n = 0; n < 30; n++) if (s_reapplySkills[n] >= 0 && s_reapplyLevels[n] > 0) { hasAny = TRUE; break; }

                if (hasAny) {
                    if (!s_reapplyPatched) {
                        s_reapplyPatched = TRUE;
                        s_reapplyStart = GetTickCount();
                        for (int n = 0; n < 30; n++) {
                            if (s_reapplySkills[n] >= 0 && s_reapplyLevels[n] > 0) {
                                __try { PatchSkillForPlayer(s_reapplySkills[n]); InsertSkillInClassList(s_reapplySkills[n]); } __except(1) {}
                            }
                        }
                    }
                    if (s_reapplyPatched && GetTickCount() - s_reapplyStart >= 500) {
                        s_reapplyDone = TRUE;
                        for (int n = 0; n < 30; n++) {
                            if (s_reapplySkills[n] >= 0 && s_reapplyLevels[n] > 0) {
                                __try {
                                    PatchSkillForPlayer(s_reapplySkills[n]);
                                    InsertSkillInClassList(s_reapplySkills[n]);
                                    for (int p = 0; p < s_reapplyLevels[n]; p++) SendSpendSkillPacket(s_reapplySkills[n]);
                                } __except(1) {}
                            }
                        }
                        Log("REAPPLY: all skills re-invested for '%s'\n", g_charName);
                    }
                } else {
                    s_reapplyDone = TRUE;
                }
            }
        }

        if (panelOpen) {
            /* Load our custom panel DC6 from disk */
            if (!s_loadAttempted) {
                s_loadAttempted = TRUE;
                char dc6Path[MAX_PATH];
                if (BuildDC6Path(dc6Path, MAX_PATH, "skltree_custom.DC6")) {
                    s_panelCel = LoadDC6FromDisk(dc6Path);
                    Log("SKILL PANEL: loaded %p from %s\n", s_panelCel, dc6Path);
                }
            }

            /* Draw 2 panel halves (top + bottom).
             * Positions configurable via d2arch.ini:
             *   PanelTopX, PanelTopY, PanelBotX, PanelBotY */
            if (fnCelDraw) {
                static void* s_topCel = NULL;
                static void* s_botCel = NULL;
                static BOOL s_halvesLoaded = FALSE;
                static int s_topX = 320, s_topY = 216;

                if (g_skillPanelReset) {
                    s_halvesLoaded = FALSE;
                    s_topCel = NULL;
                    s_botCel = NULL;
                }
                static int s_botX = 320, s_botY = 432;

                if (!s_halvesLoaded) {
                    s_halvesLoaded = TRUE;
                    /* Use fnCelLoad (D2Win 10039) - same as button loading.
                     * Path is relative to game dir, without extension. */
                    if (fnCelLoad) {
                        s_topCel = fnCelLoad("data\\global\\ui\\SPELLS\\skltree_top", 0);
                        Log("SKILL PANEL TOP: %p\n", s_topCel);
                        s_botCel = fnCelLoad("data\\global\\ui\\SPELLS\\skltree_bot", 0);
                        Log("SKILL PANEL BOT: %p\n", s_botCel);
                    }
                    /* Read positions from INI */
                    char ini[MAX_PATH], buf[32];
                    GetArchDir(ini, MAX_PATH);
                    strcat(ini, "d2arch.ini");
                    GetPrivateProfileStringA("SkillPanel", "TopX", "320", buf, 31, ini);
                    s_topX = atoi(buf);
                    GetPrivateProfileStringA("SkillPanel", "TopY", "216", buf, 31, ini);
                    s_topY = atoi(buf);
                    GetPrivateProfileStringA("SkillPanel", "BotX", "320", buf, 31, ini);
                    s_botX = atoi(buf);
                    GetPrivateProfileStringA("SkillPanel", "BotY", "432", buf, 31, ini);
                    s_botY = atoi(buf);
                    Log("SKILL PANEL POS: top=(%d,%d) bot=(%d,%d)\n",
                        s_topX, s_topY, s_botX, s_botY);
                }

                __try {
                    if (s_topCel) fnCelDraw(s_topCel, s_topX, s_topY, 0, 5, -1);
                    if (s_botCel) fnCelDraw(s_botCel, s_botX, s_botY, 0, 5, -1);
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Log("SKILL PANEL: CelDraw CRASHED\n");
                }
            }

            /* Load icon DC6 + map (once, shared by all 30 buttons) */
            if (!g_archIconsLoaded || g_skillPanelReset) {
                g_archIconsLoaded = TRUE;
                char dc6p[MAX_PATH];
                GetArchDir(dc6p, MAX_PATH);
                strcat(dc6p, "ArchIcons40.DC6");
                g_archIcons = LoadDC6FromDisk(dc6p);
            }
            if (!g_icoMapLoaded || g_skillPanelReset) {
                g_icoMapLoaded = TRUE;
                memset(g_icoMap, -1, sizeof(g_icoMap));
                char mp[MAX_PATH];
                GetArchDir(mp, MAX_PATH);
                strcat(mp, "skill_icon_map.dat");
                FILE* mf = fopen(mp, "r");
                if (mf) {
                    char ln[64];
                    while (fgets(ln, sizeof(ln), mf)) {
                        int sid, frm;
                        if (sscanf(ln, "%d=%d", &sid, &frm) == 2 && sid >= 0 && sid < 400)
                            g_icoMap[sid] = frm;
                    }
                    fclose(mf);
                }
            }

            /* === CLOSE BUTTON + HIDE COLORS TOGGLE === */
            {
                static int s_closeBtnX = 0, s_closeBtnY = 0, s_closeBtnW = 60, s_closeBtnH = 24;
                static int s_closeTxtX = 0, s_closeTxtY = 0;
                static int s_hideBtnX = 0, s_hideBtnY = 0, s_hideBtnW = 90, s_hideBtnH = 24;
                static int s_hideTxtX = 0, s_hideTxtY = 0;
                static BOOL s_btnPosLoaded = FALSE;
                /* g_stHideColors declared globally below */

                if (g_skillPanelReset) s_btnPosLoaded = FALSE;

                if (!s_btnPosLoaded) {
                    s_btnPosLoaded = TRUE;
                    char ini4[MAX_PATH], buf4[32];
                    GetArchDir(ini4, MAX_PATH); strcat(ini4, "d2arch.ini");
                    GetPrivateProfileStringA("SkillPanel", "CloseX", "130", buf4, 31, ini4);
                    s_closeBtnX = atoi(buf4);
                    GetPrivateProfileStringA("SkillPanel", "CloseY", "555", buf4, 31, ini4);
                    s_closeBtnY = atoi(buf4);
                    GetPrivateProfileStringA("SkillPanel", "CloseW", "60", buf4, 31, ini4);
                    s_closeBtnW = atoi(buf4);
                    GetPrivateProfileStringA("SkillPanel", "CloseH", "24", buf4, 31, ini4);
                    s_closeBtnH = atoi(buf4);
                    GetPrivateProfileStringA("SkillPanel", "CloseTxtX", "0", buf4, 31, ini4);
                    s_closeTxtX = atoi(buf4);
                    GetPrivateProfileStringA("SkillPanel", "CloseTxtY", "0", buf4, 31, ini4);
                    s_closeTxtY = atoi(buf4);
                    /* Default text position = center of button */
                    if (s_closeTxtX == 0) s_closeTxtX = s_closeBtnX + s_closeBtnW / 2;
                    if (s_closeTxtY == 0) s_closeTxtY = s_closeBtnY + 17;

                    GetPrivateProfileStringA("SkillPanel", "HideX", "350", buf4, 31, ini4);
                    s_hideBtnX = atoi(buf4);
                    GetPrivateProfileStringA("SkillPanel", "HideY", "555", buf4, 31, ini4);
                    s_hideBtnY = atoi(buf4);
                    GetPrivateProfileStringA("SkillPanel", "HideW", "90", buf4, 31, ini4);
                    s_hideBtnW = atoi(buf4);
                    GetPrivateProfileStringA("SkillPanel", "HideH", "24", buf4, 31, ini4);
                    s_hideBtnH = atoi(buf4);
                    GetPrivateProfileStringA("SkillPanel", "HideTxtX", "0", buf4, 31, ini4);
                    s_hideTxtX = atoi(buf4);
                    GetPrivateProfileStringA("SkillPanel", "HideTxtY", "0", buf4, 31, ini4);
                    s_hideTxtY = atoi(buf4);
                    if (s_hideTxtX == 0) s_hideTxtX = s_hideBtnX + s_hideBtnW / 2;
                    if (s_hideTxtY == 0) s_hideTxtY = s_hideBtnY + 17;
                }

                /* Mouse for buttons */
                POINT _bpt; GetCursorPos(&_bpt);
                HWND _bhw = GetForegroundWindow();
                ScreenToClient(_bhw, &_bpt);
                RECT _brc; GetClientRect(_bhw, &_brc);
                int _bcw = _brc.right - _brc.left, _bch = _brc.bottom - _brc.top;
                float _bsx = (float)_bcw / (float)g_screenW, _bsy = (float)_bch / (float)g_screenH;
                float _bsc = (_bsx < _bsy) ? _bsx : _bsy;
                int _box = (int)((_bcw - (float)g_screenW * _bsc) / 2.0f);
                int _boy = (int)((_bch - (float)g_screenH * _bsc) / 2.0f);
                int bmx = (_bsc > 0) ? (int)((_bpt.x - _box) / _bsc) : _bpt.x;
                int bmy = (_bsc > 0) ? (int)((_bpt.y - _boy) / _bsc) : _bpt.y;

                /* Close button */
                if (fnRect && fnText && fnFont && s_closeBtnX > 0) {
                    int cx = s_closeBtnX, cy = s_closeBtnY;
                    int cw = s_closeBtnW, ch = s_closeBtnH;
                    BOOL hoverClose = (bmx >= cx && bmx < cx + cw && bmy >= cy && bmy < cy + ch);
                    for (int _d = 0; _d < 4; _d++)
                        fnRect(cx, cy, cx + cw, cy + ch, hoverClose ? 1 : 0, 5);
                    fnRect(cx, cy, cx + cw, cy + 1, 7, 5);
                    fnRect(cx, cy + ch - 1, cx + cw, cy + ch, 7, 5);
                    fnRect(cx, cy, cx + 1, cy + ch, 7, 5);
                    fnRect(cx + cw - 1, cy, cx + cw, cy + ch, 7, 5);
                    fnFont(0);
                    fnText(L"Close", s_closeTxtX, s_closeTxtY, hoverClose ? 0 : 5, 1);
                    /* Click — send Escape to close the entire panel */
                    if (hoverClose && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                        static DWORD _clk = 0; DWORD _cn = GetTickCount();
                        if (_cn - _clk > 300) { _clk = _cn;
                            if (g_gameHwnd)
                                PostMessageA(g_gameHwnd, WM_KEYDOWN, VK_ESCAPE, 0);
                        }
                    }
                }

                /* Hide colors toggle button */
                if (fnRect && fnText && fnFont && s_hideBtnX > 0) {
                    int hx = s_hideBtnX, hy = s_hideBtnY;
                    int hw = s_hideBtnW, hh = s_hideBtnH;
                    BOOL hoverHide = (bmx >= hx && bmx < hx + hw && bmy >= hy && bmy < hy + hh);
                    for (int _d = 0; _d < 4; _d++)
                        fnRect(hx, hy, hx + hw, hy + hh, hoverHide ? 7 : 0, 5);
                    fnRect(hx, hy, hx + hw, hy + 1, 7, 5);
                    fnRect(hx, hy + hh - 1, hx + hw, hy + hh, 7, 5);
                    fnRect(hx, hy, hx + 1, hy + hh, 7, 5);
                    fnRect(hx + hw - 1, hy, hx + hw, hy + hh, 7, 5);
                    fnFont(0);
                    fnText(g_stHideColors ? L"Show Colors" : L"Hide Colors", s_hideTxtX, s_hideTxtY, hoverHide ? 0 : 5, 1);
                    /* Click */
                    if (hoverHide && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                        static DWORD _hclk = 0; DWORD _hcn = GetTickCount();
                        if (_hcn - _hclk > 300) { _hclk = _hcn;
                            g_stHideColors = !g_stHideColors;
                            { char _ini[MAX_PATH]; GetArchDir(_ini, MAX_PATH); strcat(_ini, "d2arch.ini");
                              WritePrivateProfileStringA("settings", "show_tier_colors", g_stHideColors ? "0" : "1", _ini); }
                        }
                    }
                }
            }

            /* === DYNAMIC SKILL BUTTONS (replaces 30 hardcoded buttons) === */
            {
                static BOOL s_dynLoaded = FALSE;
                static int s_dynBtnX[30], s_dynBtnY[30];
                static int s_dynLblX[30], s_dynLblY[30];
                static int s_dynLevel[30];
                static BOOL s_dynApplied = FALSE;
                static DWORD s_dynReapplyStart = 0;
                static BOOL s_dynPatched = FALSE;
                static int s_spLblX2 = 555, s_spLblY2 = 460;

                /* Load tier-colored backgrounds for skill tree */
                static void* s_stGreen41 = NULL;
                static void* s_stBlue41 = NULL;
                static void* s_stOrange41 = NULL;
                static void* s_stRed41 = NULL;
                static void* s_stIcons35 = NULL;
                static BOOL s_stGfxLoaded = FALSE;

                if (g_skillPanelReset) {
                    s_stGfxLoaded = FALSE;
                    s_stGreen41 = NULL; s_stBlue41 = NULL; s_stOrange41 = NULL;
                    s_stRed41 = NULL; s_stIcons35 = NULL;
                }

                if (!s_stGfxLoaded) {
                    s_stGfxLoaded = TRUE;
                    if (fnCelLoad) {
                        s_stGreen41  = fnCelLoad("data\\global\\ui\\SPELLS\\green_square_41", 0);
                        s_stBlue41   = fnCelLoad("data\\global\\ui\\SPELLS\\blue_square_41", 0);
                        s_stOrange41 = fnCelLoad("data\\global\\ui\\SPELLS\\orange_square_41", 0);
                        s_stRed41    = fnCelLoad("data\\global\\ui\\SPELLS\\red_square", 0);
                    }
                    char dp[MAX_PATH]; GetArchDir(dp, MAX_PATH); strcat(dp, "ArchIcons35.DC6");
                    s_stIcons35 = LoadDC6FromDisk(dp);
                }

                if (g_skillPanelReset) {
                    s_dynApplied = FALSE;
                    s_dynPatched = FALSE;
                    s_dynReapplyStart = 0;
                    /* Force full reload: clear loaded flag AND level data */
                    s_dynLoaded = FALSE;
                    memset(s_dynLevel, 0, sizeof(s_dynLevel));
                    Log("PANEL RESET: cleared s_dynLoaded + s_dynLevel\n");
                }

                /* Load positions from INI + saved levels (must happen BEFORE reinvest check) */
                if (!s_dynLoaded && g_charName[0]) {
                    s_dynLoaded = TRUE;
                    Log("PANEL: loading from files\n");
                    char ini2[MAX_PATH], buf2[32], key2[16];
                    GetArchDir(ini2, MAX_PATH); strcat(ini2, "d2arch.ini");

                    for (int n = 0; n < 30; n++) {
                        /* Skill 1 uses "BtnX", skills 2+ use "Btn2X" etc. */
                        if (n == 0) {
                            GetPrivateProfileStringA("TestButton", "BtnX", "0", buf2, 31, ini2);
                            s_dynBtnX[0] = atoi(buf2);
                            GetPrivateProfileStringA("TestButton", "BtnY", "0", buf2, 31, ini2);
                            s_dynBtnY[0] = atoi(buf2);
                            GetPrivateProfileStringA("TestButton", "LblX", "0", buf2, 31, ini2);
                            s_dynLblX[0] = atoi(buf2);
                            GetPrivateProfileStringA("TestButton", "LblY", "0", buf2, 31, ini2);
                            s_dynLblY[0] = atoi(buf2);
                        } else {
                            sprintf(key2, "Btn%dX", n + 1);
                            GetPrivateProfileStringA("TestButton", key2, "0", buf2, 31, ini2);
                            s_dynBtnX[n] = atoi(buf2);
                            sprintf(key2, "Btn%dY", n + 1);
                            GetPrivateProfileStringA("TestButton", key2, "0", buf2, 31, ini2);
                            s_dynBtnY[n] = atoi(buf2);
                            sprintf(key2, "Lbl%dX", n + 1);
                            GetPrivateProfileStringA("TestButton", key2, "0", buf2, 31, ini2);
                            s_dynLblX[n] = atoi(buf2);
                            sprintf(key2, "Lbl%dY", n + 1);
                            GetPrivateProfileStringA("TestButton", key2, "0", buf2, 31, ini2);
                            s_dynLblY[n] = atoi(buf2);
                        }
                    }
                    GetPrivateProfileStringA("TestButton", "SpLblX", "555", buf2, 31, ini2);
                    s_spLblX2 = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "SpLblY", "460", buf2, 31, ini2);
                    s_spLblY2 = atoi(buf2);

                    /* Load saved skill levels from reinvest files */
                    for (int n = 0; n < 30; n++) {
                        char sp[MAX_PATH], sfx[32];
                        GetCharFileDir(sp, MAX_PATH);
                        if (n == 0) strcat(sp, "d2arch_fireball_");
                        else { sprintf(sfx, "d2arch_skill%d_", n + 1); strcat(sp, sfx); }
                        strcat(sp, g_charName); strcat(sp, ".dat");
                        FILE* f = fopen(sp, "r");
                        if (f) { fscanf(f, "%d", &s_dynLevel[n]); fclose(f); }
                    }
                    Log("PANEL: file load done. s_dynLevel[0]=%d tabSlots[0][0]=%d\n",
                        s_dynLevel[0], g_tabSlots[0][0]);
                }

                /* Copy reinvested levels from global buffer (loaded in SafeDraw).
                 * This runs AFTER file loading so reinvest always takes priority.
                 * Also runs every frame so late reinvest completion still works. */
                if (g_reinvestLevelsReady) {
                    g_reinvestLevelsReady = FALSE;
                    memcpy(s_dynLevel, g_reinvestLevels, sizeof(s_dynLevel));
                    Log("PANEL: reinvest levels applied to s_dynLevel[0]=%d\n", s_dynLevel[0]);
                }

                /* Get player class for tier calculation */
                int playerCls2 = GetPlayerClass();
                if (playerCls2 < 0) playerCls2 = 0;

                /* Dynamic skill button loop: 30 buttons, original tab/slot order.
                 * Tier color determined per-slot via SlotTier (matches editor). */
                int btnIdx = 0;
                for (int tab = 0; tab < 3; tab++) {
                    for (int slot = 0; slot < 10; slot++, btnIdx++) {
                        int bx = s_dynBtnX[btnIdx];
                        int by = s_dynBtnY[btnIdx];
                        if (bx == 0 && by == 0) continue; /* hidden/unconfigured */

                        int tier = (btnIdx < 10) ? 1 : (btnIdx < 20) ? 2 : 3;
                        int poolIdx = g_tabSlots[tab][slot];
                        int skillId = (poolIdx >= 0) ? g_skillDB[g_pool[poolIdx].dbIndex].id : -1;
                        int bTop = by - 41;

                        /* Draw tier-colored background (unless hidden) */
                        if (!g_stHideColors) {
                            void* bgCel = NULL;
                            if (tier == 1 && s_stGreen41)  bgCel = s_stGreen41;
                            else if (tier == 2 && s_stBlue41)   bgCel = s_stBlue41;
                            else if (tier == 3 && s_stOrange41) bgCel = s_stOrange41;
                            __try {
                                if (bgCel && fnCelDraw)
                                    fnCelDraw(bgCel, bx, by, 0, 5, -1);
                            } __except(1) {}
                        }

                        /* Draw skill icon (35x35) if assigned, centered on 41x41 */
                        if (skillId >= 0 && s_stIcons35) {
                            int dbIdx = g_pool[poolIdx].dbIndex;
                            __try {
                                DrawCel(s_stIcons35, dbIdx, bx + 3, by - 3);
                            } __except(1) {}
                        }

                        /* Skill level label */
                        if (fnText && fnFont && s_dynLblX[btnIdx] > 0) {
                            int lvl = s_dynLevel[btnIdx];
                            wchar_t lvlBuf[8];
                            wsprintfW(lvlBuf, L"%d", lvl);
                            fnFont(1);
                            fnText(lvlBuf, s_dynLblX[btnIdx], s_dynLblY[btnIdx], 0, 0);
                        }

                        /* Hover + click — use same mouse method as old working buttons */
                        {
                            POINT _pt; GetCursorPos(&_pt);
                            HWND _hw = GetForegroundWindow();
                            ScreenToClient(_hw, &_pt);
                            RECT _rc; GetClientRect(_hw, &_rc);
                            int _cw = _rc.right - _rc.left, _ch = _rc.bottom - _rc.top;
                            float _sx = (float)_cw / (float)g_screenW;
                            float _sy = (float)_ch / (float)g_screenH;
                            float _sc = (_sx < _sy) ? _sx : _sy;
                            int _ox = (int)((_cw - (float)g_screenW * _sc) / 2.0f);
                            int _oy = (int)((_ch - (float)g_screenH * _sc) / 2.0f);
                            int mx3 = (_sc > 0) ? (int)((_pt.x - _ox) / _sc) : _pt.x;
                            int my3 = (_sc > 0) ? (int)((_pt.y - _oy) / _sc) : _pt.y;
                            if (mx3 < 0) mx3 = 0;
                            if (my3 < 0) my3 = 0;

                            BOOL hover = (mx3 >= bx && mx3 < bx + 41 &&
                                          my3 >= bTop && my3 < by);

                            if (hover && skillId >= 0) {
                                g_tooltipSkillId = skillId;
                                g_tooltipMX = mx3;
                                g_tooltipMY = my3;
                            }

                            /* Click to spend skill point */
                            if (hover && skillId >= 0 && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                                static DWORD s_lastClickTime[30] = {0};
                                DWORD now3 = GetTickCount();
                                if (now3 - s_lastClickTime[btnIdx] > 500) {
                                    s_lastClickTime[btnIdx] = now3;
                                    /* Read from SERVER player for accurate count */
                                    int pts = 0;
                                    void* pSrvPl = (g_cachedPGame && fnGetStat) ? GetServerPlayer(g_cachedPGame) : NULL;
                                    if (pSrvPl) {
                                        __try { pts = fnGetStat(pSrvPl, 5, 0); } __except(1) {}
                                    }
                                    /* 1.8.0: respect Skills.txt reqlevel —
                                     * block investment if the character isn't
                                     * the required level yet. STAT_LEVEL = 12.
                                     * reqlevel is loaded into g_skillExtra[id]
                                     * by LoadSkillExtraInfo at char load. */
                                    int charLevel = 0;
                                    if (pSrvPl && fnGetStat) {
                                        __try { charLevel = fnGetStat(pSrvPl, 12, 0); } __except(1) {}
                                    }
                                    int reqLevel = (skillId >= 0 && skillId < 400)
                                        ? g_skillExtra[skillId].reqlevel : 0;
                                    /* 1.8.2 — Three-way mutually exclusive branch so the
                                     * "Required Level" message isn't immediately overwritten
                                     * by the "No skill points" fall-through. Order matters:
                                     * level check first so under-level players get the right
                                     * reason regardless of how many points they have. */
                                    BOOL levelBlocked = (reqLevel > 0 && charLevel > 0 && charLevel < reqLevel);
                                    if (levelBlocked) {
                                        char buf[96];
                                        _snprintf(buf, sizeof(buf),
                                            "You are not high enough level (need %d, you are %d)",
                                            reqLevel, charLevel);
                                        ShowNotify(buf);
                                        Log("SKILL INVEST BLOCKED: skill=%d charLvl=%d reqLvl=%d\n",
                                            skillId, charLevel, reqLevel);
                                    } else if (pts > 0) {
                                        __try {
                                            PatchSkillForPlayer(skillId);
                                            InsertSkillInClassList(skillId);
                                        } __except(1) {}
                                        /* SKILLS_AddSkill (D2Common #10952) on BOTH server + client.
                                         * Server: gives real persistent skill level.
                                         * Client: makes skill appear in right-click spell list. */
                                        {
                                            typedef void* (__stdcall *AddSkill_t)(void* pUnit, int nSkillId);
                                            static AddSkill_t fnAddSkill = NULL;
                                            if (!fnAddSkill && hD2Common) {
                                                fnAddSkill = (AddSkill_t)GetProcAddress(hD2Common, (LPCSTR)10952);
                                            }
                                            if (fnAddSkill) {
                                                /* Server player — real level */
                                                if (pSrvPl) {
                                                    __try { fnAddSkill(pSrvPl, skillId); } __except(1) {}
                                                }
                                                /* Client player — UI update */
                                                void* pCli = Player();
                                                if (pCli) {
                                                    __try { fnAddSkill(pCli, skillId); } __except(1) {}
                                                }
                                            }
                                            /* Deduct 1 skill point from server */
                                            if (pSrvPl && fnAddStat) {
                                                __try { fnAddStat(pSrvPl, 5, -1, 0); } __except(1) {}
                                            }
                                        }
                                        s_dynLevel[btnIdx]++;
                                        /* Save level */
                                        char sp[MAX_PATH], sfx[32];
                                        GetCharFileDir(sp, MAX_PATH);
                                        if (btnIdx == 0) strcat(sp, "d2arch_fireball_");
                                        else { sprintf(sfx, "d2arch_skill%d_", btnIdx + 1); strcat(sp, sfx); }
                                        strcat(sp, g_charName); strcat(sp, ".dat");
                                        FILE* sf = fopen(sp, "w");
                                        if (sf) { fprintf(sf, "%d", s_dynLevel[btnIdx]); fclose(sf); }
                                    } else {
                                        ShowNotify("No skill points available!");
                                    }
                                }
                            }
                        }
                    }
                }

                /* Skill Points available label — read from SERVER player for accuracy */
                if (fnText && fnFont && s_spLblX2 > 0) {
                    int sp = 0;
                    void* pSrvDisp = (g_cachedPGame && fnGetStat) ? GetServerPlayer(g_cachedPGame) : NULL;
                    if (pSrvDisp) {
                        __try { sp = fnGetStat(pSrvDisp, 5, 0); } __except(1) {}
                    }
                    wchar_t spBuf[32];
                    wsprintfW(spBuf, L"Skill Points: %d", sp);
                    fnFont(1);
                    fnText(spBuf, s_spLblX2, s_spLblY2, 0, 0);
                }
            }

            /* === END DYNAMIC SKILL BUTTONS === */

            /* KEEP OLD CODE BELOW COMMENTED OUT */
            if (0) /* === skill-1 === */
            {
                static BOOL s_fbLoaded = FALSE;
                static int s_btnX = 550, s_btnY = 150;
                static int s_lblX = 605, s_lblY = 140;
                static int s_skillLevel = 0;
                static BOOL s_fbApplied = FALSE;

                #define FIREBALL_SKILL_ID 47

                /* Reset on character change */
                if (g_skillPanelReset) {
                    s_fbLoaded = FALSE;
                    s_fbApplied = FALSE;
                    s_skillLevel = 0;
                }

                if (!s_fbLoaded) {
                    s_fbLoaded = TRUE;
                    char ini2[MAX_PATH], buf2[32];
                    GetArchDir(ini2, MAX_PATH);
                    strcat(ini2, "d2arch.ini");
                    GetPrivateProfileStringA("TestButton", "BtnX", "550", buf2, 31, ini2);
                    s_btnX = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "BtnY", "150", buf2, 31, ini2);
                    s_btnY = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "LblX", "605", buf2, 31, ini2);
                    s_lblX = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "LblY", "140", buf2, 31, ini2);
                    s_lblY = atoi(buf2);

                    /* Load saved fireball level from file */
                    if (g_charName[0]) {
                        char savePath[MAX_PATH];
                        GetCharFileDir(savePath, MAX_PATH);
                        strcat(savePath, "d2arch_fireball_");
                        strcat(savePath, g_charName);
                        strcat(savePath, ".dat");
                        FILE* sf = fopen(savePath, "r");
                        if (sf) {
                            fscanf(sf, "%d", &s_skillLevel);
                            fclose(sf);
                            Log("FIREBALL: loaded level=%d for '%s'\n", s_skillLevel, g_charName);
                        }
                    }
                }

                /* Re-apply Fireball to skill tree if saved level > 0 */
                if (!s_fbApplied && s_skillLevel > 0 && Player()) {
                    static DWORD s_fbWaitStart = 0;
                    static BOOL s_fbPatched = FALSE;

                    if (!s_fbPatched) {
                        s_fbPatched = TRUE;
                        s_fbWaitStart = GetTickCount();
                        __try {
                            PatchSkillForPlayer(FIREBALL_SKILL_ID);
                            InsertSkillInClassList(FIREBALL_SKILL_ID);
                            
                            Log("FIREBALL: patched + added to class list, waiting 2s\n");
                        } __except(1) { Log("FIREBALL: patch crashed\n"); }
                    }

                    if (s_fbPatched && s_fbWaitStart > 0 &&
                        GetTickCount() - s_fbWaitStart >= 2000) {
                        s_fbApplied = TRUE;
                        __try {
                            PatchSkillForPlayer(FIREBALL_SKILL_ID);
                            InsertSkillInClassList(FIREBALL_SKILL_ID);
                            
                            for (int i = 0; i < s_skillLevel; i++) {
                                SendSpendSkillPacket(FIREBALL_SKILL_ID);
                            }
                            Log("FIREBALL: re-invested %d points for '%s'\n", s_skillLevel, g_charName);
                        } __except(1) { Log("FIREBALL: re-invest crashed\n"); }
                    }
                }

                {
                    int bTop = s_btnY - 40;

                    /* Draw button: skill icon if assigned, red square if not */
                    {
                        int _fr = (FIREBALL_SKILL_ID >= 0 && FIREBALL_SKILL_ID < 400) ? g_icoMap[FIREBALL_SKILL_ID] : -1;
                        if (g_archIcons && _fr >= 0) {
                            __try { DrawCel(g_archIcons, _fr, s_btnX, s_btnY); } __except(1) {
                                if (fnRect) fnRect(s_btnX, bTop, s_btnX + 40, s_btnY, 1, 5);
                            }
                        } else {
                            if (fnRect) fnRect(s_btnX, bTop, s_btnX + 40, s_btnY, 1, 5);
                        }
                    }

                    /* Force correct IconCel for skill bar using universal map */
                    /* nIconCel is NOT patched — D2's vanilla UI uses the original value */

                    /* Draw label: skill level */
                    if (fnText && fnFont) {
                        wchar_t lvlBuf[8];
                        wsprintfW(lvlBuf, L"%d", s_skillLevel);
                        fnFont(1);
                        fnText(lvlBuf, s_lblX, s_lblY, 0, 0);
                    }

                    /* Get mouse position via Windows API + convert to game coords */
                    {
                        POINT pt;
                        GetCursorPos(&pt);
                        HWND hWnd = GetForegroundWindow();
                        ScreenToClient(hWnd, &pt);

                        RECT rc;
                        GetClientRect(hWnd, &rc);
                        int cw = rc.right - rc.left;
                        int ch = rc.bottom - rc.top;
                        int gameW = g_screenW, gameH = g_screenH;
                        float scaleX = (float)cw / (float)gameW;
                        float scaleY = (float)ch / (float)gameH;
                        float scale = (scaleX < scaleY) ? scaleX : scaleY;
                        int offX = (int)((cw - gameW * scale) / 2.0f);
                        int offY = (int)((ch - gameH * scale) / 2.0f);
                        int mx = (scale > 0) ? (int)((pt.x - offX) / scale) : pt.x;
                        int my = (scale > 0) ? (int)((pt.y - offY) / scale) : pt.y;
                        if (mx < 0) mx = 0;
                        if (my < 0) my = 0;

                        BOOL hover = (mx >= s_btnX && mx < s_btnX + 40 &&
                                      my >= bTop && my < s_btnY);

                        if (hover) { g_tooltipSkillId = FIREBALL_SKILL_ID; g_tooltipMX = mx; g_tooltipMY = my; }

                        if (hover && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                            static DWORD s_lastClick = 0;
                            DWORD now = GetTickCount();
                            if (now - s_lastClick > 500) {
                                s_lastClick = now;
                                int pts = 0;
                                if (fnGetStat && Player()) {
                                    __try { pts = fnGetStat(Player(), 5, 0); } __except(1) {}
                                }
                                if (pts > 0) {
                                    if (s_skillLevel == 0) {
                                        __try {
                                            PatchSkillForPlayer(FIREBALL_SKILL_ID);
                                            InsertSkillInClassList(FIREBALL_SKILL_ID);
                                            
                                        } __except(1) {}
                                    }
                                    __try { SendSpendSkillPacket(FIREBALL_SKILL_ID); } __except(1) {}
                                    s_skillLevel++;
                                    Log("FIREBALL: level=%d\n", s_skillLevel);
                                    if (g_charName[0]) {
                                        char savePath[MAX_PATH];
                                        GetCharFileDir(savePath, MAX_PATH);
                                        strcat(savePath, "d2arch_fireball_");
                                        strcat(savePath, g_charName);
                                        strcat(savePath, ".dat");
                                        FILE* sf = fopen(savePath, "w");
                                        if (sf) {
                                            fprintf(sf, "%d", s_skillLevel);
                                            fclose(sf);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

            if (0) { /* === DISABLED: all old hardcoded skills 2-30 === */
            /* === skill-2 === */
            {
                static BOOL s_s2Loaded = FALSE;
                static int s_s2btnX = 550, s_s2btnY = 150;
                static int s_s2lblX = 605, s_s2lblY = 140;
                static int s_s2Level = 0;
                static BOOL s_s2Applied = FALSE;

                #define SKILL2_ID 36

                if (g_skillPanelReset) {
                    s_s2Loaded = FALSE;
                    s_s2Applied = FALSE;
                    s_s2Level = 0;
                }

                if (!s_s2Loaded) {
                    s_s2Loaded = TRUE;
                    char ini2[MAX_PATH], buf2[32];
                    GetArchDir(ini2, MAX_PATH);
                    strcat(ini2, "d2arch.ini");
                    GetPrivateProfileStringA("TestButton", "Btn2X", "0", buf2, 31, ini2);
                    s_s2btnX = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "Btn2Y", "0", buf2, 31, ini2);
                    s_s2btnY = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "Lbl2X", "0", buf2, 31, ini2);
                    s_s2lblX = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "Lbl2Y", "0", buf2, 31, ini2);
                    s_s2lblY = atoi(buf2);

                    if (g_charName[0]) {
                        char savePath[MAX_PATH];
                        GetCharFileDir(savePath, MAX_PATH);
                        strcat(savePath, "d2arch_skill2_");
                        strcat(savePath, g_charName);
                        strcat(savePath, ".dat");
                        FILE* sf = fopen(savePath, "r");
                        if (sf) {
                            fscanf(sf, "%d", &s_s2Level);
                            fclose(sf);
                        }
                    }
                }

                if (!s_s2Applied && s_s2Level > 0 && Player()) {
                    static DWORD s_s2WaitStart = 0;
                    static BOOL s_s2Patched = FALSE;

                    if (!s_s2Patched) {
                        s_s2Patched = TRUE;
                        s_s2WaitStart = GetTickCount();
                        __try { PatchSkillForPlayer(SKILL2_ID); InsertSkillInClassList(SKILL2_ID); } __except(1) {}
                    }

                    if (s_s2Patched && s_s2WaitStart > 0 && GetTickCount() - s_s2WaitStart >= 2000) {
                        s_s2Applied = TRUE;
                        __try {
                            PatchSkillForPlayer(SKILL2_ID);
                            InsertSkillInClassList(SKILL2_ID);
                            for (int i = 0; i < s_s2Level; i++) SendSpendSkillPacket(SKILL2_ID);
                        } __except(1) {}
                    }
                }

                if (s_s2btnX != 0 || s_s2btnY != 0) {
                    int bTop = s_s2btnY - 40;

                    { int _fr2 = (SKILL2_ID >= 0 && SKILL2_ID < 400) ? g_icoMap[SKILL2_ID] : -1; if (g_archIcons && _fr2 >= 0) { __try { DrawCel(g_archIcons, _fr2, s_s2btnX, s_s2btnY); } __except(1) { if (fnRect) fnRect(s_s2btnX, bTop, s_s2btnX+40, s_s2btnY, 1, 5); } } else { if (fnRect) fnRect(s_s2btnX, bTop, s_s2btnX+40, s_s2btnY, 1, 5); } }

                    if (fnText && fnFont) {
                        wchar_t lvlBuf[8];
                        wsprintfW(lvlBuf, L"%d", s_s2Level);
                        fnFont(1);
                        fnText(lvlBuf, s_s2lblX, s_s2lblY, 0, 0);
                    }

                    {
                        POINT pt;
                        GetCursorPos(&pt);
                        HWND hWnd = GetForegroundWindow();
                        ScreenToClient(hWnd, &pt);
                        RECT rc;
                        GetClientRect(hWnd, &rc);
                        int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
                        float scaleX = (float)cw / (float)g_screenW, scaleY = (float)ch / (float)g_screenH;
                        float scale = (scaleX < scaleY) ? scaleX : scaleY;
                        int offX = (int)((cw - (float)g_screenW * scale) / 2.0f);
                        int offY = (int)((ch - 600.0f * scale) / 2.0f);
                        int mx = (scale > 0) ? (int)((pt.x - offX) / scale) : pt.x;
                        int my = (scale > 0) ? (int)((pt.y - offY) / scale) : pt.y;
                        if (mx < 0) mx = 0; if (my < 0) my = 0;

                        BOOL hover = (mx >= s_s2btnX && mx < s_s2btnX + 40 && my >= bTop && my < s_s2btnY);
                        if(hover){g_tooltipSkillId=SKILL2_ID;g_tooltipMX=mx;g_tooltipMY=my;}

                        if (hover && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                            static DWORD s_s2lastClick = 0;
                            DWORD now = GetTickCount();
                            if (now - s_s2lastClick > 500) {
                                s_s2lastClick = now;
                                int pts = 0;
                                if (fnGetStat && Player()) __try { pts = fnGetStat(Player(), 5, 0); } __except(1) {}
                                if (pts > 0) {
                                    if (s_s2Level == 0) {
                                        __try { PatchSkillForPlayer(SKILL2_ID); InsertSkillInClassList(SKILL2_ID); } __except(1) {}
                                    }
                                    __try { SendSpendSkillPacket(SKILL2_ID); } __except(1) {}
                                    s_s2Level++;
                                    if (g_charName[0]) {
                                        char savePath[MAX_PATH];
                                        GetCharFileDir(savePath, MAX_PATH);
                                        strcat(savePath, "d2arch_skill2_");
                                        strcat(savePath, g_charName);
                                        strcat(savePath, ".dat");
                                        FILE* sf = fopen(savePath, "w");
                                        if (sf) { fprintf(sf, "%d", s_s2Level); fclose(sf); }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            /* === skill-3 === */
            {
                static BOOL s_s3Loaded = FALSE;
                static int s_s3btnX = 0, s_s3btnY = 0;
                static int s_s3lblX = 0, s_s3lblY = 0;
                static int s_s3Level = 0;
                static BOOL s_s3Applied = FALSE;

                #define SKILL3_ID 38

                if (g_skillPanelReset) {
                    s_s3Loaded = FALSE;
                    s_s3Applied = FALSE;
                    s_s3Level = 0;
                }

                if (!s_s3Loaded) {
                    s_s3Loaded = TRUE;
                    char ini2[MAX_PATH], buf2[32];
                    GetArchDir(ini2, MAX_PATH);
                    strcat(ini2, "d2arch.ini");
                    GetPrivateProfileStringA("TestButton", "Btn3X", "0", buf2, 31, ini2);
                    s_s3btnX = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "Btn3Y", "0", buf2, 31, ini2);
                    s_s3btnY = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "Lbl3X", "0", buf2, 31, ini2);
                    s_s3lblX = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "Lbl3Y", "0", buf2, 31, ini2);
                    s_s3lblY = atoi(buf2);

                    if (g_charName[0]) {
                        char savePath[MAX_PATH];
                        GetCharFileDir(savePath, MAX_PATH);
                        strcat(savePath, "d2arch_skill3_");
                        strcat(savePath, g_charName);
                        strcat(savePath, ".dat");
                        FILE* sf = fopen(savePath, "r");
                        if (sf) {
                            fscanf(sf, "%d", &s_s3Level);
                            fclose(sf);
                        }
                    }
                }

                if (!s_s3Applied && s_s3Level > 0 && Player()) {
                    static DWORD s_s3WaitStart = 0;
                    static BOOL s_s3Patched = FALSE;

                    if (!s_s3Patched) {
                        s_s3Patched = TRUE;
                        s_s3WaitStart = GetTickCount();
                        __try { PatchSkillForPlayer(SKILL3_ID); InsertSkillInClassList(SKILL3_ID); } __except(1) {}
                    }

                    if (s_s3Patched && s_s3WaitStart > 0 && GetTickCount() - s_s3WaitStart >= 2000) {
                        s_s3Applied = TRUE;
                        __try {
                            PatchSkillForPlayer(SKILL3_ID);
                            InsertSkillInClassList(SKILL3_ID);
                            for (int i = 0; i < s_s3Level; i++) SendSpendSkillPacket(SKILL3_ID);
                        } __except(1) {}
                    }
                }

                if (s_s3btnX != 0 || s_s3btnY != 0) {
                    int bTop = s_s3btnY - 40;

                    { int _fr3 = (SKILL3_ID >= 0 && SKILL3_ID < 400) ? g_icoMap[SKILL3_ID] : -1; if (g_archIcons && _fr3 >= 0) { __try { DrawCel(g_archIcons, _fr3, s_s3btnX, s_s3btnY); } __except(1) { if (fnRect) fnRect(s_s3btnX, bTop, s_s3btnX+40, s_s3btnY, 1, 5); } } else { if (fnRect) fnRect(s_s3btnX, bTop, s_s3btnX+40, s_s3btnY, 1, 5); } }

                    if (fnText && fnFont) {
                        wchar_t lvlBuf[8];
                        wsprintfW(lvlBuf, L"%d", s_s3Level);
                        fnFont(1);
                        fnText(lvlBuf, s_s3lblX, s_s3lblY, 0, 0);
                    }

                    {
                        POINT pt;
                        GetCursorPos(&pt);
                        HWND hWnd = GetForegroundWindow();
                        ScreenToClient(hWnd, &pt);
                        RECT rc;
                        GetClientRect(hWnd, &rc);
                        int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
                        float scaleX = (float)cw / (float)g_screenW, scaleY = (float)ch / (float)g_screenH;
                        float scale = (scaleX < scaleY) ? scaleX : scaleY;
                        int offX = (int)((cw - (float)g_screenW * scale) / 2.0f);
                        int offY = (int)((ch - 600.0f * scale) / 2.0f);
                        int mx = (scale > 0) ? (int)((pt.x - offX) / scale) : pt.x;
                        int my = (scale > 0) ? (int)((pt.y - offY) / scale) : pt.y;
                        if (mx < 0) mx = 0; if (my < 0) my = 0;

                        BOOL hover = (mx >= s_s3btnX && mx < s_s3btnX + 40 && my >= bTop && my < s_s3btnY);
                        if(hover){g_tooltipSkillId=SKILL3_ID;g_tooltipMX=mx;g_tooltipMY=my;}

                        if (hover && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                            static DWORD s_s3lastClick = 0;
                            DWORD now = GetTickCount();
                            if (now - s_s3lastClick > 500) {
                                s_s3lastClick = now;
                                int pts = 0;
                                if (fnGetStat && Player()) __try { pts = fnGetStat(Player(), 5, 0); } __except(1) {}
                                if (pts > 0) {
                                    if (s_s3Level == 0) {
                                        __try { PatchSkillForPlayer(SKILL3_ID); InsertSkillInClassList(SKILL3_ID); } __except(1) {}
                                    }
                                    __try { SendSpendSkillPacket(SKILL3_ID); } __except(1) {}
                                    s_s3Level++;
                                    if (g_charName[0]) {
                                        char savePath[MAX_PATH];
                                        GetCharFileDir(savePath, MAX_PATH);
                                        strcat(savePath, "d2arch_skill3_");
                                        strcat(savePath, g_charName);
                                        strcat(savePath, ".dat");
                                        FILE* sf = fopen(savePath, "w");
                                        if (sf) { fprintf(sf, "%d", s_s3Level); fclose(sf); }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            /* === skill-4 === */
            {
                static BOOL s_s4Loaded = FALSE;
                static int s_s4btnX = 0, s_s4btnY = 0;
                static int s_s4lblX = 0, s_s4lblY = 0;
                static int s_s4Level = 0;
                static BOOL s_s4Applied = FALSE;
                #define SKILL4_ID 39
                if (g_skillPanelReset) { s_s4Loaded = FALSE; s_s4Applied = FALSE; s_s4Level = 0; }
                if (!s_s4Loaded) {
                    s_s4Loaded = TRUE;
                    char ini2[MAX_PATH], buf2[32];
                    GetArchDir(ini2, MAX_PATH); strcat(ini2, "d2arch.ini");
                    GetPrivateProfileStringA("TestButton", "Btn4X", "0", buf2, 31, ini2); s_s4btnX = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "Btn4Y", "0", buf2, 31, ini2); s_s4btnY = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "Lbl4X", "0", buf2, 31, ini2); s_s4lblX = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "Lbl4Y", "0", buf2, 31, ini2); s_s4lblY = atoi(buf2);
                    if (g_charName[0]) {
                        char sp[MAX_PATH]; GetCharFileDir(sp, MAX_PATH); strcat(sp, "d2arch_skill4_"); strcat(sp, g_charName); strcat(sp, ".dat");
                        FILE* f = fopen(sp, "r"); if (f) { fscanf(f, "%d", &s_s4Level); fclose(f); }
                    }
                }
                if (!s_s4Applied && s_s4Level > 0 && Player()) {
                    static DWORD s_s4W = 0; static BOOL s_s4P = FALSE;
                    if (!s_s4P) { s_s4P = TRUE; s_s4W = GetTickCount(); __try { PatchSkillForPlayer(SKILL4_ID); InsertSkillInClassList(SKILL4_ID); } __except(1) {} }
                    if (s_s4P && s_s4W > 0 && GetTickCount() - s_s4W >= 2000) {
                        s_s4Applied = TRUE;
                        __try { PatchSkillForPlayer(SKILL4_ID); InsertSkillInClassList(SKILL4_ID); for (int i = 0; i < s_s4Level; i++) SendSpendSkillPacket(SKILL4_ID); } __except(1) {}
                    }
                }
                if (s_s4btnX != 0 || s_s4btnY != 0) {
                    int bTop = s_s4btnY - 40;
                    { int _fr4=(SKILL4_ID>=0&&SKILL4_ID<400)?g_icoMap[SKILL4_ID]:-1; if(g_archIcons&&_fr4>=0){__try{DrawCel(g_archIcons,_fr4,s_s4btnX,s_s4btnY);}__except(1){if(fnRect)fnRect(s_s4btnX,bTop,s_s4btnX+40,s_s4btnY,1,5);}}else{if(fnRect)fnRect(s_s4btnX,bTop,s_s4btnX+40,s_s4btnY,1,5);}}
                    if (fnText && fnFont) { wchar_t lb[8]; wsprintfW(lb, L"%d", s_s4Level); fnFont(1); fnText(lb, s_s4lblX, s_s4lblY, 0, 0); }
                    { POINT pt; GetCursorPos(&pt); HWND hW = GetForegroundWindow(); ScreenToClient(hW, &pt);
                      RECT rc; GetClientRect(hW, &rc); int cw = rc.right-rc.left, ch = rc.bottom-rc.top;
                      float sx = (float)cw/(float)g_screenW, sy = (float)ch/(float)g_screenH, sc = (sx<sy)?sx:sy;
                      int ox = (int)((cw-(float)g_screenW*sc)/2.0f), oy = (int)((ch-(float)g_screenH*sc)/2.0f);
                      int mx = (sc>0)?(int)((pt.x-ox)/sc):pt.x, my = (sc>0)?(int)((pt.y-oy)/sc):pt.y;
                      if (mx<0) mx=0; if (my<0) my=0;
                      BOOL hv = (mx>=s_s4btnX && mx<s_s4btnX+40 && my>=bTop && my<s_s4btnY);
                      if(hv){g_tooltipSkillId=SKILL4_ID;g_tooltipMX=mx;g_tooltipMY=my;}
                      if (hv && (GetAsyncKeyState(VK_LBUTTON)&0x8000)) {
                        static DWORD s_s4lc = 0; DWORD now = GetTickCount();
                        if (now - s_s4lc > 500) { s_s4lc = now; int pts = 0;
                          if (fnGetStat && Player()) __try { pts = fnGetStat(Player(),5,0); } __except(1) {}
                          if (pts > 0) {
                            if (s_s4Level==0) { __try { PatchSkillForPlayer(SKILL4_ID); InsertSkillInClassList(SKILL4_ID); } __except(1) {} }
                            __try { SendSpendSkillPacket(SKILL4_ID); } __except(1) {} s_s4Level++;
                            if (g_charName[0]) { char sp[MAX_PATH]; GetCharFileDir(sp,MAX_PATH); strcat(sp,"d2arch_skill4_"); strcat(sp,g_charName); strcat(sp,".dat");
                              FILE* f=fopen(sp,"w"); if(f){fprintf(f,"%d",s_s4Level);fclose(f);} }
                    } } } }
                }
            }

            /* === skill-5 === */
            {
                static BOOL s_s5Loaded = FALSE;
                static int s_s5btnX = 0, s_s5btnY = 0;
                static int s_s5lblX = 0, s_s5lblY = 0;
                static int s_s5Level = 0;
                static BOOL s_s5Applied = FALSE;
                #define SKILL5_ID 40
                if (g_skillPanelReset) { s_s5Loaded = FALSE; s_s5Applied = FALSE; s_s5Level = 0; }
                if (!s_s5Loaded) {
                    s_s5Loaded = TRUE;
                    char ini2[MAX_PATH], buf2[32];
                    GetArchDir(ini2, MAX_PATH); strcat(ini2, "d2arch.ini");
                    GetPrivateProfileStringA("TestButton", "Btn5X", "0", buf2, 31, ini2); s_s5btnX = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "Btn5Y", "0", buf2, 31, ini2); s_s5btnY = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "Lbl5X", "0", buf2, 31, ini2); s_s5lblX = atoi(buf2);
                    GetPrivateProfileStringA("TestButton", "Lbl5Y", "0", buf2, 31, ini2); s_s5lblY = atoi(buf2);
                    if (g_charName[0]) {
                        char sp[MAX_PATH]; GetCharFileDir(sp, MAX_PATH); strcat(sp, "d2arch_skill5_"); strcat(sp, g_charName); strcat(sp, ".dat");
                        FILE* f = fopen(sp, "r"); if (f) { fscanf(f, "%d", &s_s5Level); fclose(f); }
                    }
                }
                if (!s_s5Applied && s_s5Level > 0 && Player()) {
                    static DWORD s_s5W = 0; static BOOL s_s5P = FALSE;
                    if (!s_s5P) { s_s5P = TRUE; s_s5W = GetTickCount(); __try { PatchSkillForPlayer(SKILL5_ID); InsertSkillInClassList(SKILL5_ID); } __except(1) {} }
                    if (s_s5P && s_s5W > 0 && GetTickCount() - s_s5W >= 2000) {
                        s_s5Applied = TRUE;
                        __try { PatchSkillForPlayer(SKILL5_ID); InsertSkillInClassList(SKILL5_ID); for (int i = 0; i < s_s5Level; i++) SendSpendSkillPacket(SKILL5_ID); } __except(1) {}
                    }
                }
                if (s_s5btnX != 0 || s_s5btnY != 0) {
                    int bTop = s_s5btnY - 40;
                    { int _fr5=(SKILL5_ID>=0&&SKILL5_ID<400)?g_icoMap[SKILL5_ID]:-1; if(g_archIcons&&_fr5>=0){__try{DrawCel(g_archIcons,_fr5,s_s5btnX,s_s5btnY);}__except(1){if(fnRect)fnRect(s_s5btnX,bTop,s_s5btnX+40,s_s5btnY,1,5);}}else{if(fnRect)fnRect(s_s5btnX,bTop,s_s5btnX+40,s_s5btnY,1,5);}}
                    if (fnText && fnFont) { wchar_t lb[8]; wsprintfW(lb, L"%d", s_s5Level); fnFont(1); fnText(lb, s_s5lblX, s_s5lblY, 0, 0); }
                    { POINT pt; GetCursorPos(&pt); HWND hW = GetForegroundWindow(); ScreenToClient(hW, &pt);
                      RECT rc; GetClientRect(hW, &rc); int cw = rc.right-rc.left, ch = rc.bottom-rc.top;
                      float sx = (float)cw/(float)g_screenW, sy = (float)ch/(float)g_screenH, sc = (sx<sy)?sx:sy;
                      int ox = (int)((cw-(float)g_screenW*sc)/2.0f), oy = (int)((ch-(float)g_screenH*sc)/2.0f);
                      int mx = (sc>0)?(int)((pt.x-ox)/sc):pt.x, my = (sc>0)?(int)((pt.y-oy)/sc):pt.y;
                      if (mx<0) mx=0; if (my<0) my=0;
                      BOOL hv = (mx>=s_s5btnX && mx<s_s5btnX+40 && my>=bTop && my<s_s5btnY);
                      if(hv){g_tooltipSkillId=SKILL5_ID;g_tooltipMX=mx;g_tooltipMY=my;}
                      if (hv && (GetAsyncKeyState(VK_LBUTTON)&0x8000)) {
                        static DWORD s_s5lc = 0; DWORD now = GetTickCount();
                        if (now - s_s5lc > 500) { s_s5lc = now; int pts = 0;
                          if (fnGetStat && Player()) __try { pts = fnGetStat(Player(),5,0); } __except(1) {}
                          if (pts > 0) {
                            if (s_s5Level==0) { __try { PatchSkillForPlayer(SKILL5_ID); InsertSkillInClassList(SKILL5_ID); } __except(1) {} }
                            __try { SendSpendSkillPacket(SKILL5_ID); } __except(1) {} s_s5Level++;
                            if (g_charName[0]) { char sp[MAX_PATH]; GetCharFileDir(sp,MAX_PATH); strcat(sp,"d2arch_skill5_"); strcat(sp,g_charName); strcat(sp,".dat");
                              FILE* f=fopen(sp,"w"); if(f){fprintf(f,"%d",s_s5Level);fclose(f);} }
                    } } } }
                }
            }

            /* === skill-6 === */
            {
                static BOOL s_s6Loaded=FALSE; static int s_s6btnX=0,s_s6btnY=0,s_s6lblX=0,s_s6lblY=0,s_s6Level=0; static BOOL s_s6Applied=FALSE;
                #define SKILL6_ID 41
                if(g_skillPanelReset){s_s6Loaded=FALSE;s_s6Applied=FALSE;s_s6Level=0;}
                if(!s_s6Loaded){s_s6Loaded=TRUE;char ini2[MAX_PATH],buf2[32];GetArchDir(ini2,MAX_PATH);strcat(ini2,"d2arch.ini");
                    GetPrivateProfileStringA("TestButton","Btn6X","0",buf2,31,ini2);s_s6btnX=atoi(buf2);GetPrivateProfileStringA("TestButton","Btn6Y","0",buf2,31,ini2);s_s6btnY=atoi(buf2);
                    GetPrivateProfileStringA("TestButton","Lbl6X","0",buf2,31,ini2);s_s6lblX=atoi(buf2);GetPrivateProfileStringA("TestButton","Lbl6Y","0",buf2,31,ini2);s_s6lblY=atoi(buf2);
                    if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill6_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s6Level);fclose(f);}}}
                if(!s_s6Applied&&s_s6Level>0&&Player()){static DWORD s_s6W=0;static BOOL s_s6P=FALSE;
                    if(!s_s6P){s_s6P=TRUE;s_s6W=GetTickCount();__try{PatchSkillForPlayer(SKILL6_ID);InsertSkillInClassList(SKILL6_ID);}__except(1){}}
                    if(s_s6P&&s_s6W>0&&GetTickCount()-s_s6W>=2000){s_s6Applied=TRUE;__try{PatchSkillForPlayer(SKILL6_ID);InsertSkillInClassList(SKILL6_ID);for(int i=0;i<s_s6Level;i++)SendSpendSkillPacket(SKILL6_ID);}__except(1){}}}
                if(s_s6btnX!=0||s_s6btnY!=0){int bTop=s_s6btnY-40;
                    { int _fr6 = (SKILL6_ID >= 0 && SKILL6_ID < 400) ? g_icoMap[SKILL6_ID] : -1; if (g_archIcons && _fr6 >= 0) { __try { DrawCel(g_archIcons, _fr6, s_s6btnX, s_s6btnY); } __except(1) { if(fnRect)fnRect(s_s6btnX,bTop,s_s6btnX+40,s_s6btnY,1,5); } } else { if(fnRect)fnRect(s_s6btnX,bTop,s_s6btnX+40,s_s6btnY,1,5); } }
                    if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s6Level);fnFont(1);fnText(lb,s_s6lblX,s_s6lblY,0,0);}
                    {POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;
                    float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);
                    int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;
                    BOOL hv=(mx>=s_s6btnX&&mx<s_s6btnX+40&&my>=bTop&&my<s_s6btnY);if(hv){g_tooltipSkillId=SKILL6_ID;g_tooltipMX=mx;g_tooltipMY=my;}
                    if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD s_s6lc=0;DWORD now=GetTickCount();if(now-s_s6lc>500){s_s6lc=now;int pts=0;
                    if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}
                    if(pts>0){if(s_s6Level==0){__try{PatchSkillForPlayer(SKILL6_ID);InsertSkillInClassList(SKILL6_ID);}__except(1){}}
                    __try{SendSpendSkillPacket(SKILL6_ID);}__except(1){}s_s6Level++;
                    if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill6_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s6Level);fclose(f);}}}}}}}
            }

            /* === skill-7 === */
            {
                static BOOL s_s7Loaded=FALSE; static int s_s7btnX=0,s_s7btnY=0,s_s7lblX=0,s_s7lblY=0,s_s7Level=0; static BOOL s_s7Applied=FALSE;
                #define SKILL7_ID 42
                if(g_skillPanelReset){s_s7Loaded=FALSE;s_s7Applied=FALSE;s_s7Level=0;}
                if(!s_s7Loaded){s_s7Loaded=TRUE;char ini2[MAX_PATH],buf2[32];GetArchDir(ini2,MAX_PATH);strcat(ini2,"d2arch.ini");
                    GetPrivateProfileStringA("TestButton","Btn7X","0",buf2,31,ini2);s_s7btnX=atoi(buf2);GetPrivateProfileStringA("TestButton","Btn7Y","0",buf2,31,ini2);s_s7btnY=atoi(buf2);
                    GetPrivateProfileStringA("TestButton","Lbl7X","0",buf2,31,ini2);s_s7lblX=atoi(buf2);GetPrivateProfileStringA("TestButton","Lbl7Y","0",buf2,31,ini2);s_s7lblY=atoi(buf2);
                    if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill7_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s7Level);fclose(f);}}}
                if(!s_s7Applied&&s_s7Level>0&&Player()){static DWORD s_s7W=0;static BOOL s_s7P=FALSE;
                    if(!s_s7P){s_s7P=TRUE;s_s7W=GetTickCount();__try{PatchSkillForPlayer(SKILL7_ID);InsertSkillInClassList(SKILL7_ID);}__except(1){}}
                    if(s_s7P&&s_s7W>0&&GetTickCount()-s_s7W>=2000){s_s7Applied=TRUE;__try{PatchSkillForPlayer(SKILL7_ID);InsertSkillInClassList(SKILL7_ID);for(int i=0;i<s_s7Level;i++)SendSpendSkillPacket(SKILL7_ID);}__except(1){}}}
                if(s_s7btnX!=0||s_s7btnY!=0){int bTop=s_s7btnY-40;
                    { int _fr7 = (SKILL7_ID >= 0 && SKILL7_ID < 400) ? g_icoMap[SKILL7_ID] : -1; if (g_archIcons && _fr7 >= 0) { __try { DrawCel(g_archIcons, _fr7, s_s7btnX, s_s7btnY); } __except(1) { if(fnRect)fnRect(s_s7btnX,bTop,s_s7btnX+40,s_s7btnY,1,5); } } else { if(fnRect)fnRect(s_s7btnX,bTop,s_s7btnX+40,s_s7btnY,1,5); } }
                    if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s7Level);fnFont(1);fnText(lb,s_s7lblX,s_s7lblY,0,0);}
                    {POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;
                    float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);
                    int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;
                    BOOL hv=(mx>=s_s7btnX&&mx<s_s7btnX+40&&my>=bTop&&my<s_s7btnY);if(hv){g_tooltipSkillId=SKILL7_ID;g_tooltipMX=mx;g_tooltipMY=my;}
                    if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD s_s7lc=0;DWORD now=GetTickCount();if(now-s_s7lc>500){s_s7lc=now;int pts=0;
                    if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}
                    if(pts>0){if(s_s7Level==0){__try{PatchSkillForPlayer(SKILL7_ID);InsertSkillInClassList(SKILL7_ID);}__except(1){}}
                    __try{SendSpendSkillPacket(SKILL7_ID);}__except(1){}s_s7Level++;
                    if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill7_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s7Level);fclose(f);}}}}}}}
            }

            /* === skill-8 === */
            {
                static BOOL s_s8Loaded=FALSE; static int s_s8btnX=0,s_s8btnY=0,s_s8lblX=0,s_s8lblY=0,s_s8Level=0; static BOOL s_s8Applied=FALSE;
                #define SKILL8_ID 43
                if(g_skillPanelReset){s_s8Loaded=FALSE;s_s8Applied=FALSE;s_s8Level=0;}
                if(!s_s8Loaded){s_s8Loaded=TRUE;char ini2[MAX_PATH],buf2[32];GetArchDir(ini2,MAX_PATH);strcat(ini2,"d2arch.ini");
                    GetPrivateProfileStringA("TestButton","Btn8X","0",buf2,31,ini2);s_s8btnX=atoi(buf2);GetPrivateProfileStringA("TestButton","Btn8Y","0",buf2,31,ini2);s_s8btnY=atoi(buf2);
                    GetPrivateProfileStringA("TestButton","Lbl8X","0",buf2,31,ini2);s_s8lblX=atoi(buf2);GetPrivateProfileStringA("TestButton","Lbl8Y","0",buf2,31,ini2);s_s8lblY=atoi(buf2);
                    if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill8_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s8Level);fclose(f);}}}
                if(!s_s8Applied&&s_s8Level>0&&Player()){static DWORD s_s8W=0;static BOOL s_s8P=FALSE;
                    if(!s_s8P){s_s8P=TRUE;s_s8W=GetTickCount();__try{PatchSkillForPlayer(SKILL8_ID);InsertSkillInClassList(SKILL8_ID);}__except(1){}}
                    if(s_s8P&&s_s8W>0&&GetTickCount()-s_s8W>=2000){s_s8Applied=TRUE;__try{PatchSkillForPlayer(SKILL8_ID);InsertSkillInClassList(SKILL8_ID);for(int i=0;i<s_s8Level;i++)SendSpendSkillPacket(SKILL8_ID);}__except(1){}}}
                if(s_s8btnX!=0||s_s8btnY!=0){int bTop=s_s8btnY-40;
                    { int _fr8 = (SKILL8_ID >= 0 && SKILL8_ID < 400) ? g_icoMap[SKILL8_ID] : -1; if (g_archIcons && _fr8 >= 0) { __try { DrawCel(g_archIcons, _fr8, s_s8btnX, s_s8btnY); } __except(1) { if(fnRect)fnRect(s_s8btnX,bTop,s_s8btnX+40,s_s8btnY,1,5); } } else { if(fnRect)fnRect(s_s8btnX,bTop,s_s8btnX+40,s_s8btnY,1,5); } }
                    if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s8Level);fnFont(1);fnText(lb,s_s8lblX,s_s8lblY,0,0);}
                    {POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;
                    float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);
                    int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;
                    BOOL hv=(mx>=s_s8btnX&&mx<s_s8btnX+40&&my>=bTop&&my<s_s8btnY);if(hv){g_tooltipSkillId=SKILL8_ID;g_tooltipMX=mx;g_tooltipMY=my;}
                    if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD s_s8lc=0;DWORD now=GetTickCount();if(now-s_s8lc>500){s_s8lc=now;int pts=0;
                    if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}
                    if(pts>0){if(s_s8Level==0){__try{PatchSkillForPlayer(SKILL8_ID);InsertSkillInClassList(SKILL8_ID);}__except(1){}}
                    __try{SendSpendSkillPacket(SKILL8_ID);}__except(1){}s_s8Level++;
                    if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill8_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s8Level);fclose(f);}}}}}}}
            }

            /* === skill-9 === */
            {
                static BOOL s_s9Loaded=FALSE; static int s_s9btnX=0,s_s9btnY=0,s_s9lblX=0,s_s9lblY=0,s_s9Level=0; static BOOL s_s9Applied=FALSE;
                #define SKILL9_ID 44
                if(g_skillPanelReset){s_s9Loaded=FALSE;s_s9Applied=FALSE;s_s9Level=0;}
                if(!s_s9Loaded){s_s9Loaded=TRUE;char ini2[MAX_PATH],buf2[32];GetArchDir(ini2,MAX_PATH);strcat(ini2,"d2arch.ini");
                    GetPrivateProfileStringA("TestButton","Btn9X","0",buf2,31,ini2);s_s9btnX=atoi(buf2);GetPrivateProfileStringA("TestButton","Btn9Y","0",buf2,31,ini2);s_s9btnY=atoi(buf2);
                    GetPrivateProfileStringA("TestButton","Lbl9X","0",buf2,31,ini2);s_s9lblX=atoi(buf2);GetPrivateProfileStringA("TestButton","Lbl9Y","0",buf2,31,ini2);s_s9lblY=atoi(buf2);
                    if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill9_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s9Level);fclose(f);}}}
                if(!s_s9Applied&&s_s9Level>0&&Player()){static DWORD s_s9W=0;static BOOL s_s9P=FALSE;
                    if(!s_s9P){s_s9P=TRUE;s_s9W=GetTickCount();__try{PatchSkillForPlayer(SKILL9_ID);InsertSkillInClassList(SKILL9_ID);}__except(1){}}
                    if(s_s9P&&s_s9W>0&&GetTickCount()-s_s9W>=2000){s_s9Applied=TRUE;__try{PatchSkillForPlayer(SKILL9_ID);InsertSkillInClassList(SKILL9_ID);for(int i=0;i<s_s9Level;i++)SendSpendSkillPacket(SKILL9_ID);}__except(1){}}}
                if(s_s9btnX!=0||s_s9btnY!=0){int bTop=s_s9btnY-40;
                    { int _fr9 = (SKILL9_ID >= 0 && SKILL9_ID < 400) ? g_icoMap[SKILL9_ID] : -1; if (g_archIcons && _fr9 >= 0) { __try { DrawCel(g_archIcons, _fr9, s_s9btnX, s_s9btnY); } __except(1) { if(fnRect)fnRect(s_s9btnX,bTop,s_s9btnX+40,s_s9btnY,1,5); } } else { if(fnRect)fnRect(s_s9btnX,bTop,s_s9btnX+40,s_s9btnY,1,5); } }
                    if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s9Level);fnFont(1);fnText(lb,s_s9lblX,s_s9lblY,0,0);}
                    {POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;
                    float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);
                    int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;
                    BOOL hv=(mx>=s_s9btnX&&mx<s_s9btnX+40&&my>=bTop&&my<s_s9btnY);if(hv){g_tooltipSkillId=SKILL9_ID;g_tooltipMX=mx;g_tooltipMY=my;}
                    if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD s_s9lc=0;DWORD now=GetTickCount();if(now-s_s9lc>500){s_s9lc=now;int pts=0;
                    if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}
                    if(pts>0){if(s_s9Level==0){__try{PatchSkillForPlayer(SKILL9_ID);InsertSkillInClassList(SKILL9_ID);}__except(1){}}
                    __try{SendSpendSkillPacket(SKILL9_ID);}__except(1){}s_s9Level++;
                    if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill9_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s9Level);fclose(f);}}}}}}}
            }

            /* === skill-10 === */
            {
                static BOOL s_s10Loaded=FALSE; static int s_s10btnX=0,s_s10btnY=0,s_s10lblX=0,s_s10lblY=0,s_s10Level=0; static BOOL s_s10Applied=FALSE;
                #define SKILL10_ID 45
                if(g_skillPanelReset){s_s10Loaded=FALSE;s_s10Applied=FALSE;s_s10Level=0;}
                if(!s_s10Loaded){s_s10Loaded=TRUE;char ini2[MAX_PATH],buf2[32];GetArchDir(ini2,MAX_PATH);strcat(ini2,"d2arch.ini");
                    GetPrivateProfileStringA("TestButton","Btn10X","0",buf2,31,ini2);s_s10btnX=atoi(buf2);GetPrivateProfileStringA("TestButton","Btn10Y","0",buf2,31,ini2);s_s10btnY=atoi(buf2);
                    GetPrivateProfileStringA("TestButton","Lbl10X","0",buf2,31,ini2);s_s10lblX=atoi(buf2);GetPrivateProfileStringA("TestButton","Lbl10Y","0",buf2,31,ini2);s_s10lblY=atoi(buf2);
                    if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill10_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s10Level);fclose(f);}}}
                if(!s_s10Applied&&s_s10Level>0&&Player()){static DWORD s_s10W=0;static BOOL s_s10P=FALSE;
                    if(!s_s10P){s_s10P=TRUE;s_s10W=GetTickCount();__try{PatchSkillForPlayer(SKILL10_ID);InsertSkillInClassList(SKILL10_ID);}__except(1){}}
                    if(s_s10P&&s_s10W>0&&GetTickCount()-s_s10W>=2000){s_s10Applied=TRUE;__try{PatchSkillForPlayer(SKILL10_ID);InsertSkillInClassList(SKILL10_ID);for(int i=0;i<s_s10Level;i++)SendSpendSkillPacket(SKILL10_ID);}__except(1){}}}
                if(s_s10btnX!=0||s_s10btnY!=0){int bTop=s_s10btnY-40;
                    { int _fr10 = (SKILL10_ID >= 0 && SKILL10_ID < 400) ? g_icoMap[SKILL10_ID] : -1; if (g_archIcons && _fr10 >= 0) { __try { DrawCel(g_archIcons, _fr10, s_s10btnX, s_s10btnY); } __except(1) { if(fnRect)fnRect(s_s10btnX,bTop,s_s10btnX+40,s_s10btnY,1,5); } } else { if(fnRect)fnRect(s_s10btnX,bTop,s_s10btnX+40,s_s10btnY,1,5); } }
                    if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s10Level);fnFont(1);fnText(lb,s_s10lblX,s_s10lblY,0,0);}
                    {POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;
                    float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);
                    int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;
                    BOOL hv=(mx>=s_s10btnX&&mx<s_s10btnX+40&&my>=bTop&&my<s_s10btnY);if(hv){g_tooltipSkillId=SKILL10_ID;g_tooltipMX=mx;g_tooltipMY=my;}
                    if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD s_s10lc=0;DWORD now=GetTickCount();if(now-s_s10lc>500){s_s10lc=now;int pts=0;
                    if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}
                    if(pts>0){if(s_s10Level==0){__try{PatchSkillForPlayer(SKILL10_ID);InsertSkillInClassList(SKILL10_ID);}__except(1){}}
                    __try{SendSpendSkillPacket(SKILL10_ID);}__except(1){}s_s10Level++;
                    if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill10_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s10Level);fclose(f);}}}}}}}
            }

            /* === skill-11 === */
            {static BOOL s_s11L=FALSE;static int s_s11bx=0,s_s11by=0,s_s11lx=0,s_s11ly=0,s_s11lv=0;static BOOL s_s11A=FALSE;
            #define SKILL11_ID 46
            if(g_skillPanelReset){s_s11L=FALSE;s_s11A=FALSE;s_s11lv=0;}
            if(!s_s11L){s_s11L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn11X","0",b2,31,i2);s_s11bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn11Y","0",b2,31,i2);s_s11by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl11X","0",b2,31,i2);s_s11lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl11Y","0",b2,31,i2);s_s11ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill11_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s11lv);fclose(f);}}}
            if(!s_s11A&&s_s11lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL11_ID);InsertSkillInClassList(SKILL11_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s11A=TRUE;__try{PatchSkillForPlayer(SKILL11_ID);InsertSkillInClassList(SKILL11_ID);for(int i=0;i<s_s11lv;i++)SendSpendSkillPacket(SKILL11_ID);}__except(1){}}}
            if(s_s11bx!=0||s_s11by!=0){int bt=s_s11by-40;{ int _fr11 = (SKILL11_ID >= 0 && SKILL11_ID < 400) ? g_icoMap[SKILL11_ID] : -1; if(g_archIcons && _fr11 >= 0){__try{DrawCel(g_archIcons,_fr11,s_s11bx,s_s11by);}__except(1){if(fnRect)fnRect(s_s11bx,bt,s_s11bx+40,s_s11by,1,5);}}else{if(fnRect)fnRect(s_s11bx,bt,s_s11bx+40,s_s11by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s11lv);fnFont(1);fnText(lb,s_s11lx,s_s11ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s11bx&&mx<s_s11bx+40&&my>=bt&&my<s_s11by);if(hv){g_tooltipSkillId=SKILL11_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s11lv==0){__try{PatchSkillForPlayer(SKILL11_ID);InsertSkillInClassList(SKILL11_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL11_ID);}__except(1){}s_s11lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill11_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s11lv);fclose(f);}}}}}}}}

            /* === skill-12 === */
            {static BOOL s_s12L=FALSE;static int s_s12bx=0,s_s12by=0,s_s12lx=0,s_s12ly=0,s_s12lv=0;static BOOL s_s12A=FALSE;
            #define SKILL12_ID 48
            if(g_skillPanelReset){s_s12L=FALSE;s_s12A=FALSE;s_s12lv=0;}
            if(!s_s12L){s_s12L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn12X","0",b2,31,i2);s_s12bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn12Y","0",b2,31,i2);s_s12by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl12X","0",b2,31,i2);s_s12lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl12Y","0",b2,31,i2);s_s12ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill12_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s12lv);fclose(f);}}}
            if(!s_s12A&&s_s12lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL12_ID);InsertSkillInClassList(SKILL12_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s12A=TRUE;__try{PatchSkillForPlayer(SKILL12_ID);InsertSkillInClassList(SKILL12_ID);for(int i=0;i<s_s12lv;i++)SendSpendSkillPacket(SKILL12_ID);}__except(1){}}}
            if(s_s12bx!=0||s_s12by!=0){int bt=s_s12by-40;{ int _fr12 = (SKILL12_ID >= 0 && SKILL12_ID < 400) ? g_icoMap[SKILL12_ID] : -1; if(g_archIcons && _fr12 >= 0){__try{DrawCel(g_archIcons,_fr12,s_s12bx,s_s12by);}__except(1){if(fnRect)fnRect(s_s12bx,bt,s_s12bx+40,s_s12by,1,5);}}else{if(fnRect)fnRect(s_s12bx,bt,s_s12bx+40,s_s12by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s12lv);fnFont(1);fnText(lb,s_s12lx,s_s12ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s12bx&&mx<s_s12bx+40&&my>=bt&&my<s_s12by);if(hv){g_tooltipSkillId=SKILL12_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s12lv==0){__try{PatchSkillForPlayer(SKILL12_ID);InsertSkillInClassList(SKILL12_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL12_ID);}__except(1){}s_s12lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill12_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s12lv);fclose(f);}}}}}}}}

            /* === skill-13 === */
            {static BOOL s_s13L=FALSE;static int s_s13bx=0,s_s13by=0,s_s13lx=0,s_s13ly=0,s_s13lv=0;static BOOL s_s13A=FALSE;
            #define SKILL13_ID 49
            if(g_skillPanelReset){s_s13L=FALSE;s_s13A=FALSE;s_s13lv=0;}
            if(!s_s13L){s_s13L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn13X","0",b2,31,i2);s_s13bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn13Y","0",b2,31,i2);s_s13by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl13X","0",b2,31,i2);s_s13lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl13Y","0",b2,31,i2);s_s13ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill13_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s13lv);fclose(f);}}}
            if(!s_s13A&&s_s13lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL13_ID);InsertSkillInClassList(SKILL13_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s13A=TRUE;__try{PatchSkillForPlayer(SKILL13_ID);InsertSkillInClassList(SKILL13_ID);for(int i=0;i<s_s13lv;i++)SendSpendSkillPacket(SKILL13_ID);}__except(1){}}}
            if(s_s13bx!=0||s_s13by!=0){int bt=s_s13by-40;{ int _fr13 = (SKILL13_ID >= 0 && SKILL13_ID < 400) ? g_icoMap[SKILL13_ID] : -1; if(g_archIcons && _fr13 >= 0){__try{DrawCel(g_archIcons,_fr13,s_s13bx,s_s13by);}__except(1){if(fnRect)fnRect(s_s13bx,bt,s_s13bx+40,s_s13by,1,5);}}else{if(fnRect)fnRect(s_s13bx,bt,s_s13bx+40,s_s13by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s13lv);fnFont(1);fnText(lb,s_s13lx,s_s13ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s13bx&&mx<s_s13bx+40&&my>=bt&&my<s_s13by);if(hv){g_tooltipSkillId=SKILL13_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s13lv==0){__try{PatchSkillForPlayer(SKILL13_ID);InsertSkillInClassList(SKILL13_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL13_ID);}__except(1){}s_s13lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill13_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s13lv);fclose(f);}}}}}}}}

            /* === skill-14 === */
            {static BOOL s_s14L=FALSE;static int s_s14bx=0,s_s14by=0,s_s14lx=0,s_s14ly=0,s_s14lv=0;static BOOL s_s14A=FALSE;
            #define SKILL14_ID 50
            if(g_skillPanelReset){s_s14L=FALSE;s_s14A=FALSE;s_s14lv=0;}
            if(!s_s14L){s_s14L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn14X","0",b2,31,i2);s_s14bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn14Y","0",b2,31,i2);s_s14by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl14X","0",b2,31,i2);s_s14lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl14Y","0",b2,31,i2);s_s14ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill14_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s14lv);fclose(f);}}}
            if(!s_s14A&&s_s14lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL14_ID);InsertSkillInClassList(SKILL14_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s14A=TRUE;__try{PatchSkillForPlayer(SKILL14_ID);InsertSkillInClassList(SKILL14_ID);for(int i=0;i<s_s14lv;i++)SendSpendSkillPacket(SKILL14_ID);}__except(1){}}}
            if(s_s14bx!=0||s_s14by!=0){int bt=s_s14by-40;{ int _fr14 = (SKILL14_ID >= 0 && SKILL14_ID < 400) ? g_icoMap[SKILL14_ID] : -1; if(g_archIcons && _fr14 >= 0){__try{DrawCel(g_archIcons,_fr14,s_s14bx,s_s14by);}__except(1){if(fnRect)fnRect(s_s14bx,bt,s_s14bx+40,s_s14by,1,5);}}else{if(fnRect)fnRect(s_s14bx,bt,s_s14bx+40,s_s14by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s14lv);fnFont(1);fnText(lb,s_s14lx,s_s14ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s14bx&&mx<s_s14bx+40&&my>=bt&&my<s_s14by);if(hv){g_tooltipSkillId=SKILL14_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s14lv==0){__try{PatchSkillForPlayer(SKILL14_ID);InsertSkillInClassList(SKILL14_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL14_ID);}__except(1){}s_s14lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill14_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s14lv);fclose(f);}}}}}}}}

            /* === skill-15 === */
            {static BOOL s_s15L=FALSE;static int s_s15bx=0,s_s15by=0,s_s15lx=0,s_s15ly=0,s_s15lv=0;static BOOL s_s15A=FALSE;
            #define SKILL15_ID 51
            if(g_skillPanelReset){s_s15L=FALSE;s_s15A=FALSE;s_s15lv=0;}
            if(!s_s15L){s_s15L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn15X","0",b2,31,i2);s_s15bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn15Y","0",b2,31,i2);s_s15by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl15X","0",b2,31,i2);s_s15lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl15Y","0",b2,31,i2);s_s15ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill15_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s15lv);fclose(f);}}}
            if(!s_s15A&&s_s15lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL15_ID);InsertSkillInClassList(SKILL15_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s15A=TRUE;__try{PatchSkillForPlayer(SKILL15_ID);InsertSkillInClassList(SKILL15_ID);for(int i=0;i<s_s15lv;i++)SendSpendSkillPacket(SKILL15_ID);}__except(1){}}}
            if(s_s15bx!=0||s_s15by!=0){int bt=s_s15by-40;{ int _fr15 = (SKILL15_ID >= 0 && SKILL15_ID < 400) ? g_icoMap[SKILL15_ID] : -1; if(g_archIcons && _fr15 >= 0){__try{DrawCel(g_archIcons,_fr15,s_s15bx,s_s15by);}__except(1){if(fnRect)fnRect(s_s15bx,bt,s_s15bx+40,s_s15by,1,5);}}else{if(fnRect)fnRect(s_s15bx,bt,s_s15bx+40,s_s15by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s15lv);fnFont(1);fnText(lb,s_s15lx,s_s15ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s15bx&&mx<s_s15bx+40&&my>=bt&&my<s_s15by);if(hv){g_tooltipSkillId=SKILL15_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s15lv==0){__try{PatchSkillForPlayer(SKILL15_ID);InsertSkillInClassList(SKILL15_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL15_ID);}__except(1){}s_s15lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill15_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s15lv);fclose(f);}}}}}}}}

            /* === skill-16 thru skill-20 (row 4) === */
            {static BOOL s_s16L=FALSE;static int s_s16bx=0,s_s16by=0,s_s16lx=0,s_s16ly=0,s_s16lv=0;static BOOL s_s16A=FALSE;
            #define SKILL16_ID 52
            if(g_skillPanelReset){s_s16L=FALSE;s_s16A=FALSE;s_s16lv=0;}if(!s_s16L){s_s16L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn16X","0",b2,31,i2);s_s16bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn16Y","0",b2,31,i2);s_s16by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl16X","0",b2,31,i2);s_s16lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl16Y","0",b2,31,i2);s_s16ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill16_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s16lv);fclose(f);}}}if(!s_s16A&&s_s16lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL16_ID);InsertSkillInClassList(SKILL16_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s16A=TRUE;__try{PatchSkillForPlayer(SKILL16_ID);InsertSkillInClassList(SKILL16_ID);for(int i=0;i<s_s16lv;i++)SendSpendSkillPacket(SKILL16_ID);}__except(1){}}}if(s_s16bx!=0||s_s16by!=0){int bt=s_s16by-40;{ int _fr16 = (SKILL16_ID >= 0 && SKILL16_ID < 400) ? g_icoMap[SKILL16_ID] : -1; if(g_archIcons && _fr16 >= 0){__try{DrawCel(g_archIcons,_fr16,s_s16bx,s_s16by);}__except(1){if(fnRect)fnRect(s_s16bx,bt,s_s16bx+40,s_s16by,1,5);}}else{if(fnRect)fnRect(s_s16bx,bt,s_s16bx+40,s_s16by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s16lv);fnFont(1);fnText(lb,s_s16lx,s_s16ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s16bx&&mx<s_s16bx+40&&my>=bt&&my<s_s16by);if(hv){g_tooltipSkillId=SKILL16_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s16lv==0){__try{PatchSkillForPlayer(SKILL16_ID);InsertSkillInClassList(SKILL16_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL16_ID);}__except(1){}s_s16lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill16_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s16lv);fclose(f);}}}}}}}}
            {static BOOL s_s17L=FALSE;static int s_s17bx=0,s_s17by=0,s_s17lx=0,s_s17ly=0,s_s17lv=0;static BOOL s_s17A=FALSE;
            #define SKILL17_ID 53
            if(g_skillPanelReset){s_s17L=FALSE;s_s17A=FALSE;s_s17lv=0;}if(!s_s17L){s_s17L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn17X","0",b2,31,i2);s_s17bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn17Y","0",b2,31,i2);s_s17by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl17X","0",b2,31,i2);s_s17lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl17Y","0",b2,31,i2);s_s17ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill17_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s17lv);fclose(f);}}}if(!s_s17A&&s_s17lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL17_ID);InsertSkillInClassList(SKILL17_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s17A=TRUE;__try{PatchSkillForPlayer(SKILL17_ID);InsertSkillInClassList(SKILL17_ID);for(int i=0;i<s_s17lv;i++)SendSpendSkillPacket(SKILL17_ID);}__except(1){}}}if(s_s17bx!=0||s_s17by!=0){int bt=s_s17by-40;{ int _fr17 = (SKILL17_ID >= 0 && SKILL17_ID < 400) ? g_icoMap[SKILL17_ID] : -1; if(g_archIcons && _fr17 >= 0){__try{DrawCel(g_archIcons,_fr17,s_s17bx,s_s17by);}__except(1){if(fnRect)fnRect(s_s17bx,bt,s_s17bx+40,s_s17by,1,5);}}else{if(fnRect)fnRect(s_s17bx,bt,s_s17bx+40,s_s17by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s17lv);fnFont(1);fnText(lb,s_s17lx,s_s17ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s17bx&&mx<s_s17bx+40&&my>=bt&&my<s_s17by);if(hv){g_tooltipSkillId=SKILL17_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s17lv==0){__try{PatchSkillForPlayer(SKILL17_ID);InsertSkillInClassList(SKILL17_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL17_ID);}__except(1){}s_s17lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill17_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s17lv);fclose(f);}}}}}}}}
            {static BOOL s_s18L=FALSE;static int s_s18bx=0,s_s18by=0,s_s18lx=0,s_s18ly=0,s_s18lv=0;static BOOL s_s18A=FALSE;
            #define SKILL18_ID 54
            if(g_skillPanelReset){s_s18L=FALSE;s_s18A=FALSE;s_s18lv=0;}if(!s_s18L){s_s18L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn18X","0",b2,31,i2);s_s18bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn18Y","0",b2,31,i2);s_s18by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl18X","0",b2,31,i2);s_s18lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl18Y","0",b2,31,i2);s_s18ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill18_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s18lv);fclose(f);}}}if(!s_s18A&&s_s18lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL18_ID);InsertSkillInClassList(SKILL18_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s18A=TRUE;__try{PatchSkillForPlayer(SKILL18_ID);InsertSkillInClassList(SKILL18_ID);for(int i=0;i<s_s18lv;i++)SendSpendSkillPacket(SKILL18_ID);}__except(1){}}}if(s_s18bx!=0||s_s18by!=0){int bt=s_s18by-40;{ int _fr18 = (SKILL18_ID >= 0 && SKILL18_ID < 400) ? g_icoMap[SKILL18_ID] : -1; if(g_archIcons && _fr18 >= 0){__try{DrawCel(g_archIcons,_fr18,s_s18bx,s_s18by);}__except(1){if(fnRect)fnRect(s_s18bx,bt,s_s18bx+40,s_s18by,1,5);}}else{if(fnRect)fnRect(s_s18bx,bt,s_s18bx+40,s_s18by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s18lv);fnFont(1);fnText(lb,s_s18lx,s_s18ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s18bx&&mx<s_s18bx+40&&my>=bt&&my<s_s18by);if(hv){g_tooltipSkillId=SKILL18_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s18lv==0){__try{PatchSkillForPlayer(SKILL18_ID);InsertSkillInClassList(SKILL18_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL18_ID);}__except(1){}s_s18lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill18_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s18lv);fclose(f);}}}}}}}}
            {static BOOL s_s19L=FALSE;static int s_s19bx=0,s_s19by=0,s_s19lx=0,s_s19ly=0,s_s19lv=0;static BOOL s_s19A=FALSE;
            #define SKILL19_ID 55
            if(g_skillPanelReset){s_s19L=FALSE;s_s19A=FALSE;s_s19lv=0;}if(!s_s19L){s_s19L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn19X","0",b2,31,i2);s_s19bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn19Y","0",b2,31,i2);s_s19by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl19X","0",b2,31,i2);s_s19lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl19Y","0",b2,31,i2);s_s19ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill19_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s19lv);fclose(f);}}}if(!s_s19A&&s_s19lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL19_ID);InsertSkillInClassList(SKILL19_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s19A=TRUE;__try{PatchSkillForPlayer(SKILL19_ID);InsertSkillInClassList(SKILL19_ID);for(int i=0;i<s_s19lv;i++)SendSpendSkillPacket(SKILL19_ID);}__except(1){}}}if(s_s19bx!=0||s_s19by!=0){int bt=s_s19by-40;{ int _fr19 = (SKILL19_ID >= 0 && SKILL19_ID < 400) ? g_icoMap[SKILL19_ID] : -1; if(g_archIcons && _fr19 >= 0){__try{DrawCel(g_archIcons,_fr19,s_s19bx,s_s19by);}__except(1){if(fnRect)fnRect(s_s19bx,bt,s_s19bx+40,s_s19by,1,5);}}else{if(fnRect)fnRect(s_s19bx,bt,s_s19bx+40,s_s19by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s19lv);fnFont(1);fnText(lb,s_s19lx,s_s19ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s19bx&&mx<s_s19bx+40&&my>=bt&&my<s_s19by);if(hv){g_tooltipSkillId=SKILL19_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s19lv==0){__try{PatchSkillForPlayer(SKILL19_ID);InsertSkillInClassList(SKILL19_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL19_ID);}__except(1){}s_s19lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill19_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s19lv);fclose(f);}}}}}}}}
            {static BOOL s_s20L=FALSE;static int s_s20bx=0,s_s20by=0,s_s20lx=0,s_s20ly=0,s_s20lv=0;static BOOL s_s20A=FALSE;
            #define SKILL20_ID 56
            if(g_skillPanelReset){s_s20L=FALSE;s_s20A=FALSE;s_s20lv=0;}if(!s_s20L){s_s20L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn20X","0",b2,31,i2);s_s20bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn20Y","0",b2,31,i2);s_s20by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl20X","0",b2,31,i2);s_s20lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl20Y","0",b2,31,i2);s_s20ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill20_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s20lv);fclose(f);}}}if(!s_s20A&&s_s20lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL20_ID);InsertSkillInClassList(SKILL20_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s20A=TRUE;__try{PatchSkillForPlayer(SKILL20_ID);InsertSkillInClassList(SKILL20_ID);for(int i=0;i<s_s20lv;i++)SendSpendSkillPacket(SKILL20_ID);}__except(1){}}}if(s_s20bx!=0||s_s20by!=0){int bt=s_s20by-40;{ int _fr20 = (SKILL20_ID >= 0 && SKILL20_ID < 400) ? g_icoMap[SKILL20_ID] : -1; if(g_archIcons && _fr20 >= 0){__try{DrawCel(g_archIcons,_fr20,s_s20bx,s_s20by);}__except(1){if(fnRect)fnRect(s_s20bx,bt,s_s20bx+40,s_s20by,1,5);}}else{if(fnRect)fnRect(s_s20bx,bt,s_s20bx+40,s_s20by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s20lv);fnFont(1);fnText(lb,s_s20lx,s_s20ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s20bx&&mx<s_s20bx+40&&my>=bt&&my<s_s20by);if(hv){g_tooltipSkillId=SKILL20_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s20lv==0){__try{PatchSkillForPlayer(SKILL20_ID);InsertSkillInClassList(SKILL20_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL20_ID);}__except(1){}s_s20lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill20_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s20lv);fclose(f);}}}}}}}}

            /* === skill-21 thru skill-25 (row 5) === */
            {static BOOL s_s21L=FALSE;static int s_s21bx=0,s_s21by=0,s_s21lx=0,s_s21ly=0,s_s21lv=0;static BOOL s_s21A=FALSE;
            #define SKILL21_ID 57
            if(g_skillPanelReset){s_s21L=FALSE;s_s21A=FALSE;s_s21lv=0;}if(!s_s21L){s_s21L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn21X","0",b2,31,i2);s_s21bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn21Y","0",b2,31,i2);s_s21by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl21X","0",b2,31,i2);s_s21lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl21Y","0",b2,31,i2);s_s21ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill21_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s21lv);fclose(f);}}}if(!s_s21A&&s_s21lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL21_ID);InsertSkillInClassList(SKILL21_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s21A=TRUE;__try{PatchSkillForPlayer(SKILL21_ID);InsertSkillInClassList(SKILL21_ID);for(int i=0;i<s_s21lv;i++)SendSpendSkillPacket(SKILL21_ID);}__except(1){}}}if(s_s21bx!=0||s_s21by!=0){int bt=s_s21by-40;{ int _fr21 = (SKILL21_ID >= 0 && SKILL21_ID < 400) ? g_icoMap[SKILL21_ID] : -1; if(g_archIcons && _fr21 >= 0){__try{DrawCel(g_archIcons,_fr21,s_s21bx,s_s21by);}__except(1){if(fnRect)fnRect(s_s21bx,bt,s_s21bx+40,s_s21by,1,5);}}else{if(fnRect)fnRect(s_s21bx,bt,s_s21bx+40,s_s21by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s21lv);fnFont(1);fnText(lb,s_s21lx,s_s21ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s21bx&&mx<s_s21bx+40&&my>=bt&&my<s_s21by);if(hv){g_tooltipSkillId=SKILL21_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s21lv==0){__try{PatchSkillForPlayer(SKILL21_ID);InsertSkillInClassList(SKILL21_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL21_ID);}__except(1){}s_s21lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill21_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s21lv);fclose(f);}}}}}}}}
            {static BOOL s_s22L=FALSE;static int s_s22bx=0,s_s22by=0,s_s22lx=0,s_s22ly=0,s_s22lv=0;static BOOL s_s22A=FALSE;
            #define SKILL22_ID 58
            if(g_skillPanelReset){s_s22L=FALSE;s_s22A=FALSE;s_s22lv=0;}if(!s_s22L){s_s22L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn22X","0",b2,31,i2);s_s22bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn22Y","0",b2,31,i2);s_s22by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl22X","0",b2,31,i2);s_s22lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl22Y","0",b2,31,i2);s_s22ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill22_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s22lv);fclose(f);}}}if(!s_s22A&&s_s22lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL22_ID);InsertSkillInClassList(SKILL22_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s22A=TRUE;__try{PatchSkillForPlayer(SKILL22_ID);InsertSkillInClassList(SKILL22_ID);for(int i=0;i<s_s22lv;i++)SendSpendSkillPacket(SKILL22_ID);}__except(1){}}}if(s_s22bx!=0||s_s22by!=0){int bt=s_s22by-40;{ int _fr22 = (SKILL22_ID >= 0 && SKILL22_ID < 400) ? g_icoMap[SKILL22_ID] : -1; if(g_archIcons && _fr22 >= 0){__try{DrawCel(g_archIcons,_fr22,s_s22bx,s_s22by);}__except(1){if(fnRect)fnRect(s_s22bx,bt,s_s22bx+40,s_s22by,1,5);}}else{if(fnRect)fnRect(s_s22bx,bt,s_s22bx+40,s_s22by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s22lv);fnFont(1);fnText(lb,s_s22lx,s_s22ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s22bx&&mx<s_s22bx+40&&my>=bt&&my<s_s22by);if(hv){g_tooltipSkillId=SKILL22_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s22lv==0){__try{PatchSkillForPlayer(SKILL22_ID);InsertSkillInClassList(SKILL22_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL22_ID);}__except(1){}s_s22lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill22_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s22lv);fclose(f);}}}}}}}}
            {static BOOL s_s23L=FALSE;static int s_s23bx=0,s_s23by=0,s_s23lx=0,s_s23ly=0,s_s23lv=0;static BOOL s_s23A=FALSE;
            #define SKILL23_ID 59
            if(g_skillPanelReset){s_s23L=FALSE;s_s23A=FALSE;s_s23lv=0;}if(!s_s23L){s_s23L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn23X","0",b2,31,i2);s_s23bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn23Y","0",b2,31,i2);s_s23by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl23X","0",b2,31,i2);s_s23lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl23Y","0",b2,31,i2);s_s23ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill23_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s23lv);fclose(f);}}}if(!s_s23A&&s_s23lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL23_ID);InsertSkillInClassList(SKILL23_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s23A=TRUE;__try{PatchSkillForPlayer(SKILL23_ID);InsertSkillInClassList(SKILL23_ID);for(int i=0;i<s_s23lv;i++)SendSpendSkillPacket(SKILL23_ID);}__except(1){}}}if(s_s23bx!=0||s_s23by!=0){int bt=s_s23by-40;{ int _fr23 = (SKILL23_ID >= 0 && SKILL23_ID < 400) ? g_icoMap[SKILL23_ID] : -1; if(g_archIcons && _fr23 >= 0){__try{DrawCel(g_archIcons,_fr23,s_s23bx,s_s23by);}__except(1){if(fnRect)fnRect(s_s23bx,bt,s_s23bx+40,s_s23by,1,5);}}else{if(fnRect)fnRect(s_s23bx,bt,s_s23bx+40,s_s23by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s23lv);fnFont(1);fnText(lb,s_s23lx,s_s23ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s23bx&&mx<s_s23bx+40&&my>=bt&&my<s_s23by);if(hv){g_tooltipSkillId=SKILL23_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s23lv==0){__try{PatchSkillForPlayer(SKILL23_ID);InsertSkillInClassList(SKILL23_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL23_ID);}__except(1){}s_s23lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill23_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s23lv);fclose(f);}}}}}}}}
            {static BOOL s_s24L=FALSE;static int s_s24bx=0,s_s24by=0,s_s24lx=0,s_s24ly=0,s_s24lv=0;static BOOL s_s24A=FALSE;
            #define SKILL24_ID 60
            if(g_skillPanelReset){s_s24L=FALSE;s_s24A=FALSE;s_s24lv=0;}if(!s_s24L){s_s24L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn24X","0",b2,31,i2);s_s24bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn24Y","0",b2,31,i2);s_s24by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl24X","0",b2,31,i2);s_s24lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl24Y","0",b2,31,i2);s_s24ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill24_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s24lv);fclose(f);}}}if(!s_s24A&&s_s24lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL24_ID);InsertSkillInClassList(SKILL24_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s24A=TRUE;__try{PatchSkillForPlayer(SKILL24_ID);InsertSkillInClassList(SKILL24_ID);for(int i=0;i<s_s24lv;i++)SendSpendSkillPacket(SKILL24_ID);}__except(1){}}}if(s_s24bx!=0||s_s24by!=0){int bt=s_s24by-40;{ int _fr24 = (SKILL24_ID >= 0 && SKILL24_ID < 400) ? g_icoMap[SKILL24_ID] : -1; if(g_archIcons && _fr24 >= 0){__try{DrawCel(g_archIcons,_fr24,s_s24bx,s_s24by);}__except(1){if(fnRect)fnRect(s_s24bx,bt,s_s24bx+40,s_s24by,1,5);}}else{if(fnRect)fnRect(s_s24bx,bt,s_s24bx+40,s_s24by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s24lv);fnFont(1);fnText(lb,s_s24lx,s_s24ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s24bx&&mx<s_s24bx+40&&my>=bt&&my<s_s24by);if(hv){g_tooltipSkillId=SKILL24_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s24lv==0){__try{PatchSkillForPlayer(SKILL24_ID);InsertSkillInClassList(SKILL24_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL24_ID);}__except(1){}s_s24lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill24_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s24lv);fclose(f);}}}}}}}}
            {static BOOL s_s25L=FALSE;static int s_s25bx=0,s_s25by=0,s_s25lx=0,s_s25ly=0,s_s25lv=0;static BOOL s_s25A=FALSE;
            #define SKILL25_ID 61
            if(g_skillPanelReset){s_s25L=FALSE;s_s25A=FALSE;s_s25lv=0;}if(!s_s25L){s_s25L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn25X","0",b2,31,i2);s_s25bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn25Y","0",b2,31,i2);s_s25by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl25X","0",b2,31,i2);s_s25lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl25Y","0",b2,31,i2);s_s25ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill25_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s25lv);fclose(f);}}}if(!s_s25A&&s_s25lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL25_ID);InsertSkillInClassList(SKILL25_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s25A=TRUE;__try{PatchSkillForPlayer(SKILL25_ID);InsertSkillInClassList(SKILL25_ID);for(int i=0;i<s_s25lv;i++)SendSpendSkillPacket(SKILL25_ID);}__except(1){}}}if(s_s25bx!=0||s_s25by!=0){int bt=s_s25by-40;{ int _fr25 = (SKILL25_ID >= 0 && SKILL25_ID < 400) ? g_icoMap[SKILL25_ID] : -1; if(g_archIcons && _fr25 >= 0){__try{DrawCel(g_archIcons,_fr25,s_s25bx,s_s25by);}__except(1){if(fnRect)fnRect(s_s25bx,bt,s_s25bx+40,s_s25by,1,5);}}else{if(fnRect)fnRect(s_s25bx,bt,s_s25bx+40,s_s25by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s25lv);fnFont(1);fnText(lb,s_s25lx,s_s25ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s25bx&&mx<s_s25bx+40&&my>=bt&&my<s_s25by);if(hv){g_tooltipSkillId=SKILL25_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s25lv==0){__try{PatchSkillForPlayer(SKILL25_ID);InsertSkillInClassList(SKILL25_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL25_ID);}__except(1){}s_s25lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill25_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s25lv);fclose(f);}}}}}}}}

            /* === skill-26 thru skill-30 (row 6) === */
            {static BOOL s_s26L=FALSE;static int s_s26bx=0,s_s26by=0,s_s26lx=0,s_s26ly=0,s_s26lv=0;static BOOL s_s26A=FALSE;
            #define SKILL26_ID 62
            if(g_skillPanelReset){s_s26L=FALSE;s_s26A=FALSE;s_s26lv=0;}if(!s_s26L){s_s26L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn26X","0",b2,31,i2);s_s26bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn26Y","0",b2,31,i2);s_s26by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl26X","0",b2,31,i2);s_s26lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl26Y","0",b2,31,i2);s_s26ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill26_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s26lv);fclose(f);}}}if(!s_s26A&&s_s26lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL26_ID);InsertSkillInClassList(SKILL26_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s26A=TRUE;__try{PatchSkillForPlayer(SKILL26_ID);InsertSkillInClassList(SKILL26_ID);for(int i=0;i<s_s26lv;i++)SendSpendSkillPacket(SKILL26_ID);}__except(1){}}}if(s_s26bx!=0||s_s26by!=0){int bt=s_s26by-40;{ int _fr26 = (SKILL26_ID >= 0 && SKILL26_ID < 400) ? g_icoMap[SKILL26_ID] : -1; if(g_archIcons && _fr26 >= 0){__try{DrawCel(g_archIcons,_fr26,s_s26bx,s_s26by);}__except(1){if(fnRect)fnRect(s_s26bx,bt,s_s26bx+40,s_s26by,1,5);}}else{if(fnRect)fnRect(s_s26bx,bt,s_s26bx+40,s_s26by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s26lv);fnFont(1);fnText(lb,s_s26lx,s_s26ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s26bx&&mx<s_s26bx+40&&my>=bt&&my<s_s26by);if(hv){g_tooltipSkillId=SKILL26_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s26lv==0){__try{PatchSkillForPlayer(SKILL26_ID);InsertSkillInClassList(SKILL26_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL26_ID);}__except(1){}s_s26lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill26_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s26lv);fclose(f);}}}}}}}}
            {static BOOL s_s27L=FALSE;static int s_s27bx=0,s_s27by=0,s_s27lx=0,s_s27ly=0,s_s27lv=0;static BOOL s_s27A=FALSE;
            #define SKILL27_ID 63
            if(g_skillPanelReset){s_s27L=FALSE;s_s27A=FALSE;s_s27lv=0;}if(!s_s27L){s_s27L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn27X","0",b2,31,i2);s_s27bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn27Y","0",b2,31,i2);s_s27by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl27X","0",b2,31,i2);s_s27lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl27Y","0",b2,31,i2);s_s27ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill27_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s27lv);fclose(f);}}}if(!s_s27A&&s_s27lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL27_ID);InsertSkillInClassList(SKILL27_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s27A=TRUE;__try{PatchSkillForPlayer(SKILL27_ID);InsertSkillInClassList(SKILL27_ID);for(int i=0;i<s_s27lv;i++)SendSpendSkillPacket(SKILL27_ID);}__except(1){}}}if(s_s27bx!=0||s_s27by!=0){int bt=s_s27by-40;{ int _fr27 = (SKILL27_ID >= 0 && SKILL27_ID < 400) ? g_icoMap[SKILL27_ID] : -1; if(g_archIcons && _fr27 >= 0){__try{DrawCel(g_archIcons,_fr27,s_s27bx,s_s27by);}__except(1){if(fnRect)fnRect(s_s27bx,bt,s_s27bx+40,s_s27by,1,5);}}else{if(fnRect)fnRect(s_s27bx,bt,s_s27bx+40,s_s27by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s27lv);fnFont(1);fnText(lb,s_s27lx,s_s27ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s27bx&&mx<s_s27bx+40&&my>=bt&&my<s_s27by);if(hv){g_tooltipSkillId=SKILL27_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s27lv==0){__try{PatchSkillForPlayer(SKILL27_ID);InsertSkillInClassList(SKILL27_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL27_ID);}__except(1){}s_s27lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill27_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s27lv);fclose(f);}}}}}}}}
            {static BOOL s_s28L=FALSE;static int s_s28bx=0,s_s28by=0,s_s28lx=0,s_s28ly=0,s_s28lv=0;static BOOL s_s28A=FALSE;
            #define SKILL28_ID 64
            if(g_skillPanelReset){s_s28L=FALSE;s_s28A=FALSE;s_s28lv=0;}if(!s_s28L){s_s28L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn28X","0",b2,31,i2);s_s28bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn28Y","0",b2,31,i2);s_s28by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl28X","0",b2,31,i2);s_s28lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl28Y","0",b2,31,i2);s_s28ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill28_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s28lv);fclose(f);}}}if(!s_s28A&&s_s28lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL28_ID);InsertSkillInClassList(SKILL28_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s28A=TRUE;__try{PatchSkillForPlayer(SKILL28_ID);InsertSkillInClassList(SKILL28_ID);for(int i=0;i<s_s28lv;i++)SendSpendSkillPacket(SKILL28_ID);}__except(1){}}}if(s_s28bx!=0||s_s28by!=0){int bt=s_s28by-40;{ int _fr28 = (SKILL28_ID >= 0 && SKILL28_ID < 400) ? g_icoMap[SKILL28_ID] : -1; if(g_archIcons && _fr28 >= 0){__try{DrawCel(g_archIcons,_fr28,s_s28bx,s_s28by);}__except(1){if(fnRect)fnRect(s_s28bx,bt,s_s28bx+40,s_s28by,1,5);}}else{if(fnRect)fnRect(s_s28bx,bt,s_s28bx+40,s_s28by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s28lv);fnFont(1);fnText(lb,s_s28lx,s_s28ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s28bx&&mx<s_s28bx+40&&my>=bt&&my<s_s28by);if(hv){g_tooltipSkillId=SKILL28_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s28lv==0){__try{PatchSkillForPlayer(SKILL28_ID);InsertSkillInClassList(SKILL28_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL28_ID);}__except(1){}s_s28lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill28_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s28lv);fclose(f);}}}}}}}}
            {static BOOL s_s29L=FALSE;static int s_s29bx=0,s_s29by=0,s_s29lx=0,s_s29ly=0,s_s29lv=0;static BOOL s_s29A=FALSE;
            #define SKILL29_ID 65
            if(g_skillPanelReset){s_s29L=FALSE;s_s29A=FALSE;s_s29lv=0;}if(!s_s29L){s_s29L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn29X","0",b2,31,i2);s_s29bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn29Y","0",b2,31,i2);s_s29by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl29X","0",b2,31,i2);s_s29lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl29Y","0",b2,31,i2);s_s29ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill29_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s29lv);fclose(f);}}}if(!s_s29A&&s_s29lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL29_ID);InsertSkillInClassList(SKILL29_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s29A=TRUE;__try{PatchSkillForPlayer(SKILL29_ID);InsertSkillInClassList(SKILL29_ID);for(int i=0;i<s_s29lv;i++)SendSpendSkillPacket(SKILL29_ID);}__except(1){}}}if(s_s29bx!=0||s_s29by!=0){int bt=s_s29by-40;{ int _fr29 = (SKILL29_ID >= 0 && SKILL29_ID < 400) ? g_icoMap[SKILL29_ID] : -1; if(g_archIcons && _fr29 >= 0){__try{DrawCel(g_archIcons,_fr29,s_s29bx,s_s29by);}__except(1){if(fnRect)fnRect(s_s29bx,bt,s_s29bx+40,s_s29by,1,5);}}else{if(fnRect)fnRect(s_s29bx,bt,s_s29bx+40,s_s29by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s29lv);fnFont(1);fnText(lb,s_s29lx,s_s29ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s29bx&&mx<s_s29bx+40&&my>=bt&&my<s_s29by);if(hv){g_tooltipSkillId=SKILL29_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s29lv==0){__try{PatchSkillForPlayer(SKILL29_ID);InsertSkillInClassList(SKILL29_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL29_ID);}__except(1){}s_s29lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill29_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s29lv);fclose(f);}}}}}}}}
            {static BOOL s_s30L=FALSE;static int s_s30bx=0,s_s30by=0,s_s30lx=0,s_s30ly=0,s_s30lv=0;static BOOL s_s30A=FALSE;
            #define SKILL30_ID 66
            if(g_skillPanelReset){s_s30L=FALSE;s_s30A=FALSE;s_s30lv=0;}if(!s_s30L){s_s30L=TRUE;char i2[MAX_PATH],b2[32];GetCharFileDir(i2,MAX_PATH);strcat(i2,"d2arch.ini");GetPrivateProfileStringA("TestButton","Btn30X","0",b2,31,i2);s_s30bx=atoi(b2);GetPrivateProfileStringA("TestButton","Btn30Y","0",b2,31,i2);s_s30by=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl30X","0",b2,31,i2);s_s30lx=atoi(b2);GetPrivateProfileStringA("TestButton","Lbl30Y","0",b2,31,i2);s_s30ly=atoi(b2);if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill30_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"r");if(f){fscanf(f,"%d",&s_s30lv);fclose(f);}}}if(!s_s30A&&s_s30lv>0&&Player()){static DWORD w=0;static BOOL p=FALSE;if(!p){p=TRUE;w=GetTickCount();__try{PatchSkillForPlayer(SKILL30_ID);InsertSkillInClassList(SKILL30_ID);}__except(1){}}if(p&&w>0&&GetTickCount()-w>=2000){s_s30A=TRUE;__try{PatchSkillForPlayer(SKILL30_ID);InsertSkillInClassList(SKILL30_ID);for(int i=0;i<s_s30lv;i++)SendSpendSkillPacket(SKILL30_ID);}__except(1){}}}if(s_s30bx!=0||s_s30by!=0){int bt=s_s30by-40;{ int _fr30 = (SKILL30_ID >= 0 && SKILL30_ID < 400) ? g_icoMap[SKILL30_ID] : -1; if(g_archIcons && _fr30 >= 0){__try{DrawCel(g_archIcons,_fr30,s_s30bx,s_s30by);}__except(1){if(fnRect)fnRect(s_s30bx,bt,s_s30bx+40,s_s30by,1,5);}}else{if(fnRect)fnRect(s_s30bx,bt,s_s30bx+40,s_s30by,1,5);}}if(fnText&&fnFont){wchar_t lb[8];wsprintfW(lb,L"%d",s_s30lv);fnFont(1);fnText(lb,s_s30lx,s_s30ly,0,0);}{POINT pt;GetCursorPos(&pt);HWND hW=GetForegroundWindow();ScreenToClient(hW,&pt);RECT rc;GetClientRect(hW,&rc);int cw=rc.right-rc.left,ch=rc.bottom-rc.top;float sx=(float)cw/(float)g_screenW,sy=(float)ch/(float)g_screenH,sc=(sx<sy)?sx:sy;int ox=(int)((cw-(float)g_screenW*sc)/2.0f),oy=(int)((ch-(float)g_screenH*sc)/2.0f);int mx=(sc>0)?(int)((pt.x-ox)/sc):pt.x,my=(sc>0)?(int)((pt.y-oy)/sc):pt.y;if(mx<0)mx=0;if(my<0)my=0;BOOL hv=(mx>=s_s30bx&&mx<s_s30bx+40&&my>=bt&&my<s_s30by);if(hv){g_tooltipSkillId=SKILL30_ID;g_tooltipMX=mx;g_tooltipMY=my;}if(hv&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)){static DWORD lc=0;DWORD now=GetTickCount();if(now-lc>500){lc=now;int pts=0;if(fnGetStat&&Player())__try{pts=fnGetStat(Player(),5,0);}__except(1){}if(pts>0){if(s_s30lv==0){__try{PatchSkillForPlayer(SKILL30_ID);InsertSkillInClassList(SKILL30_ID);}__except(1){}}__try{SendSpendSkillPacket(SKILL30_ID);}__except(1){}s_s30lv++;if(g_charName[0]){char sp[MAX_PATH];GetCharFileDir(sp,MAX_PATH);strcat(sp,"d2arch_skill30_");strcat(sp,g_charName);strcat(sp,".dat");FILE*f=fopen(sp,"w");if(f){fprintf(f,"%d",s_s30lv);fclose(f);}}}}}}}}


            } /* === END DISABLED old hardcoded skills 2-30 === */

            /* === FORCE CORRECT ICONCEL — DYNAMIC from g_tabSlots === */
            {
                DWORD _dt = GetSgptDT();
                if (_dt) {
                    /* nIconCel loop removed — D2's vanilla UI uses original values */
                }
            }

                /* === SKILL POINTS AVAILABLE LABEL === */
                {
                    static BOOL s_spLoaded = FALSE;
                    static int s_spLblX = 555, s_spLblY = 500;

                    if (!s_spLoaded) {
                        s_spLoaded = TRUE;
                        char ini3[MAX_PATH], buf3[32];
                        GetArchDir(ini3, MAX_PATH);
                        strcat(ini3, "d2arch.ini");
                        GetPrivateProfileStringA("TestButton", "SpLblX", "555", buf3, 31, ini3);
                        s_spLblX = atoi(buf3);
                        GetPrivateProfileStringA("TestButton", "SpLblY", "500", buf3, 31, ini3);
                        s_spLblY = atoi(buf3);
                    }

                    if (fnText && fnFont) {
                        int sp = 0;
                        if (fnGetStat && Player()) {
                            __try { sp = fnGetStat(Player(), 5, 0); } __except(1) {}
                        }
                        wchar_t spBuf[32];
                        wsprintfW(spBuf, L"Skill Points: %d", sp);
                        fnFont(1);
                        fnText(spBuf, s_spLblX, s_spLblY, 0, 0);
                    }
                }

                /* TOOLTIP: moved outside panelOpen block for reliable execution */
            }
        }
    }

    /* AP reconnect moved to Page 2 of skill editor book */

    /* === CHEAT MENU (Ctrl+V) — draggable, TABBED, character-frozen === */
    if (g_cheatMenuOpen && fnRect && fnText && fnFont) {
        /* 1.9.1 — accumulator for mouse-wheel deltas while the menu is
         * open. WndProc (d2arch_main.c WM_MOUSEWHEEL) writes here; the
         * Loot tab's scrollable Sets/Uniques lists drain it each frame. */
        extern int g_cheatMenuWheelDelta;
        static int s_cheatX = -1, s_cheatY = -1;
        static BOOL s_cheatDrag = FALSE;
        static int s_cheatDragOX = 0, s_cheatDragOY = 0;
        static int s_activeTab = 0;  /* 0=Char 1=Combat 2=Items 3=Pand 4=Teleport 5=Portals 6=Loot 7=Mons */
        static int s_tpSubTab = 0;   /* Teleport sub-tab: 0=Act1 1=Act2 2=Act3 3=Act4 4=Act5 5=Secrets */
        /* 1.9.1 — Loot tab sub-tab: 0=Sets 1=Uniques 2=Runes 3=Gems 4=Misc.
         * Sets + Uniques pages are scrollable lists (127 / ~385 entries);
         * the rest are fixed grids that fit without scroll. */
        static int s_lootSubTab = 0;
        static int s_lootScrollSet = 0;     /* pixel offset into the Sets list */
        static int s_lootScrollUnique = 0;  /* pixel offset into the Uniques list */
        /* 1.9.2 — Mons tab sub-tab: 0=SUs 1=Bosses 2=Normal 3=Random. */
        static int s_monsSubTab = 0;
        static int s_monsScrollSU = 0;     /* pixel offset into the SU list */
        static int s_monsScrollNormal = 0; /* pixel offset into the Normal monster list */

        #define MK_HDR  1   /* full-width section header */
        #define MK_BTN  2   /* clickable button */
        #define MK_GAP  3   /* empty cell (column placeholder) */

        struct CheatCell { int kind; const wchar_t* label; int cmd; const char* notify; };

        /* cmd encoding:
         *  100..105 = +Gold/+Stat/+Skill/+Lvl/Heal/Unlock
         *  110/111  = Trap SU / Trap Mon
         *  120..124 = Loot Andariel..Baal
         *  201..213 = item-drop commands (see g_cheatItemCmd values)
         *  999      = Close
         */
        /* TAB 0 — CHARACTER */
        static const struct CheatCell TAB_CHARACTER[] = {
            { MK_HDR, L"CHARACTER",        0, NULL },
            { MK_BTN, L"+1000 Gold",     100, "+1000 Gold" },
            { MK_BTN, L"+5 Stat Pts",    101, "+5 Stat Points" },
            { MK_BTN, L"+5 Skill Pts",   102, "+5 Skill Points" },
            { MK_BTN, L"+10 Levels",     103, "+10 Levels XP" },
            { MK_BTN, L"Heal Full",      104, "Healed!" },
            { MK_BTN, L"Unlock Skills",  105, "All 210 skills unlocked!" },
            { MK_BTN, L"Close",          999, NULL },
        };

        /* TAB 1 — COMBAT (traps + boss loot) */
        static const struct CheatCell TAB_COMBAT[] = {
            { MK_HDR, L"TRAPS",            0, NULL },
            { MK_BTN, L"Spawn SU",       110, "Spawning SuperUnique..." },
            { MK_BTN, L"Spawn Monsters", 111, "Spawning monsters..." },
            { MK_GAP, NULL,                0, NULL },
            { MK_HDR, L"BOSS LOOT",        0, NULL },
            { MK_BTN, L"Andariel",       120, "Andariel loot!" },
            { MK_BTN, L"Duriel",         121, "Duriel loot!" },
            { MK_BTN, L"Mephisto",       122, "Mephisto loot!" },
            { MK_BTN, L"Diablo",         123, "Diablo loot!" },
            { MK_BTN, L"Baal",           124, "Baal loot!" },
            { MK_BTN, L"Close",          999, NULL },
        };

        /* TAB 2 — ITEMS (runes + bases + gems + pots) */
        static const struct CheatCell TAB_ITEMS[] = {
            { MK_HDR, L"RUNES",            0, NULL },
            { MK_BTN, L"Cube",           201, "Horadric Cube" },
            { MK_BTN, L"Low (1-10)",     202, "Low runes" },
            { MK_BTN, L"Mid (11-20)",    203, "Mid runes" },
            { MK_BTN, L"High (21-33)",   204, "High runes" },
            { MK_BTN, L"All 33 Runes",   205, "All 33 runes!" },
            { MK_GAP, NULL,                0, NULL },
            { MK_HDR, L"BASES (3 sox+)",   0, NULL },
            { MK_BTN, L"Body Armor",     206, "Body armor bases" },
            { MK_BTN, L"Helms",          207, "Helm bases" },
            { MK_BTN, L"Weapons",        208, "Weapon bases" },
            { MK_BTN, L"All Bases",      209, "All bases" },
            { MK_GAP, NULL,                0, NULL },
            { MK_HDR, L"GOODIES",          0, NULL },
            { MK_BTN, L"All 35 Gems",    210, "All gems!" },
            { MK_BTN, L"Healing Pots",   211, "Healing potions" },
            { MK_BTN, L"Mana Pots",      212, "Mana potions" },
            { MK_BTN, L"Recipe Pots",    213, "Recipe pots (vps+wms)" },
            { MK_BTN, L"Close",          999, NULL },
        };

        /* TAB 3 — PANDEMONIUM (event items + uber spawns) */
        static const struct CheatCell TAB_PANDEMONIUM[] = {
            { MK_HDR, L"EVENT ITEMS",      0, NULL },
            { MK_BTN, L"3 Keys",         220, "pk1+pk2+pk3 dropped" },
            { MK_BTN, L"3 Organs",       221, "bey+mbr+dhn dropped" },
            { MK_BTN, L"4 Essences",     222, "tes+ceh+bet+fed dropped" },
            { MK_BTN, L"Hellfire Torch", 223, "Hellfire Torch dropped" },
            { MK_BTN, L"Token of Absol", 224, "Token of Absolution dropped" },
            { MK_GAP, NULL,                0, NULL },
            { MK_HDR, L"UBER SPAWNS (NEAR YOU)", 0, NULL },
            { MK_BTN, L"Lilith",         230, "Spawning Lilith..." },
            { MK_BTN, L"Uber Duriel",    231, "Spawning Uber Duriel..." },
            { MK_BTN, L"Uber Izual",     232, "Spawning Uber Izual..." },
            { MK_BTN, L"Uber Mephisto",  233, "Spawning Uber Mephisto..." },
            { MK_BTN, L"Uber Diablo",    234, "Spawning Uber Diablo..." },
            { MK_BTN, L"Uber Baal",      235, "Spawning Uber Baal..." },
            { MK_BTN, L"Mini Trio",      240, "Lilith + Duriel + Izual..." },
            { MK_BTN, L"Final Trio",     241, "Mephisto + Diablo + Baal..." },
            { MK_BTN, L"Close",          999, NULL },
        };

        /* TAB 4 — TELEPORT, broken into Act sub-tabs.
         * cmd = 300 + areaId. Handler subtracts 300 to get area.
         * Area IDs match Levels.txt (verified against 1.10f vanilla).
         * Sub-tab is selected via s_tpSubTab (0=Act1..4=Act5, 5=Secrets). */
        static const struct CheatCell TP_ACT1[] = {
            { MK_HDR, L"ACT 1 LOCATIONS",       0, NULL },
            { MK_BTN, L"Rogue Encampment",    301, "Warp: Rogue Camp" },
            { MK_BTN, L"Blood Moor",          302, "Warp: Blood Moor" },
            { MK_BTN, L"Cold Plains",         303, "Warp: Cold Plains" },
            { MK_BTN, L"Stony Field",         304, "Warp: Stony Field" },
            { MK_BTN, L"Dark Wood",           305, "Warp: Dark Wood" },
            { MK_BTN, L"Black Marsh",         306, "Warp: Black Marsh" },
            { MK_BTN, L"Tamoe Highland",      307, "Warp: Tamoe Highland" },
            { MK_BTN, L"Den of Evil",         308, "Warp: Den of Evil" },
            { MK_BTN, L"Cave Lvl 1",          309, "Warp: Cave L1" },
            { MK_BTN, L"Underground Passage", 310, "Warp: Underground L1" },
            { MK_BTN, L"Hole Lvl 1",          311, "Warp: Hole L1" },
            { MK_BTN, L"Pit Lvl 1",           312, "Warp: Pit L1" },
            { MK_BTN, L"Forgotten Tower L1",  313, "Warp: Tower Cellar L1" },
            { MK_BTN, L"Tristram",            338, "Warp: Tristram" },
            { MK_BTN, L"Catacombs L4 (Anda)", 336, "Warp: Andariel" },
            { MK_BTN, L"Close",               999, NULL },
        };
        static const struct CheatCell TP_ACT2[] = {
            { MK_HDR, L"ACT 2 LOCATIONS",       0, NULL },
            { MK_BTN, L"Lut Gholein",         340, "Warp: Lut Gholein" },
            { MK_BTN, L"Rocky Waste",         341, "Warp: Rocky Waste" },
            { MK_BTN, L"Dry Hills",           342, "Warp: Dry Hills" },
            { MK_BTN, L"Far Oasis",           343, "Warp: Far Oasis" },
            { MK_BTN, L"Lost City",           344, "Warp: Lost City" },
            { MK_BTN, L"Valley Snakes",       345, "Warp: Valley of Snakes" },
            { MK_BTN, L"Canyon Magi",         346, "Warp: Canyon of Magi" },
            { MK_BTN, L"Sewers L1",           347, "Warp: A2 Sewers L1" },
            { MK_BTN, L"Harem L1",            350, "Warp: Harem L1" },
            { MK_BTN, L"Palace Cellar L1",    352, "Warp: Palace Cellar L1" },
            { MK_BTN, L"Stony Tomb L1",       355, "Warp: Stony Tomb L1" },
            { MK_BTN, L"Halls of Dead L1",    357, "Warp: Halls of Dead L1" },
            { MK_BTN, L"Maggot Lair L1",      361, "Warp: Maggot Lair L1" },
            { MK_BTN, L"Ancient Tunnels",     365, "Warp: Ancient Tunnels" },
            { MK_BTN, L"Tal Rasha Tomb 1",    66,  "Warp: Tomb 1" },
            { MK_BTN, L"Duriel's Lair",       373, "Warp: Duriel" },
            { MK_BTN, L"Arcane Sanctuary",    374, "Warp: Arcane" },
            { MK_BTN, L"Close",               999, NULL },
        };
        static const struct CheatCell TP_ACT3[] = {
            { MK_HDR, L"ACT 3 LOCATIONS",       0, NULL },
            { MK_BTN, L"Kurast Docks",        375, "Warp: Kurast Docks" },
            { MK_BTN, L"Spider Forest",       376, "Warp: Spider Forest" },
            { MK_BTN, L"Great Marsh",         377, "Warp: Great Marsh" },
            { MK_BTN, L"Flayer Jungle",       378, "Warp: Flayer Jungle" },
            { MK_BTN, L"Lower Kurast",        379, "Warp: Lower Kurast" },
            { MK_BTN, L"Kurast Bazaar",       380, "Warp: Kurast Bazaar" },
            { MK_BTN, L"Upper Kurast",        381, "Warp: Upper Kurast" },
            { MK_BTN, L"Kurast Causeway",     382, "Warp: Kurast Causeway" },
            { MK_BTN, L"Travincal",           383, "Warp: Travincal" },
            { MK_BTN, L"Spider Cave",         384, "Warp: Spider Cave" },
            { MK_BTN, L"Spider Cavern",       385, "Warp: Spider Cavern" },
            { MK_BTN, L"Swampy Pit L1",       386, "Warp: Swampy Pit L1" },
            { MK_BTN, L"Flayer Dungeon L1",   389, "Warp: Flayer Dungeon" },
            { MK_BTN, L"A3 Sewers L1",        392, "Warp: Kurast Sewers" },
            { MK_BTN, L"Durance L1",          400, "Warp: Durance L1" },
            { MK_BTN, L"Durance L3 (Meph)",   402, "Warp: Mephisto" },
            { MK_BTN, L"Close",               999, NULL },
        };
        static const struct CheatCell TP_ACT4[] = {
            { MK_HDR, L"ACT 4 LOCATIONS",       0, NULL },
            { MK_BTN, L"Pandemonium Fortress",403, "Warp: Pandemonium" },
            { MK_BTN, L"City of Damned",      404, "Warp: City of Damned" },
            { MK_BTN, L"River of Flame",      405, "Warp: River of Flame" },
            { MK_BTN, L"Chaos Sanctuary",     406, "Warp: Chaos (Diablo)" },
            { MK_BTN, L"Outer Steppes",       403, "Warp: Outer Steppes" },
            { MK_BTN, L"Plains of Despair",   404, "Warp: Plains of Despair" },
            { MK_BTN, L"Close",               999, NULL },
        };
        static const struct CheatCell TP_ACT5[] = {
            { MK_HDR, L"ACT 5 LOCATIONS",       0, NULL },
            { MK_BTN, L"Harrogath",           409, "Warp: Harrogath" },
            { MK_BTN, L"Bloody Foothills",    410, "Warp: Bloody Foothills" },
            { MK_BTN, L"Frigid Highlands",    411, "Warp: Frigid Highlands" },
            { MK_BTN, L"Arreat Plateau",      412, "Warp: Arreat Plateau" },
            { MK_BTN, L"Crystallized Cavern", 413, "Warp: Crystal Cavern" },
            { MK_BTN, L"Glacial Trail",       415, "Warp: Glacial Trail" },
            { MK_BTN, L"Frozen Tundra",       417, "Warp: Frozen Tundra" },
            { MK_BTN, L"Ancient's Way",       418, "Warp: Ancient's Way" },
            { MK_BTN, L"Icy Cellar",          419, "Warp: Icy Cellar" },
            { MK_BTN, L"Arreat Summit",       420, "Warp: Arreat Summit" },
            { MK_BTN, L"Nihlathak Temple",    421, "Warp: Nihlathak" },
            { MK_BTN, L"Halls of Anguish",    422, "Warp: Halls of Anguish" },
            { MK_BTN, L"Halls of Pain",       423, "Warp: Halls of Pain" },
            { MK_BTN, L"Halls of Vaught",     424, "Warp: Halls of Vaught" },
            { MK_BTN, L"Worldstone Keep L1",  428, "Warp: Worldstone L1" },
            { MK_BTN, L"Worldstone Keep L3",  430, "Warp: Worldstone L3" },
            { MK_BTN, L"Throne of Destruction",431,"Warp: Throne (Baal)" },
            { MK_BTN, L"Worldstone Chamber",  432, "Warp: Worldstone Chamber" },
            { MK_BTN, L"Close",               999, NULL },
        };
        static const struct CheatCell TP_SECRETS[] = {
            { MK_HDR, L"SECRET LOCATIONS",      0, NULL },
            { MK_BTN, L"Tristram",            338, "Warp: Tristram" },
            { MK_BTN, L"Cow Level",           339, "Warp: Cow Level" },
            { MK_BTN, L"Close",               999, NULL },
        };
        /* This is just a stub array; we don't render this one — TAB_TELEPORT
         * has the sub-tab strip rendered separately below. */
        static const struct CheatCell TAB_TELEPORT[] = {
            { MK_BTN, L"(see Act sub-tabs above)", 0, NULL },
            { MK_BTN, L"Close",               999, NULL },
        };

        /* TAB 5 - PORTALS. The cube-recipe Cow Portal is the only
         * physical-portal spawn that's reliable in 1.10f from any
         * source location. Town Portal blue (obj 59) and quest
         * portals (Tristram / Canyon / Duriel / Nihlathak) were
         * removed: blue TP wouldn't spawn even with bPerm=0 +
         * collision detection, and the quest portals crash the
         * game cross-act via Skills.cpp:3651's live assert. Use
         * the Teleport tab to move between zones.
         *   501 = Cow Portal (objId 60, dest 39)
         */
        static const struct CheatCell TAB_PORTALS[] = {
            { MK_HDR, L"PHYSICAL PORTAL",           0, NULL },
            { MK_BTN, L"Cow Portal",              501, "Cow Level (red)" },
            { MK_GAP, NULL,                         0, NULL },

            { MK_BTN, L"Close",                   999, NULL },
        };

        /* 1.9.1 — Loot tab — fixed grid arrays for sub-tabs 2/3/4 (Runes,
         * Gems, Misc). Sets + Uniques are dynamic scrollable lists rendered
         * directly without going through the static MENU table. */

        /* TAB_LOOT_RUNES — every rune r01..r33 individually.
         * Codes verified against Misc.txt (3-letter "rune name" pairs)
         * and the existing g_cheatItemCmd RUNES_ALL list at d2arch_gameloop.c:1312. */
        static const struct CheatCell TAB_LOOT_RUNES[] = {
            { MK_HDR, L"LOW RUNES (El..Thul)",       0, NULL },
            { MK_BTN, L"r01 El",        1001, "Rune: El"   },
            { MK_BTN, L"r02 Eld",       1002, "Rune: Eld"  },
            { MK_BTN, L"r03 Tir",       1003, "Rune: Tir"  },
            { MK_BTN, L"r04 Nef",       1004, "Rune: Nef"  },
            { MK_BTN, L"r05 Eth",       1005, "Rune: Eth"  },
            { MK_BTN, L"r06 Ith",       1006, "Rune: Ith"  },
            { MK_BTN, L"r07 Tal",       1007, "Rune: Tal"  },
            { MK_BTN, L"r08 Ral",       1008, "Rune: Ral"  },
            { MK_BTN, L"r09 Ort",       1009, "Rune: Ort"  },
            { MK_BTN, L"r10 Thul",      1010, "Rune: Thul" },
            { MK_HDR, L"MID RUNES (Amn..Lem)",       0, NULL },
            { MK_BTN, L"r11 Amn",       1011, "Rune: Amn"   },
            { MK_BTN, L"r12 Sol",       1012, "Rune: Sol"   },
            { MK_BTN, L"r13 Shael",     1013, "Rune: Shael" },
            { MK_BTN, L"r14 Dol",       1014, "Rune: Dol"   },
            { MK_BTN, L"r15 Hel",       1015, "Rune: Hel"   },
            { MK_BTN, L"r16 Io",        1016, "Rune: Io"    },
            { MK_BTN, L"r17 Lum",       1017, "Rune: Lum"   },
            { MK_BTN, L"r18 Ko",        1018, "Rune: Ko"    },
            { MK_BTN, L"r19 Fal",       1019, "Rune: Fal"   },
            { MK_BTN, L"r20 Lem",       1020, "Rune: Lem"   },
            { MK_HDR, L"HIGH RUNES (Pul..Ber)",       0, NULL },
            { MK_BTN, L"r21 Pul",       1021, "Rune: Pul" },
            { MK_BTN, L"r22 Um",        1022, "Rune: Um"  },
            { MK_BTN, L"r23 Mal",       1023, "Rune: Mal" },
            { MK_BTN, L"r24 Ist",       1024, "Rune: Ist" },
            { MK_BTN, L"r25 Gul",       1025, "Rune: Gul" },
            { MK_BTN, L"r26 Vex",       1026, "Rune: Vex" },
            { MK_BTN, L"r27 Ohm",       1027, "Rune: Ohm" },
            { MK_BTN, L"r28 Lo",        1028, "Rune: Lo"  },
            { MK_BTN, L"r29 Sur",       1029, "Rune: Sur" },
            { MK_BTN, L"r30 Ber",       1030, "Rune: Ber" },
            { MK_HDR, L"TOP RUNES (Jah/Cham/Zod)",       0, NULL },
            { MK_BTN, L"r31 Jah",       1031, "Rune: Jah"  },
            { MK_BTN, L"r32 Cham",      1032, "Rune: Cham" },
            { MK_BTN, L"r33 Zod",       1033, "Rune: Zod"  },
            { MK_BTN, L"Close",          999, NULL },
        };

        /* TAB_LOOT_GEMS — 7 colors × 5 grades = 35 buttons.
         * Order verified against Misc.txt + GEMS_ALL list at d2arch_gameloop.c:1338-1347.
         * NOTE — irregular codes preserved from data:
         *   Amethyst flawless = gzv (NOT glv — verified by cubemain output)
         *   Skull = skc/skf/sku/skl/skz (no g-prefix). */
        static const struct CheatCell TAB_LOOT_GEMS[] = {
            { MK_HDR, L"AMETHYST",                   0, NULL },
            { MK_BTN, L"Chipped",        1101, "Gem: Chipped Amethyst"   },
            { MK_BTN, L"Flawed",         1102, "Gem: Flawed Amethyst"    },
            { MK_BTN, L"Normal",         1103, "Gem: Amethyst"           },
            { MK_BTN, L"Flawless",       1104, "Gem: Flawless Amethyst (gzv)" },
            { MK_BTN, L"Perfect",        1105, "Gem: Perfect Amethyst"   },
            { MK_HDR, L"TOPAZ",                      0, NULL },
            { MK_BTN, L"Chipped",        1106, "Gem: Chipped Topaz"   },
            { MK_BTN, L"Flawed",         1107, "Gem: Flawed Topaz"    },
            { MK_BTN, L"Normal",         1108, "Gem: Topaz"           },
            { MK_BTN, L"Flawless",       1109, "Gem: Flawless Topaz"  },
            { MK_BTN, L"Perfect",        1110, "Gem: Perfect Topaz"   },
            { MK_HDR, L"SAPPHIRE",                   0, NULL },
            { MK_BTN, L"Chipped",        1111, "Gem: Chipped Sapphire"   },
            { MK_BTN, L"Flawed",         1112, "Gem: Flawed Sapphire"    },
            { MK_BTN, L"Normal",         1113, "Gem: Sapphire"           },
            { MK_BTN, L"Flawless",       1114, "Gem: Flawless Sapphire"  },
            { MK_BTN, L"Perfect",        1115, "Gem: Perfect Sapphire"   },
            { MK_HDR, L"EMERALD",                    0, NULL },
            { MK_BTN, L"Chipped",        1116, "Gem: Chipped Emerald"   },
            { MK_BTN, L"Flawed",         1117, "Gem: Flawed Emerald"    },
            { MK_BTN, L"Normal",         1118, "Gem: Emerald"           },
            { MK_BTN, L"Flawless",       1119, "Gem: Flawless Emerald"  },
            { MK_BTN, L"Perfect",        1120, "Gem: Perfect Emerald"   },
            { MK_HDR, L"RUBY",                       0, NULL },
            { MK_BTN, L"Chipped",        1121, "Gem: Chipped Ruby"   },
            { MK_BTN, L"Flawed",         1122, "Gem: Flawed Ruby"    },
            { MK_BTN, L"Normal",         1123, "Gem: Ruby"           },
            { MK_BTN, L"Flawless",       1124, "Gem: Flawless Ruby"  },
            { MK_BTN, L"Perfect",        1125, "Gem: Perfect Ruby"   },
            { MK_HDR, L"DIAMOND",                    0, NULL },
            { MK_BTN, L"Chipped",        1126, "Gem: Chipped Diamond"   },
            { MK_BTN, L"Flawed",         1127, "Gem: Flawed Diamond"    },
            { MK_BTN, L"Normal",         1128, "Gem: Diamond"           },
            { MK_BTN, L"Flawless",       1129, "Gem: Flawless Diamond"  },
            { MK_BTN, L"Perfect",        1130, "Gem: Perfect Diamond"   },
            { MK_HDR, L"SKULL",                      0, NULL },
            { MK_BTN, L"Chipped",        1131, "Gem: Chipped Skull (skc)"   },
            { MK_BTN, L"Flawed",         1132, "Gem: Flawed Skull (skf)"    },
            { MK_BTN, L"Normal",         1133, "Gem: Skull (sku)"           },
            { MK_BTN, L"Flawless",       1134, "Gem: Flawless Skull (skl)"  },
            { MK_BTN, L"Perfect",        1135, "Gem: Perfect Skull (skz)"   },
            { MK_BTN, L"Close",           999, NULL },
        };

        /* TAB_LOOT_MISC — every reward / quest item the mod awards.
         * Codes traced from DEBUG_MENU_ITEM_CATALOG_2026-05-01.md sections 5a-5k.
         * Hellfire Torch (cm2 unique) is forced ilvl=99, quality=7 by the
         * dispatch handler so it rolls as the unique torch instead of a
         * normal Charm Medium. */
        static const struct CheatCell TAB_LOOT_MISC[] = {
            { MK_HDR, L"PANDEMONIUM EVENT",          0, NULL },
            { MK_BTN, L"Key of Terror",      1201, "pk1 — Key of Terror" },
            { MK_BTN, L"Key of Hate",        1202, "pk2 — Key of Hate" },
            { MK_BTN, L"Key of Destruction", 1203, "pk3 — Key of Destruction" },
            { MK_BTN, L"Mephisto's Brain",   1204, "mbr — Mephisto's Brain" },
            { MK_BTN, L"Diablo's Horn",      1205, "dhn — Diablo's Horn" },
            { MK_BTN, L"Baal's Eye",         1206, "bey — Baal's Eye" },
            { MK_BTN, L"Twisted Essence",    1207, "tes — Twisted Essence (Suff)" },
            { MK_BTN, L"Charged Essence",    1208, "ceh — Charged Essence (Hate)" },
            { MK_BTN, L"Burning Essence",    1209, "bet — Burning Essence (Terror)" },
            { MK_BTN, L"Festering Essence",  1210, "fed — Festering Essence (Dest)" },
            { MK_BTN, L"Hellfire Torch",     1211, "cm2 unique — Hellfire Torch" },
            { MK_BTN, L"Token of Absolution",1212, "toa — Token of Absolution" },
            { MK_HDR, L"CHARMS",                     0, NULL },
            { MK_BTN, L"Charm Small (cm1)",  1220, "cm1 — Charm Small (Anni base)" },
            { MK_BTN, L"Charm Medium (cm2)", 1221, "cm2 — Charm Medium" },
            { MK_BTN, L"Charm Large (cm3)",  1222, "cm3 — Charm Large (Gheed base)" },
            { MK_HDR, L"TOMES + SCROLLS + KEY",      0, NULL },
            { MK_BTN, L"Tome of TP",         1230, "tbk — Tome of Town Portal" },
            { MK_BTN, L"Tome of ID",         1231, "ibk — Tome of Identify" },
            { MK_BTN, L"TP Scroll",          1232, "tsc — Town Portal Scroll" },
            { MK_BTN, L"ID Scroll",          1233, "isc — Identify Scroll" },
            { MK_BTN, L"Skeleton Key",       1234, "key — Skeleton Key" },
            { MK_BTN, L"Horadric Cube",      1235, "box — Horadric Cube" },
            { MK_HDR, L"POTIONS — HEALING",          0, NULL },
            { MK_BTN, L"hp1 Lesser",         1240, "hp1 — Lesser Healing" },
            { MK_BTN, L"hp2 Light",          1241, "hp2 — Light Healing" },
            { MK_BTN, L"hp3 Healing",        1242, "hp3 — Healing" },
            { MK_BTN, L"hp4 Strong",         1243, "hp4 — Strong Healing" },
            { MK_BTN, L"hp5 Greater",        1244, "hp5 — Greater Healing" },
            { MK_HDR, L"POTIONS — MANA",             0, NULL },
            { MK_BTN, L"mp1 Lesser",         1245, "mp1 — Lesser Mana" },
            { MK_BTN, L"mp2 Light",          1246, "mp2 — Light Mana" },
            { MK_BTN, L"mp3 Mana",           1247, "mp3 — Mana" },
            { MK_BTN, L"mp4 Strong",         1248, "mp4 — Strong Mana" },
            { MK_BTN, L"mp5 Greater",        1249, "mp5 — Greater Mana" },
            { MK_HDR, L"POTIONS — REJUV / SPECIALTY",0, NULL },
            { MK_BTN, L"Rejuv (small)",      1250, "rvs — Rejuvenation" },
            { MK_BTN, L"Rejuv (full)",       1251, "rvl — Full Rejuvenation" },
            { MK_BTN, L"Stamina",            1252, "vps — Stamina Potion" },
            { MK_BTN, L"Antidote",           1253, "yps — Antidote Potion" },
            { MK_BTN, L"Thawing",            1254, "wms — Thawing Potion" },
            { MK_BTN, L"Close",               999, NULL },
        };

        /* 1.9.2 — Mons tab. Four sub-tabs:
         *   0 = SUs    (scrollable list of vanilla SuperUniques 0..65;
         *               click spawns via fnSpawnSuperUnique)
         *   1 = Bosses (fixed grid: 5 act bosses + 6 ubers + 2 trios)
         *   2 = Normal (scrollable list of MonStats.txt rows; click
         *               spawns via fnSpawnMonster)
         *   3 = Random (the existing TRAPS section moved from Combat tab —
         *               random SU spawn + random monster pack spawn)
         *
         * SU + Normal lists below are PLACEHOLDER catalogs (a small subset
         * to validate the system end-to-end). The full catalogs (~66 SUs +
         * ~250 normal monsters) get filled in once the catalog research
         * pass completes — see Research/CHECK_SOURCES_MONSTER_AUDIT_*.md */

        /* Mons sub-tab 1 — Bosses. Reuses the existing g_cheatSpawnUber
         * pipeline for the 6 ubers + 2 trios; the 5 act-boss rows
         * (Andariel/Duriel/Mephisto/Diablo/Baal) use g_cheatSpawnMonsterRowId
         * with their MonStats.txt monId (verified against d2arch_gameloop.c
         * UBER_VANILLA_IDS for Mephisto/Diablo/Baal: 242/243/544; Andariel
         * is 156, Duriel is 211 in vanilla 1.10f though those rows are
         * shared with the uber spawn table in our mod). */
        static const struct CheatCell TAB_MONS_BOSSES[] = {
            { MK_HDR, L"VANILLA ACT BOSSES",         0, NULL },
            { MK_BTN, L"Andariel",         700, "Spawning Andariel..." },
            { MK_BTN, L"Duriel",           701, "Spawning Duriel..." },
            { MK_BTN, L"Mephisto",         702, "Spawning Mephisto..." },
            { MK_BTN, L"Diablo",           703, "Spawning Diablo..." },
            { MK_BTN, L"Baal",             704, "Spawning Baal..." },
            { MK_HDR, L"PANDEMONIUM UBERS",          0, NULL },
            { MK_BTN, L"Lilith",           230, "Spawning Lilith..." },
            { MK_BTN, L"Uber Duriel",      231, "Spawning Uber Duriel..." },
            { MK_BTN, L"Uber Izual",       232, "Spawning Uber Izual..." },
            { MK_BTN, L"Uber Mephisto",    233, "Spawning Uber Mephisto..." },
            { MK_BTN, L"Uber Diablo",      234, "Spawning Uber Diablo..." },
            { MK_BTN, L"Uber Baal",        235, "Spawning Uber Baal..." },
            { MK_BTN, L"Mini Trio",        240, "Lilith + Duriel + Izual..." },
            { MK_BTN, L"Final Trio",       241, "Mephisto + Diablo + Baal..." },
            { MK_BTN, L"Close",            999, NULL },
        };

        /* Mons sub-tab 3 — Random. Mirrors the legacy Combat tab TRAPS
         * section so users have one place for spawn-random-monsters too.
         * (Combat tab keeps these cells too for backward compat.) */
        static const struct CheatCell TAB_MONS_RANDOM[] = {
            { MK_HDR, L"RANDOM SPAWN",               0, NULL },
            { MK_BTN, L"Random SuperUnique", 110, "Spawning random SU..." },
            { MK_BTN, L"Random Monsters",    111, "Spawning monster pack..." },
            { MK_BTN, L"Close",              999, NULL },
        };

        /* 1.9.2 — Mons full catalog. Generated from
         * Research/CHECK_SOURCES_MONSTER_AUDIT_2026-05-02.md by
         * Tools/extract_monster_catalog.py. Re-run that script after any
         * change to the research markdown.
         *
         * MONS_SU_FULL[]      — 66 vanilla SUs (header row 42 stripped).
         *                       idx is SuperUniques.txt row, passed to
         *                       fnSpawnSuperUnique.
         * MONS_NORMAL_FULL[]  — 400 player-killable MonStats rows.
         *                       idx is MonStats.txt hcIdx, passed to
         *                       fnSpawnMonster.
         *
         * Dispatch handled in d2arch_gameloop.c via the slots
         * g_cheatSpawnSuperUniqueIdx + g_cheatSpawnMonsterRowId. */
        struct MonsCatalogEntry { int idx; const char* name; const char* note; };
        #include "_monster_catalog_generated.h"
        #define MONS_SU_CATALOG         MONS_SU_FULL
        #define MONS_SU_CATALOG_LEN     MONS_SU_FULL_LEN
        #define MONS_NORMAL_CATALOG     MONS_NORMAL_FULL
        #define MONS_NORMAL_CATALOG_LEN MONS_NORMAL_FULL_LEN

        /* Pick active tab's array */
        const struct CheatCell* MENU = TAB_CHARACTER;
        int tabSize = (int)(sizeof(TAB_CHARACTER) / sizeof(TAB_CHARACTER[0]));
        const wchar_t* tabName = L"Character";
        switch (s_activeTab) {
            case 0: MENU = TAB_CHARACTER;
                    tabSize = (int)(sizeof(TAB_CHARACTER)/sizeof(TAB_CHARACTER[0]));
                    tabName = L"Character"; break;
            case 1: MENU = TAB_COMBAT;
                    tabSize = (int)(sizeof(TAB_COMBAT)/sizeof(TAB_COMBAT[0]));
                    tabName = L"Combat"; break;
            case 2: MENU = TAB_ITEMS;
                    tabSize = (int)(sizeof(TAB_ITEMS)/sizeof(TAB_ITEMS[0]));
                    tabName = L"Items"; break;
            case 3: MENU = TAB_PANDEMONIUM;
                    tabSize = (int)(sizeof(TAB_PANDEMONIUM)/sizeof(TAB_PANDEMONIUM[0]));
                    tabName = L"Pandemonium"; break;
            case 5: MENU = TAB_PORTALS;
                    tabSize = (int)(sizeof(TAB_PORTALS)/sizeof(TAB_PORTALS[0]));
                    tabName = L"Portals"; break;
            case 6:
                /* 1.9.1 — Loot tab. Sets + Uniques (sub 0/1) are
                 * scrollable lists rendered separately; sub 2/3/4 use
                 * the static grid arrays declared above. The MENU /
                 * tabSize fallthrough below renders the grid for those
                 * sub-tabs the same way every other tab does. */
                switch (s_lootSubTab) {
                    case 2: MENU = TAB_LOOT_RUNES;
                            tabSize = (int)(sizeof(TAB_LOOT_RUNES)/sizeof(TAB_LOOT_RUNES[0]));
                            break;
                    case 3: MENU = TAB_LOOT_GEMS;
                            tabSize = (int)(sizeof(TAB_LOOT_GEMS)/sizeof(TAB_LOOT_GEMS[0]));
                            break;
                    case 4: MENU = TAB_LOOT_MISC;
                            tabSize = (int)(sizeof(TAB_LOOT_MISC)/sizeof(TAB_LOOT_MISC[0]));
                            break;
                    default:
                        /* Sets / Uniques — empty stub; the scrollable
                         * list draws its own content below. */
                        MENU = NULL;
                        tabSize = 0;
                        break;
                }
                tabName = L"Loot"; break;
            case 7:
                /* 1.9.2 — Mons tab. Sub 0=SUs and 2=Normal are scrollable
                 * lists; sub 1=Bosses and 3=Random use the static grid
                 * arrays defined above. */
                switch (s_monsSubTab) {
                    case 1: MENU = TAB_MONS_BOSSES;
                            tabSize = (int)(sizeof(TAB_MONS_BOSSES)/sizeof(TAB_MONS_BOSSES[0]));
                            break;
                    case 3: MENU = TAB_MONS_RANDOM;
                            tabSize = (int)(sizeof(TAB_MONS_RANDOM)/sizeof(TAB_MONS_RANDOM[0]));
                            break;
                    default:
                        /* SUs (0) / Normal (2) — empty stub; scrollable
                         * lists draw their own content below. */
                        MENU = NULL;
                        tabSize = 0;
                        break;
                }
                tabName = L"Mons"; break;
            case 4:
                /* Pick teleport sub-tab */
                switch (s_tpSubTab) {
                    case 0: MENU = TP_ACT1;
                            tabSize = (int)(sizeof(TP_ACT1)/sizeof(TP_ACT1[0]));
                            break;
                    case 1: MENU = TP_ACT2;
                            tabSize = (int)(sizeof(TP_ACT2)/sizeof(TP_ACT2[0]));
                            break;
                    case 2: MENU = TP_ACT3;
                            tabSize = (int)(sizeof(TP_ACT3)/sizeof(TP_ACT3[0]));
                            break;
                    case 3: MENU = TP_ACT4;
                            tabSize = (int)(sizeof(TP_ACT4)/sizeof(TP_ACT4[0]));
                            break;
                    case 4: MENU = TP_ACT5;
                            tabSize = (int)(sizeof(TP_ACT5)/sizeof(TP_ACT5[0]));
                            break;
                    case 5: MENU = TP_SECRETS;
                            tabSize = (int)(sizeof(TP_SECRETS)/sizeof(TP_SECRETS[0]));
                            break;
                    default: MENU = TP_ACT1;
                             tabSize = (int)(sizeof(TP_ACT1)/sizeof(TP_ACT1[0]));
                             break;
                }
                tabName = L"Teleport";
                break;
        }
        (void)tabName;
        const int numCells = tabSize;  /* set above based on active tab */

        /* Layout */
        const int COLS    = 3;
        const int CELL_W  = 165;
        const int CELL_H  = 26;
        const int CELL_GAP = 5;
        const int PAD     = 12;
        const int TITLE_H = 32;
        const int HDR_H   = 22;
        const int FOOT_H  = 12;

        /* 1.9.1 — Loot tab Sets/Uniques use a fixed-height scrollable
         * list area instead of the cell-walk grid. We pick a comfortable
         * 380px tall content area so ~17 list rows are visible at once.
         * 1.9.2 — Mons tab SUs/Normal sub-tabs use the same layout. */
        const int LOOT_LIST_H = 380;
        const int LOOT_ROW_H  = 22;
        BOOL isLootScrollList =
            (s_activeTab == 6 && (s_lootSubTab == 0 || s_lootSubTab == 1));
        BOOL isMonsScrollList =
            (s_activeTab == 7 && (s_monsSubTab == 0 || s_monsSubTab == 2));
        BOOL isAnyScrollList = isLootScrollList || isMonsScrollList;

        /* Walk cells once to compute total height */
        /* Reserve space for tab strip (22 tab + 4 top + 4 bottom = 30).
         * Plus extra 26 for sub-tab strip when Teleport / Loot / Mons active. */
        int yWalk = TITLE_H + 30 + PAD;
        if (s_activeTab == 4 || s_activeTab == 6 || s_activeTab == 7) yWalk += 26;
        int colCur = 0;
        if (isAnyScrollList) {
            /* Fixed scrollable list height + close-button row. */
            yWalk += LOOT_LIST_H + CELL_GAP + CELL_H + CELL_GAP;
        } else {
            for (int i = 0; i < numCells; i++) {
                int k = MENU[i].kind;
                if (k == MK_HDR) {
                    if (colCur != 0) { yWalk += CELL_H + CELL_GAP; colCur = 0; }
                    yWalk += HDR_H + CELL_GAP;
                } else {
                    if (colCur == 0 && i > 0) yWalk += 0;
                    colCur++;
                    if (colCur >= COLS) { yWalk += CELL_H + CELL_GAP; colCur = 0; }
                }
            }
            if (colCur != 0) yWalk += CELL_H + CELL_GAP;
        }
        yWalk += FOOT_H;

        int cw = PAD * 2 + CELL_W * COLS + CELL_GAP * (COLS - 1);
        int ch = yWalk;
        int cmx = MouseX(), cmy = MouseY();

        if (s_cheatX < 0) { s_cheatX = (g_screenW - cw) / 2; s_cheatY = (g_screenH - ch) / 2; }
        int cx = s_cheatX, cy = s_cheatY;

        /* Drag via title bar */
        BOOL lmbDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (lmbDown && !s_cheatDrag && InRect(cmx, cmy, cx, cy, cw, TITLE_H)) {
            s_cheatDrag = TRUE; s_cheatDragOX = cmx - cx; s_cheatDragOY = cmy - cy;
        }
        if (s_cheatDrag) {
            if (lmbDown) {
                s_cheatX = cmx - s_cheatDragOX; s_cheatY = cmy - s_cheatDragOY;
                if (s_cheatX < 0) s_cheatX = 0;
                if (s_cheatY < 0) s_cheatY = 0;
                if (s_cheatX + cw > g_screenW) s_cheatX = g_screenW - cw;
                if (s_cheatY + ch > g_screenH) s_cheatY = g_screenH - ch;
                cx = s_cheatX; cy = s_cheatY;
            } else { s_cheatDrag = FALSE; }
        }

        /* === DRAW === */
        /* Solid black background (transMode 1 = opaque) */
        fnRect(cx, cy, cx+cw, cy+ch, 0, 1);
        /* Gold outer border */
        fnRect(cx,        cy,        cx+cw,    cy+1,     4, 1);  /* top */
        fnRect(cx,        cy+ch-1,   cx+cw,    cy+ch,    4, 1);  /* bottom */
        fnRect(cx,        cy,        cx+1,     cy+ch,    4, 1);  /* left */
        fnRect(cx+cw-1,   cy,        cx+cw,    cy+ch,    4, 1);  /* right */

        /* Title bar */
        fnRect(cx+1, cy+1, cx+cw-1, cy+TITLE_H-1, 5, 1);
        fnRect(cx,   cy+TITLE_H-1, cx+cw, cy+TITLE_H, 4, 1);
        fnFont(0);
        fnText(L"Dev Tools  (Ctrl+V)", cx+cw/2, cy+22, 4, 1);

        /* Tab strip — 8 tabs across top (1.9.2 added Mons for the
         * per-monster spawn pages: SUs / Bosses / Normal / Random) */
        {
            const wchar_t* tabLabels[8] = {
                L"Char", L"Combat", L"Items", L"Pand", L"Teleport",
                L"Portals", L"Loot", L"Mons"
            };
            const int TAB_H = 22;
            const int tabW = (cw - PAD * 2) / 8;
            int tabY = cy + TITLE_H + 4;
            for (int t = 0; t < 8; t++) {
                int tx = cx + PAD + t * tabW;
                BOOL isActive = (t == s_activeTab);
                BOOL hov = InRect(cmx, cmy, tx, tabY, tabW - 2, TAB_H);
                int fillCol = isActive ? 4 : (hov ? 5 : 0);
                int textCol = isActive ? 0 : (hov ? 0 : 5);
                fnRect(tx, tabY, tx + tabW - 2, tabY + TAB_H, fillCol, 1);
                fnRect(tx, tabY, tx + tabW - 2, tabY + 1, 7, 1);
                fnRect(tx, tabY + TAB_H - 1, tx + tabW - 2, tabY + TAB_H, 7, 1);
                fnRect(tx, tabY, tx + 1, tabY + TAB_H, 7, 1);
                fnRect(tx + tabW - 3, tabY, tx + tabW - 2, tabY + TAB_H, 7, 1);
                fnFont(6);
                fnText(tabLabels[t], tx + tabW / 2 - 1, tabY + 16, textCol, 1);
                if (hov && !s_cheatDrag && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    static DWORD _tc = 0; DWORD _tn = GetTickCount();
                    if (_tn - _tc > 250) {
                        _tc = _tn;
                        s_activeTab = t;
                        Log("CHEAT MENU: switched to tab %d (%ls)\n", t, tabLabels[t]);
                    }
                }
            }
        }
        const int TAB_STRIP_H = 22 + 8;  /* 22 tab + 4 top pad + 4 bottom pad */

        /* Sub-tab strip — only when Teleport (4), Loot (6), or Mons (7)
         * tab is active. All three reuse the same paint routine; the
         * labels and selected-state variable are picked from the active
         * top tab. */
        int subTabExtraY = 0;
        if (s_activeTab == 4 || s_activeTab == 6 || s_activeTab == 7) {
            const wchar_t* subLabels[6];
            int subCount;
            int* selectedSub;
            if (s_activeTab == 4) {
                static const wchar_t* TP_LABELS[6] = {
                    L"Act 1", L"Act 2", L"Act 3", L"Act 4", L"Act 5", L"Secret"
                };
                for (int i = 0; i < 6; i++) subLabels[i] = TP_LABELS[i];
                subCount = 6;
                selectedSub = &s_tpSubTab;
            } else if (s_activeTab == 6) {
                /* Loot tab — 5 sub-tabs */
                static const wchar_t* LOOT_LABELS[5] = {
                    L"Sets", L"Uniques", L"Runes", L"Gems", L"Misc"
                };
                for (int i = 0; i < 5; i++) subLabels[i] = LOOT_LABELS[i];
                subCount = 5;
                selectedSub = &s_lootSubTab;
            } else {
                /* 1.9.2 Mons tab — 4 sub-tabs */
                static const wchar_t* MONS_LABELS[4] = {
                    L"SUs", L"Bosses", L"Normal", L"Random"
                };
                for (int i = 0; i < 4; i++) subLabels[i] = MONS_LABELS[i];
                subCount = 4;
                selectedSub = &s_monsSubTab;
            }
            const int SUB_TAB_H = 20;
            const int subTabW = (cw - PAD * 2) / subCount;
            int subY = cy + TITLE_H + TAB_STRIP_H;
            for (int t = 0; t < subCount; t++) {
                int tx = cx + PAD + t * subTabW;
                BOOL isActive = (t == *selectedSub);
                BOOL hov = InRect(cmx, cmy, tx, subY, subTabW - 2, SUB_TAB_H);
                int fillCol = isActive ? 4 : (hov ? 5 : 0);
                int textCol = isActive ? 0 : (hov ? 0 : 5);
                fnRect(tx, subY, tx + subTabW - 2, subY + SUB_TAB_H, fillCol, 1);
                fnRect(tx, subY, tx + subTabW - 2, subY + 1, 7, 1);
                fnRect(tx, subY + SUB_TAB_H - 1, tx + subTabW - 2, subY + SUB_TAB_H, 7, 1);
                fnRect(tx, subY, tx + 1, subY + SUB_TAB_H, 7, 1);
                fnRect(tx + subTabW - 3, subY, tx + subTabW - 2, subY + SUB_TAB_H, 7, 1);
                fnFont(6);
                fnText(subLabels[t], tx + subTabW / 2 - 1, subY + 14, textCol, 1);
                if (hov && !s_cheatDrag && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    static DWORD _stc = 0; DWORD _stn = GetTickCount();
                    if (_stn - _stc > 250) {
                        _stc = _stn;
                        *selectedSub = t;
                        Log("CHEAT MENU: switched to %s sub-tab %d (%ls)\n",
                            (s_activeTab == 4) ? "teleport" : "loot",
                            t, subLabels[t]);
                    }
                }
            }
            subTabExtraY = SUB_TAB_H + 6;
        }

        /* Walk cells and draw */
        int yCur = cy + TITLE_H + TAB_STRIP_H + subTabExtraY;
        colCur = 0;

        /* 1.9.1 — Loot Sets / Uniques: render a vertical scrollable list
         * instead of the cell grid. Each row is one button (one item).
         * Mouse wheel in the list area scrolls; click triggers the spawn.
         *
         * Sets index is the catalog idx 0..126 (Coll_GetSetPieceCode +
         * Quests_SetPieceName); Uniques is 0..g_uniqueCatalogCount-1.
         * Both route through Quests_QueueSpecificDrop in the gameloop —
         * exact same path AP rewards take, so this doubles as an
         * AP-delivery test bench. */
        if (isLootScrollList) {
            int isSets = (s_lootSubTab == 0);
            int* scroll = isSets ? &s_lootScrollSet : &s_lootScrollUnique;
            int totalCount;
            if (isSets) {
                totalCount = COLL_NUM_SET_PIECES;  /* 127 */
            } else {
                if (!g_uniqueCatalogLoaded) Quests_LoadUniqueCatalog();
                totalCount = g_uniqueCatalogCount;
            }

            int listX = cx + PAD;
            int listW = cw - PAD * 2 - 14;   /* leave 14 px on right for scrollbar */
            int listY = yCur;
            int listH = LOOT_LIST_H;
            int rowH  = LOOT_ROW_H;

            int totalH = totalCount * rowH;
            int maxScroll = totalH - listH;
            if (maxScroll < 0) maxScroll = 0;

            /* Mouse wheel scroll — WndProc accumulates raw delta into
             * g_cheatMenuWheelDelta when the menu is open; we drain it
             * each frame and translate to row scroll. WHEEL_DELTA=120
             * per notch, scroll 3 rows per notch. Only consumed when
             * the cursor is over the list area. */
            if (InRect(cmx, cmy, listX, listY, listW + 14, listH)) {
                if (g_cheatMenuWheelDelta != 0) {
                    *scroll -= (g_cheatMenuWheelDelta / 120) * (rowH * 3);
                    g_cheatMenuWheelDelta = 0;
                }
            }
            if (*scroll < 0) *scroll = 0;
            if (*scroll > maxScroll) *scroll = maxScroll;

            /* Background */
            fnRect(listX, listY, listX + listW + 14, listY + listH, 0, 1);
            fnRect(listX, listY, listX + listW + 14, listY + 1, 7, 1);
            fnRect(listX, listY + listH - 1, listX + listW + 14, listY + listH, 7, 1);
            fnRect(listX, listY, listX + 1, listY + listH, 7, 1);
            fnRect(listX + listW + 13, listY, listX + listW + 14, listY + listH, 7, 1);

            /* Row rendering — only draw rows in the visible window */
            int firstRow = (*scroll) / rowH;
            int lastRow  = (*scroll + listH) / rowH + 1;
            if (firstRow < 0) firstRow = 0;
            if (lastRow > totalCount) lastRow = totalCount;

            for (int r = firstRow; r < lastRow; r++) {
                int rowY = listY + (r * rowH) - (*scroll);
                if (rowY + rowH < listY || rowY > listY + listH) continue;

                /* Clip rows that overlap the list edges. */
                int drawTop    = (rowY < listY)         ? listY         : rowY;
                int drawBottom = (rowY + rowH > listY + listH) ? listY + listH : rowY + rowH;
                if (drawBottom <= drawTop) continue;

                BOOL hov = InRect(cmx, cmy, listX + 2, rowY, listW - 4, rowH)
                         && (rowY >= listY) && (rowY + rowH <= listY + listH);
                int fillCol = hov ? 4 : 0;
                int textCol = hov ? 0 : 7;

                fnRect(listX + 2, drawTop, listX + listW - 2, drawBottom, fillCol, 1);

                /* Build label: "[idx] name (extra)" */
                wchar_t wlabel[96];
                char buf[128];
                if (isSets) {
                    const char* code = Coll_GetSetPieceCode(r);
                    const char* name = Quests_SetPieceName(r);
                    _snprintf(buf, sizeof(buf), "[%3d] %s  (%s)",
                              r, name ? name : "?", code ? code : "??");
                } else {
                    const char* name = Quests_UniqueName(r);
                    const char* code = (r >= 0 && r < g_uniqueCatalogCount)
                                       ? g_uniqueCatalog[r].baseCode : "??";
                    int reqLvl = (r >= 0 && r < g_uniqueCatalogCount)
                                 ? g_uniqueCatalog[r].reqLvl : 0;
                    _snprintf(buf, sizeof(buf), "[%3d] %s  (%s, req %d)",
                              r, name ? name : "?", code, reqLvl);
                }
                buf[sizeof(buf) - 1] = 0;
                /* Truncate to fit ~52 chars */
                if (strlen(buf) > 52) { buf[49] = '.'; buf[50] = '.'; buf[51] = '.'; buf[52] = 0; }
                MultiByteToWideChar(CP_ACP, 0, buf, -1, wlabel, 96);

                fnFont(6);
                fnText(wlabel, listX + 6, rowY + 16, textCol, 0);

                if (hov && !s_cheatDrag && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    static DWORD _lc = 0; DWORD _ln = GetTickCount();
                    if (_ln - _lc > 300) {
                        _lc = _ln;
                        if (isSets) {
                            g_cheatSpecificSetIdx = r;
                            ShowNotify(Quests_SetPieceName(r));
                            Log("CHEAT LOOT: queued SET idx=%d\n", r);
                        } else {
                            g_cheatSpecificUniqueIdx = r;
                            ShowNotify(Quests_UniqueName(r));
                            Log("CHEAT LOOT: queued UNIQUE idx=%d\n", r);
                        }
                    }
                }
            }

            /* Scrollbar — track + thumb on the right edge */
            int sbX  = listX + listW;
            int sbX2 = sbX + 12;
            if (totalH > listH) {
                fnRect(sbX, listY, sbX2, listY + listH, 0, 1);
                int thumbH = (listH * listH) / totalH;
                if (thumbH < 16) thumbH = 16;
                int thumbY = listY;
                if (maxScroll > 0)
                    thumbY = listY + ((*scroll) * (listH - thumbH)) / maxScroll;
                fnRect(sbX + 1, thumbY, sbX2 - 1, thumbY + thumbH, 7, 1);

                /* Click-to-scroll: clicking the track snaps thumb to mouse Y */
                if (InRect(cmx, cmy, sbX, listY, 14, listH)
                    && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    int relY = cmy - listY - thumbH / 2;
                    if (listH - thumbH > 0) {
                        *scroll = (relY * maxScroll) / (listH - thumbH);
                        if (*scroll < 0) *scroll = 0;
                        if (*scroll > maxScroll) *scroll = maxScroll;
                    }
                }
            }

            /* Footer counter */
            wchar_t winfo[64];
            swprintf(winfo, 64, L"%d / %d  —  scroll wheel or drag thumb",
                     totalCount, totalCount);
            fnFont(6);
            fnText(winfo, listX + listW / 2, listY + listH + 12, 5, 1);

            yCur += listH + CELL_GAP + 12;

            /* Close button row */
            int bx = cx + (cw - CELL_W) / 2;
            int by = yCur;
            int bw = CELL_W, bh = CELL_H;
            BOOL hovC = InRect(cmx, cmy, bx, by, bw, bh);
            int fillCol = hovC ? 4 : 5;
            int textCol = hovC ? 0 : 1;
            fnRect(bx, by, bx + bw, by + bh, fillCol, 1);
            fnRect(bx, by, bx + bw, by + 1, 7, 1);
            fnRect(bx, by + bh - 1, bx + bw, by + bh, 7, 1);
            fnRect(bx, by, bx + 1, by + bh, 7, 1);
            fnRect(bx + bw - 1, by, bx + bw, by + bh, 7, 1);
            fnFont(6);
            fnText(L"Close", bx + bw / 2, by + 18, textCol, 1);
            if (hovC && !s_cheatDrag && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                static DWORD _cc = 0; DWORD _cn = GetTickCount();
                if (_cn - _cc > 300) { _cc = _cn; g_cheatMenuOpen = FALSE; }
            }

            goto cheat_menu_done;
        }

        /* 1.9.2 — Mons SUs / Normal: render a vertical scrollable list.
         * Same pattern as the Loot Sets/Uniques block above. Each row
         * is one monster catalog entry; click sets the appropriate
         * dispatch global which the gameloop tick consumes. */
        if (isMonsScrollList) {
            int isSU = (s_monsSubTab == 0);
            int* scroll = isSU ? &s_monsScrollSU : &s_monsScrollNormal;
            int totalCount = isSU ? MONS_SU_CATALOG_LEN : MONS_NORMAL_CATALOG_LEN;
            const struct MonsCatalogEntry* catalog =
                isSU ? MONS_SU_CATALOG : MONS_NORMAL_CATALOG;

            int listX = cx + PAD;
            int listW = cw - PAD * 2 - 14;
            int listY = yCur;
            int listH = LOOT_LIST_H;
            int rowH  = LOOT_ROW_H;

            int totalH = totalCount * rowH;
            int maxScroll = totalH - listH;
            if (maxScroll < 0) maxScroll = 0;

            if (InRect(cmx, cmy, listX, listY, listW + 14, listH)) {
                if (g_cheatMenuWheelDelta != 0) {
                    *scroll -= (g_cheatMenuWheelDelta / 120) * (rowH * 3);
                    g_cheatMenuWheelDelta = 0;
                }
            }
            if (*scroll < 0) *scroll = 0;
            if (*scroll > maxScroll) *scroll = maxScroll;

            /* Background */
            fnRect(listX, listY, listX + listW + 14, listY + listH, 0, 1);
            fnRect(listX, listY, listX + listW + 14, listY + 1, 7, 1);
            fnRect(listX, listY + listH - 1, listX + listW + 14, listY + listH, 7, 1);
            fnRect(listX, listY, listX + 1, listY + listH, 7, 1);
            fnRect(listX + listW + 13, listY, listX + listW + 14, listY + listH, 7, 1);

            int firstRow = (*scroll) / rowH;
            int lastRow  = (*scroll + listH) / rowH + 1;
            if (firstRow < 0) firstRow = 0;
            if (lastRow > totalCount) lastRow = totalCount;

            for (int r = firstRow; r < lastRow; r++) {
                int rowY = listY + (r * rowH) - (*scroll);
                if (rowY + rowH < listY || rowY > listY + listH) continue;

                int drawTop    = (rowY < listY) ? listY : rowY;
                int drawBottom = (rowY + rowH > listY + listH)
                                 ? listY + listH : rowY + rowH;
                if (drawBottom <= drawTop) continue;

                BOOL hov = InRect(cmx, cmy, listX + 2, rowY, listW - 4, rowH)
                         && (rowY >= listY) && (rowY + rowH <= listY + listH);
                int fillCol = hov ? 4 : 0;
                int textCol = hov ? 0 : 7;
                fnRect(listX + 2, drawTop, listX + listW - 2, drawBottom, fillCol, 1);

                wchar_t wlabel[96];
                char buf[128];
                _snprintf(buf, sizeof(buf), "[%4d] %s  -  %s",
                          catalog[r].idx,
                          catalog[r].name ? catalog[r].name : "?",
                          catalog[r].note ? catalog[r].note : "");
                buf[sizeof(buf) - 1] = 0;
                if (strlen(buf) > 60) {
                    buf[57] = '.'; buf[58] = '.'; buf[59] = '.'; buf[60] = 0;
                }
                MultiByteToWideChar(CP_ACP, 0, buf, -1, wlabel, 96);

                fnFont(6);
                fnText(wlabel, listX + 6, rowY + 16, textCol, 0);

                if (hov && !s_cheatDrag && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    static DWORD _mlc = 0; DWORD _mln = GetTickCount();
                    if (_mln - _mlc > 300) {
                        _mlc = _mln;
                        if (isSU) {
                            g_cheatSpawnSuperUniqueIdx = catalog[r].idx;
                            ShowNotify(catalog[r].name);
                            Log("CHEAT MONS: queued SU idx=%d (%s)\n",
                                catalog[r].idx,
                                catalog[r].name ? catalog[r].name : "?");
                        } else {
                            g_cheatSpawnMonsterRowId = catalog[r].idx;
                            ShowNotify(catalog[r].name);
                            Log("CHEAT MONS: queued Monster row=%d (%s)\n",
                                catalog[r].idx,
                                catalog[r].name ? catalog[r].name : "?");
                        }
                    }
                }
            }

            /* Scrollbar */
            int sbX  = listX + listW;
            int sbX2 = sbX + 12;
            if (totalH > listH) {
                fnRect(sbX, listY, sbX2, listY + listH, 0, 1);
                int thumbH = (listH * listH) / totalH;
                if (thumbH < 16) thumbH = 16;
                int thumbY = listY;
                if (maxScroll > 0)
                    thumbY = listY + ((*scroll) * (listH - thumbH)) / maxScroll;
                fnRect(sbX + 1, thumbY, sbX2 - 1, thumbY + thumbH, 7, 1);

                if (InRect(cmx, cmy, sbX, listY, 14, listH)
                    && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    int relY = cmy - listY - thumbH / 2;
                    if (listH - thumbH > 0) {
                        *scroll = (relY * maxScroll) / (listH - thumbH);
                        if (*scroll < 0) *scroll = 0;
                        if (*scroll > maxScroll) *scroll = maxScroll;
                    }
                }
            }

            wchar_t winfo[64];
            swprintf(winfo, 64, L"%d entries  -  scroll wheel or drag thumb",
                     totalCount);
            fnFont(6);
            fnText(winfo, listX + listW / 2, listY + listH + 12, 5, 1);

            yCur += listH + CELL_GAP + 12;

            /* Close button */
            int bx = cx + (cw - CELL_W) / 2;
            int by = yCur;
            int bw = CELL_W, bh = CELL_H;
            BOOL hovC = InRect(cmx, cmy, bx, by, bw, bh);
            int fillCol = hovC ? 4 : 5;
            int textCol = hovC ? 0 : 1;
            fnRect(bx, by, bx + bw, by + bh, fillCol, 1);
            fnRect(bx, by, bx + bw, by + 1, 7, 1);
            fnRect(bx, by + bh - 1, bx + bw, by + bh, 7, 1);
            fnRect(bx, by, bx + 1, by + bh, 7, 1);
            fnRect(bx + bw - 1, by, bx + bw, by + bh, 7, 1);
            fnFont(6);
            fnText(L"Close", bx + bw / 2, by + 18, textCol, 1);
            if (hovC && !s_cheatDrag && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                static DWORD _cc2 = 0; DWORD _cn2 = GetTickCount();
                if (_cn2 - _cc2 > 300) { _cc2 = _cn2; g_cheatMenuOpen = FALSE; }
            }

            goto cheat_menu_done;
        }

        for (int i = 0; i < numCells; i++) {
            const struct CheatCell* mc = &MENU[i];

            if (mc->kind == MK_HDR) {
                if (colCur != 0) { yCur += CELL_H + CELL_GAP; colCur = 0; }
                int hx = cx + PAD;
                int hw = cw - PAD * 2;
                /* Header strip — solid dark blue/gray fill */
                fnRect(hx, yCur, hx+hw, yCur+HDR_H, 5, 1);
                /* Thin gold underline */
                fnRect(hx, yCur+HDR_H-1, hx+hw, yCur+HDR_H, 4, 1);
                fnFont(0);
                if (mc->label && mc->label[0]) {
                    fnText(mc->label, hx+hw/2, yCur+16, 4, 1);
                }
                yCur += HDR_H + CELL_GAP;
                continue;
            }

            int bx = cx + PAD + colCur * (CELL_W + CELL_GAP);
            int by = yCur;
            int bw = CELL_W;
            int bh = CELL_H;

            if (mc->kind == MK_BTN) {
                BOOL hov = InRect(cmx, cmy, bx, by, bw, bh);
                BOOL isClose = (mc->cmd == 999);
                /* Button fill — solid dark gray, hover = solid gold */
                int fillCol = hov ? 4 : 5;
                int textCol = hov ? 0 : (isClose ? 1 : 0);
                fnRect(bx, by, bx+bw, by+bh, fillCol, 1);
                /* 1-pixel light border */
                fnRect(bx, by,        bx+bw, by+1,    7, 1);
                fnRect(bx, by+bh-1,   bx+bw, by+bh,   7, 1);
                fnRect(bx, by,        bx+1,  by+bh,   7, 1);
                fnRect(bx+bw-1, by,   bx+bw, by+bh,   7, 1);
                fnFont(6);
                fnText(mc->label, bx+bw/2, by+18, textCol, 1);

                if (hov && !s_cheatDrag && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    static DWORD _cc = 0; DWORD _cn = GetTickCount();
                    if (_cn - _cc > 300) {
                        _cc = _cn;
                        int c = mc->cmd;
                        if (c == 100) { g_cheatGold = 1000; }
                        else if (c == 101) { g_cheatStatPts = 5; }
                        else if (c == 102) { g_cheatSkillPts = 5; }
                        else if (c == 103) { g_cheatLevel = 10; }
                        else if (c == 104) { g_cheatHealFull = 1; }
                        else if (c == 105) {
                            for (int ui = 0; ui < g_poolCount; ui++) g_pool[ui].unlocked = TRUE;
                            SaveStateFile();
                            Log("CHEAT: All %d skills unlocked\n", g_poolCount);
                        }
                        else if (c == 110) { g_cheatSpawnTrapSU = 1; }
                        else if (c == 111) { g_cheatSpawnTrapMon = 1; }
                        else if (c >= 120 && c <= 124) {
                            g_cheatDropLoot = 1;
                            g_cheatDropBossId = c - 120;
                        }
                        else if (c >= 201 && c <= 213) {
                            g_cheatItemCmd = c - 200;  /* 1..13 */
                            Log("CHEAT: ItemCmd=%d requested\n", c - 200);
                        }
                        else if (c >= 220 && c <= 224) {
                            /* Pandemonium item drops: 220=keys, 221=organs,
                             * 222=essences, 223=Hellfire Torch, 224=Token */
                            g_cheatItemCmd = c - 200 + 20;  /* 40..44 */
                            Log("CHEAT: PandemoniumItemCmd=%d requested\n", c - 200 + 20);
                        }
                        else if (c >= 230 && c <= 235) {
                            /* Spawn individual uber boss (230=Lilith, 231=Duriel,
                             * 232=Izual, 233=Mephisto, 234=Diablo, 235=Baal) */
                            g_cheatSpawnUber = c - 229;  /* 1..6 */
                            Log("CHEAT: SpawnUber=%d requested\n", c - 229);
                        }
                        else if (c == 240) {
                            g_cheatSpawnUber = 7;  /* Mini Uber trio */
                            Log("CHEAT: Mini uber trio spawn\n");
                        }
                        else if (c == 241) {
                            g_cheatSpawnUber = 8;  /* Final uber trio */
                            Log("CHEAT: Final uber trio spawn\n");
                        }
                        else if (c >= 300 && c <= 499) {
                            /* Teleport: cmd is 300 + areaId.
                             * g_pendingZoneTeleport is file-static in
                             * d2arch_zones.c but visible here because
                             * unity build (all .c included in d2arch.c
                             * — zones.c at line 32, drawall.c at 52). */
                            g_pendingZoneTeleport = c - 300;
                            Log("CHEAT TELEPORT: requested area %d\n", c - 300);
                        }
                        else if (c >= 500 && c <= 599) {
                            /* Portals tab — spawn a PHYSICAL portal at the
                             * player. The gameloop handler calls
                             * D2GAME_CreatePortalObject to create an actual
                             * walk-into portal object.
                             *
                             * Object IDs:
                             *   59 = TOWN_PORTAL (blue, like cast TP scroll)
                             *   60 = PERMANENT_TOWN_PORTAL (red, Cow/Trist)
                             *
                             * Restriction: in town (Rogue Camp / Lut Gholein
                             * / Kurast Docks / Pandemonium / Harrogath) D2
                             * only allows dest=Cow + objId=60. Everywhere
                             * else any combo works.
                             */
                            int destArea = 0;
                            int objId    = 60;  /* default red permanent */
                            switch (c) {
                                case 501: destArea = 39;  objId = 60; break; /* Cow Level */
                                default: break;
                            }
                            if (destArea > 0) {
                                g_pendingPortalLevel = destArea;
                                g_pendingPortalObjId = objId;
                                Log("CHEAT PORTAL spawn: cmd=%d destArea=%d objId=%d\n",
                                    c, destArea, objId);
                            }
                        }
                        /* 1.9.1 — Loot tab: Runes / Gems / Misc grid buttons.
                         * Each button maps to a single 3-char item code.
                         * The dispatch sets g_cheatSingleItemCode + qual + lvl;
                         * the gameloop tick consumes them once and resets. */
                        else if (c >= 1001 && c <= 1033) {
                            /* Runes r01..r33 */
                            int n = c - 1000;
                            char tmp[8];
                            _snprintf(tmp, sizeof(tmp), "r%02d", n);
                            strncpy((char*)g_cheatSingleItemCode, tmp, 7);
                            g_cheatSingleItemQuality = 2;
                            g_cheatSingleItemLvl     = 50;
                            Log("CHEAT LOOT: rune %s queued\n", tmp);
                        }
                        else if (c >= 1101 && c <= 1135) {
                            /* Gems — 35 entries in fixed order matching
                             * TAB_LOOT_GEMS above (Amethyst..Skull, Chipped..Perfect).
                             * Order verified against d2arch_gameloop.c GEMS_ALL. */
                            static const char* GEM_CODES[35] = {
                                "gcv","gfv","gsv","gzv","gpv",  /* Amethyst (note gzv flawless) */
                                "gcy","gfy","gsy","gly","gpy",  /* Topaz */
                                "gcb","gfb","gsb","glb","gpb",  /* Sapphire */
                                "gcg","gfg","gsg","glg","gpg",  /* Emerald */
                                "gcr","gfr","gsr","glr","gpr",  /* Ruby */
                                "gcw","gfw","gsw","glw","gpw",  /* Diamond */
                                "skc","skf","sku","skl","skz",  /* Skull (no g-prefix) */
                            };
                            int idx = c - 1101;
                            strncpy((char*)g_cheatSingleItemCode, GEM_CODES[idx], 7);
                            g_cheatSingleItemQuality = 2;
                            g_cheatSingleItemLvl     = 50;
                            Log("CHEAT LOOT: gem %s queued\n", GEM_CODES[idx]);
                        }
                        else if (c >= 1201 && c <= 1254) {
                            /* Misc — Pandemonium + charms + tomes + scrolls + key + cube + pots.
                             * Codes traced from DEBUG_MENU_ITEM_CATALOG_2026-05-01.md
                             * sections 5a-5k. Order matches TAB_LOOT_MISC labels above. */
                            const char* code = NULL;
                            BYTE qual = 2;
                            int  lvl  = 50;
                            switch (c) {
                                /* Pandemonium event */
                                case 1201: code = "pk1"; break;
                                case 1202: code = "pk2"; break;
                                case 1203: code = "pk3"; break;
                                case 1204: code = "mbr"; break;
                                case 1205: code = "dhn"; break;
                                case 1206: code = "bey"; break;
                                case 1207: code = "tes"; break;
                                case 1208: code = "ceh"; break;
                                case 1209: code = "bet"; break;
                                case 1210: code = "fed"; break;
                                /* Hellfire Torch — unique cm2 (lvl 99 quality 7) */
                                case 1211: code = "cm2"; qual = 7; lvl = 99; break;
                                case 1212: code = "toa"; break;
                                /* Charms (magic quality 4) */
                                case 1220: code = "cm1"; qual = 4; break;
                                case 1221: code = "cm2"; qual = 4; break;
                                case 1222: code = "cm3"; qual = 4; break;
                                /* Tomes / scrolls / key / cube */
                                case 1230: code = "tbk"; break;
                                case 1231: code = "ibk"; break;
                                case 1232: code = "tsc"; break;
                                case 1233: code = "isc"; break;
                                case 1234: code = "key"; break;
                                case 1235: code = "box"; break;
                                /* Healing pots */
                                case 1240: code = "hp1"; break;
                                case 1241: code = "hp2"; break;
                                case 1242: code = "hp3"; break;
                                case 1243: code = "hp4"; break;
                                case 1244: code = "hp5"; break;
                                /* Mana pots */
                                case 1245: code = "mp1"; break;
                                case 1246: code = "mp2"; break;
                                case 1247: code = "mp3"; break;
                                case 1248: code = "mp4"; break;
                                case 1249: code = "mp5"; break;
                                /* Rejuv + specialty */
                                case 1250: code = "rvs"; break;
                                case 1251: code = "rvl"; break;
                                case 1252: code = "vps"; break;
                                case 1253: code = "yps"; break;
                                case 1254: code = "wms"; break;
                                default: break;
                            }
                            if (code) {
                                strncpy((char*)g_cheatSingleItemCode, code, 7);
                                g_cheatSingleItemQuality = qual;
                                g_cheatSingleItemLvl     = lvl;
                                Log("CHEAT LOOT: misc %s qual=%d lvl=%d queued\n",
                                    code, qual, lvl);
                            }
                        }
                        /* 1.9.2 — Mons tab Bosses grid: cmd 700-704 spawn
                         * vanilla act bosses via fnSpawnMonster with their
                         * MonStats.txt row IDs. The 6 ubers (cmd 230-235)
                         * and 2 trios (cmd 240-241) are handled by the
                         * existing g_cheatSpawnUber dispatch above. */
                        else if (c >= 700 && c <= 704) {
                            static const int ACT_BOSS_ROWS[5] = {
                                156,  /* Andariel */
                                211,  /* Duriel */
                                242,  /* Mephisto */
                                243,  /* Diablo */
                                544,  /* Baal */
                            };
                            g_cheatSpawnMonsterRowId = ACT_BOSS_ROWS[c - 700];
                            Log("CHEAT MONS: Boss queued row=%d (cmd=%d)\n",
                                g_cheatSpawnMonsterRowId, c);
                        }
                        else if (c == 999) { g_cheatMenuOpen = FALSE; }
                        if (mc->notify) ShowNotify(mc->notify);
                    }
                }
            }

            colCur++;
            if (colCur >= COLS) { colCur = 0; yCur += CELL_H + CELL_GAP; }
        }

cheat_menu_done:
        ;  /* fallthrough target for the Loot scrollable-list early-out */

        #undef MK_HDR
        #undef MK_BTN
        #undef MK_GAP
    }

    /* TOOLTIP: drawn here so it works regardless of panelOpen nesting */
    if (g_tooltipSkillId >= 0) {
        DrawSkillTooltip(g_tooltipSkillId, g_tooltipMX, g_tooltipMY);
        g_tooltipSkillId = -1;
    }

    /* Clear reset flag only when panel was open (so it actually got processed) */
    if (g_skillPanelReset) {
        int po = 0;
        __try { po = *(int*)0x6FBBA6B8; } __except(1) {}
        if (po) g_skillPanelReset = FALSE;
    }

    /* Controller input (XInput gamepad) */
    if (g_controllerEnabled) ProcessController();

    /* Periodically apply skill tree patches */
    PeriodicApply();

    /* 1.9.0: System 1 — entrance shuffle teleport hook.
     * Polls area changes and queues redirects via g_pendingZoneTeleport.
     * Runs BEFORE zone-locking so zone-locking can still override the
     * destination if the shuffled cave turns out to be locked. */
    EntranceShuffle_Tick();

    /* Zone Gating enforcement (Zone Locking toggle) */
    if (g_zoneLockingOn) {
        int curArea = GetCurrentArea();
        if (curArea > 0 && curArea < MAX_AREA_ID) {
            if (IsAreaLocked(curArea)) {
                DWORD now = GetTickCount();

                /* Show notification (throttled) */
                if (now - g_lastZoneBlockTime > 2000) {
                    g_lastZoneBlockTime = now;
                    ShowNotify("Zone locked! Returning to town...");
                    Log("ZONE BLOCKED: area %d is locked\n", curArea);
                }

                /* Draw black overlay */
                if (fnRect) fnRect(0, 0, g_screenW, g_screenH, 0x00, 5);

                /* Queue teleport — executed in HookD2DebugGame (server context)
                 * via LEVEL_WarpUnit which has NO proximity checks */
                g_pendingZoneTeleport = GetActTown(curArea);

                /* Teleport handled by HookD2DebugGame via LEVEL_WarpUnit */
            } else {
                g_lastSafeArea = curArea;
            }
        }
    }

    /* 1.9.0: entrance-shuffle test removed 2026-04-27. Findings written
     * to Research/teleport_warp_findings_2026-04-27.md. The proof-of-
     * concept showed LEVEL_WarpUnit can redirect to any area-id but
     * cannot pick a specific entry tile (waypoint vs entrance) — its
     * nTileCalc parameter is ignored on outdoor levels in 1.10f. Re-
     * enable by re-adding this block once a safe entrance-pick
     * mechanism is found (likely manual room iteration to find the
     * warp-tile object pointing back to the source level). */

    /* ================================================================
     * 1.9.0 OBJECT-SPAWN TEST — fires once per Blood Moor entry. Sets
     * `g_pendingObjectTestSpawn` so the next server-context tick in
     * gameloop.c spawns 30 of each object type (barrels, chests,
     * urns, caskets, traps) at random walkable tiles around the
     * player. Purpose: empirically verify which D2 object IDs are
     * runtime-spawnable in 1.10f via SUNIT_AllocUnitData.
     *
     * Debounced via static `s_lastObjTestArea` so we trigger ONCE per
     * entry, not every game tick while the player is in Blood Moor.
     * ================================================================ */
    /* 1.9.0: object-spawn test removed 2026-04-27. Findings in
     * Research/object_spawn_findings_2026-04-27.md. The runtime-spawn
     * approach via OBJECTS_CreateObject works mechanically but doesn't
     * give "objects baked into the map" semantics — they spawn at the
     * player and don't persist destroyed state across re-entry. The
     * recommended next path is editing objects.txt + Levels.txt +
     * objgroup.txt to make D2's vanilla level-gen pipeline place the
     * objects naturally. See research doc for the four-approach
     * comparison and verdict. */

    /* 1.8.0 cleanup: Treasure Cow area-entry spawn block extracted to
     * Tools/Archipelago/pending_reimplementation/TREASURE_COWS/source_snippets/d2arch_drawall.c.snippets */

    /* Periodic save */
    PeriodicSave();

    /* AP status and unlock polling */
    PollAPStatus();
    PollAPUnlocks();
    if (g_apConnected) {
        LoadLocationOwners();
        LoadAPItemLocations();      /* 1.8.5 — gate-key locations for F4 */
    }

    /* DeathLink: check if player died */
    CheckPlayerDeath();

    /* Keep saved class updated */
    {
        int pc = GetPlayerClass();
        if (pc >= 0) g_savedClass = pc;
    }

    /* Track player pointer for "player lost" detection */
    g_lastPlayerPtr = player;
}
