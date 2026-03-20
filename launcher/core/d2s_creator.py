"""Create Diablo II .d2s save files for automatic character creation.

Creates valid level 1 character save files verified byte-by-byte against
real D2 1.14b saves. Uses template-based approach: copies the exact binary
structure from a real save, only modifying name/class/stats.

Fixed header layout (765 bytes):
  0x000-0x14E (335 bytes): Character header
  0x14F-0x278 (298 bytes): Quest data ("Woo!" + 3 difficulties)
  0x279-0x2C8 ( 80 bytes): Waypoint data ("WS" + 3 difficulties)
  0x2C9-0x2FC ( 52 bytes): NPC data
Then variable sections: attributes, skills, items
"""

import struct
import time
import os
import random

CLASS_IDS = {
    "ama": 0, "Amazon": 0,
    "sor": 1, "Sorceress": 1,
    "nec": 2, "Necromancer": 2,
    "pal": 3, "Paladin": 3,
    "bar": 4, "Barbarian": 4,
    "dru": 5, "Druid": 5,
    "ass": 6, "Assassin": 6,
}

# Base stats: (str, dex, vit, eng, hp, mana, stamina)
CLASS_STATS = {
    0: (20, 25, 20, 15, 50, 15, 84),   # Amazon
    1: (10, 25, 10, 35, 40, 35, 74),   # Sorceress
    2: (15, 25, 15, 25, 45, 25, 79),   # Necromancer
    3: (25, 20, 25, 15, 55, 15, 89),   # Paladin
    4: (30, 20, 25, 10, 55, 10, 92),   # Barbarian
    5: (15, 20, 25, 20, 55, 20, 84),   # Druid
    6: (20, 20, 20, 25, 50, 25, 95),   # Assassin
}

# Appearance bytes per class (from real D2 saves)
# 32 bytes: encodes equipment graphics on character select screen
# 0xFF = default/empty slot, specific values = class-specific defaults
CLASS_APPEARANCE = {
    0: bytes([0xFF]*5 + [0x04, 0xFF, 0x4F] + [0xFF]*24),  # Amazon
    1: bytes([0xFF]*5 + [0x04, 0xFF, 0x4F] + [0xFF]*24),  # Sorceress
    2: bytes([0xFF]*5 + [0x04, 0xFF, 0x4F] + [0xFF]*24),  # Necromancer
    3: bytes([0xFF]*5 + [0x04, 0xFF, 0x4F] + [0xFF]*24),  # Paladin
    4: bytes([0xFF]*5 + [0x04, 0xFF, 0x4F] + [0xFF]*24),  # Barbarian
    5: bytes([0xFF]*5 + [0x04, 0xFF, 0x4F] + [0xFF]*24),  # Druid
    6: bytes([0xFF]*5 + [0x04, 0xFF, 0x4F] + [0xFF]*24),  # Assassin
}


def calculate_checksum(data: bytearray) -> int:
    """Calculate the D2S file checksum."""
    data[0x0C:0x10] = b'\x00\x00\x00\x00'
    checksum = 0
    for byte in data:
        carry = 1 if (checksum & 0x80000000) else 0
        checksum = (((checksum << 1) | carry) + byte) & 0xFFFFFFFF
    return checksum


def _build_attributes(class_id: int) -> bytes:
    """Build the bit-packed attributes section.

    IMPORTANT: Only includes non-zero stats, matching real D2 behavior.
    Zero-value stats (stat points, skill points, experience, gold) are skipped.
    """
    s, d, v, e, hp, mn, st = CLASS_STATS[class_id]

    # Stat definitions: (stat_id, bit_length, value)
    # Only include stats with non-zero values (matching real D2 saves)
    stats = [
        (0, 10, s),            # strength
        (1, 10, e),            # energy
        (2, 10, d),            # dexterity
        (3, 10, v),            # vitality
        # stat points (id=4) = 0, SKIP
        # skill points (id=5) = 0, SKIP
        (6, 21, hp * 256),     # hitpoints (fixed-point ×256)
        (7, 21, hp * 256),     # maxhp
        (8, 21, mn * 256),     # mana
        (9, 21, mn * 256),     # maxmana
        (10, 21, st * 256),    # stamina
        (11, 21, st * 256),    # maxstamina
        (12, 7, 1),            # level
        # experience (id=13) = 0, SKIP
        # gold (id=14) = 0, SKIP
        # goldbank (id=15) = 0, SKIP
    ]

    bits = []
    for stat_id, bit_length, value in stats:
        # 9-bit stat ID (LSB first)
        for i in range(9):
            bits.append((stat_id >> i) & 1)
        # value bits (LSB first)
        for i in range(bit_length):
            bits.append((value >> i) & 1)

    # End marker: 9 bits all set (0x1FF)
    for _ in range(9):
        bits.append(1)

    # Convert bits to bytes
    result = bytearray()
    for i in range(0, len(bits), 8):
        byte = 0
        for j in range(8):
            if i + j < len(bits):
                byte |= (bits[i + j] << j)
        result.append(byte)

    return b'gf' + bytes(result)


def _encode_simple_item(type_code: str, x: int = 0, y: int = 0,
                        location: int = 0, panel: int = 1,
                        identified: bool = True, starter: bool = True,
                        new_item: bool = True) -> bytes:
    """Encode a simple/compact item (potion, scroll, key, etc).

    Bit layout after "JM" marker (92 bits for v1.10+):
      0-3:   unknown (0)
      4:     identified
      5-10:  unknown (0)
      11:    socketed (0)
      12:    unknown (0)
      13:    new/picked up since save (1 for new chars)
      14-15: unknown (0)
      16:    ear (0)
      17:    starter item
      18-20: unknown (0)
      21:    simple/compact (1)
      22:    ethereal (0)
      23:    unknown (1) - MUST be 1 based on real saves
      24:    personalized (0)
      25:    unknown (0)
      26:    runeword (0)
      27-31: unknown (0)
      32-41: version (101 for 1.10+)
      42-44: location (0=stored, 1=equipped, 2=belt)
      45-48: equipped slot (0=none)
      49-52: column x
      53-56: row y
      57-59: storage panel (0=none, 1=inventory, 4=stash, 5=cube)
      60-91: type code (4 ASCII chars, 8 bits each, LSB first)
    """
    bits = []

    def add_bits(value, count):
        for i in range(count):
            bits.append((value >> i) & 1)

    # Flags (bits 0-31)
    add_bits(0, 4)                         # 0-3: unknown
    add_bits(1 if identified else 0, 1)    # 4: identified
    add_bits(0, 6)                         # 5-10: unknown
    add_bits(0, 1)                         # 11: socketed
    add_bits(0, 1)                         # 12: unknown
    add_bits(1 if new_item else 0, 1)      # 13: new/picked up
    add_bits(0, 2)                         # 14-15: unknown
    add_bits(0, 1)                         # 16: ear
    add_bits(1 if starter else 0, 1)       # 17: starter
    add_bits(0, 3)                         # 18-20: unknown
    add_bits(1, 1)                         # 21: SIMPLE item
    add_bits(0, 1)                         # 22: ethereal
    add_bits(1, 1)                         # 23: unknown flag (MUST be 1!)
    add_bits(0, 1)                         # 24: personalized
    add_bits(0, 1)                         # 25: unknown
    add_bits(0, 1)                         # 26: runeword
    add_bits(0, 5)                         # 27-31: unknown

    # Version (bits 32-41) - 101 for 1.10+
    add_bits(101, 10)

    # Location info
    add_bits(location, 3)                  # 42-44: location
    add_bits(0, 4)                         # 45-48: equipped slot
    add_bits(x, 4)                         # 49-52: column
    add_bits(y, 4)                         # 53-56: row
    add_bits(panel, 3)                     # 57-59: panel

    # Type code (4 ASCII chars, padded with space)
    code = type_code.ljust(4)[:4]
    for ch in code:
        add_bits(ord(ch), 8)

    # Convert bits to bytes
    result = bytearray()
    for i in range(0, len(bits), 8):
        byte_val = 0
        for j in range(8):
            if i + j < len(bits):
                byte_val |= (bits[i + j] << j)
        result.append(byte_val)

    return b'JM' + bytes(result)


# Complete items+corpse data extracted from real D2 1.14b Barbarian save.
# Contains: JM header + 8 items + corpse section
# Items: 4 belt hp potions, 2 inventory hp potions, hand axe (equipped), buckler (equipped)
# This is used as a template for all classes (works for any class).
REAL_BARBARIAN_ITEMS = (
    b'JM\x08\x00'                                              # Items header: count=8
    b'JM\x10\x20\xa2\x00\x65\x08\x00\x80\x06\x17\x03\x02'    # Belt potion 0
    b'JM\x10\x20\xa2\x00\x65\x08\x02\x80\x06\x17\x03\x02'    # Belt potion 1
    b'JM\x10\x20\xa2\x00\x65\x08\x04\x80\x06\x17\x03\x02'    # Belt potion 2
    b'JM\x10\x20\xa2\x00\x65\x08\x06\x80\x06\x17\x03\x02'    # Belt potion 3
    b'JM\x10\x20\xa2\x00\x65\x00\x72\x42\x37\x37\x06\x02'    # Inventory potion 0
    b'JM\x10\x20\xa2\x00\x65\x00\x52\x92\x36\x37\x06\x02'    # Inventory potion 1
    b'JM\x10\x20\x82\x00\x65\x84\x08\x80\x16\x86\x07\x02'    # Hand Axe (equipped)
    b'\xbd\x70\x73\x80\x80\x80\x83\xc3\x7f'                   # (continued)
    b'JM\x10\x20\x82\x00\x65\xa4\x0a\x20\x56\x37\x06\x82'    # Buckler (equipped)
    b'\xc9\xb7\xc2\xf3\x80\xc0\x01\x0c\x0c\xfe\x03'          # (continued)
    b'JM\x00\x00'                                              # Corpse: is_dead=0
)


def create_d2s(char_name: str, class_id: int, expansion: bool = True) -> bytearray:
    """Create a valid .d2s save file matching real D2 1.14b format."""
    name_bytes = char_name.encode('ascii')[:15]
    if len(name_bytes) < 2:
        raise ValueError("Character name must be at least 2 characters")

    # ===== FIXED HEADER (765 bytes total) =====
    data = bytearray(765)

    # --- Character header (0x000-0x14E, 335 bytes) ---
    struct.pack_into('<I', data, 0x00, 0xAA55AA55)  # Signature
    struct.pack_into('<I', data, 0x04, 96)           # Version (1.10-1.14d)
    # 0x08: file size (filled later)
    # 0x0C: checksum (filled later)
    struct.pack_into('<I', data, 0x10, 0)            # Active weapon set

    # Name (16 bytes null-padded at 0x14)
    for i, b in enumerate(name_bytes):
        data[0x14 + i] = b

    # Status at 0x24
    status = 0
    if expansion:
        status |= 0x20  # Expansion character
    data[0x24] = status

    # Progression at 0x25
    data[0x25] = 0

    # Class at 0x28
    data[0x28] = class_id

    # Reserved bytes at 0x29-0x2A (MUST match real D2: 0x10, 0x1E)
    data[0x29] = 0x10
    data[0x2A] = 0x1E

    # Level at 0x2B
    data[0x2B] = 1

    # Timestamps
    now = int(time.time())
    struct.pack_into('<I', data, 0x30, now)          # Created
    struct.pack_into('<I', data, 0x34, 0xFFFFFFFF)   # Last played (0xFFFFFFFF = never)

    # Assigned skill hotkeys (16 x 4 bytes) - 0x0000FFFF = unassigned
    # Real D2 uses 0x0000FFFF, NOT 0xFFFFFFFF!
    for i in range(16):
        struct.pack_into('<I', data, 0x38 + i * 4, 0x0000FFFF)

    # Left/right skills - 0 = Normal Attack (real D2 uses 0, NOT 0xFFFF!)
    struct.pack_into('<I', data, 0x78, 0)  # Left skill = Attack
    struct.pack_into('<I', data, 0x7C, 0)  # Right skill = Attack
    struct.pack_into('<I', data, 0x80, 0)  # Left swap = Attack
    struct.pack_into('<I', data, 0x84, 0)  # Right swap = Attack

    # Character appearance (0x88-0xA7, 32 bytes) - from real D2 save
    appearance = CLASS_APPEARANCE.get(class_id, bytes([0xFF] * 32))
    data[0x88:0xA8] = appearance

    # Difficulty info (0xA8-0xAA)
    data[0xA8] = 0x80  # Normal: active, Act 1
    data[0xA9] = 0x00  # Nightmare: not started
    data[0xAA] = 0x00  # Hell: not started

    # Map seed (0xAB-0xAE) - random
    map_seed = random.randint(1, 0xFFFFFFFF)
    struct.pack_into('<I', data, 0xAB, map_seed)

    # --- Quest section (0x14F-0x278, 298 bytes) ---
    quest_offset = 0x14F

    # Quest header
    data[quest_offset:quest_offset+4] = b'Woo!'
    struct.pack_into('<I', data, quest_offset + 4, 6)    # Version
    struct.pack_into('<H', data, quest_offset + 8, 298)  # Size

    # Quest data: all zeros for fresh character (no intro flag!)
    # Real D2 has intro=0 for a brand new character

    # --- Waypoint section (0x279-0x2C8, 80 bytes) ---
    wp_offset = 0x279

    data[wp_offset:wp_offset+2] = b'WS'
    struct.pack_into('<I', data, wp_offset + 2, 1)   # Version
    struct.pack_into('<H', data, wp_offset + 6, 80)  # Size

    # Waypoint data: first WP active for ALL 3 difficulties (real D2 behavior)
    for diff in range(3):
        diff_offset = wp_offset + 8 + (diff * 24)
        struct.pack_into('<H', data, diff_offset, 0x0102)  # Marker "02 01"
        data[diff_offset + 2] = 0x01  # First waypoint (Rogue Encampment) active

    # --- NPC section (0x2C9-0x2FC, 52 bytes) ---
    npc_offset = 0x2C9
    data[npc_offset] = 0x01
    data[npc_offset + 1] = 0x77
    struct.pack_into('<H', data, npc_offset + 2, 52)

    # ===== VARIABLE SECTIONS =====
    var_data = bytearray()

    # Attributes (skips zero-value stats, matching real D2)
    var_data += _build_attributes(class_id)

    # Skills: "if" + 30 bytes (all zero = no skill points spent)
    var_data += b'if' + b'\x00' * 30

    # Items + Corpse: use exact bytes from a real D2 save
    # This includes all starting items and the corpse section
    var_data += REAL_BARBARIAN_ITEMS

    # Expansion sections
    if expansion:
        # Merc: "jf" then DIRECTLY "kf" (NO JM when no merc!)
        var_data += b'jf'
        # NO "JM 00 00" here! Real D2 skips it when MercID=0
        var_data += b'kf' + b'\x00'

    # ===== ASSEMBLE =====
    full_data = bytearray(data) + var_data

    # Update file size
    struct.pack_into('<I', full_data, 0x08, len(full_data))

    # Calculate and write checksum
    checksum = calculate_checksum(bytearray(full_data))
    struct.pack_into('<I', full_data, 0x0C, checksum)

    return full_data


def get_save_dir() -> str:
    """Get the D2 1.14d save file directory."""
    save_dir = os.path.join(os.path.expanduser("~"), "Saved Games", "Diablo II")
    if os.path.isdir(save_dir):
        return save_dir
    parent = os.path.join(os.path.expanduser("~"), "Saved Games")
    if os.path.isdir(parent):
        os.makedirs(save_dir, exist_ok=True)
        return save_dir
    return ""


def create_character(char_name: str, class_name: str, save_dir: str = "") -> str:
    """Create a new D2 character save file.

    Returns path to the created .d2s file.
    """
    class_id = CLASS_IDS.get(class_name)
    if class_id is None:
        raise ValueError(f"Unknown class: {class_name}")

    if not save_dir:
        save_dir = get_save_dir()
    if not save_dir:
        raise ValueError("Could not find Diablo II save directory")

    os.makedirs(save_dir, exist_ok=True)

    d2s_data = create_d2s(char_name, class_id, expansion=True)

    save_path = os.path.join(save_dir, f"{char_name}.d2s")
    with open(save_path, 'wb') as f:
        f.write(d2s_data)

    return save_path
