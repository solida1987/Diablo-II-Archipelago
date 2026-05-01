"""Surgically replace game_manifest.json inside game_package.zip without
re-zipping 580 MB of Blizzard MPQs.

Streams every entry from the source ZIP to a new ZIP, swapping only the
manifest entry. ~30 seconds vs ~10 minutes for a full _pack_game.py rerun.
"""
import zipfile, sys, os, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "game_package.zip")
NEW_MANIFEST = os.path.join(REPO, "game_manifest.json")
TMP = SRC + ".tmp"

with open(NEW_MANIFEST, "rb") as f:
    new_manifest_bytes = f.read()
print(f"New manifest: {len(new_manifest_bytes):,} bytes")

count_in = 0
count_swap = 0
with zipfile.ZipFile(SRC, "r") as zin:
    print(f"Source ZIP: {len(zin.namelist()):,} entries, {os.path.getsize(SRC)/1024/1024:.1f} MB")
    with zipfile.ZipFile(TMP, "w", zipfile.ZIP_DEFLATED) as zout:
        for info in zin.infolist():
            count_in += 1
            if info.filename.lower() == "game_manifest.json":
                # Write the new version with same name + permissions
                new_info = zipfile.ZipInfo(filename=info.filename,
                                           date_time=info.date_time)
                new_info.compress_type = zipfile.ZIP_DEFLATED
                zout.writestr(new_info, new_manifest_bytes)
                count_swap += 1
                print(f"  swapped: {info.filename}  {info.file_size:,} -> {len(new_manifest_bytes):,}")
            else:
                # Stream-copy untouched
                with zin.open(info) as src:
                    data = src.read()
                zout.writestr(info, data)

print(f"Streamed {count_in} entries, swapped {count_swap}")
shutil.move(TMP, SRC)
print(f"Done. New ZIP: {os.path.getsize(SRC)/1024/1024:.1f} MB")
