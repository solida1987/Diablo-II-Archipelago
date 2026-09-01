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
#
# The 1.10f engine binaries are Blizzard's too and are skipped as well.
# History: 1.9.5 removed them for EULA reasons and 1.9.8 put them back,
# because copying them from the user's install produced a 1.14 engine that
# is binary-incompatible with this 1.10f mod. The resolution is not to ship
# Blizzard's files but to require the user's own installation to BE 1.10f;
# the launcher verifies every one of these by exact size before copying and
# refuses a source folder at any other patch level.
#
# Everything else in the package is ours or open source and ships normally:
# D2Archipelago.dll and D2Arch_Launcher.exe (this mod), patch/ (D2MOO, MIT),
# D2.Detours, d2gl (ddraw.dll, glide3x.dll), cnc-ddraw (winmm.dll), DSOAL,
# SGD2FreeRes and SFmpq.
# Copyleft components (GPL-3.0, AGPL-3.0, LGPL-2.1) are NOT distributed.
#
# These are separate programs that Diablo II loads on its own -- this mod does
# not link against any of them. That was never the question, though. GPLv3
# calls distributing a work "propagation", and propagation is conditional on
# supplying the Corresponding Source for the exact binaries handed out. We
# shipped their licence texts and credited their authors, but never their
# source, so bundling them was not permitted no matter how our own code is
# licensed. Fetching them automatically would be the same act with extra steps.
#
# So the user installs them, from the authors, under the authors' own terms.
# Using GPL software alongside differently-licensed software is entirely the
# user's right; it is the distribution that carries obligations. README.md has
# the instructions.
#
# Their .ini/.json files stay: those are this project's tuned settings for
# those tools, not the tools themselves, and keeping them means a manual
# install lands on the right configuration instead of the defaults.
# ddraw.dll is the hard one: BOTH d2gl (GPL-3.0) and cnc-ddraw (MIT) provide a
# file by that name, so a name alone cannot decide. v3.7.7 removed it by name and
# shipped a package with no DirectDraw wrapper at all -- Diablo II 1.10's own
# DirectDraw does not initialise on modern Windows, so the game died on launch
# with "Error 22". cnc-ddraw's is the one we ship, and it is MIT.
#
# So this asks the FILE, not the filename.
def is_d2gl_ddraw(path):
    """True when this ddraw.dll is d2gl's GPL-3.0 renderer rather than
    cnc-ddraw's MIT wrapper. Reads the binary because the name is ambiguous."""
    try:
        with open(path, "rb") as f:
            blob = f.read()
    except OSError:
        return False   # unreadable: do not silently drop a required file
    def has(needle: bytes) -> bool:
        # Version resources are UTF-16; plain strings are ASCII. winmm.dll only
        # names cnc-ddraw in its version resource, so checking ASCII alone would
        # have missed it entirely.
        return needle in blob or needle.decode().encode("utf-16-le") in blob

    if has(b"cnc-ddraw") or has(b"FunkyFr3sh"):
        return False
    return has(b"d2gl") or has(b"Bayaraa") or has(b"D2GL")


COPYLEFT_COMPONENT_FILES = {
    # d2gl -- GPL-3.0 (c) Bayaraa. glide3x.dll is what -3dfx needs.
    # NOTE: ddraw.dll is NOT here -- it is decided by content, see above.
    "glide3x.dll", "d2gl.mpq",
    # SGD2FreeRes -- AGPL-3.0-or-later (c) Mir Drualga. Free resolution.
    "SGD2FreeRes.dll", "SGD2FreeRes.mpq",
    # DSOAL -- LGPL-2.1. dsound.dll is Wine's DirectSound, dsoal-aldrv.dll is
    # OpenAL Soft.
    "dsound.dll", "dsoal-aldrv.dll",
}

# ...and their licence texts go with them. The package should carry the licence
# of exactly what it contains, no more: a GPL text sitting next to no GPL binary
# reads like the binary is still in here somewhere. Each component's licence
# travels with its own download, and THIRD-PARTY-NOTICES.md links to every one.
# These live in licenses/, not at the root, so they need their own check.
COPYLEFT_LICENCE_TEXTS = {
    "d2gl-GPL-3.0.txt", "SGD2FreeRes-AGPL-3.0.txt", "DSOAL-LGPL-2.1.txt",
}

BLIZZARD_ENGINE_FILES = {
    "Bnclient.dll", "D2CMP.dll", "D2Client.dll", "D2Common.dll",
    "D2DDraw.dll", "D2Direct3D.dll", "D2Game.dll", "D2Gdi.dll",
    "D2gfx.dll", "D2Glide.dll", "D2Lang.dll", "D2Launch.dll",
    "D2MCPClient.dll", "D2Multi.dll", "D2Net.dll", "D2sound.dll",
    "D2Win.dll", "Diablo II.exe", "Fog.dll", "Game.exe",
    "Patch_D2.mpq", "Storm.dll", "default.key",
}

SKIP_FILES = {
    "D2.LNG", "SmackW32.dll", "binkw32.dll", "d2exp.mpq",
    "d2music.mpq", "d2speech.mpq", "d2video.mpq", "d2xmusic.mpq",
    "d2xtalk.mpq", "d2xvideo.mpq", "ijl11.dll",
    "d2char.mpq", "d2data.mpq", "d2sfx.mpq",
} | BLIZZARD_ENGINE_FILES | COPYLEFT_COMPONENT_FILES
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
    # 2.7.0 fix — game_manifest.json is a RELEASE ASSET, never a packaged
    # game file. A stale Game/game_manifest.json (Stable-2.0.0, 890 files)
    # had been bundling into game_package.zip since 2.0.0 and extracting over
    # the correct downloaded manifest on install, so the launcher verified
    # 2.x files against a 2.0.0 manifest → 176 false "missing/wrong size"
    # warnings on every fresh install. Excluding it here (and from
    # generate_manifest.py) keeps the package clean; the launcher writes the
    # real manifest from the release asset.
    "game_manifest.json",
    # 1.9.5 (Bug O7): account-wide shared stash is dev test data. The
    # files were being included in game_package.zip even though the
    # launcher's manifest correctly omits them, so every fresh install
    # inherited the dev's test items ("Clean up yo inventory Solida!"
    # per Maegis). These three files MUST stay out of the release ZIP;
    # runtime regenerates them empty on first character load.
    "shared_stash.dat", "shared_stash_ser.dat", "shared_stash_stk.dat",
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


game_dir_for_check = "Game"


def should_skip(rel_root, fname):
    """Return True if the file should be excluded from the package."""
    # --- Dead weight, removed in 3.8.0 after auditing every file in the package ---
    #
    # shuffle_preset_NN.json: the DLL reads only the .dat form
    # (sprintf "shuffle_preset_%02d.dat" in d2arch_shuffle.c). The .json is the
    # human-readable twin -- 48 MB uncompressed, read by nothing.
    if rel_root.replace("\\", "/").lower().endswith("archipelago/shuffle_presets")             and fname.lower().endswith(".json"):
        return "dev-only preset dump"

    # Rift art. The feature was removed on 2026-05-05 (see the note at the top of
    # d2arch.c); the graphics stayed behind. No table references any of them.
    if "rift" in fname.lower() and fname.lower().endswith(".dc6"):
        return "removed feature (rift)"
    if rel_root.replace("\\", "/").lower().endswith("objects/riftportal"):
        return "removed feature (rift)"

    # SFMPQ.dll: an MPQ library for build-time tooling. Nothing at runtime
    # imports or names it -- D2Archipelago.dll imports only KERNEL32, USER32,
    # ADVAPI32 and XINPUT9_1_0, and neither the bootstrap nor D2MOO's D2Game
    # mentions it.
    if fname.lower() in ("sfmpq.dll", "sfmpqapi-terms.txt"):
        return "build-time tooling"

    # START.bat: nothing calls it, and worse, launching through it skips the
    # launcher's check for the DirectDraw wrapper -- straight into "Error 22"
    # with no explanation. The launcher is the supported way in.
    if fname.lower() in ("start.bat", "start_with_logging.bat"):
        return "unsupported launch path"

    # Root-level Blizzard originals
    if rel_root == "." and fname in SKIP_FILES:
        return "EULA"
    # cnc-ddraw is not shipped either, by choice rather than by licence: it is
    # MIT and we could. This project distributes nobody else's binaries any
    # more, so the player installs it and the launcher refuses to start without
    # it -- see D2Plugin.RendererBlocker. ddraw.dll is checked by name AND by
    # content, because d2gl ships a file with the same name and letting that one
    # through would be shipping GPL code.
    if fname.lower() in ("ddraw.dll", "ddraw.ini", "winmm.dll"):
        return "cnc-ddraw (player-installed)"
    # data/global/excel/*.bin -- the engine's own compiled caches of the .txt
    # tables. D2 rebuilds them from the .txt at runtime and the launcher deletes
    # the managed ones on every ApplySeed, so they are pure build residue.
    #
    # They are also not ours to ship: measured against a clean 1.10f
    # patch_d2.mpq, 48 of the 90 we were shipping were BYTE-IDENTICAL to
    # Blizzard's, which flatly contradicts the NOTICE. generate_manifest.py has
    # excluded them as "engine-cache" since 3.1.0; the packer never got the same
    # rule, so they went out in every release anyway.
    if rel_root.replace("\\", "/").lower() == "data/global/excel" \
            and fname.lower().endswith(".bin"):
        return "engine-cache"
    # Unmodified vanilla tables the engine reads from the player's own archives
    # when the data folder does not carry them -- shipping a byte-identical copy
    # of one the game already has achieves nothing.
    #
    # ⚠ EXCEPTION (2026-09-02, project owner's decision): the SIX tables the
    # randomizer's per-seed patcher edits -- weapons, armor, Levels, MagicPrefix,
    # MagicSuffix, SetItems -- are SHIPPED. They are plain tab-separated text
    # extracts, freely mirrored across the web and used by trackers, wikis and
    # tools; they are not the game's own packaged files and carry no engine
    # code or art. The patcher (D2DataFiles) has no source to edit unless they
    # are on disk, and every runtime way of getting them there (MPQ extraction,
    # first-launch snapshot) proved too fragile: "item requirements off", gear
    # shop shuffle and full-level generation silently did nothing on fresh
    # installs. Shipping the six removes the whole race. They are NOT in the
    # skip set below.
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
    rr = rel_root.replace("\\", "/").lower()
    if rr == "data/global/excel" and fname.lower() in VANILLA_TABLES:
        return "vanilla table"
    # Two .dc6 that are byte-identical to files in the player's own d2data.mpq.
    # The game falls back to the archive when a file is absent from data/, so
    # shipping copies of Blizzard's art buys nothing and is exactly what the
    # NOTICE says we do not do.
    if fname.lower() in ("skillicon.dc6", "skltree_a_back.dc6") \
            and rel_root.replace("\\", "/").lower() == "data/global/ui/spells":
        return "Blizzard original"
    # Licence texts of components we no longer distribute
    if rel_root.replace("\\", "/").lower() == "licenses" and fname in COPYLEFT_LICENCE_TEXTS:
        return "copyleft-licence"
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

    # should_skip has to read ddraw.dll to tell cnc-ddraw's from d2gl's.
    global game_dir_for_check
    game_dir_for_check = game_dir

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
