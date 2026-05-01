"""Pack game_package.zip excluding Blizzard originals, save files, and logs.

Usage:
    python _pack_game.py [<game_dir>] [<output_zip>]

Both arguments are optional. Defaults: game_dir="Game", output_zip="game_package.zip".
"""
import zipfile, os, sys

# Original Blizzard files - must NOT be distributed (EULA compliance).
# 1.9.1 fix — d2char/d2data/d2sfx leaked into the 1.9.0 game_package.zip
# (580 MB) and into the manifest, both EULA-illegal. Users must copy
# these from their own D2 install. Once the launcher is rebuilt with
# these added to ORIGINAL_D2_FILES, it will copy them automatically.
SKIP_FILES = {
    "D2.LNG", "SmackW32.dll", "binkw32.dll", "d2exp.mpq",
    "d2music.mpq", "d2speech.mpq", "d2video.mpq", "d2xmusic.mpq",
    "d2xtalk.mpq", "d2xvideo.mpq", "ijl11.dll",
    "d2char.mpq", "d2data.mpq", "d2sfx.mpq",
}
SKIP_DIRS = {
    "save", ".git", "crashdump",
    # Embedded Python interpreter — dev-only, not used by game or launcher
    # at runtime. ap_bridge.exe is PyInstaller-frozen and self-contained.
    # Shipping ~48 MB including pip's bytecode cache leaks dev env data.
    "python_embed",
    # Backup of the previous frozen bridge — dev-only.
    "ap_bridge_dist_backup",
    # 1.8.4: SessionLogger output — diagnostic captures from local play
    # sessions, never relevant to a fresh install.
    "sessionlogs",
    # NOTE: ap_bridge_dist/ contains the shipped frozen ap_bridge.exe and
    # MUST be packaged. Do not add it here.
}


def _is_skip_dir(d):
    """Returns True if directory should be pruned from packaging.
    Handles both exact matches (SKIP_DIRS) and pattern-based (backup dirs)."""
    dl = d.lower()
    if dl in SKIP_DIRS:
        return True
    # Backup directories: any dir with .BACKUP_ or .backup_pre_ in name
    if ".backup_" in dl:
        return True
    return False

import re

# Comprehensive skip list for runtime-generated files and backups
SKIP_NAMES = {
    "BnetLog.txt", "d2log.txt", "d2arch_log.txt", "d2arch_crash.txt",
    "d2arch_early.txt", "d2arch_questscan.txt", "d2gl.log",
    "d2glide_debug.log", "d2launch_debug.log",
    # 1.8.4: runtime AP state and dev artifacts
    "ap_status.dat", "ap_command.dat", "ap_settings.dat",
    "ap_unlocks.dat", "ap_location_owners.dat", "ap_bridge_log.txt",
    "ap_death.dat", "ap_goal.dat", "ap_deathlink_event.dat",
    "d2arch_bridge_locations.dat", "Crashdump",
    "START_WITH_LOGGING.bat",  # SessionLogger launcher, dev-only
}
SKIP_PREFIXES = ("d2arch_",)
SKIP_SUFFIXES = (
    ".log", ".bak", ".BACKUP", ".WRONG_BACKUP",
    ".vanilla_backup", ".ORIGINAL", ".BACKUP_ORIGINAL",
)

# 1.8.4: per-character runtime data files. Names embed the dev's testing
# character name → leaks into every release zip and breaks user updates
# because SHA256 never matches.
PER_CHAR_PATTERNS = [
    # 1.9.0 — same .* widening as generate_manifest.py so empty-char
    # files (d2arch_ap_.dat written before a character is loaded) are
    # also caught.
    re.compile(r"^d2arch_(state|checks|slots|applied|ap|reinvest|fireball)_.*\.dat$"),
    re.compile(r"^d2arch_skill\d+_.*\.dat$"),
    re.compile(r"^d2arch_bridge_locations(_.*)?\.dat$"),
    re.compile(r"^d2arch_spoiler_.*\.txt$"),
    re.compile(r"^ap_stash(_ser)?_.*\.dat$"),
]


def should_skip(rel_root, fname):
    """Return True if the file should be excluded from the package."""
    # Root-level Blizzard originals
    if rel_root == "." and fname in SKIP_FILES:
        return "EULA"
    # Named runtime files
    if fname in SKIP_NAMES:
        return "runtime"
    # 1.8.4: per-character runtime data
    for pat in PER_CHAR_PATTERNS:
        if pat.match(fname):
            return "per-char-runtime"
    # Prefix-based skips (d2arch_*.txt)
    if fname.startswith(SKIP_PREFIXES) and fname.endswith(".txt"):
        return "runtime"
    # Suffix-based skips (.log, .bak, .BACKUP, etc.)
    for suffix in SKIP_SUFFIXES:
        if fname.endswith(suffix):
            return "backup"
    # BACKUP_* patterns embedded anywhere in filename
    if ".BACKUP_" in fname:
        return "backup"
    # .backup_pre_VERSION files (e.g. Patch_D2.mpq.backup_pre_1.8.0)
    if ".backup_pre_" in fname.lower():
        return "backup"
    # 1.9.0 — .before_* timestamped backups (Misc.txt.before_pandemonium_*,
    # skill_data.dat.before_d2r_*, MonStats.txt.before_uberai_*, etc.).
    # These pile up during dev iterations.
    if ".before_" in fname.lower():
        return "backup"
    # Screenshots
    if fname.lower().startswith("screenshot") and fname.lower().endswith(".png"):
        return "screenshot"
    # D2-prefixed crash/log .txt files (D2260410.txt, D2YYYYMMDD.txt, etc.)
    if fname.startswith("D2") and fname.endswith(".txt") and len(fname) > 6:
        stem = fname[2:-4]
        if stem.isdigit():
            return "crashlog"
    return None


def main():
    game_dir = sys.argv[1] if len(sys.argv) > 1 else "Game"
    output = sys.argv[2] if len(sys.argv) > 2 else "game_package.zip"

    if not os.path.isdir(game_dir):
        print(f"ERROR: Game directory not found: {game_dir}")
        sys.exit(1)

    print(f"Packing {game_dir} -> {output}")

    count = 0
    skipped = {"EULA": 0, "runtime": 0, "backup": 0, "crashlog": 0}
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as zf:
        for root, dirs, files in os.walk(game_dir):
            rel_root = os.path.relpath(root, game_dir).replace("\\", "/")
            # Prune skip directories in-place (exact matches + backup patterns)
            dirs[:] = [d for d in dirs if not _is_skip_dir(d)]
            # Skip anything under save/ or crashdump/
            rel_lower = rel_root.lower()
            if (rel_lower.startswith("save")
                    or rel_lower.startswith("crashdump")):
                continue
            for fname in sorted(files):
                reason = should_skip(rel_root, fname)
                if reason:
                    if reason == "EULA":
                        print(f"  SKIP (EULA): {fname}")
                    skipped[reason] = skipped.get(reason, 0) + 1
                    continue
                full = os.path.join(root, fname)
                arcname = os.path.relpath(full, game_dir).replace("\\", "/")
                zf.write(full, arcname)
                count += 1

    total_skipped = sum(skipped.values())
    print(f"Done: {count} files added, {total_skipped} skipped")
    print(f"  EULA skips: {skipped.get('EULA', 0)}")
    print(f"  runtime skips: {skipped.get('runtime', 0)}")
    print(f"  backup skips: {skipped.get('backup', 0)}")
    print(f"  crashlog skips: {skipped.get('crashlog', 0)}")
    print(f"ZIP size: {os.path.getsize(output) / 1024 / 1024:.1f} MB")


if __name__ == "__main__":
    main()
