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
    GATE_KEY_ITEMS,
)
from .locations import (
    location_table, ALL_ACT_LOCATIONS, GOAL_QUEST_IDS, LOCATION_BASE,
    LEVEL_MILESTONES_NORMAL, LEVEL_MILESTONES_NIGHTMARE, LEVEL_MILESTONES_HELL,
    GATE_LOCATIONS,
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
    # 1.8.0: "story" removed — D2 engine requires main-story quests.
    # Story locations are always generated regardless of toggles.
    QUEST_TYPE_OPTIONS = {
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
        """Get all locations that are active based on options (combined goal + quest toggles).

        1.8.0 — Goal simplified to 3 values (0=Full Normal, 1=Full NM, 2=Full Hell).
        Act scope is ALWAYS full game (all 5 acts); only difficulty scope varies.
        """
        goal = self.options.goal.value  # 0-2 (difficulty scope)
        max_act = 5                     # always full game
        num_difficulties = goal + 1     # 1, 2, or 3

        # Goal quest is always Baal (Eve of Destruction, quest_id 406)
        goal_quest_id = 406

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

        # 1.8.0 NEW — Gate-boss kill locations (only when zone_locking is ON)
        # 18 gates × up to 3 difficulties = 54 locations for Full Hell goal.
        # Always included regardless of quest-type toggles (gate-kill is its
        # own check type, not a "quest").
        if hasattr(self.options, 'zone_locking') and self.options.zone_locking.value:
            for loc_id, loc_name, act, diff, gate_idx in GATE_LOCATIONS:
                if act > max_act:
                    continue
                if diff >= num_difficulties:
                    continue
                # Reuse the tuple format: (quest_id, name, quest_type, classification, loc_id, diff)
                # quest_id not meaningful here; we use a synthetic negative marker.
                # classification progression=true so fill treats as meaningful
                synthetic_quest_id = -(loc_id)  # marker: negative = gate location
                active.append((synthetic_quest_id, loc_name, "gate",
                               ItemClassification.progression, loc_id, diff))

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

    def generate_early(self) -> None:
        """1.8.0 — pick 15 preload IDs (5 acts × 3 difficulties) per slot.
        Baked here so the same seed always produces the same layout.
        Exposed via fill_slot_data; not user-configurable.
        """
        max_preloads = {1: 4, 2: 4, 3: 4, 4: 3, 5: 4}
        self.preloads = {}
        for act in range(1, 6):
            for diff in range(3):
                self.preloads[(act, diff)] = self.random.randint(0, max_preloads[act] - 1)

    def create_items(self) -> None:
        active_locations = self.get_active_locations()
        location_count = len(active_locations)
        skill_hunting = bool(self.options.skill_hunting.value)
        zone_locking  = bool(self.options.zone_locking.value)

        # --- Zone Locking: 18 gate-keys per played difficulty ---
        zone_keys_in_pool = []
        if zone_locking:
            num_difficulties = self.options.goal.value + 1  # 0-2 -> 1-3 diffs
            for ap_id, name, act, classification in GATE_KEY_ITEMS:
                # Determine item's difficulty from AP ID range
                if 46101 <= ap_id <= 46118:
                    item_diff = 0  # Normal
                elif 46121 <= ap_id <= 46138:
                    item_diff = 1  # Nightmare
                elif 46141 <= ap_id <= 46158:
                    item_diff = 2  # Hell
                else:
                    continue
                if item_diff >= num_difficulties:
                    continue  # Not played this difficulty
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

            # 1.8.0: Assassin trap skills always excluded from pool (prevents
            # invisible-character bug on non-Assassin classes). Matches in-game
            # behaviour — the 'I Play Assassin' toggle was removed in 1.8.0.

            # If nothing selected, fall back to all skills
            if not available_skills:
                available_skills = list(ALL_SKILL_ITEMS)

            pool_size = len(available_skills)
        else:
            # All classes mode — full 210-skill pool (trap skills always excluded)
            available_skills = list(ALL_SKILL_ITEMS)
            pool_size = len(available_skills)

        # 1.8.0: starting_skills hardcoded to 6 (matches in-game default).
        # Removed as user-facing option.
        starting = 6

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

        # Create skill items.
        # 1.8.0 classification rules (per user spec 2026-04-24):
        #   Skill Hunting ON  -> skills = USEFUL (not progression)
        #   Zone Locking  ON  -> gate keys = PROGRESSION (already set in GATE_KEY_ITEMS)
        #   Both OFF          -> story quests' rewards stay progression (default in items.py)
        # So skills default to progression in items.py (tier-1), but we
        # downgrade them to useful when skill_hunting is ON to match the spec.
        skill_items = []
        for d2_id, name, classification in ordered_pool:
            item = self.create_item(name)
            if skill_hunting:
                item.classification = ItemClassification.useful
            # Note: if skill_hunting OFF, keep original tier-1/tier-2 classification
            # (progression or useful based on items.py definitions). This lets skills
            # still gate progression in non-skill-hunt modes where they're meaningful.
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
        """Create filler items with normalized percentage distribution.
        1.8.0: filler weights hardcoded to in-game defaults (options removed)."""
        weights = {
            "Gold Bundle (Small)":  10,   # gold_pct=30 split across 3 tiers
            "Gold Bundle (Medium)": 10,
            "Gold Bundle (Large)":  10,
            "5 Stat Points":        15,
            "Skill Point":          15,
            "Trap":                 15,
            "Reset Point":          25,
        }

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
        """Set the victory condition.

        1.8.0 — Goal simplified to 3 values:
          0 = Full Normal    (beat Baal on Normal)
          1 = Full Nightmare (beat Baal on Normal AND Nightmare)
          2 = Full Hell      (beat Baal on Normal, NM, AND Hell)

        Victory = Eve of Destruction on the chosen difficulty.
        """
        goal_diff = self.options.goal.value  # 0/1/2
        goal_quest_id = 406  # Eve of Destruction (Baal)

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

        # Sanity check: goal location must exist in the flat location table.
        # If it doesn't, the quest toggles or difficulty scope are
        # inconsistent — fail loudly rather than silently succeed on any kill.
        if goal_loc_name and goal_loc_name not in location_table:
            raise ValueError(
                f"Goal location '{goal_loc_name}' missing from location_table "
                f"(goal_diff={goal_diff}, quest_id={goal_quest_id}). "
                f"Check locations.py GOAL_QUEST_IDS vs ALL_ACT_LOCATIONS."
            )

        if goal_loc_name:
            self.multiworld.completion_condition[self.player] = (
                lambda state, loc=goal_loc_name, p=self.player: (
                    state.can_reach_location(loc, p)
                )
            )

    def fill_slot_data(self) -> dict[str, Any]:
        """Data sent to the client/bridge. Bridge writes to ap_settings.dat.
        1.8.0 — Simplified: only user-facing options + auto-generated preloads.
        Internal values (skill pool=210, starting=6, filler defaults) are
        hardcoded on the DLL side and not transmitted. """
        return {
            "skill_hunting":     self.options.skill_hunting.value,
            "zone_locking":      self.options.zone_locking.value,
            "goal":              self.options.goal.value,  # 0=Normal, 1=NM, 2=Hell
            "death_link":        self.options.death_link.value,
            # Quest toggles
            "quest_story":            1,  # always ON — engine-required
            "quest_hunting":          self.options.quest_hunting.value,
            "quest_kill_zones":       self.options.quest_kill_zones.value,
            "quest_exploration":      self.options.quest_exploration.value,
            "quest_waypoints":        self.options.quest_waypoints.value,
            "quest_level_milestones": self.options.quest_level_milestones.value,
            # XP + shuffles (shop_shuffle/i_play_assassin removed — no-op in DLL)
            "xp_multiplier":   self.options.xp_multiplier.value,
            "monster_shuffle": self.options.monster_shuffle.value,
            "boss_shuffle":    self.options.boss_shuffle.value,
            # 1.8.4: filler toggles — bridge writes to ap_settings.dat,
            # DLL forces g_fillerTrapPct=0 when traps_enabled=0
            "traps_enabled":   self.options.traps_enabled.value,
            # 1.8.0 — Gate preloads (auto-generated per slot in generate_early)
            "act1_preload_normal":    self.preloads[(1, 0)],
            "act1_preload_nightmare": self.preloads[(1, 1)],
            "act1_preload_hell":      self.preloads[(1, 2)],
            "act2_preload_normal":    self.preloads[(2, 0)],
            "act2_preload_nightmare": self.preloads[(2, 1)],
            "act2_preload_hell":      self.preloads[(2, 2)],
            "act3_preload_normal":    self.preloads[(3, 0)],
            "act3_preload_nightmare": self.preloads[(3, 1)],
            "act3_preload_hell":      self.preloads[(3, 2)],
            "act4_preload_normal":    self.preloads[(4, 0)],
            "act4_preload_nightmare": self.preloads[(4, 1)],
            "act4_preload_hell":      self.preloads[(4, 2)],
            "act5_preload_normal":    self.preloads[(5, 0)],
            "act5_preload_nightmare": self.preloads[(5, 1)],
            "act5_preload_hell":      self.preloads[(5, 2)],
            # Meta
            "seed":        self.multiworld.seed,
            "player_name": self.multiworld.get_player_name(self.player),
        }


from BaseClasses import Item, Location


class Diablo2ArchipelagoItem(Item):
    game = "Diablo II Archipelago"


class Diablo2ArchipelagoLocation(Location):
    game = "Diablo II Archipelago"
