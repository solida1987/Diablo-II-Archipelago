"""
Generate game_manifest.json from a clean game installation folder.
Scans all files, computes SHA256 hashes, and outputs a JSON manifest.

Usage: python generate_manifest.py <game_folder> <version> [output_file]
"""
import os, sys, json, hashlib

# Original Blizzard files - must NOT be distributed (EULA compliance)
# These are copied from user's own D2 install during setup.
# 1.9.1 fix — d2char/d2data/d2sfx were shipping in releases (580 MB EULA
# violation) AND breaking the launcher's verify pass on user installs
# because raw.github.com has no copy to compare SHAs against → 404 +
# "Files keep failing to download". The shipped 1.9.0 launcher does not
# yet copy these from the user's D2 install; users must place them in the
# game folder manually until the launcher is rebuilt to handle them.
ORIGINAL_D2_FILES = {
    "D2.LNG", "SmackW32.dll", "binkw32.dll", "d2exp.mpq",
    "d2music.mpq", "d2speech.mpq", "d2video.mpq", "d2xmusic.mpq",
    "d2xtalk.mpq", "d2xvideo.mpq", "ijl11.dll",
    "d2char.mpq", "d2data.mpq", "d2sfx.mpq",
    # NOTE 1.9.8: Game.exe is NOT in this list. It's a 1.10f-specific
    # loader binary (90 KB) the mod requires. Copying from user's 1.14
    # install (3.5 MB) breaks compatibility with "Game version is not
    # supported" error. We MUST ship our 1.10f Game.exe.
}

import re

# Runtime-generated files that must never ship in a release manifest
SKIP_NAMES = {
    "BnetLog.txt", "d2log.txt", "d2arch_log.txt", "d2arch_crash.txt",
    "d2arch_early.txt", "d2arch_questscan.txt", "d2gl.log",
    "d2glide_debug.log", "d2launch_debug.log",
    # 1.8.4 fix — runtime AP state and dev artifacts
    "ap_status.dat", "ap_command.dat", "ap_settings.dat",
    "ap_unlocks.dat", "ap_location_owners.dat", "ap_bridge_log.txt",
    "ap_death.dat", "ap_goal.dat", "ap_deathlink_event.dat",
    "d2arch_bridge_locations.dat", "Crashdump",
    "START_WITH_LOGGING.bat",  # SessionLogger launcher, dev-only
}
SKIP_PREFIXES = ("d2arch_",)

# 1.8.4 fix — per-character runtime data files. Names embed the active
# character name, so they leak the dev's testing characters into every
# release and the launcher fails to apply them on user installs (SHA256
# never matches because every user has different per-char content).
PER_CHAR_PATTERNS = [
    # 1.9.0 — also match empty char-name suffix (e.g. d2arch_ap_.dat
    # generated when the bridge starts before a character is loaded);
    # changed `.+` to `.*` so the tail can be empty.
    re.compile(r"^d2arch_(state|checks|slots|applied|ap|reinvest|fireball)_.*\.dat$"),
    re.compile(r"^d2arch_skill\d+_.*\.dat$"),
    re.compile(r"^d2arch_bridge_locations(_.*)?\.dat$"),
    re.compile(r"^d2arch_spoiler_.*\.txt$"),  # 1.9.0 — per-char standalone spoiler
    re.compile(r"^ap_stash(_ser)?_.*\.dat$"),
]


# 1.9.0 fix — text-file extensions where git normalizes CRLF -> LF on
# commit (autocrlf=true on Windows). raw.githubusercontent.com serves
# the LF version, but the dev's working tree has CRLF. The launcher
# downloads LF and hashes LF, so manifest entries computed on the
# CRLF working-tree copy fail to match. Normalize before hashing for
# these extensions so manifest SHAs match what GitHub actually serves.
TEXT_EXTENSIONS = {
    ".txt", ".md", ".json", ".bat", ".ini", ".py", ".c", ".h",
    ".cs", ".csv", ".cfg", ".log", ".xml", ".yml", ".yaml",
}


def _is_text_path(path):
    base = os.path.basename(path).lower()
    _, ext = os.path.splitext(base)
    return ext in TEXT_EXTENSIONS


def sha256_file(path):
    """SHA-256 of file content. Text files normalize CRLF -> LF first
    so the hash matches what raw.githubusercontent.com serves
    (git's autocrlf=true on Windows stores LF in the index)."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        data = f.read()
    if _is_text_path(path):
        data = data.replace(b"\r\n", b"\n")
    h.update(data)
    return h.hexdigest()


def should_skip(rel_path, base):
    """Return True if the file should be omitted from the manifest."""
    # Original Blizzard files (EULA)
    if base in ORIGINAL_D2_FILES:
        return "EULA"
    # Save files (personal data)
    if rel_path.startswith("save/") or rel_path.lower().startswith("save/"):
        return "save"
    if rel_path.startswith("crashdump/") or rel_path.lower().startswith("crashdump/"):
        return "crashdump"
    # Embedded Python interpreter — dev-only, 48 MB of bandwidth waste +
    # pip bytecode leaks dev env data. ap_bridge.exe (PyInstaller) is
    # self-contained and doesn't need the embedded Python at runtime.
    rp_l = rel_path.lower().replace("\\", "/")
    if rp_l.startswith("python_embed/") or rp_l == "python_embed":
        return "dev-only"
    if "ap_bridge_dist_backup" in rp_l:
        return "backup"
    # 1.8.4: SessionLogger output — diagnostic captures from local play
    # sessions, never relevant to a fresh install.
    if rp_l.startswith("sessionlogs/") or rp_l == "sessionlogs":
        return "sessionlogs"
    # NOTE: ap_bridge_dist/ contains the shipped frozen ap_bridge.exe and
    # MUST be listed in the manifest (the DLL spawns it at runtime).
    # Backup directories anywhere in path (e.g. "data.BACKUP_PRESTASH_20260420_194117/",
    # "src_BACKUP_170_*", "foo.backup_pre_1.8.0/")
    for seg in rp_l.split("/"):
        if ".backup_" in seg or ".backup_pre_" in seg:
            return "backup-dir"
        if ".backup_prestash_" in seg or ".backup_prestash" in seg:
            return "backup-dir"
    # Screenshots (auto-generated by D2 Print Screen)
    if base.lower().startswith("screenshot") and base.lower().endswith(".png"):
        return "screenshot"
    # Named runtime files
    if base in SKIP_NAMES:
        return "runtime"
    # 1.8.4: per-character runtime data — embeds dev's testing character
    # names; user's local SHA256 never matches manifest, so launcher fails
    for pat in PER_CHAR_PATTERNS:
        if pat.match(base):
            return "per-char-runtime"
    # Log files anywhere
    if rel_path.endswith(".log") or base == "ap_bridge_log.txt":
        return "log"
    # d2arch_*.txt runtime files
    if base.endswith(".txt") and base.startswith(SKIP_PREFIXES):
        return "runtime"
    # D2-prefixed crash/log .txt files (D2260410.txt, D2YYYYMMDD.txt, etc.)
    if base.startswith("D2") and base.endswith(".txt") and len(base) > 6:
        stem = base[2:-4]
        if stem.isdigit():
            return "crashlog"
    # Backup files
    if base.endswith(".bak") or base.endswith(".BACKUP"):
        return "backup"
    if base.endswith(".WRONG_BACKUP") or base.endswith(".vanilla_backup"):
        return "backup"
    if base.endswith(".ORIGINAL") or base.endswith(".BACKUP_ORIGINAL"):
        return "backup"
    if ".BACKUP_" in base:
        return "backup"
    # Trailing .backup_pre_VERSION on files (e.g. Patch_D2.mpq.backup_pre_1.8.0)
    if ".backup_pre_" in base.lower():
        return "backup"
    # 1.9.0 — .before_* timestamped backups (e.g. Misc.txt.before_pandemonium_*,
    # skill_data.dat.before_d2r_*, skill_icon_map.dat.before_1.7.1_iconfix).
    # These pile up during dev iterations and were leaking into manifests.
    if ".before_" in base.lower():
        return "backup"
    return None


def generate_manifest(game_dir, version):
    files = []
    for root, dirs, filenames in os.walk(game_dir):
        rel_root = os.path.relpath(root, game_dir).replace("\\", "/")
        rel_root_lower = rel_root.lower()
        # Skip save/crashdump folders (personal data / runtime)
        if rel_root_lower.startswith("save") or rel_root_lower.startswith("crashdump"):
            continue
        for fname in sorted(filenames):
            full_path = os.path.join(root, fname)
            rel_path = os.path.relpath(full_path, game_dir).replace("\\", "/")
            base = os.path.basename(rel_path)

            reason = should_skip(rel_path, base)
            if reason:
                if reason == "EULA":
                    print(f"  SKIP (Blizzard original): {rel_path}")
                elif reason in ("log", "runtime", "crashlog", "backup"):
                    print(f"  SKIP ({reason}): {rel_path}")
                continue

            size = os.path.getsize(full_path)
            sha = sha256_file(full_path)
            files.append({
                "path": rel_path,
                "sha256": sha,
                "size": size
            })
            print(f"  {rel_path} ({size:,} bytes)")

    manifest = {
        "version": version,
        "version_display": version.replace("_", ".").replace("BETA.", "Beta "),
        "file_count": len(files),
        "files": files
    }
    return manifest


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python generate_manifest.py <game_folder> <version> [output_file]")
        sys.exit(1)

    game_dir = sys.argv[1]
    version = sys.argv[2]
    output = sys.argv[3] if len(sys.argv) > 3 else "game_manifest.json"

    print(f"Generating manifest for: {game_dir}")
    print(f"Version: {version}")
    print(f"Output: {output}")
    print()

    manifest = generate_manifest(game_dir, version)

    with open(output, "w") as f:
        json.dump(manifest, f, indent=2)

    total_size = sum(e["size"] for e in manifest["files"])
    print(f"\nManifest generated: {manifest['file_count']} files, {total_size:,} bytes total")
    print(f"Saved to: {output}")
