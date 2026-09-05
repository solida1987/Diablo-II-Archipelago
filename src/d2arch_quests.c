
/* QUEST SYSTEM — Data structures and 108 quests */
typedef enum { QTYPE_BOSS, QTYPE_AREA, QTYPE_KILL, QTYPE_WAYPOINT, QTYPE_QUESTFLAG, QTYPE_SUPERUNIQUE, QTYPE_LEVEL } QuestType;
/* 1.9.0 reward redesign: every filler-quest reward is now pre-rolled at character creation. */
typedef enum {
    REWARD_GOLD,           /* g_questGold = 1..10000 */
    REWARD_SKILL,          /* skill point (or skill unlock for prog quests) */
    REWARD_STAT,           /* +5 stat points */
    REWARD_TRAP,           /* g_questExtra = 0..3 trap type */
    REWARD_RESETPT,        /* +1 reset point */
    REWARD_LOOT,           /* g_questExtra = 0..4 boss id (Andariel..Baal) */
    REWARD_XP,             /* g_questXP = 1..250000 */
    REWARD_DROP_CHARM,     /* g_questExtra = 0..2 (small/large/grand charm) */
    REWARD_DROP_SET,       /* g_questExtra = 0..126 set-piece catalog index */
    REWARD_DROP_UNIQUE,    /* g_questExtra = uniqueitems.txt row index */
    REWARD_DROP_BATCH,     /* g_questExtra = index into g_fillerBatches */
} RewardType;

/* Filler batches: a named bundle of ordinary item codes.
 *
 * Every code below was read out of the game's own misc.txt / weapons.txt, not
 * written from memory — an invented code produces an item that silently fails
 * to spawn, which looks exactly like a lost reward.
 *
 * All of these spawn at NORMAL quality, which is the quality fnCI is reliable
 * for. The one exception is the jewel, which wants to roll magic and therefore
 * carries a fallback down to normal (see the drop processor). */
#define FILLER_BATCH_CODES_MAX 12
typedef struct {
    const char* name;          /* what the player is told they received */
    const char* codes[FILLER_BATCH_CODES_MAX];  /* NULL-terminated 3-letter item codes */
    BYTE        qty[FILLER_BATCH_CODES_MAX];    /* how many of each */
    BYTE        quality;       /* 2 = normal, 4 = magic */
    BOOL        pickOne;       /* TRUE: choose ONE code at random, not all */
} FillerBatch;

static const FillerBatch g_fillerBatches[] = {
    /* 0 */ { "a rune",              {"r01","r02","r03","r04","r05","r06","r07","r08","r09",NULL},
                                     {1,1,1,1,1,1,1,1,1,0}, 2, TRUE },
    /* 1 */ { "a gem",               {"gcv","gfv","gsv","gcy","gfy","gsy","gcb","gfb","gsb",NULL},
                                     {1,1,1,1,1,1,1,1,1,0}, 2, TRUE },
    /* 2 */ { "rejuvenation potions",{"rvs",NULL}, {3,0}, 2, FALSE },
    /* 3 */ { "full rejuvenation",   {"rvl",NULL}, {2,0}, 2, FALSE },
    /* 4 */ { "greater healing",     {"hp5",NULL}, {4,0}, 2, FALSE },
    /* 5 */ { "greater mana",        {"mp5",NULL}, {4,0}, 2, FALSE },
    /* 6 */ { "throwing potions",    {"opl","opm","ops","gpl","gpm","gps",NULL},
                                     {3,3,3,3,3,3,0}, 2, TRUE },
    /* 7 */ { "a jewel",             {"jew",NULL}, {1,0}, 4, FALSE },
    /* 8 */ { "a tome of town portal",{"tbk",NULL}, {1,0}, 2, FALSE },
    /* 9 */ { "a tome of identify",  {"ibk",NULL}, {1,0}, 2, FALSE },
    /*10 */ { "skeleton keys",       {"key",NULL}, {6,0}, 2, FALSE },
    /*11 */ { "utility potions",     {"vps","yps","wms",NULL}, {2,2,2,0}, 2, FALSE },

    /* Tiered variants. Batch 0 stays on r01-r09 on purpose: a flat roll over
     * all 33 runes makes Zod exactly as likely as El. The two bands below are
     * what the apworld's tier placement pushes into the later spheres, so the
     * rarity comes from WHERE the item is placed rather than from a weighted
     * roll the player can never see.
     *
     * The high band stops at Ohm. Lo through Zod are the runewords that end a
     * character's gearing, and handing one out as filler is a different design
     * decision than "high runes exist" — worth making deliberately, not by
     * letting a range run to the end of the table. */
    /*12 */ { "a mid rune",          {"r10","r11","r12","r13","r14","r15","r16","r17","r18","r19","r20",NULL},
                                     {1,1,1,1,1,1,1,1,1,1,1,0}, 2, TRUE },
    /*13 */ { "a high rune",         {"r21","r22","r23","r24","r25","r26","r27",NULL},
                                     {1,1,1,1,1,1,1,0}, 2, TRUE },
    /* Amethyst breaks the pattern: its flawless code is gzv, not glv. */
    /*14 */ { "a flawless gem",      {"gzv","gly","glb","glg","glr","glw","skl",NULL},
                                     {1,1,1,1,1,1,1,0}, 2, TRUE },
    /*15 */ { "a perfect gem",       {"gpv","gpy","gpb","gpg","gpr","gpw","skz",NULL},
                                     {1,1,1,1,1,1,1,0}, 2, TRUE },
    /* Charms want magic quality — a normal charm has no affixes and is junk. */
    /*16 */ { "a small charm",       {"cm1",NULL}, {1,0}, 4, FALSE },
    /*17 */ { "a large charm",       {"cm2",NULL}, {1,0}, 4, FALSE },
    /*18 */ { "a grand charm",       {"cm3",NULL}, {1,0}, 4, FALSE },
};
#define FILLER_BATCH_COUNT ((int)(sizeof(g_fillerBatches)/sizeof(g_fillerBatches[0])))

/* Reset Points — earned from filler quests, spent to remove skills from slots */
static int g_resetPoints = 0;

typedef struct {
    int         id;
    const char* name;
    const char* desc;
    QuestType   type;
    int         param;      /* boss txtId, area id */
    int         killReq;    /* kills required (QTYPE_KILL only) */
    int         killCount;  /* current kill count (runtime, per difficulty) */
    BOOL        completed;  /* runtime, per difficulty */
    RewardType  reward;
    int         goldAmount; /* random gold reward (100-10000) */
} Quest;

/* Difficulty: 0=Normal, 1=Nightmare, 2=Hell */
static int g_currentDifficulty = 0;
static int g_questLogDifficulty = 0; /* which difficulty the quest log shows */
static const char* g_diffNames[] = {"Normal", "Nightmare", "Hell"};

/* Per-difficulty quest completion state. */
#define MAX_QUEST_ID 800
#define LOCATION_BASE 42000
static BOOL g_questCompleted[3][MAX_QUEST_ID]; /* [difficulty][questId] */
static void* g_questScanQF = NULL; /* D2Client's quest record pointer (DAT_6fbb5d13) */

/* XP Multiplier (from INI XPMultiplier setting) */
static int g_xpMultiplier = 0;   /* 0-based: 0 = standard XP, each +1 = +100% */

/* 2.x — configurable GOLD / XP reward amounts (Marco). */
static int g_goldRewardMin = 100;
static int g_goldRewardMax = 10000;
static int g_xpRewardMin   = 100;
static int g_xpRewardMax   = 250000;

static int RewardRollRange(int lo, int hi) {
    DWORD roll;
    if (lo < 1)  lo = 1;
    if (hi < lo) hi = lo;
    /* rand() is 15-bit (0..32767) — a single call caps every roll at lo+32767, so a 400k..1M XP range always rolled ≈400-433k. */
    roll = ((DWORD)rand() << 15) | (DWORD)rand();
    return lo + (int)(roll % (DWORD)(hi - lo + 1));
}
static int RewardRollGold(void) { return RewardRollRange(g_goldRewardMin, g_goldRewardMax); }
static int RewardRollXp(void)   { return RewardRollRange(g_xpRewardMin,   g_xpRewardMax); }

/* LEVEL-SCALED REWARD AMOUNTS g_expThreshold[L] = total experience a character must hold to BE level L. */
#define EXP_MAX_LEVEL 99
static unsigned int g_expThreshold[EXP_MAX_LEVEL + 2] = {0};
static BOOL g_expTableLoaded = FALSE;

static void LoadExpTable(void) {
    if (g_expTableLoaded) return;
    g_expTableLoaded = TRUE;   /* one attempt; a missing file falls back below */

    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* sl = strrchr(path, '\\');
    if (!sl) return;
    int remaining = MAX_PATH - (int)(sl - path) - 1;
    if (remaining <= 32) return;
    strcpy(sl + 1, "data\\global\\excel\\experience.txt");

    FILE* f = fopen(path, "r");
    if (!f) { Log("LoadExpTable: cannot open %s\n", path); return; }

    char line[2048];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return; }

    int colLevel = -1, colClass = -1;
    {
        char* tok = strtok(line, "\t\r\n");
        int col = 0;
        while (tok) {
            if (strcmp(tok, "Level") == 0 && colLevel < 0) colLevel = col;
            else if (strcmp(tok, "Amazon") == 0 && colClass < 0) colClass = col;
            tok = strtok(NULL, "\t\r\n");
            col++;
        }
    }
    if (colLevel < 0 || colClass < 0) { fclose(f); return; }

    int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        char* fields[32] = {0};
        int nf = 0;
        char* p = line;
        while (nf < 32) {
            fields[nf] = p;
            char* tab = strchr(p, '\t');
            if (tab) { *tab = 0; p = tab + 1; }
            else {
                char* nl = strchr(p, '\r'); if (nl) *nl = 0;
                nl = strchr(p, '\n');       if (nl) *nl = 0;
                nf++; break;
            }
            nf++;
        }
        if (colLevel >= nf || colClass >= nf) continue;
        /* The "MaxLvl" row is not a level; skip anything non-numeric. */
        if (fields[colLevel][0] < '0' || fields[colLevel][0] > '9') continue;
        int lvl = atoi(fields[colLevel]);
        if (lvl < 0 || lvl > EXP_MAX_LEVEL) continue;
        /* Row N holds the threshold for level N+1. */
        g_expThreshold[lvl + 1] = (unsigned int)strtoul(fields[colClass], NULL, 10);
        loaded++;
    }
    fclose(f);
    Log("LoadExpTable: %d rows from experience.txt (lvl2=%u, lvl99=%u)\n",
        loaded, g_expThreshold[2], g_expThreshold[EXP_MAX_LEVEL]);
}

/* Experience needed to carry a level-<lvl> character to level <lvl>+1. */
static int ExpForNextLevel(int lvl) {
    LoadExpTable();
    if (lvl < 1) lvl = 1;
    if (lvl >= EXP_MAX_LEVEL) return 0;
    unsigned int here = g_expThreshold[lvl];
    unsigned int next = g_expThreshold[lvl + 1];
    if (next <= here) {
        /* experience.txt missing or malformed — a rough vanilla-shaped curve so rewards degrade instead of silently becoming nothing. */
        return 500 * lvl * lvl;
    }
    return (int)(next - here);
}

/* Gold scaled to character level: level² × 100. */
static int GoldForLevel(int lvl) {
    if (lvl < 1)  lvl = 1;
    if (lvl > EXP_MAX_LEVEL) lvl = EXP_MAX_LEVEL;
    return lvl * lvl * 100;
}

/* 2.x — XP granted as a flat REWARD (quest / bonus-check / AP XP reward) that the XP multiplier must NOT multiply. */
static volatile int g_rewardXpExclude = 0;

/* Keep the loaded ranges sane: 1 ≤ min ≤ max ≤ 1,000,000. */
static void RewardClampRanges(void) {
    if (g_goldRewardMin < 1) g_goldRewardMin = 1;
    if (g_goldRewardMax > 1000000) g_goldRewardMax = 1000000;
    if (g_goldRewardMax < g_goldRewardMin) g_goldRewardMax = g_goldRewardMin;
    if (g_xpRewardMin < 1) g_xpRewardMin = 1;
    /* XP ceiling raised 1M -> 10M (Marco / player request). */
    if (g_xpRewardMax > 10000000) g_xpRewardMax = 10000000;
    if (g_xpRewardMax < g_xpRewardMin) g_xpRewardMax = g_xpRewardMin;
}

/* Hidden cheat menu (Ctrl+V) — for testing only */
static BOOL g_cheatMenuOpen = FALSE;
static volatile int g_cheatGold = 0;
static volatile int g_cheatStatPts = 0;
static volatile int g_cheatSkillPts = 0;
static volatile int g_cheatLevel = 0;
/* Debug shortcut for building a test character quickly — enough to carry a
 * fresh character most of the way to 99 in one press. Capped at level 99's
 * total when it lands, so it cannot overflow the experience stat. */
#define CHEAT_BIG_XP 100000000u
static volatile int g_cheatXpBig = 0;
static volatile int g_cheatSpawnTrapSU = 0;   /* trigger SuperUnique trap spawn */
static volatile int g_cheatSpawnTrapMon = 0;  /* trigger regular monster spawn */
static volatile int g_cheatDropGold = 0;      /* drop gold pile on ground */
static volatile int g_cheatDropLoot = 0;      /* drop boss loot table (debug) */
static volatile int g_cheatDropBossId = 0;    /* which boss TC to drop (debug) */
static volatile int g_cheatTestRunewords = 0; /* drop everything (legacy) */
static volatile int g_cheatTestRunes = 0;     /* drop Cube + all 33 runes only */
static volatile int g_cheatTestBases = 0;     /* drop armor/helm/weapon bases + cube pots */
static volatile int g_cheatItemCmd = 0;       /* unified item-drop dispatch (1.9.0) */
static volatile int g_cheatDeliveryTest = 0;  /* 2.9.9 dev button: run the item-delivery stress test */
static volatile int g_cheatHealFull = 0;      /* refill HP+MP+Stamina */
static volatile int g_cheatSpawnUber = 0;     /* Pandemonium uber spawn (1.9.0): 1=Lilith, 2=Uber Duriel, 3=Uber Izual, 4=Uber Mephisto, 5=Uber Diablo, 6=Uber Baal, 7=mini uber trio, 8=final uber trio */
static volatile int g_cheatTeleport = 0;      /* Cheat teleport: pending area ID 0 = none, >0 = warp to that area */
static volatile int g_pendingPortalLevel = 0; /* Cheat physical portal spawn (1.9.0): 0 = none. */
static volatile int g_pendingPortalObjId = 60;/* Object class for the portal: 59=blue Town Portal, 60=red permanent portal (Cow/Trist), 100=Duriel Lair portal */
/* g_cheatItemCmd values: 1=Cube only 2=Low Runes 1-10 3=Mid Runes 11-20 4=High Runes 21-33 5=All 33 Runes 6=Body bases 7=Helm bases 8=Weapon bases 9=All bases 10=All Gems (35) 11=Healing pots 12=Mana pots 13=Recipe pots (vps+wms) */

/* Individual-item dispatch for the Loot tab in the Ctrl+V menu. */
static volatile int  g_cheatSpecificSetIdx    = -1;  /* 0..126, -1 = idle */
static volatile int  g_cheatSpecificUniqueIdx = -1;  /* 0..g_uniqueCatalogCount-1, -1 = idle */
static volatile int  g_cheatSpawnGateBoss = 0;       /* C7 v2 test: 1 = spawn next gate row (cycles the 44 pairs), 2 = reset cycle */
static volatile char g_cheatSingleItemCode[8] = "";  /* 3-char code + nul, "" = idle */
static volatile BYTE g_cheatSingleItemQuality = 2;   /* 2=normal, 4=magic, 5=set, 7=unique */
static volatile int  g_cheatSingleItemLvl     = 50;  /* spawn level (use 99 for cm2 unique torch) */

/* Specific-monster spawn dispatch for the new Mons tab in the Ctrl+V menu. */
static volatile int g_cheatSpawnSuperUniqueIdx = -1;
static volatile int g_cheatSpawnMonsterRowId   = -1;
/* Treasure Cow test spawn. */
static volatile int g_cheatSpawnTreasureCow    = 0;

/* Boss loot table TC IDs. */
/* corrected fallback values. */
/* point "Awesome Loot" at the HELL boss TCs, not Normal. */
#define BOSS_TC_ANDARIEL  671
#define BOSS_TC_DURIEL    677
#define BOSS_TC_MEPHISTO  689
#define BOSS_TC_DIABLO    695
#define BOSS_TC_BAAL      722
static int g_bossLootTCs[] = { BOSS_TC_ANDARIEL, BOSS_TC_DURIEL, BOSS_TC_MEPHISTO, BOSS_TC_DIABLO, BOSS_TC_BAAL };
/* Display names for the in-game "X Loot incoming!" notification. */
static const char* g_bossLootNames[] = { "Andariel", "Duriel", "Mephisto", "Diablo", "Baal" };
/* Exact TreasureClassEx names the boot resolver looks up (the Hell TCs). */
static const char* g_bossLootTCNames[] = { "Andariel (H)", "Duriel (H) - Base", "Mephisto (H)", "Diablo (H)", "Baal (H)" };
#define BOSS_LOOT_COUNT 5
static BOOL g_bossLootTCsResolved = FALSE;

/* Pending loot drops from quest rewards */
static volatile int g_pendingLootDrop = 0;
/* which boss TC the next g_pendingLootDrop drain should use. */
static volatile int g_pendingLootBossId = -1;

/* queue of pending specific-item drops (charm / set / unique). */
#define PENDING_DROP_QUEUE_MAX 256   /* was 32 — survive a large offline catch-up burst of set/unique/charm rewards (#60); ~48 bytes each */
typedef struct {
    int   kind;        /* REWARD_DROP_CHARM / SET / UNIQUE */
    int   idx;         /* catalog index */
    char  source[40];  /* short attribution string */
} PendingDropEntry;
static PendingDropEntry g_pendingDropQueue[PENDING_DROP_QUEUE_MAX];
static volatile int g_pendingDropCount = 0;

/* Forward declarations — bodies live further down the unity TU. */
static const char* Quests_CharmName(int c);
static const char* Quests_SetPieceName(int idx);
static const char* Quests_UniqueName(int idx);
static void        Quests_LoadUniqueCatalog(void);
typedef struct {
    int      rowIdx;
    char     name[64];
    char     baseCode[4];
    int      reqLvl;
} UniqueCatalogEntry;
#define UNIQUE_CAT_MAX 512
static UniqueCatalogEntry g_uniqueCatalog[UNIQUE_CAT_MAX];
static int  g_uniqueCatalogCount = 0;
static BOOL g_uniqueCatalogLoaded = FALSE;

static void Quests_QueueSpecificDrop(int kind, int idx, const char* source) {
    if (g_pendingDropCount >= PENDING_DROP_QUEUE_MAX) {
        Log("QueueSpecificDrop: queue full, dropping kind=%d idx=%d source=%s\n",
            kind, idx, source ? source : "(null)");
        return;
    }
    PendingDropEntry* e = &g_pendingDropQueue[g_pendingDropCount++];
    e->kind = kind;
    e->idx  = idx;
    if (source) {
        strncpy(e->source, source, sizeof(e->source) - 1);
        e->source[sizeof(e->source) - 1] = 0;
    } else {
        e->source[0] = 0;
    }
    /* Notify only — the actual spawn happens in the gameloop tick when we have a valid player unit. */
    const char* kindName = "Drop";
    char itemName[80] = "";
    if (kind == REWARD_DROP_CHARM)  { kindName = Quests_CharmName(idx); _snprintf(itemName, sizeof(itemName), "%s", kindName); }
    if (kind == REWARD_DROP_SET)    { _snprintf(itemName, sizeof(itemName), "Drop: %s", Quests_SetPieceName(idx)); }
    if (kind == REWARD_DROP_UNIQUE) { _snprintf(itemName, sizeof(itemName), "Drop: %s", Quests_UniqueName(idx)); }
    if (kind == REWARD_DROP_BATCH && idx >= 0 && idx < FILLER_BATCH_COUNT)
        _snprintf(itemName, sizeof(itemName), "Drop: %s", g_fillerBatches[idx].name);
    if (itemName[0]) ShowNotify(itemName);
    Log("QueueSpecificDrop: queued kind=%d idx=%d source=%s -> %s (queue=%d)\n",
        kind, idx, source ? source : "(none)", itemName, g_pendingDropCount);
    ItemLogAddA(2, 9, itemName[0] ? itemName : "Drop", source ? source : "AP server");
}

/* Internal accessors used by gameloop.c's drop processor. */
static BOOL Quests_PeekPendingDrop(int* outKind, int* outIdx) {
    if (g_pendingDropCount <= 0) return FALSE;
    if (outKind) *outKind = g_pendingDropQueue[0].kind;
    if (outIdx)  *outIdx  = g_pendingDropQueue[0].idx;
    return TRUE;
}
static void Quests_ConsumePendingDrop(void) {
    if (g_pendingDropCount <= 0) return;
    for (int i = 1; i < g_pendingDropCount; i++) {
        g_pendingDropQueue[i - 1] = g_pendingDropQueue[i];
    }
    g_pendingDropCount--;
}

/* Code helpers for the spawn path. */
static const char* QUESTS_CHARM_CODES[3] = { "cm1", "cm2", "cm3" };
static DWORD Quests_PackCode(const char* code3) {
    /* 3-byte code + space-pad to 4. */
    if (!code3) return 0;
    int n = (int)strlen(code3);
    if (n < 1 || n > 3) return 0;
    DWORD r = 0x20202020u;
    for (int i = 0; i < n; i++) {
        r = (r & ~(0xFFu << (i * 8))) | ((DWORD)(BYTE)code3[i] << (i * 8));
    }
    return r;
}
/* Spawn one filler batch. Returns how many items actually appeared.
 *
 * Delivered the same way the dev-menu item buttons are, which is the path
 * confirmed to work: QUESTS_CreateItem with bDroppable=1. A batch that spawns
 * nothing at all is reported by the caller rather than passing silently — a
 * reward that vanishes is the one failure mode this whole area keeps hitting. */
static int Quests_SpawnFillerBatch(void* pGame, void* pPlayer, int batchIdx, int playerLvl) {
    if (batchIdx < 0 || batchIdx >= FILLER_BATCH_COUNT) return 0;
    if (!pGame || !pPlayer || !hD2Game) return 0;

    typedef void* (__fastcall *CreateItem_t)(void*, void*, DWORD, int, BYTE, int);
    CreateItem_t fnCI = (CreateItem_t)((DWORD)hD2Game + 0x65DF0);

    const FillerBatch* b = &g_fillerBatches[batchIdx];
    int nCodes = 0;
    while (nCodes < FILLER_BATCH_CODES_MAX && b->codes[nCodes]) nCodes++;
    if (nCodes == 0) return 0;

    int lvl = playerLvl > 0 ? playerLvl : 1;
    if (lvl > 99) lvl = 99;

    int made = 0;
    int first = b->pickOne ? (rand() % nCodes) : 0;
    int last  = b->pickOne ? first : nCodes - 1;
    for (int c = first; c <= last; c++) {
        DWORD code = Quests_PackCode(b->codes[c]);
        if (!code) continue;
        int n = b->qty[c] ? b->qty[c] : 1;
        for (int k = 0; k < n; k++) {
            void* it = NULL;
            __try { it = fnCI(pGame, pPlayer, code, lvl, b->quality, 1); }
            __except(EXCEPTION_EXECUTE_HANDLER) { it = NULL; }
            if (!it && b->quality != 2) {
                /* Magic can come back empty for some bases; normal never does. */
                __try { it = fnCI(pGame, pPlayer, code, lvl, 2, 1); }
                __except(EXCEPTION_EXECUTE_HANDLER) { it = NULL; }
            }
            if (it) made++;
        }
    }
    Log("FILLER BATCH: '%s' -> %d item(s) at lvl %d\n", b->name, made, lvl);
    return made;
}

extern const char* Coll_GetSetPieceCode(int idx);
static DWORD Quests_GetDropCode(int kind, int idx) {
    if (kind == REWARD_DROP_CHARM) {
        if (idx < 0 || idx > 2) idx = 0;
        return Quests_PackCode(QUESTS_CHARM_CODES[idx]);
    }
    if (kind == REWARD_DROP_SET) {
        const char* code = Coll_GetSetPieceCode(idx);
        return code ? Quests_PackCode(code) : 0;
    }
    if (kind == REWARD_DROP_UNIQUE) {
        if (!g_uniqueCatalogLoaded) Quests_LoadUniqueCatalog();
        if (idx < 0 || idx >= g_uniqueCatalogCount) return 0;
        return Quests_PackCode(g_uniqueCatalog[idx].baseCode);
    }
    return 0;
}
/* Quality byte for QUESTS_CreateItem. */
static BYTE Quests_GetDropQuality(int kind) {
    if (kind == REWARD_DROP_CHARM)  return 4; /* Magic */
    if (kind == REWARD_DROP_SET)    return 5; /* Set */
    if (kind == REWARD_DROP_UNIQUE) return 7; /* Unique */
    return 2;
}

/* Trap effect types — randomly chosen when a REWARD_TRAP triggers */
#define TRAP_MONSTERS  0  /* spawn 8-12 monsters near player */
#define TRAP_SLOW      1  /* Decrepify: -50% velocity */
#define TRAP_WEAKEN    2  /* Amplify Damage: -100% phys resist */
#define TRAP_POISON    3  /* Poison: HP drain over time */
/* Object traps — these change the room instead of a stat. See d2arch_objtraps.c. */
#define TRAP_BARRELS   4  /* ring of exploding barrels */
#define TRAP_URNS      5  /* a floor of urns */
#define TRAP_TREASURE  6  /* six chests, two of which are not chests */
#define TRAP_JUNK      7  /* 20 useless items into the backpack */
#define TRAP_TYPE_COUNT 8
static volatile int g_pendingTrapSlow = 0;
static volatile int g_pendingTrapWeaken = 0;
static volatile int g_pendingTrapPoison = 0;
/* Object-trap queues. They live here with the others rather than in
 * d2arch_objtraps.c, which drains them: that file is included after
 * d2arch_gameloop.c, and the gameloop needs to queue them. */
static volatile int g_pendingTrapBarrels  = 0;
static volatile int g_pendingTrapUrns     = 0;
static volatile int g_pendingTrapTreasure = 0;
static volatile int g_pendingTrapJunk     = 0;
static int  g_questKillCount[3][MAX_QUEST_ID]; /* [difficulty][questId] kill progress */
static int  g_questGold[3][MAX_QUEST_ID];      /* [difficulty][questId] gold amount */
/* AP location owner names — who receives the item when this check is done */
static char g_locationOwner[3][MAX_QUEST_ID][24]; /* [diff][questId] = "PlayerName" */
static BOOL g_locationOwnersLoaded = FALSE;

/* AP gate-key item location map. */
#define APKEY_DISPLAY_LEN 64
static char g_apItemLocation[3][18][APKEY_DISPLAY_LEN]; /* [diff][gate_slot] */
static BOOL g_apItemLocationsLoaded = FALSE;

/* 1.9.0 Phase 9 — public accessor for the F4 render path. */
const char* Quests_GetGateKeyLocStr(int difficulty, int gateSlot) {
    if (!g_apItemLocationsLoaded) return NULL;
    if (difficulty < 0 || difficulty > 2) return NULL;
    if (gateSlot < 0 || gateSlot > 17) return NULL;
    if (g_apItemLocation[difficulty][gateSlot][0] == 0) return NULL;
    return g_apItemLocation[difficulty][gateSlot];
}

/* Trap system — filler items that spawn Super Uniques */
#define NUM_TRAPS_DEFAULT 20
static int g_numTraps = NUM_TRAPS_DEFAULT;  /* configurable trap count */
static volatile int g_pendingTrapSpawn = 0;  /* queued traps waiting to spawn */
/* armed by the AP poll handlers (render/UI thread) when a DeathLink arrives, drained by ProcessPendingGameTick (server tick) which performs the actual kill. */
static volatile int g_pendingDeathLinkKill = 0;

/* Per-difficulty filler quest reward types — regenerated from seed */
static int g_questRewardType[3][MAX_QUEST_ID]; /* [difficulty][questIdx] = REWARD_GOLD/STAT/SKILL/... */
/* 1.9.0 reward-redesign companion fields (parallel to g_questGold which holds the gold amount when type=REWARD_GOLD). */
static int g_questXP[3][MAX_QUEST_ID];
static int g_questExtra[3][MAX_QUEST_ID];

/* Unique items catalog forward-declared earlier (so the pending-drop queue's helpers can call into Quests_UniqueName). */
static void Quests_WriteSpoilerFile(void);

/* Helpers used by the standalone spoiler / drop pipelines. */
static const char* Quests_TrapTypeName(int t);
static const char* Quests_BossLootName(int b);

/* Act 1 quests */
/* D2 quest flag IDs: A1Q1=1(Den), A1Q2=2(Blood Raven), A1Q3=3(Malus/Tools), A1Q4=4(Cain), A1Q5=5(Countess), A1Q6=6(Andariel) */
static Quest g_act1Quests[] = {
    /* Story quests — QTYPE_QUESTFLAG: triggers on D2 quest "reward granted" flag (PROGRESSION) */
    {  1, "Den of Evil",             "Complete the Den of Evil quest",           QTYPE_QUESTFLAG, 1,  0,0, FALSE, REWARD_SKILL, 0 },
    {  2, "Sisters' Burial Grounds", "Complete Blood Raven quest",              QTYPE_QUESTFLAG, 2,  0,0, FALSE, REWARD_SKILL, 0 },
    {  3, "Tools of the Trade",      "Complete the Tools of the Trade quest",   QTYPE_QUESTFLAG, 3,  0,0, FALSE, REWARD_SKILL, 0 },
    {  4, "The Search for Cain",     "Complete the Search for Cain quest",      QTYPE_QUESTFLAG, 4,  0,0, FALSE, REWARD_SKILL, 0 },
    {  5, "The Forgotten Tower",     "Complete the Forgotten Tower quest",      QTYPE_QUESTFLAG, 5,  0,0, FALSE, REWARD_SKILL, 0 },
    {  6, "Sisters to the Slaughter","Complete the Andariel quest",             QTYPE_QUESTFLAG, 6,  0,0, FALSE, REWARD_SKILL, 0 },
    /* SuperUnique hunting (PROGRESSION) — hcIdx from SuperUniques.txt row order */
    {  7, "Hunt: Corpsefire",          "Kill Corpsefire in Den of Evil",          QTYPE_SUPERUNIQUE, 40, 0,0, FALSE, REWARD_SKILL, 0 },
    {  8, "Hunt: Bishibosh",           "Kill Bishibosh in Cold Plains",           QTYPE_SUPERUNIQUE, 0,  0,0, FALSE, REWARD_SKILL, 0 },
    {  9, "Hunt: Bonebreaker",         "Kill Bonebreaker in the Crypt",           QTYPE_SUPERUNIQUE, 1,  0,0, FALSE, REWARD_SKILL, 0 },
    { 70, "Hunt: Coldcrow",            "Kill Coldcrow in the Cave",               QTYPE_SUPERUNIQUE, 2,  0,0, FALSE, REWARD_SKILL, 0 },
    { 71, "Hunt: Rakanishu",           "Kill Rakanishu in Stony Field",           QTYPE_SUPERUNIQUE, 3,  0,0, FALSE, REWARD_SKILL, 0 },
    { 72, "Hunt: Treehead WoodFist",   "Kill Treehead WoodFist in Dark Wood",    QTYPE_SUPERUNIQUE, 4,  0,0, FALSE, REWARD_SKILL, 0 },
    { 73, "Hunt: Griswold",            "Kill Griswold in Tristram",               QTYPE_SUPERUNIQUE, 5,  0,0, FALSE, REWARD_SKILL, 0 },
    { 74, "Hunt: The Countess",        "Kill The Countess in Tower Cellar",       QTYPE_SUPERUNIQUE, 6,  0,0, FALSE, REWARD_SKILL, 0 },
    { 75, "Hunt: Pitspawn Fouldog",    "Kill Pitspawn Fouldog",                   QTYPE_SUPERUNIQUE, 7,  0,0, FALSE, REWARD_SKILL, 0 },
    /* Flamespike removed — does not exist in Lord of Destruction */
    { 77, "Hunt: Boneash",             "Kill Boneash in the Cathedral",           QTYPE_SUPERUNIQUE, 9,  0,0, FALSE, REWARD_SKILL, 0 },
    { 80, "Hunt: The Smith",            "Kill The Smith in the Barracks",          QTYPE_SUPERUNIQUE, 20, 0,0, FALSE, REWARD_SKILL, 0 },
    /* Level milestones (PROGRESSION) */
    /* Level milestones — global across all acts, grouped by difficulty */
    /* Normal: 5, 10, 15, 20, 30 */
    { 78, "Reach Level 5",             "Reach character level 5",                 QTYPE_LEVEL, 5,  0,0, FALSE, REWARD_SKILL, 0 },
    { 79, "Reach Level 10",            "Reach character level 10",                QTYPE_LEVEL, 10, 0,0, FALSE, REWARD_SKILL, 0 },
    { 81, "Reach Level 15",            "Reach character level 15",                QTYPE_LEVEL, 15, 0,0, FALSE, REWARD_SKILL, 0 },
    { 82, "Reach Level 20",            "Reach character level 20",                QTYPE_LEVEL, 20, 0,0, FALSE, REWARD_SKILL, 0 },
    { 83, "Reach Level 30",            "Reach character level 30",                QTYPE_LEVEL, 30, 0,0, FALSE, REWARD_SKILL, 0 },
    /* Nightmare: 35, 40, 45, 50, 55 */
    {180, "Reach Level 35",            "Reach character level 35",                QTYPE_LEVEL, 35, 0,0, FALSE, REWARD_SKILL, 0 },
    {181, "Reach Level 40",            "Reach character level 40",                QTYPE_LEVEL, 40, 0,0, FALSE, REWARD_SKILL, 0 },
    {182, "Reach Level 45",            "Reach character level 45",                QTYPE_LEVEL, 45, 0,0, FALSE, REWARD_SKILL, 0 },
    {183, "Reach Level 50",            "Reach character level 50",                QTYPE_LEVEL, 50, 0,0, FALSE, REWARD_SKILL, 0 },
    {184, "Reach Level 55",            "Reach character level 55",                QTYPE_LEVEL, 55, 0,0, FALSE, REWARD_SKILL, 0 },
    /* Hell: 60, 65, 70, 75 */
    {282, "Reach Level 60",            "Reach character level 60",                QTYPE_LEVEL, 60, 0,0, FALSE, REWARD_SKILL, 0 },
    {283, "Reach Level 65",            "Reach character level 65",                QTYPE_LEVEL, 65, 0,0, FALSE, REWARD_SKILL, 0 },
    {284, "Reach Level 70",            "Reach character level 70",                QTYPE_LEVEL, 70, 0,0, FALSE, REWARD_SKILL, 0 },
    {285, "Reach Level 75",            "Reach character level 75",                QTYPE_LEVEL, 75, 0,0, FALSE, REWARD_SKILL, 0 },
    /* Kill quests — FILLER */
    { 10, "Clear Blood Moor",           "Kill 25 monsters in Blood Moor",            QTYPE_KILL, 2,  25,0, FALSE, REWARD_GOLD, 0 },
    { 11, "Clear Cold Plains",          "Kill 25 monsters in Cold Plains",            QTYPE_KILL, 3,  25,0, FALSE, REWARD_GOLD, 0 },
    { 12, "Clear Stony Field",          "Kill 30 monsters in Stony Field",            QTYPE_KILL, 4,  30,0, FALSE, REWARD_GOLD, 0 },
    { 13, "Clear Dark Wood",            "Kill 30 monsters in Dark Wood",              QTYPE_KILL, 5,  30,0, FALSE, REWARD_GOLD, 0 },
    { 14, "Clear Black Marsh",          "Kill 30 monsters in Black Marsh",            QTYPE_KILL, 6,  30,0, FALSE, REWARD_GOLD, 0 },
    { 15, "Clear Tamoe Highland",       "Kill 30 monsters in Tamoe Highland",         QTYPE_KILL, 7,  30,0, FALSE, REWARD_GOLD, 0 },
    { 16, "Clear Den of Evil",          "Kill 20 monsters in the Den of Evil",        QTYPE_KILL, 8,  20,0, FALSE, REWARD_GOLD, 0 },
    { 17, "Clear Cave Level 1",         "Kill 20 monsters in Cave Level 1",           QTYPE_KILL, 9,  20,0, FALSE, REWARD_GOLD, 0 },
    { 18, "Clear Underground Passage",  "Kill 20 monsters in Underground Passage",    QTYPE_KILL, 10, 20,0, FALSE, REWARD_GOLD, 0 },
    { 19, "Clear Burial Grounds",       "Kill 8 monsters in Burial Grounds",          QTYPE_KILL, 17, 8, 0, FALSE, REWARD_GOLD, 0 },
    { 20, "Clear The Crypt",            "Kill 20 monsters in the Crypt",              QTYPE_KILL, 18, 20,0, FALSE, REWARD_GOLD, 0 },
    { 21, "Clear Mausoleum",            "Kill 20 monsters in the Mausoleum",          QTYPE_KILL, 19, 20,0, FALSE, REWARD_GOLD, 0 },
    /* V1 (3.1.0) — restored to normal kill checks. */
    { 22, "Clear Tower Cellar L1",      "Kill 8 monsters in Tower Cellar Level 1",    QTYPE_KILL, 21, 8, 0, FALSE, REWARD_GOLD, 0 },
    { 23, "Clear Tower Cellar L2",      "Kill 8 monsters in Tower Cellar Level 2",    QTYPE_KILL, 22, 8, 0, FALSE, REWARD_GOLD, 0 },
    { 24, "Clear Tower Cellar L3",      "Kill 8 monsters in Tower Cellar Level 3",    QTYPE_KILL, 23, 8, 0, FALSE, REWARD_GOLD, 0 },
    { 25, "Clear Tower Cellar L4",      "Kill 8 monsters in Tower Cellar Level 4",    QTYPE_KILL, 24, 8, 0, FALSE, REWARD_GOLD, 0 },
    { 26, "Clear Tower Cellar L5",      "Kill 8 monsters in Tower Cellar Level 5",    QTYPE_KILL, 25, 8, 0, FALSE, REWARD_GOLD, 0 },
    /* KILL -> AREA (Maegis, 2026-08-11): Monastery Gate + Inner Cloister spawn
     * almost no monsters, so "kill 8" meant dragging monsters in from the
     * neighbour zone. Same treatment as the Tower Cellar L1/L2 fix in 3.0.0 —
     * the check keeps its id (AP placement untouched), it just completes on
     * zone-in like the Enter checks do. */
    { 27, "Clear Monastery Gate",       "Reach the Monastery Gate",                   QTYPE_AREA, 26, 0, 0, FALSE, REWARD_GOLD, 0 },
    { 28, "Clear Outer Cloister",       "Kill 8 monsters in Outer Cloister",          QTYPE_KILL, 27, 8, 0, FALSE, REWARD_GOLD, 0 },
    { 29, "Clear Barracks",             "Kill 20 monsters in the Barracks",           QTYPE_KILL, 28, 20,0, FALSE, REWARD_GOLD, 0 },
    { 30, "Clear Jail Level 1",         "Kill 25 monsters in Jail Level 1",           QTYPE_KILL, 29, 25,0, FALSE, REWARD_GOLD, 0 },
    { 31, "Clear Jail Level 2",         "Kill 25 monsters in Jail Level 2",           QTYPE_KILL, 30, 25,0, FALSE, REWARD_GOLD, 0 },
    { 32, "Clear Jail Level 3",         "Kill 25 monsters in Jail Level 3",           QTYPE_KILL, 31, 25,0, FALSE, REWARD_GOLD, 0 },
    { 33, "Clear Cathedral",            "Kill 20 monsters in the Cathedral",          QTYPE_KILL, 33, 20,0, FALSE, REWARD_GOLD, 0 },
    { 34, "Clear Catacombs L1",         "Kill 30 monsters in Catacombs Level 1",      QTYPE_KILL, 34, 30,0, FALSE, REWARD_GOLD, 0 },
    { 35, "Clear Catacombs L2",         "Kill 35 monsters in Catacombs Level 2",      QTYPE_KILL, 35, 35,0, FALSE, REWARD_GOLD, 0 },
    { 36, "Clear Catacombs L3",         "Kill 40 monsters in Catacombs Level 3",      QTYPE_KILL, 36, 40,0, FALSE, REWARD_GOLD, 0 },
    { 37, "Clear Tristram",             "Kill 20 monsters in Tristram",               QTYPE_KILL, 38, 20,0, FALSE, REWARD_GOLD, 0 },
    { 38, "Clear Cave Level 2",         "Kill 8 monsters in Cave Level 2",            QTYPE_KILL, 13, 8, 0, FALSE, REWARD_GOLD, 0 },
    { 39, "Clear Underground Passage 2","Kill 8 monsters in Underground Passage 2",   QTYPE_KILL, 14, 8, 0, FALSE, REWARD_GOLD, 0 },
    { 59, "Clear Hole Level 1",         "Kill 8 monsters in the Hole Level 1",        QTYPE_KILL, 11, 8, 0, FALSE, REWARD_GOLD, 0 },
    { 60, "Clear Hole Level 2",         "Kill 8 monsters in the Hole Level 2",        QTYPE_KILL, 15, 8, 0, FALSE, REWARD_GOLD, 0 },
    {58, "Clear Pit Level 1",                 "Kill 20 monsters in Pit Level 1",                  QTYPE_KILL, 12, 20,0, FALSE, REWARD_GOLD, 0 },
    {61, "Clear Pit Level 2",                 "Kill 8 monsters in Pit Level 2",                   QTYPE_KILL, 16, 8,0, FALSE, REWARD_GOLD, 0 },
    {62, "Clear Inner Cloister",              "Reach the Inner Cloister",                         QTYPE_AREA, 32, 0,0, FALSE, REWARD_GOLD, 0 },
    {63, "Clear Catacombs L4",                "Kill 8 monsters in Catacombs L4",                  QTYPE_KILL, 37, 8,0, FALSE, REWARD_GOLD, 0 },
    /* Area entry — FILLER */
    { 40, "Enter Blood Moor",           "Enter Blood Moor",                           QTYPE_AREA, 2,  0,0, FALSE, REWARD_GOLD, 0 },
    { 41, "Enter Cold Plains",          "Enter Cold Plains",                           QTYPE_AREA, 3,  0,0, FALSE, REWARD_GOLD, 0 },
    { 42, "Enter Stony Field",          "Enter Stony Field",                           QTYPE_AREA, 4,  0,0, FALSE, REWARD_GOLD, 0 },
    { 43, "Enter Dark Wood",            "Enter Dark Wood",                             QTYPE_AREA, 5,  0,0, FALSE, REWARD_GOLD, 0 },
    { 44, "Enter Black Marsh",          "Enter Black Marsh",                           QTYPE_AREA, 6,  0,0, FALSE, REWARD_GOLD, 0 },
    { 45, "Enter Tamoe Highland",       "Enter Tamoe Highland",                        QTYPE_AREA, 7,  0,0, FALSE, REWARD_GOLD, 0 },
    { 46, "Enter Den of Evil",          "Enter the Den of Evil",                       QTYPE_AREA, 8,  0,0, FALSE, REWARD_GOLD, 0 },
    { 47, "Enter Tristram",             "Enter Tristram",                              QTYPE_AREA, 38, 0,0, FALSE, REWARD_GOLD, 0 },
    { 48, "Enter Catacombs",            "Enter the Catacombs",                         QTYPE_AREA, 34, 0,0, FALSE, REWARD_GOLD, 0 },
    { 49, "Enter Tower Cellar",         "Enter Tower Cellar",                          QTYPE_AREA, 21, 0,0, FALSE, REWARD_GOLD, 0 },
    /* Waypoint quests — FILLER */
    { 50, "Cold Plains Waypoint",        "Activate the Cold Plains waypoint",          QTYPE_WAYPOINT, 1,  0,0, FALSE, REWARD_GOLD, 0 },
    { 51, "Stony Field Waypoint",        "Activate the Stony Field waypoint",          QTYPE_WAYPOINT, 2,  0,0, FALSE, REWARD_GOLD, 0 },
    { 52, "Dark Wood Waypoint",          "Activate the Dark Wood waypoint",            QTYPE_WAYPOINT, 3,  0,0, FALSE, REWARD_GOLD, 0 },
    { 53, "Black Marsh Waypoint",        "Activate the Black Marsh waypoint",          QTYPE_WAYPOINT, 4,  0,0, FALSE, REWARD_GOLD, 0 },
    { 54, "Outer Cloister Waypoint",     "Activate the Outer Cloister waypoint",       QTYPE_WAYPOINT, 5,  0,0, FALSE, REWARD_GOLD, 0 },
    { 55, "Jail Level 1 Waypoint",       "Activate the Jail Level 1 waypoint",         QTYPE_WAYPOINT, 6,  0,0, FALSE, REWARD_GOLD, 0 },
    { 56, "Inner Cloister Waypoint",     "Activate the Inner Cloister waypoint",       QTYPE_WAYPOINT, 7,  0,0, FALSE, REWARD_GOLD, 0 },
    { 57, "Catacombs Level 2 Waypoint",  "Activate the Catacombs Level 2 waypoint",    QTYPE_WAYPOINT, 8,  0,0, FALSE, REWARD_GOLD, 0 },
};

/* Act 2 quests */
/* D2 quest flag IDs: Radament=7, Horadric Staff=8, Tainted Sun=9, Arcane=10, Summoner=11, Duriel=12 */
static Quest g_act2Quests[] = {
    /* Story quests — QTYPE_QUESTFLAG (PROGRESSION) D2MOO QUESTSTATEFLAG IDs: A2Q1=9, A2Q2=10, A2Q3=11, A2Q4=12, A2Q5=13, A2Q6=14 (NOT 7-12! */
    {101, "Radament's Lair",     "Complete the Radament quest",        QTYPE_QUESTFLAG, 9,  0,0, FALSE, REWARD_SKILL, 0 },
    {102, "The Horadric Staff",  "Complete the Horadric Staff quest",  QTYPE_QUESTFLAG, 10, 0,0, FALSE, REWARD_SKILL, 0 },
    {103, "Tainted Sun",         "Complete the Tainted Sun quest",     QTYPE_QUESTFLAG, 11, 0,0, FALSE, REWARD_SKILL, 0 },
    {104, "Arcane Sanctuary",    "Complete the Arcane Sanctuary quest",QTYPE_QUESTFLAG, 12, 0,0, FALSE, REWARD_SKILL, 0 },
    {105, "The Summoner",        "Complete the Summoner quest",        QTYPE_QUESTFLAG, 13, 0,0, FALSE, REWARD_SKILL, 0 },
    {106, "Seven Tombs",         "Complete the Duriel quest",          QTYPE_QUESTFLAG, 14, 0,0, FALSE, REWARD_SKILL, 0 },
    /* SuperUnique hunting (PROGRESSION) — hcIdx from SuperUniques.txt */
    {170, "Hunt: Radament",           "Kill Radament in the Sewers",          QTYPE_SUPERUNIQUE, 10, 0,0, FALSE, REWARD_SKILL, 0 },
    {171, "Hunt: Bloodwitch",         "Kill Bloodwitch the Wild",             QTYPE_SUPERUNIQUE, 11, 0,0, FALSE, REWARD_SKILL, 0 },
    {172, "Hunt: Fangskin",           "Kill Fangskin",                        QTYPE_SUPERUNIQUE, 12, 0,0, FALSE, REWARD_SKILL, 0 },
    {173, "Hunt: Beetleburst",        "Kill Beetleburst",                     QTYPE_SUPERUNIQUE, 13, 0,0, FALSE, REWARD_SKILL, 0 },
    {174, "Hunt: Creeping Feature",   "Kill Creeping Feature",               QTYPE_SUPERUNIQUE, 14, 0,0, FALSE, REWARD_SKILL, 0 },
    {175, "Hunt: Coldworm",           "Kill Coldworm the Burrower",          QTYPE_SUPERUNIQUE, 15, 0,0, FALSE, REWARD_SKILL, 0 },
    {176, "Hunt: Fire Eye",           "Kill Fire Eye",                       QTYPE_SUPERUNIQUE, 16, 0,0, FALSE, REWARD_SKILL, 0 },
    {177, "Hunt: Dark Elder",         "Kill Dark Elder",                     QTYPE_SUPERUNIQUE, 17, 0,0, FALSE, REWARD_SKILL, 0 },
    {178, "Hunt: The Summoner",       "Kill The Summoner in Arcane",         QTYPE_SUPERUNIQUE, 18, 0,0, FALSE, REWARD_SKILL, 0 },
    {179, "Hunt: Ancient Kaa",        "Kill Ancient Kaa the Soulless",       QTYPE_SUPERUNIQUE, 19, 0,0, FALSE, REWARD_SKILL, 0 },
    /* Level milestones (PROGRESSION) */
    /* Level milestones consolidated in Act 1 */
    /* Kill quests — FILLER */
    {110, "Clear Rocky Waste",         "Kill 25 monsters in Rocky Waste",         QTYPE_KILL, 41, 25,0, FALSE, REWARD_GOLD, 0 },
    {111, "Clear Dry Hills",           "Kill 25 monsters in Dry Hills",           QTYPE_KILL, 42, 25,0, FALSE, REWARD_GOLD, 0 },
    {112, "Clear Far Oasis",           "Kill 30 monsters in Far Oasis",           QTYPE_KILL, 43, 30,0, FALSE, REWARD_GOLD, 0 },
    {113, "Clear Lost City",           "Kill 30 monsters in Lost City",           QTYPE_KILL, 44, 30,0, FALSE, REWARD_GOLD, 0 },
    /* Valley of Snakes removed — small transitional zone, should not have a clear check at all (community report). */
    /* {114, "Clear Valley of Snakes", "Kill 20 monsters in Valley of Snakes", QTYPE_KILL, 45, 20,0, FALSE, REWARD_GOLD, 0 }, */
    /* Sewers removed — D2MOO pathfinding broken in narrow corridors */
    {115, "Clear Sewers L1",           "Kill 15 monsters in Sewers Level 1",      QTYPE_KILL, 47, 15,0, FALSE, REWARD_GOLD, 0 },
    {116, "Clear Sewers L2",           "Kill 15 monsters in Sewers Level 2",      QTYPE_KILL, 48, 15,0, FALSE, REWARD_GOLD, 0 },
    {117, "Clear Halls of Dead L1",    "Kill 20 monsters in Halls of the Dead",   QTYPE_KILL, 56, 20,0, FALSE, REWARD_GOLD, 0 },
    {118, "Clear Halls of Dead L2",    "Kill 20 monsters in Halls of the Dead 2", QTYPE_KILL, 57, 20,0, FALSE, REWARD_GOLD, 0 },
    {119, "Clear Halls of Dead L3",    "Kill 8 monsters in Halls of the Dead 3",  QTYPE_KILL, 60, 8, 0, FALSE, REWARD_GOLD, 0 },
    {120, "Clear Maggot Lair L1",      "Kill 8 monsters in Maggot Lair",          QTYPE_KILL, 62, 8, 0, FALSE, REWARD_GOLD, 0 },
    {121, "Clear Maggot Lair L2",      "Kill 8 monsters in Maggot Lair 2",        QTYPE_KILL, 63, 8, 0, FALSE, REWARD_GOLD, 0 },
    {122, "Clear Maggot Lair L3",      "Kill 8 monsters in Maggot Lair 3",        QTYPE_KILL, 64, 8, 0, FALSE, REWARD_GOLD, 0 },
    {123, "Clear Ancient Tunnels",     "Kill 25 monsters in Ancient Tunnels",     QTYPE_KILL, 65, 25,0, FALSE, REWARD_GOLD, 0 },
    {124, "Clear Arcane Sanctuary",    "Kill 30 monsters in Arcane Sanctuary",    QTYPE_KILL, 74, 30,0, FALSE, REWARD_GOLD, 0 },
    {125, "Clear Palace Cellar L1",    "Kill 8 monsters in Palace Cellar",        QTYPE_KILL, 52, 8, 0, FALSE, REWARD_GOLD, 0 },
    {126, "Clear Palace Cellar L2",    "Kill 8 monsters in Palace Cellar 2",      QTYPE_KILL, 53, 8, 0, FALSE, REWARD_GOLD, 0 },
    {127, "Clear Palace Cellar L3",    "Kill 8 monsters in Palace Cellar 3",      QTYPE_KILL, 54, 8, 0, FALSE, REWARD_GOLD, 0 },
    {128, "Clear Canyon of Magi",      "Kill 20 monsters in Canyon of the Magi",  QTYPE_KILL, 46, 20,0, FALSE, REWARD_GOLD, 0 },
    {129, "Clear Stony Tomb",          "Kill 8 monsters in Stony Tomb",           QTYPE_KILL, 55, 8, 0, FALSE, REWARD_GOLD, 0 },
    {130, "Clear Sewers L3",                  "Kill 15 monsters in Sewers L3",                    QTYPE_KILL, 49, 15,0, FALSE, REWARD_GOLD, 0 },
    {131, "Clear Harem L2",                   "Kill 15 monsters in Harem L2",                     QTYPE_KILL, 51, 15,0, FALSE, REWARD_GOLD, 0 },
    {132, "Clear Claw Viper Temple L1",       "Kill 15 monsters in Claw Viper Temple L1",         QTYPE_KILL, 58, 15,0, FALSE, REWARD_GOLD, 0 },
    {133, "Clear Claw Viper Temple L2",       "Kill 12 monsters in Claw Viper Temple L2",         QTYPE_KILL, 61, 12,0, FALSE, REWARD_GOLD, 0 },
    {134, "Clear Stony Tomb L2",              "Kill 12 monsters in Stony Tomb L2",                QTYPE_KILL, 59, 12,0, FALSE, REWARD_GOLD, 0 },
    {135, "Clear Tal Rasha's Tomb 1",         "Kill 12 monsters in Tal Rasha's Tomb 1",           QTYPE_KILL, 66, 12,0, FALSE, REWARD_GOLD, 0 },
    {136, "Clear Tal Rasha's Tomb 2",         "Kill 12 monsters in Tal Rasha's Tomb 2",           QTYPE_KILL, 67, 12,0, FALSE, REWARD_GOLD, 0 },
    {137, "Clear Tal Rasha's Tomb 3",         "Kill 12 monsters in Tal Rasha's Tomb 3",           QTYPE_KILL, 68, 12,0, FALSE, REWARD_GOLD, 0 },
    {138, "Clear Tal Rasha's Tomb 4",         "Kill 12 monsters in Tal Rasha's Tomb 4",           QTYPE_KILL, 69, 12,0, FALSE, REWARD_GOLD, 0 },
    {139, "Clear Tal Rasha's Tomb 5",         "Kill 12 monsters in Tal Rasha's Tomb 5",           QTYPE_KILL, 70, 12,0, FALSE, REWARD_GOLD, 0 },
    {145, "Clear Tal Rasha's Tomb 6",         "Kill 12 monsters in Tal Rasha's Tomb 6",           QTYPE_KILL, 71, 12,0, FALSE, REWARD_GOLD, 0 },
    {146, "Clear Tal Rasha's Tomb 7",         "Kill 12 monsters in Tal Rasha's Tomb 7",           QTYPE_KILL, 72, 12,0, FALSE, REWARD_GOLD, 0 },
    /* Area entry — FILLER */
    {140, "Enter Rocky Waste",         "Enter the Rocky Waste",                   QTYPE_AREA, 41, 0,0, FALSE, REWARD_GOLD, 0 },
    {141, "Enter Dry Hills",           "Enter the Dry Hills",                     QTYPE_AREA, 42, 0,0, FALSE, REWARD_GOLD, 0 },
    {142, "Enter Far Oasis",           "Enter the Far Oasis",                     QTYPE_AREA, 43, 0,0, FALSE, REWARD_GOLD, 0 },
    {143, "Enter Lost City",           "Enter the Lost City",                     QTYPE_AREA, 44, 0,0, FALSE, REWARD_GOLD, 0 },
    {144, "Enter Arcane Sanctuary",    "Enter the Arcane Sanctuary",              QTYPE_AREA, 74, 0,0, FALSE, REWARD_GOLD, 0 },
    /* Waypoint quests — FILLER */
    {150, "Sewers Waypoint",             "Activate the Sewers waypoint",               QTYPE_WAYPOINT, 10, 0,0, FALSE, REWARD_GOLD, 0 },
    {151, "Dry Hills Waypoint",           "Activate the Dry Hills waypoint",            QTYPE_WAYPOINT, 11, 0,0, FALSE, REWARD_GOLD, 0 },
    {152, "Halls of the Dead Waypoint",  "Activate the Halls of the Dead waypoint",    QTYPE_WAYPOINT, 12, 0,0, FALSE, REWARD_GOLD, 0 },
    {153, "Far Oasis Waypoint",           "Activate the Far Oasis waypoint",            QTYPE_WAYPOINT, 13, 0,0, FALSE, REWARD_GOLD, 0 },
    {154, "Lost City Waypoint",          "Activate the Lost City waypoint",            QTYPE_WAYPOINT, 14, 0,0, FALSE, REWARD_GOLD, 0 },
    {155, "Palace Cellar Waypoint",      "Activate the Palace Cellar waypoint",        QTYPE_WAYPOINT, 15, 0,0, FALSE, REWARD_GOLD, 0 },
    {156, "Arcane Sanctuary Waypoint",   "Activate the Arcane Sanctuary waypoint",     QTYPE_WAYPOINT, 16, 0,0, FALSE, REWARD_GOLD, 0 },
    {157, "Canyon of the Magi Waypoint", "Activate the Canyon of the Magi waypoint",   QTYPE_WAYPOINT, 17, 0,0, FALSE, REWARD_GOLD, 0 },
};

/* Act 3 quests */
/* D2 quest flag IDs: Lam Esen=17, Khalim=18, Gidbinn=19, Golden Bird=20, Travincal=21, Mephisto=22 */
static Quest g_act3Quests[] = {
    /* Story quests — QTYPE_QUESTFLAG (PROGRESSION) */
    {201, "Lam Esen's Tome",     "Complete the Lam Esen quest",        QTYPE_QUESTFLAG, 17, 0,0, FALSE, REWARD_SKILL, 0 },
    {202, "Khalim's Will",       "Complete the Khalim quest",          QTYPE_QUESTFLAG, 18, 0,0, FALSE, REWARD_SKILL, 0 },
    {203, "Blade of Old Religion","Complete the Gidbinn quest",        QTYPE_QUESTFLAG, 19, 0,0, FALSE, REWARD_SKILL, 0 },
    {204, "The Golden Bird",     "Complete the Golden Bird quest",     QTYPE_QUESTFLAG, 20, 0,0, FALSE, REWARD_SKILL, 0 },
    {205, "The Blackened Temple","Complete the Travincal quest",       QTYPE_QUESTFLAG, 21, 0,0, FALSE, REWARD_SKILL, 0 },
    {206, "The Guardian",        "Complete the Mephisto quest",        QTYPE_QUESTFLAG, 22, 0,0, FALSE, REWARD_SKILL, 0 },
    /* SuperUnique hunting (PROGRESSION) — hcIdx from SuperUniques.txt */
    /* NOTE: The Smith (hcIdx=20) moved to Act 1 where it actually spawns (Barracks) */
    {271, "Hunt: Sszark the Burning", "Kill Sszark the Burning",             QTYPE_SUPERUNIQUE, 21, 0,0, FALSE, REWARD_SKILL, 0 },
    {272, "Hunt: Witch Doctor Endugu","Kill Witch Doctor Endugu",            QTYPE_SUPERUNIQUE, 22, 0,0, FALSE, REWARD_SKILL, 0 },
    {273, "Hunt: Stormtree",          "Kill Stormtree",                      QTYPE_SUPERUNIQUE, 23, 0,0, FALSE, REWARD_SKILL, 0 },
    {274, "Hunt: Sarina",             "Kill Sarina",                         QTYPE_SUPERUNIQUE, 24, 0,0, FALSE, REWARD_SKILL, 0 },
    {275, "Hunt: Icehawk Riftwing",   "Kill Icehawk Riftwing",               QTYPE_SUPERUNIQUE, 25, 0,0, FALSE, REWARD_SKILL, 0 },
    {276, "Hunt: Ismail Vilehand",    "Kill Council Member Ismail",          QTYPE_SUPERUNIQUE, 26, 0,0, FALSE, REWARD_SKILL, 0 },
    {277, "Hunt: Geleb Flamefinger",  "Kill Council Member Geleb",           QTYPE_SUPERUNIQUE, 27, 0,0, FALSE, REWARD_SKILL, 0 },
    {278, "Hunt: Bremm Sparkfist",    "Kill Council Member Bremm",           QTYPE_SUPERUNIQUE, 28, 0,0, FALSE, REWARD_SKILL, 0 },
    {279, "Hunt: Toorc Icefist",      "Kill Council Member Toorc",           QTYPE_SUPERUNIQUE, 29, 0,0, FALSE, REWARD_SKILL, 0 },
    {280, "Hunt: Wyand Voidbringer",  "Kill Council Member Wyand",           QTYPE_SUPERUNIQUE, 30, 0,0, FALSE, REWARD_SKILL, 0 },
    {281, "Hunt: Maffer Dragonhand",  "Kill Council Member Maffer",          QTYPE_SUPERUNIQUE, 31, 0,0, FALSE, REWARD_SKILL, 0 },
    /* Level milestones (PROGRESSION) */
    /* Level milestones consolidated in Act 1 */
    /* Kill quests — FILLER */
    {210, "Clear Spider Forest",       "Kill 30 monsters in Spider Forest",       QTYPE_KILL, 76, 30,0, FALSE, REWARD_GOLD, 0 },
    {211, "Clear Great Marsh",         "Kill 30 monsters in Great Marsh",         QTYPE_KILL, 77, 30,0, FALSE, REWARD_GOLD, 0 },
    {212, "Clear Flayer Jungle",       "Kill 30 monsters in Flayer Jungle",       QTYPE_KILL, 78, 30,0, FALSE, REWARD_GOLD, 0 },
    {213, "Clear Lower Kurast",        "Kill 25 monsters in Lower Kurast",        QTYPE_KILL, 79, 25,0, FALSE, REWARD_GOLD, 0 },
    {214, "Clear Kurast Bazaar",       "Kill 25 monsters in Kurast Bazaar",       QTYPE_KILL, 80, 25,0, FALSE, REWARD_GOLD, 0 },
    {215, "Clear Upper Kurast",        "Kill 25 monsters in Upper Kurast",        QTYPE_KILL, 81, 25,0, FALSE, REWARD_GOLD, 0 },
    {216, "Clear Travincal",           "Kill 25 monsters in Travincal",           QTYPE_KILL, 83, 25,0, FALSE, REWARD_GOLD, 0 },
    {217, "Clear Spider Cave",         "Kill 8 monsters in Spider Cave",          QTYPE_KILL, 84, 8, 0, FALSE, REWARD_GOLD, 0 },
    {218, "Clear Flayer Dungeon L1",   "Kill 20 monsters in Flayer Dungeon",      QTYPE_KILL, 88, 20,0, FALSE, REWARD_GOLD, 0 },
    {219, "Clear Flayer Dungeon L2",   "Kill 20 monsters in Flayer Dungeon 2",    QTYPE_KILL, 89, 20,0, FALSE, REWARD_GOLD, 0 },
    /* Kurast Sewers removed — D2MOO pathfinding broken in narrow corridors */
    {220, "Clear Kurast Sewers L1",    "Kill 15 monsters in Kurast Sewers",       QTYPE_KILL, 92, 15,0, FALSE, REWARD_GOLD, 0 },
    /* 3.9.19: 15 -> 5. Sewers L2 is a small level with few monsters; a tester
     * sat on 9/15 with the floor empty (Marco, 5 September). */
    {221, "Clear Kurast Sewers L2",    "Kill 5 monsters in Kurast Sewers 2",      QTYPE_KILL, 93,  5,0, FALSE, REWARD_GOLD, 0 },
    {222, "Clear Durance L1",          "Kill 25 monsters in Durance of Hate",     QTYPE_KILL, 100,25,0, FALSE, REWARD_GOLD, 0 },
    {223, "Clear Durance L2",          "Kill 30 monsters in Durance of Hate 2",   QTYPE_KILL, 101,30,0, FALSE, REWARD_GOLD, 0 },
    {224, "Clear Kurast Causeway",     "Kill 20 monsters in Kurast Causeway",     QTYPE_KILL, 82, 20,0, FALSE, REWARD_GOLD, 0 },
    {230, "Clear Spider Cavern",              "Kill 12 monsters in Spider Cavern",                QTYPE_KILL, 85, 12,0, FALSE, REWARD_GOLD, 0 },
    {231, "Clear Swampy Pit L1",              "Kill 15 monsters in Swampy Pit L1",                QTYPE_KILL, 86, 15,0, FALSE, REWARD_GOLD, 0 },
    {232, "Clear Swampy Pit L2",              "Kill 15 monsters in Swampy Pit L2",                QTYPE_KILL, 87, 15,0, FALSE, REWARD_GOLD, 0 },
    {233, "Clear Swampy Pit L3",              "Kill 8 monsters in Swampy Pit L3",                 QTYPE_KILL, 90, 8,0, FALSE, REWARD_GOLD, 0 },
    {234, "Clear Flayer Dungeon L3",          "Kill 10 monsters in Flayer Dungeon L3",            QTYPE_KILL, 91, 10,0, FALSE, REWARD_GOLD, 0 },
    {235, "Clear Ruined Temple",              "Kill 8 monsters in Ruined Temple",                 QTYPE_KILL, 94, 8,0, FALSE, REWARD_GOLD, 0 },
    {236, "Clear Disused Fane",               "Kill 8 monsters in Disused Fane",                  QTYPE_KILL, 95, 8,0, FALSE, REWARD_GOLD, 0 },
    {237, "Clear Forgotten Reliquary",        "Kill 8 monsters in Forgotten Reliquary",           QTYPE_KILL, 96, 8,0, FALSE, REWARD_GOLD, 0 },
    {238, "Clear Forgotten Temple",           "Kill 8 monsters in Forgotten Temple",              QTYPE_KILL, 97, 8,0, FALSE, REWARD_GOLD, 0 },
    {239, "Clear Ruined Fane",                "Kill 8 monsters in Ruined Fane",                   QTYPE_KILL, 98, 8,0, FALSE, REWARD_GOLD, 0 },
    {245, "Clear Disused Reliquary",          "Kill 8 monsters in Disused Reliquary",             QTYPE_KILL, 99, 8,0, FALSE, REWARD_GOLD, 0 },
    /* Area entry — FILLER */
    {240, "Enter Spider Forest",       "Enter Spider Forest",                     QTYPE_AREA, 76, 0,0, FALSE, REWARD_GOLD, 0 },
    {241, "Enter Flayer Jungle",       "Enter the Flayer Jungle",                 QTYPE_AREA, 78, 0,0, FALSE, REWARD_GOLD, 0 },
    {242, "Enter Kurast Bazaar",       "Enter the Kurast Bazaar",                 QTYPE_AREA, 80, 0,0, FALSE, REWARD_GOLD, 0 },
    {243, "Enter Travincal",           "Enter Travincal",                         QTYPE_AREA, 83, 0,0, FALSE, REWARD_GOLD, 0 },
    {244, "Enter Durance of Hate",     "Enter the Durance of Hate",               QTYPE_AREA, 100,0,0, FALSE, REWARD_GOLD, 0 },
    /* Waypoint quests — FILLER */
    {250, "Spider Forest Waypoint",      "Activate the Spider Forest waypoint",        QTYPE_WAYPOINT, 19, 0,0, FALSE, REWARD_GOLD, 0 },
    {251, "Great Marsh Waypoint",        "Activate the Great Marsh waypoint",          QTYPE_WAYPOINT, 20, 0,0, FALSE, REWARD_GOLD, 0 },
    {252, "Flayer Jungle Waypoint",      "Activate the Flayer Jungle waypoint",        QTYPE_WAYPOINT, 21, 0,0, FALSE, REWARD_GOLD, 0 },
    {253, "Lower Kurast Waypoint",       "Activate the Lower Kurast waypoint",         QTYPE_WAYPOINT, 22, 0,0, FALSE, REWARD_GOLD, 0 },
    {254, "Kurast Bazaar Waypoint",      "Activate the Kurast Bazaar waypoint",        QTYPE_WAYPOINT, 23, 0,0, FALSE, REWARD_GOLD, 0 },
    {255, "Upper Kurast Waypoint",       "Activate the Upper Kurast waypoint",         QTYPE_WAYPOINT, 24, 0,0, FALSE, REWARD_GOLD, 0 },
    {256, "Travincal Waypoint",          "Activate the Travincal waypoint",            QTYPE_WAYPOINT, 25, 0,0, FALSE, REWARD_GOLD, 0 },
    {257, "Durance of Hate Waypoint",    "Activate the Durance of Hate waypoint",      QTYPE_WAYPOINT, 26, 0,0, FALSE, REWARD_GOLD, 0 },
};

/* Act 4 quests */
/* D2 quest flag IDs: Izual=25, Diablo=26, Hellforge=27 */
static Quest g_act4Quests[] = {
    /* Story quests — QTYPE_QUESTFLAG (PROGRESSION) */
    {301, "The Fallen Angel",    "Complete the Izual quest",           QTYPE_QUESTFLAG, 25, 0,0, FALSE, REWARD_SKILL, 0 },
    {302, "Hell's Forge",        "Complete the Hellforge quest",       QTYPE_QUESTFLAG, 27, 0,0, FALSE, REWARD_SKILL, 0 },
    {303, "Terror's End",        "Complete the Diablo quest",          QTYPE_QUESTFLAG, 26, 0,0, FALSE, REWARD_SKILL, 0 },
    /* SuperUnique hunting (PROGRESSION) — hcIdx from SuperUniques.txt */
    {370, "Hunt: Winged Death",       "Kill Winged Death",                    QTYPE_SUPERUNIQUE, 32, 0,0, FALSE, REWARD_SKILL, 0 },
    {371, "Hunt: The Tormentor",      "Kill The Tormentor",                   QTYPE_SUPERUNIQUE, 33, 0,0, FALSE, REWARD_SKILL, 0 },
    {372, "Hunt: Taintbreeder",       "Kill Taintbreeder",                    QTYPE_SUPERUNIQUE, 34, 0,0, FALSE, REWARD_SKILL, 0 },
    {373, "Hunt: Riftwraith",         "Kill Riftwraith the Cannibal",         QTYPE_SUPERUNIQUE, 35, 0,0, FALSE, REWARD_SKILL, 0 },
    {374, "Hunt: Infector",           "Kill Infector of Souls",               QTYPE_SUPERUNIQUE, 36, 0,0, FALSE, REWARD_SKILL, 0 },
    {375, "Hunt: Lord De Seis",       "Kill Lord De Seis",                    QTYPE_SUPERUNIQUE, 37, 0,0, FALSE, REWARD_SKILL, 0 },
    {376, "Hunt: Grand Vizier",       "Kill Grand Vizier of Chaos",           QTYPE_SUPERUNIQUE, 38, 0,0, FALSE, REWARD_SKILL, 0 },
    /* Level milestone (PROGRESSION) */
    /* Level milestones moved to Act 1 (Normal), Act 2 (NM), Act 3 (Hell) */
    /* Kill quests — FILLER */
    {310, "Clear Outer Steppes",       "Kill 25 monsters in Outer Steppes",       QTYPE_KILL, 104, 25,0, FALSE, REWARD_GOLD, 0 },
    {311, "Clear Plains of Despair",   "Kill 30 monsters in Plains of Despair",   QTYPE_KILL, 105, 30,0, FALSE, REWARD_GOLD, 0 },
    {312, "Clear City of the Damned",  "Kill 30 monsters in City of the Damned",  QTYPE_KILL, 106, 30,0, FALSE, REWARD_GOLD, 0 },
    {313, "Clear River of Flame",      "Kill 35 monsters in River of Flame",      QTYPE_KILL, 107, 35,0, FALSE, REWARD_GOLD, 0 },
    {314, "Clear Chaos Sanctuary",     "Kill 40 monsters in Chaos Sanctuary",     QTYPE_KILL, 108, 40,0, FALSE, REWARD_GOLD, 0 },
    /* Area entry — FILLER */
    {340, "Enter Outer Steppes",       "Enter the Outer Steppes",                 QTYPE_AREA, 104, 0,0, FALSE, REWARD_GOLD, 0 },
    {341, "Enter Plains of Despair",   "Enter the Plains of Despair",             QTYPE_AREA, 105, 0,0, FALSE, REWARD_GOLD, 0 },
    {342, "Enter City of the Damned",  "Enter the City of the Damned",            QTYPE_AREA, 106, 0,0, FALSE, REWARD_GOLD, 0 },
    {343, "Enter River of Flame",      "Enter the River of Flame",                QTYPE_AREA, 107, 0,0, FALSE, REWARD_GOLD, 0 },
    {344, "Enter Chaos Sanctuary",     "Enter the Chaos Sanctuary",               QTYPE_AREA, 108, 0,0, FALSE, REWARD_GOLD, 0 },
    /* Waypoint quests — FILLER */
    /* waypoint index 28 is the City of the Damned, not the Plains of Despair (which has no waypoint at all). */
    {350, "City of the Damned Waypoint", "Activate the City of the Damned waypoint",   QTYPE_WAYPOINT, 28, 0,0, FALSE, REWARD_GOLD, 0 },
    {351, "River of Flame Waypoint",     "Activate the River of Flame waypoint",       QTYPE_WAYPOINT, 29, 0,0, FALSE, REWARD_GOLD, 0 },
};

/* Act 5 quests */
/* D2 quest flag IDs: Shenk=35, Rescue=36, Prison of Ice=37, Betrayal=38, Ancients=39, Baal=40 */
static Quest g_act5Quests[] = {
    /* Story quests — QTYPE_QUESTFLAG (PROGRESSION) */
    {401, "Siege on Harrogath",  "Complete the Shenk quest",           QTYPE_QUESTFLAG, 35, 0,0, FALSE, REWARD_SKILL, 0 },
    {402, "Rescue on Mt. Arreat","Complete the Rescue quest",          QTYPE_QUESTFLAG, 36, 0,0, FALSE, REWARD_SKILL, 0 },
    {403, "Prison of Ice",       "Complete the Prison of Ice quest",   QTYPE_QUESTFLAG, 37, 0,0, FALSE, REWARD_SKILL, 0 },
    {404, "Betrayal of Harrogath","Complete the Betrayal quest",       QTYPE_QUESTFLAG, 38, 0,0, FALSE, REWARD_SKILL, 0 },
    {405, "Rite of Passage",     "Complete the Ancients quest",        QTYPE_QUESTFLAG, 39, 0,0, FALSE, REWARD_SKILL, 0 },
    {406, "Eve of Destruction",  "Complete the Baal quest",            QTYPE_QUESTFLAG, 40, 0,0, FALSE, REWARD_SKILL, 0 },
    /* SuperUnique hunting (PROGRESSION) — hcIdx from SuperUniques.txt */
    {470, "Hunt: Shenk the Overseer", "Kill Shenk the Overseer",             QTYPE_SUPERUNIQUE, 42, 0,0, FALSE, REWARD_SKILL, 0 },
    {471, "Hunt: Dac Farren",         "Kill Dac Farren",                     QTYPE_SUPERUNIQUE, 48, 0,0, FALSE, REWARD_SKILL, 0 },
    {472, "Hunt: Bonesaw Breaker",    "Kill Bonesaw Breaker",                QTYPE_SUPERUNIQUE, 47, 0,0, FALSE, REWARD_SKILL, 0 },
    {473, "Hunt: Eyeback Unleashed",  "Kill Eyeback the Unleashed",          QTYPE_SUPERUNIQUE, 50, 0,0, FALSE, REWARD_SKILL, 0 },
    {474, "Hunt: Threash Socket",     "Kill Threash Socket",                 QTYPE_SUPERUNIQUE, 51, 0,0, FALSE, REWARD_SKILL, 0 },
    {475, "Hunt: Pindleskin",         "Kill Pindleskin",                     QTYPE_SUPERUNIQUE, 52, 0,0, FALSE, REWARD_SKILL, 0 },
    {476, "Hunt: Snapchip Shatter",   "Kill Snapchip Shatter",              QTYPE_SUPERUNIQUE, 53, 0,0, FALSE, REWARD_SKILL, 0 },
    {477, "Hunt: Frozenstein",        "Kill Frozenstein",                    QTYPE_SUPERUNIQUE, 59, 0,0, FALSE, REWARD_SKILL, 0 },
    /* Level milestones (PROGRESSION) */
    /* Level milestones moved to Act 1 (Normal), Act 2 (NM), Act 3 (Hell) */
    /* Kill quests — FILLER */
    {410, "Clear Bloody Foothills",    "Kill 30 monsters in Bloody Foothills",    QTYPE_KILL, 110, 30,0, FALSE, REWARD_GOLD, 0 },
    {411, "Clear Frigid Highlands",    "Kill 30 monsters in Frigid Highlands",    QTYPE_KILL, 111, 30,0, FALSE, REWARD_GOLD, 0 },
    {412, "Clear Arreat Plateau",      "Kill 30 monsters in Arreat Plateau",      QTYPE_KILL, 112, 30,0, FALSE, REWARD_GOLD, 0 },
    {413, "Clear Crystalline Passage", "Kill 20 monsters in Crystalline Passage", QTYPE_KILL, 113, 20,0, FALSE, REWARD_GOLD, 0 },
    /* V7 (T6) — names corrected. */
    {414, "Clear The Ancients' Way",   "Kill 20 monsters in The Ancients' Way",   QTYPE_KILL, 118, 20,0, FALSE, REWARD_GOLD, 0 },
    {415, "Clear Icy Cellar",          "Kill 20 monsters in the Icy Cellar",      QTYPE_KILL, 119, 20,0, FALSE, REWARD_GOLD, 0 },
    {416, "Clear Frozen Tundra",       "Kill 25 monsters in the Frozen Tundra",   QTYPE_KILL, 117, 25,0, FALSE, REWARD_GOLD, 0 },
    {417, "Clear Halls of Anguish",    "Kill 20 monsters in Halls of Anguish",    QTYPE_KILL, 122, 20,0, FALSE, REWARD_GOLD, 0 },
    {418, "Clear Halls of Pain",       "Kill 20 monsters in the Halls of Pain",   QTYPE_KILL, 123, 20,0, FALSE, REWARD_GOLD, 0 },
    {419, "Clear Halls of Vaught",     "Kill 20 monsters in Halls of Vaught",     QTYPE_KILL, 124, 20,0, FALSE, REWARD_GOLD, 0 },
    {420, "Clear Worldstone L1",       "Kill 30 monsters in Worldstone Keep 1",   QTYPE_KILL, 128, 30,0, FALSE, REWARD_GOLD, 0 },
    {421, "Clear Worldstone L2",       "Kill 35 monsters in Worldstone Keep 2",   QTYPE_KILL, 129, 35,0, FALSE, REWARD_GOLD, 0 },
    {422, "Clear Worldstone L3",       "Kill 40 monsters in Worldstone Keep 3",   QTYPE_KILL, 130, 40,0, FALSE, REWARD_GOLD, 0 },
    {423, "Clear Throne of Destruction","Kill 40 monsters in Throne of Destruction",QTYPE_KILL,131,40,0, FALSE, REWARD_GOLD, 0 },
    {430, "Clear Cellar of Pity",             "Kill 10 monsters in Cellar of Pity",               QTYPE_KILL, 114, 10,0, FALSE, REWARD_GOLD, 0 },
    {431, "Clear Crystalized Cavern L2",      "Kill 15 monsters in Crystalized Cavern L2",        QTYPE_KILL, 115, 15,0, FALSE, REWARD_GOLD, 0 },
    {432, "Clear Echo Chamber",               "Kill 8 monsters in Echo Chamber",                  QTYPE_KILL, 116, 8,0, FALSE, REWARD_GOLD, 0 },
    {433, "Clear Abaddon",                    "Kill 20 monsters in Abaddon",                      QTYPE_KILL, 125, 20,0, FALSE, REWARD_GOLD, 0 },
    {434, "Clear Pit of Acheron",             "Kill 20 monsters in Pit of Acheron",               QTYPE_KILL, 126, 20,0, FALSE, REWARD_GOLD, 0 },
    {435, "Clear Infernal Pit",               "Kill 20 monsters in Infernal Pit",                 QTYPE_KILL, 127, 20,0, FALSE, REWARD_GOLD, 0 },
    /* Area entry — FILLER */
    {440, "Enter Bloody Foothills",    "Enter the Bloody Foothills",              QTYPE_AREA, 110, 0,0, FALSE, REWARD_GOLD, 0 },
    {441, "Enter Frigid Highlands",    "Enter the Frigid Highlands",              QTYPE_AREA, 111, 0,0, FALSE, REWARD_GOLD, 0 },
    {442, "Enter Arreat Plateau",      "Enter the Arreat Plateau",                QTYPE_AREA, 112, 0,0, FALSE, REWARD_GOLD, 0 },
    {443, "Enter Crystalline Passage", "Enter the Crystalline Passage",           QTYPE_AREA, 113, 0,0, FALSE, REWARD_GOLD, 0 },
    {444, "Enter Worldstone Keep",     "Enter the Worldstone Keep",               QTYPE_AREA, 128, 0,0, FALSE, REWARD_GOLD, 0 },
    /* Waypoint quests — FILLER */
    {450, "Frigid Highlands Waypoint",   "Activate the Frigid Highlands waypoint",     QTYPE_WAYPOINT, 31, 0,0, FALSE, REWARD_GOLD, 0 },
    {451, "Arreat Plateau Waypoint",     "Activate the Arreat Plateau waypoint",       QTYPE_WAYPOINT, 32, 0,0, FALSE, REWARD_GOLD, 0 },
    {452, "Crystalline Passage Waypoint","Activate the Crystalline Passage waypoint",  QTYPE_WAYPOINT, 33, 0,0, FALSE, REWARD_GOLD, 0 },
    /* {453, "Frozen River Waypoint", "Activate the Frozen River waypoint", QTYPE_WAYPOINT, 34, 0,0, FALSE, REWARD_GOLD, 0 }, */ /* Removed: no WP in Frozen River */
    {454, "Halls of Pain Waypoint",      "Activate the Halls of Pain waypoint",        QTYPE_WAYPOINT, 35, 0,0, FALSE, REWARD_GOLD, 0 },
    {455, "Glacial Trail Waypoint",      "Activate the Glacial Trail waypoint",        QTYPE_WAYPOINT, 34, 0,0, FALSE, REWARD_GOLD, 0 },
    /* waypoint index 37 is The Ancients' Way, not the Frozen Tundra. */
    {456, "Ancients' Way Waypoint",      "Activate the Ancients' Way waypoint",        QTYPE_WAYPOINT, 37, 0,0, FALSE, REWARD_GOLD, 0 },
    {457, "Worldstone Keep 2 Waypoint",  "Activate the Worldstone Keep 2 waypoint",    QTYPE_WAYPOINT, 38, 0,0, FALSE, REWARD_GOLD, 0 },
};

typedef struct {
    const char* name;
    Quest*      quests;
    int         num;
} ActData;

static ActData g_acts[5] = {
    { "Act I",   g_act1Quests, sizeof(g_act1Quests)/sizeof(g_act1Quests[0]) },
    { "Act II",  g_act2Quests, sizeof(g_act2Quests)/sizeof(g_act2Quests[0]) },
    { "Act III", g_act3Quests, sizeof(g_act3Quests)/sizeof(g_act3Quests[0]) },
    { "Act IV",  g_act4Quests, sizeof(g_act4Quests)/sizeof(g_act4Quests[0]) },
    { "Act V",   g_act5Quests, sizeof(g_act5Quests)/sizeof(g_act5Quests[0]) },
};

/* 2.x (roadmap #5/#6) — custom-goal SUBSYSTEM completion, computed from the REAL quest catalog (g_acts[][]) instead of the old hardcoded qid ranges in d2arch_customgoal.c. */
BOOL Quests_IsTypeCompleteOnDiff(int qtype, int diff) {
    if (diff < 0 || diff > 2) return FALSE;
    int total = 0;
    for (int a = 0; a < 5; a++) {
        for (int q = 0; q < g_acts[a].num; q++) {
            Quest* qq = &g_acts[a].quests[q];
            if ((int)qq->type != qtype) continue;
            total++;
            if (qq->id < 0 || qq->id >= MAX_QUEST_ID) return FALSE;
            if (!g_questCompleted[diff][qq->id]) return FALSE;
        }
    }
    return (total > 0);
}
BOOL Quests_IsTypeCompleteAnyDiff(int qtype) {
    int total = 0;
    for (int a = 0; a < 5; a++) {
        for (int q = 0; q < g_acts[a].num; q++) {
            Quest* qq = &g_acts[a].quests[q];
            if ((int)qq->type != qtype) continue;
            total++;
            if (qq->id < 0 || qq->id >= MAX_QUEST_ID) return FALSE;
            if (!(g_questCompleted[0][qq->id] || g_questCompleted[1][qq->id]
                  || g_questCompleted[2][qq->id])) return FALSE;
        }
    }
    return (total > 0);
}
/* Named wrappers for the custom-goal subsystem tokens. */
BOOL Quests_AllHuntsComplete(void)     { return Quests_IsTypeCompleteOnDiff(QTYPE_SUPERUNIQUE, 0); }
BOOL Quests_AllKillZonesComplete(void) { return Quests_IsTypeCompleteOnDiff(QTYPE_KILL, 0); }
BOOL Quests_AllExploreComplete(void)   { return Quests_IsTypeCompleteOnDiff(QTYPE_AREA, 0); }
BOOL Quests_AllWaypointsComplete(void) { return Quests_IsTypeCompleteOnDiff(QTYPE_WAYPOINT, 0); }
BOOL Quests_AllLevelMsComplete(void)   { return Quests_IsTypeCompleteAnyDiff(QTYPE_LEVEL); }
BOOL Quests_AllStoryComplete(int diff) { return Quests_IsTypeCompleteOnDiff(QTYPE_QUESTFLAG, diff); }

/* Total / completed quest counts now respect both: - Enabled quest TYPES (Story / Hunt / Kill / Area / WP / Lvl) - Enabled DIFFICULTIES (Goal=Normal -> ×1, Goal=NM -> ×2, Goal=Hell -> ×3) because each (qid, diff) pair is a distinct AP location. */
static BOOL IsQuestTypeActive(int questType);

static int TotalQuests(void) {
    int diffsEnabled = g_apDiffScope + 1;
    if (diffsEnabled < 1) diffsEnabled = 1;
    if (diffsEnabled > 3) diffsEnabled = 3;
    int perDiff = 0;
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++) {
            Quest* qq = &g_acts[a].quests[q];
            if (qq->id <= 0) continue;
            if (!IsQuestTypeActive((int)qq->type)) continue;
            perDiff++;
        }
    return perDiff * diffsEnabled;
}

static int CompletedQuests(void) {
    int diffsEnabled = g_apDiffScope + 1;
    if (diffsEnabled < 1) diffsEnabled = 1;
    if (diffsEnabled > 3) diffsEnabled = 3;
    int n = 0;
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++) {
            Quest* qq = &g_acts[a].quests[q];
            int qid = qq->id;
            if (qid <= 0 || qid >= MAX_QUEST_ID) continue;
            if (!IsQuestTypeActive((int)qq->type)) continue;
            for (int d = 0; d < diffsEnabled; d++)
                if (g_questCompleted[d][qid]) n++;
        }
    return n;
}

/* Reward-redesign helper functions and catalogs Pre-roll model: AssignAllRewards picks each filler quest's REWARD_TYPE first, then resolves the SPECIFIC value (gold amount, xp amount, trap variant, boss id, charm/set/unique pick) and stashes it in parallel arrays. */

/* Names used by the spoiler file. */
static const char* Quests_TrapTypeName(int t) {
    switch (t) {
        case TRAP_MONSTERS: return "Trap: Monsters";
        case TRAP_SLOW:     return "Trap: Slow (Decrepify)";
        case TRAP_WEAKEN:   return "Trap: Weaken (Amplify Damage)";
        case TRAP_POISON:   return "Trap: Poison";
        case TRAP_BARRELS:  return "Trap: Barrel Field";
        case TRAP_URNS:     return "Trap: Urn Garden";
        case TRAP_TREASURE: return "Trap: False Treasure";
        case TRAP_JUNK:     return "Trap: Junk Backpack";
        default:            return "Trap: Unknown";
    }
}
static const char* Quests_BossLootName(int b) {
    if (b < 0 || b >= BOSS_LOOT_COUNT) return "Boss Loot";
    return g_bossLootNames[b];
}
static const char* Quests_CharmName(int c) {
    switch (c) {
        case 0: return "Magic Small Charm";
        case 1: return "Magic Large Charm";
        case 2: return "Magic Grand Charm";
        default: return "Magic Charm";
    }
}
/* Set piece name lookup. */
extern const char* Coll_GetSetPieceName(int idx);
static const char* Quests_SetPieceName(int idx) {
    return Coll_GetSetPieceName(idx);
}

/* Unique catalog — parsed from data/global/excel/UniqueItems.txt at first use. */
/* The row's `lvl` from UniqueItems.txt — the item level the engine demands
 * before it will roll this unique at all. 0 when the index is out of range. */
int Quests_UniqueQlvl(int idx) {
    if (!g_uniqueCatalogLoaded) Quests_LoadUniqueCatalog();
    if (idx < 0 || idx >= g_uniqueCatalogCount) return 0;
    return g_uniqueCatalog[idx].reqLvl;
}

static const char* Quests_UniqueName(int idx) {
    if (!g_uniqueCatalogLoaded) Quests_LoadUniqueCatalog();
    if (idx < 0 || idx >= g_uniqueCatalogCount) return "Random Unique";
    return g_uniqueCatalog[idx].name;
}

/* split a tab-separated line into fields, PRESERVING empty columns between consecutive tabs. */
static int Quests_SplitTSV(char* s, char* fields[], int maxFields) {
    int n = 0;
    char* p = s;
    if (!p) return 0;
    fields[n++] = p;
    while (*p && n < maxFields) {
        if (*p == '\t') {
            *p = 0;
            fields[n++] = p + 1;
        }
        p++;
    }
    /* Strip trailing CR / LF on the final field */
    if (n > 0) {
        char* last = fields[n - 1];
        char* end  = last + strlen(last);
        while (end > last && (end[-1] == '\r' || end[-1] == '\n')) {
            *(--end) = 0;
        }
    }
    return n;
}

static void Quests_LoadUniqueCatalog(void) {
    if (g_uniqueCatalogLoaded) return;
    g_uniqueCatalogLoaded = TRUE;
    g_uniqueCatalogCount = 0;

    /* Resolve game data dir relative to the running exe. */
    char gamePath[MAX_PATH];
    GetModuleFileNameA(NULL, gamePath, MAX_PATH);
    char* slash = strrchr(gamePath, '\\');
    if (!slash) { Log("Quests_LoadUniqueCatalog: cannot find game dir\n"); return; }
    *(slash + 1) = 0;
    strcat(gamePath, "data\\global\\excel\\UniqueItems.txt");

    FILE* f = fopen(gamePath, "r");
    if (!f) {
        Log("Quests_LoadUniqueCatalog: %s not found — random-unique drops disabled\n", gamePath);
        return;
    }

    /* Locate columns by header name so we don't break if the user has a modded txt with reordered columns. */
    char header[8192];
    if (!fgets(header, sizeof(header), f)) { fclose(f); return; }
    int colName = -1, colCode = -1, colLvl = -1, colEnabled = -1;
    {
        char* hdrFields[300] = {0};
        int   hdrCount = Quests_SplitTSV(header, hdrFields, 300);
        for (int idx = 0; idx < hdrCount; idx++) {
            const char* tok = hdrFields[idx];
            if (!tok) continue;
            if (_stricmp(tok, "index") == 0)         colName    = idx;
            else if (_stricmp(tok, "code") == 0)     colCode    = idx;
            else if (_stricmp(tok, "lvl") == 0)      colLvl     = idx;
            else if (_stricmp(tok, "enabled") == 0)  colEnabled = idx;
        }
    }
    if (colName < 0 || colCode < 0) {
        Log("Quests_LoadUniqueCatalog: missing required columns (name=%d code=%d)\n",
            colName, colCode);
        fclose(f);
        return;
    }

    int rowIdx = 0;
    char line[8192];
    while (fgets(line, sizeof(line), f) && g_uniqueCatalogCount < UNIQUE_CAT_MAX) {
        char* fields[300] = {0};
        int   fieldCount = Quests_SplitTSV(line, fields, 300);

        const char* name    = (colName     < fieldCount) ? fields[colName]    : NULL;
        const char* code    = (colCode     < fieldCount) ? fields[colCode]    : NULL;
        const char* lvl     = (colLvl      < fieldCount) ? fields[colLvl]     : NULL;
        const char* enabled = (colEnabled  < fieldCount) ? fields[colEnabled] : NULL;

        rowIdx++;
        if (!name || !name[0]) continue;
        if (!code || !code[0] || strlen(code) > 3) continue;
        if (enabled && enabled[0] && enabled[0] != '1') continue;
        /* Skip the "Expansion" divider rows (no code, just a marker). */
        if (strcmp(name, "Expansion") == 0) continue;

        UniqueCatalogEntry* e = &g_uniqueCatalog[g_uniqueCatalogCount++];
        e->rowIdx = rowIdx - 1;  /* match in-game row index */
        strncpy(e->name, name, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = 0;
        memset(e->baseCode, 0, sizeof(e->baseCode));
        strncpy(e->baseCode, code, 3);
        e->reqLvl = (lvl && lvl[0]) ? atoi(lvl) : 1;
    }
    fclose(f);
    Log("Quests_LoadUniqueCatalog: loaded %d uniques from UniqueItems.txt\n",
        g_uniqueCatalogCount);
}

/* EQUIPMENT SLOTS — so a drop can say "boots" instead of "something".
 *
 * A set or unique row names only a base code; the slot lives in that base's
 * `type` column over in armor.txt / weapons.txt. The map below is built from
 * those files at runtime for the same reason the unique catalog is: a modded
 * txt is allowed to disagree with anything hardcoded here, and a wrong slot
 * silently hands out a helmet when the item said gloves.
 *
 * Type tokens read out of the shipped armor.txt, not remembered:
 *   tors                     -> Armor
 *   helm, pelt, phlm, circ   -> Helm       (druid pelts and barbarian helms
 *                                           are helms to the player)
 *   shie, ashd, head         -> Shield     (paladin shields, necro heads)
 *   glov / boot / belt       -> themselves
 * Everything in weapons.txt is a weapon; ring and amul from misc.txt are
 * jewelry. */
#define ARCH_SLOT_ARMOR    0
#define ARCH_SLOT_HELM     1
#define ARCH_SLOT_SHIELD   2
#define ARCH_SLOT_GLOVES   3
#define ARCH_SLOT_BOOTS    4
#define ARCH_SLOT_BELT     5
#define ARCH_SLOT_WEAPON   6
#define ARCH_SLOT_JEWELRY  7
#define ARCH_SLOT_COUNT    8

/* isQuest mirrors the `quest` column the item tables already carry (misc.txt
 * col 48, weapons 66, armor 60): non-empty/non-zero means the base is a quest
 * item. Tarlach was handed "Amulet of the Viper" (code `vip`, quest=10) as a
 * starting unique — a drop that can break the Act 2 chain. */
typedef struct { char code[4]; BYTE slot; BYTE isQuest; } SlotMapEntry;
/* 511 built-in rows plus whatever a modded install adds on top. The old 900
 * was sized for files alone and would silently truncate a mod's own tables. */
#define SLOT_MAP_MAX 2048
static SlotMapEntry g_slotMap[SLOT_MAP_MAX];
static int  g_slotMapCount = 0;
static BOOL g_slotMapLoaded = FALSE;

static int Quests_SlotFromArmorType(const char* t) {
    if (!t || !t[0]) return -1;
    if (_stricmp(t, "tors") == 0) return ARCH_SLOT_ARMOR;
    if (_stricmp(t, "helm") == 0 || _stricmp(t, "pelt") == 0 ||
        _stricmp(t, "phlm") == 0 || _stricmp(t, "circ") == 0) return ARCH_SLOT_HELM;
    if (_stricmp(t, "shie") == 0 || _stricmp(t, "ashd") == 0 ||
        _stricmp(t, "head") == 0) return ARCH_SLOT_SHIELD;
    if (_stricmp(t, "glov") == 0) return ARCH_SLOT_GLOVES;
    if (_stricmp(t, "boot") == 0) return ARCH_SLOT_BOOTS;
    if (_stricmp(t, "belt") == 0) return ARCH_SLOT_BELT;
    if (_stricmp(t, "ring") == 0 || _stricmp(t, "amul") == 0) return ARCH_SLOT_JEWELRY;
    return -1;
}

/* forceSlot >= 0 assigns every row in the file that slot (weapons.txt);
 * otherwise the row's own `type` column decides. */
static void Quests_LoadSlotFile(const char* relExcel, int forceSlot) {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (!slash) return;
    *(slash + 1) = 0;
    strcat(path, relExcel);
    FILE* f = fopen(path, "r");
    if (!f) { Log("Quests_LoadSlotFile: %s not found\n", path); return; }

    char header[8192];
    if (!fgets(header, sizeof(header), f)) { fclose(f); return; }
    int colCode = -1, colType = -1, colQuest = -1;
    {
        char* hf[400] = {0};
        int hc = Quests_SplitTSV(header, hf, 400);
        for (int i = 0; i < hc; i++) {
            if (!hf[i]) continue;
            if      (_stricmp(hf[i], "code") == 0 && colCode < 0) colCode = i;
            else if (_stricmp(hf[i], "type") == 0 && colType < 0) colType = i;
            else if (_stricmp(hf[i], "quest") == 0 && colQuest < 0) colQuest = i;
        }
    }
    if (colCode < 0) { fclose(f); return; }

    int added = 0;
    char line[8192];
    while (fgets(line, sizeof(line), f) && g_slotMapCount < SLOT_MAP_MAX) {
        char* fl[400] = {0};
        int fc = Quests_SplitTSV(line, fl, 400);
        const char* code = (colCode < fc) ? fl[colCode] : NULL;
        if (!code || !code[0] || strlen(code) > 3) continue;

        int slot = forceSlot;
        if (slot < 0) {
            const char* t = (colType >= 0 && colType < fc) ? fl[colType] : NULL;
            slot = Quests_SlotFromArmorType(t);
        }
        if (slot < 0) continue;

        const char* qf = (colQuest >= 0 && colQuest < fc) ? fl[colQuest] : NULL;
        SlotMapEntry* e = &g_slotMap[g_slotMapCount++];
        memset(e->code, 0, sizeof(e->code));
        strncpy(e->code, code, 3);
        e->slot = (BYTE)slot;
        e->isQuest = (qf && qf[0] && qf[0] != '0') ? 1 : 0;
        added++;
    }
    fclose(f);
    Log("Quests_LoadSlotFile: %s -> %d bases\n", relExcel, added);
}

/* The built-in floor: code -> slot for every vanilla base, generated by
 * Tools/gen_slotmap.py. See its header for why this is compiled in.
 *
 * The short version: since 3.7.6 the packer leaves the unmodified vanilla
 * tables out of the release because they are Blizzard's. The ENGINE does not
 * mind -- it reads them from the player's own MPQ archives -- but fopen() has
 * no way into an archive, so on every real installation this map loaded the
 * three codes in misc.txt and nothing else. Every slot-filtered reward then
 * fell through to an unfiltered roll: the player got an item, but not the kind
 * the multiworld said it was sending. Silent, and true for a whole release. */
#include "d2arch_slotmap.h"

static void Quests_LoadSlotMap(void) {
    if (g_slotMapLoaded) return;
    g_slotMapLoaded = TRUE;

    for (int i = 0; i < BUILTIN_SLOT_COUNT && g_slotMapCount < SLOT_MAP_MAX; i++) {
        SlotMapEntry* e = &g_slotMap[g_slotMapCount++];
        memset(e->code, 0, sizeof(e->code));
        strncpy(e->code, g_builtinSlots[i].code, 3);
        e->slot    = g_builtinSlots[i].slot;
        e->isQuest = g_builtinSlots[i].isQuest;
    }
    Log("Quests_LoadSlotMap: %d built-in base codes\n", g_slotMapCount);

    /* Files still win where they exist. They are appended after the built-in
     * rows, and both lookups below scan BACKWARDS, so a modded install's own
     * armor.txt overrides the floor instead of being shadowed by it. */
    Quests_LoadSlotFile("data\\global\\excel\\armor.txt",   -1);
    Quests_LoadSlotFile("data\\global\\excel\\weapons.txt", ARCH_SLOT_WEAPON);
    /* misc.txt carries ring and amul, which the type column already names. */
    Quests_LoadSlotFile("data\\global\\excel\\misc.txt",    -1);
    Log("Quests_LoadSlotMap: %d base codes mapped to slots\n", g_slotMapCount);
}

/* Backwards: the built-in rows are loaded first and any file rows after them,
 * so the last match is the most specific one a given install has. */
static int Quests_SlotOfCode(const char* code) {
    if (!code || !code[0]) return -1;
    Quests_LoadSlotMap();
    for (int i = g_slotMapCount - 1; i >= 0; i--) {
        if (strncmp(g_slotMap[i].code, code, 3) == 0) return g_slotMap[i].slot;
    }
    return -1;
}

/* A quest base must never be handed out as loot: the game tracks these by
 * possession, so a spare Amulet of the Viper / Khalim organ / Horadric Staff
 * piece in the player's bag is at best confusing and at worst breaks the
 * chain it belongs to. */
static BOOL Quests_CodeIsQuestItem(const char* code) {
    if (!code || !code[0]) return FALSE;
    Quests_LoadSlotMap();
    for (int i = g_slotMapCount - 1; i >= 0; i--) {   /* newest row wins */
        if (strncmp(g_slotMap[i].code, code, 3) == 0) return g_slotMap[i].isQuest != 0;
    }
    return FALSE;
}

/* Pick a random catalog index whose base sits in `slot`.
 *
 * Returns -1 when the slot is empty rather than picking something else, so the
 * caller can fall back to an unfiltered roll. Handing out nothing is the one
 * outcome that must never happen: a reward that vanishes looks identical to a
 * bug in the item pipeline, and we have chased that ghost more than once.
 *
 * Reservoir sampling, so the whole catalog is only walked once and no second
 * array has to be kept in sync with it. */
static int Quests_PickSetPieceBySlot(int slot) {
    int chosen = -1, seen = 0;
    for (int i = 0; ; i++) {
        const char* code = Coll_GetSetPieceCode(i);
        if (!code) break;              /* past the end of the set catalog */
        if (!code[0]) continue;
        if (Quests_CodeIsQuestItem(code)) continue;
        if (Quests_SlotOfCode(code) != slot) continue;
        seen++;
        if (rand() % seen == 0) chosen = i;
    }
    return chosen;
}

static int Quests_PickUniqueBySlot(int slot) {
    if (!g_uniqueCatalogLoaded) Quests_LoadUniqueCatalog();
    int chosen = -1, seen = 0;
    for (int i = 0; i < g_uniqueCatalogCount; i++) {
        if (Quests_CodeIsQuestItem(g_uniqueCatalog[i].baseCode)) continue;
        if (Quests_SlotOfCode(g_uniqueCatalog[i].baseCode) != slot) continue;
        seen++;
        if (rand() % seen == 0) chosen = i;
    }
    return chosen;
}

/* Any unique, quest bases excluded. The three callers that roll a unique
 * WITHOUT a slot constraint (bonus checks, extra checks, quest reward setup)
 * used a bare `rand() % g_uniqueCatalogCount`, which could land on a quest
 * base just as easily as the slot picker could. Returns 0 only if the catalog
 * has nothing usable — same "never hand out nothing" contract as the rest. */
static int Quests_PickAnyUnique(void) {
    if (!g_uniqueCatalogLoaded) Quests_LoadUniqueCatalog();
    int chosen = -1, seen = 0;
    for (int i = 0; i < g_uniqueCatalogCount; i++) {
        if (Quests_CodeIsQuestItem(g_uniqueCatalog[i].baseCode)) continue;
        seen++;
        if (rand() % seen == 0) chosen = i;
    }
    return (chosen >= 0) ? chosen : 0;
}

/* 2.10.0 BOSS-LOOT POOLS — each boss drops from its OWN treasure class. */
typedef struct { char code[4]; short level; } BLItem;
#define BL_MAX 400
static BLItem g_blWeap[BL_MAX]; static int g_blWeapN = 0;
static BLItem g_blArmo[BL_MAX]; static int g_blArmoN = 0;
static BOOL   g_blLoaded = FALSE;
/* Item-level bands per boss, taken from the resolved Hell boss TCs. */
static const int g_bossBandLo[BOSS_LOOT_COUNT] = { 39, 42, 48, 51, 54 };
static const int g_bossBandHi[BOSS_LOOT_COUNT] = { 69, 72, 78, 84, 87 };

/* Parse one item table (Weapons.txt / Armor.txt) for spawnable bases with a level, by header name (robust to column reordering). */
static void BossLoot_LoadFile(const char* relExcel, BLItem* arr, int* pn) {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (!slash) return;
    *(slash + 1) = 0;
    strcat(path, relExcel);
    FILE* f = fopen(path, "r");
    if (!f) { Log("BossLoot_LoadFile: %s not found\n", path); return; }
    char header[8192];
    if (!fgets(header, sizeof(header), f)) { fclose(f); return; }
    int colCode = -1, colLvl = -1, colSpawn = -1;
    {
        char* hf[400] = {0};
        int hc = Quests_SplitTSV(header, hf, 400);
        for (int i = 0; i < hc; i++) {
            if (!hf[i]) continue;
            if      (_stricmp(hf[i], "code") == 0)                  colCode  = i;
            else if (_stricmp(hf[i], "level") == 0 && colLvl < 0)   colLvl   = i;
            else if (_stricmp(hf[i], "spawnable") == 0)             colSpawn = i;
        }
    }
    if (colCode < 0 || colLvl < 0) { fclose(f); return; }
    char line[8192];
    while (fgets(line, sizeof(line), f) && *pn < BL_MAX) {
        char* fl[400] = {0};
        int fc = Quests_SplitTSV(line, fl, 400);
        const char* code = (colCode < fc) ? fl[colCode] : NULL;
        const char* lvl  = (colLvl  < fc) ? fl[colLvl]  : NULL;
        const char* spn  = (colSpawn >= 0 && colSpawn < fc) ? fl[colSpawn] : NULL;
        if (!code || !code[0] || strlen(code) > 3) continue;
        if (spn && spn[0] && spn[0] != '1') continue;   /* spawnable only */
        int L = (lvl && lvl[0]) ? atoi(lvl) : 0;
        if (L <= 0) continue;
        BLItem* e = &arr[(*pn)++];
        memset(e->code, 0, sizeof(e->code));
        strncpy(e->code, code, 3);
        e->level = (short)L;
    }
    fclose(f);
}

static void BossLoot_LoadItemDB(void) {
    if (g_blLoaded) return;
    g_blLoaded = TRUE;
    BossLoot_LoadFile("data\\global\\excel\\Weapons.txt", g_blWeap, &g_blWeapN);
    BossLoot_LoadFile("data\\global\\excel\\Armor.txt",   g_blArmo, &g_blArmoN);
    Log("BossLoot_LoadItemDB: %d weapons, %d armor bases\n", g_blWeapN, g_blArmoN);
}

/* Pick a random spawnable base for a boss's item-level band: ~12%% jewelry (rin/amu/jew), else weapon/armor within [lo,hi]. */
static DWORD BossLoot_PickBase(int lo, int hi, unsigned int r) {
    if (!g_blLoaded) BossLoot_LoadItemDB();
    int kind = (int)(r % 100);
    if (kind < 12) {
        static const char* jw[3] = { "rin", "amu", "jew" };
        return Quests_PackCode(jw[(r >> 7) % 3]);
    }
    BLItem* arr; int n;
    if (kind < 56) { arr = g_blWeap; n = g_blWeapN; }
    else           { arr = g_blArmo; n = g_blArmoN; }
    if (n <= 0) return Quests_PackCode("rin");
    /* rejection-sample within the band */
    for (int t = 0; t < 20; t++) {
        unsigned int rr = r * 1664525u + 1013904223u + (unsigned)t * 2654435761u;
        int i = (int)(rr % (unsigned)n);
        if (arr[i].level >= lo && arr[i].level <= hi)
            return Quests_PackCode(arr[i].code);
    }
    /* band starved — accept any base ≤ hi as graceful fallback */
    for (int t = 0; t < 20; t++) {
        unsigned int rr = r * 22695477u + 1u + (unsigned)t * 40503u;
        int i = (int)(rr % (unsigned)n);
        if (arr[i].level <= hi) return Quests_PackCode(arr[i].code);
    }
    return Quests_PackCode("rin");
}

/* REWARD ASSIGNMENT — progression first, then filler 1.9.0 redesign: - Filler types expanded with REWARD_XP and three REWARD_DROP_* variants (charm/set/unique). */
static void AssignAllRewards(DWORD seed) {
    srand(seed + 777);

    memset(g_questRewardType, 0, sizeof(g_questRewardType));
    memset(g_questGold, 0, sizeof(g_questGold));
    memset(g_questXP, 0, sizeof(g_questXP));
    memset(g_questExtra, 0, sizeof(g_questExtra));

    /* Lazy-load the unique catalog so AssignAllRewards can roll REWARD_DROP_UNIQUE indices into a known-valid range. */
    Quests_LoadUniqueCatalog();

    /* --- Step 1: Deterministic reward assignment based on quest type --- */
    int cntProgAct[5] = {0}, cntFillerAct[5] = {0};
    for (int a = 0; a < 5; a++) {
        for (int q = 0; q < g_acts[a].num; q++) {
            Quest* quest = &g_acts[a].quests[q];
            /* Progression quests only get REWARD_SKILL when Skill Hunting is ON. */
            BOOL isProgQuestType = (quest->type == QTYPE_QUESTFLAG ||
                                    quest->type == QTYPE_SUPERUNIQUE ||
                                    quest->type == QTYPE_LEVEL);
            if (isProgQuestType && g_skillHuntingOn) {
                quest->reward = REWARD_SKILL;
                cntProgAct[a]++;
            } else {
                quest->reward = REWARD_GOLD; /* placeholder for filler */
                cntFillerAct[a]++;
            }
        }
    }

    {
        int totalProg = 0, totalFiller = 0;
        for (int a = 0; a < 5; a++) { totalProg += cntProgAct[a]; totalFiller += cntFillerAct[a]; }
        Log("AssignAllRewards: %d progression, %d filler across %d quests\n", totalProg, totalFiller, totalProg + totalFiller);
        for (int a = 0; a < 5; a++)
            Log("  Act %d: %d quests, %d progression, %d filler\n",
                a+1, g_acts[a].num, cntProgAct[a], cntFillerAct[a]);
    }

    /* --- Step 2: Assign filler rewards for non-progression quests --- */
    for (int diff = 0; diff < 3; diff++) {
        for (int a = 0; a < 5; a++) {
            for (int q = 0; q < g_acts[a].num; q++) {
                Quest* quest = &g_acts[a].quests[q];
                int qid = quest->id;
                if (qid <= 0 || qid >= MAX_QUEST_ID) continue;

                if (quest->reward == REWARD_SKILL) {
                    /* Progression quest — no filler override needed */
                    g_questRewardType[diff][qid] = REWARD_SKILL;
                    g_questGold[diff][qid] = 0;
                    continue;
                }

                /* 1.9.0 flat weighted picker — mirrors the apworld's _build_filler_weights table so standalone and AP fillers produce the same item mix.
                 *
                 * lo/hi are the same 1-15 depth bands tierfill.py uses on the AP
                 * side. Here they cost nothing to apply: this loop already knows
                 * the difficulty and the act, so a row simply drops out of the
                 * roll for quests outside its band. AP has to reserve locations
                 * to achieve the same thing; standalone gets it for free.
                 *
                 * A row with lo=0 is unbanded and can come up anywhere. */
                struct WRow { int weight; int rewardType; int extraIdx; int lo, hi; };
                int depth = diff * 5 + (a + 1);
                struct WRow wtable[] = {
                    { 10,  REWARD_GOLD,        0, 0, 0 },
                    { 15,  REWARD_XP,          0, 0, 0 },
                    { 10,  REWARD_STAT,        5, 0, 0 },
                    /* 1.9.4 fix: gate REWARD_SKILL on skill_hunting toggle. */
                    { (g_skillHuntingOn ? 10 : 0), REWARD_SKILL,  1, 0, 0 },
                    { (g_skillHuntingOn ? 5 : 0), REWARD_RESETPT, 1, 0, 0 },
                    {  2,  REWARD_TRAP,        TRAP_MONSTERS, 0, 0 },
                    {  1,  REWARD_TRAP,        TRAP_SLOW,     0, 0 },
                    {  1,  REWARD_TRAP,        TRAP_WEAKEN,   0, 0 },
                    {  1,  REWARD_TRAP,        TRAP_POISON,   0, 0 },
                    {  2,  REWARD_TRAP,        TRAP_BARRELS,  0, 0 },
                    {  2,  REWARD_TRAP,        TRAP_URNS,     0, 0 },
                    {  2,  REWARD_TRAP,        TRAP_TREASURE, 0, 0 },
                    {  2,  REWARD_TRAP,        TRAP_JUNK,     0, 0 },
                    {  1,  REWARD_LOOT,        0, 0, 0 },  /* Andariel */
                    {  2,  REWARD_LOOT,        1, 0, 0 },  /* Duriel */
                    {  2,  REWARD_LOOT,        2, 0, 0 },  /* Mephisto */
                    {  1,  REWARD_LOOT,        3, 0, 0 },  /* Diablo */
                    {  1,  REWARD_LOOT,        4, 0, 0 },  /* Baal */
                    {  9,  REWARD_DROP_CHARM,  0, 0, 0 },  /* picked at roll-time below */
                    {  9,  REWARD_DROP_SET,    0, 0, 0 },
                    {  6,  REWARD_DROP_UNIQUE, 0, 0, 0 },
                    /* Ordinary-item bundles — indices into g_fillerBatches. */
                    {  6,  REWARD_DROP_BATCH,  0,  1,  8 },   /* low rune */
                    {  6,  REWARD_DROP_BATCH,  1,  1,  8 },   /* low gem */
                    {  5,  REWARD_DROP_BATCH,  2, 0, 0 },     /* rejuv */
                    {  3,  REWARD_DROP_BATCH,  3, 0, 0 },     /* full rejuv */
                    {  5,  REWARD_DROP_BATCH,  4, 0, 0 },     /* greater healing */
                    {  5,  REWARD_DROP_BATCH,  5, 0, 0 },     /* greater mana */
                    {  4,  REWARD_DROP_BATCH,  6, 0, 0 },     /* throwing potions */
                    {  3,  REWARD_DROP_BATCH,  7, 0, 0 },     /* jewel */
                    {  3,  REWARD_DROP_BATCH,  8, 0, 0 },     /* tome of TP */
                    {  3,  REWARD_DROP_BATCH,  9, 0, 0 },     /* tome of ID */
                    {  4,  REWARD_DROP_BATCH, 10, 0, 0 },     /* keys */
                    {  3,  REWARD_DROP_BATCH, 11, 0, 0 },     /* utility potions */
                    {  4,  REWARD_DROP_BATCH, 12,  4, 12 },   /* mid rune */
                    {  2,  REWARD_DROP_BATCH, 13,  8, 15 },   /* high rune */
                    {  4,  REWARD_DROP_BATCH, 14,  4, 12 },   /* flawless gem */
                    {  2,  REWARD_DROP_BATCH, 15,  8, 15 },   /* perfect gem */
                    {  4,  REWARD_DROP_BATCH, 16,  1, 10 },   /* small charm */
                    {  3,  REWARD_DROP_BATCH, 17,  4, 13 },   /* large charm */
                    {  2,  REWARD_DROP_BATCH, 18,  7, 15 },   /* grand charm */
                    /* Point sizes. extraIdx carries the AMOUNT, so the spoiler
                     * and the delivery cannot drift apart the way they would if
                     * the amount were rolled again at hand-out time. */
                    {  6,  REWARD_STAT,        1,  1, 10 },
                    {  5,  REWARD_STAT,        3,  3, 13 },
                    {  2,  REWARD_STAT,       10,  7, 15 },
                    { (g_skillHuntingOn ? 4 : 0), REWARD_SKILL, 2,  3, 13 },
                    { (g_skillHuntingOn ? 2 : 0), REWARD_SKILL, 3,  7, 15 },
                    { (g_skillHuntingOn ? 2 : 0), REWARD_RESETPT, 3, 4, 15 },
                };
                int wcount = (int)(sizeof(wtable) / sizeof(wtable[0]));
                /* Traps off — same gate the apworld applies. TrapsEnabled is
                 * carried as a percentage rather than a flag; zero means off. */
                if (g_fillerTrapPct <= 0) {
                    for (int i = 0; i < wcount; i++)
                        if (wtable[i].rewardType == REWARD_TRAP) wtable[i].weight = 0;
                }
                /* Depth banding. Off means every row is eligible everywhere,
                 * which is what the game did before this existed. */
                if (g_tieredFillerDepth) {
                    for (int i = 0; i < wcount; i++)
                        if (wtable[i].lo > 0 &&
                            (depth < wtable[i].lo || depth > wtable[i].hi))
                            wtable[i].weight = 0;
                }
                int totalW = 0;
                for (int i = 0; i < wcount; i++) totalW += wtable[i].weight;
                if (totalW <= 0) totalW = 1;
                int roll = rand() % totalW;
                int cum = 0;
                int rewardType = REWARD_GOLD;
                int extraIdx   = 0;
                for (int i = 0; i < wcount; i++) {
                    if (wtable[i].weight <= 0) continue;
                    cum += wtable[i].weight;
                    if (roll < cum) {
                        rewardType = wtable[i].rewardType;
                        extraIdx   = wtable[i].extraIdx;
                        break;
                    }
                }
                g_questRewardType[diff][qid] = rewardType;
                /* Pre-stash the trap/boss sub-index so the value-roll switch below stores it (overwritten for charm/set/unique which need their own catalog rolls). */
                g_questExtra[diff][qid] = extraIdx;

                /* Pre-roll the specific value for each reward type so the standalone delivery path and the spoiler file always agree. */
                switch (rewardType) {
                    case REWARD_GOLD:
                        /* Uniform [GoldRewardMin, GoldRewardMax]. */
                        g_questGold[diff][qid] = RewardRollGold();
                        break;
                    case REWARD_XP:
                        /* Uniform [XpRewardMin, XpRewardMax]. */
                        g_questXP[diff][qid] = RewardRollXp();
                        break;
                    case REWARD_TRAP:
                    case REWARD_LOOT:
                        /* g_questExtra was already pre-stashed by the weighted picker (specific trap variant / specific boss). */
                        break;
                    case REWARD_DROP_CHARM:
                        /* 0=Small, 1=Large, 2=Grand */
                        g_questExtra[diff][qid] = rand() % 3;
                        break;
                    case REWARD_DROP_SET:
                        /* 127 set pieces in g_collSetPieces[]. */
                        g_questExtra[diff][qid] = rand() % 127;
                        break;
                    case REWARD_DROP_UNIQUE:
                        /* Pick from the parsed UniqueItems.txt catalog; if it didn't load (file missing) the index ends up clamped to 0 by Quests_UniqueName. */
                        if (g_uniqueCatalogCount > 0) {
                            g_questExtra[diff][qid] = Quests_PickAnyUnique();
                        } else {
                            g_questExtra[diff][qid] = 0;
                        }
                        break;
                    default:
                        /* STAT, SKILL, RESETPT — fixed magnitude, no roll. */
                        break;
                }
            }
        }
    }

    /* --- Step 3: Log final distribution (1.9.0 expanded) --- */
    int cntProg=0, cntGold=0, cntStat=0, cntSkill=0, cntTrap=0, cntReset=0;
    int cntLoot=0, cntXP=0, cntCharm=0, cntSet=0, cntUnique=0;
    for (int a = 0; a < 5; a++) {
        for (int q = 0; q < g_acts[a].num; q++) {
            Quest* quest = &g_acts[a].quests[q];
            if (quest->reward == REWARD_SKILL) {
                cntProg++;
            } else {
                int qid = quest->id;
                if (qid > 0 && qid < MAX_QUEST_ID) {
                    int ft = g_questRewardType[0][qid];
                    switch (ft) {
                        case REWARD_GOLD:        cntGold++;   break;
                        case REWARD_STAT:        cntStat++;   break;
                        case REWARD_TRAP:        cntTrap++;   break;
                        case REWARD_RESETPT:     cntReset++;  break;
                        case REWARD_LOOT:        cntLoot++;   break;
                        case REWARD_XP:          cntXP++;     break;
                        case REWARD_DROP_CHARM:  cntCharm++;  break;
                        case REWARD_DROP_SET:    cntSet++;    break;
                        case REWARD_DROP_UNIQUE: cntUnique++; break;
                        default:                 cntSkill++;  break;
                    }
                }
            }
        }
    }
    Log("AssignAllRewards: prog=%d gold=%d stat=%d skill=%d trap=%d reset=%d "
        "loot=%d xp=%d charm=%d set=%d unique=%d (uniqueCat=%d entries)\n",
        cntProg, cntGold, cntStat, cntSkill, cntTrap, cntReset,
        cntLoot, cntXP, cntCharm, cntSet, cntUnique, g_uniqueCatalogCount);

    /* Pre-roll bonus check rewards using the same seed so the standalone spoiler can list them alongside the quest rewards. */
    extern void Bonus_PreRollAllRewards(unsigned seed);
    Bonus_PreRollAllRewards(seed);

    /* Pre-roll extra check rewards (six new categories: Cow / Merc / HF+Runes / NPC / Runeword / Cube). */
    extern void Extra_PreRollAllRewards(unsigned seed);
    Extra_PreRollAllRewards(seed);

    /* Write the per-character standalone spoiler file. */
    Quests_WriteSpoilerFile();
}

/* Standalone spoiler file. */
static void Quests_WriteSpoilerFile(void) {
    extern char g_charName[];
    if (!g_charName[0]) return;

    char dir[MAX_PATH], path[MAX_PATH];
    /* spoiler lives in Game/Save/ next to the .d2s file the spoiler describes — easier to find for the user than mixed in with the shared bridge files. */
    GetCharFileDir(dir, MAX_PATH);
    _snprintf(path, sizeof(path), "%sd2arch_spoiler_%s.txt", dir, g_charName);

    FILE* f = fopen(path, "w");
    if (!f) {
        Log("Quests_WriteSpoilerFile: cannot open %s for write\n", path);
        return;
    }

    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "Diablo II Archipelago - Standalone Reward Spoiler\n");
    fprintf(f, "==================================================\n");
    fprintf(f, "Character : %s\n", g_charName);
    fprintf(f, "Seed      : %u\n", g_seed);
    fprintf(f, "Generated : %04d-%02d-%02d %02d:%02d:%02d\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    fprintf(f, "\n");
    fprintf(f, "Reward delivery is deterministic per character: clearing the\n");
    fprintf(f, "same quest on the same difficulty always grants the value\n");
    fprintf(f, "listed below. (Skill unlocks come from a separate seeded pool\n");
    fprintf(f, "and aren't included here.)\n\n");

    static const char* diffNames[3] = {"Normal", "Nightmare", "Hell"};
    for (int diff = 0; diff < 3; diff++) {
        fprintf(f, "================ %s ================\n\n", diffNames[diff]);
        for (int a = 0; a < 5; a++) {
            int actHasFiller = 0;
            for (int q = 0; q < g_acts[a].num; q++) {
                int qid = g_acts[a].quests[q].id;
                if (qid <= 0 || qid >= MAX_QUEST_ID) continue;
                int rt = g_questRewardType[diff][qid];
                if (rt != REWARD_SKILL) { actHasFiller = 1; break; }
            }
            if (!actHasFiller) continue;
            fprintf(f, "  -- Act %d --\n", a + 1);
            for (int q = 0; q < g_acts[a].num; q++) {
                Quest* quest = &g_acts[a].quests[q];
                int qid = quest->id;
                if (qid <= 0 || qid >= MAX_QUEST_ID) continue;
                int rt = g_questRewardType[diff][qid];
                if (rt == REWARD_SKILL) continue;  /* skip prog rewards */
                char rewStr[96] = "";
                int  ext = g_questExtra[diff][qid];
                switch (rt) {
                    case REWARD_GOLD:
                        _snprintf(rewStr, sizeof(rewStr), "%d Gold", g_questGold[diff][qid]);
                        break;
                    /* The amount lives in g_questExtra. Printing a hardcoded 5
                     * here would make the spoiler disagree with the hand-out,
                     * which is the one thing a spoiler must never do. */
                    case REWARD_STAT:
                        _snprintf(rewStr, sizeof(rewStr), "+%d Stat Points",
                                  (ext > 0 && ext <= 10) ? ext : 5);
                        break;
                    case REWARD_RESETPT: {
                        int n = (ext > 0 && ext <= 3) ? ext : 1;
                        _snprintf(rewStr, sizeof(rewStr), "+%d Reset Point%s",
                                  n, n == 1 ? "" : "s");
                        break;
                    }
                    case REWARD_DROP_BATCH:
                        _snprintf(rewStr, sizeof(rewStr), "Drop: %s",
                                  (ext >= 0 && ext < FILLER_BATCH_COUNT)
                                  ? g_fillerBatches[ext].name : "?");
                        break;
                    case REWARD_TRAP:
                        _snprintf(rewStr, sizeof(rewStr), "%s", Quests_TrapTypeName(ext));
                        break;
                    case REWARD_LOOT:
                        _snprintf(rewStr, sizeof(rewStr), "Drop: %s Loot", Quests_BossLootName(ext));
                        break;
                    case REWARD_XP:
                        _snprintf(rewStr, sizeof(rewStr), "%d XP", g_questXP[diff][qid]);
                        break;
                    case REWARD_DROP_CHARM:
                        _snprintf(rewStr, sizeof(rewStr), "Drop: %s", Quests_CharmName(ext));
                        break;
                    case REWARD_DROP_SET:
                        _snprintf(rewStr, sizeof(rewStr), "Drop: %s (Set)", Quests_SetPieceName(ext));
                        break;
                    case REWARD_DROP_UNIQUE:
                        _snprintf(rewStr, sizeof(rewStr), "Drop: %s (Unique)", Quests_UniqueName(ext));
                        break;
                    default: {
                        int n = (ext > 0 && ext <= 3) ? ext : 1;
                        _snprintf(rewStr, sizeof(rewStr), "+%d Skill Point%s",
                                  n, n == 1 ? "" : "s");
                        break;
                    }
                }
                fprintf(f, "    %-32s -> %s\n", quest->name ? quest->name : "?", rewStr);
            }
            fprintf(f, "\n");
        }
    }

    /* Footer with summary counts so the user can see the final mix without re-counting by hand. */
    int totals[10] = {0};
    for (int diff = 0; diff < 3; diff++) {
        for (int a = 0; a < 5; a++) {
            for (int q = 0; q < g_acts[a].num; q++) {
                int qid = g_acts[a].quests[q].id;
                if (qid <= 0 || qid >= MAX_QUEST_ID) continue;
                int rt = g_questRewardType[diff][qid];
                if (rt >= 0 && rt < 10) totals[rt]++;
            }
        }
    }
    fprintf(f, "\n================ Reward Mix (all 3 difficulties) ================\n\n");
    /* REWARD_SKILL serves two purposes: - skill UNLOCK when the quest is a progression quest under SH=ON - skill POINT (+1) when the quest is a filler-rolled REWARD_SKILL The footer counts each separately by checking the quest's underlying type (progression types under SH=ON are unlocks; rest are filler skill points). */
    int skillUnlocks = 0, skillPoints = 0;
    for (int diff = 0; diff < 3; diff++) {
        for (int a = 0; a < 5; a++) {
            for (int q = 0; q < g_acts[a].num; q++) {
                int qid = g_acts[a].quests[q].id;
                if (qid <= 0 || qid >= MAX_QUEST_ID) continue;
                if (g_questRewardType[diff][qid] != REWARD_SKILL) continue;
                BOOL isProg = (g_acts[a].quests[q].type == QTYPE_QUESTFLAG ||
                               g_acts[a].quests[q].type == QTYPE_SUPERUNIQUE ||
                               g_acts[a].quests[q].type == QTYPE_LEVEL);
                if (isProg && g_skillHuntingOn) skillUnlocks++;
                else                            skillPoints++;
            }
        }
    }
    /* Cap skillUnlocks at the actual unique-skill pool size. */
    int skillPoolOverflow = 0;
    if (g_skillHuntingOn && skillUnlocks > g_poolCount) {
        skillPoolOverflow = skillUnlocks - g_poolCount;
        skillUnlocks = g_poolCount;
    }
    if (g_skillHuntingOn) {
        fprintf(f, "  Skill Unlock      : %d\n", skillUnlocks);
        if (skillPoolOverflow > 0)
            fprintf(f, "  Skill Pool Ovrflow: %d (deliver as +1 Skill Point)\n",
                    skillPoolOverflow);
    }
    fprintf(f, "  Gold              : %d\n", totals[REWARD_GOLD]);
    fprintf(f, "  Experience        : %d\n", totals[REWARD_XP]);
    fprintf(f, "  +5 Stat Points    : %d\n", totals[REWARD_STAT]);
    fprintf(f, "  Skill Points      : %d\n", skillPoints);
    if (g_skillHuntingOn) {
        fprintf(f, "  Reset Points      : %d\n", totals[REWARD_RESETPT]);
    }
    fprintf(f, "  Traps (4 variants): %d\n", totals[REWARD_TRAP]);
    fprintf(f, "  Boss Loot         : %d\n", totals[REWARD_LOOT]);
    fprintf(f, "  Drop: Charm       : %d\n", totals[REWARD_DROP_CHARM]);
    fprintf(f, "  Drop: Set Item    : %d\n", totals[REWARD_DROP_SET]);
    fprintf(f, "  Drop: Unique      : %d\n", totals[REWARD_DROP_UNIQUE]);
    int tot = 0;
    for (int i = 0; i < 10; i++) tot += totals[i];
    fprintf(f, "  ----------------------\n");
    fprintf(f, "  Total quest checks: %d\n", tot);
    fprintf(f, "\n");
    if (!g_skillHuntingOn) {
        fprintf(f, "Note: Skill Hunting is OFF, so progression quests (story\n");
        fprintf(f, "completions, Super Unique kills, level milestones) all roll\n");
        fprintf(f, "filler rewards from the catalog above. Vanilla D2 quest\n");
        fprintf(f, "rewards still apply on top of the filler rolls.\n");
        fprintf(f, "Reset Points are excluded from the pool because there is\n");
        fprintf(f, "no randomized skill pool to swap from.\n\n");
    }

    /* Append bonus check rewards section if any bonus categories are enabled. */
    extern void Bonus_AppendSpoilerToFile(FILE* f);
    Bonus_AppendSpoilerToFile(f);

    /* Append extra check rewards section if any extra categories are enabled. */
    extern void Extra_AppendSpoilerToFile(FILE* f);
    Extra_AppendSpoilerToFile(f);

    /* Append Custom Goal section ONLY when goal=4 (custom). */
    extern void CustomGoal_AppendSpoilerToFile(FILE* f);
    CustomGoal_AppendSpoilerToFile(f);

    /* Grand total footer. Sums every check category that the F1 Overview page displays so the spoiler footer matches the in-game "Total Checks" line exactly. The Overview includes: - Quest checks (computed above as 'tot') - Skill checks (g_poolCount when g_skillHuntingOn) - Bonus + Extra checks (only enabled categories) - Collection (always 205 — set pieces 127 + runes 33 + gems 35 + specials 10; the slots exist in the AP location_table regardless of Goal=Collection) - Zones (54 when g_zoneLockingOn — 18 gates × 3 difficulties) */
    extern int Bonus_GetTotalEnabledSlots(void);
    extern int Extra_GetTotalEnabledSlots(void);
    extern int g_poolCount;             /* d2arch_skilltree.c */
    extern BOOL g_zoneLockingOn;        /* d2arch_zones.c */
    int skillTot = g_skillHuntingOn ? g_poolCount : 0;
    int bonusTot = Bonus_GetTotalEnabledSlots();
    int extraTot = Extra_GetTotalEnabledSlots();
    int collTot  = 205;                 /* 127 set + 33 runes + 35 gems + 10 specials */
    int zoneTot  = g_zoneLockingOn ? 54 : 0;  /* 18 gates × 3 diff */
    int grandTot = tot + skillTot + bonusTot + extraTot + collTot + zoneTot;
    fprintf(f, "\n================ Grand Total ================\n\n");
    fprintf(f, "  Quest checks            : %d\n", tot);
    if (skillTot > 0)
        fprintf(f, "  Skill checks            : %d\n", skillTot);
    if (bonusTot > 0)
        fprintf(f, "  Bonus checks (enabled)  : %d\n", bonusTot);
    if (extraTot > 0)
        fprintf(f, "  Extra checks (enabled)  : %d\n", extraTot);
    fprintf(f, "  Collection checks       : %d\n", collTot);
    if (zoneTot > 0)
        fprintf(f, "  Zone checks             : %d\n", zoneTot);
    fprintf(f, "  ----------------------------------------\n");
    fprintf(f, "  TOTAL CHECKS            : %d\n", grandTot);
    fprintf(f, "\n");

    /* Total Reward Mix footer. */
    extern void Bonus_CountRewardsInto(int totals[10]);
    extern void Extra_CountRewardsInto(int totals[10]);
    int rewardTotals[10];
    for (int i = 0; i < 10; i++) rewardTotals[i] = totals[i];
    Bonus_CountRewardsInto(rewardTotals);
    Extra_CountRewardsInto(rewardTotals);

    /* Skill BR_SKILL is split into "unlock" vs "filler skill point" for quests (based on quest type + skill_hunting). */
    int totalSkillPoints = skillPoints + skillPoolOverflow
                         + (rewardTotals[REWARD_SKILL] - totals[REWARD_SKILL]);
    /* Grand reward total = sum of every printed line below. */
    int grandRewardTot = 0;
    if (g_skillHuntingOn) grandRewardTot += skillUnlocks;
    grandRewardTot += rewardTotals[REWARD_GOLD];
    grandRewardTot += rewardTotals[REWARD_XP];
    grandRewardTot += rewardTotals[REWARD_STAT];
    grandRewardTot += totalSkillPoints;
    if (g_skillHuntingOn) grandRewardTot += rewardTotals[REWARD_RESETPT];
    grandRewardTot += rewardTotals[REWARD_TRAP];
    grandRewardTot += rewardTotals[REWARD_LOOT];
    grandRewardTot += rewardTotals[REWARD_DROP_CHARM];
    grandRewardTot += rewardTotals[REWARD_DROP_SET];
    grandRewardTot += rewardTotals[REWARD_DROP_UNIQUE];

    fprintf(f, "================ Total Reward Mix (all sources) ================\n\n");
    fprintf(f, "Combined count of pre-rolled rewards across Quests + Bonus +\n");
    fprintf(f, "Extra check pools. Quest rewards always deliver; Bonus / Extra\n");
    fprintf(f, "rewards deliver as the matching slot fires (escalating-chance\n");
    fprintf(f, "for bonus objects; first-trigger for extras).\n\n");
    if (g_skillHuntingOn)
        fprintf(f, "  Skill Unlock      : %d\n", skillUnlocks);
    fprintf(f, "  Gold              : %d\n", rewardTotals[REWARD_GOLD]);
    fprintf(f, "  Experience        : %d\n", rewardTotals[REWARD_XP]);
    fprintf(f, "  +5 Stat Points    : %d\n", rewardTotals[REWARD_STAT]);
    fprintf(f, "  Skill Points      : %d\n", totalSkillPoints);
    if (g_skillHuntingOn)
        fprintf(f, "  Reset Points      : %d\n", rewardTotals[REWARD_RESETPT]);
    fprintf(f, "  Traps (4 variants): %d\n", rewardTotals[REWARD_TRAP]);
    fprintf(f, "  Boss Loot         : %d\n", rewardTotals[REWARD_LOOT]);
    fprintf(f, "  Drop: Charm       : %d\n", rewardTotals[REWARD_DROP_CHARM]);
    fprintf(f, "  Drop: Set Item    : %d\n", rewardTotals[REWARD_DROP_SET]);
    fprintf(f, "  Drop: Unique      : %d\n", rewardTotals[REWARD_DROP_UNIQUE]);
    fprintf(f, "  ----------------------\n");
    fprintf(f, "  TOTAL REWARDS     : %d\n", grandRewardTot);
    fprintf(f, "\n");

    fclose(f);
    Log("Quests_WriteSpoilerFile: wrote %s\n", path);
}

/* Quest Log UI state */
static int g_questLogAct = 0;
static int g_questLogSubTab = 0; /* 0=Main Quests, 1=Side Quests */

/* Per-area kill counts, PER DIFFICULTY. */
#define MAX_AREA_ID 150
static int g_areaKills[3][MAX_AREA_ID];

/* Clamp a difficulty index; anything unexpected falls back to Normal rather than indexing out of the array. */
static int KillDiff(int d) { return (d >= 0 && d <= 2) ? d : 0; }

/* 1.8.0 cleanup: Treasure Cow state arrays + TREASURE_COW_SU_ID define extracted to Tools/Archipelago/pending_reimplementation/TREASURE_COWS/ */

/* Forward declarations needed by zone gating system */
static BOOL IsTown(DWORD area);
static void ShowNotify(const char* text);

