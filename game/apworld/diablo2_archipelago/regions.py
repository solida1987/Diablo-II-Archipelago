"""
Diablo II Archipelago - Region Definitions

In Skill Hunt mode: flat act regions, boss kills gate act transitions.
In Zone Explorer mode: zone key regions with access rules requiring zone keys.
"""
from typing import TYPE_CHECKING

from BaseClasses import Region

from .locations import (
    ALL_ACT_LOCATIONS, LOCATION_BASE, ACT_BOSS_QUEST_IDS,
)
from .items import ZONE_KEY_ITEMS

if TYPE_CHECKING:
    from . import Diablo2ArchipelagoWorld


# Zone key → which quest/area IDs belong to this zone
# Maps zone key name → list of area IDs that this key unlocks
# Must match g_zoneKeyDefs in d2arch.c
ZONE_KEY_AREAS = {
    "Cold Plains Key":        [3, 9, 13],
    "Burial Grounds Key":     [17, 18, 19],
    "Stony Field Key":        [4, 10, 14],
    "Dark Wood Key":          [5],
    "Black Marsh Key":        [6, 11, 15, 20, 21, 22, 23, 24, 25],
    "Tristram Key":           [38],
    "Monastery Key":          [7, 12, 16, 26, 27, 28],
    "Jail & Cathedral Key":   [29, 30, 31, 32, 33],
    "Catacombs Key":          [34, 35, 36],
    "Andariel's Lair Key":    [37],
    "Rocky Waste Key":        [41, 47, 48, 49, 55, 59],
    "Dry Hills Key":          [42, 56, 57, 60],
    "Far Oasis Key":          [43, 62, 63, 64],
    "Lost City Key":          [44, 45, 58, 61, 65],
    "Palace Key":             [50, 51, 52, 53, 54],
    "Arcane Sanctuary Key":   [74],
    "Canyon of the Magi Key": [46, 66, 67, 68, 69, 70, 71, 72],
    "Duriel's Lair Key":      [73],
    "Spider Forest Key":      [76, 84, 85],
    "Jungle Key":             [77, 78, 86, 87, 88, 89, 90, 91],
    "Kurast Key":             [79, 80, 92, 93, 94, 95],
    "Upper Kurast Key":       [81, 82, 96, 97, 98, 99],
    "Travincal Key":          [83],
    "Durance of Hate Key":    [100, 101, 102],
    "Outer Steppes Key":      [104, 105],
    "City of the Damned Key": [106],
    "River of Flame Key":     [107],
    "Chaos Sanctuary Key":    [108],
    "Bloody Foothills Key":   [110],
    "Highlands Key":          [111, 112],
    "Caverns Key":            [113, 114, 115, 116, 117],
    "Summit Key":             [118, 119, 120],
    "Nihlathak Key":          [121, 122, 123, 124],
    "Worldstone Keep Key":    [128, 129, 130],
    "Throne of Destruction Key": [131, 132],
}

# Starting areas (always open, no key needed) — area IDs
STARTING_AREAS = [1, 2, 8, 40, 75, 103, 109]  # Towns + Blood Moor + Den of Evil

# Map quest ID to the area it takes place in (approximate)
QUEST_ID_TO_AREA = {}

def _build_quest_area_map():
    """Build mapping from quest IDs to area IDs based on quest definitions."""
    # Quest ID ranges map to specific areas based on d2arch.c quest arrays
    # This is approximate — some quests (level milestones, hunts) span multiple areas
    # For those, we put them in the "starting" region so they're always accessible
    pass


def _quest_id_to_act(quest_id: int) -> int:
    if quest_id < 100: return 1
    elif quest_id < 200: return 2
    elif quest_id < 300: return 3
    elif quest_id < 400: return 4
    else: return 5


def _get_zone_for_quest(quest_id: int, quest_type: str) -> str | None:
    """Determine which zone key is needed for a quest.
    Returns zone key name, or None if freely accessible."""
    # Level milestones are always accessible (can level up anywhere)
    if quest_type == "level":
        return None

    # Map quest IDs to their area based on the quest param (area ID)
    # These mappings come from d2arch.c quest arrays
    # Kill quests: param = area ID
    # Area entry quests: param = area ID
    # Waypoint quests: param = waypoint number → area ID
    # Story/hunt quests: various

    # Act 1 quest → area mappings
    act1_quest_areas = {
        # Story quests (QTYPE_QUESTFLAG)
        1: 8,    # Den of Evil → area 8
        2: 17,   # Sisters' Burial Grounds → area 17
        3: 28,   # Tools of the Trade → area 28 (Barracks)
        4: 5,    # Search for Cain → area 5 (Dark Wood)
        5: 25,   # Forgotten Tower → area 25 (Tower L5)
        6: 37,   # Sisters to the Slaughter → area 37 (Andariel)
        # Kill quests (param = area ID directly)
        10: 2, 11: 3, 12: 4, 13: 5, 14: 6, 15: 7, 16: 8, 17: 9, 18: 10,
        19: 17, 20: 18, 21: 19, 22: 21, 23: 22, 24: 23, 25: 24, 26: 25,
        27: 26, 28: 27, 29: 28, 30: 29, 31: 30, 32: 31, 33: 33,
        34: 34, 35: 35, 36: 36, 37: 38,
        38: 13, 39: 14, 57: 11, 58: 15,  # New quests we added
        # Area entry quests
        40: 2, 41: 3, 42: 4, 43: 5, 44: 6, 45: 7, 46: 8, 47: 38, 48: 34, 49: 21,
        # Waypoint quests (mapped to area)
        50: 3, 51: 4, 52: 5, 53: 6, 54: 27, 55: 29, 56: 32, 57: 35,
        # SuperUnique hunts (approximate area)
        7: 8,    # Corpsefire → Den of Evil
        8: 3,    # Bishibosh → Cold Plains
        9: 18,   # Bonebreaker → Crypt
        70: 9,   # Coldcrow → Cave
        71: 4,   # Rakanishu → Stony Field
        72: 5,   # Treehead → Dark Wood
        73: 38,  # Griswold → Tristram
        74: 25,  # Countess → Tower L5
        75: 34,  # Pitspawn → Catacombs
        77: 33,  # Boneash → Cathedral
        80: 28,  # The Smith → Barracks
    }

    act2_quest_areas = {
        101: 49, 102: 60, 103: 61, 104: 74, 105: 74, 106: 73,
        110: 41, 111: 42, 112: 43, 113: 44, 114: 45,
        117: 56, 118: 57, 119: 60, 120: 62, 121: 63, 122: 64,
        123: 65, 124: 74, 125: 52, 126: 53, 127: 54, 128: 46, 129: 55,
        140: 41, 141: 42, 142: 43, 143: 44, 144: 74,
        150: 41, 151: 42, 152: 57, 153: 43, 154: 44, 155: 52, 156: 74, 157: 46,
        170: 49, 171: 42, 172: 61, 173: 43, 174: 52, 175: 64, 176: 44, 177: 44, 178: 74, 179: 46,
    }

    act3_quest_areas = {
        201: 95, 202: 83, 203: 76, 204: 80, 205: 83, 206: 102,
        210: 76, 211: 77, 212: 78, 213: 79, 214: 80, 215: 81, 216: 83,
        217: 84, 218: 88, 219: 89, 222: 100, 223: 101, 224: 82,
        240: 76, 241: 78, 242: 80, 243: 83, 244: 100,
        250: 76, 251: 77, 252: 78, 253: 79, 254: 80, 255: 81, 256: 83, 257: 101,
        271: 84, 272: 88, 273: 79, 274: 94, 275: 92,
        276: 83, 277: 83, 278: 83, 279: 100, 280: 101, 281: 101, 290: 76,
    }

    act4_quest_areas = {
        301: 105, 302: 107, 303: 108,
        310: 104, 311: 105, 312: 106, 313: 107, 314: 108,
        340: 104, 341: 105, 342: 106, 343: 107, 344: 108,
        350: 106, 351: 107,
        370: 104, 371: 105, 372: 106, 373: 107, 374: 108, 375: 108, 376: 108,
    }

    act5_quest_areas = {
        401: 110, 402: 112, 404: 124, 405: 120, 406: 132,
        410: 110, 411: 111, 412: 112, 413: 113, 414: 118, 415: 119,
        416: 117, 417: 122, 418: 123, 419: 124, 420: 128, 421: 129, 422: 130, 423: 131,
        440: 110, 441: 111, 443: 113, 444: 128,
        450: 111, 451: 112, 452: 113, 454: 123, 455: 115, 456: 117, 457: 129,
        470: 110, 471: 111, 472: 112, 473: 118, 474: 119, 475: 121, 477: 129,
    }

    # Combine all
    all_maps = {**act1_quest_areas, **act2_quest_areas, **act3_quest_areas, **act4_quest_areas, **act5_quest_areas}

    area_id = all_maps.get(quest_id)
    if area_id is None:
        return None  # Unknown quest → freely accessible

    # Check if this area is a starting area
    if area_id in STARTING_AREAS:
        return None

    # Find which zone key unlocks this area
    for key_name, areas in ZONE_KEY_AREAS.items():
        if area_id in areas:
            return key_name

    return None  # Area not gated by any key


def create_regions(world: "Diablo2ArchipelagoWorld") -> None:
    """Create regions with access rules based on game mode."""
    multiworld = world.multiworld
    player = world.player
    game_mode = world.options.game_mode.value  # 0=Skill Hunt, 1=Zone Explorer

    goal = world.options.goal.value
    max_act = (goal // 3) + 1
    diff_scope = goal % 3

    active_locations = world.get_active_locations()

    menu_region = Region("Menu", player, multiworld)
    multiworld.regions.append(menu_region)

    if game_mode == 1:
        # === ZONE EXPLORER: Create per-zone regions with key requirements ===

        # "Open" region for starting areas + level milestones
        open_region = Region("Open Areas", player, multiworld)
        multiworld.regions.append(open_region)
        menu_region.connect(open_region)

        # Create act transition regions for boss kills (needed before zone keys)
        act_boss_regions: dict[int, Region] = {}
        for act_num in range(2, max_act + 1):
            region = Region(f"Act {act_num} Town", player, multiworld)
            multiworld.regions.append(region)
            act_boss_regions[act_num] = region

        # Connect act towns via boss kills
        boss_connections = [
            (1, 2, "Sisters to the Slaughter"),
            (2, 3, "Seven Tombs"),
            (3, 4, "The Guardian"),
            (4, 5, "Terror's End"),
        ]
        active_loc_names = {name for (_, name, _, _, _, _) in active_locations}

        for from_act, to_act, boss_loc in boss_connections:
            if to_act not in act_boss_regions:
                continue
            to_region = act_boss_regions[to_act]

            if boss_loc in active_loc_names:
                open_region.connect(
                    to_region,
                    f"Act {from_act} -> Act {to_act}",
                    lambda state, loc=boss_loc, p=player, ds=diff_scope: (
                        state.can_reach_location(loc, p)
                        or (ds >= 1 and state.can_reach_location(loc + " (Nightmare)", p))
                        or (ds >= 2 and state.can_reach_location(loc + " (Hell)", p))
                    ),
                )
            else:
                open_region.connect(to_region, f"Act {from_act} -> Act {to_act}")

        # Create a region per zone key, connected from the correct act
        # Act 1 zones connect from open_region (always accessible)
        # Act 2+ zones connect from their act town region (requires boss kill)
        zone_regions: dict[str, Region] = {}
        for ap_id, key_name, act, classification in ZONE_KEY_ITEMS:
            if act > max_act:
                continue
            region = Region(key_name.replace(" Key", ""), player, multiworld)
            multiworld.regions.append(region)
            zone_regions[key_name] = region

            # Determine parent region: Act 1 zones from open, Act 2+ from act town
            if act == 1:
                parent_region = open_region
            elif act in act_boss_regions:
                parent_region = act_boss_regions[act]
            else:
                parent_region = open_region  # fallback

            parent_region.connect(
                region,
                f"Unlock {key_name}",
                lambda state, k=key_name, p=player: state.has(k, p),
            )

        # Collect all boss quest IDs (these must NOT be behind zone keys)
        boss_quest_ids = set()
        for act_id, qid in ACT_BOSS_QUEST_IDS.items():
            boss_quest_ids.add(qid)

        # Place locations into correct regions
        for quest_id, loc_name, quest_type, classification, loc_id, diff in active_locations:
            zone_key = _get_zone_for_quest(quest_id, quest_type)
            act_num = _quest_id_to_act(quest_id)

            # Boss kill locations MUST be in their act's zone region (not behind zone keys)
            # This prevents circular dependencies: boss kill gates next act,
            # but boss kill can't be behind a zone key that requires the next act
            base_quest_id = quest_id if diff == 0 else quest_id  # same for all difficulties
            if base_quest_id in boss_quest_ids:
                if act_num > 1 and act_num in act_boss_regions:
                    target_region = act_boss_regions[act_num]
                else:
                    target_region = open_region
            elif zone_key and zone_key in zone_regions:
                target_region = zone_regions[zone_key]
            elif act_num > 1 and act_num in act_boss_regions:
                target_region = act_boss_regions[act_num]
            else:
                target_region = open_region

            loc = world.create_location(loc_name, loc_id, target_region)
            target_region.locations.append(loc)

    else:
        # === SKILL HUNT: Simple act regions (original behavior) ===
        act_regions: dict[int, Region] = {}
        for act_num in range(1, max_act + 1):
            region = Region(f"Act {act_num}", player, multiworld)
            multiworld.regions.append(region)
            act_regions[act_num] = region

        if 1 in act_regions:
            menu_region.connect(act_regions[1])

        boss_connections = [
            (1, 2, "Sisters to the Slaughter"),
            (2, 3, "Seven Tombs"),
            (3, 4, "The Guardian"),
            (4, 5, "Terror's End"),
        ]
        active_loc_names = {name for (_, name, _, _, _, _) in active_locations}

        for from_act, to_act, boss_loc in boss_connections:
            if from_act in act_regions and to_act in act_regions:
                if boss_loc in active_loc_names:
                    act_regions[from_act].connect(
                        act_regions[to_act],
                        f"Act {from_act} -> Act {to_act}",
                        lambda state, loc=boss_loc, p=player, ds=diff_scope: (
                            state.can_reach_location(loc, p)
                            or (ds >= 1 and state.can_reach_location(loc + " (Nightmare)", p))
                            or (ds >= 2 and state.can_reach_location(loc + " (Hell)", p))
                        ),
                    )
                else:
                    act_regions[from_act].connect(
                        act_regions[to_act],
                        f"Act {from_act} -> Act {to_act}",
                    )

        # Place locations into act regions
        for quest_id, loc_name, quest_type, classification, loc_id, diff in active_locations:
            act_num = _quest_id_to_act(quest_id)
            if act_num in act_regions:
                loc = world.create_location(loc_name, loc_id, act_regions[act_num])
                act_regions[act_num].locations.append(loc)
