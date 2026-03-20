"""DC6 sprite file parser and writer for Diablo II.

DC6 files store 2D sprite frames used for skill icons, UI elements, etc.
Each file has a header, frame headers, and compressed frame pixel data.

Frame structure:
  - Header: 4 ints (flip, width, height, offset_x, offset_y, unknown, next_block, length)
  - Data: RLE-compressed pixel data (length bytes)

For skill icons: 1 direction, 60 frames (30 skills × 2: normal + pressed/highlighted)
IconCel 0 = frames 0,1; IconCel 2 = frames 2,3; etc.
"""

import struct
from dataclasses import dataclass


@dataclass
class DC6Frame:
    """A single frame in a DC6 file."""
    flip: int
    width: int
    height: int
    offset_x: int
    offset_y: int
    unknown: int
    next_block: int
    data: bytes  # Raw compressed pixel data


@dataclass
class DC6File:
    """Complete DC6 file."""
    version: int
    flags: int
    encoding: int
    termination: bytes  # 4 bytes
    directions: int
    frames_per_dir: int
    frames: list[DC6Frame]


def read_dc6(filepath_or_data) -> DC6File:
    """Read a DC6 file from disk or from bytes."""
    if isinstance(filepath_or_data, (str, bytes)) and not isinstance(filepath_or_data, bytes):
        with open(filepath_or_data, 'rb') as f:
            data = f.read()
    else:
        data = filepath_or_data

    pos = 0

    # Header (24 bytes)
    version, flags, encoding = struct.unpack_from('<iii', data, pos)
    pos += 12
    termination = data[pos:pos + 4]
    pos += 4
    directions, frames_per_dir = struct.unpack_from('<ii', data, pos)
    pos += 8

    total_frames = directions * frames_per_dir

    # Frame pointers (4 bytes each)
    frame_pointers = []
    for i in range(total_frames):
        ptr = struct.unpack_from('<I', data, pos)[0]
        frame_pointers.append(ptr)
        pos += 4

    # Read each frame
    frames = []
    for i in range(total_frames):
        fpos = frame_pointers[i]

        flip = struct.unpack_from('<i', data, fpos)[0]
        width = struct.unpack_from('<i', data, fpos + 4)[0]
        height = struct.unpack_from('<i', data, fpos + 8)[0]
        offset_x = struct.unpack_from('<i', data, fpos + 12)[0]
        offset_y = struct.unpack_from('<i', data, fpos + 16)[0]
        unknown = struct.unpack_from('<i', data, fpos + 20)[0]
        next_block = struct.unpack_from('<i', data, fpos + 24)[0]
        length = struct.unpack_from('<i', data, fpos + 28)[0]

        frame_data = data[fpos + 32:fpos + 32 + length]

        frames.append(DC6Frame(
            flip=flip,
            width=width,
            height=height,
            offset_x=offset_x,
            offset_y=offset_y,
            unknown=unknown,
            next_block=next_block,
            data=frame_data,
        ))

    return DC6File(
        version=version,
        flags=flags,
        encoding=encoding,
        termination=termination,
        directions=directions,
        frames_per_dir=frames_per_dir,
        frames=frames,
    )


def write_dc6(dc6: DC6File, filepath: str):
    """Write a DC6 file to disk."""
    total_frames = dc6.directions * dc6.frames_per_dir
    assert len(dc6.frames) == total_frames

    # Calculate layout
    header_size = 24  # version + flags + encoding + termination + directions + frames_per_dir
    pointer_table_size = total_frames * 4
    data_start = header_size + pointer_table_size

    # Calculate frame positions
    frame_positions = []
    pos = data_start
    for frame in dc6.frames:
        frame_positions.append(pos)
        pos += 32 + len(frame.data)  # 32 = frame header size

    # Build file
    buf = bytearray()

    # File header
    buf += struct.pack('<iii', dc6.version, dc6.flags, dc6.encoding)
    buf += dc6.termination
    buf += struct.pack('<ii', dc6.directions, dc6.frames_per_dir)

    # Frame pointer table
    for fp in frame_positions:
        buf += struct.pack('<I', fp)

    # Frame data
    for frame in dc6.frames:
        buf += struct.pack('<i', frame.flip)
        buf += struct.pack('<i', frame.width)
        buf += struct.pack('<i', frame.height)
        buf += struct.pack('<i', frame.offset_x)
        buf += struct.pack('<i', frame.offset_y)
        buf += struct.pack('<i', frame.unknown)
        buf += struct.pack('<i', frame.next_block)
        buf += struct.pack('<i', len(frame.data))
        buf += frame.data

    with open(filepath, 'wb') as f:
        f.write(buf)


def merge_class_icons(
    class_dc6_map: dict[str, DC6File],
    class_order: list[str] | None = None,
) -> tuple[DC6File, dict[str, int]]:
    """Merge multiple class icon DC6 files into one.

    Args:
        class_dc6_map: dict of class_code -> DC6File
        class_order: order of classes in merged file (default: ama, sor, nec, pal, bar, dru, ass)

    Returns:
        Tuple of (merged DC6File, class_offset_map)
        class_offset_map: dict of class_code -> frame offset in merged file
    """
    if class_order is None:
        class_order = ["ama", "sor", "nec", "pal", "bar", "dru", "ass"]

    all_frames = []
    class_offsets = {}

    for cc in class_order:
        dc6 = class_dc6_map.get(cc)
        if dc6 is None:
            continue
        class_offsets[cc] = len(all_frames)
        all_frames.extend(dc6.frames)

    # Use first file as template for header values
    first = next(iter(class_dc6_map.values()))
    merged = DC6File(
        version=first.version,
        flags=first.flags,
        encoding=first.encoding,
        termination=first.termination,
        directions=1,
        frames_per_dir=len(all_frames),
        frames=all_frames,
    )

    return merged, class_offsets
