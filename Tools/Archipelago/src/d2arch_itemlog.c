/* ================================================================
 * D2Archipelago - Item Log (inbound/outbound check feed)
 *
 * Ring buffer of the last ITEMLOG_BUFSIZE (30) events to display in
 * the AP panel right side. Entries are added from AP bridge hooks
 * (inbound item, outbound check) and from local quest completion.
 *
 * ---------------------------------------------------------------
 * BUILD NOTE — unity-build integration
 * ---------------------------------------------------------------
 * This project is a unity build: d2arch.c #includes every .c file
 * in a fixed order (see d2arch.c). As a result, fnText / fnFont /
 * Log have internal linkage (file-static in d2arch_api.c and
 * d2arch_config.c respectively) but are visible to every #include
 * that comes LATER in d2arch.c because it is the same translation
 * unit. This file MUST be #include-ed after d2arch_config.c (for
 * Log) and after d2arch_api.c (for fnText/fnFont).
 *
 * The task spec requested `extern` declarations for fnText/fnFont,
 * but these symbols are declared `static` in d2arch_api.c and would
 * not link via extern (internal vs external linkage conflict). To
 * match the pattern used by every other module in this codebase
 * (d2arch_render.c, d2arch_drawall.c, d2arch_questlog.c, etc.) we
 * reference the statics directly and rely on include order.
 * ================================================================ */

#include "d2arch_itemlog.h"
#include <stdio.h>    /* _snwprintf */
#include <wchar.h>    /* wchar_t */

/* ----------------------------------------------------------------
 * STATE — ring buffer
 * ---------------------------------------------------------------- */
static ItemLogEntry g_itemLog[ITEMLOG_BUFSIZE];
static volatile LONG g_itemLogHead  = 0;  /* next write index */
static volatile LONG g_itemLogCount = 0;  /* total entries written, capped at BUFSIZE */
static volatile LONG g_itemLogBusy  = 0;  /* simple write guard */

/* ----------------------------------------------------------------
 * INIT / CLEAR
 * ---------------------------------------------------------------- */
void ItemLogInit(void) {
    int i;
    for (i = 0; i < ITEMLOG_BUFSIZE; i++) {
        g_itemLog[i].tick = 0;
        g_itemLog[i].direction = 0;
        g_itemLog[i].color = 0;
        g_itemLog[i].what[0] = 0;
        g_itemLog[i].source[0] = 0;
    }
    g_itemLogHead = 0;
    g_itemLogCount = 0;
    g_itemLogBusy = 0;
    Log("ItemLog: initialized (capacity=%d)\n", ITEMLOG_BUFSIZE);
}

void ItemLogClear(void) {
    ItemLogInit();
}

int ItemLogCount(void) {
    return (int)g_itemLogCount;
}

/* ----------------------------------------------------------------
 * ADD (wide char)
 * ---------------------------------------------------------------- */
void ItemLogAdd(int direction, int color, const wchar_t* what, const wchar_t* source) {
    LONG idx;
    ItemLogEntry* e;
    int i;

    /* Simple write guard: if another thread is already inside, drop.
     * Reads from the draw loop do not take the guard; they may race
     * against a writer but the worst case is a half-written text
     * buffer which is harmless (null terminator always written last
     * inside the fixed-size slot). */
    if (InterlockedIncrement(&g_itemLogBusy) != 1) {
        InterlockedDecrement(&g_itemLogBusy);
        return;
    }

    /* Claim the slot */
    idx = g_itemLogHead % ITEMLOG_BUFSIZE;
    e = &g_itemLog[idx];

    /* Wipe the slot before writing so stale bytes never leak */
    e->what[0] = 0;
    e->source[0] = 0;

    e->tick = GetTickCount();
    e->direction = direction;
    e->color = color;

    if (what) {
        for (i = 0; i < ITEMLOG_TEXTLEN - 1 && what[i]; i++) {
            e->what[i] = what[i];
        }
        e->what[i] = 0;
    }
    if (source) {
        for (i = 0; i < ITEMLOG_TEXTLEN - 1 && source[i]; i++) {
            e->source[i] = source[i];
        }
        e->source[i] = 0;
    }

    /* Advance head circularly, bump count up to cap */
    g_itemLogHead = (g_itemLogHead + 1) % ITEMLOG_BUFSIZE;
    if (g_itemLogCount < ITEMLOG_BUFSIZE) {
        InterlockedIncrement(&g_itemLogCount);
    }

    InterlockedDecrement(&g_itemLogBusy);

    Log("ItemLog: +%s what='%ls' source='%ls' (count=%d)\n",
        direction == 0 ? "IN" : direction == 1 ? "OUT" : "LOC",
        e->what, e->source, (int)g_itemLogCount);
}

/* ----------------------------------------------------------------
 * ADD (ANSI convenience — converts via MultiByteToWideChar)
 * ---------------------------------------------------------------- */
void ItemLogAddA(int direction, int color, const char* what, const char* source) {
    wchar_t wWhat[ITEMLOG_TEXTLEN];
    wchar_t wSource[ITEMLOG_TEXTLEN];

    wWhat[0] = 0;
    wSource[0] = 0;

    if (what) {
        MultiByteToWideChar(CP_UTF8, 0, what, -1, wWhat, ITEMLOG_TEXTLEN);
        wWhat[ITEMLOG_TEXTLEN - 1] = 0;
    }
    if (source) {
        MultiByteToWideChar(CP_UTF8, 0, source, -1, wSource, ITEMLOG_TEXTLEN);
        wSource[ITEMLOG_TEXTLEN - 1] = 0;
    }

    ItemLogAdd(direction, color, wWhat, wSource);
}

/* ----------------------------------------------------------------
 * GET (reverseIndex = 0 -> newest)
 * ---------------------------------------------------------------- */
const ItemLogEntry* ItemLogGet(int reverseIndex) {
    LONG count = g_itemLogCount;
    LONG head  = g_itemLogHead;
    LONG slot;

    if (reverseIndex < 0 || reverseIndex >= count) return NULL;

    /* Newest lives at (head - 1) mod BUFSIZE */
    slot = (head - 1 - reverseIndex);
    while (slot < 0) slot += ITEMLOG_BUFSIZE;
    slot %= ITEMLOG_BUFSIZE;

    return &g_itemLog[slot];
}

/* ----------------------------------------------------------------
 * DRAW
 *
 * Renders the newest `maxLines` entries starting at (xBase, yStart),
 * 14px per line. Uses quest-log-style small font (fnFont(6)).
 *
 * Format: "{dirSymbol} {what} ({source})"
 *   dirSymbol: inbound=->, outbound=<-, standalone=dot
 *
 * Text colours follow each entry's `color` field, so callers can
 * use green for common, gold for quest items, red for unique, etc.
 * ---------------------------------------------------------------- */
void ItemLogDraw(int xBase, int yStart, int maxLines) {
    int i;
    int lines;
    int total;
    wchar_t line[ITEMLOG_TEXTLEN * 2 + 16];

    /* Safety: bail if D2Win draw entry points haven't been resolved. */
    if (!fnText || !fnFont) return;
    if (maxLines <= 0) return;

    total = (int)g_itemLogCount;
    if (total <= 0) return;

    /* Quest-log style small font */
    fnFont(6);

    lines = total;
    if (lines > maxLines) lines = maxLines;

    for (i = 0; i < lines; i++) {
        const ItemLogEntry* e = ItemLogGet(i);
        const wchar_t* sym;
        int y;

        if (!e) continue;

        switch (e->direction) {
            case 0:  sym = L"\x2192"; break; /* -> inbound (AP received) */
            case 1:  sym = L"\x2190"; break; /* <- outbound (check sent) */
            default: sym = L"\x00B7"; break; /* dot standalone/local */
        }

        if (e->source[0]) {
            _snwprintf(line,
                       sizeof(line) / sizeof(line[0]) - 1,
                       L"%s %s (%s)",
                       sym, e->what, e->source);
        } else {
            _snwprintf(line,
                       sizeof(line) / sizeof(line[0]) - 1,
                       L"%s %s",
                       sym, e->what);
        }
        line[sizeof(line) / sizeof(line[0]) - 1] = 0;

        y = yStart + (i * 14);
        fnText(line, xBase, y, e->color, 0);
    }
}
