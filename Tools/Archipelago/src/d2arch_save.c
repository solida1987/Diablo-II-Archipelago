
/* io.h provides _fileno/_commit for flushing .d2s writes through to disk
 * (prevents half-written .d2s on power loss / crash-on-save). */
#include <io.h>
#include <errno.h>

/* ================================================================
 * RESET REQUEST FLAG
 * ResetD2SFile zeroes the 30 skill bytes in the character's .d2s and
 * queues a reinvest run. Running that unconditionally on every character
 * load means a single bug in the reinvest chain wipes all skill points
 * for good. Instead, the reset only runs when the in-panel reset button
 * sets this flag by calling RequestD2SReset().
 * ================================================================ */
static volatile int g_resetRequested = 0;

/* Called by the reset-skills UI button (wire-up lives in d2arch_editor.c /
 * d2arch_drawall.c click handlers). Safe to call from any thread; the
 * consumer (OnCharacterLoad) reads and clears the flag exactly once. */
static void RequestD2SReset(void) {
    g_resetRequested = 1;
}

/* ================================================================
 * REINVEST CONSUME HELPERS
 * The reinvest file d2arch_reinvest_<char>.dat is the source of truth
 * for "points that were taken off skills during a reset and need to go
 * back on". Previously the consumer (in d2arch_gameloop.c) would delete
 * the file immediately on entry — a crash mid-consume meant the points
 * were lost forever.
 *
 * The new flow:
 *   1. BeginReinvestConsume renames the file to a `.consuming` suffix.
 *      If the rename fails (file already being consumed by another tick,
 *      permissions, etc.) the caller skips this tick.
 *   2. Caller reads the .consuming file and applies the points.
 *   3. EndReinvestConsume deletes the .consuming file only after all
 *      points have been applied successfully.
 * ================================================================ */
static int BeginReinvestConsume(char* outConsumingPath, size_t pathSize) {
    if (!outConsumingPath || pathSize == 0) return 0;
    if (!g_charName[0]) return 0;
    char archDir[MAX_PATH], origPath[MAX_PATH];
    GetCharFileDir(archDir, MAX_PATH);
    snprintf(origPath, sizeof(origPath), "%sd2arch_reinvest_%s.dat", archDir, g_charName);
    if (GetFileAttributesA(origPath) == INVALID_FILE_ATTRIBUTES) return 0;
    snprintf(outConsumingPath, pathSize, "%s.consuming", origPath);
    /* If an old .consuming file exists (crash during previous consume), remove
     * it before the rename so MoveFileExA doesn't fail on the pre-existing target. */
    DeleteFileA(outConsumingPath);
    if (!MoveFileExA(origPath, outConsumingPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Log("BeginReinvestConsume: rename failed err=%lu src='%s'\n", GetLastError(), origPath);
        outConsumingPath[0] = 0;
        return 0;
    }
    return 1;
}

static void EndReinvestConsume(const char* consumingPath) {
    if (!consumingPath || !consumingPath[0]) return;
    if (!DeleteFileA(consumingPath))
        Log("EndReinvestConsume: DeleteFileA failed err=%lu path='%s'\n", GetLastError(), consumingPath);
}

/* ================================================================
 * GENERATIONAL .d2s BACKUP RETENTION
 * Backups are named <char>.d2s.YYYYMMDD-HHMMSS.bak and sort alphabetically
 * by timestamp. We keep the newest keepN and delete the rest.
 * ================================================================ */
static int CompareStrPtrs(const void* a, const void* b) {
    const char* sa = *(const char* const*)a;
    const char* sb = *(const char* const*)b;
    return strcmp(sa, sb);
}

static void PruneOldBackups(const char* d2sPath, int keepN) {
    if (!d2sPath || !d2sPath[0] || keepN < 0) return;
    /* Split d2sPath into directory and base filename. */
    char dir[MAX_PATH], base[MAX_PATH];
    strncpy(dir, d2sPath, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;
    char* sl = strrchr(dir, '\\');
    if (!sl) return;
    *(sl + 1) = 0;
    strncpy(base, sl + 1, sizeof(base) - 1);
    base[sizeof(base) - 1] = 0;

    char pat[MAX_PATH];
    snprintf(pat, sizeof(pat), "%s%s.*.bak", dir, base);

    /* Collect matches. Worst case we cap at 64 — more than that and something
     * is very wrong anyway. */
    enum { MAX_BAK = 64 };
    char* names[MAX_BAK];
    int count = 0;

    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(pat, &ffd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (count >= MAX_BAK) break;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        size_t nlen = strlen(ffd.cFileName);
        names[count] = (char*)malloc(nlen + 1);
        if (!names[count]) break;
        memcpy(names[count], ffd.cFileName, nlen + 1);
        count++;
    } while (FindNextFileA(h, &ffd));
    FindClose(h);

    if (count > 0)
        qsort(names, count, sizeof(char*), CompareStrPtrs);

    /* Delete all but newest keepN (tail of sorted list = newest timestamps). */
    int deleteUpTo = count - keepN;
    for (int i = 0; i < count; i++) {
        if (i < deleteUpTo) {
            char full[MAX_PATH];
            snprintf(full, sizeof(full), "%s%s", dir, names[i]);
            if (DeleteFileA(full))
                Log("PruneOldBackups: deleted '%s'\n", full);
            else
                Log("PruneOldBackups: DeleteFileA failed err=%lu path='%s'\n", GetLastError(), full);
        }
        free(names[i]);
    }
}

/* Save skill slots to file */
static void SaveSlots(void) {
    if (!g_charName[0] || !g_poolInitialized) return;

    char dir[MAX_PATH], path[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_slots_%s.dat", dir, g_charName);

    FILE* f = fopen(path, "w");
    if (!f) return;

    int cls = GetPlayerClass();
    if (cls < 0 && g_savedClass >= 0) cls = g_savedClass;
    fprintf(f, "class=%d\n", cls);

    for (int t = 0; t < 3; t++) {
        for (int s = 0; s < 10; s++) {
            int pidx = g_tabSlots[t][s];
            if (pidx >= 0 && pidx < g_poolCount) {
                fprintf(f, "%d,%d,%d\n", t, s, g_skillDB[g_pool[pidx].dbIndex].id);
            }
        }
    }
    fclose(f);
    Log("SaveSlots: saved to %s\n", path);

    /* Skill level files are saved by the panel button click handler (d2arch_drawall.c)
     * and by the reinvest system (d2arch_gameloop.c). We do NOT overwrite them here
     * because we cannot reliably read skill levels from the game at save time.
     * The files persist across sessions — they only get updated when the player
     * actually clicks a skill button or when reinvest completes. */
}

/* Load skill slots from file */
static void LoadSlots(void) {
    if (!g_charName[0] || !g_poolInitialized) return;

    char dir[MAX_PATH], path[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_slots_%s.dat", dir, g_charName);

    FILE* f = fopen(path, "r");
    if (!f) { Log("LoadSlots: no file for '%s'\n", g_charName); return; }

    /* Clear all assignments */
    for (int t = 0; t < 3; t++)
        for (int s = 0; s < 10; s++)
            g_tabSlots[t][s] = -1;
    for (int i = 0; i < g_poolCount; i++) {
        g_pool[i].assigned = FALSE;
        g_pool[i].assignTab = -1;
        g_pool[i].assignSlot = -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int cls;
        if (sscanf(line, "class=%d", &cls) == 1) {
            if (cls >= 0 && cls <= 6) g_savedClass = cls;
            continue;
        }
        int tab, slot, skillId;
        if (sscanf(line, "%d,%d,%d", &tab, &slot, &skillId) == 3) {
            if (tab < 0 || tab >= 3 || slot < 0 || slot >= 10) continue;
            /* Find this skill in pool */
            for (int i = 0; i < g_poolCount; i++) {
                if (g_skillDB[g_pool[i].dbIndex].id == skillId && !g_pool[i].assigned) {
                    g_pool[i].assigned = TRUE;
                    g_pool[i].assignTab = tab;
                    g_pool[i].assignSlot = slot;
                    g_tabSlots[tab][slot] = i;
                    break;
                }
            }
        }
    }
    fclose(f);

    int count = 0;
    for (int t = 0; t < 3; t++)
        for (int s = 0; s < 10; s++)
            if (g_tabSlots[t][s] >= 0) count++;
    g_slotsDirty = TRUE;
    g_slotsApplied = FALSE;
    Log("LoadSlots: loaded %d assignments for '%s'\n", count, g_charName);
}

/* Save state file (seed, skills, unlock status) */
static void SaveStateFile(void) {
    if (!g_charName[0] || !g_poolInitialized) return;

    char dir[MAX_PATH], path[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_state_%s.dat", dir, g_charName);

    FILE* f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "seed=%u\n", g_seed);
    fprintf(f, "num_starting=%d\n", NUM_STARTING);
    fprintf(f, "total_skills=%d\n", g_poolCount);
    /* 1.9.0 — pool_kind tells LoadStateFile whether to call
     * InitClassOnlySkills (SH=OFF, fixed 30 native skills) or
     * InitSkillPool (SH=ON, seeded 210-skill pool). Without this
     * marker LoadStateFile would always call InitSkillPool and
     * SH=OFF chars would see all 210 skills appear on the second
     * load — the bug the user hit. Backward compat: state files
     * missing this line fall back to InitSkillPool below. */
    fprintf(f, "pool_kind=%s\n", g_skillHuntingOn ? "hunt" : "class_only");
    fprintf(f, "saved_class_id=%d\n", g_savedClass);
    fprintf(f, "assignments=\n");
    for (int i = 0; i < g_poolCount; i++) {
        const SkillEntry* sk = &g_skillDB[g_pool[i].dbIndex];
        fprintf(f, "%s,%s,%d,%d\n", sk->name, sk->classCode, g_pool[i].unlocked ? 1 : 0, sk->id);
    }
    /* Save completed quests per difficulty */
    for (int d = 0; d < 3; d++)
        for (int a = 0; a < 5; a++)
            for (int q = 0; q < g_acts[a].num; q++) {
                int qid = g_acts[a].quests[q].id;
                if (qid > 0 && qid < MAX_QUEST_ID && g_questCompleted[d][qid])
                    fprintf(f, "quest_%d_%d=1\n", d, qid);
            }

    /* Save per-area kill counts */
    for (int i = 0; i < MAX_AREA_ID; i++)
        if (g_areaKills[i] > 0)
            fprintf(f, "areakills_%d=%d\n", i, g_areaKills[i]);

    /* questgold no longer saved — regenerated from seed by AssignAllRewards */

    fprintf(f, "difficulty=%d\n", g_currentDifficulty);

    /* Save pending rewards */
    fprintf(f, "pendingGold=%d\n", g_pendingRewardGold);
    fprintf(f, "pendingStatPts=%d\n", g_pendingRewardStatPts);
    fprintf(f, "pendingSkillPts=%d\n", g_pendingRewardSkillPts);
    fprintf(f, "pendingTraps=%d\n", g_pendingTrapSpawn);
    fprintf(f, "pendingLoot=%d\n", g_pendingLootDrop);
    fprintf(f, "pendingSlow=%d\n", g_pendingTrapSlow);
    fprintf(f, "pendingWeaken=%d\n", g_pendingTrapWeaken);
    fprintf(f, "pendingPoison=%d\n", g_pendingTrapPoison);
    fprintf(f, "resetPoints=%d\n", g_resetPoints);

    /* Save ALL settings per-character so switching chars preserves each one's config */
    /* 1.8.0: write the two new independent toggles + keep legacy field
     * populated so older mod builds can still read this file. */
    fprintf(f, "skill_hunting=%d\n", g_skillHuntingOn ? 1 : 0);
    fprintf(f, "zone_locking=%d\n",  g_zoneLockingOn  ? 1 : 0);
    fprintf(f, "game_mode=%d\n",     g_zoneLockingOn ? 1 : 0); /* legacy fallback */
    fprintf(f, "goal=%d\n", g_apGoal);
    fprintf(f, "starting_skills=%d\n", g_apStartingSkills);
    fprintf(f, "skill_pool_size=%d\n", g_apSkillPoolSize);
    fprintf(f, "quest_story=%d\n", g_apQuestStory);
    fprintf(f, "quest_hunting=%d\n", g_apQuestHunting);
    fprintf(f, "quest_kill_zones=%d\n", g_apQuestKillZones);
    fprintf(f, "quest_exploration=%d\n", g_apQuestExploration);
    fprintf(f, "quest_waypoints=%d\n", g_apQuestWaypoints);
    fprintf(f, "quest_level_milestones=%d\n", g_apQuestLevelMilestones);
    fprintf(f, "filler_gold_pct=%d\n", g_fillerGoldPct);
    fprintf(f, "filler_stat_pts_pct=%d\n", g_fillerStatPct);
    fprintf(f, "filler_skill_pts_pct=%d\n", g_fillerSkillPct);
    fprintf(f, "filler_trap_pct=%d\n", g_fillerTrapPct);
    fprintf(f, "filler_reset_pts_pct=%d\n", g_fillerResetPct);
    fprintf(f, "filler_loot_pct=%d\n", g_fillerLootPct);
    /* 1.8.0: bake shuffle/xp/class settings into per-char state so they're
     * frozen at character creation and survive title-screen toggle changes. */
    fprintf(f, "monster_shuffle=%d\n", g_monsterShuffleEnabled ? 1 : 0);
    fprintf(f, "boss_shuffle=%d\n",    g_bossShuffleEnabled    ? 1 : 0);
    fprintf(f, "shop_shuffle=%d\n",    g_shopShuffleEnabled    ? 1 : 0);
    fprintf(f, "entrance_shuffle=%d\n", g_entranceShuffleEnabled ? 1 : 0);
    fprintf(f, "xp_multiplier=%d\n",   g_xpMultiplier);
    fprintf(f, "class_filter=%d\n",    g_classFilter ? 1 : 0);
    fprintf(f, "cls_amazon=%d\n",      g_clsEnabled[0] ? 1 : 0);
    fprintf(f, "cls_sorceress=%d\n",   g_clsEnabled[1] ? 1 : 0);
    fprintf(f, "cls_necromancer=%d\n", g_clsEnabled[2] ? 1 : 0);
    fprintf(f, "cls_paladin=%d\n",     g_clsEnabled[3] ? 1 : 0);
    fprintf(f, "cls_barbarian=%d\n",   g_clsEnabled[4] ? 1 : 0);
    fprintf(f, "cls_druid=%d\n",       g_clsEnabled[5] ? 1 : 0);
    fprintf(f, "cls_assassin=%d\n",    g_clsEnabled[6] ? 1 : 0);
    fprintf(f, "i_play_assassin=%d\n", g_iPlayAssassin ? 1 : 0);
    fprintf(f, "death_link=%d\n",      g_apDeathLink   ? 1 : 0);

    /* 1.8.0 NEW — 15 preload-id fields for gated zone-locking.
     * g_actPreload[act-1][diff] where diff = 0/1/2 = Normal/NM/Hell.
     * Frozen at character creation; once baked, AP reconnects can't
     * change them (g_settingsFrozen guard in LoadAPSettings). */
    fprintf(f, "act1_preload_normal=%d\n",    g_actPreload[0][0]);
    fprintf(f, "act1_preload_nightmare=%d\n", g_actPreload[0][1]);
    fprintf(f, "act1_preload_hell=%d\n",      g_actPreload[0][2]);
    fprintf(f, "act2_preload_normal=%d\n",    g_actPreload[1][0]);
    fprintf(f, "act2_preload_nightmare=%d\n", g_actPreload[1][1]);
    fprintf(f, "act2_preload_hell=%d\n",      g_actPreload[1][2]);
    fprintf(f, "act3_preload_normal=%d\n",    g_actPreload[2][0]);
    fprintf(f, "act3_preload_nightmare=%d\n", g_actPreload[2][1]);
    fprintf(f, "act3_preload_hell=%d\n",      g_actPreload[2][2]);
    fprintf(f, "act4_preload_normal=%d\n",    g_actPreload[3][0]);
    fprintf(f, "act4_preload_nightmare=%d\n", g_actPreload[3][1]);
    fprintf(f, "act4_preload_hell=%d\n",      g_actPreload[3][2]);
    fprintf(f, "act5_preload_normal=%d\n",    g_actPreload[4][0]);
    fprintf(f, "act5_preload_nightmare=%d\n", g_actPreload[4][1]);
    fprintf(f, "act5_preload_hell=%d\n",      g_actPreload[4][2]);

    /* 1.8.0 NEW — Gate-key receipt state (54 bits: 3 diffs × 18 slots) */
    for (int d = 0; d < 3; d++) {
        for (int s = 0; s < GATEKEY_PER_DIFF; s++) {
            if (g_gateKeyReceived[d][s])
                fprintf(f, "gatekey_%d_%d=1\n", d, s);
        }
    }

    /* 1.8.2 NEW — Gate-boss kill state (54 bits: 3 diffs × 18 slots).
     * Independent of key receipt: kill marks the world (boss won't respawn),
     * key marks the inventory side (region opens). */
    for (int d = 0; d < 3; d++) {
        for (int s = 0; s < GATEKEY_PER_DIFF; s++) {
            if (g_gateBossKilled[d][s])
                fprintf(f, "gateboss_killed_%d_%d=1\n", d, s);
        }
    }

    /* Always persist zone keys when any have been received.
     * Reading them back in non-Zone-Unlock mode is harmless (unused),
     * but a mode-flip mid-session previously lost all keys. */
    {
        BOOL anyKey = FALSE;
        for (int i = 0; i < ZONE_KEY_COUNT; i++) {
            if (g_zoneKeyReceived[i]) { anyKey = TRUE; break; }
        }
        if (anyKey) {
            for (int i = 0; i < ZONE_KEY_COUNT; i++) {
                if (g_zoneKeyReceived[i])
                    fprintf(f, "zonekey_%d=1\n", i);
            }
            fprintf(f, "lastSafeArea=%d\n", g_lastSafeArea);
        }
    }

    /* 1.9.0 — Bonus check counters + fired bitmap. */
    {
        extern void Bonus_SaveToFile(FILE* f);
        Bonus_SaveToFile(f);
    }

    fclose(f);
    Log("SaveStateFile: saved to %s\n", path);
}

/* Write checks file for AP bridge compatibility.
 * AP location IDs: LOCATION_BASE(42000) + quest_id + (difficulty * 1000)
 * Bridge adds LOCATION_BASE, so we write quest_id + (diff * 1000) here.
 * Uses g_questCompleted[diff][qid] to cover ALL difficulties, not just current.
 *
 * 1.8.5 FIX: also emit gate-boss kills. Pre-1.8.5 the gate-kill handler
 * tried to send these via WriteAPCommand("location N") but the bridge's
 * command dispatcher only handles `connect`/`disconnect` actions — every
 * gate kill was silently dropped, leaving the AP server without the check
 * (and therefore never echoing back the gate-key item, leaving the next
 * region permanently locked in multiworld games). Now we write them as
 * `check=N` lines exactly like quest completions, which the bridge's
 * read_pending_checks already understands. The encoding mirrors the loc
 * ID used by the gate-kill block in d2arch_gameloop.c:
 *     locId = 47000 + diff*1000 + actIdx*10 + gi
 *     check_n = locId - LOCATION_BASE = 5000 + diff*1000 + actIdx*10 + gi
 */
static void WriteChecksFile(void) {
    if (!g_charName[0]) return;
    char dir[MAX_PATH], path[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_checks_%s.dat", dir, g_charName);
    FILE* f = fopen(path, "w");
    if (!f) return;
    for (int diff = 0; diff < 3; diff++) {
        int offset = diff * 1000;
        for (int qid = 1; qid < MAX_QUEST_ID; qid++) {
            if (g_questCompleted[diff][qid])
                fprintf(f, "check=%d\n", qid + offset);
        }
    }
    /* 1.8.5 — gate-boss kills. Mirrors loc-id calc in d2arch_gameloop.c. */
    for (int diff = 0; diff < 3; diff++) {
        for (int slot = 0; slot < GATEKEY_PER_DIFF; slot++) {
            if (!g_gateBossKilled[diff][slot]) continue;
            int act = 0, gate = 0;
            if (!GateKey_ActGateFromSlot(slot, &act, &gate)) continue;
            int check_n = 5000 + diff * 1000 + act * 10 + gate;
            fprintf(f, "check=%d\n", check_n);
        }
    }
    /* 1.9.0 — Collection checks. 110 location offsets (0..109) at
     * COLL_LOC_BASE = 50000, so check_n = 50000+offset - LOCATION_BASE
     * = 8000 + offset. Each fired offset (set complete, rune/gem/
     * special collected) emits one line. The s_collCheckFired bitset
     * lives in d2arch_collections.c — accessed via Coll_IsCheckFired. */
    {
        extern BOOL Coll_IsCheckFired(int locOffset);
        for (int off = 0; off < 110; off++) {
            if (Coll_IsCheckFired(off)) {
                int check_n = 8000 + off;
                fprintf(f, "check=%d\n", check_n);
            }
        }
    }

    /* 1.9.0 — Bonus check categories. Iterates every fired bonus
     * location and emits the matching check_n (apId minus LOCATION_BASE
     * = 42000). Range covers 60000-65226. */
    {
        extern int Bonus_NextFiredApId(int* iterState);
        int it = 0, apId;
        while ((apId = Bonus_NextFiredApId(&it)) >= 0) {
            int check_n = apId - 42000;  /* LOCATION_BASE */
            fprintf(f, "check=%d\n", check_n);
        }
    }
    fclose(f);
}

/* Load completed quests and area kills from state file */
static void LoadChecks(void) {
    /* Reset all quests and kill counts */
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++) {
            g_acts[a].quests[q].completed = FALSE;
            g_acts[a].quests[q].killCount = 0;
        }
    memset(g_areaKills, 0, sizeof(g_areaKills));
    /* 1.8.0 cleanup: Treasure Cow state reset extracted (game-new) */
    g_deadCount = 0;

    if (!g_charName[0]) return;
    char dir[MAX_PATH], path[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_state_%s.dat", dir, g_charName);

    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    memset(g_questCompleted, 0, sizeof(g_questCompleted));
    memset(g_questKillCount, 0, sizeof(g_questKillCount));
    /* NOTE: Do NOT clear g_questGold or g_questRewardType here!
     * They were already populated by AssignAllRewards() which runs
     * before LoadChecks(). Clearing them would reset all filler
     * rewards to 0 (= REWARD_SKILL), making every quest show "Unlock". */

    while (fgets(line, sizeof(line), f)) {
        int d2, qid;
        /* New format: quest_DIFF_ID=1 */
        if (sscanf(line, "quest_%d_%d=1", &d2, &qid) == 2) {
            if (d2 >= 0 && d2 < 3 && qid >= 0 && qid < MAX_QUEST_ID) {
                g_questCompleted[d2][qid] = TRUE;
                /* Also set on struct for current difficulty */
                if (d2 == g_currentDifficulty) {
                    for (int a = 0; a < 5; a++)
                        for (int q = 0; q < g_acts[a].num; q++)
                            if (g_acts[a].quests[q].id == qid)
                                g_acts[a].quests[q].completed = TRUE;
                }
            }
        }
        /* Old format backward compat: quest_ID=1 (treated as Normal) */
        else if (sscanf(line, "quest_%d=1", &qid) == 1 && qid > 0) {
            if (qid < MAX_QUEST_ID) {
                g_questCompleted[0][qid] = TRUE;
                for (int a = 0; a < 5; a++)
                    for (int q = 0; q < g_acts[a].num; q++)
                        if (g_acts[a].quests[q].id == qid)
                            g_acts[a].quests[q].completed = TRUE;
            }
        }
        int areaId, kills;
        if (sscanf(line, "areakills_%d=%d", &areaId, &kills) == 2) {
            if (areaId >= 0 && areaId < MAX_AREA_ID)
                g_areaKills[areaId] = kills;
        }
        /* questgold lines ignored — gold amounts now regenerated from seed by AssignAllRewards */
        if (sscanf(line, "difficulty=%d", &d2) == 1) {
            if (d2 >= 0 && d2 < 3) g_currentDifficulty = d2;
        }
        /* Load pending rewards */
        {
            int tmpVal;
            if (sscanf(line, "pendingGold=%d", &tmpVal) == 1)
                g_pendingRewardGold = tmpVal;
            if (sscanf(line, "pendingStatPts=%d", &tmpVal) == 1)
                g_pendingRewardStatPts = tmpVal;
            if (sscanf(line, "pendingSkillPts=%d", &tmpVal) == 1)
                g_pendingRewardSkillPts = tmpVal;
            if (sscanf(line, "pendingTraps=%d", &tmpVal) == 1)
                g_pendingTrapSpawn = tmpVal;
            if (sscanf(line, "pendingLoot=%d", &tmpVal) == 1) {
                g_pendingLootDrop = (tmpVal > 5) ? 5 : tmpVal; /* cap at 5 */
            }
            if (sscanf(line, "pendingSlow=%d", &tmpVal) == 1)
                g_pendingTrapSlow = tmpVal;
            if (sscanf(line, "pendingWeaken=%d", &tmpVal) == 1)
                g_pendingTrapWeaken = tmpVal;
            if (sscanf(line, "pendingPoison=%d", &tmpVal) == 1)
                g_pendingTrapPoison = tmpVal;
            if (sscanf(line, "resetPoints=%d", &tmpVal) == 1)
                g_resetPoints = tmpVal;
        }
        /* Load per-character settings (overrides global standalone_settings.dat) */
        {
            int tmpVal;
            if (sscanf(line, "goal=%d", &tmpVal) == 1 && tmpVal >= 0 && tmpVal <= 14)
                g_apGoal = tmpVal;
            /* Backward compat for old state files */
            if (sscanf(line, "goal_scope=%d", &tmpVal) == 1 && tmpVal >= 0 && tmpVal <= 4)
                g_apGoal = tmpVal * 3 + GOAL_DIFF_SCOPE;
            if (sscanf(line, "difficulty_scope=%d", &tmpVal) == 1 && tmpVal >= 0 && tmpVal <= 2)
                g_apGoal = GOAL_ACT_SCOPE * 3 + tmpVal;
            if (sscanf(line, "starting_skills=%d", &tmpVal) == 1 && tmpVal >= 0 && tmpVal <= 20)
                g_apStartingSkills = tmpVal;
            if (sscanf(line, "skill_pool_size=%d", &tmpVal) == 1 && tmpVal >= 1 && tmpVal <= 210)
                g_apSkillPoolSize = tmpVal;
            /* 1.8.0: quest_story is no longer a toggle — always TRUE */
            (void)0;
            if (sscanf(line, "quest_hunting=%d", &tmpVal) == 1)
                g_apQuestHunting = (tmpVal != 0);
            if (sscanf(line, "quest_kill_zones=%d", &tmpVal) == 1)
                g_apQuestKillZones = (tmpVal != 0);
            if (sscanf(line, "quest_exploration=%d", &tmpVal) == 1)
                g_apQuestExploration = (tmpVal != 0);
            if (sscanf(line, "quest_waypoints=%d", &tmpVal) == 1)
                g_apQuestWaypoints = (tmpVal != 0);
            if (sscanf(line, "quest_level_milestones=%d", &tmpVal) == 1)
                g_apQuestLevelMilestones = (tmpVal != 0);
            /* 1.8.0: restore ALL baked-at-creation settings from the per-
             * character state file. These override whatever LoadAPSettings
             * put into the globals from title screen / ap_settings.dat. */
            if (sscanf(line, "skill_hunting=%d", &tmpVal) == 1)
                g_skillHuntingOn = (tmpVal != 0);
            if (sscanf(line, "zone_locking=%d", &tmpVal) == 1)
                g_zoneLockingOn = (tmpVal != 0);
            /* Legacy game_mode fallback: only apply if neither new toggle
             * was present in this state file. We can't easily tell that
             * from a single-line sscanf, so we apply it unconditionally
             * here — but because skill_hunting / zone_locking are read
             * LATER in file order only if they appear AFTER game_mode,
             * new files always have them too and override the legacy. */
            if (sscanf(line, "game_mode=%d", &tmpVal) == 1 && tmpVal >= 0 && tmpVal <= 1) {
                /* Only honour legacy if the new keys haven't appeared yet
                 * on a prior line — simplest: assume if skill_hunting was
                 * written by 1.8.0 SaveStateFile, it's always there. */
                /* no-op: the new keys above take precedence anyway */
            }
            if (sscanf(line, "filler_gold_pct=%d", &tmpVal) == 1)
                g_fillerGoldPct = tmpVal;
            if (sscanf(line, "filler_stat_pts_pct=%d", &tmpVal) == 1)
                g_fillerStatPct = tmpVal;
            if (sscanf(line, "filler_skill_pts_pct=%d", &tmpVal) == 1)
                g_fillerSkillPct = tmpVal;
            if (sscanf(line, "filler_trap_pct=%d", &tmpVal) == 1)
                g_fillerTrapPct = tmpVal;
            if (sscanf(line, "filler_reset_pts_pct=%d", &tmpVal) == 1)
                g_fillerResetPct = tmpVal;
            if (sscanf(line, "filler_loot_pct=%d", &tmpVal) == 1)
                g_fillerLootPct = tmpVal;
            if (sscanf(line, "monster_shuffle=%d", &tmpVal) == 1)
                g_monsterShuffleEnabled = (tmpVal != 0);
            if (sscanf(line, "boss_shuffle=%d", &tmpVal) == 1)
                g_bossShuffleEnabled = (tmpVal != 0);
            if (sscanf(line, "shop_shuffle=%d", &tmpVal) == 1)
                g_shopShuffleEnabled = (tmpVal != 0);
            if (sscanf(line, "entrance_shuffle=%d", &tmpVal) == 1)
                g_entranceShuffleEnabled = (tmpVal != 0);
            if (sscanf(line, "xp_multiplier=%d", &tmpVal) == 1 && tmpVal >= 1 && tmpVal <= 10)
                g_xpMultiplier = tmpVal;
            if (sscanf(line, "class_filter=%d", &tmpVal) == 1)
                g_classFilter = (tmpVal != 0);
            if (sscanf(line, "cls_amazon=%d", &tmpVal) == 1)      g_clsEnabled[0] = (tmpVal != 0);
            if (sscanf(line, "cls_sorceress=%d", &tmpVal) == 1)   g_clsEnabled[1] = (tmpVal != 0);
            if (sscanf(line, "cls_necromancer=%d", &tmpVal) == 1) g_clsEnabled[2] = (tmpVal != 0);
            if (sscanf(line, "cls_paladin=%d", &tmpVal) == 1)     g_clsEnabled[3] = (tmpVal != 0);
            if (sscanf(line, "cls_barbarian=%d", &tmpVal) == 1)   g_clsEnabled[4] = (tmpVal != 0);
            if (sscanf(line, "cls_druid=%d", &tmpVal) == 1)       g_clsEnabled[5] = (tmpVal != 0);
            if (sscanf(line, "cls_assassin=%d", &tmpVal) == 1)    g_clsEnabled[6] = (tmpVal != 0);
            if (sscanf(line, "i_play_assassin=%d", &tmpVal) == 1) g_iPlayAssassin = (tmpVal != 0);
            if (sscanf(line, "death_link=%d", &tmpVal) == 1)      g_apDeathLink = (tmpVal != 0);

            /* 1.8.0 NEW — 15 preload-id fields (gated zone-locking). */
            if (sscanf(line, "act1_preload_normal=%d",    &tmpVal) == 1) g_actPreload[0][0] = tmpVal;
            if (sscanf(line, "act1_preload_nightmare=%d", &tmpVal) == 1) g_actPreload[0][1] = tmpVal;
            if (sscanf(line, "act1_preload_hell=%d",      &tmpVal) == 1) g_actPreload[0][2] = tmpVal;
            if (sscanf(line, "act2_preload_normal=%d",    &tmpVal) == 1) g_actPreload[1][0] = tmpVal;
            if (sscanf(line, "act2_preload_nightmare=%d", &tmpVal) == 1) g_actPreload[1][1] = tmpVal;
            if (sscanf(line, "act2_preload_hell=%d",      &tmpVal) == 1) g_actPreload[1][2] = tmpVal;
            if (sscanf(line, "act3_preload_normal=%d",    &tmpVal) == 1) g_actPreload[2][0] = tmpVal;
            if (sscanf(line, "act3_preload_nightmare=%d", &tmpVal) == 1) g_actPreload[2][1] = tmpVal;
            if (sscanf(line, "act3_preload_hell=%d",      &tmpVal) == 1) g_actPreload[2][2] = tmpVal;
            if (sscanf(line, "act4_preload_normal=%d",    &tmpVal) == 1) g_actPreload[3][0] = tmpVal;
            if (sscanf(line, "act4_preload_nightmare=%d", &tmpVal) == 1) g_actPreload[3][1] = tmpVal;
            if (sscanf(line, "act4_preload_hell=%d",      &tmpVal) == 1) g_actPreload[3][2] = tmpVal;
            if (sscanf(line, "act5_preload_normal=%d",    &tmpVal) == 1) g_actPreload[4][0] = tmpVal;
            if (sscanf(line, "act5_preload_nightmare=%d", &tmpVal) == 1) g_actPreload[4][1] = tmpVal;
            if (sscanf(line, "act5_preload_hell=%d",      &tmpVal) == 1) g_actPreload[4][2] = tmpVal;
        }
        /* Load zone key state */
        {
            int zkIdx;
            if (sscanf(line, "zonekey_%d=1", &zkIdx) == 1) {
                if (zkIdx >= 0 && zkIdx < ZONE_KEY_COUNT) {
                    g_zoneKeyReceived[zkIdx] = TRUE;
                    /* Unlock the areas without notification (silent load) */
                    const ZoneKeyDef* key = &g_zoneKeyDefs[zkIdx];
                    for (int i = 0; i < 10 && key->areas[i] != 0; i++) {
                        int a = key->areas[i];
                        if (a > 0 && a < MAX_AREA_ID)
                            g_zoneLocked[a] = FALSE;
                    }
                }
            }
            int tmpSafe;
            if (sscanf(line, "lastSafeArea=%d", &tmpSafe) == 1)
                g_lastSafeArea = tmpSafe;

            /* 1.8.0 NEW: gate-key receipt state */
            int gkDiff, gkSlot;
            if (sscanf(line, "gatekey_%d_%d=1", &gkDiff, &gkSlot) == 2) {
                if (gkDiff >= 0 && gkDiff < 3 && gkSlot >= 0 && gkSlot < GATEKEY_PER_DIFF) {
                    g_gateKeyReceived[gkDiff][gkSlot] = TRUE;
                }
            }

            /* 1.8.2 NEW: gate-boss kill state */
            int gbDiff, gbSlot;
            if (sscanf(line, "gateboss_killed_%d_%d=1", &gbDiff, &gbSlot) == 2) {
                if (gbDiff >= 0 && gbDiff < 3 && gbSlot >= 0 && gbSlot < GATEKEY_PER_DIFF) {
                    g_gateBossKilled[gbDiff][gbSlot] = TRUE;
                }
            }

            /* 1.9.0 NEW: bonus check counters + fired bitmap */
            if (strncmp(line, "bonus_", 6) == 0) {
                extern void Bonus_LoadLine(const char* line);
                Bonus_LoadLine(line);
            }
        }
    }
    fclose(f);

    /* Gold amounts and reward types are now generated by AssignAllRewards (seed-deterministic) */

    /* Update kill quest counts from loaded area kills */
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++)
            if (g_acts[a].quests[q].type == QTYPE_KILL) {
                int aid = g_acts[a].quests[q].param;
                if (aid >= 0 && aid < MAX_AREA_ID)
                    g_acts[a].quests[q].killCount = g_areaKills[aid];
            }

    Log("LoadChecks: %d/%d quests completed\n", CompletedQuests(), TotalQuests());
}

/* Load state file — restore seed and unlock status */
static BOOL LoadStateFile(void) {
    if (!g_charName[0]) return FALSE;

    char dir[MAX_PATH], path[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_state_%s.dat", dir, g_charName);

    FILE* f = fopen(path, "r");
    if (!f) return FALSE;

    DWORD seed = 0;
    char line[256];
    BOOL inAssignments = FALSE;
    int unlockIdx = 0;

    /* 1.9.0 — pool_kind tells us how to populate the pool when we
     * hit the assignments= marker. "class_only" (SH=OFF) means the
     * char only ever had its class's 30 native skills; calling
     * InitSkillPool on reload would balloon it to 210 wrong skills.
     * Default "hunt" preserves prior behavior for state files
     * missing the marker. */
    char pool_kind[16] = "hunt";
    int  saved_class_id = -1;

    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "seed=%u", &seed) == 1) continue;
        if (sscanf(line, "pool_kind=%15s", pool_kind) == 1) continue;
        if (sscanf(line, "saved_class_id=%d", &saved_class_id) == 1) continue;
        if (strncmp(line, "assignments=", 12) == 0) {
            /* Dispatch to the matching init based on pool_kind. */
            if (strcmp(pool_kind, "class_only") == 0) {
                int classId = saved_class_id;
                if (classId < 0) classId = GetPlayerClass();
                if (classId < 0) classId = g_savedClass;
                InitClassOnlySkills(classId);
                Log("LoadStateFile: pool_kind=class_only -> InitClassOnlySkills(%d)\n", classId);
            } else {
                InitSkillPool(seed);
                Log("LoadStateFile: pool_kind=hunt -> InitSkillPool(%u)\n", seed);
            }
            AssignAllRewards(seed);
            inAssignments = TRUE;
            unlockIdx = 0;
            continue;
        }
        if (inAssignments && g_poolInitialized) {
            /* Parse: name,classCode,unlocked,id */
            char name[64], cls[8];
            int unlocked, id;
            if (sscanf(line, "%[^,],%[^,],%d,%d", name, cls, &unlocked, &id) >= 3) {
                /* Find matching skill in pool by id */
                for (int i = 0; i < g_poolCount; i++) {
                    if (g_skillDB[g_pool[i].dbIndex].id == id) {
                        g_pool[i].unlocked = unlocked ? TRUE : FALSE;
                        break;
                    }
                }
            }
        }
    }
    fclose(f);

    if (g_poolInitialized) {
        Log("LoadStateFile: seed=%u, pool loaded for '%s'\n", seed, g_charName);
        return TRUE;
    }
    return FALSE;
}

/* Auto-save timer */
static DWORD g_lastSave = 0;

static void PeriodicSave(void) {
    DWORD now = GetTickCount();
    if (now - g_lastSave > 10000) { /* Every 10 seconds */
        g_lastSave = now;
        SaveSlots();
        SaveStateFile();
    }
}

/* ================================================================
 * CHARACTER DETECTION & RELOAD
 * ================================================================ */
static char g_lastCharName[32] = {0};
static void* g_lastPlayerPtr = NULL;

/* ================================================================
 * D2S SKILL RESET — Modify .d2s on disk to zero skills and refund
 * to NEWSKILLS pool. Same logic as d2skillreset.exe but runs in-game
 * when player exits to character list.
 * ================================================================ */

/* Bit helpers for D2's bitpacked stats section */
static int d2s_read_bits(const unsigned char *data, int dataLen, int bitOff, int numBits) {
    int result = 0;
    for (int i = 0; i < numBits; i++) {
        int byteIdx = (bitOff + i) / 8;
        int bitIdx  = (bitOff + i) % 8;
        if (byteIdx < dataLen) {
            if (data[byteIdx] & (1 << bitIdx))
                result |= (1 << i);
        }
    }
    return result;
}

static void d2s_write_bits(unsigned char *data, int dataLen, int bitOff, int numBits, int value) {
    for (int i = 0; i < numBits; i++) {
        int byteIdx = (bitOff + i) / 8;
        int bitIdx  = (bitOff + i) % 8;
        if (byteIdx < dataLen) {
            if (value & (1 << i))
                data[byteIdx] |= (1 << bitIdx);
            else
                data[byteIdx] &= ~(1 << bitIdx);
        }
    }
}

/* CSvBits for D2 1.10f stats — complete table from ItemStatCost.txt.
 * Only stats actually saved in .d2s appear here. Stats not listed
 * are skipped (unknown bit width = stop parsing, just skip to skills). */
static int d2s_GetStatBits(int statId) {
    switch (statId) {
        case 0: case 1: case 2: case 3: case 4: return 10; /* str,nrg,dex,vit,statpts */
        case 5: return 8;   /* NEWSKILLS */
        case 6: case 7: case 8: case 9: case 10: case 11: return 21; /* hp,maxhp,mp,maxmp,stam,maxstam */
        case 12: return 7;  /* level */
        case 13: return 32; /* experience */
        case 14: case 15: return 25; /* gold, goldbank */
        default: return 0;  /* unknown — stop parsing */
    }
}

static unsigned int d2s_CalcChecksum(unsigned char *data, int size) {
    unsigned int checksum = 0;
    for (int i = 0; i < size; i++) {
        checksum = (checksum << 1) | (checksum >> 31);
        checksum += data[i];
    }
    return checksum;
}

typedef struct { int id; int value; } D2SStat;
#define D2S_MAX_STATS 32

/* Reset skills in .d2s file on disk. Called when player exits to char list.
 * Zeros all 30 skill bytes, counts total, adds to NEWSKILLS pool.
 * This ensures the next load starts with clean skills and points in pool. */
static void ResetD2SFile(const char* charName) {
    if (!charName || !charName[0]) return;

    /* 1.9.0: kill switch to test if .d2s rewrite is corrupting items
     * (specifically: runeword items losing socketed runes after relog).
     * Set [Debug] DisableD2SReset=1 in d2arch.ini to skip the reset entirely. */
    {
        char ini[MAX_PATH], buf[8];
        GetArchDir(ini, MAX_PATH);
        strcat(ini, "d2arch.ini");
        GetPrivateProfileStringA("Debug", "DisableD2SReset", "0", buf, 7, ini);
        if (atoi(buf) != 0) {
            Log("ResetD2SFile: SKIPPED via [Debug] DisableD2SReset=1\n");
            return;
        }
    }

    /* Build path to Save directory */
    char saveDir[MAX_PATH], d2sPath[MAX_PATH];
    GetSaveDir(saveDir, MAX_PATH);
    if (!saveDir[0]) {
        Log("ResetD2SFile: ERROR — no save path found\n");
        return;
    }
    sprintf(d2sPath, "%s%s.d2s", saveDir, charName);
    Log("ResetD2SFile: opening %s\n", d2sPath);

    /* Create generational backup: <d2sPath>.YYYYMMDD-HHMMSS.bak.
     * Keep the newest 3 so one bad reset can't clobber every known-good save. */
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char bakPath[MAX_PATH];
        snprintf(bakPath, sizeof(bakPath), "%s.%04d%02d%02d-%02d%02d%02d.bak",
                 d2sPath, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        if (!CopyFileA(d2sPath, bakPath, FALSE))
            Log("ResetD2SFile: backup CopyFileA failed err=%lu path='%s'\n", GetLastError(), bakPath);
        PruneOldBackups(d2sPath, 3);
    }

    /* Read entire file */
    FILE *f = fopen(d2sPath, "rb");
    if (!f) { Log("ResetD2SFile: cannot open %s\n", d2sPath); return; }
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fileSize < 100 || fileSize == -1L) { fclose(f); return; }

    unsigned char *data = (unsigned char*)malloc(fileSize + 64);
    if (!data) { fclose(f); return; }
    fread(data, 1, fileSize, f);
    fclose(f);

    /* Verify signature */
    if (data[0] != 0x55 || data[1] != 0xAA || data[2] != 0x55 || data[3] != 0xAA) {
        Log("ResetD2SFile: bad signature\n");
        free(data); return;
    }

    /* 1.9.0: scan items section BEFORE we modify anything, to verify
     * runeword items + sockets are intact. JM = 0x4D 0x4A. */
    {
        int jmStart = -1;
        for (int i = 800; i < fileSize - 4; i++) {
            if (data[i] == 'J' && data[i+1] == 'M' && data[i+2] != 0xAA) {
                jmStart = i; break;
            }
        }
        if (jmStart > 0 && jmStart + 4 <= fileSize) {
            int itemCount = data[jmStart+2] | (data[jmStart+3] << 8);
            Log("ResetD2SFile[PRE]: items section JM at byte %d, count=%d, fileSize=%ld\n",
                jmStart, itemCount, fileSize);
        } else {
            Log("ResetD2SFile[PRE]: no JM items header found in fileSize=%ld\n", fileSize);
        }
    }

    /* Find "gf" (stats) and "if" (skills) headers.
     * IMPORTANT: search from byte 700+ to avoid false matches in header! */
    int gfPos = -1, ifPos = -1;
    if (fileSize > 767 && data[0x2FD] == 0x67 && data[0x2FE] == 0x66) {
        gfPos = 0x2FD;
    } else {
        for (int i = 700; i < fileSize - 2; i++) {
            if (data[i] == 0x67 && data[i+1] == 0x66) { gfPos = i; break; }
        }
    }
    if (gfPos < 0) { Log("ResetD2SFile: no gf section\n"); free(data); return; }

    for (int i = gfPos; i < fileSize - 32; i++) {
        if (data[i] == 0x69 && data[i+1] == 0x66) { ifPos = i; break; }
    }
    if (ifPos < 0) { Log("ResetD2SFile: no if section\n"); free(data); return; }
    /* Count skill points in 30 bytes and save original values for re-investment */
    unsigned char *skills30 = data + ifPos + 2;
    unsigned char origPoints[30];
    memcpy(origPoints, skills30, 30);
    int skillPoints = 0;
    for (int i = 0; i < 30; i++)
        skillPoints += skills30[i];

    /* Parse stats 0-15 (the only ones with CSvBits in ItemStatCost.txt). */
    D2SStat stats[D2S_MAX_STATS];
    int numStats = 0;
    int bitPos = (gfPos + 2) * 8;
    int knownEndBit = bitPos;

    while (numStats < D2S_MAX_STATS) {
        int peekBit = bitPos;
        int statId = d2s_read_bits(data, fileSize, peekBit, 9);
        peekBit += 9;
        if (statId == 0x1FF) {
            knownEndBit = peekBit;
            break;
        }
        int bits = d2s_GetStatBits(statId);
        if (bits == 0) {
            knownEndBit = bitPos;
            Log("ResetD2SFile: unknown stat %d at bit %d, preserving rest as raw\n", statId, bitPos);
            break;
        }
        int value = d2s_read_bits(data, fileSize, peekBit, bits);
        peekBit += bits;
        stats[numStats].id = statId;
        stats[numStats].value = value;
        numStats++;
        bitPos = peekBit;
    }

    /* Find current NEWSKILLS value */
    int poolValue = 0, poolIdx = -1;
    for (int i = 0; i < numStats; i++) {
        if (stats[i].id == 5) { poolValue = stats[i].value; poolIdx = i; break; }
    }

    int newPool = skillPoints + poolValue;
    Log("ResetD2SFile: '%s' skills=%d + pool=%d = %d total\n",
        charName, skillPoints, poolValue, newPool);

    /* ALWAYS run the full flow — no early returns. Even if everything is 0,
     * we still count, zero, and write. This avoids any edge case where
     * the check itself causes rewards to be skipped. */

    /* NO early returns. NO phase 1 scan. Go straight to phase 2.
     * Phase 2 always runs: parse stats, add pending, zero skills, re-encode, write. */
    Log("ResetD2SFile: pending: gold=%d stat=%d skill=%d skillPoints=%d\n",
        g_pendingRewardGold, g_pendingRewardStatPts, g_pendingRewardSkillPts, skillPoints);

    /* Save invested points for re-investment via packet 0x3B after reload */
    g_reinvestCount = 0;
    g_reinvestPending = FALSE;
    {
        char archDir[MAX_PATH], slotsPath[MAX_PATH];
        GetCharFileDir(archDir, MAX_PATH);
        sprintf(slotsPath, "%sd2arch_slots_%s.dat", archDir, charName);
        FILE *sf = fopen(slotsPath, "r");
        if (sf) {
            int slotAssign[3][10];
            memset(slotAssign, -1, sizeof(slotAssign));
            char sline[256];
            while (fgets(sline, sizeof(sline), sf)) {
                int st, ss, sid;
                if (sscanf(sline, "%d,%d,%d", &st, &ss, &sid) == 3) {
                    if (st >= 0 && st < 3 && ss >= 0 && ss < 10)
                        slotAssign[st][ss] = sid;
                }
            }
            fclose(sf);

            /* 1.8.2 FIX — Source of truth is the .d2s skill section.
             *
             * Previously we read levels from per-button cache files
             * (d2arch_fireball_<char>.dat, d2arch_skill<N>_<char>.dat).
             * Those files were:
             *   1) written by the click handler at btnIdx = t*10+s, and
             *   2) read back at a COMPACT pos index that only incremented
             *      on non-empty slots — which silently mapped every level
             *      to the wrong button when there were gaps.
             *
             * The cache files also accumulate POLLUTION from the old bug:
             * every wrong write done before this fix is still on disk,
             * pretending the character invested in skills they never
             * touched. Until those stale files are deleted, ANY read
             * from them produces phantom skills.
             *
             * The .d2s skill bytes (skills30[0..29]) are the single
             * authoritative record of each invested level. fnAddSkill
             * writes there; vanilla D2 reads from there. Position N
             * corresponds to the Nth entry in the class-skill-list
             * which, after WriteClassSkillList runs at character load,
             * is exactly skill at g_tabSlots[N/10][N%10] for the
             * class-only (all-30-filled) layout the user is running. */
            for (int t = 0; t < 3; t++) {
                for (int s = 0; s < 10; s++) {
                    int btnIdx = t * 10 + s;
                    if (slotAssign[t][s] < 0) continue;          /* empty slot */
                    if (g_reinvestCount >= 30) break;             /* full */
                    if (btnIdx >= 30) continue;                   /* defensive */

                    int level = (int)skills30[btnIdx];

                    /* Sanity-clamp: vanilla D2 levels are 0..99. Anything
                     * above 99 is corrupt data — refuse to reinvest. */
                    if (level < 0)  level = 0;
                    if (level > 99) level = 99;

                    if (level > 0) {
                        g_reinvestSkills[g_reinvestCount] = slotAssign[t][s];
                        g_reinvestPoints[g_reinvestCount] = level;
                        g_reinvestBtnIdx[g_reinvestCount] = btnIdx;
                        g_reinvestCount++;
                        Log("ResetD2SFile: will reinvest %d pts in skill %d at btnIdx %d (from .d2s)\n",
                            level, slotAssign[t][s], btnIdx);
                    }
                }
            }
            if (g_reinvestCount > 0) {
                g_reinvestPending = TRUE;
                g_reinvestTime = GetTickCount() + 2000;

                char rdir[MAX_PATH], rpath[MAX_PATH];
                GetCharFileDir(rdir, MAX_PATH);
                sprintf(rpath, "%sd2arch_reinvest_%s.dat", rdir, charName);
                FILE* rf = fopen(rpath, "w");
                if (rf) {
                    /* 1.8.2: 3-column format `skillId,level,btnIdx`. Old
                     * 2-column files are still parseable (btnIdx defaults to
                     * the entry's array index, the legacy behaviour). */
                    for (int ri = 0; ri < g_reinvestCount; ri++)
                        fprintf(rf, "%d,%d,%d\n",
                            g_reinvestSkills[ri], g_reinvestPoints[ri],
                            g_reinvestBtnIdx[ri]);
                    fclose(rf);
                    Log("Saved reinvest file: %d skills\n", g_reinvestCount);
                }
            }
        }
    }

    /* Zero all 30 skill bytes */
    memset(skills30, 0, 30);

    /* Update NEWSKILLS in parsed stats (skill reset refund only).
     * Rewards are now given directly via server-side fnAddStat — no .d2s injection needed. */
    {
        int totalNewSkills = newPool;
        if (totalNewSkills > 0) {
            if (poolIdx >= 0) {
                stats[poolIdx].value = totalNewSkills;
            } else {
                int insertAt = numStats;
                for (int i = 0; i < numStats; i++) {
                    if (stats[i].id > STAT_NEWSKILLS) { insertAt = i; break; }
                }
                for (int i = numStats; i > insertAt; i--)
                    stats[i] = stats[i-1];
                stats[insertAt].id = STAT_NEWSKILLS;
                stats[insertAt].value = totalNewSkills;
                numStats++;
                Log("ResetD2SFile: inserted NEWSKILLS stat (was absent)\n");
            }
        }
    }

    /* Legacy pending rewards cleared — no longer used for .d2s injection.
     * Rewards delivered directly via server-side fnAddStat in game tick. */
    g_pendingRewardGold = 0;
    g_pendingRewardStatPts = 0;
    g_pendingRewardSkillPts = 0;

    /* Re-encode known stats section */
    int knownStartByte = gfPos + 2;
    int knownEndByte = (knownEndBit + 7) / 8;
    int rawTailBytes = ifPos - knownEndByte;
    if (rawTailBytes < 0) rawTailBytes = 0;

    Log("ResetD2SFile: gfPos=%d ifPos=%d knownStartByte=%d knownEndByte=%d rawTailBytes=%d numStats=%d\n",
        gfPos, ifPos, knownStartByte, knownEndByte, rawTailBytes, numStats);

    int newKnownBits = 0;
    for (int i = 0; i < numStats; i++)
        newKnownBits += 9 + d2s_GetStatBits(stats[i].id);
    {
        int lastStatId = d2s_read_bits(data, fileSize, knownEndBit - 9, 9);
        if (lastStatId == 0x1FF) {
            newKnownBits += 9;
        }
    }

    int newKnownBytes = (newKnownBits + 7) / 8;
    int oldKnownBytes = knownEndByte - knownStartByte;
    int byteDiff = newKnownBytes - oldKnownBytes;

    long newFileSize = fileSize + byteDiff;
    Log("ResetD2SFile: oldKnownBytes=%d newKnownBytes=%d byteDiff=%d oldSize=%ld newSize=%ld\n",
        oldKnownBytes, newKnownBytes, byteDiff, fileSize, newFileSize);
    unsigned char *newData = (unsigned char*)calloc(newFileSize + 64, 1);
    if (!newData) { free(data); return; }

    memcpy(newData, data, knownStartByte);

    int wBit = knownStartByte * 8;
    for (int i = 0; i < numStats; i++) {
        d2s_write_bits(newData, newFileSize, wBit, 9, stats[i].id);
        wBit += 9;
        int bits = d2s_GetStatBits(stats[i].id);
        d2s_write_bits(newData, newFileSize, wBit, bits, stats[i].value);
        wBit += bits;
    }
    {
        int lastStatId = d2s_read_bits(data, fileSize, knownEndBit - 9, 9);
        if (lastStatId == 0x1FF) {
            d2s_write_bits(newData, newFileSize, wBit, 9, 0x1FF);
            wBit += 9;
        }
    }

    int newKnownEnd = knownStartByte + newKnownBytes;
    int remainingBytes = fileSize - knownEndByte;
    if (remainingBytes > 0)
        memcpy(newData + newKnownEnd, data + knownEndByte, remainingBytes);

    newData[0x08] = (newFileSize) & 0xFF;
    newData[0x09] = (newFileSize >> 8) & 0xFF;
    newData[0x0A] = (newFileSize >> 16) & 0xFF;
    newData[0x0B] = (newFileSize >> 24) & 0xFF;

    newData[0x0C] = 0; newData[0x0D] = 0; newData[0x0E] = 0; newData[0x0F] = 0;
    unsigned int ck = d2s_CalcChecksum(newData, newFileSize);
    newData[0x0C] = (ck) & 0xFF;
    newData[0x0D] = (ck >> 8) & 0xFF;
    newData[0x0E] = (ck >> 16) & 0xFF;
    newData[0x0F] = (ck >> 24) & 0xFF;

    /* Atomic write: write to <d2sPath>.tmp, flush to disk, then rename over
     * the original. On any failure between open and MoveFileEx, remove the .tmp
     * and leave the original intact. */
    {
        char tmpPath[MAX_PATH];
        snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", d2sPath);
        FILE* fo = fopen(tmpPath, "wb");
        if (!fo) {
            Log("ResetD2SFile: fopen tmp failed err=%d path='%s'\n", errno, tmpPath);
            free(data); free(newData);
            return;
        }
        /* 1.9.0: scan items section in NEW data to verify the byte
         * shift didn't corrupt the JM headers. */
        {
            int jmStart = -1;
            for (int i = 800; i < newFileSize - 4; i++) {
                if (newData[i] == 'J' && newData[i+1] == 'M' && newData[i+2] != 0xAA) {
                    jmStart = i; break;
                }
            }
            if (jmStart > 0 && jmStart + 4 <= newFileSize) {
                int itemCount = newData[jmStart+2] | (newData[jmStart+3] << 8);
                Log("ResetD2SFile[POST]: items section JM at byte %d, count=%d, newFileSize=%ld, byteDiff=%d\n",
                    jmStart, itemCount, newFileSize, byteDiff);
            } else {
                Log("ResetD2SFile[POST]: no JM items header in newFileSize=%ld!\n", newFileSize);
            }
        }

        size_t wrote = fwrite(newData, 1, newFileSize, fo);
        int flushRc = fflush(fo);
        if (wrote != (size_t)newFileSize || flushRc != 0) {
            Log("ResetD2SFile: write/flush failed wrote=%lu want=%ld flush=%d\n",
                (unsigned long)wrote, newFileSize, flushRc);
            fclose(fo);
            remove(tmpPath);
            free(data); free(newData);
            return;
        }
        /* Push through disk cache. _fileno/_commit is the Windows CRT
         * equivalent of fsync on the file descriptor. */
        int fd = _fileno(fo);
        if (fd != -1) {
            if (_commit(fd) != 0)
                Log("ResetD2SFile: _commit failed errno=%d (continuing)\n", errno);
        }
        fclose(fo);
        if (!MoveFileExA(tmpPath, d2sPath,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            Log("ResetD2SFile: rename '%s' -> '%s' failed err=%lu\n",
                tmpPath, d2sPath, GetLastError());
            remove(tmpPath);
            free(data); free(newData);
            return;
        }
        Log("ResetD2SFile: reset %d skill points -> pool (was %d, now %d)\n",
            skillPoints, poolValue, newPool);
    }

    free(data);
    free(newData);
    SaveStateFile();
}

/* ================================================================
 * GrantPendingReinvestAsFreePool
 * If the reinvest file still exists long after a reset (> 30s timeout
 * owned by the gameloop agent), something broke in the reinvest chain.
 * In that case we refund the queued points straight to NEWSKILLS so the
 * player can manually reinvest from the vanilla skill tree. Better than
 * losing the points forever.
 *
 * This function is the d2arch_save.c-owned counterpart to the gameloop
 * agent's 30s timeout — they call us; we own the .d2s mutation.
 * ================================================================ */
static void GrantPendingReinvestAsFreePool(void) {
    if (!g_charName[0]) return;

    char archDir[MAX_PATH], rpath[MAX_PATH];
    GetCharFileDir(archDir, MAX_PATH);
    snprintf(rpath, sizeof(rpath), "%sd2arch_reinvest_%s.dat", archDir, g_charName);

    FILE* rf = fopen(rpath, "r");
    if (!rf) {
        Log("GrantPendingReinvestAsFreePool: no reinvest file at '%s' (nothing to grant)\n", rpath);
        return;
    }
    int totalPoints = 0;
    char rline[64];
    while (fgets(rline, sizeof(rline), rf)) {
        int sid = 0, pts = 0;
        if (sscanf(rline, "%d,%d", &sid, &pts) == 2 && pts > 0)
            totalPoints += pts;
    }
    fclose(rf);

    if (totalPoints <= 0) {
        Log("GrantPendingReinvestAsFreePool: reinvest file had zero total points, deleting\n");
        DeleteFileA(rpath);
        return;
    }

    /* Locate and edit the .d2s directly: read current NEWSKILLS value, add
     * totalPoints to it, re-encode, atomic-write back. Mirrors ResetD2SFile's
     * disk flow but only touches NEWSKILLS. */
    char saveDir[MAX_PATH], d2sPath[MAX_PATH];
    GetSaveDir(saveDir, MAX_PATH);
    if (!saveDir[0]) {
        Log("GrantPendingReinvestAsFreePool: no save dir — aborting\n");
        return;
    }
    snprintf(d2sPath, sizeof(d2sPath), "%s%s.d2s", saveDir, g_charName);

    FILE* f = fopen(d2sPath, "rb");
    if (!f) {
        Log("GrantPendingReinvestAsFreePool: cannot open '%s'\n", d2sPath);
        return;
    }
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fileSize < 100 || fileSize == -1L) { fclose(f); return; }

    unsigned char* data = (unsigned char*)malloc(fileSize + 64);
    if (!data) { fclose(f); return; }
    fread(data, 1, fileSize, f);
    fclose(f);

    if (data[0] != 0x55 || data[1] != 0xAA || data[2] != 0x55 || data[3] != 0xAA) {
        Log("GrantPendingReinvestAsFreePool: bad .d2s signature\n");
        free(data); return;
    }

    /* Locate 'gf' (stats) section — same method as ResetD2SFile. */
    int gfPos = -1, ifPos = -1;
    if (fileSize > 767 && data[0x2FD] == 0x67 && data[0x2FE] == 0x66) {
        gfPos = 0x2FD;
    } else {
        for (int i = 700; i < fileSize - 2; i++)
            if (data[i] == 0x67 && data[i+1] == 0x66) { gfPos = i; break; }
    }
    if (gfPos < 0) { Log("GrantPendingReinvestAsFreePool: no gf section\n"); free(data); return; }
    for (int i = gfPos; i < fileSize - 32; i++)
        if (data[i] == 0x69 && data[i+1] == 0x66) { ifPos = i; break; }
    if (ifPos < 0) { Log("GrantPendingReinvestAsFreePool: no if section\n"); free(data); return; }

    /* Parse stats 0-15. */
    D2SStat stats[D2S_MAX_STATS];
    int numStats = 0;
    int bitPos = (gfPos + 2) * 8;
    int knownEndBit = bitPos;
    while (numStats < D2S_MAX_STATS) {
        int peekBit = bitPos;
        int statId = d2s_read_bits(data, fileSize, peekBit, 9);
        peekBit += 9;
        if (statId == 0x1FF) { knownEndBit = peekBit; break; }
        int bits = d2s_GetStatBits(statId);
        if (bits == 0) { knownEndBit = bitPos; break; }
        int value = d2s_read_bits(data, fileSize, peekBit, bits);
        peekBit += bits;
        stats[numStats].id = statId;
        stats[numStats].value = value;
        numStats++;
        bitPos = peekBit;
    }

    /* Find or insert NEWSKILLS, add totalPoints. */
    int poolIdx = -1;
    int oldPool = 0;
    for (int i = 0; i < numStats; i++) {
        if (stats[i].id == STAT_NEWSKILLS) { poolIdx = i; oldPool = stats[i].value; break; }
    }
    int newPool = oldPool + totalPoints;
    /* NEWSKILLS is 8 bits (0-255). Clamp — anything over 255 is likely bogus anyway. */
    if (newPool > 255) newPool = 255;

    if (poolIdx >= 0) {
        stats[poolIdx].value = newPool;
    } else {
        int insertAt = numStats;
        for (int i = 0; i < numStats; i++)
            if (stats[i].id > STAT_NEWSKILLS) { insertAt = i; break; }
        for (int i = numStats; i > insertAt; i--) stats[i] = stats[i - 1];
        stats[insertAt].id = STAT_NEWSKILLS;
        stats[insertAt].value = newPool;
        numStats++;
    }

    /* Re-encode stats section (same layout math as ResetD2SFile). */
    int knownStartByte = gfPos + 2;
    int knownEndByte = (knownEndBit + 7) / 8;

    int newKnownBits = 0;
    for (int i = 0; i < numStats; i++)
        newKnownBits += 9 + d2s_GetStatBits(stats[i].id);
    {
        int lastStatId = d2s_read_bits(data, fileSize, knownEndBit - 9, 9);
        if (lastStatId == 0x1FF) newKnownBits += 9;
    }
    int newKnownBytes = (newKnownBits + 7) / 8;
    int oldKnownBytes = knownEndByte - knownStartByte;
    int byteDiff = newKnownBytes - oldKnownBytes;

    long newFileSize = fileSize + byteDiff;
    unsigned char* newData = (unsigned char*)calloc(newFileSize + 64, 1);
    if (!newData) { free(data); return; }
    memcpy(newData, data, knownStartByte);

    int wBit = knownStartByte * 8;
    for (int i = 0; i < numStats; i++) {
        d2s_write_bits(newData, newFileSize, wBit, 9, stats[i].id);
        wBit += 9;
        int bits = d2s_GetStatBits(stats[i].id);
        d2s_write_bits(newData, newFileSize, wBit, bits, stats[i].value);
        wBit += bits;
    }
    {
        int lastStatId = d2s_read_bits(data, fileSize, knownEndBit - 9, 9);
        if (lastStatId == 0x1FF) {
            d2s_write_bits(newData, newFileSize, wBit, 9, 0x1FF);
            wBit += 9;
        }
    }
    int newKnownEnd = knownStartByte + newKnownBytes;
    int remainingBytes = fileSize - knownEndByte;
    if (remainingBytes > 0)
        memcpy(newData + newKnownEnd, data + knownEndByte, remainingBytes);

    newData[0x08] = (newFileSize) & 0xFF;
    newData[0x09] = (newFileSize >> 8) & 0xFF;
    newData[0x0A] = (newFileSize >> 16) & 0xFF;
    newData[0x0B] = (newFileSize >> 24) & 0xFF;
    newData[0x0C] = 0; newData[0x0D] = 0; newData[0x0E] = 0; newData[0x0F] = 0;
    unsigned int ck = d2s_CalcChecksum(newData, newFileSize);
    newData[0x0C] = (ck) & 0xFF;
    newData[0x0D] = (ck >> 8) & 0xFF;
    newData[0x0E] = (ck >> 16) & 0xFF;
    newData[0x0F] = (ck >> 24) & 0xFF;

    /* Atomic write via tmp + MoveFileExA. */
    char tmpPath[MAX_PATH];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", d2sPath);
    FILE* fo = fopen(tmpPath, "wb");
    if (!fo) {
        Log("GrantPendingReinvestAsFreePool: fopen tmp failed errno=%d\n", errno);
        free(data); free(newData); return;
    }
    size_t wrote = fwrite(newData, 1, newFileSize, fo);
    int flushRc = fflush(fo);
    if (wrote != (size_t)newFileSize || flushRc != 0) {
        Log("GrantPendingReinvestAsFreePool: write/flush failed\n");
        fclose(fo); remove(tmpPath);
        free(data); free(newData); return;
    }
    int fd = _fileno(fo);
    if (fd != -1) _commit(fd);
    fclose(fo);
    if (!MoveFileExA(tmpPath, d2sPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Log("GrantPendingReinvestAsFreePool: rename failed err=%lu\n", GetLastError());
        remove(tmpPath);
        free(data); free(newData); return;
    }

    /* Consume — remove the reinvest file so we don't double-grant. */
    DeleteFileA(rpath);
    /* Also clear in-memory reinvest state. */
    g_reinvestPending = FALSE;
    g_reinvestCount = 0;

    Log("GrantPendingReinvestAsFreePool: granted %d points to NEWSKILLS (was %d, now %d)\n",
        totalPoints, oldPool, newPool);

    free(data);
    free(newData);
}

/* Known d2arch_<prefix>_<charname>.dat file prefixes.
 * If our save dir resolution ever wobbles (misconfigured registry entry,
 * fresh install with no .d2s yet, etc.) we must NOT sweep files based on
 * the old second-underscore heuristic — that would happily delete every
 * state file while the save dir is empty. The prefix table limits what
 * CleanupOrphanedSaves will touch to files it recognises, and the save
 * dir validation at the top bails out early if the dir looks wrong. */
/* 1.8.0+1.9.0: every per-character file the mod writes. Order matters
 * for CleanupMatchPrefix — longer prefixes MUST come before shorter
 * ones that would also match (e.g. "ap_stash_stk_" before "ap_stash_ser_"
 * before "ap_stash_"). 1.9.0 added the missing applied/stk/collections/
 * stats/spoiler prefixes so cleanup actually removes them too. */
static const char* const s_cleanupPrefixes[] = {
    "d2arch_state_",
    "d2arch_slots_",
    "d2arch_checks_",
    "d2arch_reinvest_",
    "d2arch_fireball_",
    "d2arch_applied_",       /* 1.9.0 — AP filler dedup state */
    "d2arch_spoiler_",       /* 1.9.0 — standalone reward spoiler (.txt!) */
    "d2arch_ap_",            /* per-char AP binding (server/slot/password) */
    "ap_stash_stk_",         /* 1.9.0 — STK tab layout per char */
    "ap_stash_ser_",         /* serialized per-char AP stash (tabs 0..9) */
    "ap_stash_",             /* raw per-char AP stash dump */
    "ap_collections_",       /* 1.9.0 — F1 Collection per-char sidecar */
    "ap_stats_",             /* per-char lifetime stats */
};

/* Does `fname` match a known prefix? Returns pointer to the char-name portion
 * (the substring after the prefix) or NULL. Also matches d2arch_skill<N>_
 * for N in 2..30 (per-button skill-level files). */
static const char* CleanupMatchPrefix(const char* fname) {
    for (size_t i = 0; i < sizeof(s_cleanupPrefixes) / sizeof(s_cleanupPrefixes[0]); i++) {
        size_t plen = strlen(s_cleanupPrefixes[i]);
        if (strncmp(fname, s_cleanupPrefixes[i], plen) == 0)
            return fname + plen;
    }
    /* d2arch_skill<N>_ for N in 2..30 */
    if (strncmp(fname, "d2arch_skill", 12) == 0) {
        const char* p = fname + 12;
        int num = 0;
        while (*p >= '0' && *p <= '9') {
            num = num * 10 + (*p - '0');
            p++;
        }
        if (num >= 2 && num <= 30 && *p == '_')
            return p + 1;
    }
    return NULL;
}

/* 1.9.0 — One-time migration: move legacy per-character files from
 * Game/Archipelago/ into Game/Save/ where the post-1.9.0 code expects
 * them. Runs before CleanupOrphanedSaves so files for still-living
 * characters get moved (instead of being treated as orphans and
 * deleted). Only touches files whose prefix is in s_cleanupPrefixes
 * AND whose corresponding .d2s exists in the save dir; other matched
 * files are left alone for the cleanup pass to handle. */
static void MigrateLegacyPerCharFiles(void) {
    char archDir[MAX_PATH], saveDir[MAX_PATH];
    GetArchDir(archDir, MAX_PATH);
    GetSaveDir(saveDir, MAX_PATH);
    if (!archDir[0] || !saveDir[0]) return;

    /* If GetSaveDir resolved to the same path as GetArchDir (degenerate
     * fallback when no Save folder exists), there's nothing to move. */
    if (_stricmp(archDir, saveDir) == 0) return;

    /* Scan both .dat and .txt — spoiler files are .txt. */
    static const char* exts[] = { "*.dat", "*.txt" };
    int movedTotal = 0;
    for (int e = 0; e < 2; e++) {
        char pattern[MAX_PATH];
        snprintf(pattern, sizeof(pattern), "%s%s", archDir, exts[e]);

        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(pattern, &fd);
        if (hFind == INVALID_HANDLE_VALUE) continue;

        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const char* fname = fd.cFileName;

            const char* nameStart = CleanupMatchPrefix(fname);
            if (!nameStart) continue;  /* unrecognised — leave it alone */

            const char* lastDot = strrchr(fname, '.');
            if (!lastDot || nameStart >= lastDot) continue;

            /* Build src + dest paths */
            char srcPath[MAX_PATH], dstPath[MAX_PATH];
            snprintf(srcPath, sizeof(srcPath), "%s%s", archDir, fname);
            snprintf(dstPath, sizeof(dstPath), "%s%s", saveDir, fname);

            /* Don't clobber a newer file that's already been moved. */
            if (GetFileAttributesA(dstPath) != INVALID_FILE_ATTRIBUTES) {
                /* Dest exists — delete the legacy copy so we don't keep
                 * sweeping it on every char load. */
                DeleteFileA(srcPath);
                continue;
            }

            if (MoveFileExA(srcPath, dstPath,
                            MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
                movedTotal++;
                Log("MIGRATE: moved '%s' -> Save/\n", fname);
            } else {
                Log("MIGRATE: MoveFileExA failed err=%lu for '%s' "
                    "(src='%s' dst='%s')\n",
                    GetLastError(), fname, srcPath, dstPath);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    if (movedTotal > 0) {
        Log("MIGRATE: moved %d legacy per-char file(s) from Archipelago/ to Save/\n",
            movedTotal);
    }
}

/* Clean up orphaned save files for deleted characters.
 * Scans BOTH Game/Save/ (the new home for per-char files) AND
 * Game/Archipelago/ (legacy location, in case migration left stragglers).
 * Aborts when the save dir is invalid — protects every character's state
 * files from being wiped by a stale or misconfigured HKCU "Save Path"
 * entry that temporarily redirects saves. */
static void CleanupOrphanedSaves(void) {
    char archDir[MAX_PATH], saveDir[MAX_PATH];
    GetArchDir(archDir, MAX_PATH);
    GetSaveDir(saveDir, MAX_PATH);

    /* Guard: save dir must resolve + exist. Protects against wiping
     * state when the registry "Save Path" is corrupt or points to a
     * non-existent folder. */
    if (!saveDir[0] || GetFileAttributesA(saveDir) == INVALID_FILE_ATTRIBUTES) {
        Log("CLEANUP: aborted — save dir invalid or missing: '%s'\n", saveDir);
        return;
    }

    /* Run the cleanup pass over BOTH dirs. The .txt extension is needed
     * for spoiler files (1.9.0). Same dir twice is harmless; the second
     * pass just sees an empty directory. */
    const char* scanDirs[2] = { saveDir, archDir };
    static const char* exts[] = { "*.dat", "*.txt" };

    int totalDeleted = 0, totalInspected = 0, totalKept = 0;

    for (int d = 0; d < 2; d++) {
        if (!scanDirs[d][0]) continue;
        /* Skip duplicate scan when GetSaveDir falls back to GetArchDir. */
        if (d == 1 && _stricmp(scanDirs[0], scanDirs[1]) == 0) continue;

        for (int e = 0; e < 2; e++) {
            char pattern[MAX_PATH];
            snprintf(pattern, sizeof(pattern), "%s%s", scanDirs[d], exts[e]);

            WIN32_FIND_DATAA fd;
            HANDLE hFind = FindFirstFileA(pattern, &fd);
            if (hFind == INVALID_HANDLE_VALUE) continue;

            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const char* fname = fd.cFileName;
                totalInspected++;

                const char* nameStart = CleanupMatchPrefix(fname);
                if (!nameStart) continue;  /* unrecognised prefix — leave alone */

                const char* lastDot = strrchr(fname, '.');
                if (!lastDot || nameStart >= lastDot) continue;

                char charName[32] = {0};
                int len = (int)(lastDot - nameStart);
                if (len <= 0 || len >= (int)sizeof(charName)) continue;
                memcpy(charName, nameStart, len);
                charName[len] = 0;

                char d2sPath[MAX_PATH];
                snprintf(d2sPath, sizeof(d2sPath), "%s%s.d2s", saveDir, charName);
                if (GetFileAttributesA(d2sPath) == INVALID_FILE_ATTRIBUTES) {
                    char fullPath[MAX_PATH];
                    snprintf(fullPath, sizeof(fullPath), "%s%s", scanDirs[d], fname);
                    if (DeleteFileA(fullPath)) {
                        totalDeleted++;
                        Log("CLEANUP: deleted orphaned '%s' (no %s.d2s) from %s\n",
                            fname, charName,
                            d == 0 ? "Save/" : "Archipelago/");
                    } else {
                        Log("CLEANUP: DeleteFileA failed err=%lu path='%s'\n",
                            GetLastError(), fullPath);
                    }
                } else {
                    totalKept++;
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    }

    Log("CLEANUP: scan done — inspected=%d deleted=%d kept=%d (saveDir='%s' archDir='%s')\n",
        totalInspected, totalDeleted, totalKept, saveDir, archDir);
}

static void OnCharacterLoad(void) {
    /* Sanity banner: log resolved archDir/saveDir on every character load so
     * we can diagnose save-path issues from user logs without guesswork. */
    {
        char _archDir[MAX_PATH], _saveDir[MAX_PATH];
        GetArchDir(_archDir, sizeof(_archDir));
        GetSaveDir(_saveDir, sizeof(_saveDir));
        Log("OnCharacterLoad: char='%s' archDir='%s' saveDir='%s'\n",
            g_charName, _archDir, _saveDir);
    }

    /* Undo any monster/boss shuffle from the PREVIOUS character before we
     * switch to this character's shuffle config. MonStatsTxt is process-global
     * and previously-applied rows would stack with the new char's shuffle. */
    UndoMonsterShuffle();
    UndoBossShuffle();
    /* 1.9.0: same isolation requirement for entrance shuffle. */
    UndoEntranceShuffle();

    /* 1.9.1 — re-apply per-class skill animations. The boot patch puts every
     * class-specific animation (S1-S4 / SQ / TH / KK) on a safe-for-anyone A1
     * fallback so the global skill table is cross-class playable. Here we
     * walk the cached vanilla anims and restore the originals for skills
     * whose native class is THIS character — Smite gets its shield bash,
     * Whirlwind spins, Rabies bites in werewolf form, javelins throw,
     * Dragon Talon kicks. Non-native skills keep the A1 fallback. */
    Skilltree_OnCharacterLoadHook();

    /* 1.9.0: Move any legacy per-char files from Archipelago/ into Save/
     * BEFORE CleanupOrphanedSaves runs, so files belonging to existing
     * characters get relocated rather than deleted as orphans. */
    MigrateLegacyPerCharFiles();

    /* Clean up save files for deleted characters */
    CleanupOrphanedSaves();

    /* === Clear ALL global state for new character load === */
    g_seed = 0;
    g_poolInitialized = FALSE;
    g_poolCount = 0;
    g_reinvestPending = FALSE;
    g_reinvestCount = 0;
    g_shuffleApplied = FALSE;
    g_bossShuffleApplied = FALSE;
    g_pendingTrapSpawn = 0;
    g_pendingLootDrop = 0;
    g_pendingTrapSlow = 0;
    g_pendingTrapWeaken = 0;
    g_pendingTrapPoison = 0;
    g_apGoalComplete = FALSE;

    /* Clear reinvest-levels scratch so an old character's levels don't
     * leak into the new character's panel. */
    memset(g_reinvestLevels, 0, sizeof(g_reinvestLevels));
    g_reinvestLevelsReady = FALSE;
    g_reinvestDone = FALSE;

    memset(g_questCompleted, 0, sizeof(g_questCompleted));
    memset(g_questKillCount, 0, sizeof(g_questKillCount));
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++) {
            g_acts[a].quests[q].completed = FALSE;
            g_acts[a].quests[q].killCount = 0;
        }
    memset(g_areaKills, 0, sizeof(g_areaKills));
    /* 1.8.2 — clear gate-key receipt + gate-boss kill state on char switch.
     * LoadChecks only sets these TRUE; without an explicit clear, char A's
     * progress would leak into char B. */
    memset(g_gateKeyReceived, 0, sizeof(g_gateKeyReceived));
    memset(g_gateBossKilled,  0, sizeof(g_gateBossKilled));

    /* 1.8.2 — Defensive: reset ALL randomization-settings globals to safe
     * neutral defaults BEFORE any source (LoadAPSettings or LoadChecks) gets
     * to populate them. This guarantees that if a per-character state file
     * is missing fields (older format, partial write, manual edit), the
     * unread globals fall back to defaults instead of inheriting the
     * previously-loaded character's values.
     *
     * Defaults match d2arch.ini fresh-install + apworld defaults so a
     * brand-new install gets sensible behavior even before any source runs. */
    g_skillHuntingOn        = TRUE;
    g_zoneLockingOn         = FALSE;
    g_apGoal                = 0;        /* Full Normal */
    g_apStartingSkills      = 6;
    g_apSkillPoolSize       = 210;
    g_apQuestStory          = TRUE;     /* always ON — engine-required */
    g_apQuestHunting        = TRUE;
    g_apQuestKillZones      = TRUE;
    g_apQuestExploration    = TRUE;
    g_apQuestWaypoints      = TRUE;
    g_apQuestLevelMilestones= TRUE;
    /* 1.9.0: rebalanced filler weights per user feedback.
     * Gold reduced 30->15 (was over-represented). Reset reduced
     * 25->5 (and gated to SH=ON only — reset points are only
     * meaningful when there's a skill pool to swap from; AssignAllRewards
     * zeroes this out when g_skillHuntingOn=FALSE). */
    g_fillerGoldPct         = 15;
    g_fillerStatPct         = 15;
    g_fillerSkillPct        = 15;
    g_fillerTrapPct         = 15;
    g_fillerResetPct        = 5;
    g_fillerLootPct         = 18;
    g_monsterShuffleEnabled = FALSE;
    g_bossShuffleEnabled    = FALSE;
    g_shopShuffleEnabled    = FALSE;
    g_entranceShuffleEnabled = FALSE;
    g_xpMultiplier          = 1;
    g_classFilter           = FALSE;
    for (int c = 0; c < 7; c++) g_clsEnabled[c] = TRUE;
    g_iPlayAssassin         = FALSE;
    g_apDeathLink           = FALSE;
    memset(g_actPreload, 0, sizeof(g_actPreload));
    /* 1.8.0 cleanup: Treasure Cow state reset extracted (char-load) */
    g_deadCount = 0;
    g_pendingRewardGold = 0;
    g_pendingRewardStatPts = 0;
    g_pendingRewardSkillPts = 0;

    /* 1.9.0 — clear bonus check state so the new character starts fresh.
     * LoadStateFile / LoadChecks will repopulate from the per-char file
     * if it exists. */
    {
        extern void Bonus_ResetState(void);
        Bonus_ResetState();
    }

    Log("Global state cleared for new character load\n");

    /* Reset cached data */
    g_sgptDT = 0;
    g_origCacheInit = FALSE;
    g_applyCount = 0;
    g_lastApply = 0;

    /* 1.8.2 — Wipe stale per-button level-cache files for this character.
     * These files (d2arch_fireball_<char>.dat + d2arch_skill<N>_<char>.dat)
     * are pure display cache; the authoritative skill data lives in the
     * .d2s skill section. Wiping them at load forces the panel to rebuild
     * from the reinvest pipeline (which now reads .d2s) instead of trusting
     * cache that may have been polluted by the pre-fix reinvest-consumer
     * bug (compact ri index writing to the wrong file). */
    if (g_charName[0]) {
        char _bDir[MAX_PATH], _bPath[MAX_PATH];
        GetCharFileDir(_bDir, MAX_PATH);
        sprintf(_bPath, "%sd2arch_fireball_%s.dat", _bDir, g_charName);
        DeleteFileA(_bPath);
        for (int _bi = 1; _bi < 30; _bi++) {
            sprintf(_bPath, "%sd2arch_skill%d_%s.dat", _bDir, _bi + 1, g_charName);
            DeleteFileA(_bPath);
        }
        Log("ResetD2SFile: wiped stale per-button cache files for '%s' (reinvest will rewrite from .d2s)\n",
            g_charName);
    }

    /* 1.8.2 — Settings sourcing rule (strict per-character):
     *
     *   EXISTING character (state file exists):
     *     → Read ONLY from d2arch_state_<char>.dat (LoadStateFile / LoadChecks).
     *     Do NOT read ap_settings.dat or d2arch.ini, even if AP just connected
     *     to a different server. The character keeps the settings it was
     *     created with for its entire lifetime.
     *
     *   NEW character (no state file):
     *     → Capture from the current source NOW:
     *         - AP connected → ap_settings.dat (slot_data from server)
     *         - Standalone   → d2arch.ini [settings] (title-screen UI)
     *     Then bake those values into a fresh state file and freeze.
     *
     * The only AP control allowed for an existing AP-character is the F1
     * menu's disconnect/reconnect — it changes the SERVER ADDRESS but never
     * the randomization settings (which stay baked).
     */
    BOOL hasExistingChar = FALSE;
    if (g_charName[0]) {
        char statePath[MAX_PATH], _archDir2[MAX_PATH];
        GetCharFileDir(_archDir2, MAX_PATH);
        sprintf(statePath, "%sd2arch_state_%s.dat", _archDir2, g_charName);
        hasExistingChar = (GetFileAttributesA(statePath) != INVALID_FILE_ATTRIBUTES);
    }

    if (!hasExistingChar) {
        /* New character — capture from current source (AP or UI). */
        Log("OnCharacterLoad: NEW character — sourcing settings from %s\n",
            g_apConnected ? "AP slot_data (ap_settings.dat)" : "UI (d2arch.ini)");
        LoadAPSettings();

        /* 1.8.2 — Standalone preload randomization.
         *
         * For AP characters, ap_settings.dat carries 15 preload IDs that the
         * apworld's generate_early() picked per (act, diff). Standalone has
         * no equivalent source — without this fallback, g_actPreload stays
         * zeroed and every act × difficulty uses Preload 0 (Corpsefire /
         * Radament / Web Mage / Winged Death / Shenk). User reported the
         * F4 tracker showing identical bosses across Normal/NM/Hell — that
         * was this missing randomization step.
         *
         * Mirror the apworld's logic: random preload ID per (act, diff)
         * within each act's preload count (4 for Acts 1/2/3/5, 3 for Act 4).
         * Seed combines tick count with the character-name hash so creating
         * the same name twice produces different layouts (replay value),
         * while a single character's layout is fixed once baked. */
        if (!g_apConnected && g_zoneLockingOn) {
            unsigned seed = GetTickCount();
            for (int i = 0; g_charName[i]; i++) {
                seed = seed * 1664525u + (unsigned)(unsigned char)g_charName[i] + 1013904223u;
            }
            srand(seed);
            static const int max_preloads[5] = { 4, 4, 4, 3, 4 };
            for (int act = 0; act < 5; act++) {
                for (int diff = 0; diff < 3; diff++) {
                    g_actPreload[act][diff] = rand() % max_preloads[act];
                }
            }
            Log("Standalone preload randomization (seed=%u): "
                "A1=[%d,%d,%d] A2=[%d,%d,%d] A3=[%d,%d,%d] A4=[%d,%d,%d] A5=[%d,%d,%d]\n",
                seed,
                g_actPreload[0][0], g_actPreload[0][1], g_actPreload[0][2],
                g_actPreload[1][0], g_actPreload[1][1], g_actPreload[1][2],
                g_actPreload[2][0], g_actPreload[2][1], g_actPreload[2][2],
                g_actPreload[3][0], g_actPreload[3][1], g_actPreload[3][2],
                g_actPreload[4][0], g_actPreload[4][1], g_actPreload[4][2]);
        }
    } else {
        /* Existing character — per-char file is the only source.
         * LoadChecks (called below from LoadStateFile path) reads all 31
         * settings from disk. Skip LoadAPSettings entirely so external
         * sources can't perturb the in-memory globals before LoadChecks
         * gets to run. */
        Log("OnCharacterLoad: EXISTING character — settings will load from per-char file only\n");
    }

    /* Initialize zone locks BEFORE loading state (state will unlock received keys) */
    InitZoneLocks();

    /* Try to load existing state */
    if (LoadStateFile()) {
        LoadSlots();
        LoadChecks(); /* This OVERRIDES global settings with per-character saved settings */
        /* Re-initialize zone locks with per-character settings (goal_scope may differ).
         * Save loaded zone keys first since InitZoneLocks clears them. */
        {
            BOOL savedKeys[ZONE_KEY_COUNT];
            memcpy(savedKeys, g_zoneKeyReceived, sizeof(savedKeys));
            int savedSafe = g_lastSafeArea;
            InitZoneLocks();
            memcpy(g_zoneKeyReceived, savedKeys, sizeof(g_zoneKeyReceived));
            g_lastSafeArea = savedSafe;
            for (int k = 0; k < ZONE_KEY_COUNT; k++) {
                if (g_zoneKeyReceived[k]) {
                    const ZoneKeyDef* key = &g_zoneKeyDefs[k];
                    for (int i = 0; i < 10 && key->areas[i] != 0; i++) {
                        int a = key->areas[i];
                        if (a > 0 && a < MAX_AREA_ID)
                            g_zoneLocked[a] = FALSE;
                    }
                }
            }
        }
        /* NOTE: AssignAllRewards is NOT called here because LoadStateFile() already
         * calls it internally (line that parses "assignments=" triggers InitSkillPool
         * + AssignAllRewards). Calling it again would double-apply rewards. */

        /* 1.8.2 — Reconcile gate-key state from act-boss quest progress.
         *
         * If a player completed Act N's boss quest, they MUST have killed all
         * gate bosses for Acts 1..N (the boss is gated behind gate 4's region).
         * Pre-1.8.2 versions sometimes failed to persist gate-key receipts
         * properly across reloads, leaving players unable to enter Act 3+
         * areas after upgrading mid-game even though they're already there.
         *
         * For each completed act-boss quest, mark every gate in that act and
         * all earlier acts as both KILLED and KEY-RECEIVED. UnlockGateKey is
         * idempotent so this is safe to call repeatedly.
         *
         * Quest IDs (from d2arch_quests.c):
         *   6   = Sisters to the Slaughter (Andariel — Act 1)
         *   106 = Seven Tombs              (Duriel    — Act 2)
         *   206 = The Guardian             (Mephisto  — Act 3)
         *   303 = Terror's End             (Diablo    — Act 4)
         *   406 = Eve of Destruction       (Baal      — Act 5)
         */
        if (g_zoneLockingOn) {
            int rDiff = g_currentDifficulty;
            if (rDiff >= 0 && rDiff <= 2) {
                static const int actBossQid[6] = { 0, 6, 106, 206, 303, 406 };
                int implied = 0;
                for (int actDone = 1; actDone <= 5; actDone++) {
                    int qid = actBossQid[actDone];
                    if (qid <= 0 || qid >= MAX_QUEST_ID) continue;
                    if (!g_questCompleted[rDiff][qid]) continue;

                    /* Act <actDone> done → all 4 gates of acts 1..actDone implied */
                    for (int act = 1; act <= actDone; act++) {
                        int numGates = g_actRegions[act - 1].num_gates;
                        for (int gi = 0; gi < numGates; gi++) {
                            int slot = GateKey_SlotFromActGate(act, gi);
                            if (slot < 0) continue;
                            if (!g_gateBossKilled[rDiff][slot]) {
                                g_gateBossKilled[rDiff][slot] = TRUE;
                                implied++;
                            }
                            if (!g_gateKeyReceived[rDiff][slot]) {
                                /* Use UnlockGateKey so g_zoneLocked[] is
                                 * actually opened, plus the user-facing
                                 * notify text fires. */
                                UnlockGateKey(rDiff, slot);
                                implied++;
                            }
                        }
                    }
                }
                if (implied > 0) {
                    Log("RECONCILE: implied %d gate-state changes from act-boss quest progress (diff=%d) — saving\n",
                        implied, rDiff);
                    SaveStateFile();
                }
            }
        }
    } else {
        /* New character — initialize skill pool.
         * 1.8.0: if Skill Hunting is OFF, skip the randomized pool entirely
         * and populate only this class's 30 native skills, pre-assigned to
         * their tier slots. The editor's pool side will be empty (nothing
         * to drag) and the tree side shows the class's standard skills.
         *
         * 1.9.0: AssignAllRewards now runs in BOTH paths so the filler
         * pre-roll engine (gold/xp/trap/boss/drop selection) and the
         * standalone spoiler file are populated regardless of skill mode.
         * Without this, SH=OFF chars get neutral defaults at delivery
         * time (every quest -> 500g) and no spoiler.dat. We use a stable
         * seed derived from the char name + tick so the pre-rolls match
         * what gets persisted in the state file at next load. */
        DWORD newSeed = GetTickCount();
        for (int i = 0; g_charName[i]; i++) {
            newSeed = newSeed * 1664525u + (unsigned)(unsigned char)g_charName[i] + 1013904223u;
        }
        g_seed = newSeed;
        if (!g_skillHuntingOn) {
            int classId = GetPlayerClass();
            if (classId < 0) classId = g_savedClass;
            InitClassOnlySkills(classId);
            AssignAllRewards(newSeed);  /* fillers + spoiler */
            SaveSlots();
            SaveStateFile();
        } else {
            InitSkillPool(newSeed);
            AssignAllRewards(newSeed);
            SaveStateFile();
        }
    }

    /* Load AP config but DON'T auto-connect. The user connects via the AP menu.
     * Auto-connecting caused issues when switching characters rapidly. */
    g_apGoalComplete = FALSE;
    LoadAPCharConfig();

    /* ResetD2SFile is the refund-to-pool flow that makes the skill editor
     * work: it reads the live .d2s skill levels, adds them back to NEWSKILLS,
     * and writes a reinvest file so the game tick can re-apply them to
     * whatever slots the user currently has assigned. Running it on every
     * OnCharacterLoad is the documented 1.7.0 behaviour and must stay that
     * way — gating it behind a flag broke the reset system entirely.
     *
     * Safety-net additions (1.7.1, unchanged): the reinvest consumer in
     * d2arch_gameloop.c now has a 30-second timeout that hands pending
     * points to GrantPendingReinvestAsFreePool() if the three-stage apply
     * chain (pGame, server player, any input) ever fails — so a stuck
     * reinvest returns points to the free pool instead of losing them. */
    Log("OnCharacterLoad: running ResetD2SFile (refund + queue reinvest)\n");
    ResetD2SFile(g_charName);
    SaveStateFile(); /* re-save after rewards consumed */
    /* g_resetRequested flag preserved for future use if needed, but no
     * longer gates the normal refund flow. */
    g_resetRequested = 0;

    /* 1.7.1: recover from a crashed reinvest consume. If a `.consuming`
     * file exists on disk, the previous session died mid-apply. Rename it
     * back to `.dat` so the loader below picks it up normally. If the
     * rename fails (permission, file gone), GrantPendingReinvestAsFreePool
     * will salvage whatever points were in the backup. */
    {
        char cdir[MAX_PATH], cpath[MAX_PATH], rpath_final[MAX_PATH];
        GetCharFileDir(cdir, MAX_PATH);
        sprintf(cpath, "%sd2arch_reinvest_%s.dat.consuming", cdir, g_charName);
        sprintf(rpath_final, "%sd2arch_reinvest_%s.dat", cdir, g_charName);
        if (GetFileAttributesA(cpath) != INVALID_FILE_ATTRIBUTES) {
            Log("REINVEST RECOVERY: found .consuming file from prior crashed "
                "session, restoring to .dat\n");
            /* If a fresh .dat already exists (shouldn't normally), keep the
             * .consuming copy as priority — it represents in-flight work. */
            if (GetFileAttributesA(rpath_final) != INVALID_FILE_ATTRIBUTES) {
                DeleteFileA(rpath_final);
            }
            if (!MoveFileExA(cpath, rpath_final,
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                Log("REINVEST RECOVERY: rename failed err=%lu — "
                    "falling back to GrantPendingReinvestAsFreePool\n",
                    GetLastError());
                /* Best-effort: read the .consuming content directly and
                 * credit points to NEWSKILLS via existing helper. */
                GrantPendingReinvestAsFreePool();
                DeleteFileA(cpath);
            }
        }
    }

    /* Load reinvest data from file if it exists (survives full game restart).
     * We still process a pre-existing reinvest file even when the user didn't
     * request a reset this session — it represents points taken off skills
     * in a PREVIOUS session that never got re-applied. That file alone drives
     * the restore flow; we must not call ResetD2SFile again for it. */
    if (!g_reinvestPending) {
        char rdir[MAX_PATH], rpath[MAX_PATH];
        GetCharFileDir(rdir, MAX_PATH);
        sprintf(rpath, "%sd2arch_reinvest_%s.dat", rdir, g_charName);
        FILE* rf = fopen(rpath, "r");
        if (rf) {
            g_reinvestCount = 0;
            char rline[64];
            while (fgets(rline, sizeof(rline), rf) && g_reinvestCount < 30) {
                int sid = 0, pts = 0, btn = -1;
                /* 1.8.2: prefer the new 3-column format (skill,level,btnIdx).
                 * Fall back to the legacy 2-column file by deriving btnIdx
                 * from the entry's array position (which matches the old
                 * pre-fix behaviour for already-existing reinvest files). */
                int matched = sscanf(rline, "%d,%d,%d", &sid, &pts, &btn);
                if (matched < 3) btn = g_reinvestCount;     /* legacy fallback */
                if (matched >= 2 && sid > 0 && pts > 0) {
                    g_reinvestSkills[g_reinvestCount] = sid;
                    g_reinvestPoints[g_reinvestCount] = pts;
                    g_reinvestBtnIdx[g_reinvestCount] = btn;
                    g_reinvestCount++;
                }
            }
            fclose(rf);
            if (g_reinvestCount > 0) {
                g_reinvestPending = TRUE;
                g_reinvestTime = GetTickCount() + 2000;
                Log("Loaded reinvest from file: %d skills (resume from prior session)\n", g_reinvestCount);
            }
        }
    }

    LoadIconMap();
    LoadSkillExtraInfo();

    /* Skill positions are handled by skilldesc.txt (all skills on page 1).
     * No runtime patching needed - vanilla reads the modified data files. */

    /* Apply monster/boss shuffle if enabled */
    if (g_monsterShuffleEnabled && !g_shuffleApplied) {
        ApplyMonsterShuffle(g_seed);
    }
    if (g_bossShuffleEnabled && !g_bossShuffleApplied) {
        ApplyBossShuffle(g_seed);
    }
    /* 1.9.0: System 1 — dead-end cave entrance shuffle */
    if (g_entranceShuffleEnabled && !g_entranceShuffleApplied) {
        ApplyEntranceShuffle(g_seed);
    }

    /* Per-character AP-stash sidecar + initial tab seeding. Declared
     * here as forward references because d2arch_save.c is compiled
     * before d2arch_stash.c in the unity build (see d2arch.c). */
    {
        extern BOOL StashLoadAP(const char* charName);
        extern int  g_activeStashTab;
        /* New serialized-byte shadow tables. Reset in-memory state
         * (no carry-over from previous char), then load from disk. */
        extern void StashSerResetMemory(void);
        extern void StashSerLoadAll(const char* charName);
        /* 1.9.0 — STK (stackable) tabs. Per-character STK_AP and
         * account-wide STK_SH each load from their own sidecar. */
        extern BOOL StkLoadAP(const char* charName);
        extern BOOL StkLoadShared(void);
        extern void Coll_LoadForCharacter(const char* charName);
        extern void Coll_ScanPlayerHoldings(void* pPlayerUnit);
        extern void Stats_LoadForCharacter(const char* charName);
        extern void Stats_LoadLifetime(void);
        extern void Stats_OnCharacterChange(void);
        StashSerResetMemory();
        if (g_charName[0]) {
            StashLoadAP(g_charName);      /* legacy metadata sidecar */
            StashSerLoadAll(g_charName);  /* NEW: per-tab serialized items */
            StkLoadAP(g_charName);        /* 1.9.0 — per-char STK tabs */
            Coll_LoadForCharacter(g_charName); /* 1.9.0 — collection flags */
            Stats_LoadForCharacter(g_charName);/* 1.9.0 — F1 logbook stats */
            Stats_OnCharacterChange();         /* reset playtime anchor */
            /* Initial scan deferred — pPlayerUnit may not be ready yet.
             * The first scan fires from the gameloop tick once a player
             * unit pointer is available. See Coll_TickIfNeeded(). */
        }
        StkLoadShared();                  /* 1.9.0 — account-wide STK tabs */
        Stats_LoadLifetime();             /* 1.9.0 — account-wide stats */
        /* AP chars land on AP1 (global 0), non-AP land on SH1 (global 10). */
        g_activeStashTab = g_apMode ? 0 : 10;
    }

    /* 1.8.0 NEW: reset Custom Boss spawn state so registered bosses can
     * respawn in the newly-loaded session. */
    CustomBoss_Reset();
    /* 1.8.0 NEW: append gate-bosses from active preload set for the current
     * difficulty. This adds up to 18 gate-bosses to g_customBosses[] based
     * on g_actPreload[act-1][diff] baked into the character state. */
    CustomBoss_AppendGateBosses();
    /* 1.8.0 NEW: reset Treasure Cow per-act counters + area tracking. */
    TreasureCow_Reset();

    /* 1.8.5 — AP soft-lock reconcile (existing characters). If this char
     * was created in an earlier session OR was created with broken
     * settings, the per-character state file may say zone_locking=1 while
     * the multiworld it's attached to has no gate keys (slot_data says
     * zone_locking=0). That's a permanent soft-lock — locked zones with
     * no key items in the item pool. Detect and force-fix here.
     *
     * Only reconciles the soft-lock-causing settings. Other settings
     * (XP multiplier, class filters, quest toggles, etc.) keep their
     * frozen-at-creation values per the strict per-character settings
     * model — they don't soft-lock the run, they just feel different
     * from what the YAML says. */
    if (g_charName[0] && g_apConnected) {
        char rPath[MAX_PATH];
        GetArchDir(rPath, MAX_PATH);
        strcat(rPath, "ap_settings.dat");
        FILE* rf = fopen(rPath, "r");
        if (rf) {
            char rline[256];
            int  slotZL = -1;
            while (fgets(rline, sizeof(rline), rf)) {
                int v;
                if (sscanf(rline, "zone_locking=%d", &v) == 1) {
                    slotZL = (v != 0) ? 1 : 0;
                    break;
                }
            }
            fclose(rf);
            if (slotZL == 0 && g_zoneLockingOn) {
                Log("AP-MISMATCH RECONCILE (OnCharacterLoad): per-char "
                    "zone_locking=ON but slot_data=OFF — auto-correcting "
                    "to OFF so the run can complete\n");
                g_zoneLockingOn = FALSE;
                /* Re-init lock map with corrected setting before freeze */
                InitZoneLocks();
                /* Persist the fix to per-char state file immediately so it
                 * survives a crash. g_settingsFrozen is still FALSE here. */
                SaveStateFile();
                ShowNotify("Zone Locking auto-disabled (matches AP server)");
            }
        }
    }

    /* 1.8.0: freeze all per-character settings from this point on. Any
     * later LoadAPSettings call (AP reconnect / title-screen toggle) will
     * now be a no-op until the player exits back to the title screen.
     *
     * 1.9.0 — race-window freeze deferral removed. Previously g_apMode was
     * set TRUE on Connect-click, which forced this code to defer the freeze
     * if auth hadn't yet completed by character-load time. That created two
     * problems: (a) a click that never authenticated left the character in
     * a permanently unfrozen state, (b) the AP-side decisions (stash tab
     * default, skill pool init) had already happened with g_apMode=TRUE and
     * couldn't be unwound. Now g_apMode is only flipped TRUE by
     * PollAPStatus on actual auth, so a character that loads while
     * g_apMode=FALSE is genuinely standalone and is safe to freeze
     * immediately. If auth then arrives mid-session, LoadAPSettings sees
     * g_settingsFrozen=TRUE and is a no-op — the standalone character
     * stays standalone (matching the "AP only takes over if there is a
     * connection" rule). */
    /* 1.9.0 — Backfill bonus check counters from historical AP-server
     * data. For an existing character that already has all its checks
     * registered server-side, the bridge's per-char dedup keeps those
     * items from re-firing through PollAPUnlocks (correct, no double
     * gold). But that means the new Bonus_OnAPItemReceived call site
     * never sees them either — counters stay at 0/N.
     *
     * This loop reads the bridge's per-char dedup file directly and
     * reconstructs the bonus counters from the historical AP location
     * IDs. Idempotent: calling Bonus_OnAPItemReceived for an already-
     * fired bit is a no-op (it returns TRUE without re-bumping). */
    {
        char dedupPath[MAX_PATH];
        char dir[MAX_PATH];
        GetSaveDir(dir, sizeof(dir));
        _snprintf(dedupPath, sizeof(dedupPath), "%sd2arch_bridge_locations_%s.dat",
                  dir, g_charName);
        FILE* df = fopen(dedupPath, "r");
        if (df) {
            extern BOOL Bonus_OnAPItemReceived(int apId);
            char dline[64];
            int backfilled = 0;
            while (fgets(dline, sizeof(dline), df)) {
                /* Format: "<sender_slot>:<location_id>" */
                int senderSlot, apLoc;
                if (sscanf(dline, "%d:%d", &senderSlot, &apLoc) == 2) {
                    if (apLoc >= 60000 && apLoc < 66000) {
                        if (Bonus_OnAPItemReceived(apLoc)) backfilled++;
                    }
                }
            }
            fclose(df);
            if (backfilled > 0) {
                Log("OnCharacterLoad: backfilled %d bonus check counters "
                    "from bridge dedup file\n", backfilled);
            }
        }
    }

    g_settingsFrozen = TRUE;
    Log("OnCharacterLoad complete for '%s' — settings frozen (SH=%d ZL=%d "
        "monShuf=%d bossShuf=%d xp=%dx apMode=%d)\n",
        g_charName, g_skillHuntingOn, g_zoneLockingOn,
        g_monsterShuffleEnabled, g_bossShuffleEnabled, g_xpMultiplier,
        (int)g_apMode);

    /* 1.9.0 — Catch-up flow on character load:
     *
     * The bridge's per-character dedup file
     * (Game/Save/d2arch_bridge_locations_<char>.dat) is the source of
     * truth for "what has this character already received". It is
     * persisted on disk so it survives game restarts AND remains
     * separate from other characters in the same slot.
     *
     * Catch-up happens automatically:
     *   1. Bridge connects (or is already connected).
     *   2. AP server sends ReceivedItems for the entire slot.
     *   3. process_item checks the per-char dedup; items already in it
     *      are skipped (already applied to this character).
     *   4. Items NOT in dedup are written to ap_unlocks.dat.
     *   5. DLL applies them and the bridge marks them in dedup so they
     *      won't fire again on next login.
     *
     * No DLL-side trigger needed — earlier "ap_resync.flag" hack
     * (1.9.0 dev) was removed because it cleared dedup unconditionally
     * and caused stackable filler items (gold, XP, stat pts) to
     * re-apply on every login. */
}
