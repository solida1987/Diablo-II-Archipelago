"""
Diablo II Archipelago - World Options

1.8.0 cleanup: options list now mirrors what the in-game title-screen
settings panel offers. Removed options that were AP-only / duplicates /
internal balance knobs:
  - GameMode (deprecated; replaced by SkillHunting + ZoneLocking toggles)
  - SkillPoolSize (always 210; class filter already handles restriction)
  - 6 Filler*Pct options (internal balance — DLL uses sensible defaults)
  - 15 Act<N>_Preload_<Diff> options (AP generates randomly per slot)
"""
from dataclasses import dataclass

from Options import Choice, Range, Toggle, DeathLink, PerGameCommonOptions, OptionGroup


# ============================================================
# Goal & Game Mode
# ============================================================

class SkillHunting(Toggle):
    """
    Enable Skill Hunting. When ON, skills are added to the AP item pool as
    useful items — quests unlock skills from the randomized pool.

    Can be combined with Zone Locking (both ON = hybrid mode).
    """
    display_name = "Skill Hunting"
    default = 1


class ZoneLocking(Toggle):
    """
    Enable Zone Locking. When ON, the game is gated by 18 semi-random
    boss-kill gates per difficulty (4 per act, 2 for Act 4). Each gate
    boss drop sends a check; receiving the gate key opens the next region.

    Can be combined with Skill Hunting (both ON = hybrid mode).
    """
    display_name = "Zone Locking"
    default = 0


class Goal(Choice):
    """
    Determines what condition you must satisfy to win.

    Full Normal:    beat Baal on Normal.
    Full Nightmare: beat Baal on Normal AND Nightmare.
    Full Hell:      beat Baal on Normal, Nightmare, AND Hell.
    Collection:     fill the F1 Collection book — every targeted set,
                    rune, gem, special item, and (optionally) a gold
                    threshold. Difficulty progression is OPTIONAL in
                    this mode; you win the moment your collection
                    targets are satisfied.
    """
    display_name = "Goal"
    option_full_normal = 0
    option_full_nightmare = 1
    option_full_hell = 2
    option_collection = 3
    default = 0


# ============================================================
# Collection Goal sub-targets (only meaningful when Goal=Collection)
# ============================================================

class CollectionTargetGems(Toggle):
    """[Goal=Collection only] Require all 35 gems (7 colors × 5
    grades, Chipped..Perfect) to be collected once on this character.
    Note: gems do NOT have per-item granularity — it's all 35 or none.
    Each individual gem pickup still counts as an AP location check
    when this toggle is ON."""
    display_name = "Collection: Gems"
    default = True


class CollectionGoldTarget(Range):
    """[Goal=Collection only] Optional lifetime-gold threshold for
    the Collection goal. 0 = no gold target. The lifetime gold
    counter on the F1 Collection page is monotonic (never decreases)
    and tracks gold pickup + quest rewards (excluding vendor sales).
    Set this to require, e.g., 1,000,000 gold collected as part of
    the goal."""
    display_name = "Collection: Gold Target"
    range_start = 0
    range_end = 100000000
    default = 0


# ============================================================
# Per-item Collection toggles (Goal=Collection only)
#
# 32 sets + 33 runes + 10 specials = 75 individual checkboxes.
# Each enabled item generates an AP location/check when collected
# (handled in __init__.py:create_locations + DLL Coll_MarkSlotCollected).
#
# Standalone (non-AP) mode IGNORES these toggles and uses ALL of
# them as collection targets — the YAML options only matter when
# generating a multiworld.
#
# Defaults: every set/rune/special is ON by default. Players who
# want a focused goal (e.g. "just runes" or "no class-locked sets")
# can disable individual items in their YAML.
#
# The classes below are generated programmatically to keep this
# file readable. Each becomes a proper Toggle subclass added to
# this module's namespace, then registered in PerGameOptions later.
# ============================================================

import sys as _sys

# (slot_data field, display name) — 32 vanilla sets in catalog order.
# slot_data field names match the DLL parser in d2arch_save.c /
# d2arch_ap.c so don't rename without also updating those.
_COLL_SETS = [
    ("collect_set_civerbs",    "Civerb's Vestments"),
    ("collect_set_hsarus",     "Hsarus' Defense"),
    ("collect_set_cleglaws",   "Cleglaw's Brace"),
    ("collect_set_irathas",    "Iratha's Finery"),
    ("collect_set_isenharts",  "Isenhart's Armory"),
    ("collect_set_vidalas",    "Vidala's Rig"),
    ("collect_set_milabregas", "Milabrega's Regalia"),
    ("collect_set_cathans",    "Cathan's Traps"),
    ("collect_set_tancreds",   "Tancred's Battlegear"),
    ("collect_set_sigons",     "Sigon's Complete Steel"),
    ("collect_set_infernal",   "Infernal Tools"),
    ("collect_set_berserkers", "Berserker's Garb"),
    ("collect_set_deaths",     "Death's Disguise"),
    ("collect_set_angelical",  "Angelical Raiment"),
    ("collect_set_arctic",     "Arctic Gear"),
    ("collect_set_arcannas",   "Arcanna's Tricks"),
    # Class-locked LoD sets (7)
    ("collect_set_natalyas",   "Natalya's Odium [Assassin]"),
    ("collect_set_aldurs",     "Aldur's Watchtower [Druid]"),
    ("collect_set_immortal",   "Immortal King [Barbarian]"),
    ("collect_set_talrasha",   "Tal Rasha's Wrappings [Sorceress]"),
    ("collect_set_griswolds",  "Griswold's Legacy [Paladin]"),
    ("collect_set_trangouls",  "Trang-Oul's Avatar [Necromancer]"),
    ("collect_set_mavinas",    "M'avina's Battle Hymn [Amazon]"),
    # Generic LoD sets (9)
    ("collect_set_disciple",   "The Disciple"),
    ("collect_set_heavens",    "Heaven's Brethren"),
    ("collect_set_orphans",    "Orphan's Call"),
    ("collect_set_hwanins",    "Hwanin's Majesty"),
    ("collect_set_sazabis",    "Sazabi's Grand Tribute"),
    ("collect_set_bulkathos",  "Bul-Kathos' Children"),
    ("collect_set_cowking",    "Cow King's Leathers"),
    ("collect_set_najs",       "Naj's Ancient Set"),
    ("collect_set_mcauleys",   "McAuley's Folly"),
]

# 33 runes from El (low) to Zod (top).
_COLL_RUNES = [
    "el", "eld", "tir", "nef", "eth", "ith", "tal", "ral", "ort", "thul",
    "amn", "sol", "shael", "dol", "hel", "io", "lum", "ko", "fal", "lem",
    "pul", "um", "mal", "ist", "gul", "vex", "ohm", "lo", "sur", "ber",
    "jah", "cham", "zod",
]

# 10 specials (3 keys + 3 organs + 3 essences + Hellfire Torch).
_COLL_SPECIALS = [
    ("collect_special_pk1",  "Key of Terror (pk1)"),
    ("collect_special_pk2",  "Key of Hate (pk2)"),
    ("collect_special_pk3",  "Key of Destruction (pk3)"),
    ("collect_special_mbr",  "Mephisto's Brain (mbr)"),
    ("collect_special_dhn",  "Diablo's Horn (dhn)"),
    ("collect_special_bey",  "Baal's Eye (bey)"),
    ("collect_special_tes",  "Twisted Essence of Suffering (tes)"),
    ("collect_special_ceh",  "Charged Essence of Hatred (ceh)"),
    ("collect_special_bet",  "Burning Essence of Terror (bet)"),
    ("collect_special_cm2",  "Hellfire Torch (cm2)"),
]

def _make_collect_toggle(field_name, display, doc_extra=""):
    """Create a Toggle subclass at runtime and register it on this
    module so the options framework can pick it up by attribute name."""
    cls_name = ''.join(p.capitalize() for p in field_name.split('_'))
    bases = (Toggle,)
    attrs = {
        '__doc__': f"[Goal=Collection only] Include this item in your "
                   f"collection goal. When ON in AP mode, the slot becomes "
                   f"an AP location/check that fires when the item is "
                   f"first collected. {doc_extra}".strip(),
        'display_name': f"Collect: {display}",
        'default': True,
    }
    cls = type(cls_name, bases, attrs)
    setattr(_sys.modules[__name__], cls_name, cls)
    return cls_name, cls

# Build all 75 toggle classes. The returned (cls_name, cls) pairs
# are stashed on private lists so the dataclass annotations later
# can reference them in the same order.
_COLL_SET_CLASSES = [
    _make_collect_toggle(field, display)
    for field, display in _COLL_SETS
]
_COLL_RUNE_CLASSES = [
    _make_collect_toggle(f"collect_rune_{name}", f"{name.capitalize()} Rune")
    for name in _COLL_RUNES
]
_COLL_SPECIAL_CLASSES = [
    _make_collect_toggle(field, display)
    for field, display in _COLL_SPECIALS
]


# ============================================================
# Quest Type Toggles (which quest types generate locations)
# ============================================================

class QuestHunting(Toggle):
    """Include SuperUnique hunting quests (Corpsefire, Bishibosh, etc)."""
    display_name = "Hunting Quests"
    default = True


class QuestKillZones(Toggle):
    """Include zone-clear quests (kill X monsters in Blood Moor, etc)."""
    display_name = "Kill Zone Quests"
    default = True


class QuestExploration(Toggle):
    """Include zone-exploration quests (enter Blood Moor, Cold Plains, etc)."""
    display_name = "Exploration Quests"
    default = True


class QuestWaypoints(Toggle):
    """Include waypoint-activation quests."""
    display_name = "Waypoint Quests"
    default = True


class QuestLevelMilestones(Toggle):
    """Include level milestone quests (Reach Level 5, 10, 15... 50)."""
    display_name = "Level Milestone Quests"
    default = True


# ============================================================
# Skill class filter
# ============================================================

class SkillClassFilter(Choice):
    """
    Filter which class skills are included in the pool.

    All Classes: All 7 classes (210 total skills in pool).
    Custom: Only selected classes via the class-toggle options below.
    """
    display_name = "Skill Class Filter"
    option_all_classes = 0
    option_custom = 1
    default = 0


class IncludeAmazon(Toggle):
    """Include Amazon skills in the pool (when Class Filter = Custom)."""
    display_name = "Include Amazon"
    default = True

class IncludeSorceress(Toggle):
    """Include Sorceress skills in the pool (when Class Filter = Custom)."""
    display_name = "Include Sorceress"
    default = True

class IncludeNecromancer(Toggle):
    """Include Necromancer skills in the pool (when Class Filter = Custom)."""
    display_name = "Include Necromancer"
    default = True

class IncludePaladin(Toggle):
    """Include Paladin skills in the pool (when Class Filter = Custom)."""
    display_name = "Include Paladin"
    default = True

class IncludeBarbarian(Toggle):
    """Include Barbarian skills in the pool (when Class Filter = Custom)."""
    display_name = "Include Barbarian"
    default = True

class IncludeDruid(Toggle):
    """Include Druid skills in the pool (when Class Filter = Custom)."""
    display_name = "Include Druid"
    default = True

class IncludeAssassin(Toggle):
    """Include Assassin skills in the pool (when Class Filter = Custom).
    Trap skills are always excluded to prevent the invisible-character bug
    on non-Assassin classes. This matches 1.8.0 in-game behaviour."""
    display_name = "Include Assassin"
    default = True


# ============================================================
# XP Multiplier
# ============================================================

class XPMultiplier(Range):
    """
    Multiply XP gains from monster kills (1 = vanilla, up to 10x).
    Higher = faster leveling, less grind.
    """
    display_name = "XP Multiplier"
    range_start = 1
    range_end = 10
    default = 1


# ============================================================
# Shuffles
# ============================================================

class MonsterShuffle(Toggle):
    """Shuffle monster types across zones. Each game gets a new monster layout."""
    display_name = "Monster Shuffle"
    default = False


class BossShuffle(Toggle):
    """Shuffle SuperUnique boss placements across zones."""
    display_name = "Boss Shuffle"
    default = False


class EntranceShuffle(Toggle):
    """
    Shuffle dead-end cave entrances across each character's seed.
    Pool A (Acts 1+2) and Pool B (Acts 3+4+5) are shuffled
    independently so progression remains solvable.

    Frozen at character creation — once baked into the per-character
    state file, AP reconnects can't change it.
    """
    display_name = "Entrance Shuffle"
    default = False


# ============================================================
# Filler Options
# ============================================================

class TrapsEnabled(Toggle):
    """
    Enable trap fillers in the item pool. When OFF, traps are removed
    entirely — that share of the filler pool is redistributed proportionally
    to the other filler types (gold, stat points, skill points, reset
    points, boss-loot drops) so the pool stays balanced.

    Default ON to preserve existing trap-bearing seeds. Turn OFF if you
    don't want DeathLink-style monster-spawn traps in your run.
    """
    display_name = "Traps Enabled"
    default = True


# ============================================================
# Bonus Check Categories (1.9.0 — opt-in)
# ============================================================
# These add up to ~1494 extra check locations that fire on object
# interactions instead of quest completions. All filler-only — AP
# never places progression items at these locations, so unfilled
# slots cannot soft-lock the run.
#
# Per-difficulty quotas are gated by the active goal scope: a goal
# of full_normal only includes Normal slots, full_nightmare adds
# Nightmare's quota on top, full_hell adds Hell's quota on top.
#
# Each interaction rolls an escalating chance: 10% on the first
# attempt for a given slot, +10% per consecutive miss, capped at 100%
# (guaranteed) on the 10th attempt. Resets to 10% after a hit.

class CheckShrines(Toggle):
    """
    Add 50 shrine activations per difficulty as AP locations.
    Smashing a shrine rolls the escalating-chance check (avg ~3.5 tries
    per slot). Filler-only — no progression items are placed here.
    """
    display_name = "Check Shrines"
    default = False


class CheckUrns(Toggle):
    """
    Add 100 urn/jar destructions per difficulty as AP locations.
    Plenty of urns in tombs/dungeons make this easy to fill.
    Filler-only.
    """
    display_name = "Check Urns"
    default = False


class CheckBarrels(Toggle):
    """
    Add 100 barrel destructions per difficulty as AP locations.
    Filler-only.
    """
    display_name = "Check Barrels"
    default = False


class CheckChests(Toggle):
    """
    Add 200 chest openings per difficulty as AP locations.
    All chest types count (small/large/super/locked).
    Filler-only.
    """
    display_name = "Check Chests"
    default = False


class CheckSetPickups(Toggle):
    """
    Fire an AP check the first time you pick up each unique set piece.
    Up to 127 checks total across all set pieces. Respects the 32
    individual collect_set_* toggles — pieces of disabled sets don't
    count.
    """
    display_name = "Check Set Pickups"
    default = False


class CheckGoldMilestones(Toggle):
    """
    Add 17 lifetime-gold milestone checks. Normal grants 7 milestones
    up to 3M, Nightmare adds 5 more up to 6M, Hell adds the final 5
    up to 12M. Uses the same lifetime gold counter as Goal=Collection.
    """
    display_name = "Check Gold Milestones"
    default = False


# ============================================================
# 1.9.2 — Six new check categories on top of the bonus checks above.
# Each is an independent opt-in toggle. AP location ranges (per
# DLL d2arch_extrachecks.c):
#   Cow Level expansion       65300-65308   (9 slots)
#   Mercenary milestones      65310-65315   (6 slots)
#   Hellforge + High runes    65320-65331   (12 slots)
#   Per-NPC dialogue          65400-65480   (81 slots)
#   Runeword crafting         65500-65549   (50 slots)
#   Cube recipes              65600-65734   (135 slots)
# ============================================================

class CheckCowLevel(Toggle):
    """
    Cow Level expansion: 9 AP locations covering first entry per
    difficulty (3), Cow King kills per difficulty (3), and lifetime
    cow-kill milestones at 100/500/1000 (3). Great if your goal
    includes farming the Moo Moo Farm.
    """
    display_name = "Check Cow Level"
    default = False


class CheckMercMilestones(Toggle):
    """
    Mercenary milestones: 6 AP locations covering first hire,
    5/10/25/50 lifetime resurrections, and reaching merc level 30+.
    Encourages keeping a merc alive across the campaign.
    """
    display_name = "Check Merc Milestones"
    default = False


class CheckHellforgeRunes(Toggle):
    """
    Hellforge + High Runes: 12 AP locations covering Hellforge use
    per difficulty (3) and first pickup of each high-rune tier
    (Pul-Gul, Vex-Ber, Jah-Zod) per difficulty (9). Pure
    progression encouragement for endgame rune farming.
    """
    display_name = "Check Hellforge & High Runes"
    default = False


class CheckNpcDialogue(Toggle):
    """
    Per-NPC dialogue: 81 AP locations covering first dialogue with
    each major NPC across all 3 difficulties. 1.9.2 ships the
    apworld locations and AP wiring; the in-game DLL detection hook
    lands in 1.9.3 (until then the slots can be unlocked via the
    AP server's /release command if you want to test the placement).
    """
    display_name = "Check NPC Dialogue"
    default = False


class CheckRunewordCrafting(Toggle):
    """
    Runeword crafting: 50 AP locations covering the first time you
    create each runeword. 1.9.2 ships the apworld locations and AP
    wiring; in-game detection hangs off the existing Coll_ProcessItem
    runeword-flag transition and is finalized in 1.9.3.
    """
    display_name = "Check Runeword Crafting"
    default = False


class CheckCubeRecipes(Toggle):
    """
    Horadric cube recipe completions: 135 AP locations covering the
    first successful completion of each cube recipe. 1.9.2 ships the
    apworld locations and AP wiring; the cube-state diff detection
    lands in 1.9.3.
    """
    display_name = "Check Cube Recipes"
    default = False


# ============================================================
# Combined Options Dataclass
# ============================================================

from dataclasses import make_dataclass

# Build the option list programmatically so the 75 collection toggles
# don't bloat this file with manual field-by-field declarations. The
# resulting dataclass is functionally identical to the old static
# @dataclass — AP's framework just reads __annotations__.
_FIELDS = [
    # Mode toggles
    ("skill_hunting",        SkillHunting),
    ("zone_locking",         ZoneLocking),
    # Goal
    ("goal",                 Goal),
    # Gems-as-a-whole + gold target (granular per-item toggles below)
    ("collection_target_gems",  CollectionTargetGems),
    ("collection_gold_target",  CollectionGoldTarget),
    # Quest toggles (story is always ON internally — engine-required)
    ("quest_hunting",          QuestHunting),
    ("quest_kill_zones",       QuestKillZones),
    ("quest_exploration",      QuestExploration),
    ("quest_waypoints",        QuestWaypoints),
    ("quest_level_milestones", QuestLevelMilestones),
    # Class filter
    ("skill_class_filter",     SkillClassFilter),
    ("include_amazon",         IncludeAmazon),
    ("include_sorceress",      IncludeSorceress),
    ("include_necromancer",    IncludeNecromancer),
    ("include_paladin",        IncludePaladin),
    ("include_barbarian",      IncludeBarbarian),
    ("include_druid",          IncludeDruid),
    ("include_assassin",       IncludeAssassin),
    # XP
    ("xp_multiplier",          XPMultiplier),
    # Shuffles
    ("monster_shuffle",        MonsterShuffle),
    ("boss_shuffle",           BossShuffle),
    ("entrance_shuffle",       EntranceShuffle),
    # Filler
    ("traps_enabled",          TrapsEnabled),
    # Bonus check categories (1.9.0 — opt-in)
    ("check_shrines",          CheckShrines),
    ("check_urns",             CheckUrns),
    ("check_barrels",          CheckBarrels),
    ("check_chests",           CheckChests),
    ("check_set_pickups",      CheckSetPickups),
    ("check_gold_milestones",  CheckGoldMilestones),
    # Extra check categories (1.9.2 — opt-in)
    ("check_cow_level",         CheckCowLevel),
    ("check_merc_milestones",   CheckMercMilestones),
    ("check_hellforge_runes",   CheckHellforgeRunes),
    ("check_npc_dialogue",      CheckNpcDialogue),
    ("check_runeword_crafting", CheckRunewordCrafting),
    ("check_cube_recipes",      CheckCubeRecipes),
    # DeathLink
    ("death_link",             DeathLink),
]

# 32 sets — append to fields list
_FIELDS += [(field, cls) for (field, _disp), (_cn, cls)
            in zip(_COLL_SETS, _COLL_SET_CLASSES)]
# 33 runes
_FIELDS += [(f"collect_rune_{name}", cls)
            for name, (_cn, cls) in zip(_COLL_RUNES, _COLL_RUNE_CLASSES)]
# 10 specials
_FIELDS += [(field, cls) for (field, _disp), (_cn, cls)
            in zip(_COLL_SPECIALS, _COLL_SPECIAL_CLASSES)]

Diablo2ArchipelagoOptions = make_dataclass(
    'Diablo2ArchipelagoOptions',
    _FIELDS,
    bases=(PerGameCommonOptions,),
)


# ============================================================
# 1.9.2 — Option Groups for the Archipelago Options Creator UI
#
# Groups the ~150 options into 13 logical categories so the
# Options Creator can render them as expandable sections instead
# of one giant flat list. Long-tail Collection groups (32 sets +
# 33 runes + 10 specials) are collapsed-by-default to keep the
# panel compact for the average user.
#
# Imported and bound to Diablo2ArchipelagoWorld.option_groups
# in __init__.py so the World class controls the rendering.
# ============================================================

OPTION_GROUPS = [
    OptionGroup("Game Mode", [
        SkillHunting,
        ZoneLocking,
    ]),
    OptionGroup("Goal & Win Condition", [
        Goal,
        CollectionTargetGems,
        CollectionGoldTarget,
    ]),
    OptionGroup("Quest Categories", [
        QuestHunting,
        QuestKillZones,
        QuestExploration,
        QuestWaypoints,
        QuestLevelMilestones,
    ]),
    OptionGroup("Skill Class Filter", [
        SkillClassFilter,
        IncludeAmazon,
        IncludeSorceress,
        IncludeNecromancer,
        IncludePaladin,
        IncludeBarbarian,
        IncludeDruid,
        IncludeAssassin,
    ]),
    OptionGroup("Difficulty & XP", [
        XPMultiplier,
    ]),
    OptionGroup("Shuffles", [
        MonsterShuffle,
        BossShuffle,
        EntranceShuffle,
    ]),
    OptionGroup("Filler Items", [
        TrapsEnabled,
    ]),
    OptionGroup("Bonus Checks (1.9.0 — opt-in)", [
        CheckShrines,
        CheckUrns,
        CheckBarrels,
        CheckChests,
        CheckSetPickups,
        CheckGoldMilestones,
    ]),
    OptionGroup("Extra Checks (1.9.2 — opt-in)", [
        CheckCowLevel,
        CheckMercMilestones,
        CheckHellforgeRunes,
        CheckNpcDialogue,
        CheckRunewordCrafting,
        CheckCubeRecipes,
    ]),
    OptionGroup("Collection — Sets (32 individual toggles)",
                _COLL_SET_CLASSES,
                start_collapsed=True),
    OptionGroup("Collection — Runes (33 individual toggles)",
                _COLL_RUNE_CLASSES,
                start_collapsed=True),
    OptionGroup("Collection — Specials (10 individual toggles)",
                _COLL_SPECIAL_CLASSES,
                start_collapsed=True),
    OptionGroup("Multiworld", [
        DeathLink,
    ]),
]
