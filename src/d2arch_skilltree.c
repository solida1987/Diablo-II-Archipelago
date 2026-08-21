
/* CROSS-CLASS ANIMATION FIX Skills using S1/S2/S3/S4 animation modes only have animation files for their native class. */
#define SKT_CHARCLASS   0x0C  /* int8_t — owning class (-1=generic) */
#define SKT_ANIM        0x10  /* uint8_t — animation mode */
#define SKT_ITYPEA      0x18  /* int16_t[3] — required weapon types for attack A */
#define SKT_ITYPEB      0x1E  /* int16_t[3] — required weapon types for attack B */
#define SKT_ETYPEA      0x24  /* int16_t[2] — excluded weapon types for attack A */
#define SKT_ETYPEB      0x28  /* int16_t[2] — excluded weapon types for attack B */
#define SKT_MAXLVL      0x12C /* uint16_t — skill max level cap (0 = D2 defaults to 20) */

static BOOL g_animPatchApplied = FALSE;

/* BOSS TC NAME RESOLVER (Fix 7) The old code used hardcoded row indices (667/826/685/691/718). */

/* TreasureClassEx row size — empirically 0x104 in 1.10f (name + pickups array + probs) */
#define TC_ROW_SIZE       0x104
/* Probed sgptDataTables offset pairs (ptr + count) for TreasureClassEx */
#define TC_PTR_OFFSET_A   0xBE0  /* primary probe */
#define TC_CNT_OFFSET_A   0xBE8
/* First column of TC row is the TC name string (C string, ~32 bytes) */
#define TC_NAME_OFFSET    0x00
#define TC_NAME_MAX       32

static int ResolveBossTCByName(const char* tcName) {
    DWORD dt = GetSgptDT();
    if (!dt || !tcName) return -1;
    __try {
        DWORD arr = *(DWORD*)(dt + TC_PTR_OFFSET_A);
        int cnt = *(int*)(dt + TC_CNT_OFFSET_A);
        if (!arr || cnt <= 0 || cnt > 10000) return -1;

        /* Validate: first row's name must read as a printable C-string */
        const char* probe = (const char*)(arr + TC_NAME_OFFSET);
        int ok = 0;
        for (int i = 0; i < TC_NAME_MAX; i++) {
            char c = probe[i];
            if (c == 0) { ok = (i > 0); break; }
            if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) { ok = 0; break; }
        }
        if (!ok) return -1;

        for (int r = 0; r < cnt; r++) {
            const char* name = (const char*)(arr + r * TC_ROW_SIZE + TC_NAME_OFFSET);
            if (strncmp(name, tcName, TC_NAME_MAX) == 0) return r;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}

static void ResolveBossLootTCs(void) {
    if (g_bossLootTCsResolved) return;
    g_bossLootTCsResolved = TRUE;

    int resolved = 0;
    for (int i = 0; i < BOSS_LOOT_COUNT; i++) {
        /* resolve the HELL TC names (g_bossLootTCNames), not the display names, so Awesome Loot pulls from the Hell pools instead of the junk-heavy Normal boss TCs. */
        int row = ResolveBossTCByName(g_bossLootTCNames[i]);
        if (row >= 0) {
            g_bossLootTCs[i] = row;
            resolved++;
            Log("BOSS TC resolved: %s -> row %d\n", g_bossLootTCNames[i], row);
        }
    }
    if (resolved == 0) {
        Log("BOSS TC: name resolve failed, using hardcoded fallback (Andariel=%d Duriel=%d Mephisto=%d Diablo=%d Baal=%d)\n",
            g_bossLootTCs[0], g_bossLootTCs[1], g_bossLootTCs[2], g_bossLootTCs[3], g_bossLootTCs[4]);
    } else if (resolved < BOSS_LOOT_COUNT) {
        Log("BOSS TC: %d/%d resolved, rest use hardcoded fallback\n", resolved, BOSS_LOOT_COUNT);
    }
}

/* Cross-class animation fallback.
 *
 * A skill whose anim mode is class-specific plays an animation sequence that
 * only exists for its own class, and often only for certain weapon classes.
 * Hand it to another class through the skill pool and the client has nothing
 * to play: at best the skill is unusable, at worst it crashes.
 *
 * This used to be three hand-maintained lists of skill ids. They went stale,
 * and how we found out is instructive: Jab sat in the TH list, but Jab's anim
 * is SQ, so the TH branch never matched it and the SQ branch never looked at
 * it. A paladin holding a sword got Jab's sequence animation and crashed. An
 * audit of Skills.txt then found eleven skills in that state -- Jab, Impale,
 * Inferno, Lightning, Chain Lightning, Charge, Leap, Frenzy, Arctic Blast,
 * Dragon Tail, Dragon Flight -- and four more entries filed under an anim
 * they do not have, which is exactly what made the lists look complete.
 *
 * So there is no list any more. The rule reads the anim mode itself, and
 * cannot fall out of date:
 *
 *     non-native class + anim in {S1..S4, TH, KK, SQ}  ->  A1
 *
 * Native classes keep their own animation untouched. A1 is the plain attack
 * animation every class has for every weapon, and it is the same fallback
 * that fixed Whirlwind. */
#define ANIM_A1   7    /* PLRMODE_ATTACK1 - exists for every class+weapon */
#define ANIM_TH   11   /* throw    - amazon javelins */
#define ANIM_KK   12   /* kick     - assassin kicks */
#define ANIM_S1   13   /* S1..S4   - per-class special casts */
#define ANIM_S4   16
#define ANIM_SQ   18   /* sequence - whirlwind, jab, charge, frenzy, ... */

static BOOL AnimNeedsCrossClassFallback(BYTE anim) {
    return (anim >= ANIM_S1 && anim <= ANIM_S4) ||
           anim == ANIM_TH || anim == ANIM_KK || anim == ANIM_SQ;
}

/* vanilla class-skill-list cache for OnCharacterUnload restore. */
#define VANILLA_CLASS_LIST_STRIDE 30
static short g_origClassList[7][VANILLA_CLASS_LIST_STRIDE];
static int   g_origClassCounts[7];
static BOOL  g_origClassListCached[7] = { FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE };

/* vanilla-anim cache for the per-class repatch path. */
#define ANIM_CACHE_MAX 500
static BYTE g_origAnim[ANIM_CACHE_MAX];
static char g_origNativeClass[ANIM_CACHE_MAX];
static BOOL g_animCacheReady = FALSE;
static int  g_repatchedForClass = -2;  /* -2 = never; -1 = generic; 0..6 = class id */

static void PatchAllSkillAnimations(void) {
    if (g_animPatchApplied) return;
    DWORD dt = GetSgptDT();
    if (!dt) return;

    __try {
        DWORD arr = *(DWORD*)(dt + DT_SKILLS);
        int cnt = *(int*)(dt + DT_SKILLS_N);
        if (!arr || cnt <= 0) return;

        /* initialise the vanilla-anim cache once, before we touch the skill table. */
        if (!g_animCacheReady) {
            memset(g_origAnim, 0xFF, sizeof(g_origAnim));
            memset(g_origNativeClass, -1, sizeof(g_origNativeClass));
            g_animCacheReady = TRUE;
        }

        int weaponPatched = 0;
        int maxLvlPatched = 0;
        DWORD oldProt;
        VirtualProtect((void*)arr, cnt * SKT_SIZE, PAGE_READWRITE, &oldProt);

        for (int i = 0; i < cnt; i++) {
            DWORD rec = arr + i * SKT_SIZE;
            char charClass = *(char*)(rec + SKT_CHARCLASS);
            BYTE anim = *(BYTE*)(rec + SKT_ANIM);

            /* Cache vanilla values BEFORE we mutate them. */
            if (i < ANIM_CACHE_MAX) {
                g_origAnim[i]        = anim;
                g_origNativeClass[i] = charClass;
            }

            /* FIX 1.8.0: Lift per-skill level cap from 20 -> 99 for player skills. */
            if (charClass >= 0 && charClass <= 6) {
                WORD* pMaxLvl = (WORD*)(rec + SKT_MAXLVL);
                if (*pMaxLvl < 99) {
                    *pMaxLvl = 99;
                    maxLvlPatched++;
                }
            }

            /* ANIMATIONS ARE NOT TOUCHED HERE. See RestoreNativeAnimsForClass.
             *
             * This used to rewrite every class-specific animation (S1-S4, SQ,
             * TH, KK) to A1 for all seven classes at once, before anybody knew
             * which class was about to be played. RestoreNativeAnimsForClass
             * then put the player's OWN skills back to their real animations a
             * moment later, at character load.
             *
             * That write-and-unwrite killed the Sorceress: D2Client keeps a
             * queue of commands it has predicted and is waiting for the server
             * to confirm, and changing a skill's animation out from under it
             * desynchronised that queue. The game died on its own assert --
             * SCmd.cpp line 3910, ptCmd->bCmd == SCMD_WEAPONSWAP -- seconds
             * after entering town. She was hit because three of her skills
             * carry those animations natively; a class with none was never
             * rewritten and never crashed.
             *
             * RestoreNativeAnimsForClass computes the finished state for every
             * skill in one pass and knows the class, so it can do the whole job
             * without ever writing a value it has to take back. It is now the
             * only code that writes SKT_ANIM. The vanilla cache filled above is
             * what it reads. */

            /* FIX 2: Remove weapon type restrictions for class-owned skills. */
            if (charClass >= 0 && charClass <= 6) {
                BOOL had = FALSE;
                for (int w = 0; w < 3; w++) {
                    short* pA = (short*)(rec + SKT_ITYPEA + w * 2);
                    short* pB = (short*)(rec + SKT_ITYPEB + w * 2);
                    if (*pA != 0) { *pA = 0; had = TRUE; }
                    if (*pB != 0) { *pB = 0; had = TRUE; }
                }
                for (int e = 0; e < 2; e++) {
                    short* pEA = (short*)(rec + SKT_ETYPEA + e * 2);
                    short* pEB = (short*)(rec + SKT_ETYPEB + e * 2);
                    if (*pEA != 0) { *pEA = 0; had = TRUE; }
                    if (*pEB != 0) { *pEB = 0; had = TRUE; }
                }
                if (had) weaponPatched++;
            }
        }

        VirtualProtect((void*)arr, cnt * SKT_SIZE, oldProt, &oldProt);
        g_animPatchApplied = TRUE;
        Log("SKILL PATCH (boot): %d weapon restrictions removed, %d maxlvl 20->99 (animations are applied per class at character load)\n",
            weaponPatched, maxLvlPatched);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("SKILL PATCH: EXCEPTION during patching\n");
    }
}

/* restore native-class skill animations + re-apply cross-class A1 fallback for non-native skills based on the player's class. */
static void RestoreNativeAnimsForClass(int playerClass) {
    /* fix race condition: pre-1.9.11 this function early-returned when g_animPatchApplied was FALSE, which happened when the player loaded a character BEFORE PeriodicApply had run PatchAllSkillAnimations even once. */
    if (!g_animPatchApplied || !g_animCacheReady) {
        Log("ANIM REPATCH: boot patch not yet run — driving it synchronously now (class=%d)\n", playerClass);
        PatchAllSkillAnimations();
        if (!g_animPatchApplied || !g_animCacheReady) {
            Log("ANIM REPATCH: PatchAllSkillAnimations could not complete (sgptDT not ready?); skipping for class=%d\n", playerClass);
            return;
        }
    }
    if (playerClass < 0 || playerClass > 6) {
        Log("ANIM REPATCH skipped: invalid class %d\n", playerClass);
        return;
    }
    if (playerClass == g_repatchedForClass) {
        return; /* already in this state, nothing to do */
    }

    DWORD dt = GetSgptDT();
    if (!dt) return;

    __try {
        DWORD arr = *(DWORD*)(dt + DT_SKILLS);
        int cnt = *(int*)(dt + DT_SKILLS_N);
        if (!arr || cnt <= 0) return;

        int restored = 0, fallbacked = 0, unchanged = 0;
        DWORD oldProt;
        int walkCnt = (cnt < ANIM_CACHE_MAX) ? cnt : ANIM_CACHE_MAX;
        VirtualProtect((void*)arr, cnt * SKT_SIZE, PAGE_READWRITE, &oldProt);

        for (int i = 0; i < walkCnt; i++) {
            char native = g_origNativeClass[i];
            BYTE orig   = g_origAnim[i];
            if (native < 0 || native > 6) continue;  /* generic / monster skill */

            BYTE want;
            if (native == playerClass) {
                /* Native owner — restore vanilla anim (Smite gets S1, Whirlwind gets SQ, Rabies gets S3, javelins get TH, Dragon Talon gets KK) */
                want = orig;
            } else {
                /* Non-native — re-apply the same A1 fallback as PatchAllSkillAnimations */
                want = AnimNeedsCrossClassFallback(orig)
                       ? (BYTE)ANIM_A1 : orig;
            }

            DWORD rec = arr + i * SKT_SIZE;
            BYTE  cur = *(BYTE*)(rec + SKT_ANIM);
            if (cur != want) {
                *(BYTE*)(rec + SKT_ANIM) = want;
                if (native == playerClass) restored++;
                else                       fallbacked++;
            } else {
                unchanged++;
            }
        }

        VirtualProtect((void*)arr, cnt * SKT_SIZE, oldProt, &oldProt);
        g_repatchedForClass = playerClass;
        Log("ANIM REPATCH for class %d: restored %d native, re-fallbacked %d non-native, %d unchanged\n",
            playerClass, restored, fallbacked, unchanged);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("ANIM REPATCH: EXCEPTION (class=%d)\n", playerClass);
    }
}

/* Public hook called from d2arch_save.c OnCharacterLoad. */
extern int GetPlayerClass(void);
void Skilltree_OnCharacterLoadHook(void) {
    int pc = GetPlayerClass();
    /* The live player unit is not always up this early. Skipping the pass
     * used to be harmless because the boot patch had already applied a
     * blanket A1 fallback; now this is the only code that writes
     * animations, so a skip would leave every cross-class skill without
     * one. Fall back to the class the save file reported -- the same hint
     * InitSkillPool uses for exactly this window. */
    if (pc < 0 && g_skillClassHint >= 0) {
        pc = g_skillClassHint;
        Log("Skilltree_OnCharacterLoadHook: player unit not ready — using saved class %d\n", pc);
    }
    if (pc < 0) {
        Log("Skilltree_OnCharacterLoadHook: class unknown, skipping repatch\n");
        return;
    }
    RestoreNativeAnimsForClass(pc);
}

/* Public hook called from d2arch_main.c WndProc EXIT (char-unload path). */
void Skilltree_OnCharacterUnloadHook(void) {
    DWORD dt = GetSgptDT();
    if (!dt) {
        Log("Skilltree_OnCharacterUnloadHook: no sgptDT (skipping)\n");
        return;
    }

    int charclassRestored = 0;
    int reqLevelReset = 0;
    int classListsRestored = 0;

    __try {
        DWORD arr = *(DWORD*)(dt + DT_SKILLS);
        int cnt = *(int*)(dt + DT_SKILLS_N);
        if (!arr || cnt <= 0) {
            Log("Skilltree_OnCharacterUnloadHook: bad skills array (skipping)\n");
            return;
        }

        DWORD oldProt;

        /* Step 1+2: walk per-skill charclass cache (lives in d2arch_zones.c shared with this TU via unity-build) and restore vanilla charclass + reset reqLevel/reqSkill that PatchSkillForPlayer + SetSkillTierReqs mutated. */
        if (g_origCacheInit) {
            int walkN = (cnt < 400) ? cnt : 400;
            for (int i = 0; i < walkN; i++) {
                short cached = g_origCharClass[i];
                if (cached == -1) continue; /* skill never patched */

                DWORD rec = arr + i * SKT_SIZE;

                /* Restore vanilla charclass */
                VirtualProtect((void*)(rec + SKT_CHARCLASS), 1, PAGE_READWRITE, &oldProt);
                *(BYTE*)(rec + SKT_CHARCLASS) = (BYTE)cached;
                VirtualProtect((void*)(rec + SKT_CHARCLASS), 1, oldProt, &oldProt);
                charclassRestored++;

                /* Reset reqLevel + reqSkill[0..2] (we don't cache vanilla values per skill; reqLevel=1 is universally safe and reqSkill=-1 means no prerequisite). */
                VirtualProtect((void*)(rec + SKT_REQLEVEL), 2, PAGE_READWRITE, &oldProt);
                *(WORD*)(rec + SKT_REQLEVEL) = 1;
                VirtualProtect((void*)(rec + SKT_REQLEVEL), 2, oldProt, &oldProt);
                reqLevelReset++;

                VirtualProtect((void*)(rec + SKT_REQSKILL0), 6, PAGE_READWRITE, &oldProt);
                *(short*)(rec + SKT_REQSKILL0) = -1;
                *(short*)(rec + SKT_REQSKILL1) = -1;
                *(short*)(rec + SKT_REQSKILL2) = -1;
                VirtualProtect((void*)(rec + SKT_REQSKILL0), 6, oldProt, &oldProt);
            }
        }

        /* Step 3: restore class skill list at sgptDT+0xBA0..0xBC0 for every class we cached. */
        {
            int stride = *(int*)(dt + 0xBA8);
            short* list = *(short**)(dt + 0xBAC);
            int* counts = *(int**)(dt + 0xBA4);
            if (list && counts && stride > 0) {
                for (int cls = 0; cls < 7; cls++) {
                    if (!g_origClassListCached[cls]) continue;
                    short* classStart = list + cls * stride;
                    int copyN = (stride < VANILLA_CLASS_LIST_STRIDE)
                                ? stride : VANILLA_CLASS_LIST_STRIDE;
                    VirtualProtect(classStart, stride * sizeof(short),
                                   PAGE_READWRITE, &oldProt);
                    for (int i = 0; i < copyN; i++) {
                        classStart[i] = g_origClassList[cls][i];
                    }
                    VirtualProtect(classStart, stride * sizeof(short),
                                   oldProt, &oldProt);

                    VirtualProtect(&counts[cls], sizeof(int),
                                   PAGE_READWRITE, &oldProt);
                    counts[cls] = g_origClassCounts[cls];
                    VirtualProtect(&counts[cls], sizeof(int),
                                   oldProt, &oldProt);
                    classListsRestored++;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("Skilltree_OnCharacterUnloadHook: EXCEPTION\n");
    }

    /* Step 4: reset repatch sentinel so next char load triggers fresh anim walk. */
    g_repatchedForClass = -2;

    Log("Skilltree_OnCharacterUnloadHook: restored charclass=%d, reqLevel=%d, classLists=%d\n",
        charclassRestored, reqLevelReset, classListsRestored);
}

/* Get SkillDesc index for a skill ID */
static int GetSkillDescIdx(int skillId) {
    DWORD dt = GetSgptDT(); if (!dt) return -1;
    __try {
        DWORD arr = *(DWORD*)(dt + DT_SKILLS);
        int cnt = *(int*)(dt + DT_SKILLS_N);
        if (!arr || skillId < 0 || skillId >= cnt) return -1;
        return (int)*(WORD*)(arr + skillId * SKT_SIZE + SKT_SKILLDESC);
    } __except(EXCEPTION_EXECUTE_HANDLER) {} return -1;
}

/* Set skill tree position in SkillDescTxt */
static void SetSkillTreePos(int descIdx, int page, int row, int col) {
    DWORD dt = GetSgptDT(); if (!dt) return;
    __try {
        DWORD arr = *(DWORD*)(dt + DT_SKILLDESC);
        int cnt = *(int*)(dt + DT_SKILLDESC_N);
        if (!arr || descIdx < 0 || descIdx >= cnt) return;
        DWORD rec = arr + descIdx * SDT_SIZE;
        DWORD op;
        VirtualProtect((void*)(rec + SDT_PAGE), 3, PAGE_READWRITE, &op);
        *(BYTE*)(rec + SDT_PAGE) = (BYTE)page;
        *(BYTE*)(rec + SDT_ROW)  = (BYTE)row;
        *(BYTE*)(rec + SDT_COL)  = (BYTE)col;
        VirtualProtect((void*)(rec + SDT_PAGE), 3, op, &op);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* Patch skill's charclass to player's class (makes it appear in tree) */
static void PatchSkillForPlayer(int skillId) {
    DWORD dt = GetSgptDT(); if (!dt) return;
    int pc = GetPlayerClass();
    if (pc < 0) pc = (g_savedClass >= 0) ? g_savedClass : 0;
    __try {
        DWORD arr = *(DWORD*)(dt + DT_SKILLS);
        int cnt = *(int*)(dt + DT_SKILLS_N);
        if (!arr || skillId < 0 || skillId >= cnt) return;
        DWORD rec = arr + skillId * SKT_SIZE;
        DWORD op;

        /* Cache original charclass */
        if (!g_origCacheInit) {
            g_origCacheInit = TRUE;
            memset(g_origCharClass, 0xFF, sizeof(g_origCharClass));
        }
        if (skillId < 400 && g_origCharClass[skillId] == -1) {
            g_origCharClass[skillId] = (short)*(BYTE*)(rec + SKT_CHARCLASS);
        }

        /* Set charclass to player's class */
        VirtualProtect((void*)(rec + SKT_CHARCLASS), 1, PAGE_READWRITE, &op);
        *(BYTE*)(rec + SKT_CHARCLASS) = (BYTE)pc;
        VirtualProtect((void*)(rec + SKT_CHARCLASS), 1, op, &op);

        /* Clear prerequisites (set by SetSkillTierReqs later) */
        VirtualProtect((void*)(rec + SKT_REQLEVEL), 2, PAGE_READWRITE, &op);
        *(WORD*)(rec + SKT_REQLEVEL) = 1;
        VirtualProtect((void*)(rec + SKT_REQLEVEL), 2, op, &op);

        VirtualProtect((void*)(rec + SKT_REQSKILL0), 6, PAGE_READWRITE, &op);
        *(short*)(rec + SKT_REQSKILL0) = -1;
        *(short*)(rec + SKT_REQSKILL1) = -1;
        *(short*)(rec + SKT_REQSKILL2) = -1;
        VirtualProtect((void*)(rec + SKT_REQSKILL0), 6, op, &op);

        /* 2.1.1 (infiL — "skill points over lvl 20 lost on reload") Re-assert the 20->99 max-level cap lift on THIS skill record. */
        VirtualProtect((void*)(rec + SKT_MAXLVL), 2, PAGE_READWRITE, &op);
        if (*(WORD*)(rec + SKT_MAXLVL) < 99) *(WORD*)(rec + SKT_MAXLVL) = 99;
        VirtualProtect((void*)(rec + SKT_MAXLVL), 2, op, &op);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* Skill icon mapping: skillId -> frame index in universal DC6 */
static int g_iconMap[400]; /* skillId -> frame index, -1 = not mapped */
static BOOL g_iconMapLoaded = FALSE;
static BOOL g_iconCacheInit = FALSE;

static void LoadIconMap(void) {
    if (g_iconMapLoaded) return;
    g_iconMapLoaded = TRUE;
    memset(g_iconMap, 0xFF, sizeof(g_iconMap)); /* -1 = unmapped */

    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(path, "%sskill_icon_map.dat", dir);

    FILE* f = fopen(path, "r");
    if (!f) { Log("LoadIconMap: file not found at %s\n", path); return; }

    char line[64];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        int skillId, frame;
        if (sscanf(line, "%d=%d", &skillId, &frame) == 2) {
            if (skillId >= 0 && skillId < 400) {
                g_iconMap[skillId] = frame;
                count++;
            }
        }
    }
    fclose(f);
    Log("LoadIconMap: loaded %d mappings\n", count);
}

/* Original nIconCel values for AmSkillicon.DC6 (D2's vanilla right-click selector). */
static int g_origCel[400];
static BOOL g_origCelLoaded = FALSE;

static void LoadOrigIconCel(void) {
    if (g_origCelLoaded) return;
    g_origCelLoaded = TRUE;
    memset(g_origCel, 0xFF, sizeof(g_origCel)); /* -1 = not mapped */

    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(path, "%sorig_iconcel.dat", dir);
    FILE* f = fopen(path, "r");
    if (!f) { Log("LoadOrigIconCel: file not found\n"); return; }
    char line[32];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        int sid = 0, cel = 0;
        if (sscanf(line, "%d=%d", &sid, &cel) == 2 && sid >= 0 && sid < 400) {
            g_origCel[sid] = cel;
            count++;
        }
    }
    fclose(f);
    Log("LoadOrigIconCel: loaded %d entries\n", count);
}

/* Set nIconCel in SkillDescTxt to the ORIGINAL value for this skill. */
static void SetSkillIcon(int skillId) {
    DWORD dt = GetSgptDT(); if (!dt) return;
    int descIdx = GetSkillDescIdx(skillId);
    if (descIdx < 0) return;

    LoadOrigIconCel();

    __try {
        DWORD descArr = *(DWORD*)(dt + DT_SKILLDESC);
        int descCnt = *(int*)(dt + DT_SKILLDESC_N);
        if (!descArr || descIdx < 0 || descIdx >= descCnt) return;
        DWORD rec = descArr + descIdx * SDT_SIZE;

        /* Patch nIconCel to match our rebuilt Skillicon DC6 frame order. */
        LoadIconMap();
        if (skillId >= 0 && skillId < 400 && g_iconMap[skillId] >= 0) {
            BYTE newCel = (BYTE)g_iconMap[skillId];
            DWORD op;
            VirtualProtect((void*)(rec + SDT_ICONCEL), 1, PAGE_READWRITE, &op);
            *(BYTE*)(rec + SDT_ICONCEL) = newCel;
            VirtualProtect((void*)(rec + SDT_ICONCEL), 1, op, &op);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* Place a skill in the vanilla tree at given position */
static void PlaceSkillInTree(int skillId, int page, int row, int col) {
    int descIdx = GetSkillDescIdx(skillId);
    if (descIdx < 0) return;
    SetSkillTreePos(descIdx, page, row, col);
    SetSkillIcon(skillId);
    PatchSkillForPlayer(skillId);
}

/* Remove a skill from the vanilla tree */
static void ClearSkillFromTree(int skillId) {
    int descIdx = GetSkillDescIdx(skillId);
    if (descIdx >= 0) SetSkillTreePos(descIdx, 0, 0, 0);
}

/* Set tier-based level requirements and prerequisites */
static void SetSkillTierReqs(int skillId, int cls, int tab, int slot) {
    DWORD dt = GetSgptDT(); if (!dt) return;
    __try {
        DWORD arr = *(DWORD*)(dt + DT_SKILLS);
        int cnt = *(int*)(dt + DT_SKILLS_N);
        if (!arr || skillId < 0 || skillId >= cnt) return;
        DWORD rec = arr + skillId * SKT_SIZE;
        DWORD op;

        /* Skill Level Requirements toggle (launcher SkillLevelReqs / AP slot_data skill_level_reqs). */
        extern BOOL g_skillLevelReqs;
        int row = TREE_POS[cls][tab][slot][0];
        WORD reqLvl = 1;
        if (g_skillLevelReqs) {
            if (row >= 3 && row <= 4) reqLvl = 10;
            else if (row >= 5) reqLvl = 20;
        }

        VirtualProtect((void*)(rec + SKT_REQLEVEL), 2, PAGE_READWRITE, &op);
        *(WORD*)(rec + SKT_REQLEVEL) = reqLvl;
        VirtualProtect((void*)(rec + SKT_REQLEVEL), 2, op, &op);

        /* Find prerequisite: T2 needs T1, T3 needs T2 in same tab. */
        short prereq = -1;
        int needTier = 0;
        if (g_skillLevelReqs) {
            if (row >= 3 && row <= 4) needTier = 1;
            else if (row >= 5) needTier = 2;
        }

        if (needTier > 0) {
            for (int s = 0; s < 10; s++) {
                int pidx = g_tabSlots[tab][s];
                if (pidx < 0) continue;
                int sRow = TREE_POS[cls][tab][s][0];
                int sTier = (sRow <= 2) ? 1 : (sRow <= 4) ? 2 : 3;
                if (sTier == needTier) {
                    prereq = (short)g_skillDB[g_pool[pidx].dbIndex].id;
                    break;
                }
            }
        }

        VirtualProtect((void*)(rec + SKT_REQSKILL0), 6, PAGE_READWRITE, &op);
        *(short*)(rec + SKT_REQSKILL0) = prereq;
        *(short*)(rec + SKT_REQSKILL1) = -1;
        *(short*)(rec + SKT_REQSKILL2) = -1;
        VirtualProtect((void*)(rec + SKT_REQSKILL0), 6, op, &op);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* Write our skills into the class skill list */
static void WriteClassSkillList(void) {
    DWORD dt = GetSgptDT(); if (!dt) return;
    int cls = GetPlayerClass();
    if (cls < 0 && g_savedClass >= 0) cls = g_savedClass;
    if (cls < 0 || cls > 6) return;

    int numCustom = 0;
    for (int t = 0; t < 3; t++)
        for (int s = 0; s < 10; s++)
            if (g_tabSlots[t][s] >= 0) numCustom++;
    if (numCustom == 0) { Log("WriteClassSkillList: no custom skills\n"); return; }

    __try {
        int stride = *(int*)(dt + 0xBA8);
        short* list = *(short**)(dt + 0xBAC);
        int* counts = *(int**)(dt + 0xBA4);
        Log("WriteClassSkillList: stride=%d list=%08X counts=%08X cls=%d\n", stride, (DWORD)list, (DWORD)counts, cls);

        /* Debug: scan around 0xBA0 to find correct offsets */
        {
            Log("  DT dump around 0xBA0:\n");
            for (int off = 0xB98; off <= 0xBC0; off += 4) {
                DWORD val = *(DWORD*)(dt + off);
                Log("    dt+0x%03X = %08X (%d)\n", off, val, (int)val);
            }
        }

        if (!list || !counts || stride <= 0) { Log("WriteClassSkillList: BAD PTRS\n"); return; }

        int origCount = counts[cls];
        short* classStart = list + cls * stride;
        DWORD op;
        VirtualProtect(classStart, stride * sizeof(short), PAGE_READWRITE, &op);

        /* cache vanilla classStart[] + counts[cls] on first write per class, BEFORE we mutate. */
        if (cls >= 0 && cls < 7 && !g_origClassListCached[cls]) {
            int copyN = (stride < VANILLA_CLASS_LIST_STRIDE) ? stride : VANILLA_CLASS_LIST_STRIDE;
            for (int i = 0; i < copyN; i++) g_origClassList[cls][i] = classStart[i];
            for (int i = copyN; i < VANILLA_CLASS_LIST_STRIDE; i++) g_origClassList[cls][i] = -1;
            g_origClassCounts[cls] = origCount;
            g_origClassListCached[cls] = TRUE;
            Log("WriteClassSkillList: cached vanilla list for class %d (origCount=%d)\n",
                cls, origCount);
        }

        /* Collect original skills not in our custom list */
        short origSkills[30];
        int nOrig = 0;
        for (int i = 0; i < origCount && i < stride && nOrig < 30; i++) {
            short sid = classStart[i];
            BOOL isCustom = FALSE;
            for (int t = 0; t < 3 && !isCustom; t++)
                for (int s = 0; s < 10 && !isCustom; s++) {
                    int pidx = g_tabSlots[t][s];
                    if (pidx >= 0 && g_skillDB[g_pool[pidx].dbIndex].id == sid)
                        isCustom = TRUE;
                }
            if (!isCustom) origSkills[nOrig++] = sid;
        }

        /* Write: custom skills first, then originals */
        int pos = 0;
        for (int t = 0; t < 3; t++)
            for (int s = 0; s < 10; s++) {
                int pidx = g_tabSlots[t][s];
                if (pidx < 0) continue;
                if (pos < stride)
                    classStart[pos++] = (short)g_skillDB[g_pool[pidx].dbIndex].id;
            }
        for (int i = 0; i < nOrig && pos < stride; i++)
            classStart[pos++] = origSkills[i];

        VirtualProtect(classStart, stride * sizeof(short), op, &op);

        int newCount = pos > origCount ? pos : origCount;
        VirtualProtect(&counts[cls], sizeof(int), PAGE_READWRITE, &op);
        counts[cls] = newCount;
        VirtualProtect(&counts[cls], sizeof(int), op, &op);

        Log("WriteClassSkillList: cls=%d custom=%d total=%d\n", cls, numCustom, newCount);
    } __except(EXCEPTION_EXECUTE_HANDLER) { Log("WriteClassSkillList CRASH\n"); }
}

/* Ensure a skill exists in the class skill list — but DO NOT reorder. */
static void InsertSkillInClassList(int skillId) {
    DWORD dt = GetSgptDT(); if (!dt) return;
    int cls = GetPlayerClass();
    if (cls < 0 && g_savedClass >= 0) cls = g_savedClass;
    if (cls < 0 || cls > 6) return;
    __try {
        int stride = *(int*)(dt + 0xBA8);
        short* list = *(short**)(dt + 0xBAC);
        int* counts = *(int**)(dt + 0xBA4);
        if (!list || !counts || stride <= 0) return;
        short* classStart = list + cls * stride;
        int curCount = counts[cls];

        /* Already in the list anywhere? */
        for (int i = 0; i < curCount; i++) {
            if (classStart[i] == (short)skillId) {
                return;
            }
        }

        /* Edge case: skill not in list at all. */
        if (curCount >= stride) {
            Log("InsertSkillInClassList: skill %d not in list, list full (count=%d stride=%d) — skipping\n",
                skillId, curCount, stride);
            return;
        }
        DWORD op;
        VirtualProtect(&classStart[curCount], sizeof(short), PAGE_READWRITE, &op);
        classStart[curCount] = (short)skillId;
        VirtualProtect(&classStart[curCount], sizeof(short), op, &op);
        VirtualProtect(&counts[cls], sizeof(int), PAGE_READWRITE, &op);
        counts[cls] = curCount + 1;
        VirtualProtect(&counts[cls], sizeof(int), op, &op);
        Log("InsertSkillInClassList: skill %d APPENDED at pos %d (was missing), count=%d\n",
            skillId, curCount, curCount + 1);
    } __except(1) { Log("InsertSkillInClassList CRASH\n"); }
}

/* ICON FIX — patch nIconCel for EVERY player skill, not just tree-placed ones. */
static void PatchAllIconCels(void) {
    int patched = 0;
    for (int i = 0; i < (int)SKILL_DB_COUNT; i++) {
        SetSkillIcon(g_skillDB[i].id);
        patched++;
    }
    Log("PatchAllIconCels: %d skill icon cels aligned to rebuilt DC6 order\n", patched);
}

/* MAIN: Apply all assigned skills to vanilla tree. */
static void ApplyAllSlots_Inner(BOOL forceRebuild) {
    if (!Player()) return;

    int cls = GetPlayerClass();
    if (cls < 0 || cls > 6) cls = 0;

    /* Icon fix (see PatchAllIconCels above) — runs on BOTH branches so item-granted skills get correct icons even when the tree itself doesn't need a rebuild. */
    PatchAllIconCels();

    if (forceRebuild) {
        /* Clear ALL 210 class skills from tree — we want an empty tree that only shows our assigned skills */
        for (int i = 0; i < (int)SKILL_DB_COUNT; i++) {
            ClearSkillFromTree(g_skillDB[i].id);
        }

        /* Count assigned skills */
        int totalAssigned = 0;
        for (int t = 0; t < 3; t++)
            for (int s = 0; s < 10; s++)
                if (g_tabSlots[t][s] >= 0) totalAssigned++;
        if (totalAssigned == 0) return; /* Tree is now empty, nothing to place */

        /* Place assigned skills */
        for (int t = 0; t < 3; t++) {
            int page = t + 1;
            for (int s = 0; s < 10; s++) {
                int pidx = g_tabSlots[t][s];
                if (pidx < 0) continue;
                int row = TREE_POS[cls][t][s][0];
                int col = TREE_POS[cls][t][s][1];
                PlaceSkillInTree(g_skillDB[g_pool[pidx].dbIndex].id, page, row, col);
            }
        }

        /* Set tier requirements */
        for (int t = 0; t < 3; t++)
            for (int s = 0; s < 10; s++) {
                int pidx = g_tabSlots[t][s];
                if (pidx < 0) continue;
                SetSkillTierReqs(g_skillDB[g_pool[pidx].dbIndex].id, cls, t, s);
            }

        WriteClassSkillList();
        g_slotsApplied = TRUE;
        g_slotsDirty = FALSE;
        Log("ApplyAllSlots: %d skills placed (full rebuild)\n", totalAssigned);
    } else {
        /* Lightweight verify: just re-patch charclass for assigned skills so the game doesn't reject them. */
        for (int t = 0; t < 3; t++)
            for (int s = 0; s < 10; s++) {
                int pidx = g_tabSlots[t][s];
                if (pidx < 0) continue;
                PatchSkillForPlayer(g_skillDB[g_pool[pidx].dbIndex].id);
            }
    }
}

/* Convenience: full rebuild (used by AssignSkill/RemoveSkill/OnCharacterLoad) */
static void ApplyAllSlots(void) {
    ApplyAllSlots_Inner(TRUE);
}

/* Send packet 0x3B to spend a skill point (game's own protocol) */
static void SendSpendSkillPacket(int skillId) {
    if (!fnClientSend) return;
    unsigned char pkt[3];
    pkt[0] = 0x3B;
    pkt[1] = (unsigned char)(skillId & 0xFF);
    pkt[2] = (unsigned char)((skillId >> 8) & 0xFF);
    __try {
        fnClientSend(0, pkt, 3);
        Log("Sent packet 0x3B: skill %d\n", skillId);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("SendSpendSkillPacket FAILED for skill %d\n", skillId);
    }
}

/* Re-invest skill points after reset. */
static void ReinvestSkillPoints(void) {
    /* Reinvest now handled in ProcessPendingGameTick (D2Game thread context) where server player is available. */
    return;
    if (!g_reinvestPending || !Player()) return;
    if (GetTickCount() < g_reinvestTime) return;
    if (!g_poolInitialized) return; /* Don't reinvest before pool is ready */

    /* Get SKILLS_AddSkill function pointer */
    typedef void* (__stdcall *AddSkill_t)(void* pUnit, int nSkillId);
    static AddSkill_t fnAddSkill = NULL;
    if (!fnAddSkill && hD2Common) {
        fnAddSkill = (AddSkill_t)GetProcAddress(hD2Common, (LPCSTR)10952);
    }

    /* Get server player from D2Game global gpGame (base+0x1157FC) */
    void* pSrv = NULL;
    if (hD2Game) {
        __try {
            DWORD* ppG = (DWORD*)((DWORD)hD2Game + 0x1157FC);
            if (*ppG) {
                DWORD pG = *ppG;
                DWORD pBuckets = *(DWORD*)(pG + 0x1120);
                if (pBuckets) {
                    for (int bi = 0; bi < 128; bi++) {
                        DWORD pU = ((DWORD*)pBuckets)[bi];
                        if (pU) { pSrv = (void*)pU; break; }
                    }
                }
            }
        } __except(1) {}
    }
    void* pCli = Player();

    Log("ReinvestSkillPoints: reinvesting %d skills (srv=%08X cli=%08X)\n",
        g_reinvestCount, (DWORD)pSrv, (DWORD)pCli);

    for (int i = 0; i < g_reinvestCount; i++) {
        int skillId = g_reinvestSkills[i];
        int pts = g_reinvestPoints[i];

        /* Ensure skill is patched for player's class */
        __try { PatchSkillForPlayer(skillId); } __except(1) {}
        __try { InsertSkillInClassList(skillId); } __except(1) {}

        /* Give N levels via SKILLS_AddSkill on both server + client */
        for (int p = 0; p < pts; p++) {
            if (fnAddSkill) {
                if (pSrv) { __try { fnAddSkill(pSrv, skillId); } __except(1) {} }
                if (pCli) { __try { fnAddSkill(pCli, skillId); } __except(1) {} }
            }
            /* Deduct 1 skill point from server */
            if (pSrv && fnAddStat) {
                __try { fnAddStat(pSrv, 5, -1, 0); } __except(1) {}
            }
        }
        Log("ReinvestSkillPoints: invested %d pts in skill %d\n", pts, skillId);
    }
    g_reinvestPending = FALSE;
    g_reinvestCount = 0;

    /* Delete reinvest file */
    char rdir[MAX_PATH], rpath[MAX_PATH];
    GetCharFileDir(rdir, MAX_PATH);
    sprintf(rpath, "%sd2arch_reinvest_%s.dat", rdir, g_charName);
    remove(rpath);
}

/* Timer for periodic ApplyAllSlots */
static DWORD g_lastApply = 0;
static DWORD g_applyCount = 0;

/* Forward declaration — defined after Player() */
static void RunCheckDetection(void);

static void PeriodicApply(void) {
    if (!g_poolInitialized) return;

    /* Patch all class-specific skill animations to Cast mode (once) */
    if (!g_animPatchApplied) PatchAllSkillAnimations();
    /* Resolve boss loot TC row indices by name (fallback = hardcoded) */
    if (!g_bossLootTCsResolved) ResolveBossLootTCs();

    DWORD now = GetTickCount();
    DWORD interval = (g_applyCount < 10) ? 500 : 3000;
    if (now - g_lastApply > interval) {
        g_lastApply = now;
        g_applyCount++;
        if (g_slotsDirty || !g_slotsApplied) {
            ApplyAllSlots_Inner(TRUE);
        } else {
            ApplyAllSlots_Inner(FALSE);
        }
        RunCheckDetection();
        ReinvestSkillPoints();
    }
}

/* QUEST DETECTION — Area, monster scanning, boss kills */
/* Forward declarations for save/notify functions */
static void SaveStateFile(void);
static void WriteChecksFile(void);
static void ShowNotify(const char* text);
/* AP forward declarations */
static BOOL LoadAPCharConfig(void);
static void WriteAPCommand(const char* action);
static void StartAPBridge(void);
static void IsolateAPCharacter(void);
static void RestoreAllCharacters(void);
static void LoadAPSettings(void);
static void SaveAPCharConfig(void);

static int GetCurrentArea(void) {
    void* p = Player(); if (!p) return 0;
    __try {
        DWORD pPath = *(DWORD*)((DWORD)p + 0x2C);
        if (!pPath) return 0;
        DWORD pRoom = *(DWORD*)(pPath + 0x1C);
        if (!pRoom) return 0;
        DWORD pDrlgRoom = *(DWORD*)(pRoom + 0x38);
        if (!pDrlgRoom) return 0;
        DWORD pLevel = *(DWORD*)(pDrlgRoom + 0x00);
        if (!pLevel) return 0;
        return *(int*)(pLevel + 0x04);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

/* Pending gold queue — set from our DLL, processed from D2Game context. */
static int g_pendingGold = 0;

/* Pending rewards — accumulated from quest completions, written to .d2s at save/exit */
static int g_pendingRewardGold = 0;
static int g_pendingRewardStatPts = 0;
static int g_pendingRewardSkillPts = 0;
/* Server-side pending rewards — consumed in game tick via fnAddStat on server player */
static volatile LONG g_serverPendingGold = 0;
static volatile LONG g_serverPendingStatPts = 0;
static volatile LONG g_serverPendingSkillPts = 0;

/* Producers run on the RENDER thread (PollAPUnlocks via DrawAll) while the consumer runs on the GAME thread — a plain `+=` is a non-atomic RMW whose store can resurrect a just-consumed total (double-grant) or be lost under the consumer's zero. */
static void PendAdd(volatile LONG* p, int v) {
    if (v) InterlockedExchangeAdd(p, (LONG)v);
}
static int PendTake(volatile LONG* p) {
    return (int)InterlockedExchange(p, 0);
}
/* overflow ledgers. NEWSKILLS (stat 5) is an 8-bit .d2s field (max 255) and STATPTS (stat 4) a 10-bit field (max 1023). The reinvest system pumps the character's whole skill-point total into NEWSKILLS at save time, which can exceed 255 (infiL's Hell-start skill wipe: 280 wrapped to 24). We now clamp the .d2s write to the field maximum and park the excess here; on the next load the ledger is added back to the live stat BEFORE reinvest drains it, so no point is lost and the field never wraps. Persisted per-char in the state file. */
static int g_skillPtsLedger = 0;
static int g_statPtsLedger  = 0;
/* 2.x seed-log (Phase 4) — pending EXPERIENCE to grant a character so it can catch up to its seed's progression baseline. */
static volatile LONG g_serverPendingXP = 0;

/* LEVEL-BASED rewards. XP and gold used to be a flat random amount from a min/max slider, rolled ONCE per location when the seed was built. That made every reward either absurd or pointless depending on when you found it: fariel opened a chest in Cold Plains, took a 10-million-XP filler, hit level 30, and walked to Act 3 without fighting a single monster. The ceiling could not fix it either — the slider maxed at 10,000,000 while level 60 -> 61 alone costs 11,009,646, so the same number that trivialises Act 1 is a rounding error in Hell. These two counters hold a number of GRANTS, not an amount. The amount is worked out when the grant is consumed on the game thread, from the character's level at that moment: one XP grant = exactly one level's worth of experience, one gold grant = a level-scaled pile of gold. A reward is therefore worth the same to a level-5 character as to a level-85 one. Deliberately NOT the same as g_serverPendingXP, which carries a raw amount and is still used by the seed-log catch-up (that one must stay raw — it banks a measured XP delta, not "a level"). */
static volatile LONG g_serverPendingXpLevels   = 0;
static volatile LONG g_serverPendingGoldLevels = 0;
static DWORD g_gameUpdateHookAddr = 0;
static BYTE  g_gameUpdateTrampoline[16] = {0};
static DWORD g_gameUpdateTrampolinePtr = 0;
static BOOL  g_gameUpdateHooked = FALSE;

/* Trap spawn infrastructure — uses D2Game SpawnSuperUnique function. */
typedef void* (__fastcall *SpawnSuperUnique_t)(void* pGame, void* pRoom, int nX, int nY, int nSuperUniqueId);
static SpawnSuperUnique_t fnSpawnSuperUnique = NULL;
static int g_superUniqueCount = 0;       /* from sgptDataTables + 0xADC */

/* SpawnMonster — spawns a regular monster by MonStats hcIdx. */
typedef void* (__fastcall *SpawnMonster_t)(void* pGame, void* pRoom, int nX, int nY, int nMonsterId, int nAnimMode, int a7, short nFlags);
static SpawnMonster_t fnSpawnMonster = NULL;
/* 1.8.0 cleanup: TREASURE_COW_MONID define extracted (was 704). */

/* pGame captured from D2Debugger's D2DebugGame export every game tick. */
static DWORD g_cachedPGame = 0;

/* pGame capture is now done in ProcessPendingGameTick (D2Game context) */

typedef int (__cdecl *D2DebugGame_t)(void* pGame);
