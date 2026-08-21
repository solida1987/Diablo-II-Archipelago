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

# ============================================================
# TIER 1 skill ids — the 10 lowest-row skills of each class (70 total).
#
# Mirrors the `tier` column of g_skillDB in d2arch_skills.c, which is the
# authoritative table; regenerate with Tools/gen_tier1_ids.py if that table
# ever changes. Starting skills are drawn ONLY from this set, in AP exactly
# as in standalone: a run that opens with Frozen Orb or Blessed Hammer is not
# a run, and the standalone side has always granted its starting skills from
# tier 1 (the unlock loop only touches the T1 array) — AP handed out the first
# N of a pool shuffled across all three tiers, so the two modes disagreed.
# ============================================================
TIER1_SKILL_IDS = frozenset([6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 251, 252, 253, 254, 255, 256, 257, 258, 259, 260])

# Tiers 2 and 3, from the same g_skillDB column as tier 1. Used by the
# pool-size option to split its quota evenly across the three tiers.
TIER2_SKILL_IDS = frozenset([16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270])
TIER3_SKILL_IDS = frozenset([26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280])

CLASS_SKILLS = {
    "amazon":      AMAZON_SKILLS,
    "sorceress":   SORCERESS_SKILLS,
    "necromancer": NECROMANCER_SKILLS,
    "paladin":     PALADIN_SKILLS,
    "barbarian":   BARBARIAN_SKILLS,
    "druid":       DRUID_SKILLS,
    "assassin":    ASSASSIN_SKILLS,
}

# 1.9.11 — NATIVE-ONLY skill IDs (mirror of d2arch_skills.c:300-311).
#
# These skills are tied to class-specific animations (Amazon javelins use TH,
# Paladin Smite uses S1 shield-bash, Barbarian sequences use SQ, Druid bites
# use S3, Assassin kicks/claws use KK). When a non-native class receives one,
# the DLL's `IsNativeOnlySkill` filter excludes it from the local pool and
# the skill never appears in the player's right-click list.
#
# Pre-1.9.11 the apworld didn't know about this and would happily place these
# skill items into a slot for any class. The DLL then deferred (B2 fix) or
# permanently ate (pre-B2) the apId. Now the apworld filters them out when
# the player's class (via `skill_class_filter` toggles) is not the native
# owner — saving multiworld bandwidth and avoiding deferred-skill spam.
#
# Keep this list in sync with d2arch_skills.c. The DLL is the runtime
# source of truth; the apworld filter is a build-time optimisation.
NATIVE_ONLY_SKILL_IDS = {
    15, 20, 25, 35,            # Amazon — pure throwing javelins
    97,                         # Paladin — shield-bash (Smite)
    133, 140, 143, 151,         # Barbarian — sequence skills
    238, 242,                   # Druid — werewolf-form bites
    255, 259, 260, 266, 269, 274,  # Assassin — kicks/claws/blade-fury/charge-up
}

# Which class each NATIVE_ONLY skill belongs to (for include-class filtering).
NATIVE_ONLY_SKILL_CLASS = {
    15: "amazon", 20: "amazon", 25: "amazon", 35: "amazon",
    97: "paladin",
    133: "barbarian", 140: "barbarian", 143: "barbarian", 151: "barbarian",
    238: "druid", 242: "druid",
    255: "assassin", 259: "assassin", 260: "assassin",
    266: "assassin", 269: "assassin", 274: "assassin",
}

# All skill items combined (without trap skills — those are opt-in)
# Skill display name -> skill id. Built from the per-class tables so nothing
# has to be listed twice; used to test a placed item against TIER1_SKILL_IDS.
SKILL_ID_BY_NAME = {
    name: sid
    for skills in CLASS_SKILLS.values()
    for (sid, name, _cls) in skills
}

ALL_SKILL_ITEMS = (
    AMAZON_SKILLS + SORCERESS_SKILLS + NECROMANCER_SKILLS +
    PALADIN_SKILLS + BARBARIAN_SKILLS + DRUID_SKILLS + ASSASSIN_SKILLS
)

# Full pool including trap skills (only used when "I play Assassin" is ON)
ALL_SKILL_ITEMS_WITH_TRAPS = ALL_SKILL_ITEMS + ASSASSIN_TRAP_SKILLS

# Filler items
#
# 1.9.0 redesign: replaced the 8 generic placeholders with 17 typed
# fillers. The DLL pre-rolls specific magnitudes (gold 1-10000, xp
# 1-250000) and specific item picks (which charm / which set piece /
# which unique) at character creation, stores them in the per-char
# state file, and consumes them deterministically when the matching
# AP item arrives. This replaces the old "roll-at-drop-time" model
# so spoilers (both AP and the new standalone spoiler file) can show
# what each location's reward will be before the run starts.
FILLER_ITEMS = [
    # Direct stat / point items
    (45500, "Gold",                       ItemClassification.filler),  # DLL rolls 1-10000
    (45503, "5 Stat Points",              ItemClassification.filler),
    (45504, "Skill Point",                ItemClassification.filler),
    (45506, "Reset Point",                ItemClassification.filler),
    (45508, "Experience",                 ItemClassification.filler),  # DLL rolls 1-250000

    # Trap variants (replaces single 45505 "Trap")
    (45505, "Trap: Monsters",             ItemClassification.trap),
    (45511, "Trap: Slow",                 ItemClassification.trap),
    (45512, "Trap: Weaken",               ItemClassification.trap),
    (45513, "Trap: Poison",               ItemClassification.trap),

    # Boss-loot drops (replaces single 45507 "Boss Loot Drop")
    # Each drops the named boss's TC at the player's ilvl+5.
    (45514, "Drop: Andariel Loot",        ItemClassification.filler),
    (45515, "Drop: Duriel Loot",          ItemClassification.filler),
    (45516, "Drop: Mephisto Loot",        ItemClassification.filler),
    (45517, "Drop: Diablo Loot",          ItemClassification.filler),
    (45518, "Drop: Baal Loot",            ItemClassification.filler),

    # Specific-item drops. DLL pre-rolls WHICH charm / set piece /
    # unique to drop at char creation; the spoiler file shows the
    # specific name. Items always drop unidentified, like a real
    # monster drop.
    (45519, "Drop: Random Charm",         ItemClassification.filler),
    (45520, "Drop: Random Set Item",      ItemClassification.filler),
    (45521, "Drop: Random Unique",        ItemClassification.filler),

    # Ordinary-item bundles. Each maps to one entry in the
    # DLL's g_fillerBatches table by id order; do not reorder.
    (45522, "Random Rune",              ItemClassification.filler),
    (45523, "Random Gem",               ItemClassification.filler),
    (45524, "Rejuvenation Potions",     ItemClassification.filler),
    (45525, "Full Rejuvenation Potions",  ItemClassification.filler),
    (45526, "Greater Healing Potions",  ItemClassification.filler),
    (45527, "Greater Mana Potions",     ItemClassification.filler),
    (45528, "Throwing Potions",         ItemClassification.filler),
    (45529, "Random Jewel",             ItemClassification.filler),
    (45530, "Tome of Town Portal",      ItemClassification.filler),
    (45531, "Tome of Identify",         ItemClassification.filler),
    (45532, "Skeleton Keys",            ItemClassification.filler),
    (45533, "Utility Potions",          ItemClassification.filler),

    # Tiered variants of the bundles above. Same table, same id ordering
    # rule. These are the ones tierfill.py pushes into the later spheres,
    # which is where their rarity comes from.
    (45534, "Random Rune (Mid)",        ItemClassification.filler),
    (45535, "Random Rune (High)",       ItemClassification.filler),
    (45536, "Flawless Gem",             ItemClassification.filler),
    (45537, "Perfect Gem",              ItemClassification.filler),
    (45538, "Small Charm",              ItemClassification.filler),
    (45539, "Large Charm",              ItemClassification.filler),
    (45540, "Grand Charm",              ItemClassification.filler),

    # Point items in discrete sizes. Not part of the batch table — these
    # have their own cases in the DLL and may sit at any id.
    (45541, "1 Stat Point",             ItemClassification.filler),
    (45542, "3 Stat Points",            ItemClassification.filler),
    (45543, "10 Stat Points",           ItemClassification.filler),
    (45544, "2 Skill Points",           ItemClassification.filler),
    (45545, "3 Skill Points",           ItemClassification.filler),
    (45546, "3 Reset Points",           ItemClassification.filler),

    # Object traps. These spawn real world objects around the player
    # rather than applying a status effect, so they are the first traps
    # that can physically get in the way.
    (45547, "Trap: Barrel Field",       ItemClassification.trap),
    (45548, "Trap: Urn Garden",         ItemClassification.trap),
    (45549, "Trap: False Treasure",     ItemClassification.trap),
    (45550, "Trap: Junk Backpack",      ItemClassification.trap),

    # Set and unique drops narrowed to one equipment slot. The eight slot
    # names appear in the same order in both blocks and the DLL derives
    # the slot as (id - 45551) % 8, so the ORDER here is load-bearing.
    # If a slot turns out empty the DLL falls back to an unfiltered roll
    # rather than dropping nothing.
    (45551, "Drop: Set Armor",          ItemClassification.filler),
    (45552, "Drop: Set Helm",           ItemClassification.filler),
    (45553, "Drop: Set Shield",         ItemClassification.filler),
    (45554, "Drop: Set Gloves",         ItemClassification.filler),
    (45555, "Drop: Set Boots",          ItemClassification.filler),
    (45556, "Drop: Set Belt",           ItemClassification.filler),
    (45557, "Drop: Set Weapon",         ItemClassification.filler),
    (45558, "Drop: Set Jewelry",        ItemClassification.filler),
    (45559, "Drop: Unique Armor",       ItemClassification.filler),
    (45560, "Drop: Unique Helm",        ItemClassification.filler),
    (45561, "Drop: Unique Shield",      ItemClassification.filler),
    (45562, "Drop: Unique Gloves",      ItemClassification.filler),
    (45563, "Drop: Unique Boots",       ItemClassification.filler),
    (45564, "Drop: Unique Belt",        ItemClassification.filler),
    (45565, "Drop: Unique Weapon",      ItemClassification.filler),
    (45566, "Drop: Unique Jewelry",     ItemClassification.filler),
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
            # One ITEM per (act, diff) now; the copy count is num_gates.
            for g in range(1):
                slot = slot_by_act_gate[act](0)
                ap_id = base + slot
                # V5 - PROGRESSIVE act keys. Every gate of an act now shares ONE
                # item name, delivered as `num_gates` copies: the Nth copy you
                # receive opens that act's Nth gate, so ANY copy makes progress.
                # Previously each of the 54 keys unlocked one specific gate, which
                # made the whole act a strict 54-link serial chain that AP's fill
                # could not flatten (early D2 keys landing behind other games'
                # end-game). The AP id stays the act's FIRST gate id so existing
                # id ranges and the DLL's (diff, slot) decode are unchanged.
                name = f"Progressive Act {act} Key ({diff_name[diff]})"
                out.append((ap_id, name, act, ItemClassification.progression))
    return out

GATE_KEY_ITEMS = _gate_key_entries()  # one entry per (act, diff)
# V5 - how many copies of each progressive act key exist (= that act's gate count).
GATE_COPIES_BY_ACT = {1: 4, 2: 4, 3: 4, 4: 2, 5: 4}

# 2.1 fix — boss/gate gating tokens are locked at REAL act-boss & gate
# check-locations to drive region sphere logic via state.has(<token name>).
# They MUST carry a real integer code, NOT None: AP 0.6.7's
# _speedups.LocationStore packs the multidata locations into C int arrays, and a
# None item code crashes the SERVER load with "TypeError: an integer is
# required" (generation itself doesn't validate this, so the seed gens fine but
# won't host). One shared registered id is enough — region gating keys off the
# unique token NAME; the code only feeds the network multidata / datapackage.
BOSS_TOKEN_ITEM_ID = 46200

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

# 2.1 — register the shared boss/gate gating-token item id so the datapackage
# resolves it and AP fill accepts the locked tokens (see BOSS_TOKEN_ITEM_ID).
item_table["Boss Token"] = (BOSS_TOKEN_ITEM_ID, ItemClassification.progression)

# Reverse lookup: ap_id -> name
item_id_to_name: dict[int, str] = {v[0]: k for k, v in item_table.items()}

# Skill ID to name lookup for the bridge
skill_id_to_name: dict[int, str] = {d2_id: name for d2_id, name, _ in ALL_SKILL_ITEMS_WITH_TRAPS}
