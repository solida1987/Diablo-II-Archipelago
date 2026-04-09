"""
Diablo II Archipelago - World Definition

Randomizes skill unlocks across Diablo II's quest system.
Up to 210 skills (7 classes x 30) are shuffled into quest reward locations
spanning up to 5 acts across up to 3 difficulties.
"""
from typing import Any

from BaseClasses import ItemClassification, Tutorial
from worlds.AutoWorld import World, WebWorld

from .items import (
    item_table, FILLER_ITEMS, ALL_SKILL_ITEMS, ALL_SKILL_ITEMS_WITH_TRAPS,
    ITEM_BASE, CLASS_SKILLS, ASSASSIN_TRAP_SKILLS, ZONE_KEY_ITEMS,
)
from .locations import (
    location_table, ALL_ACT_LOCATIONS, GOAL_QUEST_IDS, LOCATION_BASE,
    LEVEL_MILESTONES_NORMAL, LEVEL_MILESTONES_NIGHTMARE, LEVEL_MILESTONES_HELL,
)
from .options import Diablo2ArchipelagoOptions
from .regions import create_regions


class Diablo2ArchipelagoWebWorld(WebWorld):
    theme = "dirt"
    tutorials = [
        Tutorial(
            "Diablo II Archipelago Setup Guide",
            "A guide to setting up Diablo II for Archipelago multiworld.",
            "English",
            "setup_en.md",
            "setup/en",
            ["D2Arch Team"],
        )
    ]


class Diablo2ArchipelagoWorld(World):
    """
    Diablo II Archipelago randomizes skill unlocks across the quest system.
    Complete quests to receive skills from any of the 7 character classes.
    Defeat the act boss specified by your Goal Scope option to achieve victory.
    """

    game = "Diablo II Archipelago"
    web = Diablo2ArchipelagoWebWorld()
    options_dataclass = Diablo2ArchipelagoOptions
    options: Diablo2ArchipelagoOptions

    topology_present = True
    data_version = 2

    item_name_to_id = {name: data[0] for name, data in item_table.items()}
    location_name_to_id = location_table.copy()

    # Quest type → option toggle mapping
    QUEST_TYPE_OPTIONS = {
        "story": "quest_story",
        "hunt": "quest_hunting",
        "kill": "quest_kill_zones",
        "area": "quest_exploration",
        "waypoint": "quest_waypoints",
        "level": "quest_level_milestones",
    }

    def create_item(self, name: str) -> "Diablo2ArchipelagoItem":
        ap_id, classification = item_table[name]
        return Diablo2ArchipelagoItem(name, classification, ap_id, self.player)

    def create_location(self, name: str, ap_id: int, region=None) -> "Diablo2ArchipelagoLocation":
        return Diablo2ArchipelagoLocation(self.player, name, ap_id, region)

    def get_active_locations(self) -> list:
        """Get all locations that are active based on options (combined goal + quest toggles)."""
        goal = self.options.goal.value  # 0-14, encodes act + difficulty
        max_act = (goal // 3) + 1      # 1-5
        num_difficulties = (goal % 3) + 1  # 1-3
        goal_scope = goal // 3          # 0-4 (for GOAL_QUEST_IDS lookup)

        # Always include the goal quest regardless of toggles
        goal_quest_id = GOAL_QUEST_IDS[goal_scope]

        active = []
        for act_idx in range(min(max_act, 5)):
            for quest_id, name, quest_type, classification in ALL_ACT_LOCATIONS[act_idx]:
                # Always include the goal quest (victory condition needs it)
                is_goal = (quest_id == goal_quest_id)
                # Check if this quest type is enabled
                option_name = self.QUEST_TYPE_OPTIONS.get(quest_type)
                if not is_goal and option_name and hasattr(self.options, option_name):
                    if not getattr(self.options, option_name).value:
                        continue  # This quest type is toggled OFF

                # Add for each difficulty
                for diff in range(num_difficulties):
                    if diff == 0:
                        loc_name = name
                        loc_id = LOCATION_BASE + quest_id
                    else:
                        diff_names = ["", " (Nightmare)", " (Hell)"]
                        loc_name = name + diff_names[diff]
                        loc_id = LOCATION_BASE + quest_id + (diff * 1000)  # offset per difficulty
                    active.append((quest_id, loc_name, quest_type, classification, loc_id, diff))

        # Add global level milestones (per difficulty, scaled to max_act)
        if hasattr(self.options, 'quest_level_milestones') and self.options.quest_level_milestones.value:
            # Normal milestones
            for quest_id, name, max_acts_needed, level in LEVEL_MILESTONES_NORMAL:
                if max_act >= max_acts_needed:
                    active.append((quest_id, name, "level", ItemClassification.progression, LOCATION_BASE + quest_id, 0))
            # Nightmare milestones
            if num_difficulties >= 2:
                for quest_id, name, max_acts_needed, level in LEVEL_MILESTONES_NIGHTMARE:
                    if max_act >= max_acts_needed:
                        loc_name = name + " (Nightmare)"
                        active.append((quest_id, loc_name, "level", ItemClassification.progression, LOCATION_BASE + quest_id + 1000, 1))
            # Hell milestones
            if num_difficulties >= 3:
                for quest_id, name, max_acts_needed, level in LEVEL_MILESTONES_HELL:
                    if max_act >= max_acts_needed:
                        loc_name = name + " (Hell)"
                        active.append((quest_id, loc_name, "level", ItemClassification.progression, LOCATION_BASE + quest_id + 2000, 2))

        return active

    def create_regions(self) -> None:
        create_regions(self)

    def create_items(self) -> None:
        active_locations = self.get_active_locations()
        location_count = len(active_locations)
        game_mode = self.options.game_mode.value  # 0=Skill Hunt, 1=Zone Explorer

        # --- Zone Explorer: add zone keys as progression items ---
        zone_keys_in_pool = []
        if game_mode == 1:
            max_act = (self.options.goal.value // 3) + 1
            for ap_id, name, act, classification in ZONE_KEY_ITEMS:
                if act <= max_act:
                    zone_keys_in_pool.append((ap_id, name, classification))
                    self.multiworld.itempool.append(self.create_item(name))

        # --- Build skill pool based on class filter ---
        if self.options.skill_class_filter.value == 1:  # Custom class filter
            # Build pool from selected classes only
            available_skills = []
            class_toggles = {
                "amazon":      self.options.include_amazon.value,
                "sorceress":   self.options.include_sorceress.value,
                "necromancer": self.options.include_necromancer.value,
                "paladin":     self.options.include_paladin.value,
                "barbarian":   self.options.include_barbarian.value,
                "druid":       self.options.include_druid.value,
                "assassin":    self.options.include_assassin.value,
            }
            for cls_name, enabled in class_toggles.items():
                if enabled:
                    available_skills.extend(CLASS_SKILLS[cls_name])

            # Add Assassin trap skills ONLY if "I Play Assassin" is ON
            if self.options.i_play_assassin.value and self.options.include_assassin.value:
                available_skills.extend(ASSASSIN_TRAP_SKILLS)

            # If nothing selected, fall back to all skills
            if not available_skills:
                available_skills = list(ALL_SKILL_ITEMS)

            pool_size = len(available_skills)
        else:
            # All classes mode — use pool size option
            available_skills = list(ALL_SKILL_ITEMS)
            # Add trap skills if "I Play Assassin" is ON
            if self.options.i_play_assassin.value:
                available_skills = list(ALL_SKILL_ITEMS_WITH_TRAPS)
            pool_size = min(self.options.skill_pool_size.value, len(available_skills))

        starting = self.options.starting_skills.value

        # Cap pool so total items never exceeds location_count
        # Total items = (pool_size - starting) skills + zone_keys + filler
        # We need: (pool_size - starting) + len(zone_keys_in_pool) <= location_count
        max_skills_in_pool = location_count - len(zone_keys_in_pool)
        if max_skills_in_pool < 0:
            max_skills_in_pool = 0
        if pool_size - starting > max_skills_in_pool:
            pool_size = max_skills_in_pool + starting
            if pool_size < starting:
                pool_size = starting

        # Shuffle skills for tier distribution (use filtered pool)
        skill_list = list(available_skills)
        self.random.shuffle(skill_list)

        # Separate by tier for smart distribution
        t1_skills = [s for s in skill_list if s[2] == ItemClassification.progression]
        t2_skills = [s for s in skill_list if s[2] == ItemClassification.useful]

        # Build ordered pool: T1 first (early acts), then T2 (later acts)
        ordered_pool = t1_skills + t2_skills
        ordered_pool = ordered_pool[:pool_size]

        # Create skill items
        # In Zone Explorer mode, skills are "useful" not "progression"
        skill_items = []
        for d2_id, name, classification in ordered_pool:
            item = self.create_item(name)
            if game_mode == 1:
                item.classification = ItemClassification.useful
            skill_items.append(item)

        # Pre-place starting skills as "start inventory" so player has them immediately
        # (first N skills go to start, rest go to item pool)
        for i, item in enumerate(skill_items):
            if i < starting:
                self.multiworld.push_precollected(item)
            else:
                self.multiworld.itempool.append(item)

        # --- Filler items ---
        items_in_pool = len(skill_items) - starting  # skills actually in the pool (not precollected)
        items_in_pool += len(zone_keys_in_pool)  # zone keys also occupy pool slots
        filler_needed = location_count - items_in_pool
        if filler_needed < 0:
            filler_needed = 0  # More items than locations — AP will handle overflow

        if filler_needed > 0:
            self._create_filler_items(filler_needed)

    def _create_filler_items(self, count: int) -> None:
        """Create filler items with normalized percentage distribution."""
        # Read weights from options
        weights = {
            "Gold Bundle (Small)": self.options.filler_gold_pct.value,
            "Gold Bundle (Medium)": self.options.filler_gold_pct.value,
            "Gold Bundle (Large)": self.options.filler_gold_pct.value,
            "5 Stat Points": self.options.filler_stat_pts_pct.value,
            "Skill Point": self.options.filler_skill_pts_pct.value,
            "Trap": self.options.filler_trap_pct.value,
            "Reset Point": self.options.filler_reset_pts_pct.value,
        }

        # Gold gets split across 3 tiers, so divide weight by 3
        gold_w = self.options.filler_gold_pct.value
        weights["Gold Bundle (Small)"] = max(1, gold_w // 3)
        weights["Gold Bundle (Medium)"] = max(1, gold_w // 3)
        weights["Gold Bundle (Large)"] = max(1, gold_w // 3)

        # Normalize: if all zero, distribute evenly
        total_weight = sum(weights.values())
        if total_weight == 0:
            for k in weights:
                weights[k] = 1
            total_weight = len(weights)

        # Calculate counts per type
        filler_items = []
        remaining = count
        sorted_types = sorted(weights.keys(), key=lambda k: weights[k], reverse=True)

        for i, filler_name in enumerate(sorted_types):
            if i == len(sorted_types) - 1:
                # Last type gets all remaining to avoid rounding issues
                filler_count = remaining
            else:
                filler_count = round(count * weights[filler_name] / total_weight)
                filler_count = min(filler_count, remaining)

            for _ in range(filler_count):
                self.multiworld.itempool.append(self.create_item(filler_name))
            remaining -= filler_count

    def set_rules(self) -> None:
        """Set the victory condition based on Goal (combined act + difficulty)."""
        goal = self.options.goal.value
        goal_scope = goal // 3  # 0-4
        goal_diff = goal % 3    # 0=Normal, 1=NM, 2=Hell
        goal_quest_id = GOAL_QUEST_IDS[goal_scope]

        # Find the victory location name (with difficulty suffix if needed)
        goal_loc_name = None
        for act_locs in ALL_ACT_LOCATIONS:
            for quest_id, name, quest_type, classification in act_locs:
                if quest_id == goal_quest_id:
                    if goal_diff == 0:
                        goal_loc_name = name  # Normal: no suffix
                    elif goal_diff == 1:
                        goal_loc_name = name + " (Nightmare)"
                    else:
                        goal_loc_name = name + " (Hell)"
                    break
            if goal_loc_name:
                break

        if goal_loc_name:
            self.multiworld.completion_condition[self.player] = (
                lambda state, loc=goal_loc_name, p=self.player: (
                    state.can_reach_location(loc, p)
                )
            )

    def fill_slot_data(self) -> dict[str, Any]:
        """Data sent to the client/bridge. Bridge writes this to ap_settings.dat for the DLL."""
        return {
            "game_mode": self.options.game_mode.value,
            "goal": self.options.goal.value,
            "starting_skills": self.options.starting_skills.value,
            "skill_pool_size": self.options.skill_pool_size.value,
            "death_link": self.options.death_link.value,
            # Quest toggles
            "quest_story": self.options.quest_story.value,
            "quest_hunting": self.options.quest_hunting.value,
            "quest_kill_zones": self.options.quest_kill_zones.value,
            "quest_exploration": self.options.quest_exploration.value,
            "quest_waypoints": self.options.quest_waypoints.value,
            "quest_level_milestones": self.options.quest_level_milestones.value,
            # Filler weights
            "filler_gold_pct": self.options.filler_gold_pct.value,
            "filler_stat_pts_pct": self.options.filler_stat_pts_pct.value,
            "filler_skill_pts_pct": self.options.filler_skill_pts_pct.value,
            "filler_trap_pct": self.options.filler_trap_pct.value,
            "filler_reset_pts_pct": self.options.filler_reset_pts_pct.value,
            # Monster shuffle
            "monster_shuffle": self.options.monster_shuffle.value,
            "boss_shuffle": self.options.boss_shuffle.value,
            "shop_shuffle": self.options.shop_shuffle.value,
            # Treasure Cows
            "treasure_cows": self.options.treasure_cows.value,
            # Class filter
            "i_play_assassin": self.options.i_play_assassin.value,
            # Meta
            "seed": self.multiworld.seed,
            "player_name": self.multiworld.get_player_name(self.player),
        }


from BaseClasses import Item, Location


class Diablo2ArchipelagoItem(Item):
    game = "Diablo II Archipelago"


class Diablo2ArchipelagoLocation(Location):
    game = "Diablo II Archipelago"
