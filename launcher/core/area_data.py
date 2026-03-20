"""Diablo II area and monster data mapping.

Builds a complete catalog of all areas, their monsters, and kill requirements.
Data is extracted from vanilla Levels.txt and MonStats.txt.
"""

import os
from dataclasses import dataclass, field
from .d2_data import D2TxtFile

VANILLA_TXT_DIR = os.path.join(os.path.dirname(__file__), "..", "data", "vanilla_txt")


@dataclass
class MonsterInfo:
    monster_id: str       # e.g. "zombie1"
    display_name: str     # e.g. "Zombie"
    base_id: str = ""     # Base monster class


@dataclass
class AreaInfo:
    area_id: int
    name: str             # Internal name (e.g. "Act 1 - Wilderness 1")
    display_name: str     # Display name (e.g. "Blood Moor")
    act: int              # 1-5
    is_town: bool = False
    monsters: list[str] = field(default_factory=list)  # Monster IDs
    kills_per_type: int = 50  # Kills needed per monster type for a check


def build_monster_lookup() -> dict[str, MonsterInfo]:
    """Build monster ID -> MonsterInfo from MonStats.txt."""
    path = os.path.join(VANILLA_TXT_DIR, "MonStats.txt")
    if not os.path.exists(path):
        return {}

    monstats = D2TxtFile.from_file(path)
    result = {}

    for ri in range(len(monstats.rows)):
        mid = monstats.get_value(ri, "Id").strip()
        name_str = monstats.get_value(ri, "NameStr").strip()
        base_id = monstats.get_value(ri, "BaseId").strip()

        if mid and name_str:
            result[mid] = MonsterInfo(
                monster_id=mid,
                display_name=name_str,
                base_id=base_id,
            )

    return result


def build_area_map() -> dict[int, AreaInfo]:
    """Build area ID -> AreaInfo from Levels.txt."""
    path = os.path.join(VANILLA_TXT_DIR, "Levels.txt")
    if not os.path.exists(path):
        return {}

    levels = D2TxtFile.from_file(path)
    result = {}

    for ri in range(len(levels.rows)):
        name = levels.get_value(ri, "Name").strip()
        lid_str = levels.get_value(ri, "Id").strip()
        act_str = levels.get_value(ri, "Act").strip()
        display_name = levels.get_value(ri, "LevelName").strip()

        if not name or name == "Null" or not lid_str:
            continue

        try:
            lid = int(lid_str)
        except ValueError:
            continue

        act = (int(act_str) + 1) if act_str else 0
        is_town = "Town" in name

        # Collect monster IDs
        monsters = []
        seen = set()
        for m in range(1, 11):
            mon = levels.get_value(ri, f"mon{m}").strip()
            if mon and mon not in seen:
                monsters.append(mon)
                seen.add(mon)

        # Kill target: 50 kills per monster type
        kills_per_type = 50 if (not is_town and monsters) else 0

        result[lid] = AreaInfo(
            area_id=lid,
            name=name,
            display_name=display_name or name,
            act=act,
            is_town=is_town,
            monsters=monsters,
            kills_per_type=kills_per_type,
        )

    return result


# Pre-built data (loaded once)
_monster_lookup: dict[str, MonsterInfo] | None = None
_area_map: dict[int, AreaInfo] | None = None


def get_monster_lookup() -> dict[str, MonsterInfo]:
    global _monster_lookup
    if _monster_lookup is None:
        _monster_lookup = build_monster_lookup()
    return _monster_lookup


def get_area_map() -> dict[int, AreaInfo]:
    global _area_map
    if _area_map is None:
        _area_map = build_area_map()
    return _area_map


def get_area_monsters_display(area_id: int) -> list[tuple[str, str]]:
    """Get (monster_id, display_name) pairs for an area."""
    areas = get_area_map()
    monsters = get_monster_lookup()

    area = areas.get(area_id)
    if not area:
        return []

    result = []
    for mid in area.monsters:
        info = monsters.get(mid)
        display = info.display_name if info else mid
        result.append((mid, display))
    return result
