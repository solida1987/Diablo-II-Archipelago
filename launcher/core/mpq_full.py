"""Full MPQ reader with encryption and PKWare DCL support.

Handles encrypted MPQ archives like d2exp.mpq that mpyq cannot open.
"""

import struct
import zlib
import bz2
import os

from .mpq_crypt import _CRYPT_TABLE, hash_string, decrypt_block
from .pkware_dcl import explode


class MPQArchive:
    """Read files from MPQ archives, including encrypted ones."""

    def __init__(self, path: str):
        self.path = path
        self.f = open(path, 'rb')

        # Read header
        magic = self.f.read(4)
        if magic != b'MPQ\x1a':
            raise ValueError(f"Not an MPQ file: {magic}")

        header_size = struct.unpack('<I', self.f.read(4))[0]
        archive_size = struct.unpack('<I', self.f.read(4))[0]
        format_version, sector_size_shift = struct.unpack('<HH', self.f.read(4))

        self.sector_size = 512 << sector_size_shift
        self.archive_offset = 0

        hash_table_offset = struct.unpack('<I', self.f.read(4))[0]
        block_table_offset = struct.unpack('<I', self.f.read(4))[0]
        self.hash_table_size = struct.unpack('<I', self.f.read(4))[0]
        self.block_table_size = struct.unpack('<I', self.f.read(4))[0]

        # Read and decrypt hash table
        self.f.seek(hash_table_offset)
        ht_data = self.f.read(self.hash_table_size * 16)
        ht_key = hash_string("(hash table)", 3)
        ht_data = decrypt_block(ht_data, ht_key)
        self.hash_table = []
        for i in range(self.hash_table_size):
            off = i * 16
            ha, hb, locale, platform, block_idx = struct.unpack_from(
                '<IIHHI', ht_data, off)
            self.hash_table.append({
                'hash_a': ha,
                'hash_b': hb,
                'locale': locale,
                'platform': platform,
                'block_index': block_idx,
            })

        # Read and decrypt block table
        self.f.seek(block_table_offset)
        bt_data = self.f.read(self.block_table_size * 16)
        bt_key = hash_string("(block table)", 3)
        bt_data = decrypt_block(bt_data, bt_key)
        self.block_table = []
        for i in range(self.block_table_size):
            off = i * 16
            offset, archived_size, size, flags = struct.unpack_from(
                '<IIII', bt_data, off)
            self.block_table.append({
                'offset': offset,
                'archived_size': archived_size,
                'size': size,
                'flags': flags,
            })

    def close(self):
        self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def _find_file(self, filename: str):
        """Find a file in the hash table."""
        hash_offset = hash_string(filename, 0)
        hash_a = hash_string(filename, 1)
        hash_b = hash_string(filename, 2)

        start = hash_offset & (self.hash_table_size - 1)
        idx = start

        while True:
            entry = self.hash_table[idx]
            if entry['block_index'] == 0xFFFFFFFF:
                return None  # Empty slot = end of search
            if entry['hash_a'] == hash_a and entry['hash_b'] == hash_b:
                return entry
            idx = (idx + 1) & (self.hash_table_size - 1)
            if idx == start:
                return None  # Wrapped around

    def _get_file_key(self, filename: str, block_entry: dict) -> int:
        """Calculate the encryption key for a file."""
        # Key is based on the filename (without path)
        basename = filename.split('\\')[-1]
        key = hash_string(basename, 3)

        # If KEY_ADJUSTED flag is set, adjust the key
        if block_entry['flags'] & 0x00020000:  # MPQ_FILE_FIX_KEY
            key = (key + block_entry['offset']) ^ block_entry['size']
            key &= 0xFFFFFFFF

        return key

    def _decompress(self, data: bytes) -> bytes:
        """Decompress sector data."""
        if len(data) == 0:
            return data

        comp_type = data[0]
        payload = data[1:]

        if comp_type == 0:
            return payload
        elif comp_type == 0x02:
            return zlib.decompress(payload, 15)
        elif comp_type == 0x08:
            return explode(payload)
        elif comp_type == 0x10:
            return bz2.decompress(payload)
        else:
            raise RuntimeError(f"Unsupported compression: {comp_type:#04x}")

    def read_file(self, filename: str) -> bytes | None:
        """Read and decompress a file from the archive."""
        entry = self._find_file(filename)
        if entry is None:
            return None

        block_idx = entry['block_index']
        if block_idx >= len(self.block_table):
            return None

        block = self.block_table[block_idx]
        flags = block['flags']

        if not (flags & 0x80000000):  # EXISTS
            return None

        is_compressed = bool(flags & 0x00000200)
        is_imploded = bool(flags & 0x00000100)
        is_encrypted = bool(flags & 0x00010000)
        is_single = bool(flags & 0x01000000)

        file_key = self._get_file_key(filename, block) if is_encrypted else 0

        if is_single:
            self.f.seek(block['offset'])
            file_data = self.f.read(block['archived_size'])

            if is_encrypted:
                file_data = decrypt_block(file_data, file_key)

            if is_compressed and block['size'] > block['archived_size']:
                file_data = self._decompress(file_data)
            elif is_imploded:
                file_data = explode(file_data)

            return file_data

        # Multi-sector file
        self.f.seek(block['offset'])
        num_sectors = (block['size'] + self.sector_size - 1) // self.sector_size

        # Read sector offset table
        offset_table_size = (num_sectors + 1) * 4
        offset_data = self.f.read(offset_table_size)

        if is_encrypted:
            offset_data = decrypt_block(offset_data, (file_key - 1) & 0xFFFFFFFF)

        offsets = struct.unpack(f'<{num_sectors + 1}I', offset_data)

        # Read and decompress each sector
        result = bytearray()
        for i in range(num_sectors):
            sector_start = block['offset'] + offsets[i]
            sector_end = block['offset'] + offsets[i + 1]
            sector_len = sector_end - sector_start

            self.f.seek(sector_start)
            sector_data = self.f.read(sector_len)

            if is_encrypted:
                sector_data = decrypt_block(sector_data, file_key + i)

            expected_size = min(self.sector_size, block['size'] - i * self.sector_size)

            if is_compressed and sector_len < expected_size:
                sector_data = self._decompress(sector_data)
            elif is_imploded and sector_len < expected_size:
                sector_data = explode(sector_data)

            result.extend(sector_data)

        return bytes(result[:block['size']])


def extract_from_mpq(mpq_path: str, internal_path: str, output_path: str) -> bool:
    """Extract a file from an MPQ archive to disk."""
    try:
        with MPQArchive(mpq_path) as m:
            data = m.read_file(internal_path)
            if data is None:
                return False
            os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)
            with open(output_path, 'wb') as f:
                f.write(data)
            return True
    except Exception as e:
        print(f"  Extract error: {e}")
        return False
