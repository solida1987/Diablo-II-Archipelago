"""Extract individual skill icons from DC6 sprite sheets for the launcher UI.

Decodes DC6 RLE-compressed frames using the D2 units palette,
producing PIL Images suitable for Tkinter display via ImageTk.
"""

import os
from PIL import Image
from .dc6 import read_dc6, DC6File
from .d2_data import D2TxtFile

DATA_DIR = os.path.join(os.path.dirname(__file__), "..", "data")
ICONS_DIR = os.path.join(DATA_DIR, "icons_dc6")
PALETTE_PATH = os.path.join(DATA_DIR, "units_palette.dat")

CLASS_CODES = ["ama", "sor", "nec", "pal", "bar", "dru", "ass"]
CLASS_TO_PREFIX = {
    "ama": "Am", "sor": "So", "nec": "Ne", "pal": "Pa",
    "bar": "Ba", "dru": "Dr", "ass": "As",
}

_palette_cache: list[tuple[int, int, int]] | None = None
_icon_cache: dict[str, Image.Image] = {}


def _load_palette() -> list[tuple[int, int, int]]:
    """Load the D2 units palette (256 RGB triplets)."""
    global _palette_cache
    if _palette_cache is not None:
        return _palette_cache

    with open(PALETTE_PATH, "rb") as f:
        raw = f.read()

    _palette_cache = []
    for i in range(256):
        r = raw[i * 3]
        g = raw[i * 3 + 1]
        b = raw[i * 3 + 2]
        _palette_cache.append((r, g, b))
    return _palette_cache


def _decode_dc6_frame(frame) -> list[int]:
    """Decode DC6 RLE-compressed frame data to palette indices."""
    pixels = []
    data = frame.data
    pos = 0
    while pos < len(data):
        b = data[pos]
        pos += 1
        if b == 0x80:  # End of scanline
            continue
        elif b & 0x80:  # Transparent run
            count = b & 0x7F
            pixels.extend([0] * count)
        else:  # Opaque run
            count = b
            for _ in range(count):
                pixels.append(data[pos])
                pos += 1
    return pixels


def _frame_to_image(frame, palette: list[tuple[int, int, int]]) -> Image.Image:
    """Convert a DC6 frame to a PIL RGBA image."""
    pixels = _decode_dc6_frame(frame)
    img = Image.new("RGBA", (frame.width, frame.height), (0, 0, 0, 0))

    for y in range(frame.height):
        for x in range(frame.width):
            # DC6 frames are stored bottom-up
            pi = (frame.height - 1 - y) * frame.width + x
            if pi < len(pixels):
                pal_idx = pixels[pi]
                r, g, b = palette[pal_idx]
                a = 0 if pal_idx == 0 else 255
                img.putpixel((x, y), (r, g, b, a))
    return img


def get_skill_icon(
    class_code: str,
    iconcel: int,
    size: int = 40,
) -> Image.Image | None:
    """Get a skill icon as a PIL Image.

    Args:
        class_code: Class code (ama, sor, nec, pal, bar, dru, ass)
        iconcel: IconCel index (0-29, the vanilla icon index for the class)
        size: Output size (icons are resized to size×size)

    Returns:
        PIL RGBA Image or None if not found.
    """
    cache_key = f"{class_code}_{iconcel}_{size}"
    if cache_key in _icon_cache:
        return _icon_cache[cache_key]

    prefix = CLASS_TO_PREFIX.get(class_code)
    if not prefix:
        return None

    dc6_path = os.path.join(ICONS_DIR, f"{prefix}Skillicon.DC6")
    if not os.path.exists(dc6_path):
        return None

    try:
        dc6 = read_dc6(dc6_path)
        # IconCel IS the frame index (even = normal, odd = pressed)
        frame_idx = iconcel
        if frame_idx >= len(dc6.frames):
            return None

        palette = _load_palette()
        img = _frame_to_image(dc6.frames[frame_idx], palette)

        if size != 48:
            img = img.resize((size, size), Image.LANCZOS)

        _icon_cache[cache_key] = img
        return img
    except Exception as e:
        print(f"Error loading icon {class_code}/{iconcel}: {e}")
        return None


def build_skill_icon_map(
    vanilla_skills_txt: D2TxtFile,
    vanilla_sdesc_txt: D2TxtFile,
    size: int = 40,
) -> dict[str, Image.Image]:
    """Build a map of skill_name -> PIL Image for all class skills.

    Uses vanilla data to look up original class and IconCel for each skill.
    """
    # Build skilldesc -> (class, iconcel) from vanilla data
    skill_to_desc: dict[str, str] = {}
    skill_to_class: dict[str, str] = {}

    for ri in range(len(vanilla_skills_txt.rows)):
        cc = vanilla_skills_txt.get_value(ri, "charclass").strip()
        name = vanilla_skills_txt.get_value(ri, "skill").strip()
        sd = vanilla_skills_txt.get_value(ri, "skilldesc").strip()
        if cc in CLASS_CODES and name and sd:
            skill_to_desc[name] = sd
            skill_to_class[name] = cc

    # Build skilldesc -> iconcel from SkillDesc.txt
    desc_to_iconcel: dict[str, int] = {}
    for ri in range(len(vanilla_sdesc_txt.rows)):
        sd_name = vanilla_sdesc_txt.get_value(ri, "skilldesc").strip()
        iconcel_str = vanilla_sdesc_txt.get_value(ri, "IconCel").strip()
        if sd_name and iconcel_str:
            try:
                desc_to_iconcel[sd_name] = int(iconcel_str)
            except ValueError:
                pass

    # Build the final map
    result: dict[str, Image.Image] = {}
    for skill_name, sd_ref in skill_to_desc.items():
        cc = skill_to_class.get(skill_name)
        iconcel = desc_to_iconcel.get(sd_ref)
        if cc and iconcel is not None:
            img = get_skill_icon(cc, iconcel, size)
            if img:
                result[skill_name] = img

    return result


def create_empty_slot_icon(size: int = 40, tier: int = 1) -> Image.Image:
    """Create a placeholder icon for an empty slot."""
    from .skill_tiers import TIER_COLORS_DIM
    color_hex = TIER_COLORS_DIM.get(tier, "#1a1a2e")
    # Convert hex to RGB
    r = int(color_hex[1:3], 16)
    g = int(color_hex[3:5], 16)
    b = int(color_hex[5:7], 16)
    img = Image.new("RGBA", (size, size), (r, g, b, 180))
    return img
