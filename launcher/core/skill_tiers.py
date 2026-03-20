"""Skill tier categorization for the Diablo II Archipelago randomizer.

Assigns each of the 210 class skills to one of 4 tiers based on
their vanilla required level in Skills.txt:
  T1 (Beginner):      reqlevel 1      ~36 skills
  T2 (Intermediate):  reqlevel 6      ~37 skills
  T3 (Advanced):      reqlevel 12-18  ~68 skills
  T4 (Endgame):       reqlevel 24-30  ~69 skills

The skill tree layout per tab (10 slots):
  Row 0: 2 slots  -> T1
  Row 1: 3 slots  -> T2
  Row 2: 3 slots  -> T3
  Row 3: 2 slots  -> T4
"""

from .d2_data import D2TxtFile

TIER_LABELS = {1: "T1", 2: "T2", 3: "T3", 4: "T4"}
TIER_NAMES = {
    1: "Beginner",
    2: "Intermediate",
    3: "Advanced",
    4: "Endgame",
}
TIER_COLORS = {
    1: "#4ecca3",  # Green
    2: "#4ea8de",  # Blue
    3: "#9b59b6",  # Purple
    4: "#d4a017",  # Gold
}
TIER_COLORS_DIM = {
    1: "#1a4a3a",
    2: "#1a3a5a",
    3: "#3a1a4a",
    4: "#4a3a0a",
}

# Slots per tab: (row_in_tab, col_in_tab, tier)
TAB_LAYOUT = [
    (0, 0, 1), (0, 2, 1),           # Row 0: 2x T1 (spaced)
    (1, 0, 2), (1, 1, 2), (1, 2, 2),  # Row 1: 3x T2
    (2, 0, 3), (2, 1, 3), (2, 2, 3),  # Row 2: 3x T3
    (3, 0, 4), (3, 2, 4),           # Row 3: 2x T4 (spaced)
]

# Total: 10 per tab, 30 across 3 tabs


def reqlevel_to_tier(reqlevel: int) -> int:
    """Map a vanilla required level to a tier (1-4)."""
    if reqlevel <= 1:
        return 1
    elif reqlevel <= 6:
        return 2
    elif reqlevel <= 18:
        return 3
    else:
        return 4


def build_tier_map(vanilla_skills_txt: D2TxtFile) -> dict[str, int]:
    """Build a map of skill_name -> tier from vanilla Skills.txt.

    Only includes class skills (those with a non-empty charclass).
    """
    tier_map = {}
    class_codes = {"ama", "sor", "nec", "pal", "bar", "dru", "ass"}

    for ri in range(len(vanilla_skills_txt.rows)):
        cc = vanilla_skills_txt.get_value(ri, "charclass").strip()
        if cc not in class_codes:
            continue
        name = vanilla_skills_txt.get_value(ri, "skill").strip()
        try:
            rl = int(vanilla_skills_txt.get_value(ri, "reqlevel").strip() or "1")
        except ValueError:
            rl = 1
        tier_map[name] = reqlevel_to_tier(rl)

    return tier_map


def get_slot_tier(tab: int, slot_index: int) -> int:
    """Get the tier for a given slot position within a tab.

    tab: 0-2 (which skill tree tab)
    slot_index: 0-9 (position within tab, matches TAB_LAYOUT order)
    """
    if 0 <= slot_index < len(TAB_LAYOUT):
        return TAB_LAYOUT[slot_index][2]
    return 1


def get_slot_position(slot_index: int) -> tuple[int, int]:
    """Get the (row, col) grid position for a slot index within a tab."""
    if 0 <= slot_index < len(TAB_LAYOUT):
        return TAB_LAYOUT[slot_index][0], TAB_LAYOUT[slot_index][1]
    return (0, 0)
