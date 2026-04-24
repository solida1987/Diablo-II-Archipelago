"""
Diablo II Archipelago - Item Definitions

210 skill items (7 classes x 30 skills) + 7 filler item types.
AP ID = 45000 + D2 skill ID for skills.

IMPORTANT: D2 skill IDs must match g_skillDB[] in d2arch.c exactly!
"""
from BaseClasses import ItemClassification

ITEM_BASE = 45000

# ============================================================
# Skill items - one per D2 skill
# (d2_skill_id, name, classification)
#
# IDs come from the actual D2 1.10f skill IDs (skills.txt).
# Names must match what the game displays.
# ============================================================

# Amazon skills: D2 IDs 6-35
AMAZON_SKILLS = [
    # Bow and Crossbow
    (6,  "Magic Arrow",          ItemClassification.progression),
    (7,  "Fire Arrow",           ItemClassification.progression),
    (11, "Cold Arrow",           ItemClassification.progression),
    (12, "Multiple Shot",        ItemClassification.progression),
    (16, "Exploding Arrow",      ItemClassification.progression),
    (21, "Ice Arrow",            ItemClassification.progression),
    (22, "Guided Arrow",         ItemClassification.progression),
    (26, "Strafe",               ItemClassification.progression),
    (27, "Immolation Arrow",     ItemClassification.progression),
    (31, "Freezing Arrow",       ItemClassification.progression),
    # Passive and Magic
    (8,  "Inner Sight",          ItemClassification.progression),
    (9,  "Critical Strike",      ItemClassification.progression),
    (13, "Dodge",                ItemClassification.progression),
    (17, "Slow Missiles",        ItemClassification.progression),
    (18, "Avoid",                ItemClassification.progression),
    (23, "Penetrate",            ItemClassification.progression),
    (28, "Decoy",                ItemClassification.progression),
    (29, "Evade",                ItemClassification.progression),
    (32, "Valkyrie",             ItemClassification.progression),
    (33, "Pierce",               ItemClassification.progression),
    # Javelin and Spear
    (10, "Jab",                  ItemClassification.progression),
    (14, "Power Strike",         ItemClassification.progression),
    (15, "Poison Javelin",       ItemClassification.progression),
    (19, "Impale",               ItemClassification.progression),
    (20, "Lightning Bolt",       ItemClassification.progression),
    (24, "Charged Strike",       ItemClassification.progression),
    (25, "Plague Javelin",       ItemClassification.progression),
    (30, "Fend",                 ItemClassification.progression),
    (34, "Lightning Strike",     ItemClassification.progression),
    (35, "Lightning Fury",       ItemClassification.progression),
]

# Sorceress skills: D2 IDs 36-65
SORCERESS_SKILLS = [
    # Fire
    (36, "Fire Bolt",            ItemClassification.progression),
    (37, "Warmth",               ItemClassification.progression),
    (41, "Inferno",              ItemClassification.progression),
    (46, "Blaze",                ItemClassification.progression),
    (47, "Fire Ball",            ItemClassification.progression),
    (51, "Fire Wall",            ItemClassification.progression),
    (52, "Enchant",              ItemClassification.progression),
    (56, "Meteor",               ItemClassification.progression),
    (61, "Fire Mastery",         ItemClassification.progression),
    (62, "Hydra",                ItemClassification.progression),
    # Lightning
    (38, "Charged Bolt",         ItemClassification.progression),
    (42, "Static Field",         ItemClassification.progression),
    (43, "Telekinesis",          ItemClassification.progression),
    (48, "Nova",                 ItemClassification.progression),
    (49, "Lightning",            ItemClassification.progression),
    (53, "Chain Lightning",      ItemClassification.progression),
    (54, "Teleport",             ItemClassification.progression),
    (57, "Thunder Storm",        ItemClassification.progression),
    (58, "Energy Shield",        ItemClassification.progression),
    (63, "Lightning Mastery",    ItemClassification.progression),
    # Cold
    (39, "Ice Bolt",             ItemClassification.progression),
    (40, "Frozen Armor",         ItemClassification.progression),
    (44, "Frost Nova",           ItemClassification.progression),
    (45, "Ice Blast",            ItemClassification.progression),
    (50, "Shiver Armor",         ItemClassification.progression),
    (55, "Glacial Spike",        ItemClassification.progression),
    (59, "Blizzard",             ItemClassification.progression),
    (60, "Chilling Armor",       ItemClassification.progression),
    (64, "Frozen Orb",           ItemClassification.progression),
    (65, "Cold Mastery",         ItemClassification.progression),
]

# Necromancer skills: D2 IDs 66-95
NECROMANCER_SKILLS = [
    # Curses
    (66, "Amplify Damage",       ItemClassification.progression),
    (71, "Dim Vision",           ItemClassification.progression),
    (72, "Weaken",               ItemClassification.progression),
    (76, "Iron Maiden",          ItemClassification.progression),
    (77, "Terror",               ItemClassification.progression),
    (81, "Confuse",              ItemClassification.progression),
    (82, "Life Tap",             ItemClassification.progression),
    (86, "Attract",              ItemClassification.progression),
    (87, "Decrepify",            ItemClassification.progression),
    (91, "Lower Resist",         ItemClassification.progression),
    # Poison and Bone
    (67, "Teeth",                ItemClassification.progression),
    (68, "Bone Armor",           ItemClassification.progression),
    (73, "Poison Dagger",        ItemClassification.progression),
    (74, "Corpse Explosion",     ItemClassification.progression),
    (78, "Bone Wall",            ItemClassification.progression),
    (83, "Poison Explosion",     ItemClassification.progression),
    (84, "Bone Spear",           ItemClassification.progression),
    (88, "Bone Prison",          ItemClassification.progression),
    (92, "Poison Nova",          ItemClassification.progression),
    (93, "Bone Spirit",          ItemClassification.progression),
    # Summoning
    (70, "Raise Skeleton",       ItemClassification.progression),
    (69, "Skeleton Mastery",     ItemClassification.progression),
    (75, "Clay Golem",           ItemClassification.progression),
    (79, "Golem Mastery",        ItemClassification.progression),
    (80, "Raise Skeletal Mage",  ItemClassification.progression),
    (85, "Blood Golem",          ItemClassification.progression),
    (89, "Summon Resist",        ItemClassification.progression),
    (90, "Iron Golem",           ItemClassification.progression),
    (94, "Fire Golem",           ItemClassification.progression),
    (95, "Revive",               ItemClassification.progression),
]

# Paladin skills: D2 IDs 96-125
PALADIN_SKILLS = [
    # Combat
    (96,  "Sacrifice",           ItemClassification.progression),
    (97,  "Smite",               ItemClassification.progression),
    (101, "Holy Bolt",           ItemClassification.progression),
    (106, "Zeal",                ItemClassification.progression),
    (107, "Charge",              ItemClassification.progression),
    (111, "Vengeance",           ItemClassification.progression),
    (112, "Blessed Hammer",      ItemClassification.progression),
    (116, "Conversion",          ItemClassification.progression),
    (117, "Holy Shield",         ItemClassification.progression),
    (121, "Fist of the Heavens", ItemClassification.progression),
    # Offensive Auras
    (98,  "Might",               ItemClassification.progression),
    (102, "Holy Fire",           ItemClassification.progression),
    (103, "Thorns",              ItemClassification.progression),
    (108, "Blessed Aim",         ItemClassification.progression),
    (113, "Concentration",       ItemClassification.progression),
    (114, "Holy Freeze",         ItemClassification.progression),
    (118, "Holy Shock",          ItemClassification.progression),
    (119, "Sanctuary",           ItemClassification.progression),
    (122, "Fanaticism",          ItemClassification.progression),
    (123, "Conviction",          ItemClassification.progression),
    # Defensive Auras
    (99,  "Prayer",              ItemClassification.progression),
    (100, "Resist Fire",         ItemClassification.progression),
    (104, "Defiance",            ItemClassification.progression),
    (105, "Resist Cold",         ItemClassification.progression),
    (109, "Cleansing",           ItemClassification.progression),
    (110, "Resist Lightning",    ItemClassification.progression),
    (115, "Vigor",               ItemClassification.progression),
    (120, "Meditation",          ItemClassification.progression),
    (124, "Redemption",          ItemClassification.progression),
    (125, "Salvation",           ItemClassification.progression),
]

# Barbarian skills: D2 IDs 126-155
BARBARIAN_SKILLS = [
    # Combat Skills
    (126, "Bash",                ItemClassification.progression),
    (132, "Leap",                ItemClassification.progression),
    (133, "Double Swing",        ItemClassification.progression),
    (139, "Stun",                ItemClassification.progression),
    (140, "Double Throw",        ItemClassification.progression),
    (143, "Leap Attack",         ItemClassification.progression),
    (144, "Concentrate",         ItemClassification.progression),
    (147, "Frenzy",              ItemClassification.progression),
    (151, "Whirlwind",           ItemClassification.progression),
    (152, "Berserk",             ItemClassification.progression),
    # Combat Masteries
    (127, "Sword Mastery",       ItemClassification.progression),
    (128, "Axe Mastery",         ItemClassification.progression),
    (129, "Mace Mastery",        ItemClassification.progression),
    (134, "Pole Arm Mastery",    ItemClassification.progression),
    (135, "Throwing Mastery",    ItemClassification.progression),
    (136, "Spear Mastery",       ItemClassification.progression),
    (141, "Increased Stamina",   ItemClassification.progression),
    (145, "Iron Skin",           ItemClassification.progression),
    (148, "Increased Speed",     ItemClassification.progression),
    (153, "Natural Resistance",  ItemClassification.progression),
    # Warcries
    (130, "Howl",                ItemClassification.progression),
    (131, "Find Potion",         ItemClassification.progression),
    (137, "Taunt",               ItemClassification.progression),
    (138, "Shout",               ItemClassification.progression),
    (142, "Find Item",           ItemClassification.progression),
    (146, "Battle Cry",          ItemClassification.progression),
    (149, "Battle Orders",       ItemClassification.progression),
    (150, "Grim Ward",           ItemClassification.progression),
    (154, "War Cry",             ItemClassification.progression),
    (155, "Battle Command",      ItemClassification.progression),
]

# Druid skills: D2 IDs 221-250
DRUID_SKILLS = [
    # Elemental
    (225, "Firestorm",           ItemClassification.progression),
    (229, "Molten Boulder",      ItemClassification.progression),
    (230, "Arctic Blast",        ItemClassification.progression),
    (234, "Fissure",             ItemClassification.progression),
    (235, "Cyclone Armor",       ItemClassification.progression),
    (240, "Twister",             ItemClassification.progression),
    (244, "Volcano",             ItemClassification.progression),
    (245, "Tornado",             ItemClassification.progression),
    (249, "Armageddon",          ItemClassification.progression),
    (250, "Hurricane",           ItemClassification.progression),
    # Shape Shifting
    (223, "Werewolf",            ItemClassification.progression),
    (224, "Lycanthropy",         ItemClassification.progression),
    (228, "Werebear",            ItemClassification.progression),
    (233, "Maul",                ItemClassification.progression),
    (232, "Feral Rage",          ItemClassification.progression),
    (239, "Fire Claws",          ItemClassification.progression),
    (238, "Rabies",              ItemClassification.progression),
    (243, "Shock Wave",          ItemClassification.progression),
    (242, "Hunger",              ItemClassification.progression),
    (248, "Fury",                ItemClassification.progression),
    # Summoning
    (221, "Raven",               ItemClassification.progression),
    (222, "Poison Creeper",      ItemClassification.progression),
    (226, "Oak Sage",            ItemClassification.progression),
    (227, "Summon Spirit Wolf",  ItemClassification.progression),
    (231, "Carrion Vine",        ItemClassification.progression),
    (236, "Heart of Wolverine",  ItemClassification.progression),
    (237, "Summon Dire Wolf",    ItemClassification.progression),
    (241, "Solar Creeper",       ItemClassification.progression),
    (246, "Spirit of Barbs",     ItemClassification.progression),
    (247, "Summon Grizzly",      ItemClassification.progression),
]

# Assassin skills: D2 IDs 251-280
ASSASSIN_SKILLS = [
    # Martial Arts
    (254, "Tiger Strike",        ItemClassification.progression),
    (255, "Dragon Talon",        ItemClassification.progression),
    (259, "Fists of Fire",       ItemClassification.progression),
    (260, "Dragon Claw",         ItemClassification.progression),
    (265, "Cobra Strike",        ItemClassification.progression),
    (269, "Claws of Thunder",    ItemClassification.progression),
    (270, "Dragon Tail",         ItemClassification.progression),
    (274, "Blades of Ice",       ItemClassification.progression),
    (275, "Dragon Flight",       ItemClassification.progression),
    (280, "Phoenix Strike",      ItemClassification.progression),
    # Shadow Disciplines
    (252, "Claw Mastery",        ItemClassification.progression),
    (253, "Psychic Hammer",      ItemClassification.progression),
    (258, "Burst of Speed",      ItemClassification.progression),
    (263, "Weapon Block",        ItemClassification.progression),
    (264, "Cloak of Shadows",    ItemClassification.progression),
    (267, "Fade",                ItemClassification.progression),
    (268, "Shadow Warrior",      ItemClassification.progression),
    (273, "Mind Blast",          ItemClassification.progression),
    (278, "Venom",               ItemClassification.progression),
    (279, "Shadow Master",       ItemClassification.progression),
    # Traps - ONLY included when "I play Assassin" is ON
    # Non-Assassin characters become invisible and can't act when using these
]

ASSASSIN_TRAP_SKILLS = [
    (251, "Fire Blast",          ItemClassification.progression),
    (256, "Shock Web",           ItemClassification.progression),
    (257, "Blade Sentinel",      ItemClassification.progression),
    (261, "Charged Bolt Sentry", ItemClassification.progression),
    (262, "Wake of Fire",        ItemClassification.progression),
    (266, "Blade Fury",          ItemClassification.progression),
    (271, "Lightning Sentry",    ItemClassification.progression),
    (272, "Wake of Inferno",     ItemClassification.progression),
    (276, "Death Sentry",        ItemClassification.progression),
    (277, "Blade Shield",        ItemClassification.progression),
]

# Per-class skill lists for class filter
CLASS_SKILLS = {
    "amazon":      AMAZON_SKILLS,
    "sorceress":   SORCERESS_SKILLS,
    "necromancer": NECROMANCER_SKILLS,
    "paladin":     PALADIN_SKILLS,
    "barbarian":   BARBARIAN_SKILLS,
    "druid":       DRUID_SKILLS,
    "assassin":    ASSASSIN_SKILLS,
}

# All skill items combined (without trap skills — those are opt-in)
ALL_SKILL_ITEMS = (
    AMAZON_SKILLS + SORCERESS_SKILLS + NECROMANCER_SKILLS +
    PALADIN_SKILLS + BARBARIAN_SKILLS + DRUID_SKILLS + ASSASSIN_SKILLS
)

# Full pool including trap skills (only used when "I play Assassin" is ON)
ALL_SKILL_ITEMS_WITH_TRAPS = ALL_SKILL_ITEMS + ASSASSIN_TRAP_SKILLS

# Filler items
FILLER_ITEMS = [
    (45500, "Gold Bundle (Small)",  ItemClassification.filler),
    (45501, "Gold Bundle (Medium)", ItemClassification.filler),
    (45502, "Gold Bundle (Large)",  ItemClassification.filler),
    (45503, "5 Stat Points",        ItemClassification.filler),
    (45504, "Skill Point",          ItemClassification.filler),
    (45505, "Trap",                 ItemClassification.trap),
    (45506, "Reset Point",          ItemClassification.filler),
    (45507, "Boss Loot Drop",       ItemClassification.filler),
]

# Zone Key items (for Zone Explorer game mode)
# AP ID = 46001 + key_index
ZONE_KEY_BASE = 46001
ZONE_KEY_ITEMS = [
    # Act 1 (10 keys)
    (46001, "Cold Plains Key",        1, ItemClassification.progression),
    (46002, "Burial Grounds Key",     1, ItemClassification.progression),
    (46003, "Stony Field Key",        1, ItemClassification.progression),
    (46004, "Dark Wood Key",          1, ItemClassification.progression),
    (46005, "Black Marsh Key",        1, ItemClassification.progression),
    (46006, "Tristram Key",           1, ItemClassification.progression),
    (46007, "Monastery Key",          1, ItemClassification.progression),
    (46008, "Jail & Cathedral Key",   1, ItemClassification.progression),
    (46009, "Catacombs Key",          1, ItemClassification.progression),
    (46010, "Andariel's Lair Key",    1, ItemClassification.progression),
    # Act 2 (8 keys)
    (46011, "Rocky Waste Key",        2, ItemClassification.progression),
    (46012, "Dry Hills Key",          2, ItemClassification.progression),
    (46013, "Far Oasis Key",          2, ItemClassification.progression),
    (46014, "Lost City Key",          2, ItemClassification.progression),
    (46015, "Palace Key",             2, ItemClassification.progression),
    (46016, "Arcane Sanctuary Key",   2, ItemClassification.progression),
    (46017, "Canyon of the Magi Key", 2, ItemClassification.progression),
    (46018, "Duriel's Lair Key",      2, ItemClassification.progression),
    # Act 3 (6 keys)
    (46019, "Spider Forest Key",      3, ItemClassification.progression),
    (46020, "Jungle Key",             3, ItemClassification.progression),
    (46021, "Kurast Key",             3, ItemClassification.progression),
    (46022, "Upper Kurast Key",       3, ItemClassification.progression),
    (46023, "Travincal Key",          3, ItemClassification.progression),
    (46024, "Durance of Hate Key",    3, ItemClassification.progression),
    # Act 4 (4 keys)
    (46025, "Outer Steppes Key",      4, ItemClassification.progression),
    (46026, "City of the Damned Key", 4, ItemClassification.progression),
    (46027, "River of Flame Key",     4, ItemClassification.progression),
    (46028, "Chaos Sanctuary Key",    4, ItemClassification.progression),
    # Act 5 (7 keys)
    (46029, "Bloody Foothills Key",   5, ItemClassification.progression),
    (46030, "Highlands Key",          5, ItemClassification.progression),
    (46031, "Caverns Key",            5, ItemClassification.progression),
    (46032, "Summit Key",             5, ItemClassification.progression),
    (46033, "Nihlathak Key",          5, ItemClassification.progression),
    (46034, "Worldstone Keep Key",    5, ItemClassification.progression),
    (46035, "Throne of Destruction Key", 5, ItemClassification.progression),
]

# 1.8.0 NEW — Gate Keys for the gated zone-locking preload system.
# 18 gates per difficulty × 3 difficulties = 54 items.
# Slot layout per difficulty: 0-3=A1G1..4, 4-7=A2G1..4, 8-11=A3G1..4,
# 12-13=A4G1..2 (Act 4 has only 2), 14-17=A5G1..4.
# AP IDs: Normal=46101-46118, Nightmare=46121-46138, Hell=46141-46158.
GATE_KEY_AP_BASE = {0: 46101, 1: 46121, 2: 46141}
GATE_KEYS_PER_DIFF = 18

def _gate_key_entries():
    out = []
    # Gates per (act, diff):
    # Act 1,2,3,5: 4 gates each. Act 4: 2 gates.
    gates_by_act = {1: 4, 2: 4, 3: 4, 4: 2, 5: 4}
    slot_by_act_gate = {
        1: lambda g: 0 + g,
        2: lambda g: 4 + g,
        3: lambda g: 8 + g,
        4: lambda g: 12 + g,
        5: lambda g: 14 + g,
    }
    diff_name = ["Normal", "Nightmare", "Hell"]
    for diff in range(3):
        base = GATE_KEY_AP_BASE[diff]
        for act, num_gates in gates_by_act.items():
            for g in range(num_gates):
                slot = slot_by_act_gate[act](g)
                ap_id = base + slot
                name = f"Act {act} Gate {g + 1} Key ({diff_name[diff]})"
                out.append((ap_id, name, act, ItemClassification.progression))
    return out

GATE_KEY_ITEMS = _gate_key_entries()  # 54 entries

# Build the complete item table: { name: (ap_id, classification) }
item_table: dict[str, tuple[int, ItemClassification]] = {}

for d2_id, name, classification in ALL_SKILL_ITEMS_WITH_TRAPS:
    item_table[name] = (ITEM_BASE + d2_id, classification)

for ap_id, name, classification in FILLER_ITEMS:
    item_table[name] = (ap_id, classification)

for ap_id, name, act, classification in ZONE_KEY_ITEMS:
    item_table[name] = (ap_id, classification)

# 1.8.0 NEW — Gate Keys (54 items for preload-gated zone-locking)
for ap_id, name, act, classification in GATE_KEY_ITEMS:
    item_table[name] = (ap_id, classification)

# Reverse lookup: ap_id -> name
item_id_to_name: dict[int, str] = {v[0]: k for k, v in item_table.items()}

# Skill ID to name lookup for the bridge
skill_id_to_name: dict[int, str] = {d2_id: name for d2_id, name, _ in ALL_SKILL_ITEMS_WITH_TRAPS}
