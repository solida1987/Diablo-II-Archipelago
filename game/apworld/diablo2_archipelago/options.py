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

from Options import Choice, Range, Toggle, DeathLink, PerGameCommonOptions, OptionGroup, OptionSet


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

    Full Normal:     beat Baal on Normal.
    Full Nightmare:  beat Baal on Normal AND Nightmare.
    Full Hell:       beat Baal on Normal, Nightmare, AND Hell.
    Gold Collection: simple lifetime-gold goal. Win when your
                     character's lifetime gold reaches the value set
                     in 'Collection: Gold Target'. No other constraints.
    Custom:          build your own win condition with checkboxes.
                     Toggle on any combination of the 50+ Custom Goal
                     options below (subsystems, act bosses, specific
                     kills, bulk checks) and an optional
                     custom_goal_gold_target. Goal completes when ALL
                     selected items are achieved AND lifetime gold
                     reaches the gold target. AP-only — standalone
                     defaults to Full Normal.
    """
    display_name = "Goal"
    option_full_normal = 0
    option_full_nightmare = 1
    option_full_hell = 2
    option_gold_collection = 3
    option_custom = 4
    default = 0
    # 1.9.2: 'collection' alias keeps older YAMLs working — was the
    # original 1.9.0 name for value 3, now relabelled gold_collection
    # since the F1 Collection book is no longer a hard requirement
    # (gold target is the only criterion).
    aliases = {"collection": 3}


# ============================================================
# Custom Goal sub-targets (only meaningful when Goal=Custom).
# AP-side only — standalone players can't access these via the
# title-screen UI and the DLL falls back to Full Normal if
# goal=custom is loaded without target data.
# ============================================================

class CustomGoalGoldTarget(Range):
    """[Goal=Custom only] Optional lifetime-gold threshold added on
    top of every other selected Custom Goal target. 0 = no gold
    requirement. Same monotonic lifetime gold counter the Gold
    Collection goal uses (pickup + quest rewards, excludes vendor
    sales)."""
    display_name = "Custom Goal: Gold Target"
    range_start = 0
    range_end = 100000000
    default = 0


# ============================================================
# Custom Goal toggles — built via factory so the 50+ individual
# Toggle classes don't bloat this file with manual decls. Each is
# a standalone Toggle the player ticks ON/OFF in the Options
# Creator. CSV is built from the on-toggles in fill_slot_data and
# sent to the DLL as `custom_goal_targets_csv` (DLL parser unchanged).
# ============================================================

def _make_custom_goal_toggle(field_name, display, doc):
    """Factory — produces a fresh Toggle subclass with given attrs."""
    cls = type(
        f"CustomGoal_{field_name}",
        (Toggle,),
        {
            "__doc__": doc,
            "display_name": display,
            "default": False,
        }
    )
    return cls


# (csv_token, field_name, display_name, docstring) tuples — order
# defines OPTION_GROUPS ordering and CSV emission ordering.
_CUSTOM_GOAL_DEFS = [
    # ============= A. BULK SUBSYSTEM INCLUDES =============
    ("subsystem_skill_hunting",     "custom_goal_subsystem_skill_hunting",
     "[Custom] Subsystem: Skill Hunting",
     "[Goal=Custom] Win requires unlocking every skill in your seeded skill pool (up to 210)."),
    ("subsystem_collection",        "custom_goal_subsystem_collection",
     "[Custom] Subsystem: Collection",
     "[Goal=Custom] Win requires the F1 Collection book to be complete according to the existing Collection — Sets/Runes/Gems/Specials toggles. Mix of items required is configured in those collection groups."),
    ("subsystem_hunt_quests",       "custom_goal_subsystem_hunt_quests",
     "[Custom] Subsystem: All Hunting Quests",
     "[Goal=Custom] Win requires all 'Hunt: <SuperUnique>' quests across all 3 difficulties (~42 quests)."),
    ("subsystem_kill_zone_quests",  "custom_goal_subsystem_kill_zone_quests",
     "[Custom] Subsystem: All Kill-Zone Quests",
     "[Goal=Custom] Win requires all 'Kill all monsters in <area>' quests across all 3 difficulties."),
    ("subsystem_exploration_quests","custom_goal_subsystem_exploration_quests",
     "[Custom] Subsystem: All Exploration Quests",
     "[Goal=Custom] Win requires all 'Reach <area>' exploration quests across all 3 difficulties."),
    ("subsystem_waypoints",         "custom_goal_subsystem_waypoints",
     "[Custom] Subsystem: All Waypoints",
     "[Goal=Custom] Win requires activating every waypoint across all 3 difficulties (~38 waypoints × 3 = 114)."),
    ("subsystem_level_milestones",  "custom_goal_subsystem_level_milestones",
     "[Custom] Subsystem: All Level Milestones",
     "[Goal=Custom] Win requires reaching every level milestone (5/10/15/.../99) across all 3 difficulties."),
    ("subsystem_story_normal",      "custom_goal_subsystem_story_normal",
     "[Custom] Subsystem: All Story Quests Normal",
     "[Goal=Custom] Win requires every story quest (Den of Evil, Sisters Burial, ..., Eve of Destruction) on Normal."),
    ("subsystem_story_nightmare",   "custom_goal_subsystem_story_nightmare",
     "[Custom] Subsystem: All Story Quests Nightmare",
     "[Goal=Custom] Win requires every story quest on Nightmare."),
    ("subsystem_story_hell",        "custom_goal_subsystem_story_hell",
     "[Custom] Subsystem: All Story Quests Hell",
     "[Goal=Custom] Win requires every story quest on Hell."),

    # ============= B. ACT BOSSES × DIFFICULTY (15) =============
    ("kill_andariel_normal",    "custom_goal_kill_andariel_normal",    "[Custom] Kill: Andariel (Normal)",    "[Goal=Custom] Win requires killing Andariel on Normal."),
    ("kill_andariel_nightmare", "custom_goal_kill_andariel_nightmare", "[Custom] Kill: Andariel (Nightmare)", "[Goal=Custom] Win requires killing Andariel on Nightmare."),
    ("kill_andariel_hell",      "custom_goal_kill_andariel_hell",      "[Custom] Kill: Andariel (Hell)",      "[Goal=Custom] Win requires killing Andariel on Hell."),
    ("kill_duriel_normal",      "custom_goal_kill_duriel_normal",      "[Custom] Kill: Duriel (Normal)",      "[Goal=Custom] Win requires killing Duriel on Normal."),
    ("kill_duriel_nightmare",   "custom_goal_kill_duriel_nightmare",   "[Custom] Kill: Duriel (Nightmare)",   "[Goal=Custom] Win requires killing Duriel on Nightmare."),
    ("kill_duriel_hell",        "custom_goal_kill_duriel_hell",        "[Custom] Kill: Duriel (Hell)",        "[Goal=Custom] Win requires killing Duriel on Hell."),
    ("kill_mephisto_normal",    "custom_goal_kill_mephisto_normal",    "[Custom] Kill: Mephisto (Normal)",    "[Goal=Custom] Win requires killing Mephisto on Normal."),
    ("kill_mephisto_nightmare", "custom_goal_kill_mephisto_nightmare", "[Custom] Kill: Mephisto (Nightmare)", "[Goal=Custom] Win requires killing Mephisto on Nightmare."),
    ("kill_mephisto_hell",      "custom_goal_kill_mephisto_hell",      "[Custom] Kill: Mephisto (Hell)",      "[Goal=Custom] Win requires killing Mephisto on Hell."),
    ("kill_diablo_normal",      "custom_goal_kill_diablo_normal",      "[Custom] Kill: Diablo (Normal)",      "[Goal=Custom] Win requires killing Diablo on Normal."),
    ("kill_diablo_nightmare",   "custom_goal_kill_diablo_nightmare",   "[Custom] Kill: Diablo (Nightmare)",   "[Goal=Custom] Win requires killing Diablo on Nightmare."),
    ("kill_diablo_hell",        "custom_goal_kill_diablo_hell",        "[Custom] Kill: Diablo (Hell)",        "[Goal=Custom] Win requires killing Diablo on Hell."),
    ("kill_baal_normal",        "custom_goal_kill_baal_normal",        "[Custom] Kill: Baal (Normal)",        "[Goal=Custom] Win requires killing Baal on Normal."),
    ("kill_baal_nightmare",     "custom_goal_kill_baal_nightmare",     "[Custom] Kill: Baal (Nightmare)",     "[Goal=Custom] Win requires killing Baal on Nightmare."),
    ("kill_baal_hell",          "custom_goal_kill_baal_hell",          "[Custom] Kill: Baal (Hell)",          "[Goal=Custom] Win requires killing Baal on Hell."),

    # ============= C. COW KING × DIFFICULTY (3) =============
    ("kill_cow_king_normal",    "custom_goal_kill_cow_king_normal",    "[Custom] Kill: Cow King (Normal)",    "[Goal=Custom] Win requires killing The Cow King on Normal in the Moo Moo Farm."),
    ("kill_cow_king_nightmare", "custom_goal_kill_cow_king_nightmare", "[Custom] Kill: Cow King (Nightmare)", "[Goal=Custom] Win requires killing The Cow King on Nightmare."),
    ("kill_cow_king_hell",      "custom_goal_kill_cow_king_hell",      "[Custom] Kill: Cow King (Hell)",      "[Goal=Custom] Win requires killing The Cow King on Hell."),

    # ============= D. PANDEMONIUM EVENT (4) =============
    ("kill_uber_mephisto",      "custom_goal_kill_uber_mephisto",      "[Custom] Kill: Uber Mephisto",        "[Goal=Custom] Win requires killing Uber Mephisto in the Furnace of Pain (Pandemonium event)."),
    ("kill_uber_diablo",        "custom_goal_kill_uber_diablo",        "[Custom] Kill: Uber Diablo",          "[Goal=Custom] Win requires killing Uber Diablo in the Forgotten Sands (Pandemonium event)."),
    ("kill_uber_baal",          "custom_goal_kill_uber_baal",          "[Custom] Kill: Uber Baal",            "[Goal=Custom] Win requires killing Uber Baal in the Matron's Den (Pandemonium event)."),
    ("hellfire_torch_complete", "custom_goal_hellfire_torch_complete", "[Custom] Pandemonium: Full Run",      "[Goal=Custom] Win requires completing one full Pandemonium event (all 3 ubers + Hellfire Torch drop)."),

    # ============= E. FAMOUS SUPER-UNIQUES (10) =============
    ("kill_bishibosh",          "custom_goal_kill_bishibosh",          "[Custom] Kill: Bishibosh",            "[Goal=Custom] Win requires killing Bishibosh (Cold Plains super-unique)."),
    ("kill_corpsefire",         "custom_goal_kill_corpsefire",         "[Custom] Kill: Corpsefire",           "[Goal=Custom] Win requires killing Corpsefire (Den of Evil super-unique)."),
    ("kill_rakanishu",          "custom_goal_kill_rakanishu",          "[Custom] Kill: Rakanishu",            "[Goal=Custom] Win requires killing Rakanishu (Stony Field super-unique)."),
    ("kill_griswold",           "custom_goal_kill_griswold",           "[Custom] Kill: Griswold",             "[Goal=Custom] Win requires killing Griswold (Tristram super-unique)."),
    ("kill_pindleskin",         "custom_goal_kill_pindleskin",         "[Custom] Kill: Pindleskin",           "[Goal=Custom] Win requires killing Pindleskin (Nihlathak's Temple super-unique)."),
    ("kill_nihlathak_su",       "custom_goal_kill_nihlathak_su",       "[Custom] Kill: Nihlathak (boss)",     "[Goal=Custom] Win requires killing Nihlathak the boss (Halls of Vaught)."),
    ("kill_summoner",           "custom_goal_kill_summoner",           "[Custom] Kill: The Summoner",         "[Goal=Custom] Win requires killing The Summoner (Arcane Sanctuary)."),
    ("kill_radament",           "custom_goal_kill_radament",           "[Custom] Kill: Radament",             "[Goal=Custom] Win requires killing Radament (Sewers Level 3)."),
    ("kill_izual",              "custom_goal_kill_izual",              "[Custom] Kill: Izual",                "[Goal=Custom] Win requires killing Izual (Plains of Despair)."),
    ("kill_council",            "custom_goal_kill_council",            "[Custom] Kill: Council Member",       "[Goal=Custom] Win requires killing one of the High Council members (Travincal)."),

    # ============= F. BULK BONUS CHECKS (6) =============
    ("all_shrines",             "custom_goal_all_shrines",             "[Custom] All Shrines fired",          "[Goal=Custom] Win requires firing every shrine bonus check (50/diff × 3 diff = 150). Requires check_shrines=true."),
    ("all_urns",                "custom_goal_all_urns",                "[Custom] All Urns fired",             "[Goal=Custom] Win requires firing every urn bonus check (100/diff × 3 = 300). Requires check_urns=true."),
    ("all_barrels",             "custom_goal_all_barrels",             "[Custom] All Barrels fired",          "[Goal=Custom] Win requires firing every barrel bonus check. Requires check_barrels=true."),
    ("all_chests",              "custom_goal_all_chests",              "[Custom] All Chests fired",           "[Goal=Custom] Win requires firing every chest bonus check. Requires check_chests=true."),
    ("all_set_pickups",         "custom_goal_all_set_pickups",         "[Custom] All Set Pickups",            "[Goal=Custom] Win requires picking up every unique set piece (127 total). Requires check_set_pickups=true."),
    ("all_gold_milestones",     "custom_goal_all_gold_milestones",     "[Custom] All Gold Milestones",        "[Goal=Custom] Win requires hitting every lifetime-gold milestone (17 total). Requires check_gold_milestones=true."),

    # ============= G. BULK EXTRA CHECKS (6) =============
    ("all_cow_level_checks",    "custom_goal_all_cow_level_checks",    "[Custom] All Cow Level Checks",       "[Goal=Custom] Win requires firing all 9 cow-level extra checks. Requires check_cow_level=true."),
    ("all_merc_milestones",     "custom_goal_all_merc_milestones",     "[Custom] All Merc Milestones",        "[Goal=Custom] Win requires firing all 6 merc milestone checks. Requires check_merc_milestones=true."),
    ("all_hellforge_runes",     "custom_goal_all_hellforge_runes",     "[Custom] All Hellforge & High Runes", "[Goal=Custom] Win requires firing all 12 Hellforge+High Rune checks. Requires check_hellforge_runes=true."),
    ("all_npc_dialogue",        "custom_goal_all_npc_dialogue",        "[Custom] All NPC Dialogue Checks",    "[Goal=Custom] Win requires firing all 81 NPC dialogue checks (27 NPCs × 3 diff). Requires check_npc_dialogue=true."),
    ("all_runeword_crafting",   "custom_goal_all_runeword_crafting",   "[Custom] All Runeword Crafting",      "[Goal=Custom] Win requires crafting 50 runewords. Requires check_runeword_crafting=true."),
    ("all_cube_recipes",        "custom_goal_all_cube_recipes",        "[Custom] All Cube Recipes",           "[Goal=Custom] Win requires completing 135 successful cube transmutes. Requires check_cube_recipes=true."),
]

_CUSTOM_GOAL_CLASSES = [
    _make_custom_goal_toggle(field, display, doc)
    for (csv_tok, field, display, doc) in _CUSTOM_GOAL_DEFS
]


# ============================================================
# Collection Goal sub-targets (only meaningful when Goal=Collection)
# ============================================================

class CollectionTargetGems(Toggle):
    """Whether all 35 gems (7 colors × 5 grades, Chipped..Perfect)
    count toward F1 Collection book completion + Custom Goal's
    'include collection' option. Each individual gem pickup still
    fires an AP check when this toggle is ON. Moved out of Goal &
    Win Condition in 1.9.2 — now lives under Collection — Gems."""
    display_name = "Collection: Gems"
    default = True


class CollectionGoldTarget(Range):
    """[Goal=Gold Collection only] Lifetime-gold threshold the
    character must reach to win. Lifetime-gold counter on the F1
    Collection page is monotonic (never decreases) and tracks gold
    pickup + quest rewards (excludes vendor sales). 0 = trivially
    complete (don't pick Gold Collection in that case — pick a real
    Goal instead)."""
    display_name = "Gold Collection: Target"
    range_start = 0
    range_end = 100000000
    default = 1000000


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
    # Custom goal sub-targets (only meaningful when goal=custom)
    ("custom_goal_gold_target", CustomGoalGoldTarget),
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

# 1.9.2 — 54 individual Custom Goal toggles (replaces the old
# CustomGoalTargets OptionSet). Each appears as a dedicated checkbox
# in the Options Creator under the four Custom Goal groups below.
_FIELDS += [(field, cls)
            for (csv_tok, field, _disp, _doc), cls
            in zip(_CUSTOM_GOAL_DEFS, _CUSTOM_GOAL_CLASSES)]

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
        CollectionGoldTarget,
        CustomGoalGoldTarget,
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
    OptionGroup("Bonus Checks", [
        CheckShrines,
        CheckUrns,
        CheckBarrels,
        CheckChests,
        CheckSetPickups,
        CheckGoldMilestones,
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
    OptionGroup("Collection — Gems",
                [CollectionTargetGems],
                start_collapsed=True),
    OptionGroup("Collection — Specials (10 individual toggles)",
                _COLL_SPECIAL_CLASSES,
                start_collapsed=True),
    # 1.9.2 — Custom Goal target groups. The 54 toggles ship as four
    # collapsed-by-default sections so they don't overwhelm the UI for
    # players using a non-Custom Goal. Slice indices match the section
    # boundaries in _CUSTOM_GOAL_DEFS:
    #   [0..10)  = subsystems (10)
    #   [10..25) = act bosses (15)
    #   [25..28) = cow king (3)
    #   [28..32) = pandemonium (4)
    #   [32..42) = super-uniques (10)
    #   [42..48) = bulk bonus checks (6)
    #   [48..54) = bulk extra checks (6)
    OptionGroup("Custom Goal — Subsystems (when Goal=Custom)",
                _CUSTOM_GOAL_CLASSES[0:10],
                start_collapsed=True),
    OptionGroup("Custom Goal — Act Boss Kills (when Goal=Custom)",
                _CUSTOM_GOAL_CLASSES[10:25],
                start_collapsed=True),
    OptionGroup("Custom Goal — Cow King + Pandemonium Ubers (when Goal=Custom)",
                _CUSTOM_GOAL_CLASSES[25:32],
                start_collapsed=True),
    OptionGroup("Custom Goal — Famous Super-Uniques (when Goal=Custom)",
                _CUSTOM_GOAL_CLASSES[32:42],
                start_collapsed=True),
    OptionGroup("Custom Goal — Bulk Object/Check Targets (when Goal=Custom)",
                _CUSTOM_GOAL_CLASSES[42:54],
                start_collapsed=True),
    OptionGroup("Multiworld", [
        DeathLink,
    ]),
]
