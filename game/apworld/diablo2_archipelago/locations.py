"""
Diablo II Archipelago - Location Definitions

All quest locations from d2arch.c organized by act.
Location AP ID = LOCATION_BASE + quest_id.
"""
from BaseClasses import ItemClassification

LOCATION_BASE = 42000

# Location data: (quest_id, name, quest_type, classification)
# quest_type is for reference/filtering; classification determines what can go there
# progression = story/hunt/level quests, filler = kill/area/waypoint quests

# ============================================================
# Act 1 - 66 quests (includes The Smith which spawns in Barracks)
# ============================================================
ACT1_LOCATIONS = [
    # Story quests (QTYPE_QUESTFLAG) - progression
    (1,   "Den of Evil",                  "story",    ItemClassification.progression),
    (2,   "Sisters' Burial Grounds",      "story",    ItemClassification.progression),
    (3,   "Tools of the Trade",           "story",    ItemClassification.progression),
    (4,   "The Search for Cain",          "story",    ItemClassification.progression),
    (5,   "The Forgotten Tower",          "story",    ItemClassification.progression),
    (6,   "Sisters to the Slaughter",     "story",    ItemClassification.progression),
    # SuperUnique hunts - progression
    (7,   "Hunt: Corpsefire",             "hunt",     ItemClassification.progression),
    (8,   "Hunt: Bishibosh",              "hunt",     ItemClassification.progression),
    (9,   "Hunt: Bonebreaker",            "hunt",     ItemClassification.progression),
    (70,  "Hunt: Coldcrow",               "hunt",     ItemClassification.progression),
    (71,  "Hunt: Rakanishu",              "hunt",     ItemClassification.progression),
    (72,  "Hunt: Treehead WoodFist",      "hunt",     ItemClassification.progression),
    (73,  "Hunt: Griswold",               "hunt",     ItemClassification.progression),
    (74,  "Hunt: The Countess",           "hunt",     ItemClassification.progression),
    (75,  "Hunt: Pitspawn Fouldog",       "hunt",     ItemClassification.progression),
    (77,  "Hunt: Boneash",                "hunt",     ItemClassification.progression),
    (80,  "Hunt: The Smith",              "hunt",     ItemClassification.progression),
    # Level milestones moved to GLOBAL_LEVEL_MILESTONES (not per-act)
    # Kill quests (28 zones) - filler
    (10,  "Clear Blood Moor",             "kill",     ItemClassification.filler),
    (11,  "Clear Cold Plains",            "kill",     ItemClassification.filler),
    (12,  "Clear Stony Field",            "kill",     ItemClassification.filler),
    (13,  "Clear Dark Wood",              "kill",     ItemClassification.filler),
    (14,  "Clear Black Marsh",            "kill",     ItemClassification.filler),
    (15,  "Clear Tamoe Highland",         "kill",     ItemClassification.filler),
    (16,  "Clear Den of Evil",            "kill",     ItemClassification.filler),
    (17,  "Clear Cave Level 1",           "kill",     ItemClassification.filler),
    (18,  "Clear Underground Passage",    "kill",     ItemClassification.filler),
    (19,  "Clear Burial Grounds",         "kill",     ItemClassification.filler),
    (20,  "Clear The Crypt",              "kill",     ItemClassification.filler),
    (21,  "Clear Mausoleum",              "kill",     ItemClassification.filler),
    (22,  "Clear Tower Cellar L1",        "kill",     ItemClassification.filler),
    (23,  "Clear Tower Cellar L2",        "kill",     ItemClassification.filler),
    (24,  "Clear Tower Cellar L3",        "kill",     ItemClassification.filler),
    (25,  "Clear Tower Cellar L4",        "kill",     ItemClassification.filler),
    (26,  "Clear Tower Cellar L5",        "kill",     ItemClassification.filler),
    (27,  "Clear Monastery Gate",         "kill",     ItemClassification.filler),
    (28,  "Clear Outer Cloister",         "kill",     ItemClassification.filler),
    (29,  "Clear Barracks",               "kill",     ItemClassification.filler),
    (30,  "Clear Jail Level 1",           "kill",     ItemClassification.filler),
    (31,  "Clear Jail Level 2",           "kill",     ItemClassification.filler),
    (32,  "Clear Jail Level 3",           "kill",     ItemClassification.filler),
    (33,  "Clear Cathedral",              "kill",     ItemClassification.filler),
    (34,  "Clear Catacombs L1",           "kill",     ItemClassification.filler),
    (35,  "Clear Catacombs L2",           "kill",     ItemClassification.filler),
    (36,  "Clear Catacombs L3",           "kill",     ItemClassification.filler),
    (37,  "Clear Tristram",               "kill",     ItemClassification.filler),
    (38,  "Clear Cave Level 2",           "kill",     ItemClassification.filler),
    (39,  "Clear Underground Passage 2",  "kill",     ItemClassification.filler),
    (59,  "Clear Hole Level 1",           "kill",     ItemClassification.filler),
    (60,  "Clear Hole Level 2",           "kill",     ItemClassification.filler),
    # Area entry (10 entries) - filler
    (40,  "Enter Blood Moor",             "area",     ItemClassification.filler),
    (41,  "Enter Cold Plains",            "area",     ItemClassification.filler),
    (42,  "Enter Stony Field",            "area",     ItemClassification.filler),
    (43,  "Enter Dark Wood",              "area",     ItemClassification.filler),
    (44,  "Enter Black Marsh",            "area",     ItemClassification.filler),
    (45,  "Enter Tamoe Highland",         "area",     ItemClassification.filler),
    (46,  "Enter Den of Evil",            "area",     ItemClassification.filler),
    (47,  "Enter Tristram",               "area",     ItemClassification.filler),
    (48,  "Enter Catacombs",              "area",     ItemClassification.filler),
    (49,  "Enter Tower Cellar",           "area",     ItemClassification.filler),
    # Waypoints (8) - filler
    (50,  "Cold Plains Waypoint",         "waypoint", ItemClassification.filler),
    (51,  "Stony Field Waypoint",         "waypoint", ItemClassification.filler),
    (52,  "Dark Wood Waypoint",           "waypoint", ItemClassification.filler),
    (53,  "Black Marsh Waypoint",         "waypoint", ItemClassification.filler),
    (54,  "Outer Cloister Waypoint",      "waypoint", ItemClassification.filler),
    (55,  "Jail Level 1 Waypoint",        "waypoint", ItemClassification.filler),
    (56,  "Inner Cloister Waypoint",      "waypoint", ItemClassification.filler),
    (57,  "Catacombs Level 2 Waypoint",   "waypoint", ItemClassification.filler),
]

# ============================================================
# Act 2 - quests (note: actual IDs from d2arch.c)
# ============================================================
ACT2_LOCATIONS = [
    # Story quests - progression
    (101, "Radament's Lair",              "story",    ItemClassification.progression),
    (102, "The Horadric Staff",           "story",    ItemClassification.progression),
    (103, "Tainted Sun",                  "story",    ItemClassification.progression),
    (104, "Arcane Sanctuary",             "story",    ItemClassification.progression),
    (105, "The Summoner",                 "story",    ItemClassification.progression),
    (106, "Seven Tombs",                  "story",    ItemClassification.progression),
    # SuperUnique hunts - progression
    (170, "Hunt: Radament",               "hunt",     ItemClassification.progression),
    (171, "Hunt: Bloodwitch",             "hunt",     ItemClassification.progression),
    (172, "Hunt: Fangskin",               "hunt",     ItemClassification.progression),
    (173, "Hunt: Beetleburst",            "hunt",     ItemClassification.progression),
    (174, "Hunt: Leatherarm",             "hunt",     ItemClassification.progression),
    (175, "Hunt: Coldworm",               "hunt",     ItemClassification.progression),
    (176, "Hunt: Fire Eye",               "hunt",     ItemClassification.progression),
    (177, "Hunt: Dark Elder",             "hunt",     ItemClassification.progression),
    (178, "Hunt: The Summoner",           "hunt",     ItemClassification.progression),
    (179, "Hunt: Ancient Kaa",            "hunt",     ItemClassification.progression),
    # Level milestones - progression
    # Level milestones moved to GLOBAL_LEVEL_MILESTONES
    # Kill quests (20 zones) - filler
    (110, "Clear Rocky Waste",            "kill",     ItemClassification.filler),
    (111, "Clear Dry Hills",              "kill",     ItemClassification.filler),
    (112, "Clear Far Oasis",              "kill",     ItemClassification.filler),
    (113, "Clear Lost City",              "kill",     ItemClassification.filler),
    (114, "Clear Valley of Snakes",       "kill",     ItemClassification.filler),
    # Sewers removed — D2MOO pathfinding broken in narrow corridors
    # (115, "Clear Sewers L1",              "kill",     ItemClassification.filler),
    # (116, "Clear Sewers L2",              "kill",     ItemClassification.filler),
    (117, "Clear Halls of Dead L1",       "kill",     ItemClassification.filler),
    (118, "Clear Halls of Dead L2",       "kill",     ItemClassification.filler),
    (119, "Clear Halls of Dead L3",       "kill",     ItemClassification.filler),
    (120, "Clear Maggot Lair L1",         "kill",     ItemClassification.filler),
    (121, "Clear Maggot Lair L2",         "kill",     ItemClassification.filler),
    (122, "Clear Maggot Lair L3",         "kill",     ItemClassification.filler),
    (123, "Clear Ancient Tunnels",        "kill",     ItemClassification.filler),
    (124, "Clear Arcane Sanctuary",       "kill",     ItemClassification.filler),
    (125, "Clear Palace Cellar L1",       "kill",     ItemClassification.filler),
    (126, "Clear Palace Cellar L2",       "kill",     ItemClassification.filler),
    (127, "Clear Palace Cellar L3",       "kill",     ItemClassification.filler),
    (128, "Clear Canyon of Magi",         "kill",     ItemClassification.filler),
    (129, "Clear Stony Tomb",             "kill",     ItemClassification.filler),
    # Area entry (5) - filler
    (140, "Enter Rocky Waste",            "area",     ItemClassification.filler),
    (141, "Enter Dry Hills",              "area",     ItemClassification.filler),
    (142, "Enter Far Oasis",              "area",     ItemClassification.filler),
    (143, "Enter Lost City",              "area",     ItemClassification.filler),
    (144, "Enter Arcane Sanctuary",       "area",     ItemClassification.filler),
    # Waypoints (8) - filler
    # (150, "Sewers Waypoint",              "waypoint", ItemClassification.filler),  # Removed: D2MOO pathfinding broken in Sewers
    (151, "Dry Hills Waypoint",            "waypoint", ItemClassification.filler),
    (152, "Halls of the Dead Waypoint",   "waypoint", ItemClassification.filler),
    (153, "Far Oasis Waypoint",            "waypoint", ItemClassification.filler),
    (154, "Lost City Waypoint",           "waypoint", ItemClassification.filler),
    (155, "Palace Cellar Waypoint",       "waypoint", ItemClassification.filler),
    (156, "Arcane Sanctuary Waypoint",    "waypoint", ItemClassification.filler),
    (157, "Canyon of the Magi Waypoint",  "waypoint", ItemClassification.filler),
]

# ============================================================
# Act 3
# ============================================================
ACT3_LOCATIONS = [
    # Story quests - progression
    (201, "Lam Esen's Tome",              "story",    ItemClassification.progression),
    (202, "Khalim's Will",                "story",    ItemClassification.progression),
    (203, "Blade of Old Religion",        "story",    ItemClassification.progression),
    (204, "The Golden Bird",              "story",    ItemClassification.progression),
    (205, "The Blackened Temple",         "story",    ItemClassification.progression),
    (206, "The Guardian",                 "story",    ItemClassification.progression),
    # SuperUnique hunts - progression
    # NOTE: The Smith (270) moved to Act 1 where it spawns (Barracks)
    (271, "Hunt: Web Mage",              "hunt",     ItemClassification.progression),
    (272, "Hunt: Witch Doctor Endugu",    "hunt",     ItemClassification.progression),
    (273, "Hunt: Stormtree",              "hunt",     ItemClassification.progression),
    (274, "Hunt: Sarina",                 "hunt",     ItemClassification.progression),
    (275, "Hunt: Icehawk Riftwing",       "hunt",     ItemClassification.progression),
    (276, "Hunt: Ismail Vilehand",        "hunt",     ItemClassification.progression),
    (277, "Hunt: Geleb Flamefinger",      "hunt",     ItemClassification.progression),
    (278, "Hunt: Bremm Sparkfist",        "hunt",     ItemClassification.progression),
    (279, "Hunt: Toorc Icefist",          "hunt",     ItemClassification.progression),
    (280, "Hunt: Wyand Voidbringer",      "hunt",     ItemClassification.progression),
    (281, "Hunt: Maffer Dragonhand",      "hunt",     ItemClassification.progression),
    # Level milestones - progression
    # Level milestones moved to GLOBAL_LEVEL_MILESTONES
    # Kill quests (15 zones) - filler
    (210, "Clear Spider Forest",          "kill",     ItemClassification.filler),
    (211, "Clear Great Marsh",            "kill",     ItemClassification.filler),
    (212, "Clear Flayer Jungle",          "kill",     ItemClassification.filler),
    (213, "Clear Lower Kurast",           "kill",     ItemClassification.filler),
    (214, "Clear Kurast Bazaar",          "kill",     ItemClassification.filler),
    (215, "Clear Upper Kurast",           "kill",     ItemClassification.filler),
    (216, "Clear Travincal",              "kill",     ItemClassification.filler),
    (217, "Clear Spider Cave",            "kill",     ItemClassification.filler),
    (218, "Clear Flayer Dungeon L1",      "kill",     ItemClassification.filler),
    (219, "Clear Flayer Dungeon L2",      "kill",     ItemClassification.filler),
    # Kurast Sewers removed — D2MOO pathfinding broken in narrow corridors
    # (220, "Clear Kurast Sewers L1",        "kill",     ItemClassification.filler),
    # (221, "Clear Kurast Sewers L2",        "kill",     ItemClassification.filler),
    (222, "Clear Durance L1",             "kill",     ItemClassification.filler),
    (223, "Clear Durance L2",             "kill",     ItemClassification.filler),
    (224, "Clear Kurast Causeway",        "kill",     ItemClassification.filler),
    # Area entry (5) - filler
    (240, "Enter Spider Forest",          "area",     ItemClassification.filler),
    (241, "Enter Flayer Jungle",          "area",     ItemClassification.filler),
    (242, "Enter Kurast Bazaar",          "area",     ItemClassification.filler),
    (243, "Enter Travincal",              "area",     ItemClassification.filler),
    (244, "Enter Durance of Hate",        "area",     ItemClassification.filler),
    # Waypoints (8) - filler
    (250, "Spider Forest Waypoint",       "waypoint", ItemClassification.filler),
    (251, "Great Marsh Waypoint",         "waypoint", ItemClassification.filler),
    (252, "Flayer Jungle Waypoint",       "waypoint", ItemClassification.filler),
    (253, "Lower Kurast Waypoint",        "waypoint", ItemClassification.filler),
    (254, "Kurast Bazaar Waypoint",       "waypoint", ItemClassification.filler),
    (255, "Upper Kurast Waypoint",        "waypoint", ItemClassification.filler),
    (256, "Travincal Waypoint",           "waypoint", ItemClassification.filler),
    (257, "Durance of Hate Waypoint",     "waypoint", ItemClassification.filler),
]

# ============================================================
# Act 4
# ============================================================
ACT4_LOCATIONS = [
    # Story quests - progression
    (301, "The Fallen Angel",             "story",    ItemClassification.progression),
    (302, "Hell's Forge",                 "story",    ItemClassification.progression),
    (303, "Terror's End",                 "story",    ItemClassification.progression),
    # SuperUnique hunts - progression
    (370, "Hunt: Winged Death",           "hunt",     ItemClassification.progression),
    (371, "Hunt: The Tormentor",          "hunt",     ItemClassification.progression),
    (372, "Hunt: Taintbreeder",           "hunt",     ItemClassification.progression),
    (373, "Hunt: Riftwraith",             "hunt",     ItemClassification.progression),
    (374, "Hunt: Infector",               "hunt",     ItemClassification.progression),
    (375, "Hunt: Lord De Seis",           "hunt",     ItemClassification.progression),
    (376, "Hunt: Grand Vizier",           "hunt",     ItemClassification.progression),
    # Level milestone - progression
    # Level milestones moved to Act 1-3
    # Kill quests (5 zones) - filler
    (310, "Clear Outer Steppes",          "kill",     ItemClassification.filler),
    (311, "Clear Plains of Despair",      "kill",     ItemClassification.filler),
    (312, "Clear City of the Damned",     "kill",     ItemClassification.filler),
    (313, "Clear River of Flame",         "kill",     ItemClassification.filler),
    (314, "Clear Chaos Sanctuary",        "kill",     ItemClassification.filler),
    # Area entry (5) - filler
    (340, "Enter Outer Steppes",          "area",     ItemClassification.filler),
    (341, "Enter Plains of Despair",      "area",     ItemClassification.filler),
    (342, "Enter City of the Damned",     "area",     ItemClassification.filler),
    (343, "Enter River of Flame",         "area",     ItemClassification.filler),
    (344, "Enter Chaos Sanctuary",        "area",     ItemClassification.filler),
    # Waypoints (2) - filler
    (350, "Plains of Despair Waypoint",   "waypoint", ItemClassification.filler),
    (351, "River of Flame Waypoint",      "waypoint", ItemClassification.filler),
]

# ============================================================
# Act 5
# ============================================================
ACT5_LOCATIONS = [
    # Story quests - progression
    (401, "Siege on Harrogath",           "story",    ItemClassification.progression),
    (402, "Rescue on Mt. Arreat",         "story",    ItemClassification.progression),
    (403, "Prison of Ice",                "story",    ItemClassification.progression),
    (404, "Betrayal of Harrogath",        "story",    ItemClassification.progression),
    (405, "Rite of Passage",              "story",    ItemClassification.progression),
    (406, "Eve of Destruction",           "story",    ItemClassification.progression),
    # SuperUnique hunts - progression
    (470, "Hunt: Siege Boss",             "hunt",     ItemClassification.progression),
    (471, "Hunt: Dac Farren",             "hunt",     ItemClassification.progression),
    (472, "Hunt: Bonesaw Breaker",        "hunt",     ItemClassification.progression),
    (473, "Hunt: Eyeback Unleashed",      "hunt",     ItemClassification.progression),
    (474, "Hunt: Threash Socket",         "hunt",     ItemClassification.progression),
    (475, "Hunt: Pindleskin",             "hunt",     ItemClassification.progression),
    (476, "Hunt: Snapchip Shatter",       "hunt",     ItemClassification.progression),
    (477, "Hunt: Frozenstein",            "hunt",     ItemClassification.progression),
    # Level milestones - progression
    # Level milestones moved to Act 1-3
    # Kill quests (14 zones) - filler
    (410, "Clear Bloody Foothills",       "kill",     ItemClassification.filler),
    (411, "Clear Frigid Highlands",       "kill",     ItemClassification.filler),
    (412, "Clear Arreat Plateau",         "kill",     ItemClassification.filler),
    (413, "Clear Crystalline Passage",    "kill",     ItemClassification.filler),
    (414, "Clear Glacial Caves L1",       "kill",     ItemClassification.filler),
    (415, "Clear Glacial Caves L2",       "kill",     ItemClassification.filler),
    (416, "Clear Tundra Wastelands",      "kill",     ItemClassification.filler),
    (417, "Clear Halls of Anguish",       "kill",     ItemClassification.filler),
    (418, "Clear Halls of Death",         "kill",     ItemClassification.filler),
    (419, "Clear Halls of Vaught",        "kill",     ItemClassification.filler),
    (420, "Clear Worldstone L1",          "kill",     ItemClassification.filler),
    (421, "Clear Worldstone L2",          "kill",     ItemClassification.filler),
    (422, "Clear Worldstone L3",          "kill",     ItemClassification.filler),
    (423, "Clear Throne of Destruction",  "kill",     ItemClassification.filler),
    # Area entry (5) - filler
    (440, "Enter Bloody Foothills",       "area",     ItemClassification.filler),
    (441, "Enter Frigid Highlands",       "area",     ItemClassification.filler),
    (442, "Enter Arreat Plateau",         "area",     ItemClassification.filler),
    (443, "Enter Crystalline Passage",    "area",     ItemClassification.filler),
    (444, "Enter Worldstone Keep",        "area",     ItemClassification.filler),
    # Waypoints (8) - filler
    (450, "Frigid Highlands Waypoint",    "waypoint", ItemClassification.filler),
    (451, "Arreat Plateau Waypoint",      "waypoint", ItemClassification.filler),
    (452, "Crystalline Passage Waypoint", "waypoint", ItemClassification.filler),
    # (453, "Frozen River Waypoint",        "waypoint", ItemClassification.filler),  # Removed: Frozen River has no waypoint
    (454, "Halls of Pain Waypoint",       "waypoint", ItemClassification.filler),
    (455, "Glacial Trail Waypoint",       "waypoint", ItemClassification.filler),
    (456, "Frozen Tundra Waypoint",       "waypoint", ItemClassification.filler),
    (457, "Worldstone Keep 2 Waypoint",   "waypoint", ItemClassification.filler),
]

# All locations by act (for region building)
# ============================================================
# GLOBAL Level Milestones — per DIFFICULTY, not per act
# Max level depends on how many acts are selected:
#   Act 1 only: up to 15    Acts 1-2: up to 20
#   Acts 1-3: up to 25      Acts 1-4: up to 30     Acts 1-5: up to 30
# Nightmare adds: 35, 40, 45, 50, 55
# Hell adds: 60, 65, 70, 75
# ============================================================
LEVEL_MILESTONES_NORMAL = [
    # (quest_id, name, max_acts_needed, level)
    (78,  "Reach Level 5",   1, 5),
    (79,  "Reach Level 10",  1, 10),
    (81,  "Reach Level 15",  1, 15),
    (82,  "Reach Level 20",  2, 20),
    (83,  "Reach Level 30",  4, 30),
]
LEVEL_MILESTONES_NIGHTMARE = [
    (180, "Reach Level 35",  1, 35),
    (181, "Reach Level 40",  1, 40),
    (182, "Reach Level 45",  2, 45),
    (183, "Reach Level 50",  3, 50),
    (184, "Reach Level 55",  4, 55),
]
LEVEL_MILESTONES_HELL = [
    (282, "Reach Level 60",  1, 60),
    (283, "Reach Level 65",  2, 65),
    (284, "Reach Level 70",  3, 70),
    (285, "Reach Level 75",  4, 75),
]

# 1.8.5 fix (R9) — Quest-id → level value lookup, used by regions.py to
# attach access rules to level-milestone locations. Without an access
# rule, AP's fill algorithm could place an Act 1 progression item like
# "Act 1 Gate 2 Key (Normal)" at the "Reach Level 30 (Normal)" location —
# making the gate unreachable until the player grinds to L30 with only
# the starting region available. The fix gates each level milestone
# behind a kill of an act boss, ensuring level milestones live in
# spheres later than the early-act progression they could otherwise
# block. See regions.py for the rule application.
QUEST_ID_TO_LEVEL: dict[int, int] = {}
for _milestones in (LEVEL_MILESTONES_NORMAL,
                    LEVEL_MILESTONES_NIGHTMARE,
                    LEVEL_MILESTONES_HELL):
    for _qid, _name, _max_acts, _level in _milestones:
        QUEST_ID_TO_LEVEL[_qid] = _level

ALL_ACT_LOCATIONS = [
    ACT1_LOCATIONS,
    ACT2_LOCATIONS,
    ACT3_LOCATIONS,
    ACT4_LOCATIONS,
    ACT5_LOCATIONS,
]

# Flat lookup: { name: ap_id } — includes ALL difficulties
# Normal: LOCATION_BASE + quest_id
# Nightmare: LOCATION_BASE + quest_id + 1000 (name + " (Nightmare)")
# Hell: LOCATION_BASE + quest_id + 2000 (name + " (Hell)")
location_table: dict[str, int] = {}
DIFF_NAMES = ["", " (Nightmare)", " (Hell)"]
DIFF_OFFSETS = [0, 1000, 2000]

for act_locs in ALL_ACT_LOCATIONS:
    for quest_id, name, quest_type, classification in act_locs:
        for diff in range(3):
            loc_name = name + DIFF_NAMES[diff]
            loc_id = LOCATION_BASE + quest_id + DIFF_OFFSETS[diff]
            location_table[loc_name] = loc_id

# Add global level milestones (not per-act).
# Each milestone is registered under the bare name (Normal) and, where
# appropriate, with the " (Nightmare)" / " (Hell)" suffixes — one entry per
# difficulty. The previous version reinserted the bare (Normal) key inside
# the NM and Hell loops, overwriting it three times and polluting the table
# with unsuffixed aliases for NM/Hell milestones. Keep only the correct
# suffix per difficulty.
for quest_id, name, max_acts, level in LEVEL_MILESTONES_NORMAL:
    location_table[name] = LOCATION_BASE + quest_id
for quest_id, name, max_acts, level in LEVEL_MILESTONES_NIGHTMARE:
    location_table[name + " (Nightmare)"] = LOCATION_BASE + quest_id + 1000
for quest_id, name, max_acts, level in LEVEL_MILESTONES_HELL:
    location_table[name + " (Hell)"] = LOCATION_BASE + quest_id + 2000

# Reverse lookup: ap_id -> name
location_id_to_name: dict[int, str] = {v: k for k, v in location_table.items()}

# Quest ID to location name (Normal only)
quest_id_to_name: dict[int, str] = {}
for act_locs in ALL_ACT_LOCATIONS:
    for quest_id, name, quest_type, classification in act_locs:
        quest_id_to_name[quest_id] = name

# Act boss quest IDs - completing these unlocks the next act
ACT_BOSS_QUEST_IDS = {
    1: 6,    # Sisters to the Slaughter (Andariel)
    2: 106,  # Seven Tombs (Duriel)
    3: 206,  # The Guardian (Mephisto)
    4: 303,  # Terror's End (Diablo)
}

# Victory condition quest IDs per goal scope
GOAL_QUEST_IDS = {
    0: 6,    # Act 1 Only -> Sisters to the Slaughter
    1: 106,  # Acts 1-2 -> Seven Tombs
    2: 206,  # Acts 1-3 -> The Guardian
    3: 303,  # Acts 1-4 -> Terror's End
    4: 406,  # Full Game -> Eve of Destruction (Baal)
}


# ============================================================
# 1.8.0 NEW — Gate-boss kill locations (preload-gated zone-locking)
# 54 locations: 18 gates × 3 difficulties.
#
# Location ID formula: 47000 + diff*1000 + act*10 + gate_idx
# (matches the DLL formula in d2arch_gameloop.c gate-kill hook)
#
# Examples:
#   47011 = Act 1 Gate 2 Normal    (diff=0, act=1, gate=1)
#   48051 = Act 5 Gate 2 Nightmare (diff=1, act=5, gate=1)
#   49013 = Act 1 Gate 4 Hell      (diff=2, act=1, gate=3)
# ============================================================

GATE_LOCATION_BASE = 47000

def _gate_location_entries():
    out = []
    gates_by_act = {1: 4, 2: 4, 3: 4, 4: 2, 5: 4}
    diff_name = ["Normal", "Nightmare", "Hell"]
    for diff in range(3):
        for act in range(1, 6):
            for g in range(gates_by_act[act]):
                loc_id = GATE_LOCATION_BASE + diff * 1000 + act * 10 + g
                name = f"Act {act} Gate {g + 1} Cleared ({diff_name[diff]})"
                out.append((loc_id, name, act, diff, g))
    return out

GATE_LOCATIONS = _gate_location_entries()  # 54 entries

# Add gate locations to the table
for loc_id, name, _act, _diff, _g in GATE_LOCATIONS:
    location_table[name] = loc_id

# Update reverse lookup after adding gate locations
location_id_to_name = {v: k for k, v in location_table.items()}
