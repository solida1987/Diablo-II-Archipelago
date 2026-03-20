"""MPQ file reader with PKWare DCL decompression support.

mpyq doesn't support PKWare DCL (implode) compression used by D2 MPQs.
This module wraps mpyq and adds the missing decompression.
"""

import struct
import zlib
import bz2
import os
import mpyq

from .pkware_dcl import explode


def decompress_sector(data: bytes) -> bytes:
    """Decompress a single MPQ sector with full compression support."""
    if len(data) == 0:
        return data

    comp_type = data[0]

    if comp_type == 0:
        return data[1:]
    elif comp_type == 0x02:
        # zlib
        return zlib.decompress(data[1:], 15)
    elif comp_type == 0x08:
        # PKWare DCL (implode)
        return explode(data[1:])
    elif comp_type == 0x10:
        # bzip2
        return bz2.decompress(data[1:])
    elif comp_type == 0x12:
        # LZMA
        raise RuntimeError("LZMA compression not supported")
    elif comp_type == 0x22:
        # Sparse + zlib
        decompressed = zlib.decompress(data[1:], 15)
        return decompressed
    elif comp_type == 0x28:
        # Sparse + PKWare
        return explode(data[1:])
    elif comp_type == 0x01:
        # Huffman
        raise RuntimeError("Huffman compression not supported")
    elif comp_type == 0x41:
        # Huffman + ADPCM mono
        raise RuntimeError("Huffman+ADPCM compression not supported")
    elif comp_type == 0x81:
        # Huffman + ADPCM stereo
        raise RuntimeError("Huffman+ADPCM stereo compression not supported")
    else:
        raise RuntimeError(f"Unknown compression type: {comp_type:#04x}")


def read_mpq_file(mpq_path: str, internal_path: str) -> bytes | None:
    """Read and decompress a file from an MPQ archive.

    Args:
        mpq_path: Path to the .mpq file
        internal_path: Internal file path (e.g. 'data\\global\\ui\\SPELLS\\AmSkillicon.DC6')

    Returns:
        Decompressed file data, or None if not found
    """
    m = mpyq.MPQArchive(mpq_path)

    # Get hash table entry
    h = m.get_hash_table_entry(internal_path)
    if h is None:
        return None

    block_entry = m.block_table[h.block_table_index]
    flags = block_entry.flags

    # Check if file exists
    if not (flags & 0x80000000):
        return None

    is_compressed = bool(flags & 0x00000200)
    is_imploded = bool(flags & 0x00000100)
    is_encrypted = bool(flags & 0x00010000)
    is_single = bool(flags & 0x01000000)

    if is_encrypted:
        raise RuntimeError(f"Encrypted files not supported: {internal_path}")

    # Read MPQ header to get sector size
    with open(mpq_path, 'rb') as f:
        f.read(4)  # magic
        header_size = struct.unpack('<I', f.read(4))[0]
        f.read(4)  # archive_size
        format_version, sector_size_shift = struct.unpack('<HH', f.read(4))
        sector_size = 512 << sector_size_shift

        if is_single:
            # Single-unit file
            f.seek(block_entry.offset)
            file_data = f.read(block_entry.archived_size)

            if is_compressed and block_entry.size > block_entry.archived_size:
                file_data = decompress_sector(file_data)
            elif is_imploded:
                file_data = explode(file_data)

            return file_data

        # Multi-sector file
        f.seek(block_entry.offset)

        num_sectors = (block_entry.size + sector_size - 1) // sector_size

        # Read sector offset table
        offset_table_size = (num_sectors + 1) * 4
        offset_data = f.read(offset_table_size)
        offsets = struct.unpack(f'<{num_sectors + 1}I', offset_data)

        # Read and decompress each sector
        result = bytearray()
        for i in range(num_sectors):
            sector_start = block_entry.offset + offsets[i]
            sector_end = block_entry.offset + offsets[i + 1]
            sector_len = sector_end - sector_start

            f.seek(sector_start)
            sector_data = f.read(sector_len)

            expected_size = min(sector_size, block_entry.size - i * sector_size)

            if is_compressed and sector_len < expected_size:
                sector_data = decompress_sector(sector_data)
            elif is_imploded and sector_len < expected_size:
                sector_data = explode(sector_data)

            result.extend(sector_data)

        return bytes(result[:block_entry.size])


def extract_file(mpq_path: str, internal_path: str, output_path: str) -> bool:
    """Extract a file from an MPQ archive to disk.

    Returns True if successful.
    """
    data = read_mpq_file(mpq_path, internal_path)
    if data is None:
        return False

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(data)
    return True
