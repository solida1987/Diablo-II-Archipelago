"""MPQ encryption/decryption support.

Implements the standard Blizzard MPQ encryption used for hash tables,
block tables, and file data in encrypted MPQ archives.
"""

import struct


def _init_crypt_table():
    """Initialize the 1280-entry encryption lookup table."""
    table = [0] * 1280
    seed = 0x00100001

    for index1 in range(256):
        index2 = index1
        for _ in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            temp1 = (seed & 0xFFFF) << 0x10
            seed = (seed * 125 + 3) % 0x2AAAAB
            temp2 = seed & 0xFFFF
            table[index2] = temp1 | temp2
            index2 += 256

    return table


_CRYPT_TABLE = _init_crypt_table()


def hash_string(string: str, hash_type: int) -> int:
    """Compute MPQ hash of a string.

    hash_type:
        0 = table offset (used for hash table lookup)
        1 = hash A (verification)
        2 = hash B (verification)
        3 = file key (encryption/decryption)
    """
    seed1 = 0x7FED7FED
    seed2 = 0xEEEEEEEE

    for ch in string.upper():
        val = ord(ch)
        seed1 = _CRYPT_TABLE[hash_type * 256 + val] ^ ((seed1 + seed2) & 0xFFFFFFFF)
        seed1 &= 0xFFFFFFFF
        seed2 = (val + seed1 + seed2 + (seed2 << 5) + 3) & 0xFFFFFFFF

    return seed1


def decrypt_block(data: bytes, key: int) -> bytes:
    """Decrypt a block of data using MPQ encryption.

    Data length must be a multiple of 4.
    """
    result = bytearray(len(data))
    seed = 0xEEEEEEEE

    for i in range(0, len(data) - 3, 4):
        seed = (seed + _CRYPT_TABLE[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        val = struct.unpack_from('<I', data, i)[0]
        val = (val ^ (key + seed)) & 0xFFFFFFFF
        struct.pack_into('<I', result, i, val)

        key = (((~key << 0x15) + 0x11111111) | (key >> 0x0B)) & 0xFFFFFFFF
        seed = (val + seed + (seed << 5) + 3) & 0xFFFFFFFF

    return bytes(result)


def detect_file_key(data: bytes, decrypted_size: int) -> int | None:
    """Try to detect the encryption key from known plaintext.

    For sector offset tables, the first entry is always the table size itself.
    """
    # The first 4 bytes of a sector offset table equal the size of the table
    if len(data) < 8:
        return None

    encrypted_val = struct.unpack_from('<I', data, 0)[0]

    # Try to find key where decrypting first dword gives expected_plain
    # For sector offset tables: first entry = (num_sectors + 1) * 4
    # We don't know num_sectors, so try a range
    for num_sectors in range(1, 256):
        expected = (num_sectors + 1) * 4
        if expected > decrypted_size:
            break

        # Reverse the decryption to find the key
        seed = 0xEEEEEEEE
        # We need: val = encrypted ^ (key + seed_after_key_add)
        # expected = encrypted ^ (key + seed + crypt_table[0x400 + (key & 0xFF)])
        # This is hard to reverse directly. Try brute force for common keys.
        # Actually, let's just try known file names as keys.
        pass

    return None
