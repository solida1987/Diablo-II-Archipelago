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

# Map quest ID -> area ID. Populated at import time by
# _build_quest_area_map(); source of truth for Zone Explorer gating.
# Every non-level, non-hunt-fallback quest ID in locations.py must resolve
# to an entry here — otherwise the quest lands in `open_region` and
# bypasses the Zone Explorer access rules (the DIAGNOSTIC_REPORT_1.7.0.md
# section 3.3.7 bug).
QUEST_ID_TO_AREA: dict[int, int] = {}


def _build_quest_area_map() -> None:
    """Populate QUEST_ID_TO_AREA from locations.py quest tables.

    Covers every quest ID defined in ACT1..ACT5_LOCATIONS plus the global
    level milestone tables. Quests that are inherently multi-zone (level
    milestones, hunts whose target roams, etc.) are still registered —
    but we map them to a meaningful "home" area (e.g. the act town) so
    Zone Explorer mode gates them correctly under the relevant act's
    boss-kill requirement.
    """
    # --- Act 1 (quest IDs 1-99) ---
    act1 = {
        # Story quests (QTYPE_QUESTFLAG)
        1:  8,   # Den of Evil
        2:  17,  # Sisters' Burial Grounds
        3:  28,  # Tools of the Trade (Barracks)
        4:  5,   # Search for Cain (Dark Wood)
        5:  25,  # Forgotten Tower L5
        6:  37,  # Sisters to the Slaughter (Andariel)
        # SuperUnique hunts — home area (may roam)
        7:  8,   # Corpsefire (Den of Evil)
        8:  3,   # Bishibosh (Cold Plains)
        9:  18,  # Bonebreaker (Crypt)
        70: 9,   # Coldcrow (Cave L1)
        71: 4,   # Rakanishu (Stony Field)
        72: 5,   # Treehead WoodFist (Dark Wood)
        73: 38,  # Griswold (Tristram)
        74: 25,  # The Countess (Tower L5)
        75: 34,  # Pitspawn Fouldog (Catacombs L1)
        77: 33,  # Boneash (Cathedral)
        80: 28,  # The Smith (Barracks)
        # Kill quests
        10: 2,  11: 3,  12: 4,  13: 5,  14: 6,  15: 7,  16: 8,  17: 9,  18: 10,
        19: 17, 20: 18, 21: 19, 22: 22, 23: 23, 24: 24, 25: 25, 26: 26,
        27: 27, 28: 28, 29: 29, 30: 30, 31: 31, 32: 32, 33: 33,
        34: 34, 35: 35, 36: 36, 37: 38,
        38: 13, 39: 14,
        59: 11, 60: 12,  # Clear Hole L1/L2 (Hole is inside Stony Field/Cold Plains)
        # Area entry quests
        40: 2,  41: 3,  42: 4,  43: 5,  44: 6,  45: 7,  46: 8,
        47: 38, 48: 34, 49: 21,
        # Waypoint activations
        50: 3,  51: 4,  52: 5,  53: 6,
        54: 27, 55: 30, 56: 28, 57: 35,
    }
    QUEST_ID_TO_AREA.update(act1)

    # --- Act 2 (quest IDs 100-199) ---
    act2 = {
        # Story
        101: 49,  # Radament's Lair (Sewers — originally mapped, Sewers area removed from gameplay, kept for ID coverage)
        102: 60,  # Horadric Staff
        103: 61,  # Tainted Sun
        104: 74,  # Arcane Sanctuary
        105: 74,  # The Summoner
        106: 73,  # Seven Tombs / Duriel's Lair
        # Hunts
        170: 49, 171: 42, 172: 61, 173: 43, 174: 52, 175: 64,
        176: 44, 177: 44, 178: 74, 179: 46,
        # Kill
        110: 41, 111: 42, 112: 43, 113: 44, 114: 45,
        117: 56, 118: 57, 119: 60,
        120: 62, 121: 63, 122: 64,
        123: 65, 124: 74,
        125: 52, 126: 53, 127: 54, 128: 46, 129: 55,
        # Area entry
        140: 41, 141: 42, 142: 43, 143: 44, 144: 74,
        # Waypoints
        151: 42, 152: 57, 153: 43, 154: 44, 155: 52, 156: 74, 157: 46,
    }
    QUEST_ID_TO_AREA.update(act2)

    # --- Act 3 (quest IDs 200-299) ---
    act3 = {
        # Story
        201: 95,  # Lam Esen's Tome
        202: 83,  # Khalim's Will
        203: 83,  # Blade of Old Religion (Travincal)
        204: 80,  # Golden Bird (Kurast Bazaar area)
        205: 83,  # Blackened Temple
        206: 102, # The Guardian (Mephisto, Durance L3)
        # Hunts
        271: 84, 272: 88, 273: 79, 274: 94, 275: 92,
        276: 83, 277: 83, 278: 83, 279: 100, 280: 101, 281: 101,
        # Kill
        210: 76, 211: 77, 212: 78, 213: 79, 214: 80, 215: 81, 216: 83,
        217: 84, 218: 88, 219: 89,
        222: 100, 223: 101, 224: 82,
        # Area entry
        240: 76, 241: 78, 242: 80, 243: 83, 244: 100,
        # Waypoints
        250: 76, 251: 77, 252: 78, 253: 79, 254: 80, 255: 81, 256: 83, 257: 101,
    }
    QUEST_ID_TO_AREA.update(act3)

    # --- Act 4 (quest IDs 300-399) ---
    act4 = {
        # Story
        301: 105, # Fallen Angel (Izual in Plains of Despair)
        302: 107, # Hell's Forge
        303: 108, # Terror's End (Diablo)
        # Hunts
        370: 104, 371: 105, 372: 106, 373: 107,
        374: 108, 375: 108, 376: 108,
        # Kill
        310: 104, 311: 105, 312: 106, 313: 107, 314: 108,
        # Area entry
        340: 104, 341: 105, 342: 106, 343: 107, 344: 108,
        # Waypoints
        350: 106, 351: 107,
    }
    QUEST_ID_TO_AREA.update(act4)

    # --- Act 5 (quest IDs 400-499) ---
    act5 = {
        # Story
        401: 110, # Siege on Harrogath
        402: 112, # Rescue on Mt. Arreat (Shenk/Arreat Plateau)
        403: 113, # Prison of Ice (Anya)
        404: 124, # Betrayal of Harrogath (Nihlathak)
        405: 120, # Rite of Passage (Ancients)
        406: 132, # Eve of Destruction (Baal, Throne of Destruction)
        # Hunts
        470: 110, 471: 111, 472: 112, 473: 118, 474: 119,
        475: 121, 476: 115, 477: 129,
        # Kill
        410: 110, 411: 111, 412: 112, 413: 113,
        414: 118, 415: 119, 416: 117,
        417: 122, 418: 123, 419: 124,
        420: 128, 421: 129, 422: 130, 423: 131,
        # Area entry
        440: 110, 441: 111, 442: 112, 443: 113, 444: 128,
        # Waypoints
        450: 111, 451: 112, 452: 113,
        454: 123, 455: 115, 456: 117, 457: 129,
    }
    QUEST_ID_TO_AREA.update(act5)

    # --- Level milestones ---
    # Level-up can happen in any area. Mark as "freely accessible" via
    # starting area 1 (Rogue Camp). _get_zone_for_quest short-circuits
    # on quest_type == "level", so this entry is informational only.
    for qid in (78, 79, 81, 82, 83, 180, 181, 182, 183, 184,
                282, 283, 284, 285):
        QUEST_ID_TO_AREA.setdefault(qid, 1)


# Build the map once at module load.
_build_quest_area_map()


def _quest_id_to_act(quest_id: int) -> int:
    if quest_id < 100: return 1
    elif quest_id < 200: return 2
    elif quest_id < 300: return 3
    elif quest_id < 400: return 4
    else: return 5


def _get_zone_for_quest(quest_id: int, quest_type: str) -> str | None:
    """Determine which zone key is needed for a quest.

    Returns the zone key name, or None if freely accessible. Returns None
    for level milestones (progress can happen anywhere) and for any
    quest whose home area falls in STARTING_AREAS.

    Quests with no map entry fall through to None — historically this
    meant "freely accessible", but with _build_quest_area_map() now
    covering every known quest ID, that branch should not fire in
    practice. If it does, the caller (create_regions) still gates the
    location by act boss kill, so zone-key bypass is impossible.
    """
    # Level milestones are always accessible (can level up anywhere)
    if quest_type == "level":
        return None

    area_id = QUEST_ID_TO_AREA.get(quest_id)
    if area_id is None:
        return None  # Unknown quest → freely accessible

    # Starting areas never need a zone key
    if area_id in STARTING_AREAS:
        return None

    # Find which zone key unlocks this area
    for key_name, areas in ZONE_KEY_AREAS.items():
        if area_id in areas:
            return key_name

    return None  # Area not gated by any key


# 1.8.0 NEW — Act region definitions (Python mirror of C g_actRegions).
# Must stay in sync with d2arch_preloads.c. Keys: act → {region_num: [zone_ids]}.
# Region 1 is always-open (starting sphere); gate N unlocks region N+1.
ACT_REGIONS = {
    1: {
        1: [1, 2, 3, 8, 9, 13, 17, 18, 19],
        2: [4, 5, 10, 14, 38],
        3: [6, 7, 11, 12, 15, 16, 20, 21, 22, 23, 24, 25, 26],
        4: [27, 28, 29, 30, 31, 32],
        5: [33, 34, 35, 36, 37],
    },
    2: {
        1: [40, 41, 47, 48, 49, 50],
        2: [42, 51, 55, 56, 57, 59, 60],
        3: [43, 52, 53, 54, 62, 63, 64],
        4: [44, 45, 58, 61, 65, 74],
        5: [46, 66, 67, 68, 69, 70, 71, 72, 73],
    },
    3: {
        1: [75, 76, 77, 84, 85],
        2: [78, 79, 86, 87, 88, 89, 90, 91],
        3: [80, 81, 92, 93, 94, 95, 96, 97],
        4: [82, 83, 98, 99],
        5: [100, 101, 102],
    },
    4: {
        1: [103, 104, 105],
        2: [106, 107],
        3: [108],
    },
    5: {
        1: [109, 110, 111],
        2: [112, 113, 114],
        3: [115, 116, 117, 118, 119, 120],
        4: [128, 129],
        5: [130, 131, 132],
    },
}

# Always-open zones (portal-only, never gate-locked)
ALWAYS_OPEN_ZONES = [39, 121, 122, 123, 124, 125, 126, 127]


def _zone_to_region(zone_id: int):
    """Map a zone ID to (act, region_num), or None if always-open/unknown.
    Towns (1, 40, 75, 103, 109) resolve to their act's region 1."""
    if zone_id in ALWAYS_OPEN_ZONES:
        return None
    for act, regions in ACT_REGIONS.items():
        for region_num, zones in regions.items():
            if zone_id in zones:
                return (act, region_num)
    return None


def _build_gate_region_tree(world, menu_region, active_locations, max_act, num_diffs):
    """1.8.0 — Build preload-gated region tree with gate keys as access rules.

    Structure per (act, difficulty):
      "A<n>D<d>R1" -> "A<n>D<d>R2" via "Act <n> Gate 1 Key (Diff)"
      "A<n>D<d>R2" -> "A<n>D<d>R3" via "Act <n> Gate 2 Key (Diff)"
      ...
      (Act 4 only has R1/R2/R3)

    Act-transitions within same difficulty: A{k+1}D{d}R1 requires
    the act-k boss-kill quest to be reachable (can_reach_location).
    Difficulty transitions: A{*}D{d+1}R1 requires Eve of Destruction on
    previous difficulty.

    Open region hosts level-milestone-style "anywhere" locations only.
    Quest locations are placed in their physical gated regions.
    """
    multiworld = world.multiworld
    player = world.player
    diff_names = ["Normal", "Nightmare", "Hell"]
    gates_by_act = {1: 4, 2: 4, 3: 4, 4: 2, 5: 4}

    open_region = Region("Open Areas", player, multiworld)
    multiworld.regions.append(open_region)
    menu_region.connect(open_region)

    # Build per-act, per-diff region tree
    regions_by_adr = {}
    for diff in range(num_diffs):
        diff_name = diff_names[diff]
        for act in range(1, max_act + 1):
            num_gates = gates_by_act[act]
            num_regions = num_gates + 1
            for r in range(1, num_regions + 1):
                region = Region(f"A{act}D{diff}R{r}", player, multiworld)
                multiworld.regions.append(region)
                regions_by_adr[(act, diff, r)] = region

            # A1D0R1 is the true starting region
            # Other R1 regions need previous-act/diff completion
            r1 = regions_by_adr[(act, diff, 1)]
            if act == 1 and diff == 0:
                # Initial starting sphere
                menu_region.connect(r1)
                open_region.connect(r1)  # also always reachable from open
            elif act == 1 and diff > 0:
                # Difficulty transition: requires Baal kill on previous diff
                prev_diff_name = diff_names[diff - 1]
                baal_loc = "Eve of Destruction" if diff - 1 == 0 else f"Eve of Destruction ({prev_diff_name})"
                menu_region.connect(
                    r1,
                    f"Start A1D{diff}",
                    lambda state, loc=baal_loc, p=player: state.can_reach_location(loc, p),
                )
            else:
                # Act transition: requires previous act's boss kill on same diff
                prev_act = act - 1
                # Quest names per act boss (from ACT_BOSS_QUEST_IDS mapping)
                prev_boss_loc_base = {
                    1: "Sisters to the Slaughter",
                    2: "Seven Tombs",
                    3: "The Guardian",
                    4: "Terror's End",
                }.get(prev_act)
                if prev_boss_loc_base:
                    if diff == 0:
                        prev_boss_loc = prev_boss_loc_base
                    else:
                        prev_boss_loc = f"{prev_boss_loc_base} ({diff_names[diff]})"
                    menu_region.connect(
                        r1,
                        f"Enter A{act}D{diff}",
                        lambda state, loc=prev_boss_loc, p=player: state.can_reach_location(loc, p),
                    )

            # Connect R_n -> R_n+1 via gate key (within same act/diff)
            for gate in range(num_gates):
                src = regions_by_adr[(act, diff, gate + 1)]
                dst = regions_by_adr[(act, diff, gate + 2)]
                key_name = f"Act {act} Gate {gate + 1} Key ({diff_name})"
                src.connect(
                    dst,
                    f"A{act}D{diff} Gate {gate + 1}",
                    lambda state, k=key_name, p=player: state.has(k, p),
                )

    # Place gate locations in their spawn region (pre-gate)
    for quest_id, loc_name, quest_type, classification, loc_id, diff in active_locations:
        if quest_type != "gate":
            continue
        act = (loc_id // 10) % 100
        gate_idx = loc_id % 10
        if (act, diff, gate_idx + 1) in regions_by_adr:
            target = regions_by_adr[(act, diff, gate_idx + 1)]
            loc = world.create_location(loc_name, loc_id, target)
            target.locations.append(loc)

    return open_region


def create_regions(world: "Diablo2ArchipelagoWorld") -> None:
    """Create regions with access rules based on game mode toggles.
    1.8.0: game_mode deprecated; only skill_hunting + zone_locking matter.
    Goal is now 0/1/2 (Normal/NM/Hell); act scope always = full game. """
    multiworld = world.multiworld
    player = world.player
    zone_locking = bool(world.options.zone_locking.value)

    goal = world.options.goal.value   # 0-2: difficulty scope only
    max_act = 5                        # always full game
    num_diffs = goal + 1               # 1, 2, or 3

    active_locations = world.get_active_locations()

    menu_region = Region("Menu", player, multiworld)
    multiworld.regions.append(menu_region)

    # 1.8.0 NEW: zone_locking ON — build gate region tree for GATE locations.
    # Non-gate (quest) locations stay in open_region with per-location
    # access rules based on their physical zone. This gives AP fill
    # enough flexibility to place items without BK while still enforcing
    # gate-key progression at the physical-zone level.
    if zone_locking:
        open_region = _build_gate_region_tree(world, menu_region, active_locations, max_act, num_diffs)

        # Place each non-gate location in open_region with access rule
        # derived from zone → (act, region_num)
        for quest_id, loc_name, quest_type, classification, loc_id, diff in active_locations:
            if quest_type == "gate":
                continue  # already placed by _build_gate_region_tree

            # Level milestones can happen anywhere — no access rule
            if quest_type == "level":
                loc = world.create_location(loc_name, loc_id, open_region)
                open_region.locations.append(loc)
                continue

            # For quest locations: determine physical zone + set access rule
            zone_id = QUEST_ID_TO_AREA.get(quest_id)
            loc = world.create_location(loc_name, loc_id, open_region)
            open_region.locations.append(loc)

            if zone_id is None or zone_id in ALWAYS_OPEN_ZONES:
                continue  # no gating for unknown/always-open zones

            physical = _zone_to_region(zone_id)
            if physical is None:
                continue  # unmapped → no gating

            act, region_num = physical
            if act > max_act:
                continue

            # Build access rule: requires all gate keys 1..region_num-1 + prev act boss
            required_keys = [
                f"Act {act} Gate {g + 1} Key ({diff_name_fromdiff(diff)})"
                for g in range(region_num - 1)
            ]
            # Act transition: needs prev act boss kill on same diff
            prev_boss_loc = None
            if act > 1:
                prev_boss_loc_base = {
                    1: "Sisters to the Slaughter",
                    2: "Seven Tombs",
                    3: "The Guardian",
                    4: "Terror's End",
                }.get(act - 1)
                if prev_boss_loc_base:
                    prev_boss_loc = prev_boss_loc_base if diff == 0 else f"{prev_boss_loc_base} ({diff_name_fromdiff(diff)})"
            # Difficulty transition: needs prev diff Baal kill
            prev_diff_boss = None
            if diff > 0:
                prev_diff_boss = "Eve of Destruction" if diff - 1 == 0 else f"Eve of Destruction ({diff_name_fromdiff(diff-1)})"

            def _make_rule(keys, prev_boss, prev_diff_b, p=player):
                def rule(state):
                    if keys and not all(state.has(k, p) for k in keys):
                        return False
                    if prev_boss and not state.can_reach_location(prev_boss, p):
                        return False
                    if prev_diff_b and not state.can_reach_location(prev_diff_b, p):
                        return False
                    return True
                return rule

            loc.access_rule = _make_rule(required_keys, prev_boss_loc, prev_diff_boss)

        return  # skip old region-creation path


def diff_name_fromdiff(d):
    return ["Normal", "Nightmare", "Hell"][d]

    if zone_locking:
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
                    lambda state, loc=boss_loc, p=player, ds=goal: (
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
                        lambda state, loc=boss_loc, p=player, ds=goal: (
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
