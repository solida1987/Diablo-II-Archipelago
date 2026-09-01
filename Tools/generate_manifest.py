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
# Copyleft components (GPL-3.0, AGPL-3.0, LGPL-2.1) are NOT distributed, so
# they must not appear in the manifest either -- a manifest entry for a file
# we deliberately do not ship makes the launcher's [Verify] report it missing
# on every install. Keep in sync with _pack_game.py COPYLEFT_COMPONENT_FILES.
#
# GPLv3 calls distributing a work "propagation", and it is conditional on
# supplying the Corresponding Source for the exact binaries. We never did, so
# bundling them was not permitted regardless of how our own code is licensed.
# The user installs them from the authors instead; README.md explains how.
COPYLEFT_COMPONENT_FILES = {
    "glide3x.dll", "d2gl.mpq",                       # d2gl, GPL-3.0
    "ddraw.dll", "ddraw.ini", "winmm.dll",           # cnc-ddraw, player-installed
    "SGD2FreeRes.dll", "SGD2FreeRes.mpq",            # SGD2FreeRes, AGPL-3.0+
    "dsound.dll", "dsoal-aldrv.dll",                 # DSOAL, LGPL-2.1
    # Their licence texts go with them: the package should carry the licence of
    # exactly what it contains. Matched by basename, so licenses/ is covered.
    "d2gl-GPL-3.0.txt", "SGD2FreeRes-AGPL-3.0.txt", "DSOAL-LGPL-2.1.txt",
}

ORIGINAL_D2_FILES = {
    "D2.LNG", "SmackW32.dll", "binkw32.dll", "d2exp.mpq",
    "d2music.mpq", "d2speech.mpq", "d2video.mpq", "d2xmusic.mpq",
    "d2xtalk.mpq", "d2xvideo.mpq", "ijl11.dll",
    "d2char.mpq", "d2data.mpq", "d2sfx.mpq",
    # The 1.10f engine is Blizzard's as well. 1.9.8 shipped these because
    # copying them from a 1.14 install yields an incompatible engine; the
    # launcher now requires the source installation to be 1.10f and checks
    # every file by exact size, so they can be copied like the MPQs.
    # Keep in sync with _pack_game.py BLIZZARD_ENGINE_FILES and the
    # launcher's ORIGINAL_D2_FILES / D2_110F_SIZES.
    "Bnclient.dll", "D2CMP.dll", "D2Client.dll", "D2Common.dll",
    "D2DDraw.dll", "D2Direct3D.dll", "D2Game.dll", "D2Gdi.dll",
    "D2gfx.dll", "D2Glide.dll", "D2Lang.dll", "D2Launch.dll",
    "D2MCPClient.dll", "D2Multi.dll", "D2Net.dll", "D2sound.dll",
    "D2Win.dll", "Diablo II.exe", "Fog.dll", "Game.exe",
    "Patch_D2.mpq", "Storm.dll", "default.key",
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
    # 2.7.0 fix — the manifest must never list ITSELF. A stale
    # Game/game_manifest.json (Stable-2.0.0) was being scanned and listed,
    # and also bundled into game_package.zip by _pack_game.py, so installs
    # verified against a 2.0.0 manifest → 176 false failures. The manifest
    # is a release asset the launcher downloads separately; it is not a
    # packaged game file and must not appear in its own file list.
    "game_manifest.json",
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
    # Original Blizzard files (EULA). Matched by name, so the path matters:
    # patch/D2Game.dll and patch/Fog.dll are D2MOO's builds, not Blizzard's, and
    # skipping them here left two shipped files that the launcher never verified
    # or repaired. Only the copies at the game root are Blizzard's.
    if base in ORIGINAL_D2_FILES and "/" not in rel_path.replace("\\", "/"):
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
    # 3.1.0: engine-regenerated .bin caches. D2 recompiles these from the .txt
    # tables at runtime, and the launcher deletes the managed tables' .bin on
    # every ApplySeed/RestorePristine. Listing them in the manifest made the
    # pre-launch verify flag them "missing" after every session and re-download
    # the whole game package on EVERY launch. (Launcher also skips them via
    # IsRegeneratedCache — keep the two rules in sync.)
    if rp_l.startswith("data/global/excel/") and rp_l.endswith(".bin"):
        return "engine-cache"
    # Files the packer deliberately leaves out. A manifest entry for a file the
    # package does not contain is worse than useless: the launcher reports it
    # missing on every launch, tries to repair, cannot find it in the release,
    # and tells the player to reinstall. That is exactly what happened in
    # v3.7.7 -- the packer learned these rules and this file did not.
    #
    # ⚠ Keep this set identical to _pack_game.py's. The six tables the per-seed
    # patcher edits (weapons, armor, Levels, MagicPrefix, MagicSuffix,
    # SetItems) are SHIPPED as of 2026-09-02 and are therefore NOT here -- see
    # the reasoning in _pack_game.py. They must be in the manifest so verify
    # and repair can restore them; a shipped file the manifest ignores is a
    # file nothing ever checks.
    VANILLA_TABLES = {
        "books.txt", "charstats.txt", "compcode.txt",
        "cubemod.txt", "difficultylevels.txt", "elemtypes.txt", "events.txt",
        "experience.txt", "gems.txt", "hireling.txt", "horcubeex.txt",
        "itemratio.txt", "itemstatcost.txt", "itemtypes.txt",
        "lvlmaze.txt", "lvlprest.txt", "lvlsub.txt", "lvltypes.txt",
        "misscalc.txt", "monai.txt",
        "monequip.txt", "monlvl.txt", "monmode.txt", "monplace.txt",
        "monpreset.txt", "monprop.txt", "monseq.txt", "monsounds.txt",
        "monstats2.txt", "montype.txt", "monumod.txt", "npc.txt",
        "objects.txt", "overlay.txt", "pettype.txt", "plrmode.txt",
        "properties.txt", "sets.txt", "shrines.txt",
        "skillcalc.txt", "skilldesc.txt", "soundenviron.txt", "sounds.txt",
        "states.txt", "treasureclass.txt", "uniqueappellation.txt", "uniqueitems2.txt",
        "uniqueprefix.txt", "uniquesuffix.txt", "uniquetitle.txt",
    }
    if rp_l.startswith("data/global/excel/") and base.lower() in VANILLA_TABLES:
        return "vanilla table (not packaged)"
    if rp_l in ("data/global/ui/spells/skillicon.dc6",
                "data/global/ui/spells/skltree_a_back.dc6"):
        return "Blizzard original (not packaged)"
    # 3.8.0 dead-weight removal — keep in sync with _pack_game.py
    if "shuffle_presets/" in rp_l and rp_l.endswith(".json"):
        return "dev-only preset dump"
    if rp_l.endswith(".dc6") and "rift" in os.path.basename(rp_l):
        return "removed feature (rift)"
    if "objects/riftportal/" in rp_l:
        return "removed feature (rift)"
    if base.lower() in ("sfmpq.dll", "sfmpqapi-terms.txt", "start.bat", "start_with_logging.bat"):
        return "not shipped"
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
    # Copyleft components the user installs themselves.
    if base in COPYLEFT_COMPONENT_FILES:
        return "user-installed component"
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
        "version_display": (version.replace("_", ".").replace("BETA.", "Beta ")
                                   .replace("Stable-", "Stable ").replace("Beta-", "Beta ")),
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
