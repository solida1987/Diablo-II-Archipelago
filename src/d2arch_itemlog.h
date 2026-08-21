#ifndef D2ARCH_ITEMLOG_H
#define D2ARCH_ITEMLOG_H

#include <windows.h>

#define ITEMLOG_BUFSIZE  30
#define ITEMLOG_TEXTLEN  48

typedef struct {
    DWORD   tick;
    int     direction;   /* 0=inbound (AP received), 1=outbound (check sent), 2=standalone/local */
    int     color;       /* D2 color index: 0=white, 2=green, 4=gold, 5=grey, 7=orange, 9=blue, 11=red */
    wchar_t what[ITEMLOG_TEXTLEN];
    wchar_t source[ITEMLOG_TEXTLEN];
} ItemLogEntry;

/* Public API */
void ItemLogInit(void);
void ItemLogAdd(int direction, int color, const wchar_t* what, const wchar_t* source);
void ItemLogAddA(int direction, int color, const char* what, const char* source);  /* ANSI convenience */
void ItemLogClear(void);
int  ItemLogCount(void);
const ItemLogEntry* ItemLogGet(int reverseIndex);  /* 0 = newest */

/* Draw helper — call from d2arch_editor.c in AP panel right side */
void ItemLogDraw(int xBase, int yStart, int maxLines);

#endif
