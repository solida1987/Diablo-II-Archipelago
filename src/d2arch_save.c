
/* io.h provides _fileno/_commit for flushing .d2s writes through to disk (prevents half-written .d2s on power loss / crash-on-save). */
#include <io.h>
#include <errno.h>

/* RESET REQUEST FLAG ResetD2SFile zeroes the 30 skill bytes in the character's .d2s and queues a reinvest run. */
static volatile int g_resetRequested = 0;

/* 2.x (refund-on-swap) — set TRUE by the F1 editor when a Reset Point is spent to OVERWRITE an existing skill in a slot (btnIdx = tab*10+slot). */
static volatile int g_slotSwapRefund[30] = {0};

/* 2.x — "Reset Skill Points" button (F1 editor). */
static volatile int   g_resetAllPending  = 0;
static volatile DWORD g_resetAllDeadline = 0;
#define RESET_ALL_COST 3

/* Called by the reset-skills UI button (wire-up lives in d2arch_editor.c / d2arch_drawall.c click handlers). */
static void RequestD2SReset(void) {
    g_resetRequested = 1;
}

/* REINVEST CONSUME HELPERS The reinvest file d2arch_reinvest_<char>.dat is the source of truth for "points that were taken off skills during a reset and need to go back on". */
static int BeginReinvestConsume(char* outConsumingPath, size_t pathSize) {
    if (!outConsumingPath || pathSize == 0) return 0;
    if (!g_charName[0]) return 0;
    char archDir[MAX_PATH], origPath[MAX_PATH];
    GetCharFileDir(archDir, MAX_PATH);
    snprintf(origPath, sizeof(origPath), "%sd2arch_reinvest_%s.dat", archDir, g_charName);
    if (GetFileAttributesA(origPath) == INVALID_FILE_ATTRIBUTES) return 0;
    snprintf(outConsumingPath, pathSize, "%s.consuming", origPath);
    /* If an old .consuming file exists (crash during previous consume), remove it before the rename so MoveFileExA doesn't fail on the pre-existing target. */
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

/* GENERATIONAL .d2s BACKUP RETENTION Backups are named <char>.d2s.YYYYMMDD-HHMMSS.bak and sort alphabetically by timestamp. */
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
    /* 1.9.13 audit fix (2026-06-08): copy the filename substring at sl+1 into 'base' BEFORE truncating 'dir' at the slash. */
    strncpy(base, sl + 1, sizeof(base) - 1);
    base[sizeof(base) - 1] = 0;
    *(sl + 1) = 0;

    char pat[MAX_PATH];
    snprintf(pat, sizeof(pat), "%s%s.*.bak", dir, base);

    /* Collect matches. Worst case we cap at 64 — more than that and something is very wrong anyway. */
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

/* Save skill slots to file. */
static void SaveSlots(void) {
    if (!g_charName[0] || !g_poolInitialized) return;

    char dir[MAX_PATH], path[MAX_PATH], tmpPath[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_slots_%s.dat", dir, g_charName);
    sprintf(tmpPath, "%sd2arch_slots_%s.dat.tmp", dir, g_charName);

    FILE* f = fopen(tmpPath, "w");
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

    if (!MoveFileExA(tmpPath, path, MOVEFILE_REPLACE_EXISTING)) {
        DWORD err = GetLastError();
        Log("SaveSlots: rename failed (err=%lu) — leaving tmp at %s\n",
            err, tmpPath);
        return;
    }
    Log("SaveSlots: saved to %s\n", path);

    /* Skill level files are saved by the panel button click handler (d2arch_drawall.c) and by the reinvest system (d2arch_gameloop.c). */
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

    char line[8192]; /* wide: bonus_fired= bitmap line is ~1320 chars; 256 truncated it (lost fired-bits => dupe rewards + chest-quota corruption) */
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

/* Save state file (seed, skills, unlock status). */
static void SaveStateFile(void) {
    if (!g_charName[0] || !g_poolInitialized) return;

    char dir[MAX_PATH], path[MAX_PATH], tmpPath[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_state_%s.dat", dir, g_charName);
    sprintf(tmpPath, "%sd2arch_state_%s.dat.tmp", dir, g_charName);

    FILE* f = fopen(tmpPath, "w");
    if (!f) return;

    fprintf(f, "seed=%u\n", g_seed);
    fprintf(f, "num_starting=%d\n", NUM_STARTING);
    fprintf(f, "total_skills=%d\n", g_poolCount);
    /* pool_kind tells LoadStateFile whether to call InitClassOnlySkills (SH=OFF, fixed 30 native skills) or InitSkillPool (SH=ON, seeded 210-skill pool). */
    fprintf(f, "pool_kind=%s\n", g_skillHuntingOn ? "hunt" : "class_only");
    fprintf(f, "saved_class_id=%d\n", g_savedClass);
    /* write the class filter BEFORE the assignments= marker. */
    fprintf(f, "class_filter=%d\n",    g_classFilter ? 1 : 0);
    fprintf(f, "cls_amazon=%d\n",      g_clsEnabled[0] ? 1 : 0);
    fprintf(f, "cls_sorceress=%d\n",   g_clsEnabled[1] ? 1 : 0);
    fprintf(f, "cls_necromancer=%d\n", g_clsEnabled[2] ? 1 : 0);
    fprintf(f, "cls_paladin=%d\n",     g_clsEnabled[3] ? 1 : 0);
    fprintf(f, "cls_barbarian=%d\n",   g_clsEnabled[4] ? 1 : 0);
    fprintf(f, "cls_druid=%d\n",       g_clsEnabled[5] ? 1 : 0);
    fprintf(f, "cls_assassin=%d\n",    g_clsEnabled[6] ? 1 : 0);
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

    /* Save per-area kill counts, per difficulty. */
    for (int d = 0; d < 3; d++)
        for (int i = 0; i < MAX_AREA_ID; i++)
            if (g_areaKills[d][i] > 0)
                fprintf(f, "areakills_%d_%d=%d\n", d, i, g_areaKills[d][i]);

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
    /* skill/stat-point overflow ledgers (points that didn't fit the .d2s 8/10-bit fields; re-granted on load before reinvest drains the pool). */
    fprintf(f, "skillPtsLedger=%d\n", g_skillPtsLedger);
    fprintf(f, "statPtsLedger=%d\n", g_statPtsLedger);
    /* persist the server-side pending reward queue. */
    fprintf(f, "srvPendGold=%d\n",     g_serverPendingGold);
    fprintf(f, "srvPendStatPts=%d\n",  g_serverPendingStatPts);
    fprintf(f, "srvPendSkillPts=%d\n", g_serverPendingSkillPts);
    fprintf(f, "srvPendXP=%d\n",       g_serverPendingXP);

    /* Save ALL settings per-character so switching chars preserves each one's config */
    /* write the two new independent toggles + keep legacy field populated so older mod builds can still read this file. */
    fprintf(f, "skill_hunting=%d\n", g_skillHuntingOn ? 1 : 0);
    fprintf(f, "zone_locking=%d\n",  g_zoneLockingOn  ? 1 : 0);
    fprintf(f, "game_mode=%d\n",     g_zoneLockingOn ? 1 : 0); /* legacy fallback */
    {
        /* persist SkillLevelReqs toggle per-character */
        extern BOOL g_skillLevelReqs;
        fprintf(f, "skill_level_reqs=%d\n", g_skillLevelReqs ? 1 : 0);
    }
    {
        /* persist the skill point cap per-character, same as every other frozen-at-creation setting. */
        extern int g_skillMaxLevel;
        fprintf(f, "skill_max_level=%d\n", g_skillMaxLevel);
    }
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
    /* bake shuffle/xp/class settings into per-char state so they're frozen at character creation and survive title-screen toggle changes. */
    fprintf(f, "monster_shuffle=%d\n", g_monsterShuffleEnabled ? 1 : 0);
    fprintf(f, "boss_shuffle=%d\n",    g_bossShuffleEnabled    ? 1 : 0);
    fprintf(f, "shop_shuffle=%d\n",    g_shopShuffleEnabled    ? 1 : 0);
    fprintf(f, "entrance_shuffle=%d\n", g_entranceShuffleEnabled ? 1 : 0);
    fprintf(f, "xp_multiplier=%d\n",   g_xpMultiplier);
    /* class_filter + cls_* are written EARLIER (before assignments=) so the loader applies them before rebuilding the skill pool — see note above. */
    fprintf(f, "death_link=%d\n",      g_apDeathLink   ? 1 : 0);

    /* 15 preload-id fields for gated zone-locking. */
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

    /* Gate-key receipt state (54 bits: 3 diffs × 18 slots) */
    for (int d = 0; d < 3; d++) {
        for (int s = 0; s < GATEKEY_PER_DIFF; s++) {
            if (g_gateKeyReceived[d][s])
                fprintf(f, "gatekey_%d_%d=1\n", d, s);
        }
    }

    /* Gate-boss kill state (54 bits: 3 diffs × 18 slots). */
    for (int d = 0; d < 3; d++) {
        for (int s = 0; s < GATEKEY_PER_DIFF; s++) {
            if (g_gateBossKilled[d][s])
                fprintf(f, "gateboss_killed_%d_%d=1\n", d, s);
        }
    }

    /* Always persist zone keys when any have been received. */
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

    /* Bonus check counters + fired bitmap. */
    {
        extern void Bonus_SaveToFile(FILE* f);
        Bonus_SaveToFile(f);
    }

    /* Extra check toggles + counters + fired bitmap. */
    {
        extern void Extra_SaveToFile(FILE* f);
        Extra_SaveToFile(f);
    }

    /* Custom goal state (only writes if goal=custom is active). */
    {
        extern void CustomGoal_SaveToFile(FILE* f);
        CustomGoal_SaveToFile(f);
    }

    fclose(f);

    /* atomic rename. See function header comment. */
    if (!MoveFileExA(tmpPath, path, MOVEFILE_REPLACE_EXISTING)) {
        DWORD err = GetLastError();
        Log("SaveStateFile: rename failed (err=%lu) — leaving tmp at %s\n",
            err, tmpPath);
        return;
    }
    Log("SaveStateFile: saved to %s\n", path);
}

/* Write checks file for AP bridge compatibility. */
static void WriteChecksFile(void) {
    if (!g_charName[0]) return;
    char dir[MAX_PATH], path[MAX_PATH], tmpPath[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_checks_%s.dat", dir, g_charName);
    /* 1.9.5 Bug 3 fix — atomic write via tmp+rename. */
    sprintf(tmpPath, "%sd2arch_checks_%s.dat.tmp", dir, g_charName);
    FILE* f = fopen(tmpPath, "w");
    if (!f) return;
    for (int diff = 0; diff < 3; diff++) {
        int offset = diff * 1000;
        for (int qid = 1; qid < MAX_QUEST_ID; qid++) {
            if (g_questCompleted[diff][qid])
                fprintf(f, "check=%d\n", qid + offset);
        }
    }
    /* gate-boss kills. Mirrors loc-id calc in d2arch_gameloop.c. */
    for (int diff = 0; diff < 3; diff++) {
        for (int slot = 0; slot < GATEKEY_PER_DIFF; slot++) {
            if (!g_gateBossKilled[diff][slot]) continue;
            int act = 0, gate = 0;
            if (!GateKey_ActGateFromSlot(slot, &act, &gate)) continue;
            int check_n = 5000 + diff * 1000 + act * 10 + gate;
            fprintf(f, "check=%d\n", check_n);
        }
    }
    /* Collection checks. 110 location offsets (0..109) at COLL_LOC_BASE = 50000, so check_n = 50000+offset - LOCATION_BASE = 8000 + offset. Each fired offset (set complete, rune/gem/ special collected) emits one line. The s_collCheckFired bitset lives in d2arch_collections.c — accessed via Coll_IsCheckFired. */
    {
        extern BOOL Coll_IsCheckFired(int locOffset);
        for (int off = 0; off < 110; off++) {
            if (Coll_IsCheckFired(off)) {
                int check_n = 8000 + off;
                fprintf(f, "check=%d\n", check_n);
            }
        }
    }

    /* Bonus check categories. */
    {
        extern int Bonus_NextFiredApId(int* iterState);
        int it = 0, apId;
        while ((apId = Bonus_NextFiredApId(&it)) >= 0) {
            int check_n = apId - 42000;  /* LOCATION_BASE */
            fprintf(f, "check=%d\n", check_n);
        }
    }
    /* Extra check categories (Cow expansion / Merc / Hellforge+High runes / NPC dialogue / Runeword / Cube). */
    {
        extern int Extra_NextFiredApId(int* iterState);
        int it = 0, apId;
        while ((apId = Extra_NextFiredApId(&it)) >= 0) {
            int check_n = apId - 42000;
            fprintf(f, "check=%d\n", check_n);
        }
    }
    fclose(f);
    /* 1.9.5 Bug 3 fix — atomic rename. */
    if (!MoveFileExA(tmpPath, path, MOVEFILE_REPLACE_EXISTING)) {
        DWORD err = GetLastError();
        Log("WriteChecksFile: rename failed (err=%lu) — leaving tmp at %s\n",
            err, tmpPath);
        return;
    }
    /* V2 pipe transport — forward any newly-fired locations to the launcher as CHECK: messages. */
    ApPipe_SendChecksFile(path);
}

/* periodic re-send sweep. */
void ResendChecksToLauncher(void) {
    if (!g_charName[0]) return;
    char dir[MAX_PATH], path[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_checks_%s.dat", dir, g_charName);
    ApPipe_SendChecksFile(path);
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
    g_deadHead  = 0;   /* ring write cursor must reset with the count */

    if (!g_charName[0]) return;
    char dir[MAX_PATH], path[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_state_%s.dat", dir, g_charName);

    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[8192]; /* wide: bonus_fired= bitmap line is ~1320 chars; 256 truncated it (lost fired-bits => dupe rewards + chest-quota corruption) */
    memset(g_questCompleted, 0, sizeof(g_questCompleted));
    memset(g_questKillCount, 0, sizeof(g_questKillCount));
    /* NOTE: Do NOT clear g_questGold or g_questRewardType here! */

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
        int areaId, kills, killDiff;
        if (sscanf(line, "areakills_%d_%d=%d", &killDiff, &areaId, &kills) == 3) {
            if (killDiff >= 0 && killDiff < 3 && areaId >= 0 && areaId < MAX_AREA_ID)
                g_areaKills[killDiff][areaId] = kills;
        } else if (sscanf(line, "areakills_%d=%d", &areaId, &kills) == 2) {
            /* pre-3.2.1 save: one shared tally, which was always the tally of whatever difficulty was last played. */
            if (areaId >= 0 && areaId < MAX_AREA_ID)
                g_areaKills[0][areaId] = kills;
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
            /* overflow ledgers (clamp negatives defensively). */
            if (sscanf(line, "skillPtsLedger=%d", &tmpVal) == 1)
                g_skillPtsLedger = (tmpVal > 0) ? tmpVal : 0;
            if (sscanf(line, "statPtsLedger=%d", &tmpVal) == 1)
                g_statPtsLedger = (tmpVal > 0) ? tmpVal : 0;
            /* restore the server-side pending reward queue (clamp negatives). */
            if (sscanf(line, "srvPendGold=%d", &tmpVal) == 1)
                g_serverPendingGold = (tmpVal > 0) ? tmpVal : 0;
            if (sscanf(line, "srvPendStatPts=%d", &tmpVal) == 1)
                g_serverPendingStatPts = (tmpVal > 0) ? tmpVal : 0;
            if (sscanf(line, "srvPendSkillPts=%d", &tmpVal) == 1)
                g_serverPendingSkillPts = (tmpVal > 0) ? tmpVal : 0;
            if (sscanf(line, "srvPendXP=%d", &tmpVal) == 1)
                g_serverPendingXP = (tmpVal > 0) ? tmpVal : 0;
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
            /* quest_story is no longer a toggle — always TRUE */
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
            /* restore ALL baked-at-creation settings from the per- character state file. */
            if (sscanf(line, "skill_hunting=%d", &tmpVal) == 1)
                g_skillHuntingOn = (tmpVal != 0);
            if (sscanf(line, "zone_locking=%d", &tmpVal) == 1)
                g_zoneLockingOn = (tmpVal != 0);
            /* Legacy game_mode fallback: only apply if neither new toggle was present in this state file. */
            if (sscanf(line, "game_mode=%d", &tmpVal) == 1 && tmpVal >= 0 && tmpVal <= 1) {
                /* Only honour legacy if the new keys haven't appeared yet on a prior line — simplest: assume if skill_hunting was written by 1.8.0 SaveStateFile, it's always there. */
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
            /* Guard was a fossil: written 1..10 back when the multiplier was
             * 1-based, never updated when it went 0-BASED 0..100 (d2arch_ap.c
             * clamps 0..100). So a saved 0 (legal: standard XP) and anything
             * over 10 (Maegis runs 25) was silently thrown away on load. Same
             * failure shape found TWICE in this file tonight — see the
             * skill_max_level read below. */
            /* ⚠ UNDER AP THE YAML IS THE CONTRACT — the state file is only a
             * cache, and it must never override a setting the slot_data just
             * delivered.
             *
             * OnCharacterLoad calls LoadAPSettings() for NEW characters only,
             * then LoadStateFile() runs and (per its own comment) "OVERRIDES
             * global settings with per-character saved settings". That is right
             * for standalone, where the state file IS the source. It is wrong
             * for AP: v3.7.1 started reading these three back and instantly
             * regressed Marco's live game — his ap_settings.dat said
             * skill_max_level=99, his state file still held the stale 20 from
             * before the key was ever read, and the stale value won. Same for
             * skill_level_reqs (yaml off, state on). */
            if (!g_apConnected) {
                if (sscanf(line, "xp_multiplier=%d", &tmpVal) == 1 && tmpVal >= 0 && tmpVal <= 100)
                    g_xpMultiplier = tmpVal;
                {
                    extern int g_skillMaxLevel;
                    if (sscanf(line, "skill_max_level=%d", &tmpVal) == 1 && tmpVal >= 1 && tmpVal <= 99)
                        g_skillMaxLevel = tmpVal;
                }
                {
                    extern BOOL g_skillLevelReqs;
                    if (sscanf(line, "skill_level_reqs=%d", &tmpVal) == 1)
                        g_skillLevelReqs = (tmpVal != 0);
                }
            }
            if (sscanf(line, "class_filter=%d", &tmpVal) == 1)
                g_classFilter = (tmpVal != 0);
            if (sscanf(line, "cls_amazon=%d", &tmpVal) == 1)      g_clsEnabled[0] = (tmpVal != 0);
            if (sscanf(line, "cls_sorceress=%d", &tmpVal) == 1)   g_clsEnabled[1] = (tmpVal != 0);
            if (sscanf(line, "cls_necromancer=%d", &tmpVal) == 1) g_clsEnabled[2] = (tmpVal != 0);
            if (sscanf(line, "cls_paladin=%d", &tmpVal) == 1)     g_clsEnabled[3] = (tmpVal != 0);
            if (sscanf(line, "cls_barbarian=%d", &tmpVal) == 1)   g_clsEnabled[4] = (tmpVal != 0);
            if (sscanf(line, "cls_druid=%d", &tmpVal) == 1)       g_clsEnabled[5] = (tmpVal != 0);
            if (sscanf(line, "cls_assassin=%d", &tmpVal) == 1)    g_clsEnabled[6] = (tmpVal != 0);
            if (sscanf(line, "death_link=%d", &tmpVal) == 1)      g_apDeathLink = (tmpVal != 0);

            /* 15 preload-id fields (gated zone-locking). */
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

            /* 1.9.2 NEW: extra check toggles + counters + fired bitmap */
            if (strncmp(line, "extra_", 6) == 0) {
                extern void Extra_LoadLine(const char* line);
                Extra_LoadLine(line);
            }

            /* 1.9.2 NEW: custom goal state (cgt_active / cgt_gold / cgt_required / cgt_fired). */
            if (strncmp(line, "cgt_", 4) == 0) {
                extern void CustomGoal_LoadLine(const char* line);
                CustomGoal_LoadLine(line);
            }
        }
    }
    fclose(f);

    /* rebuild the per-struct quest .completed flags against the FINAL difficulty. */
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++) {
            int qid = g_acts[a].quests[q].id;
            BOOL done = (g_currentDifficulty >= 0 && g_currentDifficulty < 3 &&
                         qid >= 0 && qid < MAX_QUEST_ID)
                        ? g_questCompleted[g_currentDifficulty][qid] : FALSE;
            g_acts[a].quests[q].completed = done;
        }

    /* Gold amounts and reward types are now generated by AssignAllRewards (seed-deterministic) */

    /* Update kill quest counts from loaded area kills */
    for (int a = 0; a < 5; a++)
        for (int q = 0; q < g_acts[a].num; q++)
            if (g_acts[a].quests[q].type == QTYPE_KILL) {
                int aid = g_acts[a].quests[q].param;
                if (aid >= 0 && aid < MAX_AREA_ID)
                    g_acts[a].quests[q].killCount =
                        g_areaKills[KillDiff(g_currentDifficulty)][aid];
            }

    /* re-derive the bonus slot counters from the fired bitmap now that the whole state file has been parsed. */
    {
        extern void Bonus_RebuildCountersFromBitmap(void);
        Bonus_RebuildCountersFromBitmap();
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

    /* default the ledgers to 0 so a pre-D8 state file (no ledger line) loads as 0 rather than inheriting the previous character's value. */
    g_skillPtsLedger = 0;
    g_statPtsLedger  = 0;

    DWORD seed = 0;
    char line[8192]; /* wide: bonus_fired= bitmap line is ~1320 chars; 256 truncated it (lost fired-bits => dupe rewards + chest-quota corruption) */
    BOOL inAssignments = FALSE;
    int unlockIdx = 0;

    /* pool_kind tells us how to populate the pool when we hit the assignments= marker. */
    char pool_kind[16] = "hunt";
    int  saved_class_id = -1;

    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "seed=%u", &seed) == 1) continue;
        if (sscanf(line, "pool_kind=%15s", pool_kind) == 1) continue;
        if (sscanf(line, "saved_class_id=%d", &saved_class_id) == 1) {
            /* feed InitSkillPool's class fallback (it runs below at the assignments= dispatch, possibly before the live player unit is up). */
            if (saved_class_id >= 0) g_skillClassHint = saved_class_id;
            continue;
        }
        /* 2.x — parse the class filter HERE (before the assignments= dispatch that rebuilds the skill pool), so InitSkillPool below builds the pool with the correct filter on an existing-character reload. */
        {
            int cf;
            if (sscanf(line, "class_filter=%d", &cf) == 1)    { g_classFilter   = (cf != 0); continue; }
            if (sscanf(line, "cls_amazon=%d", &cf) == 1)      { g_clsEnabled[0] = (cf != 0); continue; }
            if (sscanf(line, "cls_sorceress=%d", &cf) == 1)   { g_clsEnabled[1] = (cf != 0); continue; }
            if (sscanf(line, "cls_necromancer=%d", &cf) == 1) { g_clsEnabled[2] = (cf != 0); continue; }
            if (sscanf(line, "cls_paladin=%d", &cf) == 1)     { g_clsEnabled[3] = (cf != 0); continue; }
            if (sscanf(line, "cls_barbarian=%d", &cf) == 1)   { g_clsEnabled[4] = (cf != 0); continue; }
            if (sscanf(line, "cls_druid=%d", &cf) == 1)       { g_clsEnabled[5] = (cf != 0); continue; }
            if (sscanf(line, "cls_assassin=%d", &cf) == 1)    { g_clsEnabled[6] = (cf != 0); continue; }
        }
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
            /* Parse: name,classCode,unlocked,id. */
            char name[64], cls[8];
            int unlocked, id;
            if (!strchr(line, ',')) continue;
            /* Require all 4 fields: with >=3 a truncated line left `id` uninitialized and the pool loop below matched it against random stack garbage -> random unlock-flag flips. */
            if (sscanf(line, "%63[^,],%7[^,],%d,%d", name, cls, &unlocked, &id) == 4) {
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

/* dirty-flag pattern for crash-safe incremental save. */
static BOOL g_stateDirty = FALSE;

void MarkStateDirty(void) {
    g_stateDirty = TRUE;
}

static void PeriodicSave(void) {
    DWORD now = GetTickCount();
    /* once the launcher pipe is connected, push the full active location universe ONCE per character so the standalone tracker shows unchecked locations + totals (g_activeLocsSent reset in OnCharacterLoad). */
    if (!g_activeLocsSent && ApPipe_Mode() && ApPipe_Active()) {
        SendActiveLocationsToLauncher();
        g_activeLocsSent = TRUE;
    }
    if (g_stateDirty && (now - g_lastSave) >= 250) {
        /* Dirty + throttle elapsed -> flush */
        g_lastSave = now;
        g_stateDirty = FALSE;
        SaveSlots();
        SaveStateFile();
        /* also flush the checks file here so every newly-fired check (extra/bonus/collection — they all MarkStateDirty) is forwarded to the launcher pipe within 250ms. */
        WriteChecksFile();
    } else if (!g_stateDirty && (now - g_lastSave) > 10000) {
        /* Defensive 10-s heartbeat — catches any mutation that forgot to call MarkStateDirty. */
        g_lastSave = now;
        SaveSlots();
        SaveStateFile();
        WriteChecksFile();
    }
}

/* CHARACTER DETECTION & RELOAD */
static char g_lastCharName[32] = {0};
static void* g_lastPlayerPtr = NULL;

/* D2S SKILL RESET — Modify .d2s on disk to zero skills and refund to NEWSKILLS pool. */

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

/* CSvBits for D2 1.10f stats — complete table from ItemStatCost.txt. */
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

/* Crash-recovery fallback: read the per-button cache file for a given btnIdx (the same file the click handler in d2arch_drawall.c writes atomically on every skill point spend, 1.9.10). */
static int ReadPerButtonCacheLevel(const char* charName, int btnIdx) {
    if (!charName || !charName[0]) return -1;
    if (btnIdx < 0 || btnIdx >= 30) return -1;

    char dir[MAX_PATH], path[MAX_PATH], sfx[32];
    GetCharFileDir(dir, MAX_PATH);
    if (btnIdx == 0) {
        snprintf(path, sizeof(path), "%sd2arch_fireball_%s.dat", dir, charName);
    } else {
        snprintf(sfx, sizeof(sfx), "d2arch_skill%d_", btnIdx + 1);
        snprintf(path, sizeof(path), "%s%s%s.dat", dir, sfx, charName);
    }

    FILE* f = fopen(path, "r");
    if (!f) return -1;
    char buf[16] = {0};
    size_t r = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (r == 0) return -1;
    buf[r] = 0;
    int lvl = atoi(buf);
    if (lvl < 0)  lvl = 0;
    if (lvl > 99) lvl = 99;   /* sanity clamp matching .d2s read */
    return lvl;
}

/* Reset skills in .d2s file on disk. */
static void ResetD2SFile(const char* charName) {
    if (!charName || !charName[0]) return;

    /* kill switch to test if .d2s rewrite is corrupting items (specifically: runeword items losing socketed runes after relog). */
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

    /* Create generational backup: <d2sPath>.YYYYMMDD-HHMMSS.bak. */
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

    /* scan items section BEFORE we modify anything, to verify runeword items + sockets are intact. */
    BOOL hadItemsPre = FALSE;   /* 2.x — used by the post-rewrite validation */
    {
        int jmStart = -1;
        for (int i = 800; i < fileSize - 4; i++) {
            if (data[i] == 'J' && data[i+1] == 'M' && data[i+2] != 0xAA) {
                jmStart = i; break;
            }
        }
        if (jmStart > 0 && jmStart + 4 <= fileSize) {
            int itemCount = data[jmStart+2] | (data[jmStart+3] << 8);
            hadItemsPre = TRUE;
            Log("ResetD2SFile[PRE]: items section JM at byte %d, count=%d, fileSize=%ld\n",
                jmStart, itemCount, fileSize);
        } else {
            Log("ResetD2SFile[PRE]: no JM items header found in fileSize=%ld\n", fileSize);
        }
    }

    /* Find "gf" (stats) and "if" (skills) headers. */
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
    BOOL parseHit1FF = FALSE;   /* 2.x — clean-termination guard (see abort below) */

    while (numStats < D2S_MAX_STATS) {
        int peekBit = bitPos;
        int statId = d2s_read_bits(data, fileSize, peekBit, 9);
        peekBit += 9;
        if (statId == 0x1FF) {
            knownEndBit = peekBit;
            parseHit1FF = TRUE;
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

    /* 2.x (data-loss hardening) — if the stat section did NOT terminate on the 0x1FF marker, our understanding of the bit-stream is incomplete and a re-encode would mis-align the raw tail that follows -> a corrupt .d2s that D2 can't load ("character cleared itself"). */
    if (!parseHit1FF) {
        Log("ResetD2SFile: ABORT — stat parse did not reach 0x1FF terminator "
            "(numStats=%d); leaving '%s' untouched to avoid corruption\n",
            numStats, charName);
        free(data);
        return;
    }

    int newPool = skillPoints + poolValue;
    Log("ResetD2SFile: '%s' skills=%d + pool=%d = %d total\n",
        charName, skillPoints, poolValue, newPool);

    /* ALWAYS run the full flow — no early returns. */

    /* NO early returns. NO phase 1 scan. Go straight to phase 2. Phase 2 always runs: parse stats, add pending, zero skills, re-encode, write. */
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

            /* Source of truth is the .d2s skill section. */
            /* 2.x — "Reset Skill Points" button commit. */
            if (g_resetAllPending) {
                if (GetTickCount() <= g_resetAllDeadline && g_resetPoints >= RESET_ALL_COST) {
                    for (int t = 0; t < 3; t++)
                        for (int s = 0; s < 10; s++)
                            if (g_tabSlots[t][s] >= 0) g_slotSwapRefund[t * 10 + s] = 1;
                    g_resetPoints -= RESET_ALL_COST;
                    Log("ResetD2SFile: RESET-ALL committed — all skill points refunded, "
                        "charged %d Reset Points (now %d)\n", RESET_ALL_COST, g_resetPoints);
                } else {
                    Log("ResetD2SFile: RESET-ALL arm expired/insufficient points — cancelled, no charge\n");
                }
                g_resetAllPending = 0;
                g_resetAllDeadline = 0;
            }

            int reinvestTotal = 0;   /* running sum, clamped to newPool below */
            for (int t = 0; t < 3; t++) {
                for (int s = 0; s < 10; s++) {
                    int btnIdx = t * 10 + s;
                    if (slotAssign[t][s] < 0) continue;          /* empty slot */
                    if (g_reinvestCount >= 30) break;             /* full */
                    if (btnIdx >= 30) continue;                   /* defensive */

                    int level_d2s = (int)skills30[btnIdx];

                    /* Sanity-clamp: vanilla D2 levels are 0..99. */
                    if (level_d2s < 0)  level_d2s = 0;
                    if (level_d2s > 99) level_d2s = 99;

                    /* crash recovery: cross-reference the per-button cache file (atomic since 1.9.10) in case D2's own non- atomic .d2s write was truncated by a crash. */
                    int level_cache = ReadPerButtonCacheLevel(charName, btnIdx);
                    int level = level_d2s;
                    const char* src = ".d2s";
                    if (level_cache > level_d2s) {
                        level = level_cache;
                        src = "per-button cache (CRASH RECOVERY — .d2s was lower)";
                        Log("ResetD2SFile: btnIdx %d skill %d — .d2s=%d cache=%d "
                            "using CACHE (crash recovery)\n",
                            btnIdx, slotAssign[t][s], level_d2s, level_cache);
                    }

                    /* 2.x (refund-on-swap) — this slot's skill was REPLACED via a Reset Point in the F1 editor. */
                    if (g_slotSwapRefund[btnIdx]) {
                        Log("ResetD2SFile: btnIdx %d SWAPPED — refunding %d pts to pool, "
                            "new skill %d starts at 0\n", btnIdx, level, slotAssign[t][s]);
                        level = 0;
                        {
                            char zp[MAX_PATH], zsfx[32];
                            GetCharFileDir(zp, MAX_PATH);
                            if (btnIdx == 0) strcat(zp, "d2arch_fireball_");
                            else { sprintf(zsfx, "d2arch_skill%d_", btnIdx + 1); strcat(zp, zsfx); }
                            strcat(zp, charName); strcat(zp, ".dat");
                            FILE* zf = fopen(zp, "w");
                            if (zf) { fprintf(zf, "0"); fclose(zf); }
                        }
                        g_slotSwapRefund[btnIdx] = 0;
                    }

                    /* 2.1 (Maegis #11/#12/#13 — "skill points lost / -1 on reload") Clamp to the refunded pool. */
                    int remaining = newPool - reinvestTotal;
                    if (level > remaining) level = remaining;
                    if (level < 0) level = 0;

                    if (level > 0) {
                        g_reinvestSkills[g_reinvestCount] = slotAssign[t][s];
                        g_reinvestPoints[g_reinvestCount] = level;
                        g_reinvestBtnIdx[g_reinvestCount] = btnIdx;
                        g_reinvestCount++;
                        reinvestTotal += level;
                        Log("ResetD2SFile: will reinvest %d pts in skill %d at btnIdx %d (source=%s)\n",
                            level, slotAssign[t][s], btnIdx, src);
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
                    /* 3-column format `skillId,level,btnIdx`. */
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

    /* Update NEWSKILLS in parsed stats (skill reset refund only). */
    {
        int totalNewSkills = newPool;
        /* ACCUMULATE into the ledger — never reset it here. */
        if (totalNewSkills > 255) {
            g_skillPtsLedger += totalNewSkills - 255;
            Log("ResetD2SFile: NEWSKILLS %d > 255 — storing 255, ledger=%d\n",
                totalNewSkills, g_skillPtsLedger);
            totalNewSkills = 255;
        }
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

    /* same guard for STATPTS (stat 4, 10-bit field, max 1023). */
    {
        /* Accumulate — see NEWSKILLS ledger comment above (same load-time wipe hazard). */
        for (int i = 0; i < numStats; i++) {
            if (stats[i].id == STAT_STATPTS && stats[i].value > 1023) {
                g_statPtsLedger += stats[i].value - 1023;
                Log("ResetD2SFile: STATPTS %d > 1023 — storing 1023, ledger=%d\n",
                    stats[i].value, g_statPtsLedger);
                stats[i].value = 1023;
                break;
            }
        }
    }

    /* do NOT discard the pending reward queue here. */

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

    /* 2.x (data-loss hardening) — VALIDATE the rewritten buffer in-memory before it is allowed to replace the original. */
    {
        BOOL ok = TRUE; const char* why = "";
        if (newFileSize < 100) { ok = FALSE; why = "size<100"; }
        else if (!(newData[0]==0x55 && newData[1]==0xAA && newData[2]==0x55 && newData[3]==0xAA)) {
            ok = FALSE; why = "bad signature";
        } else {
            int vgf = -1;
            for (int i = 700; i < newFileSize - 2; i++)
                if (newData[i]==0x67 && newData[i+1]==0x66) { vgf = i; break; }
            if (vgf < 0) { ok = FALSE; why = "no gf section"; }
            else {
                int vif = -1;
                for (int i = vgf; i < newFileSize - 2; i++)
                    if (newData[i]==0x69 && newData[i+1]==0x66) { vif = i; break; }
                if (vif < 0) { ok = FALSE; why = "no if section"; }
            }
        }
        if (ok && hadItemsPre) {
            int vjm = -1;
            for (int i = 800; i < newFileSize - 4; i++)
                if (newData[i]=='J' && newData[i+1]=='M' && newData[i+2]!=0xAA) { vjm = i; break; }
            if (vjm < 0) { ok = FALSE; why = "items (JM) lost"; }
        }
        if (!ok) {
            /* The .d2s is NOT being reset, so the skills are still in it. */
            char rdir[MAX_PATH], rpath[MAX_PATH];
            GetCharFileDir(rdir, MAX_PATH);
            sprintf(rpath, "%sd2arch_reinvest_%s.dat", rdir, charName);
            remove(rpath);
            g_reinvestPending = FALSE;
            g_reinvestCount = 0;
            Log("ResetD2SFile: ABORT before write — rewrite failed validation (%s); "
                "leaving '%s' untouched (backup retained, reinvest cancelled)\n", why, charName);
            free(data); free(newData);
            return;
        }
    }

    /* Atomic write: write to <d2sPath>.tmp, flush to disk, then rename over the original. */
    {
        char tmpPath[MAX_PATH];
        snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", d2sPath);
        FILE* fo = fopen(tmpPath, "wb");
        if (!fo) {
            Log("ResetD2SFile: fopen tmp failed err=%d path='%s'\n", errno, tmpPath);
            free(data); free(newData);
            return;
        }
        /* scan items section in NEW data to verify the byte shift didn't corrupt the JM headers. */
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
        /* Push through disk cache. */
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

/* GrantPendingReinvestAsFreePool If the reinvest file still exists long after a reset (> 30s timeout in the game-tick reinvest path), something broke in the reinvest chain. */
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

    /* Locate and edit the .d2s directly: read current NEWSKILLS value, add totalPoints to it, re-encode, atomic-write back. */
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
    /* NEWSKILLS is 8 bits (0-255). */
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

/* Known d2arch_<prefix>_<charname>.dat file prefixes. */
/* 1.8.0+1.9.0: every per-character file the mod writes. */
static const char* const s_cleanupPrefixes[] = {
    "d2arch_state_",
    "d2arch_slots_",
    "d2arch_checks_",
    "d2arch_reinvest_",
    "d2arch_fireball_",
    "d2arch_applied_",       /* AP filler dedup state */
    "d2arch_spoiler_",       /* standalone reward spoiler (.txt!) */
    "d2arch_ap_",            /* per-char AP binding (server/slot/password) */
    "ap_stash_stk_",         /* STK tab layout per char */
    "ap_stash_ser_",         /* serialized per-char AP stash (tabs 0..9) */
    "ap_stash_",             /* raw per-char AP stash dump */
    "ap_collections_",       /* F1 Collection per-char sidecar */
    "ap_stats_",             /* per-char lifetime stats */
};

/* Does `fname` match a known prefix? */
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

/* One-time migration: move legacy per-character files from Game/Archipelago/ into Game/Save/ where the post-1.9.0 code expects them. */
static void MigrateLegacyPerCharFiles(void) {
    char archDir[MAX_PATH], saveDir[MAX_PATH];
    GetArchDir(archDir, MAX_PATH);
    GetSaveDir(saveDir, MAX_PATH);
    if (!archDir[0] || !saveDir[0]) return;

    /* If GetSaveDir resolved to the same path as GetArchDir (degenerate fallback when no Save folder exists), there's nothing to move. */
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
                /* Dest exists — delete the legacy copy so we don't keep sweeping it on every char load. */
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

/* Clean up orphaned save files for deleted characters. */
static void CleanupOrphanedSaves(void) {
    char archDir[MAX_PATH], saveDir[MAX_PATH];
    GetArchDir(archDir, MAX_PATH);
    GetSaveDir(saveDir, MAX_PATH);

    /* Guard: save dir must resolve + exist. */
    if (!saveDir[0] || GetFileAttributesA(saveDir) == INVALID_FILE_ATTRIBUTES) {
        Log("CLEANUP: aborted — save dir invalid or missing: '%s'\n", saveDir);
        return;
    }

    /* Run the cleanup pass over BOTH dirs. */
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
    /* Sanity banner: log resolved archDir/saveDir on every character load so we can diagnose save-path issues from user logs without guesswork. */
    {
        char _archDir[MAX_PATH], _saveDir[MAX_PATH];
        GetArchDir(_archDir, sizeof(_archDir));
        GetSaveDir(_saveDir, sizeof(_saveDir));
        Log("OnCharacterLoad: char='%s' archDir='%s' saveDir='%s'\n",
            g_charName, _archDir, _saveDir);
    }

    /* new cel "session generation". */
    {
        extern int g_celSessionGen;
        g_celSessionGen++;
        Log("OnCharacterLoad: cel session generation -> %d (UI cels will free+reload)\n",
            g_celSessionGen);
    }

    /* Undo any monster/boss shuffle from the PREVIOUS character before we switch to this character's shuffle config. */
    UndoMonsterShuffle();
    UndoBossShuffle();
    /* same isolation requirement for entrance shuffle. */
    UndoEntranceShuffle();

    /* re-apply per-class skill animations. */
    Skilltree_OnCharacterLoadHook();

    /* reset the level-milestone backfill flag so the first tick of CheckLevelMilestones silently marks already-met milestones (level <= player level) as completed without firing rewards. */
    {
        extern void Milestone_ResetBackfill(void);
        Milestone_ResetBackfill();
    }

    /* same silent-backfill for quest flags so loading a character with pre-existing .d2s quest progress doesn't flood the AP server with all its already-completed quest checks at once (Xnylhsa MultiAP report). */
    {
        extern void QuestFlag_ResetBackfill(void);
        QuestFlag_ResetBackfill();
    }
    /* re-send the active location universe to the launcher for the newly-loaded character (PeriodicSave does it once the pipe is up). */
    g_activeLocsSent = FALSE;

    /* gate the NPC dialogue poll for the first 5 seconds after character load. */
    {
        extern void Extra_NpcPoll_OnCharacterLoad(void);
        Extra_NpcPoll_OnCharacterLoad();
    }

    /* Move any legacy per-char files from Archipelago/ into Save/ BEFORE CleanupOrphanedSaves runs, so files belonging to existing characters get relocated rather than deleted as orphans. */
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

    /* Clear reinvest-levels scratch so an old character's levels don't leak into the new character's panel. */
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
    /* clear gate-key receipt + gate-boss kill state on char switch. */
    memset(g_gateKeyReceived, 0, sizeof(g_gateKeyReceived));
    memset(g_gateBossKilled,  0, sizeof(g_gateBossKilled));

    /* Defensive: reset ALL randomization-settings globals to safe neutral defaults BEFORE any source (LoadAPSettings or LoadChecks) gets to populate them. */
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
    /* rebalanced filler weights per user feedback. */
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
    g_xpMultiplier          = 0;   /* 0-based: 0 = standard XP */
    g_classFilter           = FALSE;
    for (int c = 0; c < 7; c++) g_clsEnabled[c] = TRUE;
    g_apDeathLink           = FALSE;
    memset(g_actPreload, 0, sizeof(g_actPreload));
    /* 1.8.0 cleanup: Treasure Cow state reset extracted (char-load) */
    g_deadCount = 0;
    g_deadHead  = 0;   /* ring write cursor must reset with the count */
    g_pendingRewardGold = 0;
    g_pendingRewardStatPts = 0;
    g_pendingRewardSkillPts = 0;
    /* reset the server-side pending queue for per-char isolation. */
    g_serverPendingGold = 0;
    g_serverPendingStatPts = 0;
    g_serverPendingSkillPts = 0;
    g_serverPendingXP = 0;

    /* clear bonus check state so the new character starts fresh. */
    {
        extern void Bonus_ResetState(void);
        Bonus_ResetState();
    }

    /* clear extra check state so the new character starts fresh. */
    {
        extern void Extra_ResetState(void);
        Extra_ResetState();
    }

    /* clear custom goal state. */
    {
        extern void CustomGoal_ResetState(void);
        CustomGoal_ResetState();
    }

    Log("Global state cleared for new character load\n");

    /* Reset cached data */
    g_sgptDT = 0;
    g_origCacheInit = FALSE;
    g_applyCount = 0;
    g_lastApply = 0;

    /* per-button cache wipe MOVED to after ResetD2SFile. */

    /* Settings sourcing rule (strict per-character): EXISTING character (state file exists): -> Read ONLY from d2arch_state_<char>.dat (LoadStateFile / LoadChecks). */
    BOOL hasExistingChar = FALSE;
    if (g_charName[0]) {
        char statePath[MAX_PATH], _archDir2[MAX_PATH];
        GetCharFileDir(_archDir2, MAX_PATH);
        sprintf(statePath, "%sd2arch_state_%s.dat", _archDir2, g_charName);
        hasExistingChar = (GetFileAttributesA(statePath) != INVALID_FILE_ATTRIBUTES);
    }

    /* close the AP-connect race window. */
    {
        extern BOOL AP_WaitForConnectIfPending(int maxMs);
        if (AP_WaitForConnectIfPending(5000)) {
            Log("OnCharacterLoad: AP connect raced — recovered before settings freeze\n");
        }
    }

    if (!hasExistingChar) {
        /* New character — capture from current source (AP or UI). */
        Log("OnCharacterLoad: NEW character — sourcing settings from %s\n",
            g_apConnected ? "AP slot_data (ap_settings.dat)" : "UI (d2arch.ini)");
        LoadAPSettings();

        /* Standalone preload randomization. */
        if (!g_apConnected && g_zoneLockingOn) {
            /* launcher-controlled seed. */
            unsigned seed;
            if (g_standaloneSeed != 0) {
                seed = g_standaloneSeed;
            } else {
                seed = GetTickCount();
                for (int i = 0; g_charName[i]; i++) {
                    seed = seed * 1664525u + (unsigned)(unsigned char)g_charName[i] + 1013904223u;
                }
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
        /* Existing character — per-char file is the only source. */
        Log("OnCharacterLoad: EXISTING character — settings will load from per-char file only\n");
    }

    /* Initialize zone locks BEFORE loading state (state will unlock received keys) */
    InitZoneLocks();

    /* Try to load existing state */
    if (LoadStateFile()) {
        LoadSlots();
        LoadChecks(); /* This OVERRIDES global settings with per-character saved settings */
        /* Re-initialize zone locks with per-character settings (goal_scope may differ). */
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
        /* NOTE: AssignAllRewards is NOT called here because LoadStateFile() already calls it internally (line that parses "assignments=" triggers InitSkillPool + AssignAllRewards). */

        /* Reconcile gate-key state from act-boss quest progress. */
        if (g_zoneLockingOn) {
            int rDiff = g_currentDifficulty;
            if (rDiff >= 0 && rDiff <= 2) {
                static const int actBossQid[6] = { 0, 6, 106, 206, 303, 406 };
                int implied = 0;
                for (int actDone = 1; actDone <= 5; actDone++) {
                    int qid = actBossQid[actDone];
                    if (qid <= 0 || qid >= MAX_QUEST_ID) continue;
                    if (!g_questCompleted[rDiff][qid]) continue;

                    /* Act <actDone> done -> all 4 gates of acts 1..actDone implied */
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
                                /* Use UnlockGateKey so g_zoneLocked[] is actually opened, plus the user-facing notify text fires. */
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
        /* New character — initialize skill pool. */
        /* launcher-controlled seed. */
        DWORD newSeed;
        if (!g_apConnected && g_standaloneSeed != 0) {
            newSeed = g_standaloneSeed;
        } else if (g_apConnected) {
            /* an Archipelago world must be REPRODUCIBLE. */
            char skIni[MAX_PATH], skVal[40] = {0};
            GetArchDir(skIni, MAX_PATH);
            strcat(skIni, "d2arch.ini");
            GetPrivateProfileStringA("settings", "SeedKey", "0",
                                     skVal, sizeof(skVal), skIni);
            unsigned __int64 sk = _strtoui64(skVal, NULL, 10);
            newSeed = (DWORD)(sk ^ (sk >> 32));
            if (newSeed == 0) {
                newSeed = GetTickCount();
                for (int i = 0; g_charName[i]; i++) {
                    newSeed = newSeed * 1664525u + (unsigned)(unsigned char)g_charName[i] + 1013904223u;
                }
                Log("SEED: AP but no SeedKey in ini - falling back to tick seed %u\n", newSeed);
            } else {
                Log("SEED: AP world seed from launcher SeedKey '%s' -> %u\n",
                    skVal, newSeed);
            }
        } else {
            newSeed = GetTickCount();
            for (int i = 0; g_charName[i]; i++) {
                newSeed = newSeed * 1664525u + (unsigned)(unsigned char)g_charName[i] + 1013904223u;
            }
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

    /* Load AP config but DON'T auto-connect. */
    g_apGoalComplete = FALSE;
    LoadAPCharConfig();

    /* ResetD2SFile is the refund-to-pool flow that makes the skill editor work: it reads the live .d2s skill levels, adds them back to NEWSKILLS, and writes a reinvest file so the game tick can re-apply them to whatever slots the user currently has assigned. */
    Log("OnCharacterLoad: running ResetD2SFile (refund + queue reinvest)\n");
    ResetD2SFile(g_charName);

    /* Wipe per-button cache files (deferred to after ResetD2SFile). */
    if (g_charName[0]) {
        char _bDir[MAX_PATH], _bPath[MAX_PATH];
        GetCharFileDir(_bDir, MAX_PATH);
        sprintf(_bPath, "%sd2arch_fireball_%s.dat", _bDir, g_charName);
        DeleteFileA(_bPath);
        for (int _bi = 1; _bi < 30; _bi++) {
            sprintf(_bPath, "%sd2arch_skill%d_%s.dat", _bDir, _bi + 1, g_charName);
            DeleteFileA(_bPath);
        }
        Log("OnCharacterLoad: wiped per-button cache files for '%s' "
            "(post-ResetD2SFile; reinvest will rewrite atomically)\n",
            g_charName);
    }
    SaveStateFile(); /* re-save after rewards consumed */
    /* g_resetRequested flag preserved for future use if needed, but no longer gates the normal refund flow. */
    g_resetRequested = 0;

    /* recover from a crashed reinvest consume. */
    {
        char cdir[MAX_PATH], cpath[MAX_PATH], rpath_final[MAX_PATH];
        GetCharFileDir(cdir, MAX_PATH);
        sprintf(cpath, "%sd2arch_reinvest_%s.dat.consuming", cdir, g_charName);
        sprintf(rpath_final, "%sd2arch_reinvest_%s.dat", cdir, g_charName);
        if (GetFileAttributesA(cpath) != INVALID_FILE_ATTRIBUTES) {
            Log("REINVEST RECOVERY: found .consuming file from prior crashed "
                "session, restoring to .dat\n");
            /* If a fresh .dat already exists (shouldn't normally), keep the .consuming copy as priority — it represents in-flight work. */
            if (GetFileAttributesA(rpath_final) != INVALID_FILE_ATTRIBUTES) {
                DeleteFileA(rpath_final);
            }
            if (!MoveFileExA(cpath, rpath_final,
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                Log("REINVEST RECOVERY: rename failed err=%lu — "
                    "falling back to GrantPendingReinvestAsFreePool\n",
                    GetLastError());
                /* Best-effort: read the .consuming content directly and credit points to NEWSKILLS via existing helper. */
                GrantPendingReinvestAsFreePool();
                DeleteFileA(cpath);
            }
        }
    }

    /* Load reinvest data from file if it exists (survives full game restart). */
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
                /* prefer the new 3-column format (skill,level,btnIdx). */
                int matched = sscanf(rline, "%d,%d,%d", &sid, &pts, &btn);
                if (matched < 3) btn = g_reinvestCount;     /* legacy fallback */
                /* re-clamp on apply: a single skill can't exceed level 99, so a larger value is corrupt file data. */
                if (pts > 99) pts = 99;
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

    /* Skill positions are handled by skilldesc.txt (all skills on page 1). */

    /* Apply monster/boss shuffle if enabled */
    if (g_monsterShuffleEnabled && !g_shuffleApplied) {
        ApplyMonsterShuffle(g_seed);
    }
    /* ⚠ TEMPORARILY DISABLED UPSTREAM — this branch no longer runs.
     *
     * Nothing here is switched off: the flag simply never arrives set any
     * more. The apworld forces act_boss_shuffle and boss_shuffle to 0 in
     * slot_data, and the launcher's standalone dialog no longer offers the
     * checkbox, so g_bossShuffleEnabled stays FALSE on every path.
     *
     * WHY: a shuffled act boss has been seen giving up its attack for good
     * once the player breaks line of sight -- reported as BetaHub #13, "Boss
     * Not Attacking After Hiding Behind Pillar with boss shuffle in Act 1". A
     * boss that will not fight is a seed that cannot be finished, and the
     * cause is not understood yet.
     *
     * ApplyBossShuffle and everything it touches are left exactly as they
     * were, deliberately: when the AI problem is solved this comes back by
     * opening the two gates, not by writing the feature again.
     *
     * TO RE-ENABLE, all three:
     *   1. apworld options.py  -- put ActBossShuffle back in the "Shuffles"
     *                             OptionGroup
     *   2. apworld __init__.py -- restore the two `or` expressions that build
     *                             act_boss_shuffle / boss_shuffle in slot_data
     *   3. Diablo-London-Plugin -- restore the checkbox in
     *      D2StandaloneSettingsDialog.BuildShuffles and the two B(false)
     *      emissions in D2RandomizerSettings
     *
     * Super-unique shuffle is a separate system and is NOT affected. */
    if (g_bossShuffleEnabled && !g_bossShuffleApplied) {
        ApplyBossShuffle(g_seed);
    }
    /* System 1 — dead-end cave entrance shuffle */
    if (g_entranceShuffleEnabled && !g_entranceShuffleApplied) {
        ApplyEntranceShuffle(g_seed);
    }

    /* Rift Maps removed 2026-05-05 — see Research/RIFT_AND_RESET_FAILURE_2026-05-05.md */

    /* Per-character AP-stash sidecar + initial tab seeding. */
    {
        extern BOOL StashLoadAP(const char* charName);
        extern int  g_activeStashTab;
        /* New serialized-byte shadow tables. */
        extern void StashSerResetMemory(void);
        extern void StashSerLoadAll(const char* charName);
        /* STK (stackable) tabs. */
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
            StkLoadAP(g_charName);        /* per-char STK tabs */
            Coll_LoadForCharacter(g_charName); /* collection flags */
            Stats_LoadForCharacter(g_charName);/* F1 logbook stats */
            Stats_OnCharacterChange();         /* reset playtime anchor */
            /* Initial scan deferred — pPlayerUnit may not be ready yet. */
        }
        StkLoadShared();                  /* account-wide STK tabs */
        Stats_LoadLifetime();             /* account-wide stats */
        /* AP chars land on AP1 (global 0), non-AP land on SH1 (global 10). */
        g_activeStashTab = g_apMode ? 0 : 10;
    }

    /* 1.8.0 NEW: reset Custom Boss spawn state so registered bosses can respawn in the newly-loaded session. */
    CustomBoss_Reset();
    /* 1.8.0 NEW: append gate-bosses from active preload set for the current difficulty. */
    CustomBoss_AppendGateBosses();
    /* BetaHub #10: whichever custom hunt targets the preload roll skipped
     * still spawn in their home zones — a hunt location must be completable
     * in every seed, not only the seeds whose preload happened to place it. */
    CustomBoss_AppendHuntFallbacks();
    /* 1.8.0 NEW: reset Treasure Cow per-act counters + area tracking. */
    TreasureCow_Reset();

    /* AP soft-lock reconcile (existing characters). */
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
                /* Persist the fix to per-char state file immediately so it survives a crash. */
                SaveStateFile();
                ShowNotify("Zone Locking auto-disabled (matches AP server)");
            }
        }
    }

    /* freeze all per-character settings from this point on. */
    /* Backfill bonus check counters from historical AP-server data. */
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

    /* The player-side settings follow the slot on every load, not only at
     * creation — an existing character otherwise came back at 0x XP every
     * launch (BetaHub #22). Runs before the freeze so the state file written
     * below carries the corrected values. */
    {
        extern void AP_ReapplyPlayerSideSettings(const char* why);
        AP_ReapplyPlayerSideSettings("character load");
    }

    g_settingsFrozen = TRUE;
    Log("OnCharacterLoad complete for '%s' — settings frozen (SH=%d ZL=%d "
        "monShuf=%d bossShuf=%d xp=%dx apMode=%d)\n",
        g_charName, g_skillHuntingOn, g_zoneLockingOn,
        g_monsterShuffleEnabled, g_bossShuffleEnabled, g_xpMultiplier,
        (int)g_apMode);

    /* Catch-up flow on character load: The bridge's per-character dedup file (Game/Save/d2arch_bridge_locations_<char>.dat) is the source of truth for "what has this character already received". */

    /* 1.9.5 Bug 3 fix — re-emit the full checks file after LoadChecks populates g_questCompleted/g_gateBossKilled/etc. */
    WriteChecksFile();

    /* eager pre-load of AP dedup sets so they are ready BEFORE the first PollAPUnlocks tick can fire. */
    {
        extern void AP_PreloadDedupForCurrentChar(void);
        if (g_apMode) {
            AP_PreloadDedupForCurrentChar();
        }
    }
}
