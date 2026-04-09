"""
Diablo II Archipelago - World Options
All settings configurable via YAML for AP multiworld generation.
"""
from dataclasses import dataclass

from Options import Choice, Range, Toggle, DeathLink, PerGameCommonOptions


# ============================================================
# Goal & Scope
# ============================================================

class GameMode(Choice):
    """
    Determines the progression system.

    Skill Hunt: Skills are progression items. All zones are open from the start.
    Zone Explorer: Zone Keys are progression items. Zones must be unlocked.
      Skills become useful items (given more frequently).
    """
    display_name = "Game Mode"
    option_skill_hunt = 0
    option_zone_explorer = 1
    default = 0


class Goal(Choice):
    """
    Determines which act boss must be defeated and on which difficulty.
    This controls both how many acts are playable AND how many difficulties.

    Example: "Acts 1-2 Nightmare" means play Acts 1-2 on Normal AND Nightmare,
    and defeat Duriel on Nightmare to win.
    """
    display_name = "Goal"
    option_act_1_normal = 0
    option_act_1_nightmare = 1
    option_act_1_hell = 2
    option_acts_1_2_normal = 3
    option_acts_1_2_nightmare = 4
    option_acts_1_2_hell = 5
    option_acts_1_3_normal = 6
    option_acts_1_3_nightmare = 7
    option_acts_1_3_hell = 8
    option_acts_1_4_normal = 9
    option_acts_1_4_nightmare = 10
    option_acts_1_4_hell = 11
    option_full_game_normal = 12
    option_full_game_nightmare = 13
    option_full_game_hell = 14
    default = 12


# ============================================================
# Quest Type Toggles (which quest types generate locations)
# ============================================================

class QuestStory(Toggle):
    """Include story quests (Den of Evil, Andariel, etc). Always recommended ON."""
    display_name = "Story Quests"
    default = True


class QuestHunting(Toggle):
    """Include Super Unique hunting quests (Kill Corpsefire, Kill Rakanishu, etc)."""
    display_name = "Hunting Quests"
    default = True


class QuestKillZones(Toggle):
    """Include zone clear quests (Kill 25 monsters in Blood Moor, etc)."""
    display_name = "Kill Zone Quests"
    default = True


class QuestExploration(Toggle):
    """Include area entry quests (Enter Blood Moor, Enter Tristram, etc)."""
    display_name = "Exploration Quests"
    default = True


class QuestWaypoints(Toggle):
    """Include waypoint activation quests."""
    display_name = "Waypoint Quests"
    default = True


class QuestLevelMilestones(Toggle):
    """Include level milestone quests (Reach Level 5, 10, 15... 50)."""
    display_name = "Level Milestone Quests"
    default = True


# ============================================================
# Skill Pool
# ============================================================

class SkillPoolSize(Range):
    """
    How many skills are in the item pool as progression/useful items.
    Only used when no specific classes are selected.
    Lower = more filler items. Higher = more skills to find.
    """
    display_name = "Skill Pool Size"
    range_start = 20
    range_end = 210
    default = 210


class StartingSkills(Range):
    """Number of random skills the player starts with already unlocked."""
    display_name = "Starting Skills"
    range_start = 1
    range_end = 20
    default = 6


class SkillClassFilter(Choice):
    """
    Filter which class skills are included in the pool.

    All Classes: All 7 classes (default, uses Skill Pool Size).
    Custom: Only selected classes (use the class toggle options below).
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
    NOTE: Assassin trap skills are only included if 'I Play Assassin' is also ON."""
    display_name = "Include Assassin"
    default = True

class IPlayAssassin(Toggle):
    """Enable this if you are playing as an Assassin.
    This unlocks Assassin trap skills (Fire Blast, Wake of Fire, etc.)
    which only work on Assassin characters. Without this, trap skills
    are excluded to prevent invisible character bugs."""
    display_name = "I Play Assassin"
    default = False


# ============================================================
# Filler Distribution (percentages, auto-normalized to 100%)
# ============================================================

class FillerGoldPct(Range):
    """Relative weight for Gold filler items. Set to 0 to disable gold rewards."""
    display_name = "Filler: Gold Weight"
    range_start = 0
    range_end = 100
    default = 30


class FillerStatPtsPct(Range):
    """Relative weight for Stat Point filler items."""
    display_name = "Filler: Stat Points Weight"
    range_start = 0
    range_end = 100
    default = 15


class FillerSkillPtsPct(Range):
    """Relative weight for Skill Point filler items."""
    display_name = "Filler: Skill Points Weight"
    range_start = 0
    range_end = 100
    default = 15


class FillerTrapPct(Range):
    """
    Relative weight for Trap filler items.
    Traps spawn dangerous Super Unique monsters near the player.
    """
    display_name = "Filler: Trap Weight"
    range_start = 0
    range_end = 100
    default = 15


class FillerResetPtsPct(Range):
    """Relative weight for Reset Point filler items (used to remove assigned skills)."""
    display_name = "Filler: Reset Points Weight"
    range_start = 0
    range_end = 100
    default = 25


# ============================================================
# Starting Resources
# ============================================================

## StartingGold REMOVED — gold is earned through gameplay, not given at start


# ============================================================
# Monster Shuffle
# ============================================================

class MonsterShuffle(Toggle):
    """Shuffle all monster types across all areas. Act 5 monsters can appear in Act 1 etc.
    Stats are scaled to match area difficulty. Automatically disables Hunting Quests."""
    display_name = "Monster Shuffle"
    default = False


class BossShuffle(Toggle):
    """Shuffle all SuperUnique bosses across all areas."""
    display_name = "Boss Shuffle"
    default = False


class ShopShuffle(Toggle):
    """Shuffle vendor inventories. Each vendor sells randomized items from all acts."""
    display_name = "Shop Shuffle"
    default = False


class TreasureCows(Toggle):
    """Enable Treasure Cows - special SuperUnique cow monsters placed across all acts.
    Each one drops valuable loot and counts as a location check when killed.
    28 Treasure Cows spread across Acts 1-5."""
    display_name = "Treasure Cows"
    default = True


# ============================================================
# DeathLink
# ============================================================

# Uses the built-in DeathLink from Options module
# When ON: your death sends to others, others' deaths spawn a trap on you


# ============================================================
# Combined Options Dataclass
# ============================================================

@dataclass
class Diablo2ArchipelagoOptions(PerGameCommonOptions):
    # Game Mode
    game_mode: GameMode
    # Goal (combined act + difficulty)
    goal: Goal
    # Quest toggles
    quest_story: QuestStory
    quest_hunting: QuestHunting
    quest_kill_zones: QuestKillZones
    quest_exploration: QuestExploration
    quest_waypoints: QuestWaypoints
    quest_level_milestones: QuestLevelMilestones
    # Pool
    skill_pool_size: SkillPoolSize
    starting_skills: StartingSkills
    skill_class_filter: SkillClassFilter
    include_amazon: IncludeAmazon
    include_sorceress: IncludeSorceress
    include_necromancer: IncludeNecromancer
    include_paladin: IncludePaladin
    include_barbarian: IncludeBarbarian
    include_druid: IncludeDruid
    include_assassin: IncludeAssassin
    i_play_assassin: IPlayAssassin
    # Filler distribution
    filler_gold_pct: FillerGoldPct
    filler_stat_pts_pct: FillerStatPtsPct
    filler_skill_pts_pct: FillerSkillPtsPct
    filler_trap_pct: FillerTrapPct
    filler_reset_pts_pct: FillerResetPtsPct
    # Monster Shuffle
    monster_shuffle: MonsterShuffle
    boss_shuffle: BossShuffle
    shop_shuffle: ShopShuffle
    treasure_cows: TreasureCows
    # DeathLink
    death_link: DeathLink
