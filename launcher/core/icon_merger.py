"""Merge class skill icon sprite sheets for randomized skill trees.

Problem: D2 loads skill icons from class-specific DC6 sprite sheets.
When skills from other classes are placed in the target class's tree,
the IconCel index references the wrong icon (from the target class's
sprite sheet instead of the skill's original class).

Solution: Merge ALL class sprite sheets into one big sprite sheet.
Replace every class's sprite sheet with this merged version.
Remap IconCel values to point to the correct frame in the merged sheet.

Each class has 60 frames (30 skills × 2 states: normal + pressed).
Merged sheet: 7 × 60 = 420 frames.
Remapped IconCel = original_IconCel + (class_position × 60)
"""

import os
from .dc6 import read_dc6, write_dc6, DC6File, merge_class_icons
from .d2_data import D2TxtFile

# Class order and prefixes
CLASS_CODES = ["ama", "sor", "nec", "pal", "bar", "dru", "ass"]
CLASS_TO_PREFIX = {
    "ama": "Am", "sor": "So", "nec": "Ne", "pal": "Pa",
    "bar": "Ba", "dru": "Dr", "ass": "As",
}

ICONS_DIR = os.path.join(os.path.dirname(__file__), "..", "data", "icons_dc6")
FRAMES_PER_CLASS = 60  # Standard: 30 skills × 2 states


def load_class_icons() -> dict[str, DC6File]:
    """Load all 7 class icon DC6 files from the bundled icons directory."""
    result = {}
    for cc in CLASS_CODES:
        prefix = CLASS_TO_PREFIX[cc]
        path = os.path.join(ICONS_DIR, f"{prefix}Skillicon.DC6")
        if os.path.exists(path):
            dc6 = read_dc6(path)
            # Only take first 60 frames (Barbarian has 156, but only 60 are standard)
            if len(dc6.frames) > FRAMES_PER_CLASS:
                dc6.frames = dc6.frames[:FRAMES_PER_CLASS]
                dc6.frames_per_dir = FRAMES_PER_CLASS
            result[cc] = dc6
        else:
            print(f"WARNING: Missing icon file: {path}")
    return result


def create_merged_icons(class_icons: dict[str, DC6File]) -> tuple[DC6File, dict[str, int]]:
    """Create a merged sprite sheet from all class icons.

    Returns:
        Tuple of (merged DC6File, class_offsets)
        class_offsets: dict of class_code -> frame offset in merged sheet
    """
    return merge_class_icons(class_icons, CLASS_CODES)


def remap_iconcel(
    skilldesc_txt: D2TxtFile,
    skills_txt: D2TxtFile,
    class_offsets: dict[str, int],
):
    """Remap IconCel values in SkillDesc.txt based on merged sprite sheet.

    For each skill with a class, adjusts its IconCel by adding the
    class's frame offset in the merged sprite sheet.
    """
    # Build map: skilldesc_name -> original_class from Skills.txt
    skilldesc_to_class: dict[str, str] = {}
    for ri in range(len(skills_txt.rows)):
        cc = skills_txt.get_value(ri, "charclass").strip()
        sd_ref = skills_txt.get_value(ri, "skilldesc").strip()
        if cc and sd_ref:
            # Use the ORIGINAL class from vanilla data, not the reassigned class
            skilldesc_to_class[sd_ref] = cc

    # Remap IconCel values
    remapped = 0
    for ri in range(len(skilldesc_txt.rows)):
        sd_name = skilldesc_txt.get_value(ri, "skilldesc").strip()
        iconcel_str = skilldesc_txt.get_value(ri, "IconCel").strip()

        if not sd_name or not iconcel_str:
            continue

        # Look up this skill's original class
        orig_class = skilldesc_to_class.get(sd_name)
        if orig_class and orig_class in class_offsets:
            try:
                old_cel = int(iconcel_str)
                new_cel = old_cel + class_offsets[orig_class]
                skilldesc_txt.set_value(ri, "IconCel", str(new_cel))
                remapped += 1
            except ValueError:
                pass

    print(f"Icon remap: {remapped} IconCel values updated")
    return remapped


def deploy_merged_icons(merged_dc6: DC6File, game_dir: str):
    """Save the merged sprite sheet as ALL class icon files in the game data dir.

    Each class's sprite sheet is replaced with the same merged version,
    so no matter which class the player picks, the icons are correct.
    """
    spells_dir = os.path.join(game_dir, "data", "global", "ui", "SPELLS")
    os.makedirs(spells_dir, exist_ok=True)

    for cc in CLASS_CODES:
        prefix = CLASS_TO_PREFIX[cc]
        out_path = os.path.join(spells_dir, f"{prefix}Skillicon.DC6")
        write_dc6(merged_dc6, out_path)
        print(f"  Deployed: {prefix}Skillicon.DC6 ({os.path.getsize(out_path):,} bytes)")


def merge_and_deploy(
    skilldesc_txt: D2TxtFile,
    skills_txt: D2TxtFile,
    game_dir: str,
) -> bool:
    """Full icon merge pipeline: load, merge, remap, deploy.

    Args:
        skilldesc_txt: The VANILLA SkillDesc.txt (before randomization modifies it)
        skills_txt: The VANILLA Skills.txt (to get original class assignments)
        game_dir: Path to the D2 game directory

    Returns:
        True if successful
    """
    try:
        # Load class icons
        class_icons = load_class_icons()
        if len(class_icons) < 7:
            missing = [cc for cc in CLASS_CODES if cc not in class_icons]
            print(f"WARNING: Missing icon files for: {missing}")
            if len(class_icons) == 0:
                print("ERROR: No icon files found. Skipping icon merge.")
                return False

        # Create merged sprite sheet
        merged, offsets = create_merged_icons(class_icons)
        print(f"Merged sprite sheet: {len(merged.frames)} frames")
        for cc, off in offsets.items():
            print(f"  {cc}: offset {off}")

        # Remap IconCel values
        remap_iconcel(skilldesc_txt, skills_txt, offsets)

        # Deploy to game directory
        deploy_merged_icons(merged, game_dir)

        return True
    except Exception as e:
        print(f"ERROR merging icons: {e}")
        import traceback
        traceback.print_exc()
        return False
