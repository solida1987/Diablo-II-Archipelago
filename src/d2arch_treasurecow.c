/* d2arch_treasurecow.c — Archipelago Treasure Cow (Phases 5a-d) The complete Treasure Cow system: a) Spawn mechanism (fnSpawnMonster + Hell Bovine) b) Per-act counter + per-area tracking + chance roll c) Post-spawn HP boost so cow is boss-tier tanky d) Death detection -> Diablo/boss-tier loot drop e) Purple name "Archipelago Treasure Cow" via TBL override (applied separately via d2tbl.exe — no runtime modification needed) DESIGN CHOICES - Uses vanilla Hell Bovine (MonStats hcIdx 391). */

#include <stdlib.h>
#include <time.h>

#define TC_MAX_AREAS        256
#define TC_HELL_BOVINE_ID   391    /* vanilla MonStats hcIdx for Hell Bovine */
/* our own SuperUniques.txt row (Class=hellbovine, appended after the gate rows which end at 143). */
#define TC_COW_SU_HCIDX     99
#define TC_MAX_TRACKED      64     /* enough for 5×5 acts + headroom */

typedef struct {
    int   maxSpawns;
    int   chancePct;
    int   hpBonus;                   /* flat HP boost (in 256ths — 256 = 1 HP) */
    int   resistPct;                 /* fire/cold/lightning/poison resist % */
    int   spawnedCount;
    BOOL  areaHasCow[TC_MAX_AREAS];
} TreasureCowAct;

typedef struct {
    int    unitId;                   /* D2 unit ID of the spawned cow */
    int    areaId;                   /* which area it's in */
    void*  pUnit;                    /* last-known unit pointer (may go stale) */
    BOOL   alive;                    /* last-known alive state */
    BOOL   lootDropped;              /* did we already trigger loot for its death? */
} TrackedCow;

static TreasureCowAct g_tcActs[5];
static TrackedCow     g_tcTracked[TC_MAX_TRACKED];
static int            g_tcTrackedCount = 0;
static int            s_tc_lastArea = 0;
static DWORD          s_tc_lastRollTick = 0;          /* for re-roll */
static BOOL           s_tc_randSeeded = FALSE;
#define TC_REROLL_INTERVAL_MS 60000                    /* re-roll every 60s if camped */

/* dedicated treasure-cow log file. */
static void TCLog(const char* fmt, ...) {
    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(path, "%streasure_cow.log", dir);

    FILE* f = fopen(path, "a");
    if (!f) return;

    /* Timestamp prefix — wall-clock ms, mirrors what the user sees in the status overlay, easier to correlate with screenshots. */
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);

    /* Add newline if format string didn't end with one */
    size_t flen = strlen(fmt);
    if (flen == 0 || fmt[flen - 1] != '\n')
        fputc('\n', f);

    fclose(f);

    /* Also pipe through the main Log so it shows up in d2arch.log too. */
    char buf[512];
    va_list ap2;
    va_start(ap2, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap2);
    va_end(ap2);
    buf[sizeof(buf) - 1] = 0;
    Log("[TC] %s", buf);
}

static int TreasureCow_AreaToAct(int areaId) {
    if (areaId >=   1 && areaId <=  39) return 1;
    if (areaId >=  40 && areaId <=  74) return 2;
    if (areaId >=  75 && areaId <= 102) return 3;
    if (areaId >= 103 && areaId <= 108) return 4;
    if (areaId >= 109 && areaId <= 132) return 5;
    return 0;
}


static BOOL TreasureCow_IsTown(int areaId) {
    return areaId ==   1 || areaId ==  40 || areaId ==  75 ||
           areaId == 103 || areaId == 109;
}

/* Config loader. Format: ACT MAX_SPAWNS CHANCE_PCT HP_BONUS RESIST_PCT */
static void TreasureCow_LoadConfig(void) {
    /* Defaults */
    /* 2.1 fix (#17 — cow effectively unkillable): the old boosts (up to +100000 max HP) made the cow impossible to kill, because D2 monster life-regen scales with MAX HP — a 100k-HP boost also handed it ~100k- scaled regen that out-healed weak/early skills (reported: a L21 Frost Nova barely dented it). */
    /* hpBonus is now a PERCENTAGE of the cow's own level-matched HP, not a flat amount. */
    /* Testrunde 2026-08-11 (Marco): max 3 cows per act (was 5) and a chance
     * that stops meaning "everyone meets one in Den of Evil" — 25%/area put a
     * boss-tier cow in front of level-1 characters ("everyone got a treasure
     * cow and are too strong"). Early-act HP also came down: +150% in Act 1
     * was unkillable at the levels Act 1 is played at. treasure_cows.dat can
     * still override all of it per act. */
    int defaultHp[5]     = {    60,     90,    120,     160,     200 };
    int defaultResist[5] = {    10,     15,     20,      25,     30 };
    /* BetaHub #12 (Marco, 20/8): three cows per act meant everybody met one
     * in every other zone — and a boss-tier surprise should be rare enough
     * to be an event. Two per act at 5% keeps it a story, not a toll. */
    for (int i = 0; i < 5; i++) {
        g_tcActs[i].maxSpawns    = 2;
        g_tcActs[i].chancePct    = 5;
        g_tcActs[i].hpBonus      = defaultHp[i];
        g_tcActs[i].resistPct    = defaultResist[i];
        g_tcActs[i].spawnedCount = 0;
        memset(g_tcActs[i].areaHasCow, 0, sizeof(g_tcActs[i].areaHasCow));
    }

    char path[MAX_PATH];
    GetArchDir(path, MAX_PATH);
    strcat(path, "treasure_cows.dat");

    FILE* f = fopen(path, "r");
    if (!f) {
        Log("TreasureCow: '%s' not found — using defaults\n", path);
        return;
    }

    char line[256];
    int  lineNum = 0, accepted = 0;
    while (fgets(line, sizeof(line), f)) {
        lineNum++;
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;

        int act = 0, maxSp = 0, chance = 0, hpBonus = 0, resist = 0;
        int n = sscanf(p, "%d %d %d %d %d", &act, &maxSp, &chance, &hpBonus, &resist);
        if (n < 3) {
            Log("TreasureCow: line %d malformed: '%s'\n", lineNum, p);
            continue;
        }
        if (act < 1 || act > 5) continue;
        if (maxSp < 0 || maxSp > 100) continue;
        if (chance < 0 || chance > 100) continue;
        g_tcActs[act - 1].maxSpawns = maxSp;
        g_tcActs[act - 1].chancePct = chance;
        if (n >= 4 && hpBonus >= 0)   g_tcActs[act - 1].hpBonus   = hpBonus;
        if (n >= 5 && resist  >= 0)   g_tcActs[act - 1].resistPct = resist;
        accepted++;
    }
    fclose(f);

    Log("TreasureCow: loaded %d act configs from '%s'\n", accepted, path);
    for (int i = 0; i < 5; i++) {
        Log("  Act %d: max=%d chance=%d%% hp=+%d resist=+%d%%\n",
            i + 1, g_tcActs[i].maxSpawns, g_tcActs[i].chancePct,
            g_tcActs[i].hpBonus, g_tcActs[i].resistPct);
    }
}

static void TreasureCow_Reset(void) {
    s_tc_lastArea = 0;
    s_tc_lastRollTick = 0;
    g_tcTrackedCount = 0;
    memset(g_tcTracked, 0, sizeof(g_tcTracked));
    TreasureCow_LoadConfig();
    TCLog("RESET state cleared, config reloaded — see d2arch.log for "
          "per-act chance/max numbers");
    Log("TreasureCow: state reset (tracked cows cleared, config reloaded)\n");
}

/* Post-spawn stat boost. */

/* Level to scale the cow to. */
static int TreasureCow_ScaleLevel(void) {
    void* p = Player();
    if (!p || !fnGetStat) return 0;
    int lvl = 0;
    __try { lvl = fnGetStat(p, 12, 0); }   /* STAT_LEVEL */
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return (lvl > 0 && lvl < 110) ? lvl : 0;
}

static void TreasureCow_BoostStats(void* pUnit, int actIdx) {
    if (!pUnit || !fnAddStat) return;
    TreasureCowAct* ta = &g_tcActs[actIdx];

    __try {
        /* 1. LEVEL-MATCH. A Hell Bovine is natively level 28/61/81, so in Act 1 Normal it arrives 18 levels above everything around it. Pulling it down to the area's own level makes its native HP, damage, defence and experience all land where the player is. STAT_LEVEL = 12. */
        int areaLvl = TreasureCow_ScaleLevel();
        int nativeLvl = 0;
        if (fnGetStat) nativeLvl = fnGetStat(pUnit, 12, 0);
        if (areaLvl > 0 && fnSetStat) {
            fnSetStat(pUnit, 12, areaLvl, 0);
        }

        /* 1b. DAMAGE IS BAKED AT SPAWN (BetaHub #12). Setting STAT_LEVEL
         * only changes what is DERIVED from the level. The damage, attack
         * rating and defence stats were already written by the spawn at the
         * cow's native level — a level-8 character in Act 1 met level-8 HP
         * but level-28 fists, which is the reported one-shot. Scale the
         * baked stats by the same ratio the level moved.
         * 19=attackrating 21/22=min/maxdamage 23/24=secondary 31=defense */
        /* Both directions on purpose: early it comes DOWN to the player,
         * and an over-levelled player in an old act meets a cow scaled UP —
         * "dependent on the player's level" cuts both ways. */
        if (areaLvl > 0 && nativeLvl > 0 && nativeLvl != areaLvl
            && fnGetStat && fnSetStat) {
            /* HP is on this list now (6=current, 7=max). It never was, so a
             * superunique cow kept its native-level life times the unique
             * multiplier while everything else came down — the reported
             * "way too much life when you meet it early". The percentage
             * bonus below then applies to the LEVEL-MATCHED life. */
            static const int dmgStats[8] = { 19, 21, 22, 23, 24, 31, 6, 7 };
            for (int s = 0; s < 8; s++) {
                int v = fnGetStat(pUnit, dmgStats[s], 0);
                if (v <= 0) continue;
                int scaled = (int)(((__int64)v * areaLvl) / nativeLvl);
                if (scaled < 1) scaled = 1;
                fnSetStat(pUnit, dmgStats[s], scaled, 0);
            }
            Log("TreasureCow: baked stats scaled %d -> %d (dmg/AR/def/HP)\n",
                nativeLvl, areaLvl);
        }

        /* 2. HP as a MULTIPLIER of its own HP, not a flat brick. D2 derives monster life regen from MAX HP (hellbovine DamageRegen=2), so every point of max HP we add also adds regen. A flat +2000 on a 285-HP cow was an 8x multiplier and therefore 8x the regen -- the reported "crazy high regen". A percentage keeps the cow boss-tier while the regen stays proportional to what a monster of that level should have. STAT_HITPOINTS (current) = 6, STAT_MAXHP = 7; both are 8.8 fixed point, hence the 256 scaling. */
        int pct = ta->hpBonus;
        if (pct > 1000) pct = 250;              /* legacy flat value in .dat */
        int curMax = 0;
        if (fnGetStat) curMax = fnGetStat(pUnit, 7, 0);
        if (curMax > 0) {
            /* 64-bit intermediate: curMax is already 8.8 fixed point, so a high-level cow times 250% overflows a 32-bit multiply. */
            __int64 add = ((__int64)curMax * pct) / 100;
            if (add > 0x3FFFFFFF) add = 0x3FFFFFFF;
            fnAddStat(pUnit, 7, (int)add, 0);
            fnAddStat(pUnit, 6, (int)add, 0);
        }

        /* 3. Resistances: SET, never ADD, and scaled to the PLAYER's level.
         *
         * Adding stacked on whatever the spawn already gave the unit — with
         * the row's old Magic Resistant mod that stack crossed 100 and the
         * cow met a level-15 character as "Immune to Lightning". Setting an
         * absolute value makes this line the only author of the number, and
         * the hard cap stays far below the 100 where immunity begins.
         *
         * The curve: the act's configured resist is the level-99 ceiling,
         * scaled down linearly by the player's level. A level-15 Act 1
         * character meets ~2%, a level-85 Act 5 one meets ~26%. */
        {
            int rTarget = (ta->resistPct * (areaLvl > 0 ? areaLvl : 1)) / 99;
            if (rTarget > 60) rTarget = 60;
            if (rTarget < 0)  rTarget = 0;
            if (fnSetStat) {
                fnSetStat(pUnit, 39, rTarget, 0);   /* fire      */
                fnSetStat(pUnit, 41, rTarget, 0);   /* lightning */
                fnSetStat(pUnit, 43, rTarget, 0);   /* cold      */
                fnSetStat(pUnit, 45, rTarget, 0);   /* poison    */
            }
            Log("TreasureCow: resists SET to %d%% (act ceiling %d%%, level %d)\n",
                rTarget, ta->resistPct, areaLvl);
        }

        Log("TreasureCow: level=%d HP %d -> %d (+%d%%) resist +%d%% (act %d)\n",
            areaLvl, curMax / 256,
            (int)((curMax + ((__int64)curMax * pct) / 100) / 256),
            pct, ta->resistPct, actIdx + 1);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("TreasureCow: EXCEPTION during stat boost\n");
    }
}

/* Spawn helper — reads player coords, picks random dir+dist, retries up to 30 times. */
static void* TreasureCow_Spawn(void* pGame, void* pSrvPlayer, int* outUnitId) {
    *outUnitId = 0;
    __try {
        DWORD pPath = *(DWORD*)((DWORD)pSrvPlayer + 0x2C);
        if (!pPath) return NULL;
        DWORD pRoom = *(DWORD*)(pPath + 0x1C);
        if (!pRoom) return NULL;
        int playerX = (int)*(unsigned short*)(pPath + 0x02);
        int playerY = (int)*(unsigned short*)(pPath + 0x06);

        /* 40-300 was almost ALWAYS outside the player's loaded rooms (a room
         * spans 8x8-32x32 subtiles, the neighbour chain maybe ±60) — and since
         * D2Common ASSERTS on a room/coordinate mismatch instead of returning
         * NULL, the "30 attempts" never retried once: the first bad draw WAS
         * the cow crash. 25-100 keeps the cow off-screen-ish but inside the
         * loaded chain, and Room_ForPoint (d2arch_gameloop.c) now proves every
         * candidate point before anything is spawned at it. */
        int minDist = 25, maxDist = 100;
        static const int dx8[8] = { 1, 1, 0,-1,-1,-1, 0, 1};
        static const int dy8[8] = { 0, 1, 1, 1, 0,-1,-1,-1};

        for (int attempt = 0; attempt < 30; attempt++) {
            int dir  = rand() & 7;
            int dist = minDist + (rand() % (maxDist - minDist));
            int x    = playerX + dx8[dir] * dist;
            int y    = playerY + dy8[dir] * dist;
            if (x < 0 || y < 0) continue;
            DWORD spawnRoom = Room_ForPoint(x, y);
            if (!spawnRoom) continue;            /* point in no loaded room — NOW the retry loop actually retries */

            /* spawn the real SUPER-UNIQUE, not a plain Hell Bovine. */
            void* cow = NULL;
            int suRow = CustomBoss_SuRowFromHcIdx(TC_COW_SU_HCIDX);
            if (suRow >= 0 && fnSpawnSuperUnique) {
                cow = fnSpawnSuperUnique(pGame, (void*)spawnRoom, x, y, suRow);
            } else if (attempt == 0) {
                /* Old game data (no cow row) or the export is missing: fall back to the plain bovine so the feature degrades instead of disappearing. */
                TCLog("SU row for hcIdx %d not found (row=%d, fn=%p) — "
                      "falling back to plain Hell Bovine",
                      TC_COW_SU_HCIDX, suRow, (void*)fnSpawnSuperUnique);
            }
            if (!cow) {
                if (!fnSpawnMonster) return NULL;
                cow = fnSpawnMonster(pGame, (void*)spawnRoom, x, y,
                                     TC_HELL_BOVINE_ID, 1, -1, 0);
            }
            if (cow) {
                /* Read unitId from pUnit+0x0C (standard D2 UnitAny offset) */
                __try {
                    *outUnitId = (int)*(DWORD*)((DWORD)cow + 0x0C);
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
                Log("TreasureCow: SPAWN OK at (%d,%d) unit=%p unitId=%d attempt=%d\n",
                    x, y, (DWORD)cow, *outUnitId, attempt);
                return cow;
            }
        }
        Log("TreasureCow: failed to spawn after 30 attempts\n");
        return NULL;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("TreasureCow: EXCEPTION during spawn\n");
        return NULL;
    }
}

/* THE COW'S HOARD — the engine's own treasure class
 *
 * Two earlier attempts paid the cow out from our side, and both had a tell.
 * The first handed items over through fnCI: that path knows nothing about
 * magic find and quietly gives up when the inventory is full, which is where
 * every "the cow dropped nothing" report came from. The second spawned sparkly
 * chests and let D2 fill them — the loot was real, but the chests are solid
 * objects, and in a one-tile corridor one of them walls the player in. A chest
 * with nowhere to stand is simply lost, so the reward shrank silently too.
 *
 * The cow is a superunique, and SuperUniques.txt carries TC / TC(N) / TC(H)
 * columns. The Cow King has always used his; ours were empty, so the cow fell
 * through to the base monster's ordinary "Act 4 Unique B". Filling those three
 * cells in hands the whole thing to the engine: magic find, quality rolls and
 * item level all apply because it is the same code that drops Baal's loot.
 * Nothing of ours is placed on the floor, so nothing can block a corridor and
 * nothing can be lost. See "AP Cow Hoard" in TreasureClassEx.txt. */

/* Announce a killed cow. The drop itself is the engine's, on the death it
 * already saw; this is the message, plus the log line that lets a "the cow
 * gave me nothing" report be checked against whether we even saw the kill. */
static void TreasureCow_AwardHoard(const char* why, int unitId) {
    ShowNotify("The cow's hoard spills out!");
    TCLog("cow unitId=%d %s -> hoard is the AP Cow Hoard treasure class",
          unitId, why);
    Log("TreasureCow: cow unitId=%d %s — hoard dropped by treasure class\n",
        unitId, why);
}

/* Check tracked cows each tick: if any died, trigger loot drop. */
static void TreasureCow_CheckDeaths(void) {
    for (int i = 0; i < g_tcTrackedCount; i++) {
        TrackedCow* tc = &g_tcTracked[i];
        if (!tc->alive || tc->lootDropped) continue;
        if (!tc->pUnit) { tc->alive = FALSE; continue; }

        BOOL died = FALSE;
        BOOL lost = FALSE;
        __try {
            /* Identity check FIRST: the cached pUnit is a raw server pointer polled for the whole session. */
            DWORD liveId = *(DWORD*)((DWORD)tc->pUnit + 0x0C);
            if (liveId != (DWORD)tc->unitId) {
                lost = TRUE;
            } else {
                int mode     = *(int*)((DWORD)tc->pUnit + 0x10);
                DWORD flags  = *(DWORD*)((DWORD)tc->pUnit + 0xC4);
                /* MODE_DEATH(0) is unreliable due to SEH but MODE_DEAD(12) + UNITFLAG_ISDEAD(0x10000) are solid (same logic as ScanMonsters). */
                if (mode == 12 || (flags & 0x10000)) died = TRUE;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            /* Unit freed by D2 — cow is LOST, not killed: no free loot. */
            lost = TRUE;
        }

        if (lost) {
            /* BetaHub #12: "presumed killed" was a lie with a message on it.
             * The server recycles units in rooms the player walked away from
             * — INSIDE the same area — so retreating from a live cow tripped
             * this path, announced "the hoard spills out", and the engine
             * (which never saw a death) dropped nothing. A vanished unit is
             * a vanished unit: no hoard, no message that promises one. Real
             * kills are caught by the death poll the same tick they happen.
             * The slot is given back so the act can roll another cow. */
            tc->alive = FALSE;
            int here = GetCurrentArea();
            TCLog("cow unitId=%d LOST (unit recycled, player %d -> %d) — "
                  "no hoard, slot freed", tc->unitId, tc->areaId, here);
            Log("TreasureCow: cow unitId=%d LOST — slot freed, no loot\n",
                tc->unitId);
            {
                int act = TreasureCow_AreaToAct(tc->areaId);
                if (act >= 1 && act <= 5) {
                    TreasureCowAct* ta = &g_tcActs[act - 1];
                    if (ta->spawnedCount > 0) ta->spawnedCount--;
                    if (tc->areaId >= 0 && tc->areaId < TC_MAX_AREAS)
                        ta->areaHasCow[tc->areaId] = FALSE;
                }
            }
            continue;
        }
        if (died) {
            tc->alive = FALSE;
            tc->lootDropped = TRUE;
            /* The whole point of this creature is that killing it is worth it. */
            TCLog("cow unitId=%d DIED", tc->unitId);
            TreasureCow_AwardHoard("died", tc->unitId);
        }
    }
}

/* cheat-menu test spawn. */
static void TreasureCow_CheatSpawn(void* pGame) {
    if (!pGame) { TCLog("CHEAT SPAWN: no pGame"); return; }
    void* pSrvPlayer = CustomBoss_GetServerPlayer((DWORD)pGame);
    if (!pSrvPlayer) { TCLog("CHEAT SPAWN: no server player"); return; }

    int unitId = 0;
    void* cow = TreasureCow_Spawn(pGame, pSrvPlayer, &unitId);
    if (!cow) {
        TCLog("CHEAT SPAWN: FAILED (no free position?)");
        ShowNotify("Treasure Cow: spawn failed");
        return;
    }

    int curArea = GetCurrentArea();
    int actIdx  = 0;
    { int a = TreasureCow_AreaToAct(curArea); if (a >= 1 && a <= 5) actIdx = a - 1; }
    TreasureCow_BoostStats(cow, actIdx);

    if (g_tcTrackedCount < TC_MAX_TRACKED) {
        TrackedCow* tc  = &g_tcTracked[g_tcTrackedCount++];
        tc->unitId      = unitId;
        tc->areaId      = curArea;
        tc->pUnit       = cow;
        tc->alive       = TRUE;
        tc->lootDropped = FALSE;
    } else {
        TCLog("CHEAT SPAWN: tracking table full — this cow will drop nothing");
    }

    TCLog("CHEAT SPAWN OK area=%d unitId=%d unit=%p tracked=%d",
          curArea, unitId, cow, g_tcTrackedCount);
    Log("TreasureCow: CHEAT spawn in area %d (unitId=%d)\n", curArea, unitId);
    ShowNotify("Treasure Cow spawned!");
}

/* Per-tick entry point. */
static void TreasureCow_Tick(void* pGame) {
    static DWORD s_tc_heartbeatTick = 0;
    static unsigned long s_tc_callCount = 0;
    s_tc_callCount++;

    /* Periodic heartbeat — proves the function is actually running. */
    DWORD nowHB = GetTickCount();
    if (nowHB - s_tc_heartbeatTick > 10000) {
        s_tc_heartbeatTick = nowHB;
        TCLog("HEARTBEAT calls=%lu pGame=%p Player=%p fnSpawnMonster=%p tracked=%d",
              s_tc_callCount, pGame, (void*)(DWORD)(Player() ? 1 : 0),
              (void*)fnSpawnMonster, g_tcTrackedCount);
    }

    if (!pGame) {
        /* No log here — would spam every tick the menu is open. */
        return;
    }
    if (!Player()) {
        return;
    }

    /* Death-check runs EVERY tick (not just on area transition) so we catch kills promptly and drop loot where the cow died. */
    TreasureCow_CheckDeaths();

    /* cheat-menu test spawn. */
    if (g_cheatSpawnTreasureCow) {
        g_cheatSpawnTreasureCow = 0;
        TreasureCow_CheatSpawn(pGame);
    }

    if (!fnSpawnMonster) {
        /* This is a one-time symptom — log once per heartbeat window. */
        if (nowHB - s_tc_heartbeatTick < 100)
            TCLog("SKIP fnSpawnMonster=NULL — cow spawn logic disabled");
        return;
    }

    int curArea = GetCurrentArea();
    if (curArea <= 0 || curArea >= TC_MAX_AREAS) return;

    /* 1.8.5 FIX: rolled-once-per-area was bricking 75% of areas (1×25%=25% coverage). */
    DWORD now = GetTickCount();
    BOOL  areaChanged = (curArea != s_tc_lastArea);
    BOOL  timerReady  = (s_tc_lastRollTick == 0) ||
                        (now - s_tc_lastRollTick >= TC_REROLL_INTERVAL_MS);
    if (!areaChanged && !timerReady) {
        /* No-op: same area + cool-down still active. */
        return;
    }
    s_tc_lastArea = curArea;

    if (!s_tc_randSeeded) {
        srand((unsigned)time(NULL) ^ GetTickCount() ^ 0xC0FFEE);
        s_tc_randSeeded = TRUE;
        TCLog("RNG seeded");
    }

    if (TreasureCow_IsTown(curArea)) {
        TCLog("SKIP area=%d is town — no cow spawn", curArea);
        s_tc_lastRollTick = now;       /* don't re-trigger every tick in town */
        return;
    }

    int act = TreasureCow_AreaToAct(curArea);
    if (act < 1 || act > 5) {
        TCLog("SKIP area=%d -> act=%d (out of range)", curArea, act);
        s_tc_lastRollTick = now;
        return;
    }

    TreasureCowAct* ta = &g_tcActs[act - 1];
    if (ta->spawnedCount >= ta->maxSpawns) {
        TCLog("SKIP act=%d full (%d/%d cows spawned this session)",
              act, ta->spawnedCount, ta->maxSpawns);
        s_tc_lastRollTick = now;
        return;
    }
    if (ta->areaHasCow[curArea]) {
        TCLog("SKIP act=%d area=%d already has a cow this session",
              act, curArea);
        s_tc_lastRollTick = now;
        return;
    }

    int roll = rand() % 100;
    BOOL hit = (roll < ta->chancePct);
    TCLog("ROLL act=%d area=%d count=%d/%d roll=%d vs chance=%d%% -> %s (%s)",
          act, curArea, ta->spawnedCount, ta->maxSpawns,
          roll, ta->chancePct,
          hit ? "HIT — spawning cow" : "miss",
          areaChanged ? "area entry" : "re-roll");
    /* Keep the original Log() too so existing log-readers keep working. */
    Log("TreasureCow: act=%d area=%d count=%d/%d roll=%d vs chance=%d%% (%s)\n",
        act, curArea, ta->spawnedCount, ta->maxSpawns, roll, ta->chancePct,
        areaChanged ? "area entry" : "re-roll");

    s_tc_lastRollTick = now;
    if (!hit) return;

    void* pSrvPlayer = CustomBoss_GetServerPlayer((DWORD)pGame);
    if (!pSrvPlayer) {
        TCLog("FAIL CustomBoss_GetServerPlayer returned NULL — skipping spawn");
        return;
    }

    int unitId = 0;
    void* cow = TreasureCow_Spawn(pGame, pSrvPlayer, &unitId);
    if (!cow) {
        TCLog("FAIL TreasureCow_Spawn returned NULL act=%d area=%d "
              "(see d2arch.log for spawn-attempt details)", act, curArea);
        return;
    }

    TCLog("SPAWN OK act=%d area=%d unitId=%d unit=%p", act, curArea, unitId, cow);

    /* Boost cow stats. */
    TreasureCow_BoostStats(cow, act - 1);

    /* Mark area + increment counter. */
    ta->areaHasCow[curArea] = TRUE;
    ta->spawnedCount++;

    /* Tip the player off that something unusual is in this area. */
    /* shorter so it doesn't overflow at the big-warning font size (font is now also INI-tunable via [Notifications] BigWarnFont). */
    ShowBigWarning("SOMETHING UNUSUAL IS NEARBY...");

    /* Add to tracking list for death-detection. */
    if (g_tcTrackedCount < TC_MAX_TRACKED) {
        TrackedCow* tc  = &g_tcTracked[g_tcTrackedCount++];
        tc->unitId      = unitId;
        tc->areaId      = curArea;
        tc->pUnit       = cow;
        tc->alive       = TRUE;
        tc->lootDropped = FALSE;
    } else {
        TCLog("WARN tracking table full (%d/%d) — death-detection may miss "
              "this cow's loot", g_tcTrackedCount, TC_MAX_TRACKED);
    }

    TCLog("DONE act=%d %d/%d (area=%d marked, tracked=%d)",
          act, ta->spawnedCount, ta->maxSpawns, curArea, g_tcTrackedCount);
    Log("TreasureCow: act %d %d/%d (area %d marked, tracked=%d)\n",
        act, ta->spawnedCount, ta->maxSpawns, curArea, g_tcTrackedCount);
}
