/* ================================================================
 * SKILL DATABASE - All 210 class skills
 * ================================================================ */
typedef struct {
    int id;
    const char* name;
    const char* classCode; /* ama,sor,nec,pal,bar,dru,ass */
    int tier; /* 1=T1(basic), 2=T2(mid), 3=T3(advanced) */
} SkillEntry;

/* Extra info loaded from Skills.txt at runtime */
typedef struct {
    int reqlevel;
    int manaCost;
    char elemType[8];  /* fire, ltng, cold, pois, mag, "" */
    int eMin, eMax;
} SkillExtraInfo;

static SkillExtraInfo g_skillExtra[400] = {0}; /* indexed by skill ID */
static BOOL g_skillExtraLoaded = FALSE;

static const char* FullClassName(const char* code) {
    if (!code) return "Unknown";
    if (strcmp(code, "ama") == 0) return "Amazon";
    if (strcmp(code, "sor") == 0) return "Sorceress";
    if (strcmp(code, "nec") == 0) return "Necromancer";
    if (strcmp(code, "pal") == 0) return "Paladin";
    if (strcmp(code, "bar") == 0) return "Barbarian";
    if (strcmp(code, "dru") == 0) return "Druid";
    if (strcmp(code, "ass") == 0) return "Assassin";
    return code;
}

static void LoadSkillExtraInfo(void) {
    if (g_skillExtraLoaded) return;
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* sl = strrchr(path, '\\');
    if (sl) {
        int remaining = MAX_PATH - (int)(sl - path) - 1;
        if (remaining > 28) strcpy(sl + 1, "data\\global\\excel\\Skills.txt");
        else return;
    }

    FILE* f = fopen(path, "r");
    if (!f) { Log("LoadSkillExtra: cannot open %s\n", path); return; }

    char line[4096];
    /* Read header to find column indices */
    if (!fgets(line, sizeof(line), f)) { fclose(f); return; }

    int colId = -1, colReqLevel = -1, colMana = -1, colEType = -1, colEMin = -1, colEMax = -1;
    {
        char* tok = strtok(line, "\t\r\n");
        int col = 0;
        while (tok) {
            if (strcmp(tok, "Id") == 0) colId = col;
            else if (strcmp(tok, "reqlevel") == 0) colReqLevel = col;
            else if (strcmp(tok, "mana") == 0) colMana = col;
            else if (strcmp(tok, "EType") == 0) colEType = col;
            else if (strcmp(tok, "EMin") == 0) colEMin = col;
            else if (strcmp(tok, "EMax") == 0) colEMax = col;
            tok = strtok(NULL, "\t\r\n");
            col++;
        }
    }

    if (colId < 0) { fclose(f); return; }

    /* Read data rows */
    int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        char* fields[300] = {0};
        int nf = 0;
        char* p = line;
        while (nf < 300) {
            fields[nf] = p;
            char* tab = strchr(p, '\t');
            if (tab) { *tab = 0; p = tab + 1; }
            else { char* nl = strchr(p, '\r'); if (nl) *nl = 0; nl = strchr(p, '\n'); if (nl) *nl = 0; nf++; break; }
            nf++;
        }

        int id = (colId < nf && fields[colId][0]) ? atoi(fields[colId]) : -1;
        if (id < 0 || id >= 400) continue;

        g_skillExtra[id].reqlevel = (colReqLevel >= 0 && colReqLevel < nf) ? atoi(fields[colReqLevel]) : 0;
        g_skillExtra[id].manaCost = (colMana >= 0 && colMana < nf) ? atoi(fields[colMana]) : 0;
        g_skillExtra[id].eMin = (colEMin >= 0 && colEMin < nf) ? atoi(fields[colEMin]) : 0;
        g_skillExtra[id].eMax = (colEMax >= 0 && colEMax < nf) ? atoi(fields[colEMax]) : 0;
        if (colEType >= 0 && colEType < nf && fields[colEType][0]) {
            strncpy(g_skillExtra[id].elemType, fields[colEType], 7);
        }
        loaded++;
    }
    fclose(f);
    g_skillExtraLoaded = TRUE;
    Log("LoadSkillExtra: loaded %d skills from Skills.txt\n", loaded);
}

/* Tier assignment: based on row position in original skill tree
 * Row 1-2 = T1 (skills 1-6 per class roughly)
 * Row 3-4 = T2 (skills 7-16 per class roughly)
 * Row 5-6 = T3 (skills 17-30 per class roughly)
 * Each class has 30 skills, ~10 per tier */

static const SkillEntry g_skillDB[] = {
    /* === AMAZON (ama) === */
    {6,"Magic Arrow","ama",1}, {7,"Fire Arrow","ama",1}, {8,"Inner Sight","ama",1},
    {9,"Critical Strike","ama",1}, {10,"Jab","ama",1}, {11,"Cold Arrow","ama",1},
    {12,"Multiple Shot","ama",1}, {13,"Dodge","ama",1}, {14,"Power Strike","ama",1},
    {15,"Poison Javelin","ama",1},
    {16,"Exploding Arrow","ama",2}, {17,"Slow Missiles","ama",2}, {18,"Avoid","ama",2},
    {19,"Impale","ama",2}, {20,"Lightning Bolt","ama",2}, {21,"Ice Arrow","ama",2},
    {22,"Guided Arrow","ama",2}, {23,"Penetrate","ama",2}, {24,"Charged Strike","ama",2},
    {25,"Plague Javelin","ama",2},
    {26,"Strafe","ama",3}, {27,"Immolation Arrow","ama",3}, {28,"Decoy","ama",3},
    {29,"Evade","ama",3}, {30,"Fend","ama",3}, {31,"Freezing Arrow","ama",3},
    {32,"Valkyrie","ama",3}, {33,"Pierce","ama",3}, {34,"Lightning Strike","ama",3},
    {35,"Lightning Fury","ama",3},

    /* === SORCERESS (sor) === */
    {36,"Fire Bolt","sor",1}, {37,"Warmth","sor",1}, {38,"Charged Bolt","sor",1},
    {39,"Ice Bolt","sor",1}, {40,"Frozen Armor","sor",1}, {41,"Inferno","sor",1},
    {42,"Static Field","sor",1}, {43,"Telekinesis","sor",1}, {44,"Frost Nova","sor",1},
    {45,"Ice Blast","sor",1},
    {46,"Blaze","sor",2}, {47,"Fire Ball","sor",2}, {48,"Nova","sor",2},
    {49,"Lightning","sor",2}, {50,"Shiver Armor","sor",2}, {51,"Fire Wall","sor",2},
    {52,"Enchant","sor",2}, {53,"Chain Lightning","sor",2}, {54,"Teleport","sor",2},
    {55,"Glacial Spike","sor",2},
    {56,"Meteor","sor",3}, {57,"Thunder Storm","sor",3}, {58,"Energy Shield","sor",3},
    {59,"Blizzard","sor",3}, {60,"Chilling Armor","sor",3}, {61,"Fire Mastery","sor",3},
    {62,"Hydra","sor",3}, {63,"Lightning Mastery","sor",3}, {64,"Frozen Orb","sor",3},
    {65,"Cold Mastery","sor",3},

    /* === NECROMANCER (nec) === */
    {66,"Amplify Damage","nec",1}, {67,"Teeth","nec",1}, {68,"Bone Armor","nec",1},
    {69,"Skeleton Mastery","nec",1}, {70,"Raise Skeleton","nec",1}, {71,"Dim Vision","nec",1},
    {72,"Weaken","nec",1}, {73,"Poison Dagger","nec",1}, {74,"Corpse Explosion","nec",1},
    {75,"Clay Golem","nec",1},
    {76,"Iron Maiden","nec",2}, {77,"Terror","nec",2}, {78,"Bone Wall","nec",2},
    {79,"Golem Mastery","nec",2}, {80,"Raise Skeletal Mage","nec",2}, {81,"Confuse","nec",2},
    {82,"Life Tap","nec",2}, {83,"Poison Explosion","nec",2}, {84,"Bone Spear","nec",2},
    {85,"Blood Golem","nec",2},
    {86,"Attract","nec",3}, {87,"Decrepify","nec",3}, {88,"Bone Prison","nec",3},
    {89,"Summon Resist","nec",3}, {90,"Iron Golem","nec",3}, {91,"Lower Resist","nec",3},
    {92,"Poison Nova","nec",3}, {93,"Bone Spirit","nec",3}, {94,"Fire Golem","nec",3},
    {95,"Revive","nec",3},

    /* === PALADIN (pal) === */
    {96,"Sacrifice","pal",1}, {97,"Smite","pal",1}, {98,"Might","pal",1},
    {99,"Prayer","pal",1}, {100,"Resist Fire","pal",1}, {101,"Holy Bolt","pal",1},
    {102,"Holy Fire","pal",1}, {103,"Thorns","pal",1}, {104,"Defiance","pal",1},
    {105,"Resist Cold","pal",1},
    {106,"Zeal","pal",2}, {107,"Charge","pal",2}, {108,"Blessed Aim","pal",2},
    {109,"Cleansing","pal",2}, {110,"Resist Lightning","pal",2}, {111,"Vengeance","pal",2},
    {112,"Blessed Hammer","pal",2}, {113,"Concentration","pal",2}, {114,"Holy Freeze","pal",2},
    {115,"Vigor","pal",2},
    {116,"Conversion","pal",3}, {117,"Holy Shield","pal",3}, {118,"Holy Shock","pal",3},
    {119,"Sanctuary","pal",3}, {120,"Meditation","pal",3}, {121,"Fist of the Heavens","pal",3},
    {122,"Fanaticism","pal",3}, {123,"Conviction","pal",3}, {124,"Redemption","pal",3},
    {125,"Salvation","pal",3},

    /* === BARBARIAN (bar) === */
    /* 1.8.2 — Spear Mastery moved from T1 to T2 to rebalance the barb's
     * 11+9+10 tier distribution to 10+10+10. InitClassOnlySkills clamps each
     * tier to 10 slots, so the 11th T1 entry was being silently dropped (the
     * empty cell in the editor panel reported by the user). Spear Mastery
     * fits naturally in T2 alongside the other intermediate skills. */
    {126,"Bash","bar",1}, {127,"Sword Mastery","bar",1}, {128,"Axe Mastery","bar",1},
    {129,"Mace Mastery","bar",1}, {130,"Howl","bar",1}, {131,"Find Potion","bar",1},
    {132,"Leap","bar",1}, {133,"Double Swing","bar",1}, {134,"Pole Arm Mastery","bar",1},
    {135,"Throwing Mastery","bar",1},
    {136,"Spear Mastery","bar",2}, {137,"Taunt","bar",2}, {138,"Shout","bar",2},
    {139,"Stun","bar",2}, {140,"Double Throw","bar",2}, {141,"Increased Stamina","bar",2},
    {142,"Find Item","bar",2}, {143,"Leap Attack","bar",2}, {144,"Concentrate","bar",2},
    {145,"Iron Skin","bar",2},
    {146,"Battle Cry","bar",3}, {147,"Frenzy","bar",3}, {148,"Increased Speed","bar",3},
    {149,"Battle Orders","bar",3}, {150,"Grim Ward","bar",3}, {151,"Whirlwind","bar",3},
    {152,"Berserk","bar",3}, {153,"Natural Resistance","bar",3}, {154,"War Cry","bar",3},
    {155,"Battle Command","bar",3},

    /* === DRUID (dru) === */
    {221,"Raven","dru",1}, {222,"Poison Creeper","dru",1}, {223,"Werewolf","dru",1},
    {224,"Lycanthropy","dru",1}, {225,"Firestorm","dru",1}, {226,"Oak Sage","dru",1},
    {227,"Summon Spirit Wolf","dru",1}, {228,"Werebear","dru",1}, {229,"Molten Boulder","dru",1},
    {230,"Arctic Blast","dru",1},
    {231,"Carrion Vine","dru",2}, {232,"Feral Rage","dru",2}, {233,"Maul","dru",2},
    {234,"Fissure","dru",2}, {235,"Cyclone Armor","dru",2}, {236,"Heart of Wolverine","dru",2},
    {237,"Summon Dire Wolf","dru",2}, {238,"Rabies","dru",2}, {239,"Fire Claws","dru",2},
    {240,"Twister","dru",2},
    {241,"Solar Creeper","dru",3}, {242,"Hunger","dru",3}, {243,"Shock Wave","dru",3},
    {244,"Volcano","dru",3}, {245,"Tornado","dru",3}, {246,"Spirit of Barbs","dru",3},
    {247,"Summon Grizzly","dru",3}, {248,"Fury","dru",3}, {249,"Armageddon","dru",3},
    {250,"Hurricane","dru",3},

    /* === ASSASSIN (ass) === */
    {251,"Fire Blast","ass",1}, {252,"Claw Mastery","ass",1}, {253,"Psychic Hammer","ass",1},
    {254,"Tiger Strike","ass",1}, {255,"Dragon Talon","ass",1}, {256,"Shock Web","ass",1},
    {257,"Blade Sentinel","ass",1}, {258,"Burst of Speed","ass",1}, {259,"Fists of Fire","ass",1},
    {260,"Dragon Claw","ass",1},
    {261,"Charged Bolt Sentry","ass",2}, {262,"Wake of Fire","ass",2},
    {263,"Weapon Block","ass",2}, {264,"Cloak of Shadows","ass",2}, {265,"Cobra Strike","ass",2},
    {266,"Blade Fury","ass",2}, {267,"Fade","ass",2}, {268,"Shadow Warrior","ass",2},
    {269,"Claws of Thunder","ass",2}, {270,"Dragon Tail","ass",2},
    {271,"Lightning Sentry","ass",3}, {272,"Wake of Inferno","ass",3},
    {273,"Mind Blast","ass",3}, {274,"Blades of Ice","ass",3}, {275,"Dragon Flight","ass",3},
    {276,"Death Sentry","ass",3}, {277,"Blade Shield","ass",3}, {278,"Venom","ass",3},
    {279,"Shadow Master","ass",3}, {280,"Phoenix Strike","ass",3},
};

#define SKILL_DB_COUNT (sizeof(g_skillDB) / sizeof(g_skillDB[0]))

/* Player's skill pool — ALL 210 skills, 6 start unlocked */
#define POOL_SIZE 210
#define NUM_STARTING 6

typedef struct {
    int dbIndex;     /* Index into g_skillDB */
    BOOL unlocked;   /* TRUE if available to assign */
    BOOL assigned;   /* TRUE if placed in a slot */
    int assignTab;   /* Tab 0-2 if assigned */
    int assignSlot;  /* Slot 0-9 if assigned */
} PoolSkill;

static PoolSkill g_pool[POOL_SIZE];
static int       g_poolCount = 0;
static DWORD     g_seed = 0;
static BOOL      g_poolInitialized = FALSE;

/* Tab/Slot assignments */
static int g_tabSlots[3][10]; /* skill DB index, -1 = empty */

/* Skill point tracking: how many points each skill had before reset.
 * After reset + reload, we re-spend these via packet 0x3B. */
static int  g_reinvestSkills[30]; /* skill IDs to reinvest */
static int  g_reinvestPoints[30]; /* points per skill */
static int  g_reinvestBtnIdx[30]; /* 1.8.2: original btnIdx (tab*10+slot) per
                                   * reinvest entry. Without this the consumer
                                   * was writing per-button level files using
                                   * the compact ri index, which corrupted the
                                   * panel display when skills with empty gaps
                                   * were reinvested. */
static int  g_reinvestCount = 0;  /* number of skills to reinvest */
static BOOL g_reinvestPending = FALSE;
static DWORD g_reinvestTime = 0;  /* when to start reinvesting */
static BOOL g_reinvestDone = FALSE; /* set TRUE after reinvest completes — triggers level reload */
static int  g_reinvestLevels[30] = {0}; /* levels loaded from files after reinvest */
static BOOL g_reinvestLevelsReady = FALSE; /* TRUE when g_reinvestLevels has fresh data */

/* Forward declaration - GetPlayerClass is defined later but needed here */
static int GetPlayerClass(void);

/* Assassin trap skills - "lay trap" animation only exists for Assassin class.
 * Non-Assassin characters become invisible and unable to act when using these.
 * In AP mode: removed from apworld item pool entirely.
 * In SP mode: only included if player is Assassin (class 6). */
static const int TRAP_SKILL_IDS[] = { 251, 256, 257, 261, 262, 266, 271, 272, 276, 277 };
#define TRAP_SKILL_COUNT 10

static BOOL IsAssassinTrapSkill(int skillId) {
    for (int i = 0; i < TRAP_SKILL_COUNT; i++)
        if (TRAP_SKILL_IDS[i] == skillId) return TRUE;
    return FALSE;
}

/* ================================================================
 * SKILL RANDOMIZER
 * ================================================================ */
static void InitSkillPool(DWORD seed) {
    int t1[70], t2[70], t3[70];
    int n1 = 0, n2 = 0, n3 = 0;

    /* All skills are now available for all classes.
     * Animation fix (PatchAllSkillAnimations) ensures cross-class casting works.
     * Assassin trap filter removed — no longer needed. */

    /* Categorize all skills by tier */
    for (int i = 0; i < (int)SKILL_DB_COUNT; i++) {
        /* Class filter: skip skills from disabled classes (standalone only) */
        if (!g_apMode && g_classFilter && !IsClassEnabled(g_skillDB[i].classCode))
            continue;
        switch (g_skillDB[i].tier) {
            case 1: t1[n1++] = i; break;
            case 2: t2[n2++] = i; break;
            case 3: t3[n3++] = i; break;
        }
    }

    /* Fisher-Yates shuffle each tier */
    srand(seed);
    for (int i = n1 - 1; i > 0; i--) { int j = rand() % (i + 1); int tmp = t1[i]; t1[i] = t1[j]; t1[j] = tmp; }
    for (int i = n2 - 1; i > 0; i--) { int j = rand() % (i + 1); int tmp = t2[i]; t2[i] = t2[j]; t2[j] = tmp; }
    for (int i = n3 - 1; i > 0; i--) { int j = rand() % (i + 1); int tmp = t3[i]; t3[i] = t3[j]; t3[j] = tmp; }

    /* Take ALL skills from each tier (210 total) — shuffled order determines unlock priority */
    g_poolCount = 0;
    for (int i = 0; i < n1; i++) {
        g_pool[g_poolCount].dbIndex = t1[i];
        g_pool[g_poolCount].unlocked = (g_apMode ? FALSE : (i < g_apStartingSkills)); /* AP: nothing pre-unlocked, SP: use setting */
        g_pool[g_poolCount].assigned = FALSE;
        g_pool[g_poolCount].assignTab = -1;
        g_pool[g_poolCount].assignSlot = -1;
        g_poolCount++;
    }
    for (int i = 0; i < n2; i++) {
        g_pool[g_poolCount].dbIndex = t2[i];
        g_pool[g_poolCount].unlocked = FALSE;
        g_pool[g_poolCount].assigned = FALSE;
        g_pool[g_poolCount].assignTab = -1;
        g_pool[g_poolCount].assignSlot = -1;
        g_poolCount++;
    }
    for (int i = 0; i < n3; i++) {
        g_pool[g_poolCount].dbIndex = t3[i];
        g_pool[g_poolCount].unlocked = FALSE;
        g_pool[g_poolCount].assigned = FALSE;
        g_pool[g_poolCount].assignTab = -1;
        g_pool[g_poolCount].assignSlot = -1;
        g_poolCount++;
    }

    /* Clear tab slots */
    for (int t = 0; t < 3; t++)
        for (int s = 0; s < 10; s++)
            g_tabSlots[t][s] = -1;

    g_seed = seed;
    g_poolInitialized = TRUE;
    Log("Skill pool initialized: seed=%u, %d skills (%d unlocked)\n", seed, g_poolCount, NUM_STARTING);
}

/* 1.9.2 — Accessors for d2arch_customgoal.c (which is included
 * earlier in the unity build than skills.c so it can't reach the
 * static globals directly). */
int Skills_GetPoolCount(void) { return g_poolCount; }
int Skills_GetUnlockedCount(void) {
    int n = 0;
    for (int i = 0; i < g_poolCount; i++) {
        if (g_pool[i].unlocked) n++;
    }
    return n;
}

/* 1.8.0: class id (from player unit dwClassId) -> skill DB class code. */
static const char* ClassIdToCode(int classId) {
    switch (classId) {
        case 0: return "ama";
        case 1: return "sor";
        case 2: return "nec";
        case 3: return "pal";
        case 4: return "bar";
        case 5: return "dru";
        case 6: return "ass";
        default: return NULL;
    }
}

/* 1.8.0: Skill Hunting = OFF path — no shuffle, no drag-drop, no cross-class.
 * Populate the pool with EXACTLY this class's 30 skills, all unlocked and
 * already assigned to their native tier slots:
 *   Tier 1 skills -> tab 0 slots 0..9
 *   Tier 2 skills -> tab 1 slots 0..9
 *   Tier 3 skills -> tab 2 slots 0..9
 * The editor's pool side becomes empty (no skills left to drag) and the
 * tree side shows the class's skills in their fixed positions — the
 * standard D2 class skill layout mirrored into our 30-slot grid. */
static void InitClassOnlySkills(int classId) {
    const char* code = ClassIdToCode(classId);
    if (!code) {
        Log("InitClassOnlySkills: unknown classId=%d — falling back to InitSkillPool\n", classId);
        InitSkillPool(GetTickCount());
        return;
    }

    int t1Idx[10], t2Idx[10], t3Idx[10];
    int n1 = 0, n2 = 0, n3 = 0;

    for (int i = 0; i < (int)SKILL_DB_COUNT; i++) {
        if (strcmp(g_skillDB[i].classCode, code) != 0) continue;
        switch (g_skillDB[i].tier) {
            case 1: if (n1 < 10) t1Idx[n1++] = i; break;
            case 2: if (n2 < 10) t2Idx[n2++] = i; break;
            case 3: if (n3 < 10) t3Idx[n3++] = i; break;
        }
    }

    g_poolCount = 0;
    for (int t = 0; t < 3; t++)
        for (int s = 0; s < 10; s++)
            g_tabSlots[t][s] = -1;

    for (int i = 0; i < n1; i++) {
        g_pool[g_poolCount].dbIndex   = t1Idx[i];
        g_pool[g_poolCount].unlocked  = TRUE;
        g_pool[g_poolCount].assigned  = TRUE;
        g_pool[g_poolCount].assignTab = 0;
        g_pool[g_poolCount].assignSlot = i;
        g_tabSlots[0][i] = g_poolCount;
        g_poolCount++;
    }
    for (int i = 0; i < n2; i++) {
        g_pool[g_poolCount].dbIndex   = t2Idx[i];
        g_pool[g_poolCount].unlocked  = TRUE;
        g_pool[g_poolCount].assigned  = TRUE;
        g_pool[g_poolCount].assignTab = 1;
        g_pool[g_poolCount].assignSlot = i;
        g_tabSlots[1][i] = g_poolCount;
        g_poolCount++;
    }
    for (int i = 0; i < n3; i++) {
        g_pool[g_poolCount].dbIndex   = t3Idx[i];
        g_pool[g_poolCount].unlocked  = TRUE;
        g_pool[g_poolCount].assigned  = TRUE;
        g_pool[g_poolCount].assignTab = 2;
        g_pool[g_poolCount].assignSlot = i;
        g_tabSlots[2][i] = g_poolCount;
        g_poolCount++;
    }

    g_seed = 0;
    g_poolInitialized = TRUE;
    Log("InitClassOnlySkills: class=%d (%s), %d skills (T1=%d T2=%d T3=%d), all assigned\n",
        classId, code, g_poolCount, n1, n2, n3);
}
