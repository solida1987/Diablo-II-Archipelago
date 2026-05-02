
/* ================================================================
 * CROSS-CLASS ANIMATION FIX
 * Skills using S1/S2/S3/S4 animation modes only have animation files
 * for their native class. Other classes crash/go invisible.
 * This patches the nAnim field in memory to SC (Cast=12) for all
 * skills where the animation mode is class-specific (S1-S4).
 * Called once after sgptDataTables is available.
 * ================================================================ */
#define SKT_CHARCLASS   0x0C  /* int8_t — owning class (-1=generic) */
#define SKT_ANIM        0x10  /* uint8_t — animation mode */
#define SKT_ITYPEA      0x18  /* int16_t[3] — required weapon types for attack A */
#define SKT_ITYPEB      0x1E  /* int16_t[3] — required weapon types for attack B */
#define SKT_ETYPEA      0x24  /* int16_t[2] — excluded weapon types for attack A */
#define SKT_ETYPEB      0x28  /* int16_t[2] — excluded weapon types for attack B */
#define SKT_MAXLVL      0x12C /* uint16_t — skill max level cap (0 = D2 defaults to 20) */

static BOOL g_animPatchApplied = FALSE;

/* ================================================================
 * BOSS TC NAME RESOLVER (Fix 7)
 *
 * The old code used hardcoded row indices (667/826/685/691/718). Any mod that
 * rearranges TreasureClassEx.txt breaks those. This resolver looks up the row
 * index by NAME at boot, then patches g_bossLootTCs[]. On any failure the
 * hardcoded fallback values (the old defaults) remain.
 *
 * Offset notes:
 *   The D2MOO D2DataTablesStrc has a TreasureClassEx pointer + count pair.
 *   The exact offsets for our 1.10f build are not documented anywhere we
 *   trust, so we probe a conservative range and validate by checking that
 *   the count is sane (< 10000) and the first row's name field dereferences
 *   cleanly. If we cannot validate, we bail and keep the hardcoded values.
 * ================================================================ */

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
        int row = ResolveBossTCByName(g_bossLootNames[i]);
        if (row >= 0) {
            g_bossLootTCs[i] = row;
            resolved++;
            Log("BOSS TC resolved: %s -> row %d\n", g_bossLootNames[i], row);
        }
    }
    if (resolved == 0) {
        Log("BOSS TC: name resolve failed, using hardcoded fallback (Andariel=%d Duriel=%d Mephisto=%d Diablo=%d Baal=%d)\n",
            g_bossLootTCs[0], g_bossLootTCs[1], g_bossLootTCs[2], g_bossLootTCs[3], g_bossLootTCs[4]);
    } else if (resolved < BOSS_LOOT_COUNT) {
        Log("BOSS TC: %d/%d resolved, rest use hardcoded fallback\n", resolved, BOSS_LOOT_COUNT);
    }
}

/* Cross-class skills that use anim mode SQ (18) — Whirlwind, Double Swing, martial arts etc.
 * Native class keeps SQ (per-frame events drive Whirlwind ticks etc.); when cast by a
 * non-native class we substitute A1 (PLRMODE_ATTACK1, idx 7) — every class has A1 frames
 * AND A1 schedules per-frame attack events so the secondary hits / projectiles still
 * fire. Earlier (1.7.1) this was a blanket SQ -> SC (cast) rewrite which broke native
 * owners AND killed the per-frame event chain — Whirlwind animated as a static cast
 * pose and only the first hit fired. */
static const int SQ_SKILLS_TO_CONVERT[] = {
    133, /* Double Swing (bar) */
    140, /* Double Throw (bar) */
    143, /* Leap Attack (bar) */
    151, /* Whirlwind (bar) */
    257, /* Blade Sentinel (ass) */
    259, /* Fists of Fire (ass) */
    260, /* Dragon Claw (ass) */
    266, /* Blade Fury (ass) */
    269, /* Claws of Thunder (ass) */
    274, /* Blades of Ice (ass) */
    -1
};

/* Amazon javelin/throw skills with anim TH (11). Native Amazon keeps TH (proper throw
 * animation); cross-class casters get A1 (single melee swing) so they don't softlock
 * waiting for non-existent throw frames. */
static const int TH_SKILLS_TO_CONVERT[] = {
    10, /* Jab (ama) */
    15, /* Poison Javelin (ama) */
    20, /* Lightning Bolt (ama) */
    25, /* Plague Javelin (ama) */
    30, /* Power Strike (ama) */
    35, /* Lightning Fury (ama) */
    40, /* Charged Strike (ama) */
    45, /* Lightning Strike (ama) */
    -1
};

/* Assassin kick skills using anim KK (12). Native Assassin keeps KK; cross-class
 * fallback is A1 (single attack). */
static const int KK_SKILLS_TO_CONVERT[] = {
    255, /* Dragon Talon (ass) */
    -1
};

static BOOL SkillIdInList(int skillId, const int* list) {
    for (int i = 0; list[i] != -1; i++) {
        if (list[i] == skillId) return TRUE;
    }
    return FALSE;
}

/* 1.9.1 — vanilla-anim cache for the per-class repatch path.
 *
 * The 1.7.1 boot-time animation patch unconditionally rewrote class-specific anim
 * modes (S1-S4, SQ, TH, KK) to SC for ALL skills, even when the player IS the native
 * owner — which broke Smite (S1 -> SC, cast pose instead of shield bash), Whirlwind
 * (SQ -> SC, single hit + cast pose instead of spin), Rabies/Hunger (S3 -> SC, no
 * bite in werewolf form), Amazon javelins (TH -> SC, no throw motion), Dragon Talon
 * (KK -> SC, no kick), Leap Attack (SQ -> SC, character doesn't move + softlocks).
 *
 * The fix is two-staged:
 *   1. At boot (PatchAllSkillAnimations) we cache the vanilla anim + native charclass
 *      for every skill, then apply the cross-class A1 fallback so the global skill
 *      table is "safe for any class". A1 is preferred over SC because A1 schedules
 *      per-frame attack events (Whirlwind / sequence tick), while SC plays a static
 *      cast pose with no per-frame events.
 *   2. At OnCharacterLoad (Skilltree_OnCharacterLoadHook -> RestoreNativeAnimsForClass)
 *      we walk the cache: any skill whose native_class == player_class gets its
 *      vanilla anim restored; other skills keep the A1 fallback. Re-applies on every
 *      character switch.
 *
 * Trade-off: this is a SOLO-focused fix. In multiplayer (multiple players in one
 * game) the skill table would alternate between repatchings, with the last char
 * loaded winning. Out of scope for now; AP play is solo-by-design. */
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

        /* 1.9.1 — initialise the vanilla-anim cache once, before we touch the
         * skill table. We need this for the per-character anim restore path
         * that runs at OnCharacterLoad (RestoreNativeAnimsForClass). The cache
         * stores the ORIGINAL anim + ORIGINAL charclass for every skill row
         * so we can later either restore the vanilla anim (native owner) or
         * re-apply the cross-class A1 fallback (non-native player). */
        if (!g_animCacheReady) {
            memset(g_origAnim, 0xFF, sizeof(g_origAnim));
            memset(g_origNativeClass, -1, sizeof(g_origNativeClass));
            g_animCacheReady = TRUE;
        }

        int animPatched = 0, weaponPatched = 0, crossClassPatched = 0;
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

            /* FIX 1.8.0: Lift per-skill level cap from 20 -> 99 for player skills.
             * D2Common's SKILLS_AddSkill (#10952) reads wMaxLvl at offset 0x12C
             * and refuses to increment past it — if maxlvl is 0 (not set in
             * Skills.txt), D2 silently defaults to 20. Setting it to 99 means
             * you can now invest up to 99 base points. Item +skill bonuses
             * still add on top and are clamped at player max (also 99) by
             * SKILLS_GetSkillLevel, so "+3 to Fire Bolt" gear still works.
             * Monsters are unaffected (their skills bypass the cap check). */
            if (charClass >= 0 && charClass <= 6) {
                WORD* pMaxLvl = (WORD*)(rec + SKT_MAXLVL);
                if (*pMaxLvl < 99) {
                    *pMaxLvl = 99;
                    maxLvlPatched++;
                }
            }

            /* 1.9.1 — class-specific anim modes (S1-S4, SQ for the listed skills,
             * TH for amazon throws, KK for assassin kicks) get rewritten to A1
             * (PLRMODE_ATTACK1 = 7) instead of the older SC (PLRMODE_CAST = 10).
             *
             * Why A1 over SC:
             *   - A1 frames exist on every class (so no softlock cross-class)
             *   - A1 schedules per-frame attack events, so Whirlwind / Leap
             *     Attack / Double Throw / Dragon Claw / Blade Fury fire all
             *     their hits / projectiles, not just the first one
             *   - A1 also drives STATE_SKILL_MOVE clearance for Leap Attack,
             *     so the character actually moves and does NOT softlock
             *
             * This is the SAFE-FOR-EVERY-CLASS state. The native owner's
             * vanilla anim is restored at OnCharacterLoad in
             * RestoreNativeAnimsForClass(). */
            if (anim >= 13 && anim <= 16 && charClass >= 0 && charClass <= 6) {
                *(BYTE*)(rec + SKT_ANIM) = 7;
                animPatched++;
            }
            if (charClass >= 0 && charClass <= 6) {
                if (anim == 18 && SkillIdInList(i, SQ_SKILLS_TO_CONVERT)) {
                    *(BYTE*)(rec + SKT_ANIM) = 7;
                    crossClassPatched++;
                } else if (anim == 11 && SkillIdInList(i, TH_SKILLS_TO_CONVERT)) {
                    *(BYTE*)(rec + SKT_ANIM) = 7;
                    crossClassPatched++;
                } else if (anim == 12 && SkillIdInList(i, KK_SKILLS_TO_CONVERT)) {
                    *(BYTE*)(rec + SKT_ANIM) = 7;
                    crossClassPatched++;
                }
            }

            /* FIX 2: Remove weapon type restrictions for class-owned skills.
             * Zeroing itype/etype fields = no weapon restriction = works with anything. */
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
        Log("SKILL PATCH (boot, A1-fallback): %d S1-S4 anim, %d SQ/TH/KK cross-class, %d weapon restrictions removed, %d maxlvl 20->99\n",
            animPatched, crossClassPatched, weaponPatched, maxLvlPatched);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("SKILL PATCH: EXCEPTION during patching\n");
    }
}

/* 1.9.1 — restore native-class skill animations + re-apply cross-class A1 fallback
 * for non-native skills based on the player's class.
 *
 * Runs at OnCharacterLoad (called from d2arch_save.c via Skilltree_OnCharacterLoadHook).
 * Skips early if the same class is already in effect (idempotent re-call from successive
 * loads of the same character). Walks every cached skill row and writes back either the
 * vanilla anim (native owner) or the A1 fallback (non-native).
 *
 * Trade-off acknowledged: in multiplayer with mixed classes, the LAST character loaded
 * wins — the global skill table can only carry one anim per skill. AP play is solo by
 * design so this is acceptable. */
static void RestoreNativeAnimsForClass(int playerClass) {
    if (!g_animPatchApplied || !g_animCacheReady) {
        Log("ANIM REPATCH skipped: boot patch not applied yet (class=%d)\n", playerClass);
        return;
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
                /* Native owner — restore vanilla anim (Smite gets S1, Whirlwind
                 * gets SQ, Rabies gets S3, javelins get TH, Dragon Talon gets KK) */
                want = orig;
            } else {
                /* Non-native — re-apply the same A1 fallback as PatchAllSkillAnimations */
                want = orig;
                if (orig >= 13 && orig <= 16) {
                    want = 7;
                } else if (orig == 18 && SkillIdInList(i, SQ_SKILLS_TO_CONVERT)) {
                    want = 7;
                } else if (orig == 11 && SkillIdInList(i, TH_SKILLS_TO_CONVERT)) {
                    want = 7;
                } else if (orig == 12 && SkillIdInList(i, KK_SKILLS_TO_CONVERT)) {
                    want = 7;
                }
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

/* Public hook called from d2arch_save.c OnCharacterLoad. Reads the live player class
 * via GetPlayerClass() and re-applies the per-class anim configuration. */
extern int GetPlayerClass(void);
void Skilltree_OnCharacterLoadHook(void) {
    int pc = GetPlayerClass();
    if (pc < 0) {
        Log("Skilltree_OnCharacterLoadHook: GetPlayerClass returned %d, skipping repatch\n", pc);
        return;
    }
    RestoreNativeAnimsForClass(pc);
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

/* Original nIconCel values for AmSkillicon.DC6 (D2's vanilla right-click selector).
 * Different from g_iconMap which is for our ArchIcons DC6.
 * Loaded from orig_iconcel.dat: skillId=nIconCel */
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

/* Set nIconCel in SkillDescTxt to the ORIGINAL value for this skill.
 * This ensures D2's vanilla right-click selector shows the correct icon
 * from AmSkillicon.DC6 (which has different frame order than our ArchIcons). */
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

        /* Patch nIconCel to match our rebuilt Skillicon DC6 frame order.
         * All Skillicon DC6 files now use g_skillDB order (= g_iconMap order).
         * So nIconCel = g_iconMap[skillId] = the sequential dbIndex. */
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

        int row = TREE_POS[cls][tab][slot][0];
        WORD reqLvl = 1;
        if (row >= 3 && row <= 4) reqLvl = 10;
        else if (row >= 5) reqLvl = 20;

        VirtualProtect((void*)(rec + SKT_REQLEVEL), 2, PAGE_READWRITE, &op);
        *(WORD*)(rec + SKT_REQLEVEL) = reqLvl;
        VirtualProtect((void*)(rec + SKT_REQLEVEL), 2, op, &op);

        /* Find prerequisite: T2 needs T1, T3 needs T2 in same tab */
        short prereq = -1;
        int needTier = 0;
        if (row >= 3 && row <= 4) needTier = 1;
        else if (row >= 5) needTier = 2;

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

/* Ensure a skill exists in the class skill list — but DO NOT reorder.
 *
 * 1.8.2 BUGFIX (critical): the previous version of this function ALWAYS
 * shifted the invested skill to position 0, pushing all others down. That
 * felt harmless on the active session, but it silently corrupted the .d2s
 * skill section across reloads:
 *
 *   - .d2s stores 30 skill levels by **position** in the in-memory
 *     class-skill-list. Position 0 = byte 0, position 1 = byte 1, …
 *   - WriteClassSkillList (called at char load) lays the 30 custom slots
 *     out in g_tabSlots order: tab0/slot0 -> position 0, tab0/slot1 -> 1,
 *     and so on.
 *   - Old InsertSkillInClassList ran every + click + drag, reshuffling the
 *     list to "most-recently-invested-first". The next .d2s save then
 *     wrote skill bytes in that shuffled order.
 *   - On the next character load WriteClassSkillList re-built the list in
 *     g_tabSlots order again — but the .d2s bytes were laid out for the
 *     SHUFFLED order, so byte i now mapped to a *different* skill ID.
 *     Result: the points the user spent on (e.g.) Bash and Axe Mastery
 *     showed up on Sword Mastery and Mace Mastery after a relog, plus a
 *     bunch of other "extra" skills that were never invested.
 *
 * The fix is to keep the list order STABLE for the lifetime of the
 * character. WriteClassSkillList sets the order once at load; this
 * function is now a verify-only no-op when the skill is already present.
 * If for any reason a skill is NOT in the list (edge case — should not
 * happen because all 30 g_tabSlots entries are seeded by
 * WriteClassSkillList) we APPEND it at the end so .d2s mapping for
 * existing skills stays untouched. */
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

        /* Already in the list anywhere? Done — DO NOT reorder. */
        for (int i = 0; i < curCount; i++) {
            if (classStart[i] == (short)skillId) {
                return;
            }
        }

        /* Edge case: skill not in list at all. Append at the end so existing
         * skill positions (and therefore the .d2s skill-byte mapping) are
         * preserved. If the list is already at stride capacity, give up
         * rather than overflow — fnAddSkill will silently fail for skills
         * outside positions 0..29 anyway. */
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

/* MAIN: Apply all assigned skills to vanilla tree.
 * When forceRebuild is TRUE, clears all skills and re-places them.
 * When FALSE, only verifies charclass is correct (safe for periodic use). */
static void ApplyAllSlots_Inner(BOOL forceRebuild) {
    if (!Player()) return;

    int cls = GetPlayerClass();
    if (cls < 0 || cls > 6) cls = 0;

    if (forceRebuild) {
        /* Clear ALL 210 class skills from tree — we want an empty tree
         * that only shows our assigned skills */
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
        /* Lightweight verify: just re-patch charclass for assigned skills
         * so the game doesn't reject them. NO clear/rebuild cycle. */
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

/* Re-invest skill points after reset.
 * Called from PeriodicApply after a delay to ensure tree is patched. */
static void ReinvestSkillPoints(void) {
    /* Reinvest now handled in ProcessPendingGameTick (D2Game thread context)
     * where server player is available. This function is kept as no-op. */
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

/* ================================================================
 * QUEST DETECTION — Area, monster scanning, boss kills
 * ================================================================ */
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

/* Pending gold queue — set from our DLL, processed from D2Game context.
 * We hook GAME_UpdateClients (D2Game ordinal 10005) which runs every game tick.
 * From that hook, we call AddUnitStat in D2Game's thread context,
 * which properly triggers stat callbacks and persistence. */
static int g_pendingGold = 0;

/* Pending rewards — accumulated from quest completions, written to .d2s at save/exit */
static int g_pendingRewardGold = 0;
static int g_pendingRewardStatPts = 0;
static int g_pendingRewardSkillPts = 0;
/* Server-side pending rewards — consumed in game tick via fnAddStat on server player */
static volatile int g_serverPendingGold = 0;
static volatile int g_serverPendingStatPts = 0;
static volatile int g_serverPendingSkillPts = 0;
static DWORD g_gameUpdateHookAddr = 0;
static BYTE  g_gameUpdateTrampoline[16] = {0};
static DWORD g_gameUpdateTrampolinePtr = 0;
static BOOL  g_gameUpdateHooked = FALSE;

/* Trap spawn infrastructure — uses D2Game SpawnSuperUnique function.
 * pGame captured by hooking D2Debugger.dll's exported D2DebugGame(pGame) function.
 * D2Debugger calls D2DebugGame every game tick with valid pGame.
 * SpawnSuperUnique at D2Game + 0x6F690 (0x6FC6F690 - 0x6FC30000). */
typedef void* (__fastcall *SpawnSuperUnique_t)(void* pGame, void* pRoom, int nX, int nY, int nSuperUniqueId);
static SpawnSuperUnique_t fnSpawnSuperUnique = NULL;
static int g_superUniqueCount = 0;       /* from sgptDataTables + 0xADC */

/* SpawnMonster — spawns a regular monster by MonStats hcIdx.
 * D2Game + 0x39F10 (0x6FC69F10 - 0x6FC30000).
 * Unlike SpawnSuperUnique, can spawn same type multiple times. */
typedef void* (__fastcall *SpawnMonster_t)(void* pGame, void* pRoom, int nX, int nY, int nMonsterId, int nAnimMode, int a7, short nFlags);
static SpawnMonster_t fnSpawnMonster = NULL;
/* 1.8.0 cleanup: TREASURE_COW_MONID define extracted (was 704). Unused anywhere. */

/* pGame captured from D2Debugger's D2DebugGame export every game tick.
 * We use Microsoft Detours-style hooking: overwrite first bytes with JMP,
 * save original function pointer for calling via trampoline.
 * D2DebugGame: bool __cdecl D2DebugGame(D2GameStrc* pGame) */
static DWORD g_cachedPGame = 0;

/* pGame capture is now done in ProcessPendingGameTick (D2Game context) */

typedef int (__cdecl *D2DebugGame_t)(void* pGame);
