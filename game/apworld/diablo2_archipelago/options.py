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

from Options import Choice, Range, Toggle, DeathLink, PerGameCommonOptions


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
    Determines which difficulty you must complete to win.

    Full Normal:    beat Baal on Normal.
    Full Nightmare: beat Baal on Normal AND Nightmare.
    Full Hell:      beat Baal on Normal, Nightmare, AND Hell.
    """
    display_name = "Goal"
    option_full_normal = 0
    option_full_nightmare = 1
    option_full_hell = 2
    default = 0


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


# ============================================================
# Combined Options Dataclass
# ============================================================

@dataclass
class Diablo2ArchipelagoOptions(PerGameCommonOptions):
    # Mode toggles
    skill_hunting: SkillHunting
    zone_locking: ZoneLocking
    # Goal
    goal: Goal
    # Quest toggles (story is always ON internally — engine-required)
    quest_hunting: QuestHunting
    quest_kill_zones: QuestKillZones
    quest_exploration: QuestExploration
    quest_waypoints: QuestWaypoints
    quest_level_milestones: QuestLevelMilestones
    # Class filter
    skill_class_filter: SkillClassFilter
    include_amazon: IncludeAmazon
    include_sorceress: IncludeSorceress
    include_necromancer: IncludeNecromancer
    include_paladin: IncludePaladin
    include_barbarian: IncludeBarbarian
    include_druid: IncludeDruid
    include_assassin: IncludeAssassin
    # XP
    xp_multiplier: XPMultiplier
    # Shuffles (shop_shuffle removed — no logic implemented in DLL)
    monster_shuffle: MonsterShuffle
    boss_shuffle: BossShuffle
    # DeathLink
    death_link: DeathLink
