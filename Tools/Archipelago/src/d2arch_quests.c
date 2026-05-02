
/* ================================================================
 * QUEST SYSTEM — Data structures and 108 quests
 * ================================================================ */
typedef enum { QTYPE_BOSS, QTYPE_AREA, QTYPE_KILL, QTYPE_WAYPOINT, QTYPE_QUESTFLAG, QTYPE_SUPERUNIQUE, QTYPE_LEVEL } QuestType;
/* 1.9.0 reward redesign: every filler-quest reward is now pre-rolled at
 * character creation. The specific value (gold amount, xp amount, which
 * trap, which boss, which charm/set/unique) lives in the parallel
 * g_questXP / g_questExtra tables and gets persisted in the per-char
 * state file alongside g_questGold and g_questRewardType. */
typedef enum {
    REWARD_GOLD,           /* g_questGold = 1..10000                          */
    REWARD_SKILL,          /* skill point (or skill unlock for prog quests)   */
    REWARD_STAT,           /* +5 stat points                                   */
    REWARD_TRAP,           /* g_questExtra = 0..3 trap type                   */
    REWARD_RESETPT,        /* +1 reset point                                   */
    REWARD_LOOT,           /* g_questExtra = 0..4 boss id (Andariel..Baal)    */
    REWARD_XP,             /* g_questXP   = 1..250000                          */
    REWARD_DROP_CHARM,     /* g_questExtra = 0..2 (small/large/grand charm)   */
    REWARD_DROP_SET,       /* g_questExtra = 0..126 set-piece catalog index   */
    REWARD_DROP_UNIQUE,    /* g_questExtra = uniqueitems.txt row index        */
} RewardType;

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

/* Per-difficulty quest completion state.
 * completedPerDiff[diff][questId] = TRUE if completed on that difficulty */
#define MAX_QUEST_ID 800
#define LOCATION_BASE 42000
static BOOL g_questCompleted[3][MAX_QUEST_ID]; /* [difficulty][questId] */
static void* g_questScanQF = NULL; /* D2Client's quest record pointer (DAT_6fbb5d13) */

/* XP Multiplier (from INI XPMultiplier setting) */
static int g_xpMultiplier = 1;

/* Hidden cheat menu (Ctrl+V) — for testing only */
static BOOL g_cheatMenuOpen = FALSE;
static volatile int g_cheatGold = 0;
static volatile int g_cheatStatPts = 0;
static volatile int g_cheatSkillPts = 0;
static volatile int g_cheatLevel = 0;
static volatile int g_cheatSpawnTrapSU = 0;   /* trigger SuperUnique trap spawn */
static volatile int g_cheatSpawnTrapMon = 0;  /* trigger regular monster spawn */
static volatile int g_cheatDropGold = 0;      /* drop gold pile on ground */
static volatile int g_cheatDropLoot = 0;      /* drop boss loot table (debug) */
static volatile int g_cheatDropBossId = 0;    /* which boss TC to drop (debug) */
static volatile int g_cheatTestRunewords = 0; /* drop everything (legacy) */
static volatile int g_cheatTestRunes = 0;     /* drop Cube + all 33 runes only */
static volatile int g_cheatTestBases = 0;     /* drop armor/helm/weapon bases + cube pots */
static volatile int g_cheatItemCmd = 0;       /* unified item-drop dispatch (1.9.0) */
static volatile int g_cheatHealFull = 0;      /* refill HP+MP+Stamina */
static volatile int g_cheatSpawnUber = 0;     /* Pandemonium uber spawn (1.9.0):
                                                 1=Lilith, 2=Uber Duriel, 3=Uber Izual,
                                                 4=Uber Mephisto, 5=Uber Diablo,
                                                 6=Uber Baal, 7=mini uber trio,
                                                 8=final uber trio */
static volatile int g_cheatTeleport = 0;      /* Cheat teleport: pending area ID
                                                 0 = none, >0 = warp to that area */
static volatile int g_pendingPortalLevel = 0; /* Cheat physical portal spawn (1.9.0):
                                                 0 = none. >0 = destination level ID
                                                 to spawn portal to. */
static volatile int g_pendingPortalObjId = 60;/* Object class for the portal:
                                                 59=blue Town Portal, 60=red
                                                 permanent portal (Cow/Trist),
                                                 100=Duriel Lair portal */
/* g_cheatItemCmd values:
 *   1=Cube only       2=Low Runes 1-10   3=Mid Runes 11-20  4=High Runes 21-33
 *   5=All 33 Runes    6=Body bases       7=Helm bases       8=Weapon bases
 *   9=All bases      10=All Gems (35)   11=Healing pots    12=Mana pots
 *  13=Recipe pots (vps+wms)
 */

/* 1.9.1 — Individual-item dispatch for the Loot tab in the Ctrl+V menu.
 * Each "spawn one specific item" button on the new debug pages writes to
 * exactly ONE of these slots; the gameloop tick reads, fires, resets.
 *
 * Set / unique pieces route through Quests_QueueSpecificDrop (same path
 * the AP server uses) so clicking the menu mirrors a real AP delivery.
 *
 * Single-item spawns (runes / gems / charms / pots / Pandemonium items)
 * call QUESTS_CreateItem directly with the supplied 3-char code, level,
 * and quality. Format mirrors the existing g_cheatItemCmd switch in
 * d2arch_gameloop.c so behaviour is identical to the legacy batch path. */
static volatile int  g_cheatSpecificSetIdx    = -1;  /* 0..126, -1 = idle */
static volatile int  g_cheatSpecificUniqueIdx = -1;  /* 0..g_uniqueCatalogCount-1, -1 = idle */
static volatile char g_cheatSingleItemCode[8] = "";  /* 3-char code + nul, "" = idle */
static volatile BYTE g_cheatSingleItemQuality = 2;   /* 2=normal, 4=magic, 5=set, 7=unique */
static volatile int  g_cheatSingleItemLvl     = 50;  /* spawn level (use 99 for cm2 unique torch) */

/* 1.9.2 — Specific-monster spawn dispatch for the new Mons tab in the
 * Ctrl+V menu.  Two slots:
 *   g_cheatSpawnSuperUniqueIdx — 0..65 (vanilla SU), uses fnSpawnSuperUnique
 *   g_cheatSpawnMonsterRowId   — MonStats.txt row index, uses fnSpawnMonster
 * Both spawn one unit at the player's position + small offset, mirroring
 * the existing g_cheatSpawnUber pipeline. */
static volatile int g_cheatSpawnSuperUniqueIdx = -1;
static volatile int g_cheatSpawnMonsterRowId   = -1;

/* Boss loot table TC IDs.
 * 1.7.1: resolved dynamically at boot via ResolveBossLootTCs() by matching the
 * TC name in sgptDataTables->pTreasureClassExTxt. If the name lookup fails
 * (e.g. heavily modded TC file or offsets moved), we keep the hardcoded
 * fallback values below which are correct for vanilla LoD 1.10f. */
#define BOSS_TC_ANDARIEL  667
#define BOSS_TC_DURIEL    826
#define BOSS_TC_MEPHISTO  685
#define BOSS_TC_DIABLO    691
#define BOSS_TC_BAAL      718
static int g_bossLootTCs[] = { BOSS_TC_ANDARIEL, BOSS_TC_DURIEL, BOSS_TC_MEPHISTO, BOSS_TC_DIABLO, BOSS_TC_BAAL };
static const char* g_bossLootNames[] = { "Andariel", "Duriel", "Mephisto", "Diablo", "Baal" };
#define BOSS_LOOT_COUNT 5
static BOOL g_bossLootTCsResolved = FALSE;

/* Pending loot drops from quest rewards */
static volatile int g_pendingLootDrop = 0;
/* 1.9.0: which boss TC the next g_pendingLootDrop drain should use.
 * Set by REWARD_LOOT delivery and by AP receive of Drop: <Boss> Loot.
 * Defaults to -1 = drop loop falls back to a random pick (legacy
 * behavior). The drop loop resets it to -1 after each drop. */
static volatile int g_pendingLootBossId = -1;

/* 1.9.0: queue of pending specific-item drops (charm / set / unique).
 * Each entry remembers the drop kind + catalog index + the source name
 * (quest title or "AP server") so the in-game item-log can attribute
 * the drop. The tick handler in d2arch_gameloop.c drains at most one
 * entry per tick to avoid carpeting the floor. */
#define PENDING_DROP_QUEUE_MAX 32
typedef struct {
    int   kind;        /* REWARD_DROP_CHARM / SET / UNIQUE */
    int   idx;         /* catalog index */
    char  source[40];  /* short attribution string */
} PendingDropEntry;
static PendingDropEntry g_pendingDropQueue[PENDING_DROP_QUEUE_MAX];
static volatile int g_pendingDropCount = 0;

/* Forward declarations — bodies live further down the unity TU.
 * Quests_QueueSpecificDrop needs the name lookups to render its
 * notification immediately; the unique catalog flag/count back the
 * UNIQUE branch's name lookup. */
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
    /* Notify only — the actual spawn happens in the gameloop tick when
     * we have a valid player unit. */
    const char* kindName = "Drop";
    char itemName[80] = "";
    if (kind == REWARD_DROP_CHARM)  { kindName = Quests_CharmName(idx); _snprintf(itemName, sizeof(itemName), "%s", kindName); }
    if (kind == REWARD_DROP_SET)    { _snprintf(itemName, sizeof(itemName), "Drop: %s", Quests_SetPieceName(idx)); }
    if (kind == REWARD_DROP_UNIQUE) { _snprintf(itemName, sizeof(itemName), "Drop: %s", Quests_UniqueName(idx)); }
    if (itemName[0]) ShowNotify(itemName);
    Log("QueueSpecificDrop: queued kind=%d idx=%d source=%s -> %s (queue=%d)\n",
        kind, idx, source ? source : "(none)", itemName, g_pendingDropCount);
    ItemLogAddA(2, 9, itemName[0] ? itemName : "Drop", source ? source : "AP server");
}

/* Internal accessors used by gameloop.c's drop processor. The
 * pending queue is FIFO; ConsumePending pops the head. */
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

/* Code helpers for the spawn path. CHARM_CODES[0]=cm1 (Small),
 * [1]=cm2 (Large), [2]=cm3 (Grand). The hex form is space-padded
 * 4-byte per D2's DATATBLS_GetItemRecordFromItemCode hash. */
static const char* QUESTS_CHARM_CODES[3] = { "cm1", "cm2", "cm3" };
static DWORD Quests_PackCode(const char* code3) {
    /* 3-byte code + space-pad to 4. Returns 0 if invalid. */
    if (!code3) return 0;
    int n = (int)strlen(code3);
    if (n < 1 || n > 3) return 0;
    DWORD r = 0x20202020u;
    for (int i = 0; i < n; i++) {
        r = (r & ~(0xFFu << (i * 8))) | ((DWORD)(BYTE)code3[i] << (i * 8));
    }
    return r;
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
/* Quality byte for QUESTS_CreateItem.
 *   2 = Normal, 4 = Magic, 5 = Set, 6 = Rare, 7 = Unique */
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
#define TRAP_TYPE_COUNT 4
static volatile int g_pendingTrapSlow = 0;
static volatile int g_pendingTrapWeaken = 0;
static volatile int g_pendingTrapPoison = 0;
static int  g_questKillCount[3][MAX_QUEST_ID]; /* [difficulty][questId] kill progress */
static int  g_questGold[3][MAX_QUEST_ID];      /* [difficulty][questId] gold amount */
/* AP location owner names — who receives the item when this check is done */
static char g_locationOwner[3][MAX_QUEST_ID][24]; /* [diff][questId] = "PlayerName" */
static BOOL g_locationOwnersLoaded = FALSE;

/* 1.8.5 — AP gate-key item location map. Filled from ap_item_locations.dat
 * which the bridge writes when LocationInfo is received (after scout). Each
 * entry is a printable summary like "loc 47020 in Bob's world" suitable for
 * display in the F4 zone tracker. Empty string means unknown / not yet
 * scouted (the typical multiworld case where keys live in OTHER players'
 * worlds, requiring hint data). */
#define APKEY_DISPLAY_LEN 64
static char g_apItemLocation[3][18][APKEY_DISPLAY_LEN]; /* [diff][gate_slot] */
static BOOL g_apItemLocationsLoaded = FALSE;

/* 1.9.0 Phase 9 — public accessor for the F4 render path. Returns
 * a pointer to the location string for the given (difficulty, gate
 * slot) pair, or NULL if no data has been loaded yet OR the slot is
 * out of range OR the string is empty. The returned pointer is owned
 * by this module — callers must not modify or free it. */
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

/* Per-difficulty filler quest reward types — regenerated from seed */
static int g_questRewardType[3][MAX_QUEST_ID]; /* [difficulty][questIdx] = REWARD_GOLD/STAT/SKILL/... */
/* 1.9.0 reward-redesign companion fields (parallel to g_questGold which
 * holds the gold amount when type=REWARD_GOLD). g_questXP holds the xp
 * amount when type=REWARD_XP; g_questExtra holds the type-specific
 * sub-index for trap/boss/charm/set/unique rewards. */
static int g_questXP[3][MAX_QUEST_ID];
static int g_questExtra[3][MAX_QUEST_ID];

/* 1.9.0 — Unique items catalog forward-declared earlier (so the
 * pending-drop queue's helpers can call into Quests_UniqueName).
 * Definitions live further down at the helper-body section. */
static void Quests_WriteSpoilerFile(void);

/* Helpers used by the standalone spoiler / drop pipelines. The four
 * non-unique ones (TrapTypeName, BossLootName, CharmName,
 * SetPieceName) and Quests_LoadUniqueCatalog are forward-declared
 * earlier (see PendingDropEntry block); listed here only for clarity. */
static const char* Quests_TrapTypeName(int t);
static const char* Quests_BossLootName(int b);

/* ---- Act 1 quests ---- */
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
    { 22, "Clear Tower Cellar L1",      "Kill 8 monsters in Tower Cellar Level 1",    QTYPE_KILL, 21, 8, 0, FALSE, REWARD_GOLD, 0 },
    { 23, "Clear Tower Cellar L2",      "Kill 8 monsters in Tower Cellar Level 2",    QTYPE_KILL, 22, 8, 0, FALSE, REWARD_GOLD, 0 },
    { 24, "Clear Tower Cellar L3",      "Kill 8 monsters in Tower Cellar Level 3",    QTYPE_KILL, 23, 8, 0, FALSE, REWARD_GOLD, 0 },
    { 25, "Clear Tower Cellar L4",      "Kill 8 monsters in Tower Cellar Level 4",    QTYPE_KILL, 24, 8, 0, FALSE, REWARD_GOLD, 0 },
    { 26, "Clear Tower Cellar L5",      "Kill 8 monsters in Tower Cellar Level 5",    QTYPE_KILL, 25, 8, 0, FALSE, REWARD_GOLD, 0 },
    { 27, "Clear Monastery Gate",       "Kill 8 monsters at the Monastery Gate",      QTYPE_KILL, 26, 8, 0, FALSE, REWARD_GOLD, 0 },
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

/* ---- Act 2 quests ---- */
/* D2 quest flag IDs: Radament=7, Horadric Staff=8, Tainted Sun=9, Arcane=10, Summoner=11, Duriel=12 */
static Quest g_act2Quests[] = {
    /* Story quests — QTYPE_QUESTFLAG (PROGRESSION)
     * D2MOO QUESTSTATEFLAG IDs: A2Q1=9, A2Q2=10, A2Q3=11, A2Q4=12, A2Q5=13, A2Q6=14
     * (NOT 7-12! 7=A1COMPLETED, 8=A2Q0 gossip) */
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
    {174, "Hunt: Leatherarm",         "Kill Creeping Feature",               QTYPE_SUPERUNIQUE, 14, 0,0, FALSE, REWARD_SKILL, 0 },
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
    {114, "Clear Valley of Snakes",    "Kill 20 monsters in Valley of Snakes",    QTYPE_KILL, 45, 20,0, FALSE, REWARD_GOLD, 0 },
    /* Sewers removed — D2MOO pathfinding broken in narrow corridors */
    /* {115, "Clear Sewers L1",           "Kill 15 monsters in Sewers Level 1",      QTYPE_KILL, 47, 15,0, FALSE, REWARD_GOLD, 0 }, */
    /* {116, "Clear Sewers L2",           "Kill 15 monsters in Sewers Level 2",      QTYPE_KILL, 48, 15,0, FALSE, REWARD_GOLD, 0 }, */
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
    /* Area entry — FILLER */
    {140, "Enter Rocky Waste",         "Enter the Rocky Waste",                   QTYPE_AREA, 41, 0,0, FALSE, REWARD_GOLD, 0 },
    {141, "Enter Dry Hills",           "Enter the Dry Hills",                     QTYPE_AREA, 42, 0,0, FALSE, REWARD_GOLD, 0 },
    {142, "Enter Far Oasis",           "Enter the Far Oasis",                     QTYPE_AREA, 43, 0,0, FALSE, REWARD_GOLD, 0 },
    {143, "Enter Lost City",           "Enter the Lost City",                     QTYPE_AREA, 44, 0,0, FALSE, REWARD_GOLD, 0 },
    {144, "Enter Arcane Sanctuary",    "Enter the Arcane Sanctuary",              QTYPE_AREA, 74, 0,0, FALSE, REWARD_GOLD, 0 },
    /* Waypoint quests — FILLER */
    /* {150, "Sewers Waypoint",             "Activate the Sewers waypoint",               QTYPE_WAYPOINT, 10, 0,0, FALSE, REWARD_GOLD, 0 }, */ /* Removed: D2MOO pathfinding broken */
    {151, "Dry Hills Waypoint",           "Activate the Dry Hills waypoint",            QTYPE_WAYPOINT, 11, 0,0, FALSE, REWARD_GOLD, 0 },
    {152, "Halls of the Dead Waypoint",  "Activate the Halls of the Dead waypoint",    QTYPE_WAYPOINT, 12, 0,0, FALSE, REWARD_GOLD, 0 },
    {153, "Far Oasis Waypoint",           "Activate the Far Oasis waypoint",            QTYPE_WAYPOINT, 13, 0,0, FALSE, REWARD_GOLD, 0 },
    {154, "Lost City Waypoint",          "Activate the Lost City waypoint",            QTYPE_WAYPOINT, 14, 0,0, FALSE, REWARD_GOLD, 0 },
    {155, "Palace Cellar Waypoint",      "Activate the Palace Cellar waypoint",        QTYPE_WAYPOINT, 15, 0,0, FALSE, REWARD_GOLD, 0 },
    {156, "Arcane Sanctuary Waypoint",   "Activate the Arcane Sanctuary waypoint",     QTYPE_WAYPOINT, 16, 0,0, FALSE, REWARD_GOLD, 0 },
    {157, "Canyon of the Magi Waypoint", "Activate the Canyon of the Magi waypoint",   QTYPE_WAYPOINT, 17, 0,0, FALSE, REWARD_GOLD, 0 },
};

/* ---- Act 3 quests ---- */
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
    {271, "Hunt: Web Mage",           "Kill Sszark the Burning",             QTYPE_SUPERUNIQUE, 21, 0,0, FALSE, REWARD_SKILL, 0 },
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
    /* {220, "Clear Kurast Sewers L1",    "Kill 15 monsters in Kurast Sewers",       QTYPE_KILL, 92, 15,0, FALSE, REWARD_GOLD, 0 }, */
    /* {221, "Clear Kurast Sewers L2",    "Kill 15 monsters in Kurast Sewers 2",     QTYPE_KILL, 93, 15,0, FALSE, REWARD_GOLD, 0 }, */
    {222, "Clear Durance L1",          "Kill 25 monsters in Durance of Hate",     QTYPE_KILL, 100,25,0, FALSE, REWARD_GOLD, 0 },
    {223, "Clear Durance L2",          "Kill 30 monsters in Durance of Hate 2",   QTYPE_KILL, 101,30,0, FALSE, REWARD_GOLD, 0 },
    {224, "Clear Kurast Causeway",     "Kill 20 monsters in Kurast Causeway",     QTYPE_KILL, 82, 20,0, FALSE, REWARD_GOLD, 0 },
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

/* ---- Act 4 quests ---- */
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
    {350, "Plains of Despair Waypoint",  "Activate the Plains of Despair waypoint",    QTYPE_WAYPOINT, 28, 0,0, FALSE, REWARD_GOLD, 0 },
    {351, "River of Flame Waypoint",     "Activate the River of Flame waypoint",       QTYPE_WAYPOINT, 29, 0,0, FALSE, REWARD_GOLD, 0 },
};

/* ---- Act 5 quests ---- */
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
    {470, "Hunt: Siege Boss",         "Kill Shenk the Overseer",             QTYPE_SUPERUNIQUE, 42, 0,0, FALSE, REWARD_SKILL, 0 },
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
    {414, "Clear Glacial Caves L1",    "Kill 20 monsters in Glacial Caves",       QTYPE_KILL, 118, 20,0, FALSE, REWARD_GOLD, 0 },
    {415, "Clear Glacial Caves L2",    "Kill 20 monsters in Glacial Caves 2",     QTYPE_KILL, 119, 20,0, FALSE, REWARD_GOLD, 0 },
    {416, "Clear Tundra Wastelands",   "Kill 25 monsters in Tundra Wastelands",   QTYPE_KILL, 117, 25,0, FALSE, REWARD_GOLD, 0 },
    {417, "Clear Halls of Anguish",    "Kill 20 monsters in Halls of Anguish",    QTYPE_KILL, 122, 20,0, FALSE, REWARD_GOLD, 0 },
    {418, "Clear Halls of Death",      "Kill 20 monsters in Halls of Death",      QTYPE_KILL, 123, 20,0, FALSE, REWARD_GOLD, 0 },
    {419, "Clear Halls of Vaught",     "Kill 20 monsters in Halls of Vaught",     QTYPE_KILL, 124, 20,0, FALSE, REWARD_GOLD, 0 },
    {420, "Clear Worldstone L1",       "Kill 30 monsters in Worldstone Keep 1",   QTYPE_KILL, 128, 30,0, FALSE, REWARD_GOLD, 0 },
    {421, "Clear Worldstone L2",       "Kill 35 monsters in Worldstone Keep 2",   QTYPE_KILL, 129, 35,0, FALSE, REWARD_GOLD, 0 },
    {422, "Clear Worldstone L3",       "Kill 40 monsters in Worldstone Keep 3",   QTYPE_KILL, 130, 40,0, FALSE, REWARD_GOLD, 0 },
    {423, "Clear Throne of Destruction","Kill 40 monsters in Throne of Destruction",QTYPE_KILL,131,40,0, FALSE, REWARD_GOLD, 0 },
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
    /* {453, "Frozen River Waypoint",       "Activate the Frozen River waypoint",         QTYPE_WAYPOINT, 34, 0,0, FALSE, REWARD_GOLD, 0 }, */ /* Removed: no WP in Frozen River */
    {454, "Halls of Pain Waypoint",      "Activate the Halls of Pain waypoint",        QTYPE_WAYPOINT, 35, 0,0, FALSE, REWARD_GOLD, 0 },
    {455, "Glacial Trail Waypoint",      "Activate the Glacial Trail waypoint",        QTYPE_WAYPOINT, 34, 0,0, FALSE, REWARD_GOLD, 0 },
    {456, "Frozen Tundra Waypoint",      "Activate the Frozen Tundra waypoint",        QTYPE_WAYPOINT, 37, 0,0, FALSE, REWARD_GOLD, 0 },
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

/* 1.9.0 — Total / completed quest counts now respect both:
 *   - Enabled quest TYPES (Story / Hunt / Kill / Area / WP / Lvl)
 *   - Enabled DIFFICULTIES (Goal=Normal -> ×1, Goal=NM -> ×2, Goal=Hell -> ×3)
 * because each (qid, diff) pair is a distinct AP location. The legacy
 * version returned the raw catalog count, so users on Goal=Hell with
 * everything enabled saw "1/232" when the real total was 696, and
 * users on Normal-only with KillZones disabled still saw all kill
 * quests in the total.
 *
 * Forward-declares: IsQuestTypeActive lives in d2arch_questlog.c
 * (included after this file in unity build); the call is deferred to
 * runtime so the forward decl is enough. */
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

/* ================================================================
 * 1.9.0 — Reward-redesign helper functions and catalogs
 *
 * Pre-roll model:
 *   AssignAllRewards picks each filler quest's REWARD_TYPE first,
 *   then resolves the SPECIFIC value (gold amount, xp amount, trap
 *   variant, boss id, charm/set/unique pick) and stashes it in
 *   parallel arrays. The standalone delivery path and the AP-receive
 *   path both consume those pre-rolled values, so what the spoiler
 *   file says is what the player gets.
 * ================================================================ */

/* Names used by the spoiler file. Indices match the enums used in
 * g_questExtra (TRAP_MONSTERS=0..TRAP_POISON=3, boss 0=Andariel..4=Baal,
 * charm 0=cm1..2=cm3). */
static const char* Quests_TrapTypeName(int t) {
    switch (t) {
        case TRAP_MONSTERS: return "Trap: Monsters";
        case TRAP_SLOW:     return "Trap: Slow (Decrepify)";
        case TRAP_WEAKEN:   return "Trap: Weaken (Amplify Damage)";
        case TRAP_POISON:   return "Trap: Poison";
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
/* Set piece name lookup. The actual catalog (g_collSetPieces[]) lives
 * in d2arch_collections.c which is included AFTER this file in the
 * unity build, so we forward-declare the lookup here and let the
 * linker resolve to the body further down the TU. 127 set pieces
 * in 1.10f. */
extern const char* Coll_GetSetPieceName(int idx);
static const char* Quests_SetPieceName(int idx) {
    return Coll_GetSetPieceName(idx);
}

/* Unique catalog — parsed from data/global/excel/UniqueItems.txt at
 * first use. We deliberately read the .txt file (not the runtime
 * sgptDataTables struct) because (a) the .txt is always present in
 * an installed copy of D2, (b) parsing it doesn't depend on D2's
 * data tables having finished loading, and (c) it avoids tight
 * coupling to D2 1.10f-specific struct offsets. */
static const char* Quests_UniqueName(int idx) {
    if (!g_uniqueCatalogLoaded) Quests_LoadUniqueCatalog();
    if (idx < 0 || idx >= g_uniqueCatalogCount) return "Random Unique";
    return g_uniqueCatalog[idx].name;
}

/* 1.9.1 — split a tab-separated line into fields, PRESERVING empty
 * columns between consecutive tabs. strtok merges them into a single
 * delimiter which silently shifts every column right of the first
 * empty cell — that bug had Quests_LoadUniqueCatalog reading "cost mult"
 * (numeric) into baseCode for every unique whose row had an empty
 * `ladder` column (most of UniqueItems.txt). Symptom: AP-delivered and
 * cheat-menu-spawned uniques tried to spawn item code "5   " (FAIL).
 *
 * Trims trailing CR / LF on the last field. fields[] receives pointers
 * into `s`; `s` is mutated in place. Returns the field count. */
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

    /* Resolve game data dir relative to the running exe. The Game/
     * directory contains data/global/excel/UniqueItems.txt for both
     * 1.10f and the AP-shipped variant. */
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

    /* Locate columns by header name so we don't break if the user has
     * a modded txt with reordered columns. */
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

/* ================================================================
 * REWARD ASSIGNMENT — progression first, then filler
 *
 * 1.9.0 redesign:
 *   - Filler types expanded with REWARD_XP and three REWARD_DROP_*
 *     variants (charm/set/unique).
 *   - Every filler quest gets its specific value pre-rolled here:
 *       REWARD_GOLD     -> g_questGold[d][q]   = 1..10000
 *       REWARD_XP       -> g_questXP[d][q]     = 1..250000
 *       REWARD_TRAP     -> g_questExtra[d][q]  = 0..3 trap variant
 *       REWARD_LOOT     -> g_questExtra[d][q]  = 0..4 boss id
 *       REWARD_DROP_*   -> g_questExtra[d][q]  = catalog row index
 *
 * Logic:
 * 1. Count how many skills need unlocking (POOL_SIZE - NUM_STARTING = 54)
 * 2. Collect ALL quests across all acts into a flat list
 * 3. Spread 54 progression rewards EVENLY across acts
 *    (each act gets proportional share based on quest count)
 * 4. Within each act, progression slots are shuffled (seed-based)
 * 5. Remaining quests get filler: weights from g_filler*Pct
 * ================================================================ */
static void AssignAllRewards(DWORD seed) {
    srand(seed + 777);

    memset(g_questRewardType, 0, sizeof(g_questRewardType));
    memset(g_questGold, 0, sizeof(g_questGold));
    memset(g_questXP, 0, sizeof(g_questXP));
    memset(g_questExtra, 0, sizeof(g_questExtra));

    /* 1.9.0 — Lazy-load the unique catalog so AssignAllRewards can roll
     * REWARD_DROP_UNIQUE indices into a known-valid range. */
    Quests_LoadUniqueCatalog();

    /* --- Step 1: Deterministic reward assignment based on quest type --- */
    int cntProgAct[5] = {0}, cntFillerAct[5] = {0};
    for (int a = 0; a < 5; a++) {
        for (int q = 0; q < g_acts[a].num; q++) {
            Quest* quest = &g_acts[a].quests[q];
            /* 1.9.0: Progression quests only get REWARD_SKILL when Skill
             * Hunting is ON. With SH=OFF the player isn't earning skill
             * unlocks per quest (vanilla D2 rewards apply for those
             * quests instead), so we treat ALL quests as filler so the
             * pre-roll system can hand out gold/xp/traps/drops on every
             * tick. This matches what the user expects from the spoiler
             * file: everything that completes should grant something
             * specific from our reward catalog. */
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

                /* 1.9.0 filler distribution — ten categories. The DLL-side
                 * pcts cover gold/stat/skill/trap/reset/loot from the legacy
                 * config; the new xp/charm/set/unique rows use small fixed
                 * shares carved out of the existing "loot" budget so the
                 * default INI / d2arch.ini still adds up to 100. Modders
                 * who tweak g_filler*Pct keep their existing semantics. */
                /* 1.9.0 flat weighted picker — mirrors the apworld's
                 * _build_filler_weights table so standalone and AP
                 * fillers produce the same item mix. Traps and Reset
                 * Point are gated by traps_enabled / skill_hunting (the
                 * latter via g_skillHuntingOn — INI/d2arch.ini side has
                 * no trap toggle for standalone, so traps always roll). */
                struct WRow { int weight; int rewardType; int extraIdx; };
                struct WRow wtable[] = {
                    { 10,  REWARD_GOLD,        0 },
                    { 15,  REWARD_XP,          0 },
                    { 10,  REWARD_STAT,        0 },
                    { 10,  REWARD_SKILL,       0 },
                    { (g_skillHuntingOn ? 5 : 0), REWARD_RESETPT, 0 },
                    {  2,  REWARD_TRAP,        TRAP_MONSTERS },
                    {  1,  REWARD_TRAP,        TRAP_SLOW     },
                    {  1,  REWARD_TRAP,        TRAP_WEAKEN   },
                    {  1,  REWARD_TRAP,        TRAP_POISON   },
                    {  1,  REWARD_LOOT,        0 },  /* Andariel */
                    {  2,  REWARD_LOOT,        1 },  /* Duriel */
                    {  2,  REWARD_LOOT,        2 },  /* Mephisto */
                    {  1,  REWARD_LOOT,        3 },  /* Diablo */
                    {  1,  REWARD_LOOT,        4 },  /* Baal */
                    {  9,  REWARD_DROP_CHARM,  0 },  /* picked at roll-time below */
                    {  9,  REWARD_DROP_SET,    0 },
                    {  6,  REWARD_DROP_UNIQUE, 0 },
                };
                int wcount = (int)(sizeof(wtable) / sizeof(wtable[0]));
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
                /* Pre-stash the trap/boss sub-index so the value-roll
                 * switch below stores it (overwritten for charm/set/unique
                 * which need their own catalog rolls). */
                g_questExtra[diff][qid] = extraIdx;

                /* Pre-roll the specific value for each reward type so the
                 * standalone delivery path and the spoiler file always
                 * agree. AP receives go through the same lookup tables. */
                switch (rewardType) {
                    case REWARD_GOLD:
                        /* Uniform 1..10000. */
                        g_questGold[diff][qid] = 1 + (rand() % 10000);
                        break;
                    case REWARD_XP:
                        /* Uniform 1..250000. */
                        g_questXP[diff][qid] = 1 + (rand() % 250000);
                        break;
                    case REWARD_TRAP:
                    case REWARD_LOOT:
                        /* g_questExtra was already pre-stashed by the
                         * weighted picker (specific trap variant / specific
                         * boss). Nothing more to roll. */
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
                        /* Pick from the parsed UniqueItems.txt catalog;
                         * if it didn't load (file missing) the index ends
                         * up clamped to 0 by Quests_UniqueName. */
                        if (g_uniqueCatalogCount > 0) {
                            g_questExtra[diff][qid] = rand() % g_uniqueCatalogCount;
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

    /* 1.9.0 — Pre-roll bonus check rewards using the same seed so the
     * standalone spoiler can list them alongside the quest rewards.
     * Forward decl (body in d2arch_bonuschecks.c, included before this
     * file in unity-build order — but quests.c is BEFORE bonuschecks.c,
     * so we just declare extern). */
    extern void Bonus_PreRollAllRewards(unsigned seed);
    Bonus_PreRollAllRewards(seed);

    /* 1.9.2 — Pre-roll extra check rewards (six new categories: Cow /
     * Merc / HF+Runes / NPC / Runeword / Cube). Same seed, +888 offset
     * inside the function so the rolls don't collide with Bonus or
     * quest rolls. The standalone spoiler lists these alongside the
     * other categories. */
    extern void Extra_PreRollAllRewards(unsigned seed);
    Extra_PreRollAllRewards(seed);

    /* 1.9.0 — Write the per-character standalone spoiler file. The
     * file documents what each filler quest will reward this character;
     * it lives next to the per-char state file so users can browse
     * their pre-rolled outcomes. Re-written every time AssignAllRewards
     * runs (deterministic from seed + g_filler*Pct). */
    Quests_WriteSpoilerFile();
}

/* 1.9.0 — Standalone spoiler file. Lives at
 * Game/Archipelago/d2arch_spoiler_<char>.txt. Lists every filler quest
 * across all 3 difficulties with the resolved reward type + value.
 * Progression quests are listed too for completeness. */
static void Quests_WriteSpoilerFile(void) {
    extern char g_charName[];
    if (!g_charName[0]) return;

    char dir[MAX_PATH], path[MAX_PATH];
    /* 1.9.0: spoiler lives in Game/Save/ next to the .d2s file the
     * spoiler describes — easier to find for the user than mixed in
     * with the shared bridge files. */
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
                    case REWARD_STAT:
                        _snprintf(rewStr, sizeof(rewStr), "+5 Stat Points");
                        break;
                    case REWARD_RESETPT:
                        _snprintf(rewStr, sizeof(rewStr), "+1 Reset Point");
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
                    default:
                        _snprintf(rewStr, sizeof(rewStr), "Skill Point");
                        break;
                }
                fprintf(f, "    %-32s -> %s\n", quest->name ? quest->name : "?", rewStr);
            }
            fprintf(f, "\n");
        }
    }

    /* Footer with summary counts so the user can see the final mix
     * without re-counting by hand.
     *
     * 1.9.0 fix: iterate the actual quest catalog (g_acts[][]) instead
     * of the full 800-slot g_questRewardType array. The latter contains
     * uninitialised slots that all read as REWARD_GOLD (enum value 0),
     * which made the spoiler over-count Gold by ~10x. */
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
    /* 1.9.0: REWARD_SKILL serves two purposes:
     *   - skill UNLOCK when the quest is a progression quest under SH=ON
     *   - skill POINT (+1) when the quest is a filler-rolled REWARD_SKILL
     * The footer counts each separately by checking the quest's
     * underlying type (progression types under SH=ON are unlocks; rest
     * are filler skill points). */
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
    /* 1.9.2 — Cap skillUnlocks at the actual unique-skill pool size.
     * If progression-quest count > pool size, the overflow slots fall
     * back to +1 Skill Point at delivery time (gameloop.c pool-exhaustion
     * fallback added in 1.9.2). The spoiler now shows both numbers
     * accurately so the user sees how many UNIQUE skills they can
     * unlock vs how many slots fall through to skill-point overflow. */
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

    /* 1.9.0 — Append bonus check rewards section if any bonus categories
     * are enabled. Includes shrines, urns, barrels, chests (per-diff),
     * set piece pickups (127), and gold milestones (17). */
    extern void Bonus_AppendSpoilerToFile(FILE* f);
    Bonus_AppendSpoilerToFile(f);

    /* 1.9.2 — Append extra check rewards section if any extra categories
     * are enabled. Includes cow level (9), merc milestones (6), Hellforge
     * + high runes (12), NPC dialogue (81), runeword crafting (50), cube
     * recipes (135). */
    extern void Extra_AppendSpoilerToFile(FILE* f);
    Extra_AppendSpoilerToFile(f);

    /* 1.9.2 — Append Custom Goal section ONLY when goal=4 (custom).
     * Lists every required target (from CSV parse) + current fired
     * status, plus the gold target. Lets the standalone player see
     * exactly what they need to complete to win. */
    extern void CustomGoal_AppendSpoilerToFile(FILE* f);
    CustomGoal_AppendSpoilerToFile(f);

    /* 1.9.2 — Grand total footer. Sums every check category that the
     * F1 Overview page displays so the spoiler footer matches the
     * in-game "Total Checks" line exactly. The Overview includes:
     *   - Quest checks (computed above as 'tot')
     *   - Skill checks (g_poolCount when g_skillHuntingOn)
     *   - Bonus + Extra checks (only enabled categories)
     *   - Collection (always 205 — set pieces 127 + runes 33 + gems 35
     *     + specials 10; the slots exist in the AP location_table
     *     regardless of Goal=Collection)
     *   - Zones (54 when g_zoneLockingOn — 18 gates × 3 difficulties) */
    extern int Bonus_GetTotalEnabledSlots(void);
    extern int Extra_GetTotalEnabledSlots(void);
    extern int g_poolCount;             /* d2arch_skilltree.c */
    extern BOOL g_zoneLockingOn;        /* d2arch_zones.c     */
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

    /* 1.9.2 — Total Reward Mix footer. Aggregates the pre-rolled
     * reward type counts across ALL three reward sources:
     *   - Quest rewards (already counted above as `totals[]`)
     *   - Bonus check rewards (per-slot pre-rolled in g_bonusState)
     *   - Extra check rewards (per-slot pre-rolled in g_extraState)
     * So the user can see exactly how many gold rewards / stat-point
     * rewards / charm drops / etc. their entire pool will deliver. */
    extern void Bonus_CountRewardsInto(int totals[10]);
    extern void Extra_CountRewardsInto(int totals[10]);
    int rewardTotals[10];
    for (int i = 0; i < 10; i++) rewardTotals[i] = totals[i];
    Bonus_CountRewardsInto(rewardTotals);
    Extra_CountRewardsInto(rewardTotals);

    /* Skill BR_SKILL is split into "unlock" vs "filler skill point"
     * for quests (based on quest type + skill_hunting). Bonus + Extra
     * BR_SKILL are always +1 skill point (filler). Aggregate the
     * skill-unlock count from the existing quest computation above
     * (skillUnlocks, already capped at g_poolCount); the filler skill
     * points = quest skillPoints + bonus BR_SKILL + extra BR_SKILL +
     * pool-overflow fallback (quests where pool was exhausted at
     * delivery time and fell through to +1 Skill Point per 1.9.2 fix). */
    int totalSkillPoints = skillPoints + skillPoolOverflow
                         + (rewardTotals[REWARD_SKILL] - totals[REWARD_SKILL]);
    /* Grand reward total = sum of every printed line below. Built
     * up explicitly to avoid double-counting REWARD_SKILL (which is
     * already split between skillUnlocks + totalSkillPoints). */
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

/* Per-area kill counts */
#define MAX_AREA_ID 150
static int g_areaKills[MAX_AREA_ID];

/* 1.8.0 cleanup: Treasure Cow state arrays + TREASURE_COW_SU_ID define extracted
 * to Tools/Archipelago/pending_reimplementation/TREASURE_COWS/ */

/* Forward declarations needed by zone gating system */
static BOOL IsTown(DWORD area);
static void ShowNotify(const char* text);

