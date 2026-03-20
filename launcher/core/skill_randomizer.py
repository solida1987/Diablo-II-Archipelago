"""Skill randomizer for Diablo II.

Selects 30 random skills from the full pool of 210 class skills,
assigns them to a target class, removes prerequisites, and arranges
them in the skill tree UI (3 tabs x 10 skills).
"""

import random
from dataclasses import dataclass
from .d2_data import D2TxtFile

CLASS_CODES = ["ama", "sor", "nec", "pal", "bar", "dru", "ass"]

CLASS_NAMES = {
    "ama": "Amazon",
    "sor": "Sorceress",
    "nec": "Necromancer",
    "pal": "Paladin",
    "bar": "Barbarian",
    "dru": "Druid",
    "ass": "Assassin",
}

CLASS_CODE_FROM_NAME = {v: k for k, v in CLASS_NAMES.items()}


@dataclass
class SkillEntry:
    row_index: int          # Index in Skills.txt rows
    skill_name: str         # 'skill' column
    skill_id: int           # 'Id' column
    original_class: str     # Original charclass code
    skilldesc_ref: str       # 'skilldesc' column (links to SkillDesc.txt)


def get_all_class_skills(skills_txt: D2TxtFile) -> list[SkillEntry]:
    """Extract all 210 class skills from Skills.txt."""
    results = []
    class_rows = skills_txt.find_rows_nonempty("charclass")
    for ri in class_rows:
        name = skills_txt.get_value(ri, "skill")
        sid = skills_txt.get_value(ri, "Id")
        cc = skills_txt.get_value(ri, "charclass")
        sdesc = skills_txt.get_value(ri, "skilldesc")
        if cc in CLASS_CODES:
            results.append(SkillEntry(
                row_index=ri,
                skill_name=name,
                skill_id=int(sid) if sid else 0,
                original_class=cc,
                skilldesc_ref=sdesc,
            ))
    return results


def get_skills_by_class(all_skills: list[SkillEntry]) -> dict[str, list[SkillEntry]]:
    """Group skills by their original class code."""
    by_class: dict[str, list[SkillEntry]] = {c: [] for c in CLASS_CODES}
    for s in all_skills:
        by_class[s.original_class].append(s)
    return by_class


def select_random_skills(
    all_skills: list[SkillEntry],
    count: int = 30,
    seed: int | None = None,
) -> list[SkillEntry]:
    """Randomly select `count` skills from the pool."""
    rng = random.Random(seed)
    return rng.sample(all_skills, count)


def randomize_skills(
    skills_txt: D2TxtFile,
    skilldesc_txt: D2TxtFile,
    charstats_txt: D2TxtFile,
    target_class: str,
    seed: int | None = None,
    vanilla_skills_txt: D2TxtFile | None = None,
    vanilla_sdesc_txt: D2TxtFile | None = None,
    initial_count: int = 6,
) -> list[SkillEntry]:
    """Main randomization entry point.

    1. Select `initial_count` random T1 skills from all 210
    2. Assign them to target_class
    3. Remove prerequisites, set reqlevel=1
    4. Place them in skill tree (2 per tab for 6 initial)
    5. Rebalance other classes
    6. Update CharStats.txt

    Returns the list of initially selected/placed skills.
    """
    from .skill_tiers import reqlevel_to_tier, TAB_LAYOUT

    all_skills = get_all_class_skills(skills_txt)
    by_class = get_skills_by_class(all_skills)

    # Validate
    for cc in CLASS_CODES:
        assert len(by_class[cc]) == 30, f"Expected 30 skills for {cc}, got {len(by_class[cc])}"

    rng = random.Random(seed)

    # Get vanilla reqlevels for tier assignment
    tier_map = {}
    if vanilla_skills_txt:
        for ri in range(len(vanilla_skills_txt.rows)):
            name = vanilla_skills_txt.get_value(ri, "skill").strip()
            try:
                rl = int(vanilla_skills_txt.get_value(ri, "reqlevel").strip() or "1")
            except ValueError:
                rl = 1
            tier_map[name] = reqlevel_to_tier(rl)

    # Select initial skills: pick T1 skills first (2 per tab = 6 total)
    t1_skills = [s for s in all_skills if tier_map.get(s.skill_name, 1) == 1]
    rng.shuffle(t1_skills)
    selected = t1_skills[:initial_count]

    if len(selected) < initial_count:
        # Fall back: fill remaining from any tier
        remaining = [s for s in all_skills if s not in selected]
        rng.shuffle(remaining)
        selected.extend(remaining[:initial_count - len(selected)])

    selected_set = {s.row_index for s in selected}

    # --- Step 1: Assign selected skills to target class ---
    for skill in selected:
        skills_txt.set_value(skill.row_index, "charclass", target_class)
        skills_txt.set_value(skill.row_index, "reqlevel", "1")
        skills_txt.set_value(skill.row_index, "reqskill1", "")
        skills_txt.set_value(skill.row_index, "reqskill2", "")
        skills_txt.set_value(skill.row_index, "reqskill3", "")

    # --- Step 2: Rebalance other classes ---
    spare_skills = [s for s in by_class[target_class] if s.row_index not in selected_set]
    spare_idx = 0
    for cc in CLASS_CODES:
        if cc == target_class:
            continue
        taken = [s for s in by_class[cc] if s.row_index in selected_set]
        for t in taken:
            if spare_idx < len(spare_skills):
                spare = spare_skills[spare_idx]
                skills_txt.set_value(spare.row_index, "charclass", cc)
                spare_idx += 1

    # --- Step 3: Place selected skills in skill tree ---
    # Hide ALL skills from tree first
    sd_name_to_row: dict[str, int] = {}
    for ri in range(len(skilldesc_txt.rows)):
        name = skilldesc_txt.get_value(ri, "skilldesc")
        if name:
            sd_name_to_row[name] = ri

    for ri in range(len(skilldesc_txt.rows)):
        page = skilldesc_txt.get_value(ri, "SkillPage")
        if page and page != "0":
            skilldesc_txt.set_value(ri, "SkillPage", "0")

    # Place initial skills: 2 per tab in T1 slots
    skills_per_tab = initial_count // 3
    placed = 0
    for tab in range(3):
        tab_skills = selected[tab * skills_per_tab:(tab + 1) * skills_per_tab]
        t1_slots = [(i, r, c, t) for i, (r, c, t) in enumerate(TAB_LAYOUT) if t == 1]

        for skill_idx, skill in enumerate(tab_skills):
            if skill_idx >= len(t1_slots):
                break
            slot_idx, row, col, tier = t1_slots[skill_idx]
            sd_ref = skill.skilldesc_ref
            if sd_ref not in sd_name_to_row:
                continue

            sd_row = sd_name_to_row[sd_ref]
            page = tab + 1
            d2_row = {0: 1, 1: 3, 2: 5, 3: 6}[row]
            d2_col = col + 1

            skilldesc_txt.set_value(sd_row, "SkillPage", str(page))
            skilldesc_txt.set_value(sd_row, "SkillRow", str(d2_row))
            skilldesc_txt.set_value(sd_row, "SkillColumn", str(d2_col))
            skilldesc_txt.set_value(sd_row, "ListRow", str(page))
            placed += 1

    print(f"Skill tree layout: {placed}/{len(selected)} skills placed")

    # --- Step 4: Update CharStats.txt ---
    _update_charstats(charstats_txt, target_class, selected[0].skill_name)

    return selected


def _get_vanilla_positions(
    skilldesc_txt: D2TxtFile,
    skills_txt: D2TxtFile,
    target_class: str,
) -> list[tuple[int, int, int]]:
    """Get the vanilla skill tree positions for the target class.

    Returns list of (page, row, col) tuples, one per skill slot (30 total),
    sorted by page then row then col.
    """
    # Find all vanilla skills for the target class
    target_descs = set()
    for ri in range(len(skills_txt.rows)):
        cc = skills_txt.get_value(ri, "charclass")
        if cc == target_class:
            sd = skills_txt.get_value(ri, "skilldesc")
            if sd:
                target_descs.add(sd)

    # Get their SkillDesc positions
    positions = []
    for ri in range(len(skilldesc_txt.rows)):
        name = skilldesc_txt.get_value(ri, "skilldesc")
        if name in target_descs:
            page = skilldesc_txt.get_value(ri, "SkillPage")
            row = skilldesc_txt.get_value(ri, "SkillRow")
            col = skilldesc_txt.get_value(ri, "SkillColumn")
            if page and page != "0":
                positions.append((int(page), int(row), int(col)))

    positions.sort()
    return positions


def _apply_skill_tree_layout(
    skilldesc_txt: D2TxtFile,
    selected: list[SkillEntry],
    target_class: str = "",
    vanilla_skills_txt: D2TxtFile | None = None,
    vanilla_sdesc_txt: D2TxtFile | None = None,
):
    """Place 30 selected skills into the skill tree in SkillDesc.txt.

    Uses the target class's original skill positions so icons align
    with the background image's slot positions and arrows.
    """
    # Build lookup: skilldesc name -> row index in skilldesc_txt
    sd_name_to_row: dict[str, int] = {}
    for ri in range(len(skilldesc_txt.rows)):
        name = skilldesc_txt.get_value(ri, "skilldesc")
        if name:
            sd_name_to_row[name] = ri

    selected_descs = {s.skilldesc_ref for s in selected}

    # First: hide ALL visible skills from the tree (set SkillPage=0)
    for ri in range(len(skilldesc_txt.rows)):
        page = skilldesc_txt.get_value(ri, "SkillPage")
        if page and page != "0":
            sd_name = skilldesc_txt.get_value(ri, "skilldesc")
            if sd_name not in selected_descs:
                skilldesc_txt.set_value(ri, "SkillPage", "0")

    # Get positions: use vanilla class positions if available, else fallback grid
    positions = []
    if vanilla_skills_txt and vanilla_sdesc_txt and target_class:
        positions = _get_vanilla_positions(
            vanilla_sdesc_txt, vanilla_skills_txt, target_class)

    if len(positions) < len(selected):
        # Fallback: generate grid positions (rows 1-6, cols 1-3)
        positions = []
        for tab in range(1, 4):
            for row in range(1, 7):
                for col in [1, 2, 3]:
                    positions.append((tab, row, col))
                    if len(positions) >= len(selected):
                        break
                if len(positions) >= len(selected):
                    break

    # Place selected skills at the positions
    placed = 0
    for slot_idx, skill in enumerate(selected):
        sd_ref = skill.skilldesc_ref
        if sd_ref not in sd_name_to_row:
            print(f"WARNING: No SkillDesc match for '{skill.skill_name}' "
                  f"(skilldesc='{sd_ref}')")
            continue
        if slot_idx >= len(positions):
            print(f"WARNING: No position for skill #{slot_idx} '{skill.skill_name}'")
            continue

        placed += 1
        sd_row = sd_name_to_row[sd_ref]
        page, row, col = positions[slot_idx]

        skilldesc_txt.set_value(sd_row, "SkillPage", str(page))
        skilldesc_txt.set_value(sd_row, "SkillRow", str(row))
        skilldesc_txt.set_value(sd_row, "SkillColumn", str(col))
        skilldesc_txt.set_value(sd_row, "ListRow", str(page))

    print(f"Skill tree layout: {placed}/{len(selected)} skills placed")


def _update_charstats(charstats_txt: D2TxtFile, target_class: str, start_skill: str):
    """Set the StartSkill for the target class in CharStats.txt."""
    class_name = CLASS_NAMES.get(target_class, "")
    rows = charstats_txt.find_rows("class", class_name)
    if rows:
        charstats_txt.set_value(rows[0], "StartSkill", start_skill)


def swap_skill(
    skills_txt: D2TxtFile,
    skilldesc_txt: D2TxtFile,
    equipped: list[SkillEntry],
    slot_index: int,
    new_skill: SkillEntry,
    target_class: str,
) -> list[SkillEntry]:
    """Swap a skill in an equipped slot with a new unlocked skill.

    The old skill gets hidden, the new skill takes the slot's position.
    Returns the updated equipped list.
    """
    old_skill = equipped[slot_index]

    # Remove old skill from target class (set back to original or hide)
    skills_txt.set_value(old_skill.row_index, "charclass", old_skill.original_class)

    # Assign new skill to target class
    skills_txt.set_value(new_skill.row_index, "charclass", target_class)
    skills_txt.set_value(new_skill.row_index, "reqlevel", "1")
    skills_txt.set_value(new_skill.row_index, "reqskill1", "")
    skills_txt.set_value(new_skill.row_index, "reqskill2", "")
    skills_txt.set_value(new_skill.row_index, "reqskill3", "")

    # Update SkillDesc: hide old, show new in same position
    sd_name_to_row: dict[str, int] = {}
    for ri in range(len(skilldesc_txt.rows)):
        name = skilldesc_txt.get_value(ri, "skilldesc")
        if name:
            sd_name_to_row[name] = ri

    # Get old skill's position
    old_sd = sd_name_to_row.get(old_skill.skilldesc_ref)
    if old_sd is not None:
        old_page = skilldesc_txt.get_value(old_sd, "SkillPage")
        old_row = skilldesc_txt.get_value(old_sd, "SkillRow")
        old_col = skilldesc_txt.get_value(old_sd, "SkillColumn")
        old_lr = skilldesc_txt.get_value(old_sd, "ListRow")
        # Hide old
        skilldesc_txt.set_value(old_sd, "SkillPage", "0")

        # Place new in same position
        new_sd = sd_name_to_row.get(new_skill.skilldesc_ref)
        if new_sd is not None:
            skilldesc_txt.set_value(new_sd, "SkillPage", old_page)
            skilldesc_txt.set_value(new_sd, "SkillRow", old_row)
            skilldesc_txt.set_value(new_sd, "SkillColumn", old_col)
            skilldesc_txt.set_value(new_sd, "ListRow", old_lr)

    # Update equipped list
    equipped[slot_index] = new_skill
    return equipped
