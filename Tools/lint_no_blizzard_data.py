# -*- coding: utf-8 -*-
"""Fail the release if the package contains a file that IS Blizzard's.

The existing EULA check in prerelease_check.bat matches FILENAMES. That catches
d2data.mpq and D2Client.dll, and it caught nothing else -- because a file only
has to be named something new to slip past it.

It slipped. A moderator asked what the .bin files in data/global/excel were, and
measuring them showed 48 of the 90 we shipped were byte-identical to a clean
1.10f patch_d2.mpq, plus two .dc6 identical to d2data.mpq art. Every one of them
passed the filename check, and the NOTICE said plainly that we ship nothing of
Blizzard's.

So this compares CONTENT. Every file in the built package is looked up by its
own path inside a set of clean Blizzard archives; a byte-for-byte match is a
failure. Same path with different bytes is reported separately -- that is a
derivative work, which is a decision to make deliberately, not a bug.

Needs a clean reference archive. Without one it says so and passes, because a
gate that cannot run must not silently look green -- but it must not block a
release on a missing local reference either.

Exit 0 = nothing of Blizzard's ships, 1 = something does.
"""
from __future__ import annotations

import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# The clean archives, in the order D2 itself would consult them.
REFERENCE_DIRS = [
    os.path.join(ROOT, "..", "old shit", "Diablo II Lod old", "Reference",
                 "Clean Patch File for 1.10 Final"),
    os.path.join(ROOT, "..", "old shit", "Diablo II Lod old", "Game"),
]
REFERENCE_NAMES = ["patch_d2.mpq", "d2data.mpq", "d2exp.mpq",
                   "d2char.mpq", "d2sfx.mpq"]

# Only these extensions can meaningfully collide with archive content.
INTERESTING = (".bin", ".dc6", ".txt", ".tbl", ".ds1", ".dt1", ".cof", ".pl2")

# The package used to carry 56 unmodified vanilla tables on the theory that
# the engine needed them next to the ones we change; it does not -- it reads
# them from the player's own archives -- so they were dropped.
#
# These SIX are the exception (2026-09-02, project owner's decision). They
# are the tables the per-seed patcher edits, and the patcher has nothing to
# edit unless they are on disk; every runtime way of putting them there
# proved too fragile. They ship unmodified as plain text extracts (freely
# mirrored, used by trackers/wikis/tools; not the game's packaged files, no
# engine code or art) and are patched per seed at launch. Reviewed and
# accepted here so the lint reports them rather than failing.
KNOWN_UNMODIFIED_TABLES = {
    "weapons.txt", "armor.txt", "levels.txt",
    "magicprefix.txt", "magicsuffix.txt", "setitems.txt",
}


def load_reader():
    """The MPQ reader lives in the scratch tooling, not in the repo."""
    for cand in (os.path.join(HERE, "mpq_extract.py"),
                 os.path.join(ROOT, "Tools", "mpq_extract.py")):
        if os.path.exists(cand):
            sys.path.insert(0, os.path.dirname(cand))
            import mpq_extract  # noqa
            return mpq_extract
    return None


def main() -> int:
    zpath = os.path.join(ROOT, "game_package.zip")
    if not os.path.exists(zpath):
        print("[SKIP] blizzard-data lint: game_package.zip not built yet")
        return 0

    reader = load_reader()
    if reader is None:
        print("[SKIP] blizzard-data lint: mpq_extract.py not next to this "
              "script -- content comparison not run")
        return 0

    archives = []
    for d in REFERENCE_DIRS:
        for n in REFERENCE_NAMES:
            p = os.path.normpath(os.path.join(d, n))
            if os.path.exists(p) and not any(a[0] == n for a in archives):
                try:
                    archives.append((n, reader.Archive(p, 0)))
                except Exception as e:
                    print("   (could not read %s: %s)" % (n, e))
    if not archives:
        print("[SKIP] blizzard-data lint: no clean reference archive found")
        return 0

    identical, derived, known = [], [], []
    with zipfile.ZipFile(zpath) as z:
        for name in z.namelist():
            if not name.lower().endswith(INTERESTING):
                continue
            mpq_path = name.replace("/", "\\")
            for arcname, arc in archives:
                try:
                    blizz = arc.read_file(mpq_path)
                except Exception:
                    blizz = None
                if blizz is None:
                    continue
                ours = z.read(name)
                if ours == blizz:
                    if os.path.basename(name).lower() in KNOWN_UNMODIFIED_TABLES:
                        known.append(name)
                    else:
                        identical.append((name, arcname))
                else:
                    derived.append((name, arcname))
                break

    if known:
        print("   %d unmodified vanilla data table(s) shipped -- reviewed "
              "and accepted, see KNOWN_UNMODIFIED_TABLES" % len(known))

    if derived:
        print("   %d file(s) share a path with Blizzard's but differ "
              "(derivative works, reported not failed):" % len(derived))
        for n, a in derived[:10]:
            print("      %s  (vs %s)" % (n, a))
        if len(derived) > 10:
            print("      ... and %d more" % (len(derived) - 10))

    if identical:
        print("[FAIL] blizzard-data lint: %d file(s) in game_package.zip are "
              "BYTE-IDENTICAL to Blizzard's own:" % len(identical))
        for n, a in identical[:20]:
            print("      %s  ==  %s" % (n, a))
        if len(identical) > 20:
            print("      ... and %d more" % (len(identical) - 20))
        print("   The package must not contain these. Either drop them (the "
              "game reads its own archives) or, if the mod needs a changed "
              "version, ship the change rather than the file.")
        return 1

    print("[OK] blizzard-data lint: checked %d archive(s), no shipped file is "
          "byte-identical to Blizzard's" % len(archives))
    return 0


if __name__ == "__main__":
    sys.exit(main())
