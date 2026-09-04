# -*- coding: utf-8 -*-
"""A DC6 we loaded from disk must never be handed to D2Win's free.

WHAT THIS CAUGHT (2026-09-03)

The F1 skill icons drew characters and monsters after every save&quit —
Marco's "the graphics break themselves when you relog" that had come and gone
since 2.x. His fresh log showed six relogs, six correct character detections,
six generation bumps — the relog detection was NOT the fault.

The fault was a mismatched free. `LoadDC6FromDisk` allocates with Fog
(ordinal 10042) and normalises through D2CMP; the result is OUR block, in
Fog's pool, which D2 never owns. Six editor frame cels loaded that way sat in
a "free+reload on session change" block that released them with `EdCelFree`
— D2Win's `WinFreeCellFile` (10041), the free for ARCHIVE cels. Freeing a Fog
block through D2Win corrupted the pool that every other disk DC6 lives in
(g_icons28, g_archIcons, s_stIcons35, allocated right beside them), and D2
then handed that memory out for sprites. Only after a relog, because that is
when the block freed.

The rule this pins: anything assigned from LoadDC6FromDisk is load-once and
is never passed to EdCelFree or fnCelFree. If it must be released, that is
Fog's free (g_fnFogFree, 10043) — but nothing in the mod needs to: these are
static UI pictures, identical for every character.

    python Tools/lint_no_d2win_free_on_heap_dc6.py
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TREES = [HERE, HERE + " experimental"]

ASSIGN = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)(?:\[[^\]]*\])*\s*=\s*LoadDC6FromDisk\s*\(")
FREE   = re.compile(r"\b(?:EdCelFree\s*\(\s*&\s*|fnCelFree\s*\(\s*)([A-Za-z_][A-Za-z_0-9]*)")


def scan(tree):
    src = os.path.join(tree, "src")
    if not os.path.isdir(src):
        return None
    heap = {}    # name -> file:line where it was loaded from disk
    frees = []   # (name, file:line)
    for fn in sorted(os.listdir(src)):
        if not fn.endswith(".c"):
            continue
        path = os.path.join(src, fn)
        with open(path, encoding="utf-8", errors="replace") as fh:
            for no, line in enumerate(fh, 1):
                for m in ASSIGN.finditer(line):
                    heap.setdefault(m.group(1), "%s:%d" % (fn, no))
                for m in FREE.finditer(line):
                    frees.append((m.group(1), "%s:%d" % (fn, no)))
    bad = [(n, where, heap[n]) for n, where in frees if n in heap]
    return heap, bad


def main():
    problems = 0
    for tree in TREES:
        r = scan(tree)
        if r is None:
            continue
        heap, bad = r
        label = os.path.basename(tree)
        print("%s: %d disk-loaded DC6 pointer(s)" % (label, len(heap)))
        for name, where, origin in bad:
            problems += 1
            print("  FAIL %s: %s is freed with D2Win's WinFreeCellFile, but it "
                  "was loaded from disk at %s (Fog block). That corrupts the pool "
                  "the other icons live in." % (where, name, origin))
    print("\n  %s\n" % ("OK — no disk DC6 is handed to D2Win's free"
                        if not problems else "%d problem(s)" % problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
