# -*- coding: utf-8 -*-
"""Every DC6 we load from disk must be reloaded per session, and freed with Fog.

MEASURED 2026-09-03 (CELCHK, Marco's real install): after a save&quit every
disk-loaded DC6 was still a perfectly valid DC6 at the very same address —
and the ones kept across the session drew garbage anyway, while the ones
reloaded that session drew fine. D2Gfx (ordinal 10072) caches the decoded
frames per cel POINTER and the game rebuilds that cache's memory on exit; a
kept pointer is a stale cache key that blits recycled sprite memory. That is
the "graphics break themselves when you relog" bug, in both directions it
ever appeared: load-once frames broke the book, load-once icons broke the
icons.

Two rules, both trees:

  1. A pointer assigned from LoadDC6FromDisk (or filled by LoadIconChain) is
     never handed to EdCelFree / fnCelFree — that is D2Win's free (10041),
     for archive cels. Ours came from Fog (10042) and go back via DiskCelFree.

  2. Every LoadDC6FromDisk / LoadIconChain call sits inside a block whose
     opening `if` compares g_celSessionGen (reload per session), or inside
     the helpers that implement that. A "loaded once" BOOL guard is exactly
     the bug. The block is found by brace depth, not by a line count, so a
     comment or a row of frees above the call cannot hide the guard.

    python Tools/lint_disk_dc6_reload_per_session.py
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TREES = [HERE, HERE + " experimental"]

ASSIGN  = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)(?:->[A-Za-z_]+)?(?:\[[^\]]*\])*\s*=\s*LoadDC6FromDisk\s*\(")
CHAINED = re.compile(r"\bLoadIconChain\s*\(\s*&\s*([A-Za-z_][A-Za-z_0-9]*)")
FREE    = re.compile(r"\b(?:EdCelFree\s*\(\s*&\s*|fnCelFree\s*\(\s*)([A-Za-z_][A-Za-z_0-9]*)")
CALL    = re.compile(r"\b(LoadDC6FromDisk|LoadIconChain)\s*\(")
PROTO   = re.compile(r"^\s*(?:static\s+)?[A-Za-z_][\w\*\s]*\b(?:LoadDC6FromDisk|LoadIconChain)\s*\([^;{]*\)\s*;\s*$")
DEFN    = re.compile(r"^\s*(?:static\s+)?[A-Za-z_][\w\*\s]*\b(LoadDC6FromDisk|LoadIconChain|EnsureIconChain|FreeIconChain)\s*\([^;]*\)\s*\{?\s*$")
GUARD   = re.compile(r"g_celSessionGen")


def strip_code(line):
    """Drop string literals and // comments so braces inside them do not count."""
    line = re.sub(r'"(?:\\.|[^"\\])*"', '""', line)
    return line.split("//", 1)[0]


def enclosing_opener(lines, idx):
    """Index of the line holding the `{` that opens the innermost block
    containing lines[idx]; None at file scope."""
    depth = 0
    for k in range(idx - 1, -1, -1):
        code = strip_code(lines[k])
        depth += code.count("}") - code.count("{")
        if depth < 0:
            return k
    return None


def guarded(lines, call_idx):
    """True when the innermost enclosing block's opener (or the two lines
    above it, for a multi-line condition) mentions g_celSessionGen."""
    k = enclosing_opener(lines, call_idx)
    while k is not None:
        head = "\n".join(lines[max(0, k - 2):k + 1])
        if GUARD.search(head):
            return True
        k = enclosing_opener(lines, k)   # nested block: look further out
    return False


def scan(tree):
    src = os.path.join(tree, "src")
    if not os.path.isdir(src):
        return None
    heap, frees, unguarded = {}, [], []
    for fn in sorted(os.listdir(src)):
        if not fn.endswith(".c"):
            continue
        with open(os.path.join(src, fn), encoding="utf-8", errors="replace") as fh:
            lines = fh.read().split("\n")
        in_impl = False
        for i, line in enumerate(lines):
            no = i + 1
            if PROTO.match(line):
                continue                       # a prototype is not a call
            if DEFN.match(line):
                in_impl = True                 # inside the mechanism itself
            elif in_impl and line.startswith("}"):
                in_impl = False
            for m in ASSIGN.finditer(line):
                heap.setdefault(m.group(1), "%s:%d" % (fn, no))
            for m in CHAINED.finditer(line):
                heap.setdefault(m.group(1), "%s:%d" % (fn, no))
            for m in FREE.finditer(line):
                frees.append((m.group(1), "%s:%d" % (fn, no)))
            for m in CALL.finditer(line):
                if in_impl:
                    continue
                if not guarded(lines, i):
                    unguarded.append((m.group(1), "%s:%d" % (fn, no)))
    bad_free = [(n, w, heap[n]) for n, w in frees if n in heap]
    return heap, bad_free, unguarded


def main():
    problems = 0
    for tree in TREES:
        r = scan(tree)
        if r is None:
            continue
        heap, bad_free, unguarded = r
        print("%s: %d disk DC6 pointer(s)" % (os.path.basename(tree), len(heap)))
        for name, where, origin in bad_free:
            problems += 1
            print("  FAIL %s: %s freed with D2Win's WinFreeCellFile, but it was loaded "
                  "from disk at %s — use DiskCelFree." % (where, name, origin))
        for what, where in unguarded:
            problems += 1
            print("  FAIL %s: %s() is not inside a g_celSessionGen block — a DC6 kept "
                  "across a session draws garbage after save&quit." % (where, what))
    print("\n  %s\n" % ("OK — every disk DC6 reloads per session and is freed with Fog"
                        if not problems else "%d problem(s)" % problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
