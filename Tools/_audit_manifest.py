"""Pre-release audit: verify game_manifest.json has no forbidden Blizzard files."""
import json
import os

path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    "game_manifest.json")
with open(path) as f:
    m = json.load(f)

print(f"Manifest version: {m['version']}")
print(f"Total files: {m['file_count']}")
print(f"Files in manifest array: {len(m['files'])}")
print()

forbidden = [
    "D2.LNG", "SmackW32.dll", "binkw32.dll",
    "d2exp.mpq", "d2music.mpq", "d2speech.mpq", "d2video.mpq",
    "d2xmusic.mpq", "d2xtalk.mpq", "d2xvideo.mpq", "ijl11.dll",
    "d2char.mpq", "d2data.mpq", "d2sfx.mpq",
    "Game.exe",
]
forbidden_lc = {f.lower() for f in forbidden}

leaks = []
for entry in m["files"]:
    p = entry["path"]
    base = os.path.basename(p.replace("\\", "/"))
    if base.lower() in forbidden_lc:
        leaks.append((p, base))

if leaks:
    print(f"LEAKS FOUND ({len(leaks)}):")
    for p, b in leaks:
        print(f"  {b} at {p}")
else:
    print("CLEAN: No forbidden Blizzard files in manifest")

# Confirm our own stub is present
diablo_present = any(e["path"] == "Diablo II.exe" for e in m["files"])
print(f"Diablo II.exe (our stub) present in manifest: {diablo_present}")

# Check for shared_stash files
stash_present = any("shared_stash" in e["path"] for e in m["files"])
print(f"shared_stash*.dat present in manifest: {stash_present}")
