"""Regenerate game_manifest.json so paths + SHAs match what
raw.githubusercontent.com actually serves.

Walks `git ls-tree HEAD game/` and hashes blob content directly. This
guarantees:
  * Exact path casing (raw.github.com is case-sensitive; Windows isn't)
  * SHA matches the LF-normalized bytes git stores (no CRLF drift)
  * Files that aren't committed (Blizzard MPQs, runtime .dat, *.pyc)
    are silently dropped — the launcher would 404 on them anyway

Run from repo root:
    py Tools/regen_manifest_from_git.py
"""
import os, sys, json, hashlib, subprocess, re

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST_PATH = os.path.join(REPO_ROOT, "game_manifest.json")

# Original Blizzard files — must NEVER be in the manifest. The launcher
# copies these from the user's own D2 install.
ORIGINAL_D2_FILES = {
    "d2.lng", "smackw32.dll", "binkw32.dll", "d2exp.mpq",
    "d2music.mpq", "d2speech.mpq", "d2video.mpq", "d2xmusic.mpq",
    "d2xtalk.mpq", "d2xvideo.mpq", "ijl11.dll",
    # Catch d2data.mpq / d2char.mpq / d2sfx.mpq if they ever sneak back in.
    # The shipped launcher does NOT yet copy these, so they remain a manual
    # step for users; but they must not be in the manifest under any
    # circumstance because we cannot redistribute them and raw.github.com
    # has no copy to verify against.
    "d2char.mpq", "d2data.mpq", "d2sfx.mpq",
    # NOTE 1.9.8: Game.exe is NOT in this skip list. It's the 1.10f loader
    # binary (90 KB) that the mod is built around. Copying from user's
    # install gets the 1.14 version (3.5 MB) which is binary-incompatible
    # and causes "Game version is not supported" error. We MUST ship it.
}

# Runtime/per-character files that may be tracked in git by accident but
# must not be re-downloaded by the launcher (they'd overwrite live state).
SKIP_NAMES = {
    "bnetlog.txt", "d2log.txt", "d2arch_log.txt", "d2arch_crash.txt",
    "d2arch_early.txt", "d2arch_questscan.txt", "d2gl.log",
    "d2glide_debug.log", "d2launch_debug.log",
    "ap_status.dat", "ap_command.dat", "ap_settings.dat",
    "ap_unlocks.dat", "ap_location_owners.dat", "ap_bridge_log.txt",
    "ap_death.dat", "ap_goal.dat", "ap_deathlink_event.dat",
    "d2arch_bridge_locations.dat", "crashdump",
    "start_with_logging.bat",
}
SKIP_PREFIXES = ("d2arch_",)

PER_CHAR_PATTERNS = [
    re.compile(r"^d2arch_(state|checks|slots|applied|ap|reinvest|fireball)_.*\.dat$", re.I),
    re.compile(r"^d2arch_skill\d+_.*\.dat$", re.I),
    re.compile(r"^d2arch_bridge_locations(_.*)?\.dat$", re.I),
    re.compile(r"^d2arch_spoiler_.*\.txt$", re.I),
    re.compile(r"^ap_stash(_ser)?_.*\.dat$", re.I),
]


def should_skip_path(rel_path):
    """Same rules as generate_manifest.should_skip but case-insensitive
    against the lowercased path/base since git may have any casing."""
    rp = rel_path.replace("\\", "/")
    rp_l = rp.lower()
    base = os.path.basename(rp)
    base_l = base.lower()

    if base_l in ORIGINAL_D2_FILES:
        return "EULA"
    if rp_l.startswith("save/"):
        return "save"
    if rp_l.startswith("crashdump/"):
        return "crashdump"
    if rp_l.startswith("python_embed/") or rp_l == "python_embed":
        return "dev-only"
    if "ap_bridge_dist_backup" in rp_l:
        return "backup"
    if rp_l.startswith("sessionlogs/") or rp_l == "sessionlogs":
        return "sessionlogs"
    for seg in rp_l.split("/"):
        if ".backup_" in seg or ".backup_pre_" in seg:
            return "backup-dir"
        if ".backup_prestash_" in seg or ".backup_prestash" in seg:
            return "backup-dir"
    if base_l.startswith("screenshot") and base_l.endswith(".png"):
        return "screenshot"
    if base_l in SKIP_NAMES:
        return "runtime"
    for pat in PER_CHAR_PATTERNS:
        if pat.match(base):
            return "per-char-runtime"
    if base_l.endswith(".log") or base_l == "ap_bridge_log.txt":
        return "log"
    if base_l.endswith(".txt") and base_l.startswith(SKIP_PREFIXES):
        return "runtime"
    if base_l.startswith("d2") and base_l.endswith(".txt") and len(base) > 6:
        stem = base[2:-4]
        if stem.isdigit():
            return "crashlog"
    if base_l.endswith((".bak", ".backup", ".wrong_backup",
                        ".vanilla_backup", ".original", ".backup_original")):
        return "backup"
    if ".backup_" in base_l:
        return "backup"
    if ".backup_pre_" in base_l:
        return "backup"
    if ".before_" in base_l:
        return "backup"
    # __pycache__ artifacts (always per-machine, never appropriate for
    # raw.github.com verification — they ARE in git history by accident).
    if "/__pycache__/" in rp_l or rp_l.startswith("__pycache__/"):
        return "pycache"
    if base_l.endswith(".pyc"):
        return "pycache"
    # Hidden tmp files (.ap_tmp_*)
    if base_l.startswith(".ap_tmp_"):
        return "tmp"
    # 1.9.1 — the manifest cannot list itself. Its size depends on the
    # entries inside, so any self-entry creates a fixed-point problem the
    # launcher's size-only verifier cannot resolve. Excluding it means
    # the launcher's verifier ignores it (it always exists locally because
    # the launcher itself wrote it), and the launcher uses the release
    # asset as the canonical copy.
    if rp_l == "game_manifest.json":
        return "self-reference"
    return None


def git_ls_game():
    """Return list of (path_under_game, blob_sha) pairs at HEAD."""
    res = subprocess.run(
        ["git", "ls-tree", "-r", "HEAD"],
        cwd=REPO_ROOT, capture_output=True, text=True, check=True,
    )
    out = []
    for line in res.stdout.splitlines():
        # format: "<mode> <type> <sha>\t<path>"
        meta, _, path = line.partition("\t")
        if not path.startswith("game/"):
            continue
        parts = meta.split()
        if len(parts) < 3 or parts[1] != "blob":
            continue
        blob = parts[2]
        rel = path[len("game/"):]
        out.append((rel, blob))
    return out


def git_blob_bytes(blob_sha):
    res = subprocess.run(
        ["git", "cat-file", "blob", blob_sha],
        cwd=REPO_ROOT, capture_output=True, check=True,
    )
    return res.stdout


def main():
    if not os.path.exists(MANIFEST_PATH):
        print(f"ERROR: {MANIFEST_PATH} not found")
        sys.exit(1)

    with open(MANIFEST_PATH, "rb") as f:
        old = json.load(f)

    print(f"Old manifest: {len(old['files'])} entries, version={old.get('version')}")

    pairs = git_ls_game()
    print(f"Files committed under game/ at HEAD: {len(pairs)}")

    new_entries = []
    skip_counts = {}
    for rel, blob in pairs:
        reason = should_skip_path(rel)
        if reason:
            skip_counts[reason] = skip_counts.get(reason, 0) + 1
            continue
        data = git_blob_bytes(blob)
        sha = hashlib.sha256(data).hexdigest()
        new_entries.append({
            "path": rel,
            "sha256": sha,
            "size": len(data),
        })

    new_entries.sort(key=lambda e: e["path"].lower())

    # Diff: what we kept vs what was in the old manifest
    old_paths = {e["path"] for e in old["files"]}
    new_paths = {e["path"] for e in new_entries}
    dropped = sorted(old_paths - new_paths)
    added   = sorted(new_paths - old_paths)
    common  = old_paths & new_paths
    sha_changed = []
    old_by_path = {e["path"]: e for e in old["files"]}
    new_by_path = {e["path"]: e for e in new_entries}
    for p in common:
        if old_by_path[p]["sha256"] != new_by_path[p]["sha256"]:
            sha_changed.append(p)

    print()
    print(f"  Manifest entries:       {len(new_entries)}")
    print(f"  Skipped by reason:")
    for reason, n in sorted(skip_counts.items()):
        print(f"    {reason:20s} {n}")
    print(f"  DROPPED (not in git):   {len(dropped)}")
    for p in dropped[:30]:
        print(f"    - {p}")
    if len(dropped) > 30:
        print(f"    ... and {len(dropped) - 30} more")
    print(f"  ADDED (new in git):     {len(added)}")
    for p in added[:30]:
        print(f"    + {p}")
    if len(added) > 30:
        print(f"    ... and {len(added) - 30} more")
    print(f"  SHA changed:            {len(sha_changed)}")
    for p in sha_changed[:30]:
        print(f"    ~ {p}")
    if len(sha_changed) > 30:
        print(f"    ... and {len(sha_changed) - 30} more")

    new_manifest = {
        "version": old.get("version"),
        "version_display": old.get("version_display"),
        "file_count": len(new_entries),
        "files": new_entries,
    }

    with open(MANIFEST_PATH, "w", newline="\n") as f:
        json.dump(new_manifest, f, indent=2)

    total_size = sum(e["size"] for e in new_entries)
    print()
    print(f"Wrote {MANIFEST_PATH}")
    print(f"  {len(new_entries)} files, {total_size:,} bytes total")


if __name__ == "__main__":
    main()
