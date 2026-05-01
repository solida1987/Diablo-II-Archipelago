"""Spot-check manifest entries by fetching N random files from
raw.githubusercontent.com and comparing SHA256.

Usage: py Tools/verify_manifest_against_raw.py [sample_size]
"""
import json, hashlib, urllib.request, random, sys, os

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST_PATH = os.path.join(REPO_ROOT, "game_manifest.json")
BASE = "https://raw.githubusercontent.com/solida1987/Diablo-II-Archipelago/main/game/"


def fetch(rel):
    url = BASE + rel
    try:
        with urllib.request.urlopen(url, timeout=30) as r:
            return r.read()
    except Exception as e:
        return ("ERROR", str(e))


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 25
    with open(MANIFEST_PATH, "rb") as f:
        m = json.load(f)
    entries = m["files"]
    print(f"Manifest: {len(entries)} files. Sampling {n}.")

    # Pick a deterministic but varied sample: mix small text + large binaries
    random.seed(42)
    sample = random.sample(entries, min(n, len(entries)))
    # Always include known sensitive ones
    forced = ["Archipelago/version.dat", "Archipelago/news.txt", "START.bat",
              "data/global/excel/misc.txt", "d2gl.ini",
              "apworld/diablo2_archipelago.apworld"]
    by_path = {e["path"]: e for e in entries}
    for p in forced:
        if p in by_path and by_path[p] not in sample:
            sample.append(by_path[p])

    matches = 0
    failures = []
    for e in sample:
        rel = e["path"]
        data = fetch(rel)
        if isinstance(data, tuple):
            failures.append((rel, "FETCH_ERROR", data[1]))
            print(f"  FETCH ERR {rel}: {data[1]}")
            continue
        sha = hashlib.sha256(data).hexdigest()
        if sha == e["sha256"]:
            matches += 1
            print(f"  OK   {rel}  ({len(data):,}b)")
        else:
            failures.append((rel, "SHA_MISMATCH",
                             f"manifest={e['sha256']} actual={sha} size={len(data)}"))
            print(f"  FAIL {rel}: m={e['sha256'][:12]} a={sha[:12]} size={len(data)}")

    print()
    print(f"RESULT: {matches}/{len(sample)} match")
    if failures:
        print()
        for p, kind, info in failures:
            print(f"  {kind:14s} {p}")
            print(f"                 {info}")
        sys.exit(1)


if __name__ == "__main__":
    main()
