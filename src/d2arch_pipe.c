/* V2 LAUNCHER NAMED-PIPE TRANSPORT (DLL <-> launcher IPC) The V2 launcher hosts the Archipelago WebSocket client itself and exposes a named pipe server (\\.\pipe\d2arch_v2_<launcher_pid>) instead of spawning the V1 ap_bridge.exe Python process. */

#define AP_PIPE_CONNECT_RETRIES  30     /* x 200ms = ~6s connect window */
#define AP_PIPE_CONNECT_WAIT_MS  200
#define AP_PIPE_QCAP             128    /* outbound queue slots */
#define AP_PIPE_QMSG             192    /* max bytes per outbound line */
#define AP_PIPE_SENT_CAP         4096   /* CHECK dedup set (per session) */
#define AP_PIPE_SENDER_MAX       39     /* unlock-line sender cap -- PollAPUnlocks reads it with %39[^|], longer is truncated at parse time anyway */
#define AP_PIPE_DL_SRC_MAX       48     /* DeathLink source buffer (mirrors the V1 sourceName[48] in the 45505 trap) */
#define AP_PIPE_DL_CAUSE_MAX     96     /* DeathLink cause buffer */

static char             g_apPipeName[128] = {0};
static volatile HANDLE  g_apPipeHandle    = INVALID_HANDLE_VALUE;
static volatile LONG    g_apPipeModeFlag  = 0;   /* latched 1 after first connect */
static volatile LONG    g_apPipeBroken    = 0;   /* 1 = transport dead */
static volatile LONG    g_apPipeStateEvt  = -1;  /* -1 none / 0 discon / 1 conn */

/* Inbound DeathLink event slot. */
static CRITICAL_SECTION g_apPipeDeathLock;
static char             g_apPipeDeathSource[AP_PIPE_DL_SRC_MAX]  = {0};
static char             g_apPipeDeathCause[AP_PIPE_DL_CAUSE_MAX] = {0};
static volatile LONG    g_apPipeDeathEvt = 0;    /* 1 = event pending */

/* Outbound queue (game thread appends, writer thread drains). */
static CRITICAL_SECTION g_apPipeQLock;
static char             g_apPipeQ[AP_PIPE_QCAP][AP_PIPE_QMSG];
static int              g_apPipeQHead  = 0;
static int              g_apPipeQCount = 0;
static HANDLE           g_apPipeQEvt   = NULL;   /* auto-reset: work available */

/* Forced-check queue (launcher "Cheat" button in standalone). */
#define AP_PIPE_FORCE_CAP 32
static volatile LONG g_apPipeForceQ[AP_PIPE_FORCE_CAP][2];   /* qid, diff */
static volatile LONG g_apPipeForceCount = 0;

static void ApPipe_QueueForceCheck(int qid, int diff) {
    LONG n = g_apPipeForceCount;
    if (n < 0 || n >= AP_PIPE_FORCE_CAP) return;             /* full -- drop */
    g_apPipeForceQ[n][0] = qid;
    g_apPipeForceQ[n][1] = diff;
    InterlockedExchange(&g_apPipeForceCount, n + 1);
}

/* Game thread only. Returns TRUE and fills the pair when one was waiting. */
static BOOL ApPipe_PopForceCheck(int* qid, int* diff) {
    LONG n = g_apPipeForceCount;
    if (n <= 0) return FALSE;
    *qid  = (int)g_apPipeForceQ[0][0];
    *diff = (int)g_apPipeForceQ[0][1];
    for (LONG i = 1; i < n; i++) {
        g_apPipeForceQ[i - 1][0] = g_apPipeForceQ[i][0];
        g_apPipeForceQ[i - 1][1] = g_apPipeForceQ[i][1];
    }
    InterlockedExchange(&g_apPipeForceCount, n - 1);
    return TRUE;
}

/* CHECK dedup -- location ids already forwarded this session. */
static long g_apPipeSentChecks[AP_PIPE_SENT_CAP];
static int  g_apPipeSentCount = 0;

/* TRUE once the pipe transport has connected at least once this session. */
static BOOL ApPipe_Mode(void) {
    return g_apPipeModeFlag != 0;
}

/* TRUE while the pipe is currently usable for sends. */
static BOOL ApPipe_Active(void) {
    return g_apPipeHandle != INVALID_HANDLE_VALUE && !g_apPipeBroken;
}

/* Consume the latest pending STATE event. */
static int ApPipe_PollStateEvent(void) {
    return (int)InterlockedExchange(&g_apPipeStateEvt, -1);
}

static void ApPipe_PushStateEvent(int state) {
    InterlockedExchange(&g_apPipeStateEvt, (LONG)state);
}

/* Consume the pending inbound DeathLink event, if any. */
static BOOL ApPipe_PollDeathLinkEvent(char* srcOut, int srcCap,
                                      char* causeOut, int causeCap) {
    if (!ApPipe_Mode()) return FALSE;
    if (!srcOut || srcCap <= 0 || !causeOut || causeCap <= 0) return FALSE;
    if (InterlockedExchange(&g_apPipeDeathEvt, 0) == 0) return FALSE;
    EnterCriticalSection(&g_apPipeDeathLock);
    strncpy(srcOut, g_apPipeDeathSource, srcCap - 1);
    srcOut[srcCap - 1] = 0;
    strncpy(causeOut, g_apPipeDeathCause, causeCap - 1);
    causeOut[causeCap - 1] = 0;
    LeaveCriticalSection(&g_apPipeDeathLock);
    return TRUE;
}

/* Queue one outbound line ('\n' appended here). */
/* Returns TRUE if the line was queued, FALSE if it was dropped (inactive transport or full queue). */
static BOOL ApPipe_QueueLine(const char* msg) {
    if (!ApPipe_Active() || !msg || !msg[0]) return FALSE;
    EnterCriticalSection(&g_apPipeQLock);
    if (g_apPipeQCount >= AP_PIPE_QCAP) {
        LeaveCriticalSection(&g_apPipeQLock);
        Log("AP PIPE: send queue full -- dropped '%.24s'\n", msg);
        return FALSE;
    }
    {
        int tail = (g_apPipeQHead + g_apPipeQCount) % AP_PIPE_QCAP;
        int len = (int)strlen(msg);
        if (len > AP_PIPE_QMSG - 2) len = AP_PIPE_QMSG - 2;  /* keep room for \n + NUL */
        memcpy(g_apPipeQ[tail], msg, len);
        g_apPipeQ[tail][len]     = '\n';
        g_apPipeQ[tail][len + 1] = 0;
        g_apPipeQCount++;
    }
    LeaveCriticalSection(&g_apPipeQLock);
    if (g_apPipeQEvt) SetEvent(g_apPipeQEvt);
    return TRUE;
}

/* GOAL -- player completed the Archipelago goal. */
static void ApPipe_SendGoal(void) {
    if (!ApPipe_Mode()) return;
    ApPipe_QueueLine("GOAL");
    Log("AP PIPE: GOAL sent\n");
}

/* LOG:<text> -- diagnostic line for the launcher's AP log panel. */
static void ApPipe_SendLog(const char* text) {
    char buf[AP_PIPE_QMSG];
    if (!ApPipe_Mode() || !text) return;
    _snprintf(buf, sizeof(buf), "LOG:%s", text);
    buf[sizeof(buf) - 1] = 0;
    ApPipe_QueueLine(buf);
}

/* DEATH:<cause> -- local player died (DeathLink outbound). */
/* CHAR:<name> -- a character just loaded. */
void ApPipe_SendChar(const char* name) {
    char buf[AP_PIPE_QMSG];
    if (!ApPipe_Mode() || !name || !name[0]) return;
    _snprintf(buf, sizeof(buf), "CHAR:%s", name);
    buf[sizeof(buf) - 1] = 0;
    ApPipe_QueueLine(buf);
    Log("AP PIPE: %s sent — launcher will replay this slot's items for it\n", buf);
}

static void ApPipe_SendDeath(const char* cause) {
    char buf[AP_PIPE_QMSG];
    if (!ApPipe_Mode()) return;
    _snprintf(buf, sizeof(buf), "DEATH:%s",
              (cause && cause[0]) ? cause : "died");
    buf[sizeof(buf) - 1] = 0;
    ApPipe_QueueLine(buf);
    Log("AP PIPE: %s sent\n", buf);
}

static BOOL ApPipe_CheckAlreadySent(long locId) {
    for (int i = 0; i < g_apPipeSentCount; i++) {
        if (g_apPipeSentChecks[i] == locId) return TRUE;
    }
    return FALSE;
}

static void ApPipe_MarkCheckSent(long locId) {
    if (g_apPipeSentCount >= AP_PIPE_SENT_CAP) return; /* set full: re-sends are harmless */
    g_apPipeSentChecks[g_apPipeSentCount++] = locId;
}

/* Forward newly-fired locations to the launcher. */
static void ApPipe_SendChecksFile(const char* checksPath) {
    if (!ApPipe_Mode() || !ApPipe_Active()) return;
    FILE* f = fopen(checksPath, "r");
    if (!f) return;

    char line[64];
    char batch[AP_PIPE_QMSG];
    long batchIds[64];            /* locIds in the current batch, marked sent only after the queue accepts them */
    int  blen = 0, nBatch = 0, sent = 0;
    const int BATCH_MAX = (int)(sizeof(batchIds) / sizeof(batchIds[0]));
    batch[0] = 0;

    while (fgets(line, sizeof(line), f)) {
        int n = 0;
        if (sscanf(line, "check=%d", &n) != 1 || n <= 0) continue;
        long locId = (long)LOCATION_BASE + n;
        if (ApPipe_CheckAlreadySent(locId)) continue;

        char idStr[16];
        _snprintf(idStr, sizeof(idStr), "%ld", locId);
        idStr[sizeof(idStr) - 1] = 0;
        int idLen = (int)strlen(idStr);

        /* Flush the batch when the next id would overflow the line ("CHECK:" prefix + payload + '\n' must fit one queue slot) or the batch-id array is full. */
        if (blen > 0 && (6 + blen + 1 + idLen >= AP_PIPE_QMSG - 2 || nBatch >= BATCH_MAX)) {
            char out[AP_PIPE_QMSG];
            _snprintf(out, sizeof(out), "CHECK:%s", batch);
            out[sizeof(out) - 1] = 0;
            /* mark these locations delivered ONLY if the queue accepted the line. */
            if (ApPipe_QueueLine(out)) {
                for (int b = 0; b < nBatch; b++) ApPipe_MarkCheckSent(batchIds[b]);
                sent += nBatch;
            }
            blen = 0; nBatch = 0; batch[0] = 0;
        }
        if (blen > 0) batch[blen++] = ',';
        memcpy(batch + blen, idStr, idLen);
        blen += idLen;
        batch[blen] = 0;
        batchIds[nBatch++] = locId;
    }
    fclose(f);

    if (blen > 0) {
        char out[AP_PIPE_QMSG];
        _snprintf(out, sizeof(out), "CHECK:%s", batch);
        out[sizeof(out) - 1] = 0;
        if (ApPipe_QueueLine(out)) {
            for (int b = 0; b < nBatch; b++) ApPipe_MarkCheckSent(batchIds[b]);
            sent += nBatch;
        }
    }
    if (sent > 0) {
        Log("AP PIPE: forwarded %d new location check(s) to launcher\n", sent);
    }
}

/* pipe I/O internals (reader/writer threads) */

/* Synchronous-style overlapped read/write. */
static BOOL ApPipe_OvRead(HANDLE h, void* buf, DWORD cap, DWORD* outGot, HANDLE evt) {
    OVERLAPPED ov;
    memset(&ov, 0, sizeof(ov));
    ov.hEvent = evt;
    ResetEvent(evt);
    if (!ReadFile(h, buf, cap, NULL, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) return FALSE;
    }
    return GetOverlappedResult(h, &ov, outGot, TRUE);
}

static BOOL ApPipe_OvWrite(HANDLE h, const void* buf, DWORD len, HANDLE evt) {
    OVERLAPPED ov;
    DWORD written = 0;
    memset(&ov, 0, sizeof(ov));
    ov.hEvent = evt;
    ResetEvent(evt);
    if (!WriteFile(h, buf, len, NULL, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) return FALSE;
    }
    if (!GetOverlappedResult(h, &ov, &written, TRUE)) return FALSE;
    return written == len;
}

/* Copy a pipe-delimited display-string field out of an inbound line: trims surrounding spaces, drops CR/LF, and rewrites '|' to '_' so the value can never grow extra columns in the unlock-line format (the launcher contract says senders contain no '|', but a torn frame must not be able to smuggle one). */
static void ApPipe_CopyDisplayField(char* dst, int dstCap,
                                    const char* src, int srcLen) {
    int n = 0;
    if (!dst || dstCap <= 0) return;
    while (srcLen > 0 && (*src == ' ' || *src == '\t')) { src++; srcLen--; }
    for (int i = 0; i < srcLen && n < dstCap - 1; i++) {
        char c = src[i];
        if (c == '\r' || c == '\n') continue;
        if (c == '|') c = '_';
        dst[n++] = c;
    }
    while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '\t')) n--;
    dst[n] = 0;
}

/* Parse a strictly-numeric field (digits only, optional surrounding spaces). */
static long ApPipe_ParseNumericField(const char* s) {
    long v = 0;
    int digits = 0;
    while (*s == ' ' || *s == '\t') s++;
    for (; *s >= '0' && *s <= '9'; s++) {
        if (v > (0x7FFFFFFFL - 9) / 10) return 0;
        v = v * 10 + (*s - '0');
        digits++;
    }
    while (*s == ' ' || *s == '\t') s++;
    if (digits == 0 || *s != 0) return 0;   /* empty or trailing junk */
    return v;
}

/* Append a received AP item to ap_unlocks.dat. */
static void ApPipe_AppendUnlock(long itemId, const char* sender, long locId) {
    char dir[MAX_PATH], path[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    _snprintf(path, MAX_PATH, "%sap_unlocks.dat", dir);
    path[MAX_PATH - 1] = 0;

    FILE* f = NULL;
    for (int i = 0; i < 5 && !f; i++) {
        f = fopen(path, "a");
        if (!f) Sleep(20);
    }
    if (!f) {
        Log("AP PIPE: FAILED to append unlock %ld to %s -- item NOT queued\n",
            itemId, path);
        return;
    }
    if (sender && sender[0]) {
        if (locId > 0) fprintf(f, "unlock=%ld|%s|%ld\n", itemId, sender, locId);
        else           fprintf(f, "unlock=%ld|%s\n", itemId, sender);
    } else {
        fprintf(f, "unlock=%ld\n", itemId);
    }
    fclose(f);
}

/* Dispatch one complete inbound line (already '\n'/'\r'-stripped). */
static void ApPipe_HandleLine(const char* line) {
    if (!line[0]) return;
    if (strncmp(line, "ITEM:", 5) == 0) {
        /* Two accepted shapes (launchers may roll back to the first): ITEM:<id> v1 -- legacy unlock line ITEM:<id>|<sender>[|<location>] v2 -- sender display name, plus the AP location id as decimal digits. */
        long itemId = atol(line + 5);
        if (itemId > 0) {
            char sender[AP_PIPE_SENDER_MAX + 1] = {0};
            long locId = 0;
            const char* bar = strchr(line + 5, '|');
            if (bar) {
                const char* s = bar + 1;
                const char* e = strchr(s, '|');
                ApPipe_CopyDisplayField(sender, sizeof(sender), s,
                                        e ? (int)(e - s) : (int)strlen(s));
                if (e) locId = ApPipe_ParseNumericField(e + 1);
            }
            ApPipe_AppendUnlock(itemId, sender, locId);
            if (sender[0]) {
                Log("AP PIPE: ITEM %ld queued for game thread "
                    "(from '%s', loc=%ld)\n", itemId, sender, locId);
            } else {
                Log("AP PIPE: ITEM %ld queued for game thread\n", itemId);
            }
        } else {
            Log("AP PIPE: malformed ITEM line ignored ('%.32s')\n", line);
        }
    } else if (strcmp(line, "STATE:CONNECTED") == 0) {
        ApPipe_PushStateEvent(1);
        Log("AP PIPE: launcher reports STATE:CONNECTED\n");
    } else if (strcmp(line, "STATE:DISCONNECTED") == 0) {
        ApPipe_PushStateEvent(0);
        Log("AP PIPE: launcher reports STATE:DISCONNECTED\n");
    } else if (strncmp(line, "DEATHLINK:", 10) == 0) {
        /* DEATHLINK:<source>|<cause> -- another player died. */
        char src[AP_PIPE_DL_SRC_MAX], cause[AP_PIPE_DL_CAUSE_MAX];
        const char* payload = line + 10;
        const char* bar = strchr(payload, '|');
        if (bar) {
            ApPipe_CopyDisplayField(src, sizeof(src), payload,
                                    (int)(bar - payload));
            ApPipe_CopyDisplayField(cause, sizeof(cause), bar + 1,
                                    (int)strlen(bar + 1));
        } else {
            ApPipe_CopyDisplayField(src, sizeof(src), payload,
                                    (int)strlen(payload));
            cause[0] = 0;
        }
        if (!src[0])   strcpy(src, "Unknown");
        if (!cause[0]) strcpy(cause, "died");
        EnterCriticalSection(&g_apPipeDeathLock);
        strcpy(g_apPipeDeathSource, src);   /* same capacities, pre-capped */
        strcpy(g_apPipeDeathCause, cause);
        LeaveCriticalSection(&g_apPipeDeathLock);
        InterlockedExchange(&g_apPipeDeathEvt, 1);
        Log("AP PIPE: DEATHLINK from '%s' (%s)\n", src, cause);
    } else if (strcmp(line, "KEYLOC:RESET") == 0) {
        /* Must be tested BEFORE the general KEYLOC: prefix below, or the
           prefix swallows it and reports it as malformed. */
        extern void Zones_ClearGateKeyLocations(void);
        Zones_ClearGateKeyLocations();
        Log("AP PIPE: KEYLOC reset\n");
    } else if (strncmp(line, "KEYLOC:", 7) == 0) {
        /* KEYLOC:<diff>|<slot>|<where> -- the launcher scouted the server and
           found the gate key we are still missing. <slot> is the flattened
           0..17 gate slot, <where> is already-formatted text.

           An empty <where> clears the entry: the launcher says that on a new
           connection, so a placement from the previous seed cannot linger. */
        int diff = -1, slot = -1;
        const char* p = line + 7;
        const char* b1 = strchr(p, '|');
        const char* b2 = b1 ? strchr(b1 + 1, '|') : NULL;
        if (b1 && b2) {
            diff = atoi(p);
            slot = atoi(b1 + 1);
            {
                extern void Zones_SetGateKeyLocation(int, int, const char*);
                char where[128] = {0};
                ApPipe_CopyDisplayField(where, sizeof(where), b2 + 1,
                                        (int)strlen(b2 + 1));
                Zones_SetGateKeyLocation(diff, slot, where);
                Log("AP PIPE: KEYLOC diff=%d slot=%d -> '%s'\n",
                    diff, slot, where);
            }
        } else {
            Log("AP PIPE: malformed KEYLOC ('%.32s')\n", line);
        }
    } else if (strcmp(line, "KEYLOC:RESET") == 0) {
        extern void Zones_ClearGateKeyLocations(void);
        Zones_ClearGateKeyLocations();
        Log("AP PIPE: KEYLOC reset\n");
    } else if (strncmp(line, "FORCECHECK:", 11) == 0) {
        /* FORCECHECK:<quest_id>|<difficulty> -- the launcher's "Cheat" button in STANDALONE, where there is no Archipelago server to ask. */
        int qid = 0, diff = 0;
        if (sscanf(line + 11, "%d|%d", &qid, &diff) == 2) {
            ApPipe_QueueForceCheck(qid, diff);
            Log("AP PIPE: FORCECHECK qid=%d diff=%d queued\n", qid, diff);
        } else {
            Log("AP PIPE: malformed FORCECHECK ('%.32s')\n", line);
        }
    } else {
        Log("AP PIPE: unknown message ignored ('%.32s')\n", line);
    }
}

/* Writer thread -- drains the outbound queue with blocking writes. */
static DWORD WINAPI ApPipe_WriterThread(LPVOID param) {
    (void)param;
    HANDLE evt = CreateEventA(NULL, TRUE, FALSE, NULL); /* manual-reset for OVERLAPPED */
    if (!evt) return 0;

    while (!g_apPipeBroken) {
        WaitForSingleObject(g_apPipeQEvt, 1000);

        for (;;) {
            char msg[AP_PIPE_QMSG];
            HANDLE h;

            EnterCriticalSection(&g_apPipeQLock);
            if (g_apPipeQCount == 0) {
                LeaveCriticalSection(&g_apPipeQLock);
                break;
            }
            memcpy(msg, g_apPipeQ[g_apPipeQHead], AP_PIPE_QMSG);
            g_apPipeQHead = (g_apPipeQHead + 1) % AP_PIPE_QCAP;
            g_apPipeQCount--;
            LeaveCriticalSection(&g_apPipeQLock);

            h = g_apPipeHandle;
            if (h == INVALID_HANDLE_VALUE || g_apPipeBroken) break;
            if (!ApPipe_OvWrite(h, msg, (DWORD)strlen(msg), evt)) {
                Log("AP PIPE: WriteFile failed (err=%lu) -- transport marked broken\n",
                    GetLastError());
                InterlockedExchange(&g_apPipeBroken, 1);
                ApPipe_PushStateEvent(0);
                break;
            }
        }
    }
    CloseHandle(evt);
    return 0;
}

/* Reader thread -- owns connect, the receive loop and the handle teardown. */
static DWORD WINAPI ApPipe_ReaderThread(LPVOID param) {
    (void)param;
    char fullPipe[192];
    _snprintf(fullPipe, sizeof(fullPipe), "\\\\.\\pipe\\%s", g_apPipeName);
    fullPipe[sizeof(fullPipe) - 1] = 0;

    HANDLE h = INVALID_HANDLE_VALUE;
    for (int i = 0; i < AP_PIPE_CONNECT_RETRIES && h == INVALID_HANDLE_VALUE; i++) {
        h = CreateFileA(fullPipe, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if (h == INVALID_HANDLE_VALUE) Sleep(AP_PIPE_CONNECT_WAIT_MS);
    }
    if (h == INVALID_HANDLE_VALUE) {
        Log("AP PIPE: connect FAILED after %d attempts (%s, err=%lu) -- "
            "staying on file/bridge transport\n",
            AP_PIPE_CONNECT_RETRIES, fullPipe, GetLastError());
        return 0;
    }

    /* Byte-mode read on purpose: the launcher server uses a byte-mode pipe with newline framing. */
    g_apPipeHandle = h;
    InterlockedExchange(&g_apPipeModeFlag, 1);
    Log("AP PIPE: connected to %s -- pipe transport active\n", fullPipe);

    {
        HANDLE wt = CreateThread(NULL, 0, ApPipe_WriterThread, NULL, 0, NULL);
        if (wt) CloseHandle(wt);
        else Log("AP PIPE: writer thread create FAILED (err=%lu)\n", GetLastError());
    }
    ApPipe_SendLog("D2Archipelago DLL pipe transport online (" D2ARCH_VERSION_DISPLAY ")");

    {
        HANDLE evt = CreateEventA(NULL, TRUE, FALSE, NULL);
        char rd[512];
        char line[1024];
        int  lineLen = 0;
        BOOL overflowed = FALSE;

        while (evt) {
            DWORD got = 0;
            if (!ApPipe_OvRead(h, rd, sizeof(rd), &got, evt)) break;
            if (got == 0) break;  /* server closed its end */

            for (DWORD i = 0; i < got; i++) {
                char c = rd[i];
                if (c == '\n') {
                    /* strip optional trailing CR, terminate, dispatch */
                    while (lineLen > 0 && line[lineLen - 1] == '\r') lineLen--;
                    line[lineLen] = 0;
                    if (!overflowed) ApPipe_HandleLine(line);
                    lineLen = 0;
                    overflowed = FALSE;
                } else if (lineLen < (int)sizeof(line) - 1) {
                    line[lineLen++] = c;
                } else if (!overflowed) {
                    /* Defensive: a frame longer than the buffer is not a valid protocol message -- drop it whole. */
                    Log("AP PIPE: inbound line exceeds %d bytes -- dropped\n",
                        (int)sizeof(line));
                    overflowed = TRUE;
                }
            }
        }
        if (evt) CloseHandle(evt);
    }

    /* Teardown: stop senders first, then surface the disconnect for the game thread, then release the handle (single owner). */
    InterlockedExchange(&g_apPipeBroken, 1);
    ApPipe_PushStateEvent(0);
    g_apPipeHandle = INVALID_HANDLE_VALUE;
    CloseHandle(h);
    if (g_apPipeQEvt) SetEvent(g_apPipeQEvt);  /* wake writer so it can exit */
    Log("AP PIPE: pipe closed (launcher exited or read failed, err=%lu) -- "
        "AP marked disconnected\n", GetLastError());
    return 0;
}

/* Entry point, called once from D2Arch_Init. */
static void ApPipe_InitFromIni(void) {
    char iniPath[MAX_PATH];
    GetModuleFileNameA(NULL, iniPath, MAX_PATH);
    {
        char* sl = strrchr(iniPath, '\\');
        if (sl) *(sl + 1) = 0;
    }
    if ((int)strlen(iniPath) + 24 >= MAX_PATH) return;
    strcat(iniPath, "Archipelago\\d2arch.ini");

    g_apPipeName[0] = 0;
    GetPrivateProfileStringA("ap", "PipeName", "", g_apPipeName,
                             sizeof(g_apPipeName), iniPath);
    if (!g_apPipeName[0]) {
        /* No PipeName -> V1 launcher or standalone launch. */
        Log("AP PIPE: no PipeName in d2arch.ini [ap] -- file/bridge transport (V1 mode)\n");
        return;
    }

    InitializeCriticalSection(&g_apPipeQLock);
    InitializeCriticalSection(&g_apPipeDeathLock);
    g_apPipeQEvt = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!g_apPipeQEvt) {
        Log("AP PIPE: event create FAILED (err=%lu) -- pipe transport disabled\n",
            GetLastError());
        return;
    }
    {
        HANDLE rt = CreateThread(NULL, 0, ApPipe_ReaderThread, NULL, 0, NULL);
        if (rt) {
            CloseHandle(rt);
            Log("AP PIPE: PipeName='%s' -- connecting to V2 launcher in background\n",
                g_apPipeName);
        } else {
            Log("AP PIPE: reader thread create FAILED (err=%lu) -- pipe transport disabled\n",
                GetLastError());
        }
    }
}
