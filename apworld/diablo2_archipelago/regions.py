"""
Diablo II Archipelago - Region Definitions

In Skill Hunt mode: flat act regions, boss kills gate act transitions.
In Zone Explorer mode: zone key regions with access rules requiring zone keys.
"""
from typing import TYPE_CHECKING

from BaseClasses import Region, Item, ItemClassification as _IC

from .locations import (
    ALL_ACT_LOCATIONS, LOCATION_BASE, ACT_BOSS_QUEST_IDS,
    QUEST_ID_TO_LEVEL, QUEST_ID_TO_MAX_ACTS,
)
from .items import BOSS_TOKEN_ITEM_ID

if TYPE_CHECKING:
    from . import Diablo2ArchipelagoWorld


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
        # Pitspawn Fouldog is in JAIL LEVEL 2 (30), not the Catacombs. The name
        # sends everyone to the Pit and the table sent the logic to the
        # Catacombs; he has always been in a dead-end corner of Jail 2, which
        # the game's own presets say plainly — Act1/Barracks/JailWPitspawn.ds1
        # and one each for E/S/N. Caught when a player cleared the Pit, then the
        # Catacombs, and found nothing.
        # This made the hunt gate on a LATER region than the one it lives in, so
        # nothing became unreachable — but a key placed there was held back
        # further than it should have been.
        75: 30,  # Pitspawn Fouldog (Jail L2)
        77: 33,  # Boneash (Cathedral)
        80: 28,  # The Smith (Barracks)
        # Kill quests
        10: 2,  11: 3,  12: 4,  13: 5,  14: 6,  15: 7,  16: 8,  17: 9,  18: 10,
        19: 17, 20: 18, 21: 19, 22: 22, 23: 23, 24: 24, 25: 25, 26: 26,
        # 3.3.5 — q27 is the Monastery Gate, area 26 (region 3); it was
        # mapped to 27 (Outer Cloister, region 4), one gate too strict.
        27: 26, 28: 28, 29: 29, 30: 30, 31: 31, 32: 32, 33: 33,
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
        115: 47, 116: 48,          # Sewers L1/L2 (restored 3.5.3)
        117: 56, 118: 57, 119: 60,
        120: 62, 121: 63, 122: 64,
        123: 65, 124: 74,
        125: 52, 126: 53, 127: 54, 128: 46, 129: 55,
        # Area entry
        140: 41, 141: 42, 142: 43, 143: 44, 144: 74,
        # Waypoints
        150: 48,                   # Sewers WP sits in Sewers Level 2
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
        220: 92, 221: 93,          # Kurast Sewers L1/L2 (restored 3.5.3)
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

    # --- 3.3.5: the 33 that were missing -------------------------------
    # Every zone here is copied from the DLL's own quest table
    # (d2arch_quests.c, the `param` field of a KILL quest = the area id),
    # so the model matches what the game actually does. Without them these
    # locations fell through to "no zone" and were treated as reachable
    # from the start, which let Archipelago place an act's gate key inside
    # a region that same key is needed to open — a dead seed. The worst
    # case was "Clear Catacombs L4": physically behind all four Act 1
    # gates, modelled as free.
    QUEST_ID_TO_AREA.update({
        # Act 1
        58: 12,   # Clear Pit Level 1            (R3)
        61: 16,   # Clear Pit Level 2            (R3)
        62: 32,   # Clear Inner Cloister         (R4)
        63: 37,   # Clear Catacombs L4           (R5)
        # Act 2
        130: 49,  # Clear Sewers L3              (R1)
        131: 51,  # Clear Harem L2               (R2)
        134: 59,  # Clear Stony Tomb L2          (R2)
        132: 58,  # Clear Claw Viper Temple L1   (R4)
        133: 61,  # Clear Claw Viper Temple L2   (R4)
        135: 66, 136: 67, 137: 68, 138: 69,      # Tal Rasha's Tomb 1-4 (R5)
        139: 70, 145: 71, 146: 72,               # Tal Rasha's Tomb 5-7 (R5)
        # Act 3
        230: 85,  # Clear Spider Cavern          (R1)
        231: 86, 232: 87, 233: 90,               # Swampy Pit L1-L3     (R2)
        234: 91,  # Clear Flayer Dungeon L3      (R2)
        235: 94, 236: 95, 237: 96, 238: 97,      # Kurast temples       (R3)
        239: 98,  # Clear Ruined Fane            (R4)
        245: 99,  # Clear Disused Reliquary      (R4)
        # Act 5
        430: 114,  # Clear Cellar of Pity         (R2)
        431: 115,  # Clear Crystalized Cavern L2  (R3)
        432: 116,  # Clear Echo Chamber           (R3)
        # Pandemonium mini-ubers: their own zones, which are in
        # ALWAYS_OPEN_ZONES because the DLL never zone-locks them.
        433: 125, 434: 126, 435: 127,
    })

    # --- Level milestones ---
    # Level-up can happen in any area. Entries here are informational only;
    # quest_type == "level" is handled separately in create_regions.
    for qid in (78, 79, 81, 82, 83, 180, 181, 182, 183, 184,
                282, 283, 284, 285):
        QUEST_ID_TO_AREA.setdefault(qid, 1)


# Build the map once at module load.
_build_quest_area_map()


def _assert_every_quest_is_mapped() -> None:
    """A quest with no zone silently loses its gate rule, so refuse to load.

    The file has claimed this invariant in a comment since 1.7.0 and never
    checked it; 33 quests had drifted out of the map by 3.3.4, every one of
    them a place Archipelago could hide a gate key behind its own gate.
    Tools/ap_model_vs_dll.py checks the same thing against the DLL's tables,
    but this runs at generation time, where it can actually stop a bad seed.
    """
    unmapped = sorted(
        row[0]
        for act_locs in ALL_ACT_LOCATIONS
        for row in act_locs
        if row[2] != "level" and row[0] not in QUEST_ID_TO_AREA
    )
    if unmapped:
        raise AssertionError(
            "diablo2_archipelago: quest ids with no QUEST_ID_TO_AREA entry "
            f"({len(unmapped)}): {unmapped}. Every non-level quest needs a "
            "zone or it bypasses zone-locking and can hide a gate key behind "
            "its own gate. Zones come from d2arch_quests.c."
        )


_assert_every_quest_is_mapped()


class _BossToken(Item):
    """Locked progression token pre-placed at act boss / diff-transition locations.

    Each active act boss location (Sisters to the Slaughter, Seven Tombs, etc.)
    and each difficulty-transition location (Eve of Destruction Normal/NM) gets
    one of these placed on it. The act-to-act and diff-to-diff region connections
    then gate on state.has(token_name) instead of can_reach_location(), creating
    real sphere depth: you must physically reach and complete each act boss to
    unlock the next act or difficulty.

    These tokens are NOT added to the random item pool — they are locked at their
    specific locations and are never sent to other players. AP fill sees them as
    already-placed progression items and sweeps them when their location becomes
    reachable, expanding the state and opening the next region.
    """
    game = "Diablo II Archipelago"


def _quest_id_to_act(quest_id: int) -> int:
    if quest_id < 100: return 1
    elif quest_id < 200: return 2
    elif quest_id < 300: return 3
    elif quest_id < 400: return 4
    else: return 5


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

# Always-open zones: the DLL never gate-locks these (they are portal-only, so
# there is no map edge to lock), which is why they are absent from ACT_REGIONS.
#
# 3.3.5 — "never gate-locked" is NOT the same as "reachable from the start",
# and treating it that way was a way to lose a seed. Nihlathak's Temple and the
# three Halls (121-124) are entered through the red portal Anya opens, and Anya
# is in the Frozen River — Act 5 region 2, one gate key in. So Archipelago must
# not consider them free: it would happily hide the Act 5 key that opens region
# 2 behind a portal that only exists once region 2 is open. "Betrayal of
# Harrogath" is a story quest and is generated even with every quest category
# switched off, so this could bite any zone-locking seed.
#
# The Pandemonium zones (125-127) and Tristram (39) really are free of gate
# keys: Tristram's portal is in Stony Field (region 1) and the uber portals are
# built from cube recipes, so they stay here.
ALWAYS_OPEN_ZONES = [39, 125, 126, 127]

# Zone -> the region whose key is needed to reach the portal INTO that zone.
PORTAL_ENTRY_REGION = {
    121: (5, 2), 122: (5, 2), 123: (5, 2), 124: (5, 2),   # Anya's red portal
}

# 3.3.5 — zones entrance shuffle is allowed to move, straight from the DLL's
# dungeon-set table (d2arch_levelshuffle.c; the six sets carrying
# DS_FLAG_NO_SHUFFLE are pinned and therefore absent here).
#
# Entrance shuffle deals across ACTS on purpose — pool A is acts 1+2, pool B is
# acts 3+4+5 — and it uses a Sattolo permutation, which has no fixed points, so
# every zone below genuinely ends up somewhere else. The region model, however,
# describes VANILLA topology and has no way to know where a dungeon went. That
# is fine on its own; it stops being fine when zone locking is also on, because
# then Archipelago may hide a gate key in a cave that shuffle has just moved
# behind the very gate that key opens (the classic report: "the Act 1 key was in
# Act 4"). Checks in these zones are therefore made junk-only for the duration
# of such a seed — see _shuffle_excluded_quest_ids.
SHUFFLABLE_ZONES = {
    # Pool A — acts 1 + 2
    8, 9, 11, 12, 13, 15, 16, 18, 19, 20, 21, 22, 23, 24, 25,
    47, 48, 49, 55, 59, 65, 66, 67, 68, 69, 70, 71, 72,
    # Pool B — acts 3 + 4 + 5 (+ the cow portal)
    84, 86, 87, 90, 94, 95, 96, 97, 98, 99,
    39, 113, 114, 115, 116, 118, 119, 121, 122, 123, 124,
}


def shuffle_excluded_quest_ids(entrance_shuffle: bool,
                               zone_locking: bool) -> set:
    """Quest ids that must not hold progression while both shuffles are on.

    Shared with create_items so the filler budget counts the same locations
    this module marks EXCLUDED — if the two disagree, generation fails on an
    item/location mismatch instead of quietly doing the wrong thing.
    """
    if not (entrance_shuffle and zone_locking):
        return set()
    return {qid for qid, zone in QUEST_ID_TO_AREA.items()
            if zone in SHUFFLABLE_ZONES}


def _zone_to_region(zone_id: int):
    """Map a zone ID to (act, region_num), or None if always-open/unknown.
    Towns (1, 40, 75, 103, 109) resolve to their act's region 1."""
    if zone_id in ALWAYS_OPEN_ZONES:
        return None
    if zone_id in PORTAL_ENTRY_REGION:
        return PORTAL_ENTRY_REGION[zone_id]
    for act, regions in ACT_REGIONS.items():
        for region_num, zones in regions.items():
            if zone_id in zones:
                return (act, region_num)
    return None


def _make_open_rule(prev_boss, prev_diff_b, p):
    """Rule for a location with no gate keys of its own: you still have to have
    reached the previous act's boss at this difficulty, and the previous
    difficulty's Baal. Hoisted to module scope in 3.3.5 — it used to be
    re-defined inside two branches that both `continue`, so a third caller
    could not see it."""
    def rule(state):
        if prev_boss and not state.can_reach_location(prev_boss, p):
            return False
        if prev_diff_b and not state.can_reach_location(prev_diff_b, p):
            return False
        return True
    return rule


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
                # V5 - progressive: reaching region gate+2 needs (gate+1) COPIES of
                # this act's single progressive key, instead of one specific key.
                key_name = f"Progressive Act {act} Key ({diff_name})"
                src.connect(
                    dst,
                    f"A{act}D{diff} Gate {gate + 1}",
                    lambda state, k=key_name, n=gate + 1, p=player: state.has(k, p, n),
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
    Goal is now 0/1/2 (Normal/NM/Hell); act scope always = full game.

    1.9.9: Goal=4 (Custom) now uses computed scope from selected
    sub-target toggles instead of always generating the full
    3-difficulty 5-act region tree. See compute_custom_goal_scope()
    in options.py for the rule.
    """
    multiworld = world.multiworld
    player = world.player
    zone_locking = bool(world.options.zone_locking.value)
    # 3.3.5 — see SHUFFLABLE_ZONES: with both shuffles on, the region model
    # cannot know where a dungeon moved, so its checks must not hold anything
    # the player could need. create_items counts the same set.
    shuffle_excluded = shuffle_excluded_quest_ids(
        bool(world.options.entrance_shuffle.value), zone_locking)

    goal = world.options.goal.value    # 0-4: scope, gold_collection, or custom
    # 1.9.0 — Goal=3 (Gold Collection) treats as Normal-only for region/fill.
    # 1.9.9 — Goal=4 (Custom) now scope-aware (was: full pool, DLL filters).
    if goal == 3:
        max_act = 5
        num_diffs = 1
    elif goal == 4:
        # 1.9.9 — match get_active_locations() so the region tree
        # covers exactly what the player will traverse.
        from .options import compute_custom_goal_scope
        scope_act, scope_diff = compute_custom_goal_scope(world.options)
        max_act = scope_act
        num_diffs = scope_diff + 1
    else:
        max_act = 5                     # 0/1/2: always full game
        num_diffs = goal + 1            # 1, 2, or 3

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

        # 1.9.10 — set of all active location names. Used by the level-
        # milestone access-rule builder to filter prereqs that don't
        # exist in the current scope (e.g. Hell prereqs in a Normal-only
        # seed should be silently dropped, not referenced and KeyError).
        active_loc_names = {name for (_, name, _, _, _, _) in active_locations}

        # Place each non-gate location in open_region with access rule
        # derived from zone → (act, region_num)
        # 1.9.10 — added "collection" to ZL_BONUS_TYPES.
        # Background: Goal=3 (Gold Collection) emits collection-event
        # locations with negative quest_ids (loc_id offsets in the
        # COLL_LOC_BASE 50000 range). These have no zone_id mapping,
        # so the loop below would fall through to the ALWAYS_OPEN_ZONES
        # branch with no access rule, letting AP fill place progression
        # items (gate keys etc.) at collection-event locations like
        # "Collection: Zod Rune" — a hard softlock since Zod rune drops
        # at ~1/5000 per Hell L99 kill. Marking as EXCLUDED forces
        # filler-only placement, same as the bonus/extra check classes.
        from BaseClasses import LocationProgressType
        ZL_BONUS_TYPES = ("bonus_object", "bonus_gold", "bonus_setpickup",
                          "extra_cow", "extra_merc", "extra_hfrunes",
                          "extra_npc", "extra_runeword", "extra_cube",
                          "collection")
        for quest_id, loc_name, quest_type, classification, loc_id, diff in active_locations:
            if quest_type == "gate":
                continue  # already placed by _build_gate_region_tree

            # 1.9.0 — bonus check locations: filler-only via EXCLUDED so
            # AP fill never places progression items at slots that may
            # never be consumed by the escalating-chance roll. Same as
            # the non-zone-locking branch below.
            if quest_type in ZL_BONUS_TYPES:
                loc = world.create_location(loc_name, loc_id, open_region)
                loc.progress_type = LocationProgressType.EXCLUDED
                open_region.locations.append(loc)
                continue

            # 1.8.5 fix (R9) — Level milestones now have access rules so
            # AP fill cannot place early-act progression behind high-level
            # checks. Without this, a player with zone_locking on could
            # have "Act 1 Gate 2 Key (Normal)" placed at "Reach Level 30
            # (Normal)" — making Act 1 R3+ unreachable until they ground
            # to L30 in R1+R2 only. The level → act-boss-prerequisite
            # mapping below ensures level milestones always live in a
            # later sphere than the early-act gate keys they could
            # otherwise block.
            #
            # Mapping (Normal scope; NM/Hell scaled by diff offset):
            #   L 5/10/15 → no rule (achievable in R1)
            #   L 20      → require Andariel killed at this diff (Act 2+)
            #   L 30      → require Mephisto killed at this diff (Act 4+)
            #   L 35..55  → require previous-diff Baal kill (NM access)
            #   L 60..75  → require NM Baal kill (Hell access)
            if quest_type == "level":
                loc = world.create_location(loc_name, loc_id, open_region)
                open_region.locations.append(loc)
                level_val = QUEST_ID_TO_LEVEL.get(quest_id, 0)
                prereq_locs = _level_milestone_prereq(level_val, diff)
                # 1.9.10 — _level_milestone_prereq now returns a list (or None).
                # All listed prereqs must be reachable. Filter against active
                # location set so we don't reference locations that don't
                # exist in the current scope (e.g. Hell milestones in a
                # Normal-only scope shouldn't even be generated, but
                # belt-and-suspenders).
                if prereq_locs:
                    valid = [pl for pl in prereq_locs if pl in active_loc_names]
                    if valid:
                        def _make_level_rule(prls, p=player):
                            return lambda state: all(
                                state.can_reach_location(pl, p) for pl in prls
                            )
                        loc.access_rule = _make_level_rule(tuple(valid))
                continue

            # For quest locations: determine physical zone + set access rule
            zone_id = QUEST_ID_TO_AREA.get(quest_id)
            loc = world.create_location(loc_name, loc_id, open_region)
            open_region.locations.append(loc)
            if quest_id in shuffle_excluded:
                loc.progress_type = LocationProgressType.EXCLUDED

            # 1.9.0 fix — even for ALWAYS_OPEN_ZONES and unknown zones, we
            # still need to gate by previous-difficulty Baal at higher
            # difficulties. Without this, AP fill could place a Normal
            # gate-key at a Hell-difficulty "always open" location like
            # "Clear Halls of Vaught (Hell)" — making the Normal gate
            # unreachable until the player has already beaten NM Baal.
            # Build the diff-only rule first; zone-key + prev-act rules
            # stack on top below for non-always-open zones.
            #
            # Quests in act > 1 also need the previous act's boss kill at
            # the same difficulty, regardless of whether the zone itself
            # is "always open" (you can't teleport to Halls of Vaught
            # without progressing through Acts 1-4 of the same diff).
            quest_act = _quest_id_to_act(quest_id)
            prev_boss_loc = None
            if quest_act > 1:
                prev_boss_loc_base = {
                    1: "Sisters to the Slaughter",
                    2: "Seven Tombs",
                    3: "The Guardian",
                    4: "Terror's End",
                }.get(quest_act - 1)
                if prev_boss_loc_base:
                    prev_boss_loc = prev_boss_loc_base if diff == 0 else f"{prev_boss_loc_base} ({diff_name_fromdiff(diff)})"
            prev_diff_boss = None
            if diff > 0:
                prev_diff_boss = "Eve of Destruction" if diff - 1 == 0 else f"Eve of Destruction ({diff_name_fromdiff(diff-1)})"

            if zone_id is None or zone_id in ALWAYS_OPEN_ZONES:
                # Diff-only / prev-act-only rule (no gate keys).
                if prev_boss_loc or prev_diff_boss:
                    loc.access_rule = _make_open_rule(prev_boss_loc,
                                                      prev_diff_boss, player)
                continue

            physical = _zone_to_region(zone_id)
            if physical is None:
                # Same diff/prev-act gating as ALWAYS_OPEN above.
                if prev_boss_loc or prev_diff_boss:
                    loc.access_rule = _make_open_rule(prev_boss_loc,
                                                      prev_diff_boss, player)
                continue

            act, region_num = physical
            if act > max_act:
                # Out of the goal's scope. The location object already exists,
                # so falling through with no rule would mark it sphere-0 and
                # invite a key placement — give it the same diff/prev-act rule
                # the always-open branch uses instead of nothing.
                if prev_boss_loc or prev_diff_boss:
                    loc.access_rule = _make_open_rule(prev_boss_loc,
                                                      prev_diff_boss, player)
                continue

            # Build access rule: requires all gate keys 1..region_num-1 + prev act boss
            # V5 - progressive: need (region_num - 1) copies of this act's key.
            required_keys = []
            required_key_count = max(0, region_num - 1)
            required_key_name = f"Progressive Act {act} Key ({diff_name_fromdiff(diff)})"
            if required_key_count:
                required_keys = [(required_key_name, required_key_count)]
            # Re-derive prev_boss using the physical act (which may differ
            # from quest_act for hunts that roam) — keep the stricter rule.
            if act > 1:
                prev_boss_loc_base = {
                    1: "Sisters to the Slaughter",
                    2: "Seven Tombs",
                    3: "The Guardian",
                    4: "Terror's End",
                }.get(act - 1)
                if prev_boss_loc_base:
                    prev_boss_loc = prev_boss_loc_base if diff == 0 else f"{prev_boss_loc_base} ({diff_name_fromdiff(diff)})"

            def _make_rule(keys, prev_boss, prev_diff_b, p=player):
                def rule(state):
                    if keys and not all(state.has(k, p, n) for k, n in keys):
                        return False
                    if prev_boss and not state.can_reach_location(prev_boss, p):
                        return False
                    if prev_diff_b and not state.can_reach_location(prev_diff_b, p):
                        return False
                    return True
                return rule

            loc.access_rule = _make_rule(required_keys, prev_boss_loc, prev_diff_boss)

        return

    # === SKILL HUNT (zone_locking=False) ===
    #
    # 1.9.11 — per-difficulty act regions.
    #
    # Pre-1.9.11 the skill_hunt branch created one region per act with all
    # difficulties collapsed into it. Menu → Act 1 → Act 2 → ... was boss-gated
    # but every difficulty's locations lived in the same region, so an NM Den
    # of Evil check was reachable directly from Menu with no requirement to
    # have beaten Normal Baal first. AP fill would happily place progression
    # items at NM/Hell locations in sphere 1, defeating multiworld pacing and
    # potentially creating "you need to be in Hell to find the key that
    # unlocks Hell" loops if multiple slots cross-reference.
    #
    # 1.9.11 fix: act_regions is now keyed by (act_num, diff). Difficulty
    # transitions (Normal → NM → Hell) are gated on the act-5-end boss kill
    # ("Eve of Destruction") at the previous difficulty. For max_act < 5 the
    # gate uses the last present act's boss kill instead. Locations are
    # placed in the (act, diff) region matching their tuple's diff field, so
    # NM Den of Evil ends up in Act 1 Nightmare which is only reachable
    # after Normal Baal (or last-act boss in narrow scopes).
    DIFF_SUFFIX  = ["", " (Nightmare)", " (Hell)"]
    DIFF_NAME    = ["Normal", "Nightmare", "Hell"]
    act_regions: dict[tuple[int, int], Region] = {}
    for diff in range(num_diffs):
        for act_num in range(1, max_act + 1):
            region = Region(f"Act {act_num} {DIFF_NAME[diff]}", player, multiworld)
            multiworld.regions.append(region)
            act_regions[(act_num, diff)] = region

    if (1, 0) in act_regions:
        menu_region.connect(act_regions[(1, 0)])

    boss_connections = [
        (1, 2, "Sisters to the Slaughter"),
        (2, 3, "Seven Tombs"),
        (3, 4, "The Guardian"),
        (4, 5, "Terror's End"),
    ]
    # Last-act boss (used to gate difficulty transitions). max_act can be
    # 1..5 for narrow Custom Goal scopes.
    LAST_ACT_BOSS = {
        1: "Den of Evil",
        2: "Sisters to the Slaughter",
        3: "Seven Tombs",
        4: "The Guardian",
        5: "Eve of Destruction",
    }
    active_loc_names = {name for (_, name, _, _, _, _) in active_locations}

    def _loc_at(base: str, diff: int) -> str:
        return base + DIFF_SUFFIX[diff]

    # SKILL HUNT / BASE GAME (zone_locking=False): tier sub-regions + boss tokens.
    #
    # Each (act_num, diff) pair is split into depth tiers matching ACT_REGIONS so
    # locations land in the correct physical zone for spoiler purposes. Tier
    # connections are UNCONDITIONAL — tiers within an act are all accessible once
    # the act is open, and the DLL enforces the physical ordering in-game.
    #
    # Act-to-act and difficulty transitions are gated by _BossToken progression
    # items pre-placed at the act boss kill locations. Boss tokens are swept by
    # AP's fill algorithm when the boss location becomes reachable, expanding the
    # state and opening the next act / difficulty. This creates meaningful sphere
    # depth: one sphere per act per difficulty (~15 spheres for full hell).
    #
    # Why not can_reach_location? Those gates evaluated to True immediately
    # because the referenced story-quest locations had no item prerequisites —
    # their regions were reachable from sphere 1 through the unconditional tier
    # chain, so every gate was transparent and everything collapsed to 1 sphere.
    #
    # Bonus/extra check locations are EXCLUDED so AP fill never places
    # progression items at escalating-chance slots that may not be consumed.
    from BaseClasses import LocationProgressType
    BONUS_TYPES = ("bonus_object", "bonus_gold", "bonus_setpickup",
                   "extra_cow", "extra_merc", "extra_hfrunes",
                   "extra_npc", "extra_runeword", "extra_cube",
                   "collection")

    act_sub_regions: dict[tuple[int, int, int], Region] = {}
    for diff in range(num_diffs):
        for act_num in range(1, max_act + 1):
            act_sub_regions[(act_num, diff, 1)] = act_regions[(act_num, diff)]
            num_tiers = len(ACT_REGIONS.get(act_num, {}))
            for tier in range(2, num_tiers + 1):
                r = Region(
                    f"Act {act_num} {DIFF_NAME[diff]} Tier {tier}",
                    player, multiworld,
                )
                multiworld.regions.append(r)
                act_sub_regions[(act_num, diff, tier)] = r

    # Tier connections: unconditional. Boss tokens at act boundaries provide
    # sphere depth — no item gate is needed within an act's own tiers.
    for diff in range(num_diffs):
        for act_num in range(1, max_act + 1):
            num_tiers = len(ACT_REGIONS.get(act_num, {}))
            for tier in range(1, num_tiers):
                src = act_sub_regions[(act_num, diff, tier)]
                dst = act_sub_regions[(act_num, diff, tier + 1)]
                src.connect(dst, f"Act {act_num} T{tier}->T{tier+1} ({DIFF_NAME[diff]})")

    # Boss connections: act N last-tier → act N+1 T1, gated by boss token.
    last_boss_base = LAST_ACT_BOSS.get(max_act, "Eve of Destruction")
    for diff in range(num_diffs):
        for from_act, to_act, boss_loc_base in boss_connections:
            if (from_act, diff) not in act_regions or (to_act, diff) not in act_regions:
                continue
            boss_loc_diff = _loc_at(boss_loc_base, diff)
            from_last_tier = len(ACT_REGIONS.get(from_act, {}))
            src = act_sub_regions.get(
                (from_act, diff, from_last_tier),
                act_regions[(from_act, diff)],
            )
            dst = act_regions[(to_act, diff)]
            if boss_loc_diff in active_loc_names:
                token_name = f"{boss_loc_diff} [Token]"
                src.connect(
                    dst,
                    f"Act {from_act} -> Act {to_act} ({DIFF_NAME[diff]})",
                    lambda state, p=player, t=token_name: state.has(t, p),
                )
            else:
                src.connect(dst, f"Act {from_act} -> Act {to_act} ({DIFF_NAME[diff]})")

    # Difficulty transitions: last-act last-tier → next-diff T1, gated by boss token.
    for diff in range(num_diffs - 1):
        if (max_act, diff) not in act_regions or (1, diff + 1) not in act_regions:
            continue
        gate_loc = _loc_at(last_boss_base, diff)
        last_tier = len(ACT_REGIONS.get(max_act, {}))
        src = act_sub_regions.get(
            (max_act, diff, last_tier),
            act_regions[(max_act, diff)],
        )
        dst = act_regions[(1, diff + 1)]
        if gate_loc in active_loc_names:
            token_name = f"{gate_loc} [Token]"
            src.connect(
                dst,
                f"{DIFF_NAME[diff]} complete -> {DIFF_NAME[diff + 1]}",
                lambda state, p=player, t=token_name: state.has(t, p),
            )
        else:
            src.connect(dst, f"{DIFF_NAME[diff]} complete -> {DIFF_NAME[diff + 1]}")

    # Place locations in tier sub-regions based on physical zone.
    for quest_id, loc_name, quest_type, classification, loc_id, diff in active_locations:
        if quest_type == "level":
            target_act  = QUEST_ID_TO_MAX_ACTS.get(quest_id, 1)
            target_tier = 1
        else:
            target_act = _quest_id_to_act(quest_id)
            zone_id = QUEST_ID_TO_AREA.get(quest_id)
            if zone_id is not None and zone_id not in ALWAYS_OPEN_ZONES:
                physical = _zone_to_region(zone_id)
                target_tier = physical[1] if physical is not None else 1
            else:
                target_tier = 1

        target_region = act_sub_regions.get(
            (target_act, diff, target_tier),
            act_regions.get((target_act, diff)),
        )
        if target_region is None:
            continue
        loc = world.create_location(loc_name, loc_id, target_region)
        if quest_type in BONUS_TYPES:
            loc.progress_type = LocationProgressType.EXCLUDED
        target_region.locations.append(loc)

    # Pre-place boss tokens at act boss + diff-transition locations.
    # These locked _BossToken items are the progression items that gate the
    # act-to-act and difficulty-transition connections above. They are swept by
    # AP's fill algorithm when their location becomes reachable, expanding state.
    # create_items must subtract this count from its filler budget so the total
    # item pool + locked tokens = total locations.
    locked_tokens = 0
    loc_by_name = {l.name: l for r in multiworld.regions for l in r.locations}

    for diff in range(num_diffs):
        # Act-to-act boss tokens (Andariel, Duriel, Mephisto, Diablo per diff)
        for from_act, to_act, boss_loc_base in boss_connections:
            if (from_act, diff) not in act_regions:
                continue
            boss_loc_diff = _loc_at(boss_loc_base, diff)
            if boss_loc_diff not in active_loc_names:
                continue
            loc = loc_by_name.get(boss_loc_diff)
            if loc and loc.item is None:
                loc.place_locked_item(
                    _BossToken(f"{boss_loc_diff} [Token]", _IC.progression, BOSS_TOKEN_ITEM_ID, player)
                )
                locked_tokens += 1

        # Difficulty transition token (Baal at end of each non-final difficulty)
        if diff < num_diffs - 1:
            gate_loc_name = _loc_at(last_boss_base, diff)
            if gate_loc_name not in active_loc_names:
                continue
            loc = loc_by_name.get(gate_loc_name)
            if loc and loc.item is None:
                loc.place_locked_item(
                    _BossToken(f"{gate_loc_name} [Token]", _IC.progression, BOSS_TOKEN_ITEM_ID, player)
                )
                locked_tokens += 1

    world._locked_token_count = locked_tokens


def diff_name_fromdiff(d):
    return ["Normal", "Nightmare", "Hell"][d]


def _level_milestone_prereq(level: int, diff: int):
    """1.8.5 fix (R9) — Map a level-milestone (level, difficulty) to a list of
    locations the player must be able to reach before the milestone is
    considered satisfiable.

    Returns a list of location names (str) or None when no prerequisites are
    needed (low-level milestones in the starting region).

    The intent is purely to push level milestones into spheres LATER
    than the act gate-keys that could otherwise be blocked by them.
    Without this, AP fill places progression items at level-milestone
    locations because they have no access rules — leaving the player
    to grind to that level inside the starting region with no other
    sources of items, which is exactly the soft-lock that R9 reported.

    1.9.10 fix (#3b) — Hell-tier level milestones (L60-75) used to gate
    only on NM Baal kill, which meant AP fill could legitimately place
    Hell-difficulty Act 1 gate keys at "Reach Level 60 (Hell)" — the
    player would have NM access but no Hell Act 1 R3+ access, forcing
    them to grind to L60 in Hell Act 1 R1+R2 only. Same pattern for
    NM-tier (L35-55). Now both tiers also require the same-diff Act 1
    boss kill so the grinding location actually has reachable monsters.

    Diff parameter follows the apworld convention (0=Normal, 1=NM,
    2=Hell). Boss location names get the difficulty suffix appended.
    """
    if level <= 15:
        return None  # Achievable in Act 1 starting region; no rule.

    def at_diff(name: str, d: int) -> str:
        return name if d == 0 else f"{name} ({diff_name_fromdiff(d)})"

    # Within current difficulty: gate level milestones behind act bosses.
    if 16 <= level <= 25:
        # Level 20: must be in Act 2 → Andariel kill at this diff.
        return [at_diff("Sisters to the Slaughter", diff)]
    if 26 <= level <= 34:
        # Level 30: must be deep into the run → Mephisto at this diff.
        return [at_diff("The Guardian", diff)]

    # Level 35+ implies Nightmare or Hell access. Require previous-diff
    # Baal kill AND same-diff Act 1 boss kill so the player has access
    # to the difficulty's monsters AND can grind there efficiently.
    if 35 <= level <= 55:
        # NM tier — require Normal Baal (diff transition) AND NM Andariel
        # (so they're inside NM with real monster pool, not stuck in Normal).
        return [
            "Eve of Destruction",
            at_diff("Sisters to the Slaughter", 1),  # NM Andariel
        ]
    if level >= 60:
        # Hell tier — require NM Baal (diff transition) AND Hell Andariel
        # (so they're inside Hell with real monster pool).
        return [
            "Eve of Destruction (Nightmare)",
            at_diff("Sisters to the Slaughter", 2),  # Hell Andariel
        ]

    return None
