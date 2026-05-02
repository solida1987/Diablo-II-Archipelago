"""Stream-rewrites game_package.zip to:
  1. Replace the bundled game_manifest.json with the new one
  2. Convert every data/.../*.txt entry from LF to CRLF (D2 1.10f needs CRLF)

Avoids re-running _pack_game.py from scratch (~10 min for 580 MB of MPQs).
This script touches only the entries that need changing.
"""
import zipfile, os, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "game_package.zip")
NEW_MANIFEST = os.path.join(REPO, "game_manifest.json")
TMP = SRC + ".tmp"

with open(NEW_MANIFEST, "rb") as f:
    new_manifest_bytes = f.read()
print(f"New manifest: {len(new_manifest_bytes):,} bytes")

# Other entries to overwrite from the dev's local Game/ tree if their
# git-HEAD blob differs from the bundled version. Lets a manifest regen
# automatically refresh the corresponding ZIP entries without re-running
# _pack_game.py for the 580 MB MPQs.
import hashlib, json
with open(NEW_MANIFEST, "rb") as f:
    mfst = json.loads(f.read())
expected = {e["path"]: (e["sha256"], e["size"]) for e in mfst["files"]}
def maybe_local(name):
    """Return new bytes for `name` if the dev's local Game/<name> matches
    the manifest entry's SHA but differs from the ZIP entry — i.e., the
    ZIP needs refreshing. Returns None to keep ZIP entry unchanged."""
    p = os.path.join(REPO, "Game", name.replace("/", os.sep))
    if not os.path.exists(p):
        return None
    with open(p, "rb") as f:
        data = f.read()
    exp = expected.get(name)
    if exp and hashlib.sha256(data).hexdigest() == exp[0]:
        return data
    return None

n_total = 0
n_manifest_swapped = 0
n_crlf_fixed = 0
n_streamed_unchanged = 0
n_refreshed_from_local = 0

with zipfile.ZipFile(SRC, "r") as zin:
    print(f"Source ZIP: {len(zin.namelist()):,} entries, {os.path.getsize(SRC)/1024/1024:.1f} MB")
    with zipfile.ZipFile(TMP, "w", zipfile.ZIP_DEFLATED) as zout:
        for info in zin.infolist():
            n_total += 1
            name = info.filename

            if name.lower() == "game_manifest.json":
                ni = zipfile.ZipInfo(filename=name, date_time=info.date_time)
                ni.compress_type = zipfile.ZIP_DEFLATED
                zout.writestr(ni, new_manifest_bytes)
                n_manifest_swapped += 1
                continue

            # CRLF-fix every Windows-bound text file shipped to users:
            #   data .txt  -> D2 1.10f parser crashes on LF
            #   *.ini      -> d2gl/ddraw/d2arch parsers may ignore LF settings
            #   *.bat      -> cmd.exe historically chokes on LF
            lname = name.lower().replace("\\", "/")
            needs_crlf = (
                (lname.endswith(".txt") and (
                    lname.startswith("data/global/excel/")
                    or lname.startswith("data/local/lng/")))
                or lname.endswith(".ini")
                or lname.endswith(".bat")
            )

            # Refresh-from-local is now ALWAYS attempted first. If the dev's
            # local Game/<name> matches the manifest SHA, that's the canonical
            # bytes to ship — the previous bug was that the CRLF branch ran
            # before this check and locked in stale bytes for data .txt files
            # whose content changed (e.g. Missiles.txt Holy Bolt fix, Runes.txt
            # aura par fixes were applied but never reached the ZIP because the
            # files were already CRLF and the CRLF branch did nothing).
            local = maybe_local(name)
            if local is not None:
                with zin.open(info) as src:
                    zip_data = src.read()
                if hashlib.sha256(zip_data).hexdigest() != hashlib.sha256(local).hexdigest():
                    ni = zipfile.ZipInfo(filename=name, date_time=info.date_time)
                    ni.compress_type = zipfile.ZIP_DEFLATED
                    zout.writestr(ni, local)
                    n_refreshed_from_local += 1
                    continue
                # Bytes already match — fall through to stream copy.
                zout.writestr(info, zip_data)
                n_streamed_unchanged += 1
                continue

            # CRLF-fix every Windows-bound text file shipped to users:
            #   data .txt  -> D2 1.10f parser crashes on LF
            #   *.ini      -> d2gl/ddraw/d2arch parsers may ignore LF settings
            #   *.bat      -> cmd.exe historically chokes on LF
            if needs_crlf:
                with zin.open(info) as src:
                    data = src.read()
                if b"\r\n" in data:
                    # already CRLF, leave alone
                    zout.writestr(info, data)
                    n_streamed_unchanged += 1
                else:
                    new_data = data.replace(b"\n", b"\r\n")
                    ni = zipfile.ZipInfo(filename=name, date_time=info.date_time)
                    ni.compress_type = zipfile.ZIP_DEFLATED
                    zout.writestr(ni, new_data)
                    n_crlf_fixed += 1
                continue

            # Pass through everything else byte-for-byte
            with zin.open(info) as src:
                data = src.read()
            zout.writestr(info, data)
            n_streamed_unchanged += 1

print(f"  total entries:        {n_total}")
print(f"  manifest swapped:     {n_manifest_swapped}")
print(f"  CRLF-fixed (.txt):    {n_crlf_fixed}")
print(f"  refreshed from local: {n_refreshed_from_local}")
print(f"  streamed unchanged:   {n_streamed_unchanged}")

shutil.move(TMP, SRC)
print(f"Done. New ZIP: {os.path.getsize(SRC)/1024/1024:.1f} MB")
