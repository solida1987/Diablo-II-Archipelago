        if (!p) return -1;
        return (int)*(BYTE*)((DWORD)p + 0x04);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* Get sgptDataTables pointer - via ordinal 10042 (D2MOO exports it) */
static DWORD g_sgptDT = 0;

static DWORD GetSgptDT(void) {
    if (g_sgptDT) return g_sgptDT;
    if (!hD2Common) return 0;

    /* Try ordinal 10042 first (D2MOO) - this is a pointer TO the struct pointer */
    DWORD* pDT = (DWORD*)GetProcAddress(hD2Common, (LPCSTR)10042);
    if (pDT) {
        __try {
            g_sgptDT = *pDT; /* Dereference: ordinal gives us &sgptDataTables, we need the value */
            Log("sgptDataTables via ordinal 10042: %08X (ptr at %08X)\n", g_sgptDT, (DWORD)pDT);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    /* Fallback: old hardcoded offset (original D2Common) */
    if (!g_sgptDT) {
        __try {
            g_sgptDT = *(DWORD*)((DWORD)hD2Common + DT_OFFSET);
            Log("sgptDataTables via offset 0x%X: %08X\n", DT_OFFSET, g_sgptDT);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    return g_sgptDT;
}

/* ================================================================
 * RUNTIME MONSTER SHUFFLE — swap appearance+abilities in memory
 * Stats (HP, damage, defense, resistances) stay at original level.
 * ================================================================ */
#define MON_RECORD_SIZE 0x1A8
#define MON_TBL_PTR    0xA78
#define MON_TBL_COUNT  0xA80
/* Offsets within D2MonStatsTxt for swappable fields */
#define MON_CODE       0x10  /* uint32 - graphics token */
#define MON_NAMESTR    0x06  /* uint16 - name string */
#define MON_SOUND      0x14  /* uint16 - sound */
#define MON_USOUND     0x16  /* uint16 - unique sound */
#define MON_STATSEX    0x18  /* uint16 - MonStats2 ref */
#define MON_AI         0x1E  /* uint16 - AI type */
#define MON_MINION     0x26  /* int16[2] - minion IDs */
#define MON_SKILLS     0x170 /* int16[8] - skill IDs */
#define MON_SKMODE     0x180 /* uint8[8] - skill modes */
#define MON_SKLVL      0x198 /* uint8[8] - skill levels */
#define MON_LEVEL      0xAA  /* uint16[3] - levels per difficulty */
#define MON_FLAGS      0x0C  /* uint32 - flags */
#define MON_ALIGN      0x4C  /* uint8 - alignment */

/* Stat offsets for preset-based shuffle (save/restore original stats) */
#define MON_MINHP      0xB0
#define MON_MAXHP      0xB6
#define MON_AC_DEF     0xBC
#define MON_EXP        0xD4
#define MON_A1MIND     0xDA
#define MON_A1MAXD     0xE0
#define MON_RESFI      0x150
#define MON_RESLI      0x156
#define MON_RESCO      0x15C
#define MON_RESPO      0x162

static BOOL g_monsterShuffleEnabled = FALSE;
static BOOL g_bossShuffleEnabled = FALSE;
/* 1.8.0: moved g_shopShuffleEnabled here from d2arch_ap.c so d2arch_save.c
 * (earlier in include order) can reference it when baking per-char state. */
static BOOL g_shopShuffleEnabled = FALSE;
static BOOL g_shuffleApplied = FALSE;

/* Backup of original cosmetic data for undo */
#define MAX_SHUFFLE_MON 900
typedef struct {
    DWORD code; WORD nameStr, sound, usound, statsEx, ai;
    short minion[2];
    short skills[8]; BYTE skmode[8]; BYTE sklvl[8];
} MonCosmeticBackup;
static MonCosmeticBackup g_monBackup[MAX_SHUFFLE_MON];
static int g_monBackupCount = 0;

/* 1.9.2 — Monster shuffle BAN list. Verified 2026-05-02 against
 * MonStats.txt + SuperUniques.txt for D2 1.10f. Source:
 * Research/CHECK_SOURCES_MONSTER_AUDIT_2026-05-02.md
 *
 * These rows MUST NEVER appear as either origIdx OR replIdx in a
 * SWAP entry. Categories covered:
 *   - Spawners (their AI generates more units): maggot families,
 *     fallen/fetish shamans, crow/sucker nests, sarcophagi,
 *     minionspawners, evilhuts, evilholes, cantors, baalhighpriest
 *   - Quest bosses: Andariel/Duriel/Mephisto/Diablo/Baal + clones,
 *     Council, Ancients, Cow King, Smith, Hephasto, Nihlathak,
 *     Council Members, baal subjects
 *   - Special-AI / terrain-bound: drowned (water-only), gloams
 *     (indoor), tentacles, traps, turrets, hydras, catapults,
 *     barricades
 *   - Player pet / summon AI rows: golems, valkyrie, sentries,
 *     druid totems/pets
 *   - NPCs / vendors / dummies / unused placeholder rows
 *
 * Pre-1.9.2 the safety lived in the data the shuffle presets were
 * generated from; this runtime check is belt-and-suspenders so the
 * presets can be regenerated more aggressively in the future without
 * risking quest softlocks. */
static const int g_shuffleBannedIdx[] = {
    /* Spawner pillars: Sand Maggot family */
    68, 69, 70, 71, 72,                         /* adults */
    180, 181, 182, 183, 184,                    /* babies */
    190, 191, 192, 193, 194,                    /* eggs */
    284, 285, 286, 287, 288,                    /* queens */
    679, 680, 681,                              /* extra hell-tier maggots */

    /* Spawner pillars: shamans + fetish shamans */
    58, 59, 60, 61, 62,                         /* fallenshaman1-5 */
    645, 646, 647,                              /* hell-tier fallenshaman */
    278, 279, 280, 281, 282,                    /* fetishshaman1-5 */
    662, 663, 664,                              /* hell-tier fetishshaman */

    /* Spawner pillars: nests / sarcophagi / minion spawners */
    206, 207, 208, 209,                         /* crownest 1-4 */
    334, 335, 336, 337,                         /* suckernest 1-4 */
    228,                                        /* sarcophagus (mummy generator) */
    484, 485, 486, 487, 488, 489, 490, 491,     /* minionspawner 1-8 */
    528,                                        /* evilhut */
    321, 322, 323, 324, 325,                    /* evilhole 1-5 (Den of Evil) */
    238, 239, 240, 241,                         /* cantor 1-4 */
    557,                                        /* baalhighpriest */
    673, 674,                                   /* hell-tier cantors */

    /* Quest bosses + scripted SU hosts */
    156, 211, 242, 243,                         /* Andariel/Duriel/Mephisto/Diablo */
    333,                                        /* diabloclone (Uber D) */
    256,                                        /* izual */
    250,                                        /* summoner */
    229,                                        /* radament */
    267,                                        /* bloodraven */
    365,                                        /* griswold */
    402,                                        /* smith */
    409,                                        /* hephasto */
    526,                                        /* nihlathakboss */
    540, 541, 542,                              /* ancientbarb 1-3 (Ancients) */
    543, 544, 559, 545, 570,                    /* baal forms + taunt + clone */
    562, 563, 564, 565, 566,                    /* baal tentacles */
    571, 572, 573,                              /* baal minions (throne adds) */
    345, 346, 347,                              /* councilmember 1-3 (Travincal) */
    391,                                        /* hellbovine (Cow Level + Cow King base) */
    537, 538, 539,                              /* ancient statues */
    366,                                        /* compellingorb (Anya quest) */
    704, 705, 706, 707, 708, 709,               /* 1.9.0 custom uber rows */
    558,                                        /* venomlord (Baal subject 5) */

    /* Special-terrain / pet AI / scripted-only rows */
    8,                                          /* zombie4 (Drowned Carcass) */
    118, 119, 120, 121,                         /* willowisp 1-4 (Gloams) */
    258, 259, 260,                              /* tentacle limbs */
    261, 262, 263,                              /* tentacle heads */
    273,                                        /* gargoyletrap */
    326, 327, 328, 329, 330,                    /* trap-* */
    354,                                        /* trap-melee */
    369,                                        /* trap-nova */
    348, 349, 350, 372,                         /* turrets / firetower */
    371,                                        /* lightningspire */
    432, 433, 434, 435, 524, 525,               /* barricades */
    497, 498, 499, 500,                         /* catapults */
    516, 517, 518, 519,                         /* catapult spotters */
    351, 352, 353,                              /* hydra (sorc skill object) */
    401,                                        /* mephistospirit */
    332,                                        /* invisospawner */
    153,                                        /* hellmeteor */

    /* Player pet / summon AI rows */
    289, 290, 291, 292,                         /* golems */
    293, 357,                                   /* familiar, valkyrie */
    363, 364,                                   /* necroskeleton, necromage */
    410, 411, 412, 413, 414, 415, 416, 417, 418, /* assassin sentries / shadow */
    419, 420, 421, 422, 423, 424,               /* druid pets/totems */
    425, 426, 427, 428,                         /* druid plant/bear */
    429, 430, 431,                              /* eagle/wolf/bear */

    /* NPCs (humans, vendors, hireable, cain variants, tyrael, etc.) */
    146, 147, 148, 150, 154, 155,               /* cain1, gheed, akara, kashya, charsi, warriv1 */
    175, 176, 177, 178,                         /* warriv2, atma, drognan, fara */
    195, 196, 197, 198, 199, 200,               /* act2 male/female/child + greiz/elzix/geglash */
    201, 202, 203, 204, 205,                    /* jerhyn, lysander, act2guard1, vendors */
    210,                                        /* meshif1 */
    244, 245, 246,                              /* cain2,3,4 */
    251, 252, 253, 254, 255,                    /* tyrael1, asheara, hratli, alkor, ormus */
    257,                                        /* halbu */
    264, 265, 266,                              /* meshif2, cain5, navi */
    270, 271, 272,                              /* rogue2, roguehire, rogue3 */
    294, 296, 297,                              /* act3 male/female, natalya */
    331,                                        /* act2guard2 (Kaelan) */
    338,                                        /* act2hire */
    358, 359,                                   /* act2guard3, act3hire */
    367, 368,                                   /* tyrael2, darkwanderer */
    405,                                        /* jamella */
    406, 407, 408,                              /* izualghost, fetish11, malachai */
    511, 512, 513, 514, 515,                    /* larzuk, drehya, malah, nihlathak (town), qual-kehk */
    520, 521,                                   /* cain6, tyrael3 */
    522, 523,                                   /* act5barb1, act5barb2 */
    527,                                        /* drehyaiced */
    534, 535, 536,                              /* act5pow, act5barb3, act5barb4 */
    560, 561,                                   /* act5hire1, act5hire2 */
    567, 568, 569,                              /* injuredbarb1-3 */

    /* Animal / decoration / dummy rows */
    149, 151, 152, 157, 158, 159,               /* chicken, rat, rogue1, bird1, bird2, bat */
    179,                                        /* cow */
    185,                                        /* camel */
    227,                                        /* maggot */
    268, 269,                                   /* bug, scorpion */
    283,                                        /* larva */
    318, 319, 320,                              /* snake, parrot, fish */
    339,                                        /* minispider */
    370,                                        /* spiritmummy */
    392, 393,                                   /* window1, window2 */
    340, 341, 342, 343, 344,                    /* boneprison/bonewall */
    355,                                        /* seventombs */
    356,                                        /* dopplezon */
    450, 451, 452,                              /* wolfrider1-3 (unused) */
    545,                                        /* baaltaunt */
    556,                                        /* bunny */
    574,                                        /* worldstoneeffect */

    /* Unused placeholder rows (BaseId = "unused") */
    34, 35, 36, 37,                             /* gorgon1-4 */
    106, 107, 108, 109,                         /* chaoshorde1-4 */
    217, 218, 219, 220, 221,                    /* darkguard1-5 */
    222, 223, 224, 225, 226,                    /* bloodmage1-5 */
    230, 231, 232, 233,                         /* firebeast/iceglobe/lightningbeast/poisonorb */
};
#define SHUFFLE_BANNED_COUNT \
    ((int)(sizeof(g_shuffleBannedIdx) / sizeof(g_shuffleBannedIdx[0])))

static BOOL IsShuffleBanned(int idx) {
    for (int i = 0; i < SHUFFLE_BANNED_COUNT; i++) {
        if (g_shuffleBannedIdx[i] == idx) return TRUE;
    }
    return FALSE;
}

static void ApplyMonsterShuffle(DWORD seed) {
    DWORD dt = GetSgptDT();
    if (!dt) { Log("SHUFFLE: no sgptDataTables\n"); return; }

    DWORD pMonArr = 0;
    int monCount = 0;
    __try {
        pMonArr = *(DWORD*)(dt + MON_TBL_PTR);
        monCount = *(int*)(dt + MON_TBL_COUNT);
    } __except(1) { return; }
    if (!pMonArr || monCount <= 0 || monCount > MAX_SHUFFLE_MON) return;

    /* Pick preset file: (seed % 20) + 1 -> shuffle_presets/shuffle_preset_XX.dat */
    int presetNum = (int)(seed % 20) + 1;
    char presetPath[MAX_PATH], archDir[MAX_PATH];
    GetArchDir(archDir, MAX_PATH);
    sprintf(presetPath, "%sshuffle_presets\\shuffle_preset_%02d.dat", archDir, presetNum);

    FILE* fp = fopen(presetPath, "r");
    if (!fp) {
        Log("SHUFFLE: preset file not found: %s\n", presetPath);
        return;
    }

    DWORD oldProt;
    VirtualProtect((void*)pMonArr, monCount * MON_RECORD_SIZE, PAGE_READWRITE, &oldProt);

    /* Backup original cosmetic data (for UndoMonsterShuffle) */
    g_monBackupCount = monCount;
    for (int i = 0; i < monCount; i++) {
        BYTE* rec = (BYTE*)(pMonArr + i * MON_RECORD_SIZE);
        g_monBackup[i].code = *(DWORD*)(rec + MON_CODE);
        g_monBackup[i].nameStr = *(WORD*)(rec + MON_NAMESTR);
        g_monBackup[i].sound = *(WORD*)(rec + MON_SOUND);
        g_monBackup[i].usound = *(WORD*)(rec + MON_USOUND);
        g_monBackup[i].statsEx = *(WORD*)(rec + MON_STATSEX);
        g_monBackup[i].ai = *(WORD*)(rec + MON_AI);
        memcpy(g_monBackup[i].minion, rec + MON_MINION, 4);
        memcpy(g_monBackup[i].skills, rec + MON_SKILLS, 16);
        memcpy(g_monBackup[i].skmode, rec + MON_SKMODE, 8);
        memcpy(g_monBackup[i].sklvl, rec + MON_SKLVL, 8);
    }

    /* Read preset file line by line.
     * Format: SWAP <origIdx> <replIdx> <code> <ai> | <stats...>
     * We only use origIdx and replIdx: memcpy replacement's record over original,
     * then restore the ORIGINAL monster's stats (HP, dmg, AC, level, exp, resists, align). */
    int swapCount = 0;
    int swapsSkippedByBan = 0;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "SWAP ", 5) != 0) continue;

        int origIdx = 0, replIdx = 0;
        if (sscanf(line + 5, "%d %d", &origIdx, &replIdx) < 2) continue;
        if (origIdx < 0 || origIdx >= monCount) continue;
        if (replIdx < 0 || replIdx >= monCount) continue;
        if (origIdx == replIdx) continue;

        /* 1.9.2 — runtime ban filter. Quest bosses, spawners, NPCs,
         * special-AI rows, pets and unused placeholders MUST never be
         * swapped (in either direction) or they break quests / spawn
         * scripted units in wrong zones / crash the game. */
        if (IsShuffleBanned(origIdx) || IsShuffleBanned(replIdx)) {
            swapsSkippedByBan++;
            continue;
        }

        BYTE* origRec = (BYTE*)(pMonArr + origIdx * MON_RECORD_SIZE);
        BYTE* replRec = (BYTE*)(pMonArr + replIdx * MON_RECORD_SIZE);

        /* Save original monster's stats before overwrite */
        WORD origLevel[3]; memcpy(origLevel, origRec + MON_LEVEL, 6);
        WORD origMinHP[3]; memcpy(origMinHP, origRec + MON_MINHP, 6);
        WORD origMaxHP[3]; memcpy(origMaxHP, origRec + MON_MAXHP, 6);
        WORD origAC[3];    memcpy(origAC,    origRec + MON_AC_DEF, 6);
        WORD origExp[3];   memcpy(origExp,   origRec + MON_EXP, 6);
        WORD origA1MinD[3]; memcpy(origA1MinD, origRec + MON_A1MIND, 6);
        WORD origA1MaxD[3]; memcpy(origA1MaxD, origRec + MON_A1MAXD, 6);
        WORD origResFi[3]; memcpy(origResFi, origRec + MON_RESFI, 6);
        WORD origResLi[3]; memcpy(origResLi, origRec + MON_RESLI, 6);
        WORD origResCo[3]; memcpy(origResCo, origRec + MON_RESCO, 6);
        WORD origResPo[3]; memcpy(origResPo, origRec + MON_RESPO, 6);
        DWORD origFlags = *(DWORD*)(origRec + MON_FLAGS);
        BYTE origAlign  = *(BYTE*)(origRec + MON_ALIGN);
        short origMinion[2]; memcpy(origMinion, origRec + MON_MINION, 4);

        /* Copy entire replacement record over original */
        memcpy(origRec, replRec, MON_RECORD_SIZE);

        /* Restore original monster's stats so difficulty stays the same */
        memcpy(origRec + MON_LEVEL, origLevel, 6);
        memcpy(origRec + MON_MINHP, origMinHP, 6);
        memcpy(origRec + MON_MAXHP, origMaxHP, 6);
        memcpy(origRec + MON_AC_DEF, origAC, 6);
        memcpy(origRec + MON_EXP, origExp, 6);
        memcpy(origRec + MON_A1MIND, origA1MinD, 6);
        memcpy(origRec + MON_A1MAXD, origA1MaxD, 6);
        memcpy(origRec + MON_RESFI, origResFi, 6);
        memcpy(origRec + MON_RESLI, origResLi, 6);
        memcpy(origRec + MON_RESCO, origResCo, 6);
        memcpy(origRec + MON_RESPO, origResPo, 6);
        *(DWORD*)(origRec + MON_FLAGS) = origFlags;
        *(BYTE*)(origRec + MON_ALIGN) = origAlign;
        memcpy(origRec + MON_MINION, origMinion, 4);

        swapCount++;
    }
    fclose(fp);

    VirtualProtect((void*)pMonArr, monCount * MON_RECORD_SIZE, oldProt, &oldProt);
    g_shuffleApplied = TRUE;
    Log("MONSTER SHUFFLE: preset %02d applied %d swaps (seed=%u, %d skipped by ban list of %d)\n",
        presetNum, swapCount, seed, swapsSkippedByBan, SHUFFLE_BANNED_COUNT);
}

static void UndoMonsterShuffle(void) {
    if (!g_shuffleApplied || g_monBackupCount == 0) return;
    DWORD dt = GetSgptDT();
    if (!dt) return;

    DWORD pMonArr = 0;
    __try { pMonArr = *(DWORD*)(dt + MON_TBL_PTR); } __except(1) { return; }
    if (!pMonArr) return;

    DWORD oldProt;
    VirtualProtect((void*)pMonArr, g_monBackupCount * MON_RECORD_SIZE, PAGE_READWRITE, &oldProt);

    for (int i = 0; i < g_monBackupCount; i++) {
        BYTE* rec = (BYTE*)(pMonArr + i * MON_RECORD_SIZE);
        *(DWORD*)(rec + MON_CODE) = g_monBackup[i].code;
        *(WORD*)(rec + MON_NAMESTR) = g_monBackup[i].nameStr;
        *(WORD*)(rec + MON_SOUND) = g_monBackup[i].sound;
        *(WORD*)(rec + MON_USOUND) = g_monBackup[i].usound;
        *(WORD*)(rec + MON_STATSEX) = g_monBackup[i].statsEx;
        *(WORD*)(rec + MON_AI) = g_monBackup[i].ai;
        memcpy(rec + MON_MINION, g_monBackup[i].minion, 4);
        memcpy(rec + MON_SKILLS, g_monBackup[i].skills, 16);
        memcpy(rec + MON_SKMODE, g_monBackup[i].skmode, 8);
        memcpy(rec + MON_SKLVL, g_monBackup[i].sklvl, 8);
    }

    VirtualProtect((void*)pMonArr, g_monBackupCount * MON_RECORD_SIZE, oldProt, &oldProt);
    g_shuffleApplied = FALSE;
    Log("MONSTER SHUFFLE: undone (%d monsters restored)\n", g_monBackupCount);
}

/* ================================================================
 * BOSS SHUFFLE — swap appearance of act end bosses only
 * Andariel(156), Duriel(211), Mephisto(242), Diablo(243), Baal(544)
 * ================================================================ */
static MonCosmeticBackup g_bossBackup[5];
static BOOL g_bossShuffleApplied = FALSE;

static void ApplyBossShuffle(DWORD seed) {
    DWORD dt = GetSgptDT();
    if (!dt) return;
    DWORD pMonArr = 0; int monCount = 0;
    __try { pMonArr = *(DWORD*)(dt + MON_TBL_PTR); monCount = *(int*)(dt + MON_TBL_COUNT); } __except(1) { return; }
    if (!pMonArr || monCount <= 0) return;

    static const int bossIds[5] = {156, 211, 242, 243, 544};
    /* Verify all boss IDs are in range */
    for (int i = 0; i < 5; i++) {
        if (bossIds[i] >= monCount) { Log("BOSS SHUFFLE: boss %d out of range\n", bossIds[i]); return; }
    }

    DWORD oldProt;
    VirtualProtect((void*)pMonArr, monCount * MON_RECORD_SIZE, PAGE_READWRITE, &oldProt);

    /* Backup boss cosmetics */
    for (int i = 0; i < 5; i++) {
        BYTE* rec = (BYTE*)(pMonArr + bossIds[i] * MON_RECORD_SIZE);
        g_bossBackup[i].code = *(DWORD*)(rec + MON_CODE);
        g_bossBackup[i].nameStr = *(WORD*)(rec + MON_NAMESTR);
        g_bossBackup[i].sound = *(WORD*)(rec + MON_SOUND);
        g_bossBackup[i].usound = *(WORD*)(rec + MON_USOUND);
        g_bossBackup[i].statsEx = *(WORD*)(rec + MON_STATSEX);
        g_bossBackup[i].ai = *(WORD*)(rec + MON_AI);
        memcpy(g_bossBackup[i].skills, rec + MON_SKILLS, 16);
        memcpy(g_bossBackup[i].skmode, rec + MON_SKMODE, 8);
        memcpy(g_bossBackup[i].sklvl, rec + MON_SKLVL, 8);
    }

    /* Fisher-Yates on 5 bosses */
    int order[5] = {0,1,2,3,4};
    srand(seed + 54321);
    for (int i = 4; i > 0; i--) { int j = rand() % (i+1); int t = order[i]; order[i] = order[j]; order[j] = t; }

    /* Apply: boss[i] gets look of boss[order[i]] */
    for (int i = 0; i < 5; i++) {
        int srcBoss = order[i];
        BYTE* dstRec = (BYTE*)(pMonArr + bossIds[i] * MON_RECORD_SIZE);
        MonCosmeticBackup* src = &g_bossBackup[srcBoss];
        *(DWORD*)(dstRec + MON_CODE) = src->code;
        *(WORD*)(dstRec + MON_NAMESTR) = src->nameStr;
        *(WORD*)(dstRec + MON_SOUND) = src->sound;
        *(WORD*)(dstRec + MON_USOUND) = src->usound;
        *(WORD*)(dstRec + MON_STATSEX) = src->statsEx;
        *(WORD*)(dstRec + MON_AI) = src->ai;
        memcpy(dstRec + MON_SKILLS, src->skills, 16);
        memcpy(dstRec + MON_SKMODE, src->skmode, 8);
        /* Scale skill levels to target boss level */
        WORD dstLvl = *(WORD*)(dstRec + MON_LEVEL);
        WORD srcLvl = *(WORD*)((BYTE*)(pMonArr + bossIds[srcBoss] * MON_RECORD_SIZE) + MON_LEVEL);
        if (srcLvl == 0) srcLvl = 1;
        for (int s = 0; s < 8; s++) {
            int sl = src->sklvl[s];
            if (sl > 0) { sl = (sl * dstLvl) / srcLvl; if (sl < 1) sl = 1; if (sl > 255) sl = 255; }
            dstRec[MON_SKLVL + s] = (BYTE)sl;
        }
    }

    VirtualProtect((void*)pMonArr, monCount * MON_RECORD_SIZE, oldProt, &oldProt);
    g_bossShuffleApplied = TRUE;
    Log("BOSS SHUFFLE: applied (seed=%u) A=%d D=%d M=%d Di=%d B=%d\n",
        seed, bossIds[order[0]], bossIds[order[1]], bossIds[order[2]], bossIds[order[3]], bossIds[order[4]]);
}

static void UndoBossShuffle(void) {
    if (!g_bossShuffleApplied) return;
    DWORD dt = GetSgptDT(); if (!dt) return;
    DWORD pMonArr = 0;
    __try { pMonArr = *(DWORD*)(dt + MON_TBL_PTR); } __except(1) { return; }
    if (!pMonArr) return;
    static const int bossIds[5] = {156, 211, 242, 243, 544};
    DWORD oldProt;
    VirtualProtect((void*)pMonArr, 600 * MON_RECORD_SIZE, PAGE_READWRITE, &oldProt);
    for (int i = 0; i < 5; i++) {
        BYTE* rec = (BYTE*)(pMonArr + bossIds[i] * MON_RECORD_SIZE);
        *(DWORD*)(rec + MON_CODE) = g_bossBackup[i].code;
        *(WORD*)(rec + MON_NAMESTR) = g_bossBackup[i].nameStr;
        *(WORD*)(rec + MON_SOUND) = g_bossBackup[i].sound;
        *(WORD*)(rec + MON_USOUND) = g_bossBackup[i].usound;
        *(WORD*)(rec + MON_STATSEX) = g_bossBackup[i].statsEx;
        *(WORD*)(rec + MON_AI) = g_bossBackup[i].ai;
        memcpy(rec + MON_SKILLS, g_bossBackup[i].skills, 16);
        memcpy(rec + MON_SKMODE, g_bossBackup[i].skmode, 8);
        memcpy(rec + MON_SKLVL, g_bossBackup[i].sklvl, 8);
    }
    VirtualProtect((void*)pMonArr, 600 * MON_RECORD_SIZE, oldProt, &oldProt);
    g_bossShuffleApplied = FALSE;
    Log("BOSS SHUFFLE: undone\n");
}
