"""
Merge all Diablo II class Skillicon DC6 files into one mega file.
Extracts from MPQ, merges all frames, writes to data/ directory.

DC6 format:
  Header: version(4) flags(4) encoding(4) termination(4) directions(4) framesPerDir(4) = 24 bytes
  Frame pointers: N * 4 bytes (N = directions * framesPerDir)
  Frame data blocks: each has header(32 bytes) + encoded pixel data + 3-byte terminator

Usage: python merge_icons.py <game_dir>
"""
import struct, os, sys, ctypes

# DC6 class icon file names and their class IDs
CLASS_FILES = [
    (0, "AmSkillicon"),   # Amazon
    (1, "SoSkillicon"),   # Sorceress
    (2, "NeSkillicon"),   # Necromancer
    (3, "PaSkillicon"),   # Paladin
    (4, "BaSkillicon"),   # Barbarian
    (5, "DrSkillicon"),   # Druid
    (6, "AsSkillicon"),   # Assassin
]

MPQ_PATH = "data\\global\\ui\\SPELLS\\%s.DC6"

def read_dc6(data):
    """Parse DC6 file, return header info and raw frame blocks."""
    if len(data) < 24:
        return None
    ver, flags, enc = struct.unpack_from('<iii', data, 0)
    term = data[12:16]
    dirs, fpd = struct.unpack_from('<II', data, 16)

    n_frames = dirs * fpd
    ptr_start = 24
    ptrs = []
    for i in range(n_frames):
        p = struct.unpack_from('<I', data, ptr_start + i*4)[0]
        ptrs.append(p)

    # Extract each frame block (header + data + terminator)
    frames = []
    for i in range(n_frames):
        off = ptrs[i]
        # Frame header: 7 fields * 4 bytes + length field
        flip, w, h, ox, oy, unk, nxt, length = struct.unpack_from('<iiiiiIII', off=off, buffer=data)
        frame_hdr = data[off:off+32]
        frame_data = data[off+32:off+32+length]
        # 3-byte terminator after data
        frame_term = data[off+32+length:off+32+length+3]
        frames.append(frame_hdr + frame_data + frame_term)

    return {
        'version': ver, 'flags': flags, 'encoding': enc, 'termination': term,
        'directions': dirs, 'frames_per_dir': fpd,
        'frames': frames, 'n_frames': n_frames
    }

def write_dc6(info_list):
    """Write a merged DC6 file from multiple parsed DC6s."""
    # Combine all frames
    all_frames = []
    for info in info_list:
        all_frames.extend(info['frames'])

    n_frames = len(all_frames)
    # Use first file's header values
    ver = info_list[0]['version']
    flags = info_list[0]['flags']
    enc = info_list[0]['encoding']
    term = info_list[0]['termination']

    # Header: 24 bytes
    header = struct.pack('<iii', ver, flags, enc) + term + struct.pack('<II', 1, n_frames)

    # Calculate frame offsets (after header + pointer table)
    ptr_table_size = n_frames * 4
    data_start = 24 + ptr_table_size

    offsets = []
    pos = data_start
    for f in all_frames:
        offsets.append(pos)
        pos += len(f)

    # Build pointer table
    ptr_data = b''
    for off in offsets:
        ptr_data += struct.pack('<I', off)

    # Combine
    result = header + ptr_data
    for f in all_frames:
        result += f

    return result

def extract_from_mpq(game_dir, filename):
    """Extract a file from the game's MPQ files using SFMPQ.dll or Storm.dll."""
    # Try to read from data/ directory first (if -direct is used)
    direct_path = os.path.join(game_dir, "data", "global", "ui", "SPELLS", filename + ".DC6")
    if os.path.exists(direct_path):
        with open(direct_path, 'rb') as f:
            return f.read()

    # Try using SFMPQ.dll to extract from MPQ
    sfmpq_path = os.path.join(game_dir, "Archipelago", "build", "SFMPQ.dll")
    if not os.path.exists(sfmpq_path):
        # Try Storm.dll
        sfmpq_path = os.path.join(game_dir, "Storm.dll")

    if os.path.exists(sfmpq_path):
        try:
            mpq = ctypes.WinDLL(sfmpq_path)

            # Try each MPQ file
            mpq_files = ["patch_d2.mpq", "d2exp.mpq", "d2data.mpq"]
            mpq_path = MPQ_PATH % filename

            for mpq_name in mpq_files:
                mpq_full = os.path.join(game_dir, mpq_name)
                if not os.path.exists(mpq_full):
                    continue

                hMpq = ctypes.c_void_p()
                if not mpq.SFileOpenArchive(mpq_full.encode(), 0, 0, ctypes.byref(hMpq)):
                    continue

                hFile = ctypes.c_void_p()
                if mpq.SFileOpenFileEx(hMpq, mpq_path.encode(), 0, ctypes.byref(hFile)):
                    size = mpq.SFileGetFileSize(hFile, None)
                    if size > 0 and size < 10000000:
                        buf = ctypes.create_string_buffer(size)
                        read = ctypes.c_uint32()
                        mpq.SFileReadFile(hFile, buf, size, ctypes.byref(read), None)
                        mpq.SFileCloseFile(hFile)
                        mpq.SFileCloseArchive(hMpq)
                        print(f"  Extracted {filename} from {mpq_name} ({size} bytes)")
                        return buf.raw[:read.value]
                    mpq.SFileCloseFile(hFile)
                mpq.SFileCloseArchive(hMpq)
        except Exception as e:
            print(f"  SFMPQ error: {e}")

    print(f"  WARNING: Could not extract {filename}")
    return None

def main():
    if len(sys.argv) < 2:
        game_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    else:
        game_dir = sys.argv[1]

    print(f"Game dir: {game_dir}")

    # Extract all class icon files
    dc6_data = {}
    for cls_id, name in CLASS_FILES:
        print(f"Extracting {name}...")
        data = extract_from_mpq(game_dir, name)
        if data:
            dc6_data[cls_id] = data

    if not dc6_data:
        print("ERROR: No DC6 files could be extracted!")
        print("Please extract the Skillicon DC6 files manually and place them in:")
        print(f"  {game_dir}\\data\\global\\ui\\SPELLS\\")
        return 1

    # Parse all DC6 files
    parsed = {}
    frame_offsets = {}  # class_id -> starting frame index in merged file
    offset = 0
    for cls_id, name in CLASS_FILES:
        if cls_id not in dc6_data:
            print(f"  Skipping {name} (not extracted)")
            continue
        info = read_dc6(dc6_data[cls_id])
        if info:
            parsed[cls_id] = info
            frame_offsets[cls_id] = offset
            print(f"  {name}: {info['n_frames']} frames (offset {offset})")
            offset += info['n_frames']
        else:
            print(f"  ERROR parsing {name}")

    if not parsed:
        print("ERROR: No DC6 files could be parsed!")
        return 1

    total_frames = offset
    print(f"\nTotal frames in merged file: {total_frames}")

    # Merge all frames
    merged = write_dc6([parsed[cls_id] for cls_id in sorted(parsed.keys())])
    print(f"Merged DC6 size: {len(merged)} bytes")

    # Write merged file for each class
    out_dir = os.path.join(game_dir, "data", "global", "ui", "SPELLS")
    os.makedirs(out_dir, exist_ok=True)

    for cls_id, name in CLASS_FILES:
        out_path = os.path.join(out_dir, name + ".DC6")
        with open(out_path, 'wb') as f:
            f.write(merged)
        print(f"Written: {out_path}")

    # Also write the generic Skillicon.DC6
    generic_path = os.path.join(out_dir, "Skillicon.DC6")
    with open(generic_path, 'wb') as f:
        f.write(merged)
    print(f"Written: {generic_path}")

    # Write offset table for runtime use
    offsets_path = os.path.join(game_dir, "Archipelago", "icon_offsets.dat")
    with open(offsets_path, 'w') as f:
        for cls_id, name in CLASS_FILES:
            if cls_id in frame_offsets:
                f.write(f"{cls_id},{frame_offsets[cls_id]}\n")
    print(f"Written offset table: {offsets_path}")

    print("\nDone! Now the game needs to run with -direct flag to load from data/")
    print("Runtime nIconCel adjustment: new_cel = icon_offsets[original_charclass] + original_nIconCel")
    return 0

if __name__ == '__main__':
    sys.exit(main())
