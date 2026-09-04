
/* MAIN DRAW FUNCTION */
/* AP PANEL — drawn from EndScene hook in menu screens */
/* ARCHIPELAGO IPC — File-based communication with ap_bridge.exe */

/* Start AP bridge process if not already running */
static PROCESS_INFORMATION g_bridgePI = {0};
static BOOL g_bridgeStarted = FALSE;

/* Robust AP lifecycle (Gaps 1, 2, 4 from Research/AP_LIFECYCLE_AUDIT_2026-05-11.md) */

/* Gap 1 — ghost-disconnect detection. */
static DWORD g_apLastBridgeHealthCheck = 0;   /* GetTickCount of last call */
static int   g_apBridgeRespawnCount    = 0;   /* total respawns this session */

/* Gap 2 — disconnect/reconnect notify state. */
static BOOL  g_apHadDisconnect         = FALSE;  /* set on disconnect, cleared on reconnect */
static DWORD g_apLastDisconnectNotify  = 0;

/* Gap 4 — server+slot hash for slot-change collision detection. */
static unsigned int g_apStoredServerSlotHash = 0;  /* 0 = no stored value */

/* Simple djb2 hash for server+slot fingerprint. */
static unsigned int APHashStr(const char* s) {
    unsigned int h = 5381;
    while (s && *s) {
        h = (h * 33u) ^ (unsigned char)*s++;
    }
    return h;
}

static unsigned int APCurrentServerSlotHash(void) {
    char combined[256];
    _snprintf(combined, sizeof(combined), "%s|%s", g_apIP, g_apSlot);
    combined[sizeof(combined) - 1] = 0;
    return APHashStr(combined);
}

/* additional slot_data fields from APworld that the DLL now acknowledges. */
/* g_shopShuffleEnabled moved to d2arch_shuffle.c so earlier files can see it */
/* 1.8.0 cleanup: g_treasureCowsEnabled extracted to Tools/Archipelago/pending_reimplementation/TREASURE_COWS/ */

/* persistent applied-AP-id set for filler dedup across reconnects. */
/* 4096 -> 16384 (2026-08-11). Both dedup stores failed SILENTLY at their cap:
 * this one dropped its oldest half on overflow, the filler-loc one truncated
 * on load — and everything that fell out got REDELIVERED on the next server
 * reconnect with no log line anywhere ("save and quit gave me 28 skill pts").
 * The caps are now big enough for any real run AND loud when hit. */
#define G_APPLIED_AP_CAP 16384
static int  g_appliedApIds[G_APPLIED_AP_CAP];
static int  g_appliedApCount = 0;
static BOOL g_appliedApLoaded = FALSE;

static void GetAppliedApFilePath(char* path, int pathSize) {
    char dir[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    _snprintf(path, pathSize, "%sd2arch_applied_%s.dat", dir, g_charName);
    path[pathSize - 1] = 0;
}

/* the per-character sidecars are keyed on the character NAME alone, so re-using a name on a NEW seed inherited the old seed's dedup state (items silently "already applied" = never delivered). */
static void ResolveSeedKey(const char* ini, char* key, size_t n);

/* WHICH of ResolveSeedKey's three sources the key came from, alongside the key itself. */
static void GetCurrentSeedKeyAndSrc(char* key, size_t n, char* src, size_t sn) {
    char dir[MAX_PATH], ini[MAX_PATH], probe[64];
    key[0] = 0;
    if (sn) src[0] = 0;
    GetArchDir(dir, MAX_PATH);
    _snprintf(ini, sizeof(ini), "%sd2arch.ini", dir);
    ini[sizeof(ini) - 1] = 0;
    ResolveSeedKey(ini, key, n);
    if (!key[0] || !sn) return;

    GetPrivateProfileStringA("settings", "SeedKey", "", probe, sizeof(probe), ini);
    if (probe[0]) { strncpy(src, "key", sn - 1); src[sn - 1] = 0; return; }
    GetPrivateProfileStringA("settings", "ShuffleSeed", "0", probe, sizeof(probe), ini);
    if (probe[0] && strcmp(probe, "0") != 0) {
        strncpy(src, "shuffle", sn - 1); src[sn - 1] = 0; return;
    }
    strncpy(src, "slot", sn - 1); src[sn - 1] = 0;
}

/* (GetCurrentSeedKey deleted 2026-08-29: defined but never called -- dead-code sweep, docs/BUILDPLAN_2026-08-29.md part 4) */


/* TRUE only when the two stamps are comparable AND genuinely differ. */
static BOOL SeedStampSaysDifferentWorld(const char* fileKey, const char* fileSrc,
                                        const char* curKey,  const char* curSrc) {
    if (!fileKey[0] || !curKey[0]) return FALSE;
    if (!fileSrc[0] || !curSrc[0]) return FALSE;
    if (strcmp(fileSrc, curSrc) != 0) return FALSE;
    return strcmp(fileKey, curKey) != 0;
}

static void LoadAppliedApIds(void) {
    if (g_appliedApLoaded) return;
    g_appliedApLoaded = TRUE;
    g_appliedApCount = 0;
    if (!g_charName[0]) return;
    char path[MAX_PATH];
    GetAppliedApFilePath(path, MAX_PATH);
    FILE* f = fopen(path, "r");
    if (!f) return;
    char curKey[64], fileKey[64], curSrc[16], fileSrc[16];
    GetCurrentSeedKeyAndSrc(curKey, sizeof(curKey), curSrc, sizeof(curSrc));
    fileKey[0] = 0; fileSrc[0] = 0;
    char line[64];
    while (fgets(line, sizeof(line), f) && g_appliedApCount < G_APPLIED_AP_CAP) {
        int id = 0;
        if (sscanf(line, "seed_key=%63[^\r\n]", fileKey) == 1) continue;
        if (sscanf(line, "seed_src=%15[^\r\n]", fileSrc) == 1) continue;
        if (sscanf(line, "applied_ap_id=%d", &id) == 1 && id > 0) {
            g_appliedApIds[g_appliedApCount++] = id;
        }
    }
    if (g_appliedApCount >= G_APPLIED_AP_CAP && !feof(f))
        Log("DEDUP: applied-AP file EXCEEDS cap %d — tail NOT loaded, "
            "those items will be redelivered on reconnect!\n", G_APPLIED_AP_CAP);
    fclose(f);
    /* Wipe the inherited dedup set only when the stamps prove a different world (see SeedStampSaysDifferentWorld). */
    if (SeedStampSaysDifferentWorld(fileKey, fileSrc, curKey, curSrc)) {
        Log("AP DEDUP: seed changed ('%s' -> '%s', src=%s) for char '%s' — wiping %d "
            "inherited applied-ids so this seed's items deliver\n",
            fileKey, curKey, curSrc, g_charName, g_appliedApCount);
        g_appliedApCount = 0;
        /* Truncate the file to just the new stamp — leaving the old ids behind would let the next load re-adopt them (the loader keeps every applied_ap_id line regardless of which stamp precedes it). */
        FILE* wf = fopen(path, "w");
        if (wf) { fprintf(wf, "seed_key=%s\nseed_src=%s\n", curKey, curSrc); fclose(wf); }
    }
    Log("AP DEDUP: loaded %d applied ap ids from %s\n", g_appliedApCount, path);
}

static BOOL IsApIdApplied(int apId) {
    if (!g_appliedApLoaded) LoadAppliedApIds();
    for (int i = 0; i < g_appliedApCount; i++) {
        if (g_appliedApIds[i] == apId) return TRUE;
    }
    return FALSE;
}

static void MarkApIdApplied(int apId) {
    if (!g_appliedApLoaded) LoadAppliedApIds();
    if (IsApIdApplied(apId)) return;
    if (g_appliedApCount >= G_APPLIED_AP_CAP) {
        /* Buffer full — drop the oldest half to make room. LOUDLY: every id
         * dropped here is an item the next reconnect will deliver AGAIN. */
        Log("DEDUP: applied-AP-id cap %d HIT — dropping oldest %d ids; "
            "expect redelivery of early items on next reconnect!\n",
            G_APPLIED_AP_CAP, G_APPLIED_AP_CAP / 2);
        memmove(g_appliedApIds, g_appliedApIds + (G_APPLIED_AP_CAP / 2),
                (G_APPLIED_AP_CAP / 2) * sizeof(int));
        g_appliedApCount = G_APPLIED_AP_CAP / 2;
    }
    g_appliedApIds[g_appliedApCount++] = apId;

    /* Append to per-character file so dedup survives restarts. */
    if (!g_charName[0]) return;
    char path[MAX_PATH];
    GetAppliedApFilePath(path, MAX_PATH);
    /* (re)stamp the seed key once per session/char so the next load can detect a name re-used on a different seed and wipe the stale dedup set. */
    {
        static char s_stampedFor[64] = {0};
        if (strcmp(s_stampedFor, g_charName) != 0) {
            char curKey[64], curSrc[16];
            GetCurrentSeedKeyAndSrc(curKey, sizeof(curKey), curSrc, sizeof(curSrc));
            if (curKey[0]) {
                FILE* sf = fopen(path, "a");
                if (sf) { fprintf(sf, "seed_key=%s\nseed_src=%s\n", curKey, curSrc); fclose(sf); }
                strncpy(s_stampedFor, g_charName, sizeof(s_stampedFor) - 1);
                s_stampedFor[sizeof(s_stampedFor) - 1] = 0;
            }
        }
    }
    FILE* f = fopen(path, "a");
    if (f) {
        fprintf(f, "applied_ap_id=%d\n", apId);
        fclose(f);
    }
}

/* per-location dedup for stackable filler items.
 * 2048 -> 8192 (2026-08-11): the load loop below stops reading at the cap, so
 * a file past it was silently truncated and the tail redelivered on
 * reconnect. See the matching note at G_APPLIED_AP_CAP. */
#define G_APPLIED_FILLER_LOC_CAP 8192
static int   g_appliedFillerLocs[G_APPLIED_FILLER_LOC_CAP];
static DWORD g_appliedFillerSndr[G_APPLIED_FILLER_LOC_CAP]; /* FNV of sender; 0 = legacy "any sender" */
static int   g_appliedFillerLocCount = 0;
static BOOL  g_appliedFillerLocsLoaded = FALSE;

/* AP location ids are only unique PER WORLD — a MultiAP with two D2 slots has overlapping 42000+ ids, so dedup must include WHO sent the item or the second world's copy of the same numeric location is swallowed as a replay (dropped filler; for progressive act keys: a gate that never opens). */
static DWORD FillerSenderHash(const char* sender) {
    DWORD h = 2166136261u;
    if (!sender || !sender[0]) return 0;
    for (const unsigned char* p = (const unsigned char*)sender; *p; p++) {
        h ^= (DWORD)((*p >= 'A' && *p <= 'Z') ? (*p + 32) : *p); /* case-fold */
        h *= 16777619u;
    }
    return h ? h : 1u; /* 0 is reserved for legacy/any */
}

static void GetAppliedFillerLocsPath(char* path, int pathSize) {
    char dir[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    _snprintf(path, pathSize, "%sd2arch_filler_locs_%s.dat", dir, g_charName);
    path[pathSize - 1] = 0;
}

static void LoadAppliedFillerLocs(void) {
    if (g_appliedFillerLocsLoaded) return;
    g_appliedFillerLocsLoaded = TRUE;
    g_appliedFillerLocCount = 0;
    if (!g_charName[0]) return;
    char path[MAX_PATH];
    GetAppliedFillerLocsPath(path, MAX_PATH);
    FILE* f = fopen(path, "r");
    if (!f) return;
    char curKey[64], fileKey[64], curSrc[16], fileSrc[16];
    GetCurrentSeedKeyAndSrc(curKey, sizeof(curKey), curSrc, sizeof(curSrc));
    fileKey[0] = 0; fileSrc[0] = 0;
    char line[96];
    while (fgets(line, sizeof(line), f) && g_appliedFillerLocCount < G_APPLIED_FILLER_LOC_CAP) {
        int loc = 0; unsigned sh = 0;
        if (sscanf(line, "seed_key=%63[^\r\n]", fileKey) == 1) continue;
        if (sscanf(line, "seed_src=%15[^\r\n]", fileSrc) == 1) continue;
        if (sscanf(line, "loc=%d,sh=%x", &loc, &sh) >= 1 && loc > 0) {
            g_appliedFillerLocs[g_appliedFillerLocCount]   = loc;
            g_appliedFillerSndr[g_appliedFillerLocCount++] = (DWORD)sh; /* legacy lines -> 0 */
        }
    }
    if (g_appliedFillerLocCount >= G_APPLIED_FILLER_LOC_CAP && !feof(f))
        Log("AP FILLER LOC DEDUP: file EXCEEDS cap %d — tail NOT loaded, "
            "those fillers will be redelivered on reconnect!\n",
            G_APPLIED_FILLER_LOC_CAP);
    fclose(f);
    /* C5 (fixed): same seed-stamp wipe as the applied-ids file. */
    if (SeedStampSaysDifferentWorld(fileKey, fileSrc, curKey, curSrc)) {
        Log("AP FILLER LOC DEDUP: seed changed ('%s' -> '%s', src=%s) for char '%s' — wiping %d "
            "inherited locations\n", fileKey, curKey, curSrc, g_charName, g_appliedFillerLocCount);
        g_appliedFillerLocCount = 0;
        FILE* wf = fopen(path, "w");
        if (wf) { fprintf(wf, "seed_key=%s\nseed_src=%s\n", curKey, curSrc); fclose(wf); }
    }
    Log("AP FILLER LOC DEDUP: loaded %d locations from %s\n",
        g_appliedFillerLocCount, path);
}

static BOOL IsFillerLocApplied(int locId, DWORD senderHash) {
    if (locId <= 0) return FALSE;
    if (!g_appliedFillerLocsLoaded) LoadAppliedFillerLocs();
    for (int i = 0; i < g_appliedFillerLocCount; i++) {
        if (g_appliedFillerLocs[i] != locId) continue;
        /* Legacy entries (hash 0) match any sender — conservative: never dupe-grant across an upgrade. */
        if (g_appliedFillerSndr[i] == 0 || senderHash == 0 ||
            g_appliedFillerSndr[i] == senderHash)
            return TRUE;
    }
    return FALSE;
}

static void MarkFillerLocApplied(int locId, DWORD senderHash) {
    if (locId <= 0) return;
    if (!g_appliedFillerLocsLoaded) LoadAppliedFillerLocs();
    if (IsFillerLocApplied(locId, senderHash)) return;
    if (g_appliedFillerLocCount >= G_APPLIED_FILLER_LOC_CAP) {
        /* Buffer full — drop oldest half. */
        memmove(g_appliedFillerLocs, g_appliedFillerLocs + (G_APPLIED_FILLER_LOC_CAP / 2),
                (G_APPLIED_FILLER_LOC_CAP / 2) * sizeof(int));
        memmove(g_appliedFillerSndr, g_appliedFillerSndr + (G_APPLIED_FILLER_LOC_CAP / 2),
                (G_APPLIED_FILLER_LOC_CAP / 2) * sizeof(DWORD));
        g_appliedFillerLocCount = G_APPLIED_FILLER_LOC_CAP / 2;
    }
    g_appliedFillerLocs[g_appliedFillerLocCount]   = locId;
    g_appliedFillerSndr[g_appliedFillerLocCount++] = senderHash;

    if (!g_charName[0]) return;
    char path[MAX_PATH];
    GetAppliedFillerLocsPath(path, MAX_PATH);
    /* Stamp the seed key once per char/session (mirrors the applied-ids file). */
    {
        static char s_fillerStampedFor[64] = {0};
        if (strcmp(s_fillerStampedFor, g_charName) != 0) {
            char curKey[64], curSrc[16];
            GetCurrentSeedKeyAndSrc(curKey, sizeof(curKey), curSrc, sizeof(curSrc));
            if (curKey[0]) {
                FILE* sf = fopen(path, "a");
                if (sf) { fprintf(sf, "seed_key=%s\nseed_src=%s\n", curKey, curSrc); fclose(sf); }
                strncpy(s_fillerStampedFor, g_charName, sizeof(s_fillerStampedFor) - 1);
                s_fillerStampedFor[sizeof(s_fillerStampedFor) - 1] = 0;
            }
        }
    }
    FILE* f = fopen(path, "a");
    if (f) {
        fprintf(f, "loc=%d,sh=%x\n", locId, (unsigned)senderHash);
        fflush(f);
        fclose(f);
    }
}

/* public wrapper for d2arch_save.c OnCharacterLoad. */
void AP_PreloadDedupForCurrentChar(void) {
    /* Force reload (drop any stale state from previous character). */
    g_appliedApLoaded = FALSE;
    g_appliedApCount = 0;
    g_appliedFillerLocsLoaded = FALSE;
    g_appliedFillerLocCount = 0;
    LoadAppliedApIds();
    LoadAppliedFillerLocs();
    SUShuffle_Invalidate();   /* another character can be on another seed */

    /* and tell the launcher, so it rewinds the item stream and replays it for THIS character. */
    {
        static char s_announced[64] = {0};
        if (g_charName[0] && strcmp(s_announced, g_charName) != 0) {
            extern void ApPipe_SendChar(const char* name);
            ApPipe_SendChar(g_charName);
            strncpy(s_announced, g_charName, sizeof(s_announced) - 1);
            s_announced[sizeof(s_announced) - 1] = 0;
        }
    }
}

/* 1.9.5 Bug C1 fix — kill any orphan ap_bridge.exe processes left behind by a previous game session that didn't get to run DLL DLL_PROCESS_DETACH (Alt-F4, task-kill, crash). */
#include <tlhelp32.h>
static void KillOrphanBridges(void) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    int killed = 0;
    DWORD ourPid = g_bridgePI.hProcess ? g_bridgePI.dwProcessId : 0;
    DWORD selfPid = GetCurrentProcessId();
    if (Process32First(snap, &pe)) {
        do {
            /* Image name in szExeFile is filename only (no path) */
            if (_stricmp(pe.szExeFile, "ap_bridge.exe") != 0) continue;
            /* Skip ourselves' bridge (if we have one) */
            if (ourPid != 0 && pe.th32ProcessID == ourPid) continue;
            /* Defensive: don't kill our own game.exe (shouldn't match anyway) */
            if (pe.th32ProcessID == selfPid) continue;
            HANDLE hOrphan = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE,
                                          FALSE, pe.th32ProcessID);
            if (hOrphan) {
                Log("AP Bridge: killing orphan ap_bridge.exe PID %lu\n",
                    pe.th32ProcessID);
                TerminateProcess(hOrphan, 1);
                WaitForSingleObject(hOrphan, 500);
                CloseHandle(hOrphan);
                killed++;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    if (killed > 0) {
        Log("AP Bridge: killed %d orphan bridge(s) from previous sessions\n",
            killed);
    }
}

static void StartAPBridge(void) {
    /* V2 pipe transport active -- the launcher itself is the AP client. */
    if (ApPipe_Mode()) {
        Log("AP Bridge: spawn suppressed (V2 pipe transport active)\n");
        return;
    }
    if (g_bridgeStarted) {
        /* Check if still running */
        if (g_bridgePI.hProcess) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(g_bridgePI.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                Log("AP Bridge: process exited (code %d), restarting...\n", exitCode);
                CloseHandle(g_bridgePI.hProcess);
                CloseHandle(g_bridgePI.hThread);
                memset(&g_bridgePI, 0, sizeof(g_bridgePI));
                g_bridgeStarted = FALSE;
            } else {
                return; /* Still running */
            }
        }
    }

    /* 1.9.5 Bug C1 fix — sweep orphan bridges before spawning a new one. */
    KillOrphanBridges();

    /* Find ap_bridge.exe */
    char dir[MAX_PATH], bridgePath[MAX_PATH], bridgeCmd[MAX_PATH * 2];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char* sl = strrchr(dir, '\\');
    if (sl) *(sl + 1) = 0;

    /* Try ap_bridge_dist folder first */
    sprintf(bridgePath, "%sap_bridge_dist\\ap_bridge.exe", dir);
    if (GetFileAttributesA(bridgePath) == INVALID_FILE_ATTRIBUTES) {
        sprintf(bridgePath, "%sap_bridge.exe", dir);
        if (GetFileAttributesA(bridgePath) == INVALID_FILE_ATTRIBUTES) {
            Log("AP Bridge: not found (tried ap_bridge_dist/ and root)\n");
            return;
        }
    }

    sprintf(bridgeCmd, "\"%s\" --gamedir \".\"", bridgePath);
    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; /* Hidden — debug output goes to log file */

    if (CreateProcessA(bridgePath, bridgeCmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &g_bridgePI)) {
        g_bridgeStarted = TRUE;
        Log("AP Bridge: started PID %d (%s)\n", g_bridgePI.dwProcessId, bridgePath);
    } else {
        Log("AP Bridge: CreateProcess FAILED (err=%d) path=%s\n", GetLastError(), bridgePath);
    }
}

static void StopAPBridge(void) {
    if (g_bridgeStarted && g_bridgePI.hProcess) {
        /* 1.9.5 Bug C1 fix — wait for the bridge to actually exit before releasing the handle. */
        TerminateProcess(g_bridgePI.hProcess, 0);
        WaitForSingleObject(g_bridgePI.hProcess, 1000);
        CloseHandle(g_bridgePI.hProcess);
        CloseHandle(g_bridgePI.hThread);
        memset(&g_bridgePI, 0, sizeof(g_bridgePI));
        g_bridgeStarted = FALSE;
        Log("AP Bridge: stopped\n");
    }
}

/* Gap 1: ghost-disconnect detection. */
static void CheckBridgeHealth(void) {
    DWORD nowTicks = GetTickCount();
    if (nowTicks - g_apLastBridgeHealthCheck < 60000) return;
    g_apLastBridgeHealthCheck = nowTicks;

    /* Don't run the check until the user has actually pressed Connect. */
    if (!g_apPolling) return;
    if (!g_bridgeStarted) return;

    BOOL bridgeIsDead = FALSE;
    const char* deathReason = "unknown";

    /* --- Check (a): process exit code --- */
    if (g_bridgePI.hProcess) {
        DWORD exitCode = 0;
        BOOL gotExit = GetExitCodeProcess(g_bridgePI.hProcess, &exitCode);
        if (gotExit && exitCode != STILL_ACTIVE) {
            bridgeIsDead = TRUE;
            deathReason  = "process exited";
            Log("BRIDGE HEALTH: process exit code %lu — bridge dead\n",
                exitCode);
        }
    }

    /* --- Check (b): heartbeat staleness (only if process appears alive) --- */
    if (!bridgeIsDead) {
        char statusPath[MAX_PATH];
        GetArchDir(statusPath, MAX_PATH);
        strcat(statusPath, "ap_status.dat");
        FILE* f = fopen(statusPath, "r");
        if (f) {
            char line[256];
            unsigned long heartbeatTs = 0;
            while (fgets(line, sizeof(line), f)) {
                if (sscanf(line, "heartbeat=%lu", &heartbeatTs) == 1) {
                    break;   /* found it */
                }
            }
            fclose(f);

            if (heartbeatTs > 0) {
                time_t nowUnix = time(NULL);
                if (nowUnix > 0 && (unsigned long)nowUnix > heartbeatTs) {
                    unsigned long age = (unsigned long)nowUnix - heartbeatTs;
                    /* 120s threshold: bridge writes heartbeat every 30s, so 120s = 4 consecutive missed writes. */
                    if (age > 120) {
                        bridgeIsDead = TRUE;
                        deathReason  = "heartbeat stale";
                        Log("BRIDGE HEALTH: heartbeat is %lu seconds old "
                            "(>120s threshold) — bridge stuck\n", age);
                    }
                }
            }
        }
    }

    if (!bridgeIsDead) return;

    /* --- Bridge is dead: notify, respawn, reconnect --- */
    g_apBridgeRespawnCount++;
    char msg[160];
    _snprintf(msg, sizeof(msg),
              "AP bridge died (%s) — restarting [respawn #%d]",
              deathReason, g_apBridgeRespawnCount);
    msg[sizeof(msg) - 1] = 0;
    ShowNotify(msg);
    Log("BRIDGE HEALTH: %s\n", msg);

    /* Clean up old process handles. */
    if (g_bridgePI.hProcess) {
        DWORD exitCode = 0;
        BOOL stillAlive = GetExitCodeProcess(g_bridgePI.hProcess, &exitCode)
                          && exitCode == STILL_ACTIVE;
        if (stillAlive) {
            Log("BRIDGE HEALTH: terminating hung bridge PID %lu\n",
                g_bridgePI.dwProcessId);
            TerminateProcess(g_bridgePI.hProcess, 1);
            /* Give Windows ~500ms to actually tear it down so the next StartAPBridge doesn't race against still-open file handles. */
            WaitForSingleObject(g_bridgePI.hProcess, 500);
        }
        CloseHandle(g_bridgePI.hProcess);
        if (g_bridgePI.hThread) CloseHandle(g_bridgePI.hThread);
        memset(&g_bridgePI, 0, sizeof(g_bridgePI));
    }
    g_bridgeStarted = FALSE;
    g_apConnected   = FALSE;
    strcpy(g_apStatus, "Bridge restarting...");
    g_apHadDisconnect = TRUE;  /* so the upcoming reconnect fires the green notify */

    /* 1.9.5 Gap 9 fix — also run the slot-change wipe protection on the health-respawn path. */
    CheckSlotChangeOnConnect();

    /* Respawn + reconnect */
    StartAPBridge();
    if (g_bridgeStarted) {
        WriteAPCommand("connect");
    } else {
        Log("BRIDGE HEALTH: respawn FAILED — manual Connect required\n");
        ShowNotify("AP bridge respawn FAILED — click Connect manually");
    }
}

/* Write command file for AP bridge (DLL -> Bridge) */
static void WriteAPCommand(const char* action) {
    /* V2 pipe transport active -- there is no bridge process reading ap_command.dat; connect/disconnect is owned by the launcher. */
    if (ApPipe_Mode()) {
        Log("AP: command '%s' suppressed (V2 pipe transport active)\n", action);
        return;
    }
    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(path, "%sap_command.dat", dir);
    FILE* f = fopen(path, "w");
    if (!f) { Log("AP: Failed to write command file\n"); return; }
    fprintf(f, "action=%s\n", action);
    fprintf(f, "server=%s\n", g_apIP);
    fprintf(f, "slot=%s\n", g_apSlot);
    fprintf(f, "password=%s\n", g_apPassword);
    fprintf(f, "char=%s\n", g_charName);
    fprintf(f, "deathlink=%d\n", g_apDeathLink ? 1 : 0);
    fclose(f);
    Log("AP: Command '%s' written (server=%s slot=%s)\n", action, g_apIP, g_apSlot);
}

/* Save AP settings to d2arch.ini for persistence */
static void SaveAPConfig(void) {
    char iniPath[MAX_PATH];
    GetArchDir(iniPath, MAX_PATH);
    strcat(iniPath, "d2arch.ini");
    WritePrivateProfileStringA("ap", "ServerIP", g_apIP, iniPath);
    WritePrivateProfileStringA("ap", "SlotName", g_apSlot, iniPath);
    WritePrivateProfileStringA("ap", "Password", g_apPassword, iniPath);
    Log("AP: Config saved to d2arch.ini\n");
}

/* PER-CHARACTER AP CONFIG — save/load AP connection info per char */
static void SaveAPCharConfig(void) {
    char dir[MAX_PATH], path[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_ap_%s.dat", dir, g_charName);
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "ap_mode=%d\n", g_apConnected ? 1 : 0);
    fprintf(f, "ap_server=%s\n", g_apIP);
    fprintf(f, "ap_slot=%s\n", g_apSlot);
    fprintf(f, "ap_password=%s\n", g_apPassword);
    /* 1.9.5 Gap 4 — fingerprint the (server,slot) pair so a future Connect against a DIFFERENT pair can wipe dedup files before the new server's items collide with stale applied-id markers. */
    fprintf(f, "server_slot_hash=%u\n", APCurrentServerSlotHash());
    fclose(f);
    Log("AP: Saved per-char config for '%s'\n", g_charName);
}

static BOOL LoadAPCharConfig(void) {
    char dir[MAX_PATH], path[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);
    sprintf(path, "%sd2arch_ap_%s.dat", dir, g_charName);
    FILE* f = fopen(path, "r");
    if (!f) return FALSE;
    char line[256];
    BOOL apMode = FALSE;
    g_apStoredServerSlotHash = 0;   /* reset before load */
    while (fgets(line, sizeof(line), f)) {
        char val[64];
        int n = 0;
        unsigned int u = 0;
        if (sscanf(line, "ap_mode=%d", &n) == 1) apMode = (n != 0);
        if (sscanf(line, "ap_server=%63[^\n]", val) == 1) strncpy(g_apIP, val, 63);
        if (sscanf(line, "ap_slot=%31[^\n]", val) == 1) strncpy(g_apSlot, val, 31);
        if (sscanf(line, "ap_password=%31[^\n]", val) == 1) strncpy(g_apPassword, val, 31);
        /* 1.9.5 Gap 4 — load stored hash if present (older configs may not have it, in which case stays 0 and slot-change detection is effectively disabled for that character until next Save). */
        if (sscanf(line, "server_slot_hash=%u", &u) == 1) g_apStoredServerSlotHash = u;
    }
    fclose(f);
    Log("AP: Loaded per-char config for '%s' (mode=%d hash=%u)\n",
        g_charName, apMode, g_apStoredServerSlotHash);
    return apMode;
}

/* 1.9.5 Gap 4 — call this RIGHT BEFORE issuing a Connect command. */
static void CheckSlotChangeOnConnect(void) {
    if (!g_charName[0]) return;
    if (g_apStoredServerSlotHash == 0) return;   /* first connect for this char */
    if (!g_apIP[0] || !g_apSlot[0]) return;

    unsigned int curHash = APCurrentServerSlotHash();
    if (curHash == g_apStoredServerSlotHash) return;   /* same server, no action */

    Log("AP SLOT-CHANGE DETECTED: stored hash=%u current=%u "
        "(server '%s' or slot '%s' changed) — wiping dedup files\n",
        g_apStoredServerSlotHash, curHash, g_apIP, g_apSlot);
    ShowNotify("AP server/slot changed — clearing old item history");

    /* Wipe DLL applied AP IDs */
    char appliedPath[MAX_PATH];
    GetAppliedApFilePath(appliedPath, MAX_PATH);
    if (DeleteFileA(appliedPath)) {
        Log("AP SLOT-CHANGE: deleted %s\n", appliedPath);
    }
    g_appliedApCount = 0;
    g_appliedApLoaded = FALSE;

    /* also wipe the per-location filler dedup added in 1.9.11 (B22-B24 fix). */
    {
        char fillerLocPath[MAX_PATH];
        GetAppliedFillerLocsPath(fillerLocPath, MAX_PATH);
        if (DeleteFileA(fillerLocPath)) {
            Log("AP SLOT-CHANGE: deleted %s\n", fillerLocPath);
        }
        g_appliedFillerLocCount  = 0;
        g_appliedFillerLocsLoaded = FALSE;
    }

    /* Wipe bridge dedup file + spoiler + checklist (all per-char in Save/) */
    char dir[MAX_PATH];
    GetCharFileDir(dir, MAX_PATH);

    char bridgeLocPath[MAX_PATH];
    _snprintf(bridgeLocPath, MAX_PATH, "%sd2arch_bridge_locations_%s.dat",
              dir, g_charName);
    bridgeLocPath[MAX_PATH - 1] = 0;
    if (DeleteFileA(bridgeLocPath)) {
        Log("AP SLOT-CHANGE: deleted %s\n", bridgeLocPath);
    }

    char spoilerPath[MAX_PATH];
    _snprintf(spoilerPath, MAX_PATH, "%sd2arch_ap_spoiler_%s.txt",
              dir, g_charName);
    spoilerPath[MAX_PATH - 1] = 0;
    DeleteFileA(spoilerPath);

    char checklistPath[MAX_PATH];
    _snprintf(checklistPath, MAX_PATH, "%sd2arch_ap_checklist_%s.txt",
              dir, g_charName);
    checklistPath[MAX_PATH - 1] = 0;
    DeleteFileA(checklistPath);

    /* NOTE on state + checks files (NOT wiped here): Alphena's 1.9.5 report ("Cannot connect to previous AP game") and the post-1.9.10 audit flagged that slot-change wipes 4 files but keeps d2arch_state_<char>.dat and d2arch_checks_<char>.dat. */

    /* Update stored hash so we don't trigger again this session */
    g_apStoredServerSlotHash = curHash;
}

/* CHARACTER ISOLATION — hide other .d2s files during AP session */
static void IsolateAPCharacter(void) {
    char saveDir[MAX_PATH];
    GetSaveDir(saveDir, MAX_PATH);
    if (!saveDir[0]) { Log("IsolateAPCharacter: no save dir\n"); return; }

    /* Create hidden subfolder */
    char hiddenDir[MAX_PATH];
    sprintf(hiddenDir, "%s_arch_hidden\\", saveDir);
    CreateDirectoryA(hiddenDir, NULL);

    /* Move all .d2s files except current character */
    char searchPath[MAX_PATH];
    sprintf(searchPath, "%s*.d2s", saveDir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    int moved = 0;
    do {
        /* Extract character name from filename (strip .d2s) */
        char baseName[MAX_PATH];
        strncpy(baseName, fd.cFileName, MAX_PATH - 1);
        char* dot = strrchr(baseName, '.');
        if (dot) *dot = 0;

        /* Skip current character */
        if (_stricmp(baseName, g_charName) == 0) continue;

        /* Move file to hidden dir */
        char srcPath[MAX_PATH], dstPath[MAX_PATH];
        sprintf(srcPath, "%s%s", saveDir, fd.cFileName);
        sprintf(dstPath, "%s%s", hiddenDir, fd.cFileName);
        if (MoveFileA(srcPath, dstPath)) moved++;

        /* Also move companion files (.key, .ma0, .map) */
        const char* exts[] = {".key", ".ma0", ".map"};
        for (int e = 0; e < 3; e++) {
            sprintf(srcPath, "%s%s%s", saveDir, baseName, exts[e]);
            sprintf(dstPath, "%s%s%s", hiddenDir, baseName, exts[e]);
            MoveFileA(srcPath, dstPath); /* OK if fails (file may not exist) */
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    Log("IsolateAPCharacter: moved %d characters to _arch_hidden\n", moved);
}

static void RestoreAllCharacters(void) {
    char saveDir[MAX_PATH];
    GetSaveDir(saveDir, MAX_PATH);
    if (!saveDir[0]) return;

    char hiddenDir[MAX_PATH];
    sprintf(hiddenDir, "%s_arch_hidden\\", saveDir);

    /* Move everything back from hidden dir */
    char searchPath[MAX_PATH];
    sprintf(searchPath, "%s*.*", hiddenDir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    int restored = 0;
    do {
        if (fd.cFileName[0] == '.') continue; /* skip . and .. */
        char srcPath[MAX_PATH], dstPath[MAX_PATH];
        sprintf(srcPath, "%s%s", hiddenDir, fd.cFileName);
        sprintf(dstPath, "%s%s", saveDir, fd.cFileName);
        if (MoveFileA(srcPath, dstPath)) restored++;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);

    /* Try to remove the hidden directory (only succeeds if empty) */
    RemoveDirectoryA(hiddenDir);
    Log("RestoreAllCharacters: restored %d files\n", restored);
}

/* AP SETTINGS — read slot_data from bridge (ap_settings.dat) (globals defined at top of file) */

/* Sync AP-managed settings (g_*) into the title-screen UI state (ts_*) and immediately re-render every toggle/dropdown button so the player sees the AP server's actual values instead of their stale local d2arch.ini values. */
static void SyncAPToTitleSettings(void) {
    /* Copy g_* -> ts_* so the buttons render the authoritative AP values. */
    if (g_apGoal == 3) {
        ts_Goal = 15;                    /* display index 15 */
    } else if (g_apGoal >= 0 && g_apGoal <= 2) {
        ts_Goal = (g_apGoal * 5) + 4;   /* 0->4, 1->9, 2->14 */
    } else if (g_apGoal >= 0 && g_apGoal < GOAL_COUNT) {
        ts_Goal = g_apGoal;
    } else {
        ts_Goal = 14;                    /* fallback: Full Hell */
    }
    ts_SkillHuntingOn        = g_skillHuntingOn ? 1 : 0;
    ts_ZoneLockingOn         = g_zoneLockingOn  ? 1 : 0;
    ts_QuestStory            = g_apQuestStory   ? 1 : 0;
    ts_QuestHunting          = g_apQuestHunting ? 1 : 0;
    ts_QuestKillZones        = g_apQuestKillZones ? 1 : 0;
    ts_QuestExploration      = g_apQuestExploration ? 1 : 0;
    ts_QuestWaypoints        = g_apQuestWaypoints ? 1 : 0;
    ts_QuestLevelMilestones  = g_apQuestLevelMilestones ? 1 : 0;
    ts_MonsterShuffle        = g_monsterShuffleEnabled ? 1 : 0;
    ts_BossShuffle           = g_bossShuffleEnabled ? 1 : 0;
    ts_ShopShuffle           = g_shopShuffleEnabled ? 1 : 0;
    ts_EntranceShuffle       = g_entranceShuffleEnabled ? 1 : 0; /* 1.9.0 */
    /* TrapsEnabled is encoded indirectly: LoadAPSettings zeroes g_fillerTrapPct when slot_data has traps_enabled=0. */
    ts_TrapsEnabled          = (g_fillerTrapPct > 0) ? 1 : 0;
    ts_ClassFilter           = g_classFilter;
    for (int i = 0; i < 7; i++) ts_ClsEnabled[i] = g_clsEnabled[i];
    /* XP multiplier: g_* stores the real 1..100 value; ts_*_idx is the nearest curated dropdown slot for display (xpValues[] in d2arch_api.c). */
    ts_XPMultiplier_idx = XpIdxFromValue(g_xpMultiplier);
    ts_XPMultiplier     = g_xpMultiplier;

    /* Re-render all toggle buttons via SetBtnColor + ButtonSetText. */
    for (int i = 0; i < g_titleBtnCount; i++) {
        void* btn = g_titleBtns[i];
        int*  val = g_titleBtnVals[i];
        const wchar_t* lbl = g_titleBtnLabels[i];
        if (!btn || !val || !lbl) continue;
        if (fnButtonSetText) {
            wchar_t txt[64];
            _snwprintf(txt, 63, L"%s:%s", lbl, *val ? L"ON" : L"OFF");
            fnButtonSetText(btn, 1, txt, *val ? 1 : 3);
        }
        SetBtnColor(btn, *val);
    }

    /* Re-render the two dropdowns (Goal + XP). */
    if (g_btnGoal && fnButtonSetText && ts_Goal >= 0 && ts_Goal < GOAL_COUNT)
        fnButtonSetText(g_btnGoal, 1, goalOptions[ts_Goal], 0);
    if (g_btnXP && fnButtonSetText && ts_XPMultiplier_idx >= 0 && ts_XPMultiplier_idx < XP_COUNT)
        fnButtonSetText(g_btnXP, 1, xpOptions[ts_XPMultiplier_idx], 0);

    /* Class filter button affects sibling class buttons' tint; refresh. */
    UpdateClassColors();

    Log("AP: synced settings to title-screen buttons "
        "(Goal=%d XP=%dx skillHunt=%d zoneLock=%d traps=%d monShuf=%d bossShuf=%d)\n",
        ts_Goal, ts_XPMultiplier, ts_SkillHuntingOn, ts_ZoneLockingOn,
        ts_TrapsEnabled, ts_MonsterShuffle, ts_BossShuffle);
}

/* Reverse of SyncAPToTitleSettings: when AP disconnects, reload the d2arch.ini values into ts_* and re-render so the buttons go back to displaying the user's offline preferences. */
static void RestoreTitleSettingsFromINI(void) {
    /* Calling TitleSettings_Load reloads the entire ts_* set from d2arch.ini. */
    TitleSettings_Load();
    for (int i = 0; i < g_titleBtnCount; i++) {
        void* btn = g_titleBtns[i];
        int*  val = g_titleBtnVals[i];
        const wchar_t* lbl = g_titleBtnLabels[i];
        if (!btn || !val || !lbl) continue;
        if (fnButtonSetText) {
            wchar_t txt[64];
            _snwprintf(txt, 63, L"%s:%s", lbl, *val ? L"ON" : L"OFF");
            fnButtonSetText(btn, 1, txt, *val ? 1 : 3);
        }
        SetBtnColor(btn, *val);
    }
    if (g_btnGoal && fnButtonSetText && ts_Goal >= 0 && ts_Goal < GOAL_COUNT)
        fnButtonSetText(g_btnGoal, 1, goalOptions[ts_Goal], 0);
    if (g_btnXP && fnButtonSetText && ts_XPMultiplier_idx >= 0 && ts_XPMultiplier_idx < XP_COUNT)
        fnButtonSetText(g_btnXP, 1, xpOptions[ts_XPMultiplier_idx], 0);
    UpdateClassColors();
    Log("AP: restored title-screen buttons from d2arch.ini after disconnect\n");
}

/* Compare what the server just told us against what this character has frozen,
   and log every disagreement.

   The freeze is intentional — a run must not change shape underneath the
   player — but it also means a value baked in wrongly at creation stays wrong
   forever, and until now it did so in complete silence. This changes nothing
   about the settings themselves; it only makes a mismatch findable.

   It works on raw key=value text rather than a list of known settings, so a
   setting added next year is covered without anyone remembering this function
   exists. Keys that legitimately differ are skipped by name. */
static void ReportFrozenSettingDrift(void) {
    if (!g_charName[0]) return;

    char apPath[MAX_PATH], stPath[MAX_PATH], dir[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(apPath, "%sap_settings.dat", dir);
    GetCharFileDir(dir, MAX_PATH);
    sprintf(stPath, "%sd2arch_state_%s.dat", dir, g_charName);

    /* `seed` means two different things on the two sides: the room's seed on
       the server, this character's data seed here. Not a disagreement. */
    static const char* const kIgnored[] = { "seed", NULL };

    FILE* ap = fopen(apPath, "r");
    if (!ap) return;

    char line[256];
    int reported = 0;
    while (fgets(line, sizeof(line), ap) && reported < 12) {
        char key[64], apVal[128];
        char* eq = strchr(line, '=');
        if (!eq) continue;
        {
            size_t klen = (size_t)(eq - line);
            if (klen == 0 || klen >= sizeof(key)) continue;
            memcpy(key, line, klen);
            key[klen] = 0;
        }
        lstrcpynA(apVal, eq + 1, sizeof(apVal));
        {
            char* nl = strpbrk(apVal, "\r\n");
            if (nl) *nl = 0;
        }
        {
            int skip = 0, i;
            for (i = 0; kIgnored[i]; i++)
                if (strcmp(key, kIgnored[i]) == 0) { skip = 1; break; }
            if (skip) continue;
        }

        /* Find the same key in the character's frozen state. */
        {
            FILE* st = fopen(stPath, "r");
            char sline[256];
            if (!st) break;
            while (fgets(sline, sizeof(sline), st)) {
                char* seq = strchr(sline, '=');
                if (!seq) continue;
                if ((size_t)(seq - sline) != strlen(key)) continue;
                if (strncmp(sline, key, strlen(key)) != 0) continue;
                {
                    char stVal[128];
                    lstrcpynA(stVal, seq + 1, sizeof(stVal));
                    {
                        char* nl = strpbrk(stVal, "\r\n");
                        if (nl) *nl = 0;
                    }
                    if (strcmp(stVal, apVal) != 0) {
                        Log("AP FROZEN MISMATCH: %s server=%s character=%s "
                            "(frozen at creation, not applied)\n",
                            key, apVal, stVal);
                        reported++;
                    }
                }
                break;
            }
            fclose(st);
        }
    }
    fclose(ap);
    if (reported)
        Log("AP FROZEN MISMATCH: %d setting(s) differ from the server\n",
            reported);
}

/* The player-side settings, read again from slot_data for a character that
 * already exists.
 *
 * BetaHub #22, 4 September: "XP multiplier lost upon saving and relaunching".
 * Three things conspired. OnCharacterLoad zeroes g_xpMultiplier with the rest
 * of the frozen set; LoadAPSettings runs for NEW characters only; and the
 * state file's copy is skipped under AP by design (the YAML is the contract,
 * the file only a cache — see the note in LoadStateFile). So an existing
 * character came back at 0x every launch, and the log said so in plain
 * sight: "settings frozen (... xp=0x ...)" against a slot that asked for 5x.
 * The connect-time refresh already corrected the skill cap and the level
 * requirements for the same reason; the multiplier belongs to that set — it
 * is the player's own rate, not the shape of the world — and this is that
 * refresh, callable from character load as well. */
void AP_ReapplyPlayerSideSettings(const char* why) {
    extern int  g_skillMaxLevel;
    extern BOOL g_skillLevelReqs;
    extern BOOL g_settingsFrozen;
    extern void SaveStateFile(void);

    if (!g_apConnected) return;

    char path[MAX_PATH];
    GetArchDir(path, MAX_PATH);
    strcat(path, "ap_settings.dat");
    FILE* sf = fopen(path, "r");
    if (!sf) return;

    int slotXp = -1, slotCap = -1, slotReqs = -1;
    char line[256];
    while (fgets(line, sizeof(line), sf)) {
        int v;
        if (sscanf(line, "xp_multiplier=%d", &v) == 1 && v >= 0 && v <= 100) slotXp = v;
        if (sscanf(line, "skill_max_level=%d", &v) == 1 && v >= 1 && v <= 99) slotCap = v;
        if (sscanf(line, "skill_level_reqs=%d", &v) == 1) slotReqs = (v != 0) ? 1 : 0;
    }
    fclose(sf);

    BOOL changed = FALSE;
    if (slotXp >= 0 && slotXp != g_xpMultiplier) {
        Log("AP LIVE SETTING (%s): xp multiplier %dx -> %dx (from slot_data)\n",
            why, g_xpMultiplier, slotXp);
        g_xpMultiplier = slotXp;
        changed = TRUE;
    }
    if (slotCap > 0 && slotCap != g_skillMaxLevel) {
        Log("AP LIVE SETTING (%s): skill cap %d -> %d (from slot_data)\n",
            why, g_skillMaxLevel, slotCap);
        g_skillMaxLevel = slotCap;
        changed = TRUE;
    }
    if (slotReqs >= 0 && (BOOL)slotReqs != g_skillLevelReqs) {
        Log("AP LIVE SETTING (%s): skill level reqs %d -> %d (from slot_data)\n",
            why, (int)g_skillLevelReqs, slotReqs);
        g_skillLevelReqs = (slotReqs != 0);
        changed = TRUE;
    }
    if (changed && g_charName[0]) {
        BOOL wf = g_settingsFrozen;
        g_settingsFrozen = FALSE;
        SaveStateFile();
        g_settingsFrozen = wf;
    }
}

static void LoadAPSettings(void) {
    /* settings are baked into the per-character state file at creation time. */
    if (g_settingsFrozen) {
        Log("LoadAPSettings: settings frozen for character '%s' — skipping\n",
            g_charName);
        return;
    }

    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);

    /* Only read ap_settings.dat if AP is currently connected. */
    FILE* f = NULL;
    if (g_apConnected) {
        sprintf(path, "%sap_settings.dat", dir);
        f = fopen(path, "r");
        if (!f) {
            sprintf(path, "%sstandalone_settings.dat", dir);
            f = fopen(path, "r");
        }
    } else {
        Log("LoadAPSettings: AP disconnected — using d2arch.ini [settings] as source\n");
    }
    /* LauncherDataShuffle is a launcher-control flag written to d2arch.ini [settings] in BOTH AP and standalone launches. */
    {
        char ldsIni[MAX_PATH];
        GetArchDir(ldsIni, MAX_PATH); strcat(ldsIni, "d2arch.ini");
        g_launcherDataShuffle = GetPrivateProfileIntA("settings", "LauncherDataShuffle", 0, ldsIni) != 0;
    }
    /* 2.x — Monster Revive Trap settings. */
    {
        char mtIni[MAX_PATH];
        GetArchDir(mtIni, MAX_PATH); strcat(mtIni, "d2arch.ini");
        extern int g_monsterTrap, g_monsterTrapChance, g_monsterTrapQuota;
        extern int g_monsterTrapCount, g_monsterTrapDelayMs, g_monTrapRemaining;
        g_monsterTrap        = GetPrivateProfileIntA("settings",    "MonsterTrap", 0, mtIni);
        g_monsterTrapChance  = GetPrivateProfileIntA("MonsterTrap", "ChancePct",   3, mtIni);
        g_monsterTrapQuota   = GetPrivateProfileIntA("MonsterTrap", "Quota",      15, mtIni);
        g_monsterTrapCount   = GetPrivateProfileIntA("MonsterTrap", "Count",       8, mtIni);
        g_monsterTrapDelayMs = GetPrivateProfileIntA("MonsterTrap", "DelayMs",  1000, mtIni);
        if (g_monsterTrapChance  < 0)   g_monsterTrapChance  = 0;
        if (g_monsterTrapChance  > 100) g_monsterTrapChance  = 100;
        if (g_monsterTrapCount   < 1)   g_monsterTrapCount   = 1;
        if (g_monsterTrapCount   > 16)  g_monsterTrapCount   = 16;
        if (g_monsterTrapDelayMs < 0)   g_monsterTrapDelayMs = 0;
        if (g_monsterTrapQuota   < 0)   g_monsterTrapQuota   = 0;
        g_monTrapRemaining = g_monsterTrapQuota;
    }
    if (!f) {
        /* Read from d2arch.ini [settings] section (written by D2Launch main menu) */
        char iniPath[MAX_PATH]; int v;
        GetArchDir(iniPath, MAX_PATH); strcat(iniPath, "d2arch.ini");
        Log("Loading settings from d2arch.ini [settings]\n");
        /* normalize Goal: 1.8.x uses 0..2 (Full Norm/NM/Hell); older INIs may still carry the legacy 0..14 (act × 3 + diff) encoding which makes gate logic and goal-completion comparisons misfire. */
        {
            /* goal range now 0..3. 3 = Collection (F1 page tracker win condition). Legacy 0..14 encoding still normalizes via %3 so old INIs map onto Normal/NM/Hell. Title-screen UI saves Goal=15 for Collection, which maps to internal 3. */
            int rawGoal = GetPrivateProfileIntA("settings", "Goal", 2, iniPath);
            if (rawGoal == 15)            g_apGoal = 3;   /* Title UI Collection */
            /* accept 0..4: 0/1/2 full-game per diff, 3 Collection, 4 Custom (was capped at 3, so the launcher's Custom goal mapped to 4%3=1=Nightmare and silently broke). */
            else if (rawGoal >= 0 && rawGoal <= 4) g_apGoal = rawGoal;
            else                           g_apGoal = (rawGoal % 3);
            if (g_apGoal < 0) g_apGoal = 0;
            if (g_apGoal > 4) g_apGoal = 4;

            /* Collection: standalone INI fallback. */
            if (g_apGoal == 3) {
                extern uint32_t g_collGoalOverrideSetsMask;
                extern uint64_t g_collGoalOverrideRunesMask;
                extern uint16_t g_collGoalOverrideSpecialsMask;
                extern uint8_t  g_collGoalOverrideGems;
                extern BOOL     g_collGoalOverridePresent;
                /* Default: keep masks at "all on". */
                int setsHi = GetPrivateProfileIntA("settings", "CollSetsMaskHi", -1, iniPath);
                int setsLo = GetPrivateProfileIntA("settings", "CollSetsMaskLo", -1, iniPath);
                if (setsLo >= 0 && setsHi >= 0) {
                    g_collGoalOverrideSetsMask =
                        (uint32_t)setsLo | ((uint32_t)setsHi << 16);
                    g_collGoalOverridePresent = TRUE;
                }
                int runesLo = GetPrivateProfileIntA("settings", "CollRunesMaskLo", -1, iniPath);
                int runesMd = GetPrivateProfileIntA("settings", "CollRunesMaskMd", -1, iniPath);
                int runesHi = GetPrivateProfileIntA("settings", "CollRunesMaskHi", -1, iniPath);
                if (runesLo >= 0 && runesMd >= 0 && runesHi >= 0) {
                    g_collGoalOverrideRunesMask =
                        (uint64_t)runesLo |
                        ((uint64_t)runesMd << 16) |
                        ((uint64_t)runesHi << 32);
                    g_collGoalOverridePresent = TRUE;
                }
                int specs = GetPrivateProfileIntA("settings", "CollSpecialsMask", -1, iniPath);
                if (specs >= 0) {
                    g_collGoalOverrideSpecialsMask = (uint16_t)specs;
                    g_collGoalOverridePresent = TRUE;
                }
                int gems = GetPrivateProfileIntA("settings", "CollGoalGems", -1, iniPath);
                if (gems >= 0) {
                    g_collGoalOverrideGems = (uint8_t)(gems != 0);
                    g_collGoalOverridePresent = TRUE;
                }
            }
        }
        /* Custom goal targets from d2arch.ini [settings] (the launcher writes CustomGoalTargets=<csv of tokens> + CustomGoalGold=<n> when Goal=Custom). */
        if (g_apGoal == 4) {
            char cgCsv[1024] = "";
            GetPrivateProfileStringA("settings", "CustomGoalTargets", "",
                                     cgCsv, sizeof(cgCsv), iniPath);
            uint64_t cgGold = (uint64_t)(unsigned int)
                GetPrivateProfileIntA("settings", "CustomGoalGold", 0, iniPath);
            extern void CustomGoal_ParseTargetsCSV(const char* csv, uint64_t goldTarget);
            CustomGoal_ParseTargetsCSV(cgCsv, cgGold);
            /* 2.x HOTFIX (Maegis) — an empty custom goal didn't activate; fall back to Full Normal so the game is winnable instead of stuck on goal=4 with no condition (and no false "complete" on entry). */
            {
                extern BOOL CustomGoal_IsActive(void);
                if (g_apGoal == 4 && !CustomGoal_IsActive()) {
                    Log("CGT: custom goal empty (no targets, no gold) -> falling back to Full Normal (goal=0)\n");
                    g_apGoal = 0;
                }
            }
        }
        /* two independent game-mode toggles replace the old enum. */
        {
            int legacyMode = GetPrivateProfileIntA("settings", "GameMode", -1, iniPath);
            g_skillHuntingOn = GetPrivateProfileIntA("settings", "SkillHunting",
                                                    (legacyMode == 1) ? 0 : 1,
                                                    iniPath) != 0;
            g_zoneLockingOn  = GetPrivateProfileIntA("settings", "ZoneLocking",
                                                    (legacyMode == 1) ? 1 : 0,
                                                    iniPath) != 0;
        }
        /* story quests always ON (toggle removed — D2 requires them anyway) */
        g_apQuestStory = TRUE;
        g_apQuestHunting = GetPrivateProfileIntA("settings", "QuestHunting", 1, iniPath) != 0;
        g_apQuestKillZones = GetPrivateProfileIntA("settings", "QuestKillZones", 1, iniPath) != 0;
        g_apQuestExploration = GetPrivateProfileIntA("settings", "QuestExploration", 1, iniPath) != 0;
        g_apQuestWaypoints = GetPrivateProfileIntA("settings", "QuestWaypoints", 1, iniPath) != 0;
        g_apQuestLevelMilestones = GetPrivateProfileIntA("settings", "QuestLevelMilestones", 1, iniPath) != 0;
        g_apSkillPoolSize = GetPrivateProfileIntA("settings", "SkillPoolSize", 210, iniPath);
        g_apStartingSkills = GetPrivateProfileIntA("settings", "StartingSkills", 6, iniPath);
        g_fillerTrapPct = GetPrivateProfileIntA("settings", "TrapPct", 15, iniPath);
        /* TrapsEnabled toggle. When 0, force trap filler weight to 0 so the weighted-roll redistributes proportionally to gold/stat/ skill/reset/loot — pool stays balanced, no traps generated. */
        if (GetPrivateProfileIntA("settings", "TrapsEnabled", 1, iniPath) == 0) {
            g_fillerTrapPct = 0;
        }
        g_tieredFillerDepth = GetPrivateProfileIntA("settings", "TieredFillerDepth",
                                                    1, iniPath) != 0;
        g_fillerGoldPct = GetPrivateProfileIntA("settings", "GoldPct", 30, iniPath);
        g_fillerStatPct = GetPrivateProfileIntA("settings", "StatPtsPct", 15, iniPath);
        g_fillerSkillPct = GetPrivateProfileIntA("settings", "SkillPtsPct", 15, iniPath);
        g_fillerResetPct = GetPrivateProfileIntA("settings", "ResetPtsPct", 25, iniPath);
        g_fillerLootPct = GetPrivateProfileIntA("settings", "LootPct", 18, iniPath);
        g_monsterShuffleEnabled = GetPrivateProfileIntA("settings", "MonsterShuffle", 0, iniPath) != 0;
        /* boss shuffle split in two: the launcher does the SUPER-UNIQUE shuffle via SuperUniques.txt, while the DLL owns the ACT-BOSS shuffle (cosmetic swap of Andariel/Duriel/Mephisto/Diablo/Baal — the only thing txt can't do). */
        g_bossShuffleEnabled = GetPrivateProfileIntA("settings", "ActBossShuffle",
            GetPrivateProfileIntA("settings", "BossShuffle", 0, iniPath), iniPath) != 0;
        g_shopShuffleEnabled = GetPrivateProfileIntA("settings", "ShopShuffle", 0, iniPath) != 0;
        g_entranceShuffleEnabled = GetPrivateProfileIntA("settings", "EntranceShuffle", 0, iniPath) != 0;
        /* launcher already randomized monsters/bosses via the seed-bound data files; skip the DLL's own runtime shuffle so it doesn't double up. */
        g_launcherDataShuffle = GetPrivateProfileIntA("settings", "LauncherDataShuffle", 0, iniPath) != 0;
        /* SkillLevelReqs toggle (Maegis #2). */
        {
            extern BOOL g_skillLevelReqs;
            g_skillLevelReqs = GetPrivateProfileIntA("settings", "SkillLevelReqs", 1, iniPath) != 0;
        }
        /* Skill point cap. Default 20 = vanilla. Clamped here so a hand-edited ini can never hand the click handler a nonsense cap. */
        {
            extern int g_skillMaxLevel;
            int v = GetPrivateProfileIntA("settings", "SkillMaxLevel", 20, iniPath);
            if (v < 1)  v = 1;
            if (v > 99) v = 99;
            g_skillMaxLevel = v;
        }
        /* ItemLevelReqs toggle (Maegis #2 second half). */
        {
            extern BOOL g_itemLevelReqs, g_itemStatsReqs;
            g_itemLevelReqs = GetPrivateProfileIntA("settings", "ItemLevelReqs", 1, iniPath) != 0;
            g_itemStatsReqs = GetPrivateProfileIntA("settings", "ItemStatsReqs", 1, iniPath) != 0;
        }
        /* 1.8.0 cleanup: TreasureCows INI parse removed — pending reimplementation */
        g_xpMultiplier = GetPrivateProfileIntA("settings", "XPMultiplier", 0, iniPath);
        if (g_xpMultiplier < 0) g_xpMultiplier = 0;
        if (g_xpMultiplier > 100) g_xpMultiplier = 100;
        /* Standalone: object checks random (1) or guaranteed (0, default). */
        {
            extern void Bonus_SetRandomChance(BOOL on);
            Bonus_SetRandomChance(GetPrivateProfileIntA("settings", "BonusChecksRandom", 0, iniPath) != 0);
        }
        /* Standalone: gold cap multiplier (1 = vanilla, default 30). */
        {
            int gm = GetPrivateProfileIntA("settings", "GoldCapMultiplier", 30, iniPath);
            if (gm >= 1 && gm <= 100) g_goldCapMultiplier = gm;
        }
        /* 2.x — configurable gold/XP reward ranges (standalone INI). */
        g_goldRewardMin = GetPrivateProfileIntA("settings", "GoldRewardMin", 100,    iniPath);
        g_goldRewardMax = GetPrivateProfileIntA("settings", "GoldRewardMax", 10000,  iniPath);
        g_xpRewardMin   = GetPrivateProfileIntA("settings", "XpRewardMin",   100,    iniPath);
        g_xpRewardMax   = GetPrivateProfileIntA("settings", "XpRewardMax",   250000, iniPath);
        RewardClampRanges();
        /* launcher-controlled standalone seed. */
        g_standaloneSeed = (DWORD)GetPrivateProfileIntA("settings", "ShuffleSeed", 0, iniPath);
        g_classFilter = GetPrivateProfileIntA("settings", "ClassFilter", 0, iniPath) != 0;
        g_clsEnabled[0] = GetPrivateProfileIntA("settings", "ClsAmazon", 1, iniPath) != 0;
        g_clsEnabled[1] = GetPrivateProfileIntA("settings", "ClsSorceress", 1, iniPath) != 0;
        g_clsEnabled[2] = GetPrivateProfileIntA("settings", "ClsNecromancer", 1, iniPath) != 0;
        g_clsEnabled[3] = GetPrivateProfileIntA("settings", "ClsPaladin", 1, iniPath) != 0;
        g_clsEnabled[4] = GetPrivateProfileIntA("settings", "ClsBarbarian", 1, iniPath) != 0;
        g_clsEnabled[5] = GetPrivateProfileIntA("settings", "ClsDruid", 1, iniPath) != 0;
        g_clsEnabled[6] = GetPrivateProfileIntA("settings", "ClsAssassin", 1, iniPath) != 0;
        g_stHideColors = GetPrivateProfileIntA("settings", "show_tier_colors", 0, iniPath) == 0;
        /* Bonus check toggles for standalone */
        {
            BOOL shr  = GetPrivateProfileIntA("settings", "CheckShrines",        0, iniPath) != 0;
            BOOL urn  = GetPrivateProfileIntA("settings", "CheckUrns",           0, iniPath) != 0;
            BOOL bar  = GetPrivateProfileIntA("settings", "CheckBarrels",        0, iniPath) != 0;
            BOOL chs  = GetPrivateProfileIntA("settings", "CheckChests",         0, iniPath) != 0;
            BOOL set  = GetPrivateProfileIntA("settings", "CheckSetPickups",     0, iniPath) != 0;
            BOOL gold = GetPrivateProfileIntA("settings", "CheckGoldMilestones", 0, iniPath) != 0;
            extern void Bonus_ApplyToggles(BOOL,BOOL,BOOL,BOOL,BOOL,BOOL);
            Bonus_ApplyToggles(shr, urn, bar, chs, set, gold);
        }
        /* Extra check toggles for standalone */
        {
            BOOL cow  = GetPrivateProfileIntA("settings", "CheckCowLevel",         0, iniPath) != 0;
            BOOL merc = GetPrivateProfileIntA("settings", "CheckMercMilestones",   0, iniPath) != 0;
            BOOL hf   = GetPrivateProfileIntA("settings", "CheckHellforgeRunes",   0, iniPath) != 0;
            BOOL npc  = GetPrivateProfileIntA("settings", "CheckNpcDialogue",      0, iniPath) != 0;
            BOOL rw   = GetPrivateProfileIntA("settings", "CheckRunewordCrafting", 0, iniPath) != 0;
            BOOL cube = GetPrivateProfileIntA("settings", "CheckCubeRecipes",      0, iniPath) != 0;
            extern void Extra_ApplyToggles(BOOL,BOOL,BOOL,BOOL,BOOL,BOOL);
            Extra_ApplyToggles(cow, merc, hf, npc, rw, cube);
        }
        Log("INI settings: goal=%d mode=%d monshuffle=%d bossshuffle=%d xp=%dx pool=%d classFilter=%d\n",
            g_apGoal, g_skillHuntingOn, g_monsterShuffleEnabled, g_bossShuffleEnabled, g_xpMultiplier, g_apSkillPoolSize, g_classFilter);
        return;
    }
    if (strstr(path, "ap_settings")) {
        g_apMode = TRUE;
    } else {
        Log("Loading standalone settings from %s\n", path);
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int ival;
        if (sscanf(line, "goal=%d", &ival) == 1 && ival >= 0 && ival <= 14) {
            /* accept both new (0-2) and legacy (0-14) encoding. */
            if (ival == 3)       g_apGoal = 3;          /* Collection mode */
            else if (ival == 4)  g_apGoal = 4;          /* Custom Goal mode */
            else                 g_apGoal = (ival <= 2) ? ival : (ival % 3);
        }
        /* Legacy goal_scope/difficulty_scope parsing removed 1.8.0 — apworlds have emitted unified `goal` since 1.6.x. */
        if (sscanf(line, "starting_gold=%d", &ival) == 1) g_apStartingGold = ival;
        if (sscanf(line, "death_link=%d", &ival) == 1) g_apDeathLink = (ival != 0);
        if (sscanf(line, "skill_pool_size=%d", &ival) == 1 && ival >= 1 && ival <= 210)
            g_apSkillPoolSize = ival;
        if (sscanf(line, "starting_skills=%d", &ival) == 1 && ival >= 0 && ival <= 20)
            g_apStartingSkills = ival;
        /* quest_story from AP slot_data is IGNORED — story quests are always required by D2's engine, so we keep g_apQuestStory = TRUE. */
        (void)0; /* was: if (sscanf(line, "quest_story=%d", ...)) g_apQuestStory = ...; */
        if (sscanf(line, "quest_hunting=%d", &ival) == 1) g_apQuestHunting = (ival != 0);
        if (sscanf(line, "quest_kill_zones=%d", &ival) == 1) g_apQuestKillZones = (ival != 0);
        if (sscanf(line, "quest_exploration=%d", &ival) == 1) g_apQuestExploration = (ival != 0);
        if (sscanf(line, "quest_waypoints=%d", &ival) == 1) g_apQuestWaypoints = (ival != 0);
        if (sscanf(line, "quest_level_milestones=%d", &ival) == 1) g_apQuestLevelMilestones = (ival != 0);
        /* new independent toggles; legacy game_mode kept as fallback so older apworld-generated ap_settings.dat files still work. */
        if (sscanf(line, "skill_hunting=%d", &ival) == 1) g_skillHuntingOn = (ival != 0);
        if (sscanf(line, "zone_locking=%d",  &ival) == 1) g_zoneLockingOn  = (ival != 0);
        if (sscanf(line, "game_mode=%d", &ival) == 1 && ival >= 0 && ival <= 1) {
            /* Only apply legacy if neither new field present (handled above). */
            g_skillHuntingOn = (ival == 0);
            g_zoneLockingOn  = (ival == 1);
        }
        if (sscanf(line, "controller=%d", &ival) == 1)
            g_controllerEnabled = (ival != 0);
        /* Filler distribution */
        if (sscanf(line, "filler_gold_pct=%d", &ival) == 1) g_fillerGoldPct = ival;
        if (sscanf(line, "filler_stat_pts_pct=%d", &ival) == 1) g_fillerStatPct = ival;
        if (sscanf(line, "filler_skill_pts_pct=%d", &ival) == 1) g_fillerSkillPct = ival;
        if (sscanf(line, "filler_trap_pct=%d", &ival) == 1) g_fillerTrapPct = ival;
        /* TrapsEnabled override from slot_data */
        if (sscanf(line, "traps_enabled=%d", &ival) == 1 && ival == 0) g_fillerTrapPct = 0;
        if (sscanf(line, "tiered_filler_depth=%d", &ival) == 1) g_tieredFillerDepth = (ival != 0);
        if (sscanf(line, "filler_reset_pts_pct=%d", &ival) == 1) g_fillerResetPct = ival;
        if (sscanf(line, "filler_loot_pct=%d", &ival) == 1) g_fillerLootPct = ival;
        if (sscanf(line, "monster_shuffle=%d", &ival) == 1) g_monsterShuffleEnabled = (ival != 0);
        if (sscanf(line, "boss_shuffle=%d", &ival) == 1) g_bossShuffleEnabled = (ival != 0);
        if (sscanf(line, "entrance_shuffle=%d", &ival) == 1) g_entranceShuffleEnabled = (ival != 0); /* 1.9.0 */
        /* SkillLevelReqs toggle (Maegis #2). */
        {
            extern BOOL g_skillLevelReqs;
            if (sscanf(line, "skill_level_reqs=%d", &ival) == 1) g_skillLevelReqs = (ival != 0);
        }
        /* Skill point cap under AP. */
        {
            extern int g_skillMaxLevel;
            if (sscanf(line, "skill_max_level=%d", &ival) == 1) {
                if (ival < 1)  ival = 1;
                if (ival > 99) ival = 99;
                g_skillMaxLevel = ival;
            }
        }
        /* 2.x (roadmap #4) — ItemLevelReqs toggle under AP. */
        {
            extern BOOL g_itemLevelReqs, g_itemStatsReqs;
            if (sscanf(line, "item_level_reqs=%d", &ival) == 1) g_itemLevelReqs = (ival != 0);
            if (sscanf(line, "item_stats_reqs=%d", &ival) == 1) g_itemStatsReqs = (ival != 0);
        }
        /* 2.x — Monster Revive Trap on/off from AP slot_data (overrides the ini read above). */
        {
            extern int g_monsterTrap, g_monsterTrapCount;
            if (sscanf(line, "monster_trap=%d", &ival) == 1) g_monsterTrap = (ival != 0);
            if (sscanf(line, "monster_trap_count=%d", &ival) == 1 && ival >= 1 && ival <= 16)
                g_monsterTrapCount = ival;
        }
        /* Bonus check toggles. Stashed locally; applied via Bonus_ApplyToggles below the loop. */
        {
            static int s_chShr=0, s_chUrn=0, s_chBar=0, s_chCh=0, s_chSet=0, s_chGold=0;
            static int s_anyBonusSeen=0;
            if (sscanf(line, "check_shrines=%d",         &ival) == 1) { s_chShr  = ival; s_anyBonusSeen=1; }
            if (sscanf(line, "check_urns=%d",            &ival) == 1) { s_chUrn  = ival; s_anyBonusSeen=1; }
            if (sscanf(line, "check_barrels=%d",         &ival) == 1) { s_chBar  = ival; s_anyBonusSeen=1; }
            if (sscanf(line, "check_chests=%d",          &ival) == 1) { s_chCh   = ival; s_anyBonusSeen=1; }
            if (sscanf(line, "check_set_pickups=%d",     &ival) == 1) { s_chSet  = ival; s_anyBonusSeen=1; }
            if (sscanf(line, "check_gold_milestones=%d", &ival) == 1) { s_chGold = ival; s_anyBonusSeen=1; }
            /* Random (escalating chance) or guaranteed. Absent key = guaranteed. */
            if (sscanf(line, "bonus_checks_random=%d", &ival) == 1) {
                extern void Bonus_SetRandomChance(BOOL on);
                Bonus_SetRandomChance(ival != 0);
            }
            /* Apply on every line — idempotent, so the final state after the loop is the union of all parsed values. */
            if (s_anyBonusSeen) {
                extern void Bonus_ApplyToggles(BOOL,BOOL,BOOL,BOOL,BOOL,BOOL);
                Bonus_ApplyToggles(s_chShr!=0, s_chUrn!=0, s_chBar!=0,
                                   s_chCh!=0,  s_chSet!=0, s_chGold!=0);
            }
        }
        /* Extra check toggles (Cow / Merc / Hellforge+High runes / NPC dialogue / Runeword crafting / Cube recipes). */
        {
            static int s_xCow=0, s_xMerc=0, s_xHF=0, s_xNpc=0, s_xRw=0, s_xCube=0;
            static int s_anyExtraSeen=0;
            if (sscanf(line, "check_cow_level=%d",       &ival) == 1) { s_xCow  = ival; s_anyExtraSeen=1; }
            if (sscanf(line, "check_merc_milestones=%d", &ival) == 1) { s_xMerc = ival; s_anyExtraSeen=1; }
            if (sscanf(line, "check_hellforge_runes=%d", &ival) == 1) { s_xHF   = ival; s_anyExtraSeen=1; }
            if (sscanf(line, "check_npc_dialogue=%d",    &ival) == 1) { s_xNpc  = ival; s_anyExtraSeen=1; }
            if (sscanf(line, "check_runeword_crafting=%d", &ival) == 1) { s_xRw   = ival; s_anyExtraSeen=1; }
            if (sscanf(line, "check_cube_recipes=%d",    &ival) == 1) { s_xCube = ival; s_anyExtraSeen=1; }
            if (s_anyExtraSeen) {
                extern void Extra_ApplyToggles(BOOL,BOOL,BOOL,BOOL,BOOL,BOOL);
                Extra_ApplyToggles(s_xCow!=0, s_xMerc!=0, s_xHF!=0,
                                   s_xNpc!=0, s_xRw!=0,   s_xCube!=0);
            }
        }
        /* Collection-goal sub-targets. */
        {
            /* granular per-item Collection toggles. */
            extern uint32_t g_collGoalOverrideSetsMask;     /* 32 bits */
            extern uint64_t g_collGoalOverrideRunesMask;    /* 33 bits in low part */
            extern uint16_t g_collGoalOverrideSpecialsMask; /* 10 bits */
            extern uint8_t  g_collGoalOverrideGems;
            extern uint64_t g_collGoalOverrideGold;
            extern BOOL     g_collGoalOverridePresent;
            int   tmp = 0;
            unsigned long long tmpGold = 0;
            if (sscanf(line, "collection_sets_mask_lo=%d", &tmp) == 1) {
                g_collGoalOverrideSetsMask =
                    (g_collGoalOverrideSetsMask & 0xFFFF0000u) | (uint32_t)(tmp & 0xFFFF);
                g_collGoalOverridePresent = TRUE;
            }
            if (sscanf(line, "collection_sets_mask_hi=%d", &tmp) == 1) {
                g_collGoalOverrideSetsMask =
                    (g_collGoalOverrideSetsMask & 0x0000FFFFu) | ((uint32_t)(tmp & 0xFFFF) << 16);
                g_collGoalOverridePresent = TRUE;
            }
            if (sscanf(line, "collection_runes_mask_lo=%d", &tmp) == 1) {
                g_collGoalOverrideRunesMask =
                    (g_collGoalOverrideRunesMask & ~(uint64_t)0xFFFFu) | (uint64_t)(tmp & 0xFFFF);
                g_collGoalOverridePresent = TRUE;
            }
            if (sscanf(line, "collection_runes_mask_md=%d", &tmp) == 1) {
                g_collGoalOverrideRunesMask =
                    (g_collGoalOverrideRunesMask & ~((uint64_t)0xFFFF << 16)) |
                    ((uint64_t)(tmp & 0xFFFF) << 16);
                g_collGoalOverridePresent = TRUE;
            }
            if (sscanf(line, "collection_runes_mask_hi=%d", &tmp) == 1) {
                g_collGoalOverrideRunesMask =
                    (g_collGoalOverrideRunesMask & ~((uint64_t)0xFF << 32)) |
                    ((uint64_t)(tmp & 0xFF) << 32);
                g_collGoalOverridePresent = TRUE;
            }
            if (sscanf(line, "collection_specials_mask=%d", &tmp) == 1) {
                g_collGoalOverrideSpecialsMask = (uint16_t)(tmp & 0xFFFF);
                g_collGoalOverridePresent = TRUE;
            }
            if (sscanf(line, "collection_target_gems=%d", &tmp) == 1) {
                g_collGoalOverrideGems = (uint8_t)(tmp != 0);
                g_collGoalOverridePresent = TRUE;
            }
            if (sscanf(line, "collection_gold_target=%llu", &tmpGold) == 1) {
                g_collGoalOverrideGold = (uint64_t)tmpGold;
                g_collGoalOverridePresent = TRUE;
            }
        }
        /* Custom goal slot_data: gold target + targets CSV. */
        {
            static uint64_t s_cgGold = 0;
            static char     s_cgCsv[1024] = "";
            static int      s_cgGoldSeen = 0, s_cgCsvSeen = 0;
            unsigned long long tmpGold2 = 0;
            if (sscanf(line, "custom_goal_gold_target=%llu", &tmpGold2) == 1) {
                s_cgGold = (uint64_t)tmpGold2;
                s_cgGoldSeen = 1;
            }
            if (strncmp(line, "custom_goal_targets_csv=", 24) == 0) {
                strncpy(s_cgCsv, line + 24, sizeof(s_cgCsv) - 1);
                s_cgCsv[sizeof(s_cgCsv) - 1] = 0;
                /* Trim trailing newline if present */
                size_t L = strlen(s_cgCsv);
                while (L > 0 && (s_cgCsv[L-1] == '\n' || s_cgCsv[L-1] == '\r')) {
                    s_cgCsv[--L] = 0;
                }
                s_cgCsvSeen = 1;
            }
            /* When BOTH keys have been seen, commit to the parser. */
            if (s_cgGoldSeen && s_cgCsvSeen) {
                extern void CustomGoal_ParseTargetsCSV(const char* csv, uint64_t goldTarget);
                CustomGoal_ParseTargetsCSV(s_cgCsv, s_cgGold);
                /* 2.x HOTFIX (Maegis) — empty custom goal falls back to Full Normal (winnable; no false "complete" on entry). */
                {
                    extern BOOL CustomGoal_IsActive(void);
                    if (g_apGoal == 4 && !CustomGoal_IsActive()) {
                        Log("CGT: custom goal empty (no targets, no gold) -> falling back to Full Normal (goal=0)\n");
                        g_apGoal = 0;
                    }
                }
            }
        }
        if (sscanf(line, "xp_multiplier=%d", &ival) == 1) {
            g_xpMultiplier = ival;
            if (g_xpMultiplier < 0) g_xpMultiplier = 0;
            if (g_xpMultiplier > 100) g_xpMultiplier = 100;
        }
        /* Gold caps (carried + stash) scaled; absent key = the DLL default. */
        if (sscanf(line, "gold_cap_multiplier=%d", &ival) == 1 && ival >= 1 && ival <= 100)
            g_goldCapMultiplier = ival;
        /* 2.x — configurable gold/XP reward ranges (AP slot_data). */
        if (sscanf(line, "gold_reward_min=%d", &ival) == 1) { g_goldRewardMin = ival; RewardClampRanges(); }
        if (sscanf(line, "gold_reward_max=%d", &ival) == 1) { g_goldRewardMax = ival; RewardClampRanges(); }
        if (sscanf(line, "xp_reward_min=%d",   &ival) == 1) { g_xpRewardMin   = ival; RewardClampRanges(); }
        if (sscanf(line, "xp_reward_max=%d",   &ival) == 1) { g_xpRewardMax   = ival; RewardClampRanges(); }
        /* additional slot_data fields */
        if (sscanf(line, "shop_shuffle=%d", &ival) == 1) g_shopShuffleEnabled = (ival != 0);
        /* 1.8.0 cleanup: treasure_cows slot_data parse removed */
        /* 2.9.7 cleanup: i_play_assassin removed (dead since 1.7.0 trap-filter removal) */

        /* 15 preload-id fields for gated zone-locking. */
        if (sscanf(line, "act1_preload_normal=%d",    &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[0][0] = ival;
        if (sscanf(line, "act1_preload_nightmare=%d", &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[0][1] = ival;
        if (sscanf(line, "act1_preload_hell=%d",      &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[0][2] = ival;
        if (sscanf(line, "act2_preload_normal=%d",    &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[1][0] = ival;
        if (sscanf(line, "act2_preload_nightmare=%d", &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[1][1] = ival;
        if (sscanf(line, "act2_preload_hell=%d",      &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[1][2] = ival;
        if (sscanf(line, "act3_preload_normal=%d",    &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[2][0] = ival;
        if (sscanf(line, "act3_preload_nightmare=%d", &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[2][1] = ival;
        if (sscanf(line, "act3_preload_hell=%d",      &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[2][2] = ival;
        if (sscanf(line, "act4_preload_normal=%d",    &ival) == 1 && ival >= 0 && ival < 3) g_actPreload[3][0] = ival;
        if (sscanf(line, "act4_preload_nightmare=%d", &ival) == 1 && ival >= 0 && ival < 3) g_actPreload[3][1] = ival;
        if (sscanf(line, "act4_preload_hell=%d",      &ival) == 1 && ival >= 0 && ival < 3) g_actPreload[3][2] = ival;
        if (sscanf(line, "act5_preload_normal=%d",    &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[4][0] = ival;
        if (sscanf(line, "act5_preload_nightmare=%d", &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[4][1] = ival;
        if (sscanf(line, "act5_preload_hell=%d",      &ival) == 1 && ival >= 0 && ival < 4) g_actPreload[4][2] = ival;
    }
    fclose(f);
    /* 2.x — combined mode (zone_locking + entrance_shuffle BOTH on) is now SUPPORTED. */
    if (g_zoneLockingOn && g_entranceShuffleEnabled) {
        Log("AP: combined mode ON (zone_locking + entrance_shuffle) — gate keys "
            "via key-conditions (d2arch_keycond.c)\n");
    }
    Log("AP: Settings loaded (goal=%d [act=%d diff=%d], skillHunt=%d zoneLock=%d, "
        "death_link=%d, monShuf=%d bossShuf=%d shopShuf=%d)\n",
        g_apGoal, GOAL_ACT_SCOPE, GOAL_DIFF_SCOPE,
        g_skillHuntingOn, g_zoneLockingOn, g_apDeathLink,
        g_monsterShuffleEnabled, g_bossShuffleEnabled, g_shopShuffleEnabled);
    Log("AP: Settings loaded extra (entrance_shuffle=%d)\n", g_entranceShuffleEnabled);
}

/* DEATHLINK — detect player death and write to file for bridge */
static DWORD g_lastDeathSendTime = 0;
static BOOL  g_wasAlive = TRUE;

/* a death we CAUSED (incoming DeathLink) must not be re-broadcast: A dies -> B is killed -> B's death goes out -> A is killed -> ping-pong until every linked player is farming corpses. */
static DWORD g_deathLinkSuppressUntil = 0;

/* REALLY kill the local player on an incoming DeathLink. */
typedef void (__fastcall* DL_StartXY_Dead_t)(void* pGame, void* pUnit,
                                             int a3, int a4, int a5);

/* Is the character we are playing hardcore? */
static BOOL IsHardcoreCharacter(void) {
    if (!g_charName[0]) return TRUE;

    char dir[MAX_PATH], path[MAX_PATH];
    GetSaveDir(dir, MAX_PATH);
    if (!dir[0]) return TRUE;
    _snprintf(path, sizeof(path), "%s%s.d2s", dir, g_charName);
    path[sizeof(path) - 1] = 0;

    FILE* f = fopen(path, "rb");
    if (!f) return TRUE;
    unsigned char hdr[0x30];
    size_t got = fread(hdr, 1, sizeof(hdr), f);
    fclose(f);
    if (got < 0x25) return TRUE;

    BOOL hc = (hdr[0x24] & 0x04) != 0;
    Log("DEATHLINK: character '%s' status byte 0x%02X -> %s\n",
        g_charName, hdr[0x24], hc ? "HARDCORE" : "softcore");
    return hc;
}
/* Non-static: the consumer lives in d2arch_gameloop.c (earlier in the unity TU), which forward-declares it. */
BOOL DeathLink_KillLocalPlayer(void) {
    HMODULE hGame = GetModuleHandleA("D2Game.dll");
    if (!hGame || !g_cachedPGame) return FALSE;

    static const BYTE kExpect[13] = { 0x53, 0x55, 0x56, 0x8B, 0xF2,
                                      0x57, 0x8B, 0xF9, 0x8B, 0x06,
                                      0x83, 0xF8, 0x02 };
    BYTE* fp = (BYTE*)hGame + 0x50710;
    __try {
        if (memcmp(fp, kExpect, sizeof(kExpect)) != 0) {
            Log("DEATHLINK KILL: prologue mismatch at D2Game+0x50710 — "
                "unexpected D2Game build, falling back to trap\n");
            return FALSE;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return FALSE; }

    /* Hardcore stays out of this — see the note above. */
    if (IsHardcoreCharacter()) {
        Log("DEATHLINK KILL: hardcore character — refusing to kill "
            "(notification only; a DeathLink must not end a HC run)\n");
        return TRUE;   /* handled: deliberately no kill, and no trap either */
    }

    void* pSrv = GetServerPlayer(g_cachedPGame);
    if (!pSrv) return FALSE;

    /* Already dead? Then the link has nothing to add. */
    void* pClient = Player();
    if (pClient) {
        __try {
            int mode = *(int*)((DWORD)pClient + 0x10);
            if (mode == 0 || mode == 17) return TRUE; /* handled: already dead */
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    __try {
        ((DL_StartXY_Dead_t)fp)((void*)g_cachedPGame, pSrv, 0, 0, 0);
        Log("DEATHLINK KILL: player killed via PLRMODE_StartXY_Dead "
            "(D2Game+0x50710)\n");
        return TRUE;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("DEATHLINK KILL: StartXY_Dead call EXCEPTION — trap fallback\n");
        return FALSE;
    }
}

DWORD g_lastDeathTick = 0;

static void CheckPlayerDeath(void) {
    void* p = Player();
    if (!p) return;

    __try {
        int mode = *(int*)((DWORD)p + 0x10); /* unit mode */
        /* PLRMODE_DEATH=0 (dying animation), PLRMODE_DEAD=17 (corpse). */
        BOOL isDead = (mode == 0 || mode == 17);

        if (isDead && g_wasAlive) g_lastDeathTick = GetTickCount();

        if (!g_apConnected || !g_apDeathLink) { g_wasAlive = !isDead; return; }

        if (isDead && g_wasAlive) {
            /* a death caused by an INCOMING DeathLink is consumed here, not re-broadcast (the spec's no-bounce rule; without it two linked D2 players kill each other in a loop). */
            if (GetTickCount() < g_deathLinkSuppressUntil) {
                g_deathLinkSuppressUntil = 0;
                g_wasAlive = FALSE;
                Log("AP DEATHLINK: local death was caused by a received "
                    "DeathLink — not re-broadcast\n");
                return;
            }
            DWORD now = GetTickCount();
            if (now - g_lastDeathSendTime > 30000) { /* 30sec cooldown */
                g_lastDeathSendTime = now;
                if (ApPipe_Mode()) {
                    /* V2 pipe transport: the launcher owns the AP socket, so the death goes out as a DEATH:<cause> line. */
                    char cause[96];
                    const char* zn = Preload_ZoneName(GetCurrentArea());
                    if (zn) {
                        _snprintf(cause, sizeof(cause), "slain in %s", zn);
                        cause[sizeof(cause) - 1] = 0;
                    } else {
                        strcpy(cause, "died");
                    }
                    ApPipe_SendDeath(cause);
                    Log("AP DEATHLINK: Death sent over pipe for '%s' (%s)\n",
                        g_charName, cause);
                } else {
                    char dir[MAX_PATH], path[MAX_PATH];
                    GetArchDir(dir, MAX_PATH);
                    sprintf(path, "%sap_death.dat", dir);
                    FILE* f = fopen(path, "w");
                    if (f) {
                        fprintf(f, "death=1\nplayer=%s\ncause=Killed in combat\n", g_charName);
                        fclose(f);
                        Log("AP DEATHLINK: Death sent for '%s'\n", g_charName);
                    }
                }
            }
        }
        g_wasAlive = !isDead;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

/* Load location owner names from bridge (who gets the item for each check) */
static void LoadLocationOwners(void) {
    if (g_locationOwnersLoaded) return;
    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(path, "%sap_location_owners.dat", dir);
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[128];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        int locId = 0;
        char owner[24] = "";
        if (sscanf(line, "%d=%23[^\n]", &locId, owner) == 2 && locId > LOCATION_BASE) {
            /* Map AP location ID back to quest ID + difficulty */
            int diff = 0;
            int questId = locId - LOCATION_BASE;
            if (questId >= 2000) { diff = 2; questId -= 2000; }
            else if (questId >= 1000) { diff = 1; questId -= 1000; }
            if (questId >= 0 && questId < MAX_QUEST_ID && diff >= 0 && diff < 3) {
                strncpy(g_locationOwner[diff][questId], owner, 23);
                g_locationOwner[diff][questId][23] = 0;
                count++;
            }
        }
    }
    fclose(f);
    if (count > 0) {
        g_locationOwnersLoaded = TRUE;
        Log("Loaded %d location owners from bridge\n", count);
    }
}

/* Load AP gate-key item locations from bridge. */
static DWORD g_lastAPItemLocPoll = 0;
static void LoadAPItemLocations(void) {
    /* The bridge updates this file as new scouts complete, so we re-read it periodically rather than once. */
    DWORD now = GetTickCount();
    if (g_apItemLocationsLoaded && now - g_lastAPItemLocPoll < 5000) return;
    g_lastAPItemLocPoll = now;

    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(path, "%sap_item_locations.dat", dir);
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        int itemId = 0;
        char value[200] = "";
        if (sscanf(line, "%d=%199[^\n]", &itemId, value) != 2) continue;

        /* Parse value: "loc_id|finder_slot|finder_name|recipient_slot" */
        int locId = 0, finderSlot = 0, recipientSlot = 0;
        char finderName[64] = "";
        char* p1 = strchr(value, '|');
        if (!p1) continue;
        *p1++ = 0;
        locId = atoi(value);
        char* p2 = strchr(p1, '|');
        if (!p2) continue;
        *p2++ = 0;
        finderSlot = atoi(p1);
        char* p3 = strchr(p2, '|');
        if (!p3) continue;
        *p3++ = 0;
        strncpy(finderName, p2, sizeof(finderName) - 1);
        finderName[sizeof(finderName) - 1] = 0;
        recipientSlot = atoi(p3);
        (void)finderSlot; (void)recipientSlot;  /* reserved for future filtering */

        /* Map item ID to (diff, gate_slot). */
        int diff = -1, slot = -1;
        if (itemId >= 46101 && itemId <= 46118)      { diff = 0; slot = itemId - 46101; }
        else if (itemId >= 46121 && itemId <= 46138) { diff = 1; slot = itemId - 46121; }
        else if (itemId >= 46141 && itemId <= 46158) { diff = 2; slot = itemId - 46141; }
        else continue;  /* not a gate-key item — skip silently */

        _snprintf(g_apItemLocation[diff][slot], APKEY_DISPLAY_LEN - 1,
                  "loc %d @ %s", locId, finderName);
        g_apItemLocation[diff][slot][APKEY_DISPLAY_LEN - 1] = 0;
        count++;
    }
    fclose(f);
    if (count > 0) {
        g_apItemLocationsLoaded = TRUE;
        Log("Loaded %d AP gate-key locations from bridge\n", count);
    }
}

/* Poll AP bridge status file (Bridge -> DLL) */
static DWORD g_lastAPStatusPoll = 0;
static void PollAPStatus(void);

/* public wrapper used by d2arch_save.c OnCharacterLoad to close the AP-connect race window (B7 / Maegis "click AP Connect then SP too fast" / Dank_Santa "silent AP lost connection"). */
BOOL AP_WaitForConnectIfPending(int maxMs) {
    if (g_apConnected) return TRUE;
    /* V2 pipe transport: the launcher drives the connection, so there is no Connect click and g_apPolling stays FALSE — wait on the pipe's STATE event instead (PollAPStatus applies it inside the loop). */
    if (!g_apPolling && !ApPipe_Mode()) return FALSE;   /* User didn't click AP Connect */

    DWORD start = GetTickCount();
    int polls = 0;
    while ((GetTickCount() - start) < (DWORD)maxMs) {
        g_lastAPStatusPoll = 0;        /* bypass the 2s throttle */
        PollAPStatus();
        polls++;
        if (g_apConnected) {
            Log("AP_WaitForConnectIfPending: connected after %d polls / %lums\n",
                polls, GetTickCount() - start);
            return TRUE;
        }
        Sleep(150);
    }
    Log("AP_WaitForConnectIfPending: gave up after %d polls / %lums "
        "(g_apPolling=%d g_apConnected=%d)\n",
        polls, GetTickCount() - start, (int)g_apPolling, (int)g_apConnected);
    return FALSE;
}

static void PollAPStatus(void) {
    DWORD now = GetTickCount();
    /* was 2000ms. This poll also applies inbound AP items from ap_unlocks.dat, so the old 2s cadence meant received items could take up to 2 seconds to appear. 250ms = items land near-instantly; the body only reads small local .dat files so 4Hz is cheap. */
    if (now - g_lastAPStatusPoll < 250) return;
    g_lastAPStatusPoll = now;

    /* V2 pipe transport: connection state comes from STATE: messages the launcher pushes over the pipe, not from ap_status.dat (no bridge process exists to write it -- and a stale ap_status.dat left behind by an old V1 session must not be trusted). */
    if (ApPipe_Mode()) {
        int ev = ApPipe_PollStateEvent();
        if (ev == 1 && !g_apConnected) {
            g_apConnected = TRUE;
            strcpy(g_apStatus, "Connected");
            Log("AP PIPE: connection established (launcher authenticated)\n");
            if (g_apHadDisconnect) {
                ShowNotify("AP RECONNECTED — pending checks will sync");
                g_apHadDisconnect = FALSE;
            }
            /* Same gating moment as the file-based auth transition below: only a REAL launcher-confirmed connection flips g_apMode. */
            g_apMode = TRUE;
            SaveAPCharConfig();
            {
                void* cell = g_btnCellFileGreen ? g_btnCellFileGreen : g_btnCellFile;
                if (g_btnConnectBtn && cell) {
                    *(void**)((BYTE*)g_btnConnectBtn + 0x04) = cell;
                }
            }
        } else if (ev == 0 && g_apConnected) {
            g_apConnected = FALSE;
            strcpy(g_apStatus, "Disconnected");
            Log("AP PIPE: connection lost (launcher reports disconnect)\n");
            DWORD notifyNow = GetTickCount();
            if (notifyNow - g_apLastDisconnectNotify > 30000) {
                ShowNotify("AP DISCONNECTED — your checks will queue locally");
                g_apLastDisconnectNotify = notifyNow;
            }
            g_apHadDisconnect = TRUE;
            if (g_btnConnectBtn && g_btnCellFileRed) {
                *(void**)((BYTE*)g_btnConnectBtn + 0x04) = g_btnCellFileRed;
            }
        }

        /* DeathLink inbound (V2 pipe only). */
        {
            char dlSource[48], dlCause[96];
            if (ApPipe_PollDeathLinkEvent(dlSource, sizeof(dlSource),
                                          dlCause, sizeof(dlCause))) {
                char dlMsg[192];
                _snprintf(dlMsg, sizeof(dlMsg), "[DeathLink] %s: %s",
                          dlSource, dlCause);
                dlMsg[sizeof(dlMsg) - 1] = 0;
                ShowNotify(dlMsg);
                /* was "DeathLink from <who>" in the TEXT field with "AP server" in the SOURCE field, which rendered as "DeathLink from Maegis (AP server)" — the name said twice and the wrong one attributed. */
                ItemLogAddA(0, 11, "DeathLink", dlSource[0] ? dlSource : "AP server");
                Log("AP PIPE DEATHLINK: %s: %s\n", dlSource, dlCause);

                /* DeathLink means DEATH. */
                if (g_cachedPGame && Player()) {
                    g_deathLinkSuppressUntil = GetTickCount() + 60000;
                    g_pendingDeathLinkKill = 1;
                } else {
                    Log("AP PIPE DEATHLINK: received outside a live game — "
                        "notification only\n");
                }
            }
        }
        return;
    }

    /* 1.9.5 Gap 4 fix — capture wasConnected BEFORE CheckBridgeHealth so the heartbeat-stale path (which sets g_apConnected=FALSE internally) doesn't hide the connected->disconnected transition from the notify logic below. */
    BOOL wasConnected = g_apConnected;

    /* 1.9.5 Gap 1 — detect ghost-dead bridge (rate-limited to 60s inside) */
    CheckBridgeHealth();

    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    /* poll only after the user has actually pressed Connect (bridge has been started). */
    if (!g_apPolling) return;

    sprintf(path, "%sap_status.dat", dir);

    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    /* 1.9.5 Bug C3 fix — track whether we just transitioned to an error state so we can fire a one-shot ShowNotify on the error with its reason. */
    BOOL prevWasError = (strcmp(g_apStatus, "Refused") == 0
                         || strcmp(g_apStatus, "Error") == 0);
    BOOL newIsError = FALSE;
    while (fgets(line, sizeof(line), f)) {
        char val[64];
        char emsg[120];
        /* 1.9.5 Bug C3 fix — capture errormsg= lines into g_apErrorMsg. */
        if (sscanf(line, "errormsg=%119[^\r\n]", emsg) == 1) {
            strncpy(g_apErrorMsg, emsg, sizeof(g_apErrorMsg) - 1);
            g_apErrorMsg[sizeof(g_apErrorMsg) - 1] = 0;
            continue;
        }
        if (sscanf(line, "status=%63s", val) == 1) {
            if (strcmp(val, "authenticated") == 0) {
                g_apConnected = TRUE;
                strcpy(g_apStatus, "Connected");
                /* 1.9.5 Bug C3 — clear any prior error on successful auth */
                g_apErrorMsg[0] = 0;
            } else if (strcmp(val, "connected") == 0) {
                /* websocket open but slot_data not yet received. */
                g_apConnected = FALSE;
                strcpy(g_apStatus, "Authenticating...");
            } else if (strcmp(val, "reconnecting") == 0) {
                /* bridge is in retry-backoff. */
                g_apConnected = FALSE;
                strcpy(g_apStatus, "Reconnecting...");
            } else if (strcmp(val, "disconnected") == 0) {
                g_apConnected = FALSE;
                strcpy(g_apStatus, "Disconnected");
            } else if (strcmp(val, "connecting") == 0) {
                g_apConnected = FALSE;
                strcpy(g_apStatus, "Connecting...");
            } else if (strcmp(val, "error") == 0) {
                g_apConnected = FALSE;
                strcpy(g_apStatus, "Error");
                newIsError = TRUE;
            } else if (strcmp(val, "refused") == 0) {
                g_apConnected = FALSE;
                strcpy(g_apStatus, "Refused");
                newIsError = TRUE;
            }
        }
    }
    fclose(f);
    /* 1.9.5 Bug C3 fix — on transition into error/refused, surface the reason on screen so the player knows what to fix. */
    if (newIsError && !prevWasError && g_apErrorMsg[0]) {
        char notify[160];
        _snprintf(notify, sizeof(notify),
                  "AP %s: %s", g_apStatus, g_apErrorMsg);
        notify[sizeof(notify) - 1] = 0;
        ShowNotify(notify);
        Log("AP ERROR STATUS: %s — %s\n", g_apStatus, g_apErrorMsg);
    }
    if (!wasConnected && g_apConnected) {
        /* banner removed at user request; the green Connect button already signals authentication. */
        Log("AP: Connection established\n");
        /* 1.9.5 Gap 2 — show reconnect notify if we previously disconnected (and notified the player). */
        if (g_apHadDisconnect) {
            ShowNotify("AP RECONNECTED — pending checks will sync");
            g_apHadDisconnect = FALSE;
        }
        /* flip g_apMode TRUE here, on actual auth, instead of at Connect-click time. */
        g_apMode = TRUE;
        SaveAPCharConfig();
        /* race-recovery removed. */
        LoadAPSettings();
        if (g_charName[0] && g_settingsFrozen) {
            /* Existing-character soft-lock reconcile. */
            char checkPath[MAX_PATH];
            GetArchDir(checkPath, MAX_PATH);
            strcat(checkPath, "ap_settings.dat");
            FILE* sf = fopen(checkPath, "r");
            if (sf) {
                char line[256];
                int  slotZL    = -1;   /* -1 = not present in file */
                int  slotCap   = -1;
                int  slotReqs  = -1;
                while (fgets(line, sizeof(line), sf)) {
                    int v;
                    if (sscanf(line, "zone_locking=%d", &v) == 1)
                        slotZL = (v != 0) ? 1 : 0;
                    if (sscanf(line, "skill_max_level=%d", &v) == 1
                        && v >= 1 && v <= 99)
                        slotCap = v;
                    if (sscanf(line, "skill_level_reqs=%d", &v) == 1)
                        slotReqs = (v != 0) ? 1 : 0;
                }
                fclose(sf);

                /* Settings are frozen into a character at creation so the world
                   cannot change shape underneath a run in progress. The skill
                   point cap and the level requirements are not world shape —
                   they are limits on the player's own panel, and a character
                   created before the server's answer arrived kept whatever the
                   ini defaulted to (cap 20) with no way back. Marco's 'solida'
                   was stuck at 20 while slot_data said 99.

                   So these two follow the server on every connect. The frozen
                   set still owns everything that decides what the world looks
                   like. */
                {
                    extern int  g_skillMaxLevel;
                    extern BOOL g_skillLevelReqs;
                    BOOL changed = FALSE;
                    if (slotCap > 0 && slotCap != g_skillMaxLevel) {
                        Log("AP LIVE SETTING: skill cap %d -> %d (from slot_data)\n",
                            g_skillMaxLevel, slotCap);
                        g_skillMaxLevel = slotCap;
                        changed = TRUE;
                    }
                    if (slotReqs >= 0 && (BOOL)slotReqs != g_skillLevelReqs) {
                        Log("AP LIVE SETTING: skill level reqs %d -> %d (from slot_data)\n",
                            (int)g_skillLevelReqs, slotReqs);
                        g_skillLevelReqs = (slotReqs != 0);
                        changed = TRUE;
                    }
                    if (changed) {
                        BOOL wf = g_settingsFrozen;
                        g_settingsFrozen = FALSE;
                        SaveStateFile();
                        g_settingsFrozen = wf;
                    }
                }

                /* Two settings now follow the server, but 33 others are still
                   frozen at creation with no way to correct them. Marco's cap
                   sat wrong for a day because nothing ever said so — the game
                   simply behaved differently from the YAML and stayed quiet.
                   This does not unfreeze anything; it just refuses to be quiet
                   about a disagreement, so the next one is a log line instead
                   of a day of confusion.

                   Deliberately generic: it compares whatever keys both files
                   happen to share, so a setting added later is covered without
                   anyone remembering to add it here. */
                ReportFrozenSettingDrift();
                if (slotZL == 0 && g_zoneLockingOn) {
                    Log("AP-MISMATCH RECONCILE: per-char zone_locking=ON "
                        "but slot_data=OFF — auto-correcting to OFF "
                        "(no gate keys exist in this multiworld so any "
                        "locked zones would soft-lock the run)\n");
                    g_zoneLockingOn = FALSE;
                    InitZoneLocks();
                    /* Bypass freeze just long enough to persist the fix. */
                    BOOL wasFrozen = g_settingsFrozen;
                    g_settingsFrozen = FALSE;
                    SaveStateFile();
                    g_settingsFrozen = wasFrozen;
                    ShowNotify("Zone Locking auto-disabled (matches AP server)");
                }
            }
        }
        g_locationOwnersLoaded   = FALSE; /* Reload owners on new connection */
        g_apItemLocationsLoaded  = FALSE; /* Reload key locations too */
        memset(g_apItemLocation, 0, sizeof(g_apItemLocation));
        /* swap Connect button to GREEN so the player gets a clear visual confirmation that the AP server authenticated and the randomization settings are now coming from the server's slot_data. */
        {
            void* cell = g_btnCellFileGreen ? g_btnCellFileGreen : g_btnCellFile;
            if (g_btnConnectBtn && cell) {
                *(void**)((BYTE*)g_btnConnectBtn + 0x04) = cell;
                Log("AP: Connect button -> %s (connected)\n",
                    g_btnCellFileGreen ? "green" : "yellow (green DC6 missing)");
            }
        }
        /* push slot_data values onto every title-screen toggle/ dropdown so the user can SEE the server's settings. */
        SyncAPToTitleSettings();
    } else if (wasConnected && !g_apConnected) {
        Log("AP: Connection lost\n");
        /* 1.9.5 Gap 2 — explicit on-screen notify so in-game players see the disconnect (previously only visible via title-screen button color or F1 Editor Page 2). */
        DWORD notifyNow = GetTickCount();
        if (notifyNow - g_apLastDisconnectNotify > 30000) {
            ShowNotify("AP DISCONNECTED — your checks will queue locally");
            g_apLastDisconnectNotify = notifyNow;
        }
        g_apHadDisconnect = TRUE;   /* mark so reconnect notify fires too */
        /* swap Connect button back to red (disconnected) */
        if (g_btnConnectBtn && g_btnCellFileRed) {
            *(void**)((BYTE*)g_btnConnectBtn + 0x04) = g_btnCellFileRed;
            Log("AP: Connect button -> red (disconnected)\n");
        }
        /* clean up ap_settings.dat so the next character load won't re-apply stale AP settings. */
        {
            char cleanupPath[MAX_PATH];
            GetArchDir(cleanupPath, MAX_PATH);
            strcat(cleanupPath, "ap_settings.dat");
            if (DeleteFileA(cleanupPath)) {
                Log("AP: deleted stale ap_settings.dat on disconnect\n");
            }
        }
        /* restore title-screen buttons from d2arch.ini so the user can edit them again in standalone mode. */
        RestoreTitleSettingsFromINI();
        /* RestoreAllCharacters() disabled — all characters always visible */
    }
}

/* Poll for AP skill unlocks (Bridge -> DLL via ap_unlocks.dat) */
static void PollAPUnlocks(void) {
    if (!g_apConnected || !g_charName[0] || !g_poolInitialized) return;

    char dir[MAX_PATH], path[MAX_PATH], processing[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    sprintf(path, "%sap_unlocks.dat", dir);
    sprintf(processing, "%sap_unlocks.dat.processing", dir);

    /* 1.8.0 FIX: atomic rename-then-read to avoid race with bridge's atomic write. */
    /* a previous tick may have deferred part of a large backlog into our private pending file. */
    char pendPath[MAX_PATH];
    sprintf(pendPath, "%sap_unlocks_pending.dat", dir);
    BOOL havePending = (GetFileAttributesA(pendPath) != INVALID_FILE_ATTRIBUTES);

    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES && !havePending) return;
    /* If a previous poll crashed mid-process, clean up the old .processing file first so MoveFileEx's REPLACE_EXISTING succeeds. */
    if (GetFileAttributesA(processing) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileA(processing);
    }
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        if (!MoveFileExA(path, processing, MOVEFILE_REPLACE_EXISTING)) {
            /* Rename failed — either file vanished between our check and the move, or another process has it locked. */
            if (!havePending) return;
        }
    }
    /* Fold the deferred remainder into .processing (deferred lines FIRST so the original arrival order is preserved), then clear the pending file. */
    if (havePending) {
        FILE* pf = fopen(pendPath, "r");
        if (pf) {
            char oldProc[MAX_PATH];
            sprintf(oldProc, "%sap_unlocks.dat.processing.new", dir);
            FILE* nf = fopen(oldProc, "w");
            if (nf) {
                char ln[160];
                while (fgets(ln, sizeof(ln), pf)) fputs(ln, nf);
                fclose(pf); pf = NULL;
                FILE* cur = fopen(processing, "r");
                if (cur) {
                    while (fgets(ln, sizeof(ln), cur)) fputs(ln, nf);
                    fclose(cur);
                }
                fclose(nf);
                MoveFileExA(oldProc, processing, MOVEFILE_REPLACE_EXISTING);
            }
            if (pf) fclose(pf);
        }
        DeleteFileA(pendPath);
    }

    FILE* f = fopen(processing, "r");
    if (!f) return;

    /* ensure dedup set is loaded for the CURRENT character. */
    {
        static char s_dedupForChar[64] = {0};
        if (strcmp(s_dedupForChar, g_charName) != 0) {
            g_appliedApLoaded = FALSE;
            g_appliedApCount = 0;
            /* same omission as the char-exit path: without this the filler locations of the previous character stayed marked. */
            g_appliedFillerLocsLoaded = FALSE;
            g_appliedFillerLocCount = 0;
            strncpy(s_dedupForChar, g_charName, sizeof(s_dedupForChar) - 1);
            s_dedupForChar[sizeof(s_dedupForChar) - 1] = 0;
        }
    }
    if (!g_appliedApLoaded) LoadAppliedApIds();
    if (!g_appliedFillerLocsLoaded) LoadAppliedFillerLocs();

    char line[160];
    int unlockCount = 0;
    const int AP_UNLOCK_BATCH_CAP = 8;   /* max applications per tick */
    while (fgets(line, sizeof(line), f)) {
        /* batch cap: don't drain a 200-item backlog in one render frame (notify/ItemLog spam + every queued drop at once). */
        if (unlockCount >= AP_UNLOCK_BATCH_CAP) {
            /* Requeue to a DLL-PRIVATE file (never ap_unlocks.dat) so we can't race the bridge's atomic os.replace and lose the remainder. */
            char pend[MAX_PATH];
            sprintf(pend, "%sap_unlocks_pending.dat", dir);
            FILE* rq = fopen(pend, "a");
            if (rq) {
                fputs(line, rq);
                char rest[160];
                while (fgets(rest, sizeof(rest), f)) fputs(rest, rq);
                fclose(rq);
            }
            break;
        }
        int apId = 0;
        char unlockSender[40] = "";
        int unlockLoc = 0;
        /* Line format evolution (parser tolerates all three): 1.8.4 added sender: unlock=<id>|<sender> 1.9.0 added location: unlock=<id>|<sender>|<loc> legacy fallback: unlock=<id> The location lets us mark a self-released quest as locally complete (same effect as if the player completed it in-game) so the F1 quest list and check counters update correctly. */
        int parsed = sscanf(line, "unlock=%d|%39[^|]|%d", &apId, unlockSender, &unlockLoc);
        if (parsed < 3) {
            /* Reset and try 2-field format. */
            unlockSender[0] = 0;
            unlockLoc = 0;
            parsed = sscanf(line, "unlock=%d|%39[^\r\n]", &apId, unlockSender);
        }
        /* Trim possible trailing CR on the sender (no |loc field present). */
        {
            int sl = (int)strlen(unlockSender);
            while (sl > 0 && (unlockSender[sl-1] == '\r' || unlockSender[sl-1] == '\n')) {
                unlockSender[--sl] = 0;
            }
        }

        /* WHO SENT IT. Every ItemLog line below used to be attributed to the literal string "AP server", so the feed read "→ +5000 gold (AP server)" no matter who found it. The outgoing direction has always named the receiver correctly, which made the asymmetry look deliberate. It wasn't: the sender has been on the wire since the 1.8.4 ITEM v2 format ("ITEM:<id>|<sender>[|<loc>]"), the launcher fills it in for every item (D2Plugin.ReceiveItemsAsync), and PollAPUnlocks has been parsing it into unlockSender all along — purely to compare it against our own slot for self-release detection. Nothing ever showed it to the player. So this needs no protocol change and no launcher change: it is one variable, already parsed, finally used. "AP server" survives only as the honest fallback for v1 lines from an older launcher, where the sender genuinely is unknown. */
        const char* apSrc = unlockSender[0] ? unlockSender : "AP server";
        if (parsed >= 1 && apId > 45000) {
            /* AP self-release auto-complete. */
            if (unlockLoc >= 42000 && unlockLoc < 45000 &&
                unlockSender[0] != 0 && g_apSlot[0] != 0 &&
                _stricmp(unlockSender, g_apSlot) == 0) {
                int locOff = unlockLoc - 42000;
                int rDiff = locOff / 1000;
                int rQid  = locOff % 1000;
                if (rDiff >= 0 && rDiff < 3 && rQid > 0 && rQid < MAX_QUEST_ID) {
                    if (!g_questCompleted[rDiff][rQid]) {
                        g_questCompleted[rDiff][rQid] = TRUE;
                        Log("AP SELF-COMPLETE: marked qid=%d diff=%d (loc=%d, item=%d)\n",
                            rQid, rDiff, unlockLoc, apId);
                    }
                }
            }

            /* Bonus check auto-track. */
            if (unlockLoc >= 60000 && unlockLoc < 65300 &&
                unlockSender[0] != 0 && g_apSlot[0] != 0 &&
                _stricmp(unlockSender, g_apSlot) == 0) {
                extern BOOL Bonus_OnAPItemReceived(int apId);
                if (Bonus_OnAPItemReceived(unlockLoc)) {
                    Log("AP BONUS-TRACKED: loc=%d (item=%d)\n", unlockLoc, apId);
                }
            }

            /* Extra check auto-track. */
            if (unlockLoc >= 65300 && unlockLoc < 65800 &&
                unlockSender[0] != 0 && g_apSlot[0] != 0 &&
                _stricmp(unlockSender, g_apSlot) == 0) {
                extern BOOL Extra_OnAPItemReceived(int apId);
                if (Extra_OnAPItemReceived(unlockLoc)) {
                    Log("AP EXTRA-TRACKED: loc=%d (item=%d)\n", unlockLoc, apId);
                }
            }

            /* 1.7.1 DEDUP: skip already-applied items, EXCEPT stackable fillers (45500-45999 range). */
            /* ...but ONLY when the item carries a location. */
            BOOL isFiller = (apId >= 45500 && apId < 46000) && unlockLoc > 0;
            /* gate-keys (46101-46158) are exempt from dedup. */
            BOOL isGateKey = (apId >= GATEKEY_AP_BASE_NORMAL &&
                              apId <  GATEKEY_AP_BASE_HELL + GATEKEY_PER_DIFF);
            /* every act-boss check delivers the SAME id (46200), so apId dedup would swallow all but the first one and the player would see the act-complete message once per game instead of once per act. */
            BOOL isBossToken = (apId == 46200);
            if (!isFiller && !isGateKey && !isBossToken && IsApIdApplied(apId)) {
                Log("AP DEDUP: apId=%d already applied (non-filler), skipping\n", apId);
                continue;
            }
            /* Boss Token (46200). The apworld locks one of these onto every act-boss and difficulty-transition check; it is what its region logic uses to open the next act, so it is never sent to another player and never carries a reward. Until now no branch below matched the id, so killing an act boss produced silence: no notify, no item-log line, not even a log entry. It is still a pure logic marker — but the player should see that the act opened. */
            if (apId == 46200) {
                ShowNotify("Act complete — the way onward is open!");
                ItemLogAddA(0, 2, "Act progress", apSrc);
                Log("AP ITEM: Boss Token (46200) from loc %d — act progress\n",
                    unlockLoc);
                continue;
            }

            /* 1.8.0 NEW: Handle gate-keys FIRST (46101-46158) */
            {
                int gDiff, gSlot;
                if (GateKey_FromAPId(apId, &gDiff, &gSlot)) {
                    /* PROGRESSIVE act keys. */
                    int actOfSlot = (gSlot <  4) ? 1 : (gSlot <  8) ? 2 :
                                    (gSlot < 12) ? 3 : (gSlot < 14) ? 4 : 5;
                    int gatesInAct = (actOfSlot == 4) ? 2 : 4;

                    if (unlockLoc > 0) {
                        /* Derive the count from the ALREADY-PERSISTED gate flags rather than a runtime counter: the location set is saved to disk, so a counter starting at 0 after a restart would never rebuild and the 4th key would re-open gate 1. */
                        int already = 0;
                        for (int gi = 0; gi < gatesInAct; gi++) {
                            int s = GateKey_SlotFromActGate(actOfSlot, gi);
                            if (s >= 0 && g_gateKeyReceived[gDiff][s]) already++;
                        }
                        if (!IsFillerLocApplied(unlockLoc, FillerSenderHash(unlockSender))) {
                            MarkFillerLocApplied(unlockLoc, FillerSenderHash(unlockSender));
                            int want = already + 1;
                            if (want > gatesInAct) want = gatesInAct;
                            /* Open the first `want` gates. */
                            for (int gi = 0; gi < want; gi++) {
                                int s = GateKey_SlotFromActGate(actOfSlot, gi);
                                if (s >= 0) UnlockGateKey(gDiff, s);
                            }
                            Log("AP GATE KEY (progressive): act=%d diff=%d now %d/%d gates (loc=%d)\n",
                                actOfSlot, gDiff, want, gatesInAct, unlockLoc);
                        } else {
                            /* Replayed copy — re-assert what we already own, never grow. */
                            for (int gi = 0; gi < already; gi++) {
                                int s = GateKey_SlotFromActGate(actOfSlot, gi);
                                if (s >= 0) UnlockGateKey(gDiff, s);
                            }
                        }
                    } else {
                        /* a key with NO location. */
                        static int  s_llBase[3][6];      /* gates owned at first sight */
                        static int  s_llSeen[3][6];      /* location-less keys this session */
                        static BOOL s_llInit[3][6];
                        if (gDiff >= 0 && gDiff < 3 && actOfSlot >= 1 && actOfSlot <= 5) {
                            if (!s_llInit[gDiff][actOfSlot]) {
                                int owned = 0;
                                for (int gi = 0; gi < gatesInAct; gi++) {
                                    int s = GateKey_SlotFromActGate(actOfSlot, gi);
                                    if (s >= 0 && g_gateKeyReceived[gDiff][s]) owned++;
                                }
                                s_llBase[gDiff][actOfSlot] = owned;
                                s_llSeen[gDiff][actOfSlot] = 0;
                                s_llInit[gDiff][actOfSlot] = TRUE;
                            }
                            s_llSeen[gDiff][actOfSlot]++;
                            int want = s_llBase[gDiff][actOfSlot] + s_llSeen[gDiff][actOfSlot];
                            if (want > gatesInAct) want = gatesInAct;
                            for (int gi = 0; gi < want; gi++) {
                                int s = GateKey_SlotFromActGate(actOfSlot, gi);
                                if (s >= 0) UnlockGateKey(gDiff, s);
                            }
                            Log("AP GATE KEY (no location, cheated/legacy): act=%d diff=%d "
                                "now %d/%d gates\n", actOfSlot, gDiff, want, gatesInAct);
                        } else {
                            UnlockGateKey(gDiff, gSlot);
                        }
                    }
                    MarkApIdApplied(apId);
                    unlockCount++;
                    continue; /* Gate key handled */
                }
            }

            /* Handle legacy zone keys (46001+) — before skill check */
            {
                int zoneKeyIdx = APItemToZoneKeyIdx(apId);
                if (zoneKeyIdx >= 0) {
                    UnlockZoneKey(zoneKeyIdx);
                    MarkApIdApplied(apId);
                    unlockCount++;
                    continue; /* Zone key handled, skip skill/filler checks */
                }
            }

            /* Handle skill items (45001-45499) */
            if (apId >= 45001 && apId < 45500) {
                int skillId = apId - 45000;
                BOOL foundInPool = FALSE;       /* B2 fix */
                for (int i = 0; i < g_poolCount; i++) {
                    if (g_skillDB[g_pool[i].dbIndex].id == skillId) {
                        foundInPool = TRUE;
                        if (!g_pool[i].unlocked) {
                            g_pool[i].unlocked = TRUE;
                            /* 1.7.1 FIX: rebuild the skill tree on next tick so the unlock is visible without reloading the character. */
                            g_slotsDirty = TRUE;
                            g_slotsApplied = FALSE;
                            char msg[160];
                            /* include sender slot in banner if another player found this skill for us. */
                            if (unlockSender[0] != 0 && g_apSlot[0] != 0
                                && _stricmp(unlockSender, g_apSlot) != 0) {
                                _snprintf(msg, sizeof(msg) - 1, "AP: %s unlocked! (from %s)",
                                          g_skillDB[g_pool[i].dbIndex].name, unlockSender);
                                msg[sizeof(msg) - 1] = 0;
                            } else {
                                sprintf(msg, "AP: %s unlocked!", g_skillDB[g_pool[i].dbIndex].name);
                            }
                            ShowNotify(msg);
                            Log("AP UNLOCK: %s (skill %d, AP item %d) from='%s' — tree rebuild queued\n",
                                g_skillDB[g_pool[i].dbIndex].name, skillId, apId,
                                unlockSender[0] ? unlockSender : "(self/unknown)");
                            unlockCount++;
                            /* Item Log: AP skill unlock (inbound) */
                            {
                                char skillNameBuf[64];
                                sprintf(skillNameBuf, "%s unlock", g_skillDB[g_pool[i].dbIndex].name);
                                ItemLogAddA(0, 2, skillNameBuf, apSrc);
                            }
                        }
                        break;
                    }
                }
                /* B2 fix: only mark applied when we actually FOUND the skill in this slot's pool (either freshly unlocked or already unlocked is fine — both mean "I have a slot for this skill so dedup is safe"). */
                if (foundInPool) {
                    if (!IsApIdApplied(apId)) MarkApIdApplied(apId);
                } else {
                    Log("AP SKILL DEFER: apId=%d (skill %d) not in this slot's pool — "
                        "leaving un-dedup'd in case pool changes later\n", apId, skillId);
                }
            }

            /* Handle filler items (45500-45999) — use server-side delivery */
            if (apId >= 45500 && apId < 46000) {
                /* THE dedup for fillers, applied to ALL of them. */
                if (unlockLoc > 0 &&
                    IsFillerLocApplied(unlockLoc, FillerSenderHash(unlockSender))) {
                    Log("AP FILLER LOC DEDUP: location %d already delivered "
                        "(apId=%d) — suppressing replay\n", unlockLoc, apId);
                    continue;
                }
                BOOL filler_handled = TRUE;
                switch (apId) {
                    case 45500: {
                        /* Level-scaled: the amount is level^2 x 100 and the DRAIN
                         * computes it on the game thread, where the character's
                         * level is readable. It is NOT known here.
                         *
                         * When gold went level-scaled the amount moved to the
                         * drain but these two lines stayed behind, still
                         * formatting a now-dead `int gold = 0` — so the player
                         * was told "AP: 0 Gold!" and the item log said
                         * "+0 gold" while 57,600 was actually being granted
                         * (Marco's log has both lines back to back). The drain
                         * announces the real number now; this side only records
                         * WHO sent it, which is all the item log needs. */
                        PendAdd(&g_serverPendingGoldLevels, 1);
                        ItemLogAddA(0, 4, "Gold reward", apSrc);
                        break;
                    }
                    case 45501: PendAdd(&g_serverPendingGold, 2000); ShowNotify("AP: 2000 Gold!");
                                ItemLogAddA(0, 4, "+2000 gold", apSrc); break;
                    case 45502: PendAdd(&g_serverPendingGold, 5000); ShowNotify("AP: 5000 Gold!");
                                ItemLogAddA(0, 4, "+5000 gold", apSrc); break;
                    case 45503: PendAdd(&g_serverPendingStatPts, 5); ShowNotify("AP: 5 Stat Points!");
                                ItemLogAddA(0, 7, "+5 Stat Points", apSrc); break;
                    case 45504: PendAdd(&g_serverPendingSkillPts, 1); ShowNotify("AP: 1 Skill Point!");
                                ItemLogAddA(0, 2, "+1 Skill Point", apSrc); break;
                    case 45505: {
                        /* Two very different things share this id: the ordinary "Trap: Monsters" pool item, and the V1 bridge's DeathLink delivery (same id + ap_deathlink_event.dat with the source's name). */
                        char eventPath[MAX_PATH];
                        GetArchDir(eventPath, MAX_PATH);
                        strcat(eventPath, "ap_deathlink_event.dat");
                        char sourceName[48] = "";
                        FILE* ef = fopen(eventPath, "r");
                        if (ef) {
                            char line[128];
                            while (fgets(line, sizeof(line), ef)) {
                                char tmp[48];
                                if (sscanf(line, "source=%47[^\n]", tmp) == 1) {
                                    strncpy(sourceName, tmp, 47);
                                    sourceName[47] = 0;
                                }
                            }
                            fclose(ef);
                            DeleteFileA(eventPath);
                        }
                        char notifyBuf[128];
                        char logBuf[96];
                        if (sourceName[0]) {
                            /* DeathLink means death (see the pipe-mode handler for the full story: pended to the server tick, suppress window against re-broadcast, trap only as fallback on an unexpected D2Game build). */
                            if (g_cachedPGame && Player()) {
                                g_deathLinkSuppressUntil = GetTickCount() + 60000;
                                g_pendingDeathLinkKill = 1;
                                _snprintf(notifyBuf, sizeof(notifyBuf),
                                    "DEATHLINK: %s died - so do you", sourceName);
                            } else {
                                _snprintf(notifyBuf, sizeof(notifyBuf),
                                    "DEATHLINK: %s died", sourceName);
                            }
                            _snprintf(logBuf, sizeof(logBuf), "DeathLink from %s", sourceName);
                        } else {
                            g_pendingTrapSpawn++;
                            strcpy(notifyBuf, "AP TRAP! Monster incoming!");
                            strcpy(logBuf, "TRAP: Monsters");
                        }
                        notifyBuf[sizeof(notifyBuf) - 1] = 0;
                        logBuf[sizeof(logBuf) - 1] = 0;
                        ShowNotify(notifyBuf);
                        ItemLogAddA(0, 11, logBuf, apSrc);
                        break;
                    }
                    case 45506: g_resetPoints++; ShowNotify("AP: Reset Point!");
                                ItemLogAddA(0, 2, "+1 Reset Point", apSrc); break;

                    /* Point items in several sizes rather than one item that
                     * secretly rolls its own amount. The item NAME is what the
                     * multiworld shows to every other player and what hints are
                     * written against, so "5 Stat Points" handing out 2 would be
                     * a lie told to six other people. Discrete named sizes give
                     * the same variety with an honest label, and let the tier
                     * placement push the big ones into later spheres. */
                    case 45541: PendAdd(&g_serverPendingStatPts, 1);
                                ShowNotify("AP: 1 Stat Point!");
                                ItemLogAddA(0, 7, "+1 Stat Point", apSrc); break;
                    case 45542: PendAdd(&g_serverPendingStatPts, 3);
                                ShowNotify("AP: 3 Stat Points!");
                                ItemLogAddA(0, 7, "+3 Stat Points", apSrc); break;
                    case 45543: PendAdd(&g_serverPendingStatPts, 10);
                                ShowNotify("AP: 10 Stat Points!");
                                ItemLogAddA(0, 7, "+10 Stat Points", apSrc); break;
                    /* Skill points stay small. The stat that holds them is 8-bit
                     * and we have already wiped a character once by overflowing
                     * it; 3 at a time keeps a long run well clear of the edge. */
                    case 45544: PendAdd(&g_serverPendingSkillPts, 2);
                                ShowNotify("AP: 2 Skill Points!");
                                ItemLogAddA(0, 2, "+2 Skill Points", apSrc); break;
                    case 45545: PendAdd(&g_serverPendingSkillPts, 3);
                                ShowNotify("AP: 3 Skill Points!");
                                ItemLogAddA(0, 2, "+3 Skill Points", apSrc); break;
                    case 45546: g_resetPoints += 3; ShowNotify("AP: 3 Reset Points!");
                                ItemLogAddA(0, 2, "+3 Reset Points", apSrc); break;

                    /* Object traps. Queued only — the spawn needs a room and a
                     * subtile, so ObjTraps_Tick drains these on the game thread
                     * and holds them until the player is out of town. */
                    case 45547: g_pendingTrapBarrels++;
                                ItemLogAddA(0, 5, "Trap: barrel field", apSrc); break;
                    case 45548: g_pendingTrapUrns++;
                                ItemLogAddA(0, 5, "Trap: urn garden", apSrc); break;
                    case 45549: g_pendingTrapTreasure++;
                                ItemLogAddA(0, 5, "Trap: false treasure", apSrc); break;
                    case 45550: g_pendingTrapJunk++;
                                ItemLogAddA(0, 5, "Trap: junk backpack", apSrc); break;

                    /* Slot-filtered set and unique drops. Ids run set slots
                     * 0-7 then unique slots 0-7, so the slot is (id-45551)%8
                     * and the kind is which half the id falls in. Adding a
                     * ninth slot means shifting the unique block, which is
                     * why items.py carries the same warning. */
                    case 45551: case 45552: case 45553: case 45554:
                    case 45555: case 45556: case 45557: case 45558:
                    case 45559: case 45560: case 45561: case 45562:
                    case 45563: case 45564: case 45565: case 45566: {
                        int d       = apId - 45551;
                        int slot    = d % ARCH_SLOT_COUNT;
                        BOOL unique = (d >= ARCH_SLOT_COUNT);
                        int idx     = unique ? Quests_PickUniqueBySlot(slot)
                                             : Quests_PickSetPieceBySlot(slot);
                        if (idx < 0) {
                            /* Slot came up empty — a modded txt, or a slot with
                             * no rows at all. Drop something rather than
                             * nothing; a silent no-op is indistinguishable from
                             * a lost item. */
                            Log("AP: slot %d empty for %s — falling back to "
                                "unfiltered roll\n", slot,
                                unique ? "unique" : "set");
                            if (unique) {
                                if (!g_uniqueCatalogLoaded) Quests_LoadUniqueCatalog();
                                int n = g_uniqueCatalogCount > 0 ? g_uniqueCatalogCount : 1;
                                idx = rand() % n;
                            } else {
                                idx = rand() % 127;
                            }
                        }
                        Quests_QueueSpecificDrop(unique ? REWARD_DROP_UNIQUE
                                                        : REWARD_DROP_SET,
                                                 idx, apSrc);
                        break;
                    }
                    /* 1.7.1 LEGACY: generic boss loot drop — kept for in-flight seeds from before the 1.9.0 redesign (which ships specific boss IDs 45514-45518). */
                    case 45507:
                        g_pendingLootDrop++;
                        g_pendingLootBossId = -1; /* leave random */
                        Log("AP LOOT DROP (legacy generic): pending=%d\n", g_pendingLootDrop);
                        ItemLogAddA(0, 9, "Boss loot drop", apSrc);
                        break;

                    /* 1.9.0 NEW: Experience filler — DLL injects via the same fnAddStat(statId 13) path the XP Multiplier feature uses. */
                    case 45508: {
                        /* one grant = one level, resolved at drain. */
                        int xp = 0;
                        /* C4/C9 — route flat XP through the pending queue instead of applying it here. */
                        PendAdd(&g_serverPendingXpLevels, 1);
                        ShowNotify("AP: Experience - one level!");
                        ItemLogAddA(0, 7, "+1 level of XP", apSrc);
                        break;
                    }

                    /* 1.9.0 NEW: typed trap variants. */
                    case 45511:
                        g_pendingTrapSlow++;
                        ShowNotify("AP TRAP! You feel sluggish...");
                        ItemLogAddA(0, 11, "Trap: Slow", apSrc);
                        break;
                    case 45512:
                        g_pendingTrapWeaken++;
                        ShowNotify("AP TRAP! Your defenses crumble!");
                        ItemLogAddA(0, 11, "Trap: Weaken", apSrc);
                        break;
                    case 45513:
                        g_pendingTrapPoison++;
                        ShowNotify("AP TRAP! Poison seeps in!");
                        ItemLogAddA(0, 11, "Trap: Poison", apSrc);
                        break;

                    /* 1.9.0 NEW: specific boss-loot drops (Andariel..Baal). */
                    case 45514: case 45515: case 45516: case 45517: case 45518: {
                        int bossIdx = apId - 45514;  /* 0..4 */
                        g_pendingLootDrop++;
                        g_pendingLootBossId = bossIdx;
                        char ln[48]; _snprintf(ln, sizeof(ln), "AP: %s Loot incoming!",
                                               g_bossLootNames[bossIdx]);
                        ShowNotify(ln);
                        char lg[48]; _snprintf(lg, sizeof(lg), "Drop: %s Loot",
                                               g_bossLootNames[bossIdx]);
                        ItemLogAddA(0, 9, lg, apSrc);
                        Log("AP LOOT DROP queued: boss=%s pending=%d\n",
                            g_bossLootNames[bossIdx], g_pendingLootDrop);
                        break;
                    }

                    /* 1.9.0 NEW: specific-item drops (charm/set/unique). */
                    case 45519: case 45520: case 45521: {
                        /* defensive dedup. Maegis bug: "first magic charm gave 2, first unique gave 2". The bridge can re-send the same filler ID during connect/reconnect, and both arrivals would queue a separate drop. We suppress duplicate receives of the same filler ID within a 5-second window. 3.4.7 — the across-session location dedup that used to live here has moved to the top of the filler branch, where every filler passes through it rather than only these three. What is left is the 5-second window, which still guards the one case the shared rule cannot: two copies arriving in the same burst with no location to tell them apart. */
                        static struct { int apId; DWORD when; } s_recentFillers[8] = {0};
                        DWORD nowMs = GetTickCount();
                        BOOL isDuplicate = FALSE;

                        /* Intra-session 5-second window — ONLY for legacy lines with NO location info. */
                        if (!isDuplicate && unlockLoc <= 0) {
                            for (int i = 0; i < 8; i++) {
                                if (s_recentFillers[i].apId == apId &&
                                    nowMs - s_recentFillers[i].when < 5000) {
                                    isDuplicate = TRUE;
                                    Log("AP FILLER DEDUP: suppressing duplicate apId=%d within 5s window (no loc)\n", apId);
                                    break;
                                }
                            }
                        }

                        if (!isDuplicate) {
                            /* Find oldest slot to overwrite */
                            int oldest = 0;
                            for (int i = 1; i < 8; i++) {
                                if (s_recentFillers[i].when < s_recentFillers[oldest].when) oldest = i;
                            }
                            s_recentFillers[oldest].apId = apId;
                            s_recentFillers[oldest].when = nowMs;

                            switch (apId) {
                                case 45519: {
                                    int idx = rand() % 3;
                                    Quests_QueueSpecificDrop(REWARD_DROP_CHARM, idx, apSrc);
                                    break;
                                }
                                case 45520: {
                                    int idx = rand() % 127;
                                    Quests_QueueSpecificDrop(REWARD_DROP_SET, idx, apSrc);
                                    break;
                                }
                                case 45521: {
                                    if (!g_uniqueCatalogLoaded) Quests_LoadUniqueCatalog();
                                    int n = g_uniqueCatalogCount > 0 ? g_uniqueCatalogCount : 1;
                                    int idx = rand() % n;
                                    Quests_QueueSpecificDrop(REWARD_DROP_UNIQUE, idx, apSrc);
                                    break;
                                }
                            }

                            /* the location is persisted by the shared tail below, which does it for every filler. */
                        } else {
                            /* Swallowed by the 5-second window: do not let the tail record it, or a legitimate second copy from a different check would be locked out. */
                            filler_handled = FALSE;
                        }
                        break;
                    }

                    /* Ordinary-item filler bundles. The id order matches
                     * g_fillerBatches, so a new bundle needs only a new entry
                     * in that table and one more id on this range. */
                    case 45522: case 45523: case 45524: case 45525:
                    case 45526: case 45527: case 45528: case 45529:
                    case 45530: case 45531: case 45532: case 45533:
                    case 45534: case 45535: case 45536: case 45537:
                    case 45538: case 45539: case 45540: {
                        int batch = apId - 45522;
                        Quests_QueueSpecificDrop(REWARD_DROP_BATCH, batch, apSrc);
                        break;
                    }

                    default:
                        Log("AP: Unknown filler item %d\n", apId);
                        filler_handled = FALSE;
                        break;
                }
                /* do NOT mark filler apId as applied. */
                if (filler_handled) {
                    if (unlockLoc > 0) {
                        MarkFillerLocApplied(unlockLoc, FillerSenderHash(unlockSender));
                    } else if (!IsApIdApplied(apId)) {
                        MarkApIdApplied(apId);
                        Log("AP FILLER: apId=%d had no location — recorded by id so "
                            "it is not delivered again on the next connect\n", apId);
                    }
                    unlockCount++;
                }
            }
        }
    }
    fclose(f);

    /* 1.8.0 FIX: delete the .processing copy (not the live ap_unlocks.dat which may already contain new unlocks from the bridge). */
    DeleteFileA(processing);
    if (unlockCount > 0) {
        SaveStateFile();
        Log("AP: Processed %d unlocks, state saved\n", unlockCount);
    }
}

static void HandleAPPanelClick(int mx, int my) {
    /* Read panel position from INI to match rendering */
    char iniP[MAX_PATH]; GetArchDir(iniP, MAX_PATH); strcat(iniP, "d2arch.ini");
    int px = GetPrivateProfileIntA("layout", "APX", 300, iniP);
    int py = GetPrivateProfileIntA("layout", "APY", 300, iniP);
    int pw = 210;
    int fy = py + 16;
    if (mx >= px + 32 && mx <= px + pw && my >= fy && my <= fy + 14) { g_apFocusField = 0; return; }
    fy += 16;
    if (mx >= px + 32 && mx <= px + pw && my >= fy && my <= fy + 14) { g_apFocusField = 1; return; }
    fy += 16;
    if (mx >= px + 32 && mx <= px + pw && my >= fy && my <= fy + 14) { g_apFocusField = 2; return; }
    fy += 18;
    int bx = px + 20, bw = pw - 40, bh = 20;
    if (mx >= bx && mx <= bx + bw && my >= fy && my <= fy + bh) {
        if (g_apConnected) {
            WriteAPCommand("disconnect");
            g_apConnected = FALSE;
            strcpy(g_apStatus, "Disconnecting...");
        } else if (g_apIP[0] && g_apSlot[0]) {
            Log("AP CONNECT: starting bridge and writing command (IP=%s Slot=%s)\n", g_apIP, g_apSlot);
            /* 1.9.5 Gap 4 — slot-change collision check */
            CheckSlotChangeOnConnect();
            StartAPBridge();
            WriteAPCommand("connect");
            SaveAPConfig();
            /* set g_apPolling so PollAPStatus runs; g_apMode stays FALSE until auth actually succeeds. */
            g_apPolling = TRUE;
            strcpy(g_apStatus, "Connecting...");
        } else {
            Log("AP CONNECT: no IP or Slot entered\n");
            strcpy(g_apStatus, "Enter IP and Slot");
        }
        return;
    }
    g_apFocusField = -1;
}

static BOOL HandleAPKeyInput(UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (g_apFocusField < 0) return FALSE;
    if (msg == WM_KEYDOWN) {
        if (wp == VK_TAB) { g_apFocusField = (g_apFocusField + 1) % 3; return TRUE; }
        if (wp == VK_RETURN || wp == VK_ESCAPE) { g_apFocusField = -1; return TRUE; }
        if (wp == VK_BACK) {
            char* field = (g_apFocusField == 0) ? g_apIP : (g_apFocusField == 1) ? g_apSlot : g_apPassword;
            int len = (int)strlen(field); if (len > 0) field[len - 1] = 0; return TRUE;
        }
    }
    if (msg == WM_CHAR) {
        char ch = (char)wp; if (ch < 32 || ch > 126) return TRUE;
        char* field = (g_apFocusField == 0) ? g_apIP : (g_apFocusField == 1) ? g_apSlot : g_apPassword;
        int maxLen = (g_apFocusField == 0) ? 62 : 30;
        int len = (int)strlen(field);
        if (len < maxLen) { field[len] = ch; field[len + 1] = 0; }
        return TRUE;
    }
    return FALSE;
}

static void RenderAPPanelD2(void) {
    if (!fnRect || !fnText || !fnFont) return;

    /* Only show on menu screens (NOT in-game) */
    DWORD now = GetTickCount();
    if (g_lastDrawGameUITime && (now - g_lastDrawGameUITime) < 500) return;

    /* 1.8.5 FIX: poll AP status here too. */
    PollAPStatus();

    /* Simple status display — no input fields, AP setup is in the launcher now */
    if (!g_apConnected) return; /* Don't show anything if not connected */

    int px = 620, py = 5;
    fnFont(0);
    DrawText2("AP", px, py + 11, 2, 0);
    fnFont(6);
    DrawText2(g_apStatus, px + 20, py + 11, 2, 0);
}

/* EndScene hook asm */
static void __declspec(naked) EndSceneHook(void) {
    __asm {
        pushad
        call RenderAPPanelD2
        popad
        jmp [g_endSceneTrampolinePtr]
    }
}

/* ARCHIPELAGO SKILL TREE PANEL — 30 skills, single page, no tabs Replaces the vanilla 3-tab skill tree entirely. */

/* (g_sklTreeBg/g_sklIconFile/g_sklTreeLoaded deleted 2026-08-29 together with
 * the never-called LoadSkillTreeAssets in d2arch_drawall.c.) */

/* D2CMP ordinal 10024: CelFileNormalize Converts raw DC6 file data (read from disk) into a D2CellFileStrc with resolved pointers. */
typedef void (__stdcall *CelFileNormalize_t)(void* pRawFile, void** ppOutFile,
                                              const char* szFile, int nLine,
                                              int nSpecVersion, int nUnused);
static CelFileNormalize_t g_fnCelNormalize = NULL;

/* Fog ordinal 10042: FOG_Alloc (__fastcall) Fog ordinal 10043: FOG_Free (__fastcall) CelFileNormalize expects memory from Fog's allocator because it may call FOG_Free internally on error paths. */
typedef void* (__fastcall *FogAlloc_t)(int nSize, const char* szFile, int nLine, int nFlags);
typedef void  (__fastcall *FogFree_t)(void* pFree, const char* szFile, int nLine, int nFlags);
static FogAlloc_t g_fnFogAlloc = NULL;
static FogFree_t  g_fnFogFree  = NULL;

/* Read a DC6 file from disk and normalize it into a D2CellFileStrc. */
static void* LoadDC6FromDisk(const char* szPath) {
    FILE* fp;
    long fileSize;
    void* pRawBuf;
    void* pCellFile = NULL;

    /* Resolve D2CMP CelFileNormalize (ordinal 10024) and Fog allocator once */
    if (!g_fnCelNormalize) {
        HMODULE hCMP = GetModuleHandleA("D2CMP.dll");
        HMODULE hFog = GetModuleHandleA("Fog.dll");
        if (hCMP) {
            g_fnCelNormalize = (CelFileNormalize_t)GetProcAddress(hCMP, (LPCSTR)10024);
        }
        if (hFog) {
            g_fnFogAlloc = (FogAlloc_t)GetProcAddress(hFog, (LPCSTR)10042);
            g_fnFogFree  = (FogFree_t)GetProcAddress(hFog, (LPCSTR)10043);
        }
        if (!g_fnCelNormalize) {
            Log("LoadDC6: FAILED to get D2CMP ordinal 10024 (CelFileNormalize)\n");
            return NULL;
        }
        if (!g_fnFogAlloc) {
            Log("LoadDC6: FAILED to get Fog ordinal 10042 (Alloc) -- using HeapAlloc fallback\n");
        }
        Log("LoadDC6: D2CMP CelFileNormalize at %p, FogAlloc at %p\n",
            g_fnCelNormalize, g_fnFogAlloc);
    }

    /* Open and read the DC6 file */
    fp = fopen(szPath, "rb");
    if (!fp) {
        Log("LoadDC6: fopen FAILED: %s\n", szPath);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fileSize < 24) { /* DC6 header is at minimum 24 bytes */
        Log("LoadDC6: file too small (%ld bytes): %s\n", fileSize, szPath);
        fclose(fp);
        return NULL;
    }

    /* Allocate using Fog's allocator (same allocator D2's archive system uses). */
    if (g_fnFogAlloc) {
        pRawBuf = g_fnFogAlloc((int)fileSize, "d2arch.c", __LINE__, 0);
    } else {
        pRawBuf = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)fileSize);
    }
    if (!pRawBuf) {
        Log("LoadDC6: Alloc FAILED (%ld bytes)\n", fileSize);
        fclose(fp);
        return NULL;
    }

    if ((long)fread(pRawBuf, 1, (size_t)fileSize, fp) != fileSize) {
        Log("LoadDC6: fread FAILED (expected %ld bytes)\n", fileSize);
        if (g_fnFogFree) g_fnFogFree(pRawBuf, "d2arch.c", __LINE__, 0);
        else HeapFree(GetProcessHeap(), 0, pRawBuf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    Log("LoadDC6: Read %ld bytes from %s\n", fileSize, szPath);

    /* Call CelFileNormalize -- this converts raw DC6 data into a D2CellFileStrc with resolved D2GfxCellStrc pointers. */
    __try {
        g_fnCelNormalize(pRawBuf, &pCellFile, "d2arch.c", __LINE__, -1, 0);
    } __except(1) {
        Log("LoadDC6: CelFileNormalize CRASHED for %s\n", szPath);
        pCellFile = NULL;
    }

    /* Note: We do NOT free pRawBuf -- CelFileNormalize may have stored it as the output (pCellFile == pRawBuf after in-place normalization), or it allocated new memory. */

    if (pCellFile) {
        /* DIAGNOSTIC: raw vs normalised. Equal = in-place (one Fog block);
         * different = D2CMP allocated the D2CellFileStrc itself, in which
         * case D2CMP may well own — and free — that block at teardown. */
        Log("LoadDC6: SUCCESS -> CellFile=%p raw=%p %s (%s)\n", pCellFile, pRawBuf,
            (pCellFile == pRawBuf) ? "IN-PLACE" : "SEPARATE", szPath);
    } else {
        Log("LoadDC6: CelFileNormalize returned NULL for %s\n", szPath);
        if (g_fnFogFree) g_fnFogFree(pRawBuf, "d2arch.c", __LINE__, 0);
        else HeapFree(GetProcessHeap(), 0, pRawBuf);
    }

    return pCellFile;
}

/* Free a DC6 we loaded from disk, with the allocator that gave it to us.
 * ⚠ Not EdCelFree/fnCelFree (D2Win 10041): that is the free for ARCHIVE
 * cels. This block came from Fog 10042 (or HeapAlloc when Fog was absent),
 * so it goes back the same way. NULLs the slot. */
static void DiskCelFree(void** pSlot) {
    if (!pSlot || !*pSlot) return;
    __try {
        if (g_fnFogFree) g_fnFogFree(*pSlot, "d2arch.c", __LINE__, 0);
        else HeapFree(GetProcessHeap(), 0, *pSlot);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    *pSlot = NULL;
}

/* Build the full path to a DC6 file in the runtime data directory. */
static BOOL BuildDC6Path(char* out, int outSize, const char* subPath) {
    char exeDir[MAX_PATH];
    GetModuleFileNameA(NULL, exeDir, MAX_PATH);
    {
        char* sl = strrchr(exeDir, '\\');
        if (sl) *(sl + 1) = 0;
    }
    _snprintf(out, outSize - 1, "%sdata\\global\\ui\\SPELLS\\%s", exeDir, subPath);
    out[outSize - 1] = 0;

    /* Check if file exists */
    if (GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES) {
        Log("BuildDC6Path: NOT FOUND: %s\n", out);
        return FALSE;
    }
    return TRUE;
}
