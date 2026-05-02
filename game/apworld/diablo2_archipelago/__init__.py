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
from .options import (
    Diablo2ArchipelagoOptions,
    OPTION_GROUPS,
    _COLL_SETS, _COLL_RUNES, _COLL_SPECIALS,
)
from .locations import COLL_LOCATIONS, COLL_LOC_BASE
from .regions import create_regions


# 1.9.0 — granular Collection helpers ----------------------------------
def _build_coll_mask(field_prefix, opts, lo, hi, _kind="set"):
    """Pack toggle values for items [lo..hi) into a single int bitmask.
    Used by fill_slot_data to compress 32+33+10 = 75 booleans into 6
    small integer fields the DLL parses bit-by-bit.

    field_prefix: e.g. "collect_set_" / "collect_rune_" / "collect_special_"
    opts: self.options
    lo, hi: half-open range of item indices
    _kind: which catalog list to look up names from
    """
    mask = 0
    if _kind == "set":
        names = [field for (field, _disp) in _COLL_SETS]
    elif _kind == "rune":
        names = [f"collect_rune_{r}" for r in _COLL_RUNES]
    elif _kind == "special":
        names = [field for (field, _disp) in _COLL_SPECIALS]
    else:
        return 0
    for i in range(lo, hi):
        if i >= len(names):
            break
        attr = names[i]
        if hasattr(opts, attr):
            v = getattr(opts, attr).value
            if v:
                mask |= (1 << (i - lo))
    return mask


class Diablo2ArchipelagoWebWorld(WebWorld):
    theme = "dirt"
    # 1.9.2 — Some AP versions look for option_groups on WebWorld
    # instead of World. Mirror it here too so the Options Creator
    # picks it up regardless of AP framework version.
    option_groups = OPTION_GROUPS
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

    # 1.9.2 — option_groups categorises ~150 options into 13 logical
    # sections so the Archipelago Options Creator (and YAML template
    # generator) can render them as expandable categories instead of
    # one giant flat list under "Game Options". Defined in options.py
    # at the bottom of the file (after every option class is in scope).
    option_groups = OPTION_GROUPS

    topology_present = True
    # 1.9.2: bumped from 2 to 3 because we added 293 new locations
    # (the six extra-check categories: Cow / Merc / HF+Runes / NPC /
    # Runeword / Cube). AP clients cache the datapackage keyed by
    # (game, data_version); without this bump, Universal Tracker /
    # spoiler clients can keep stale name<->id maps and fail to
    # resolve the new location names.
    data_version = 3

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

        1.9.0 — Goal=3 (Collection): difficulty progression is OPTIONAL in
        this mode (DLL fires goal-complete the moment all collection
        targets are satisfied), so for AP fill purposes we generate
        Normal-difficulty quest locations only. The 110 collection
        locations are added separately at the end of this function.
        """
        goal = self.options.goal.value  # 0-4
        max_act = 5                     # always full game
        if goal == 3:
            num_difficulties = 1        # Collection mode: Normal-only quest locations
        elif goal == 4:
            num_difficulties = 3        # Custom: generate full pool, DLL filters at runtime
        else:
            num_difficulties = goal + 1 # 1, 2, or 3

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

        # 1.9.0 NEW — Collection locations (only when goal=collection).
        # 110 IDs broken down as 32 sets + 33 runes + 35 gems + 10
        # specials. Each item is conditionally included based on its
        # individual toggle in the YAML. The `kind` field on each
        # COLL_LOCATIONS entry tells us which option-group the item
        # belongs to.
        if goal == 3:  # Goal=Collection
            for loc_id, loc_name, kind, idx in COLL_LOCATIONS:
                included = False
                if kind == "set":
                    field = _COLL_SETS[idx][0]
                    if hasattr(self.options, field) and getattr(self.options, field).value:
                        included = True
                elif kind == "rune":
                    field = f"collect_rune_{_COLL_RUNES[idx]}"
                    if hasattr(self.options, field) and getattr(self.options, field).value:
                        included = True
                elif kind == "gem":
                    if self.options.collection_target_gems.value:
                        included = True
                elif kind == "special":
                    field = _COLL_SPECIALS[idx][0]
                    if hasattr(self.options, field) and getattr(self.options, field).value:
                        included = True
                if included:
                    active.append((-(loc_id), loc_name, "collection",
                                   ItemClassification.filler, loc_id, 0))

        # 1.9.0 NEW — Bonus check categories (filler-only).
        # Per-difficulty quotas honor the active goal scope. Each location
        # has classification=filler so AP fill never places progression
        # items here — the escalating-chance roll on the DLL side may not
        # consume every slot, and stranded filler items are harmless.
        from .locations import (
            BONUS_BASE_SHRINE, BONUS_BASE_URN, BONUS_BASE_BARREL, BONUS_BASE_CHEST,
            BONUS_BASE_GOLDMS, BONUS_BASE_SETPICK,
            BONUS_QUOTA_SHRINE, BONUS_QUOTA_URN, BONUS_QUOTA_BARREL, BONUS_QUOTA_CHEST,
            GOLD_MILESTONE_NORMAL, GOLD_MILESTONE_NIGHTMARE, GOLD_MILESTONE_HELL,
            DIFF_LABEL,
        )

        def _emit_object_locs(toggle_attr: str, base: int, quota: int, label: str):
            """Add quota×num_difficulties locations for an object category."""
            if not getattr(self.options, toggle_attr).value:
                return
            for diff in range(num_difficulties):
                for slot in range(quota):
                    ap_id = base + diff * quota + slot
                    name = f"{label} #{slot + 1}{DIFF_LABEL[diff]}"
                    active.append((-(ap_id), name, "bonus_object",
                                   ItemClassification.filler, ap_id, diff))

        _emit_object_locs("check_shrines", BONUS_BASE_SHRINE, BONUS_QUOTA_SHRINE, "Shrine")
        _emit_object_locs("check_urns",    BONUS_BASE_URN,    BONUS_QUOTA_URN,    "Urn")
        _emit_object_locs("check_barrels", BONUS_BASE_BARREL, BONUS_QUOTA_BARREL, "Barrel")
        _emit_object_locs("check_chests",  BONUS_BASE_CHEST,  BONUS_QUOTA_CHEST,  "Chest")

        # Gold milestones (per-difficulty scoped)
        if self.options.check_gold_milestones.value:
            for i, gold in enumerate(GOLD_MILESTONE_NORMAL):
                ap_id = BONUS_BASE_GOLDMS + i
                active.append((-(ap_id), f"Gold Milestone: {gold:,}", "bonus_gold",
                               ItemClassification.filler, ap_id, 0))
            if num_difficulties >= 2:
                offs = len(GOLD_MILESTONE_NORMAL)
                for i, gold in enumerate(GOLD_MILESTONE_NIGHTMARE):
                    ap_id = BONUS_BASE_GOLDMS + offs + i
                    active.append((-(ap_id), f"Gold Milestone: {gold:,} (Nightmare)",
                                   "bonus_gold", ItemClassification.filler, ap_id, 1))
            if num_difficulties >= 3:
                offs = len(GOLD_MILESTONE_NORMAL) + len(GOLD_MILESTONE_NIGHTMARE)
                for i, gold in enumerate(GOLD_MILESTONE_HELL):
                    ap_id = BONUS_BASE_GOLDMS + offs + i
                    active.append((-(ap_id), f"Gold Milestone: {gold:,} (Hell)",
                                   "bonus_gold", ItemClassification.filler, ap_id, 2))

        # Set piece pickups (127 individual pieces; respects per-set
        # collect_set_* toggles from the Collection options block).
        if self.options.check_set_pickups.value:
            # Set pieces map to set indices via firstSlot+pieceCount in
            # d2arch_collections.c g_collSets[]. Authoring order is fixed:
            # Civerb's = pieces 0..2, Hsarus = 3..5, Cleglaw = 6..8, etc.
            # We don't replicate the full piece->set mapping here; the
            # DLL gates by per-set toggle when firing the check. The
            # apworld just enumerates all 127 location slots.
            for i in range(127):
                ap_id = BONUS_BASE_SETPICK + i
                active.append((-(ap_id), f"Set Pickup #{i + 1}",
                               "bonus_setpickup", ItemClassification.filler, ap_id, 0))

        # ============================================================
        # 1.9.2 — Six new check categories (Cow / Merc / HF+Runes /
        # NPC / Runeword / Cube). All filler; the DLL's standalone
        # reward path falls back to flat 1000 gold per slot.
        # ============================================================

        # Cow Level expansion (9 slots)
        if self.options.check_cow_level.value:
            from .locations import EXTRA_BASE_COW
            for d in range(num_difficulties):
                ap_id = EXTRA_BASE_COW + 0 + d
                active.append((-(ap_id), f"Cow Level Entry{DIFF_LABEL[d]}",
                               "extra_cow", ItemClassification.filler, ap_id, d))
            for d in range(num_difficulties):
                ap_id = EXTRA_BASE_COW + 3 + d
                active.append((-(ap_id), f"Cow King Killed{DIFF_LABEL[d]}",
                               "extra_cow", ItemClassification.filler, ap_id, d))
            for i, n in enumerate([100, 500, 1000]):
                ap_id = EXTRA_BASE_COW + 6 + i
                active.append((-(ap_id), f"Cow Kills: {n:,}",
                               "extra_cow", ItemClassification.filler, ap_id, 0))

        # Mercenary milestones (6 slots — single-difficulty, lifetime)
        if self.options.check_merc_milestones.value:
            from .locations import EXTRA_BASE_MERC
            ap_id = EXTRA_BASE_MERC + 0
            active.append((-(ap_id), "First Mercenary Hired",
                           "extra_merc", ItemClassification.filler, ap_id, 0))
            for i, n in enumerate([5, 10, 25, 50]):
                ap_id = EXTRA_BASE_MERC + 1 + i
                active.append((-(ap_id), f"Merc Resurrects: {n}",
                               "extra_merc", ItemClassification.filler, ap_id, 0))
            ap_id = EXTRA_BASE_MERC + 5
            active.append((-(ap_id), "Mercenary Reaches Level 30",
                           "extra_merc", ItemClassification.filler, ap_id, 0))

        # Hellforge + High runes (12 slots)
        if self.options.check_hellforge_runes.value:
            from .locations import EXTRA_BASE_HFRUNES
            for d in range(num_difficulties):
                ap_id = EXTRA_BASE_HFRUNES + 0 + d
                active.append((-(ap_id), f"Hellforge Used{DIFF_LABEL[d]}",
                               "extra_hfrunes", ItemClassification.filler, ap_id, d))
            tier_names = ["Pul-Gul", "Vex-Ber", "Jah-Zod"]
            for tier_idx, tname in enumerate(tier_names):
                for d in range(num_difficulties):
                    ap_id = EXTRA_BASE_HFRUNES + 3 + tier_idx * 3 + d
                    active.append((-(ap_id), f"High Rune {tname}{DIFF_LABEL[d]}",
                                   "extra_hfrunes", ItemClassification.filler, ap_id, d))

        # NPC dialogue (81 slots — 27 NPCs × 3 diff)
        if self.options.check_npc_dialogue.value:
            from .locations import EXTRA_BASE_NPC, EXTRA_NPC_NAMES
            for npc_idx, npc_name in enumerate(EXTRA_NPC_NAMES):
                for d in range(num_difficulties):
                    ap_id = EXTRA_BASE_NPC + npc_idx * 3 + d
                    active.append((-(ap_id), f"NPC Dialogue: {npc_name}{DIFF_LABEL[d]}",
                                   "extra_npc", ItemClassification.filler, ap_id, d))

        # Runeword crafting (50 slots)
        if self.options.check_runeword_crafting.value:
            from .locations import EXTRA_BASE_RUNEWORD
            for i in range(50):
                ap_id = EXTRA_BASE_RUNEWORD + i
                active.append((-(ap_id), f"Runeword Crafted #{i + 1}",
                               "extra_runeword", ItemClassification.filler, ap_id, 0))

        # Cube recipes (135 slots)
        if self.options.check_cube_recipes.value:
            from .locations import EXTRA_BASE_CUBE
            for i in range(135):
                ap_id = EXTRA_BASE_CUBE + i
                active.append((-(ap_id), f"Cube Recipe #{i + 1}",
                               "extra_cube", ItemClassification.filler, ap_id, 0))

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
            # Goal=3 (Collection) treats as Normal-only for AP fill purposes.
            # Goal=4 (Custom) generates full pool — DLL filters by required targets.
            goal_val = self.options.goal.value
            if goal_val == 3:
                num_difficulties = 1
            elif goal_val == 4:
                num_difficulties = 3
            else:
                num_difficulties = goal_val + 1  # 0-2 -> 1-3 diffs
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

    def get_filler_item_name(self) -> str:
        """1.9.0: Override AP's default get_filler_item_name to respect
        traps_enabled and skill_hunting gates. Without this override AP
        falls back to picking ANY item with classification=filler/trap
        from item_table when it needs an extra filler — that bypassed our
        _create_filler_items weight logic and let trap items leak into
        seeds that had traps_enabled=false (and Reset Point leak into
        skill_hunting=false seeds).

        Mirrors the weight distribution from _create_filler_items so the
        secondary fill path produces the same item mix.
        """
        weights = self._build_filler_weights()
        # Filter out zero-weight rows
        active = [(k, v) for k, v in weights.items() if v > 0]
        total = sum(v for _, v in active)
        if total == 0:
            return "Gold"  # absolute fallback (should never happen)
        roll = self.random.randrange(total)
        cum = 0
        for name, w in active:
            cum += w
            if roll < cum:
                return name
        return active[-1][0]

    def _build_filler_weights(self) -> dict:
        """Centralized weight table — used by both _create_filler_items
        (bulk fill) and get_filler_item_name (secondary single picks).
        Keeps the two paths consistent so toggles can never be bypassed
        by AP framework choosing the alternate code path.

        2026-05-01 rebalance per user feedback:
          - Gold lowered 15->10 (still felt over-represented)
          - Trap weights halved (10->5 total) — were too punishing
          - Charm/Set/Unique tripled (3/3/2 -> 9/9/6) — were too rare,
            now ~28% of pool combined which is the intended "drops feel
            meaningful" weight target. """
        weights = {
            "Gold":                    10,
            "Experience":              15,
            "5 Stat Points":           10,
            "Skill Point":             10,
            "Reset Point":              5,
            "Trap: Monsters":           2,
            "Trap: Slow":               1,
            "Trap: Weaken":             1,
            "Trap: Poison":             1,
            "Drop: Andariel Loot":      1,
            "Drop: Duriel Loot":        2,
            "Drop: Mephisto Loot":      2,
            "Drop: Diablo Loot":        1,
            "Drop: Baal Loot":          1,
            "Drop: Random Charm":       9,
            "Drop: Random Set Item":    9,
            "Drop: Random Unique":      6,
        }
        if not self.options.traps_enabled.value:
            for trap_name in ("Trap: Monsters", "Trap: Slow",
                              "Trap: Weaken", "Trap: Poison"):
                weights[trap_name] = 0
        if not self.options.skill_hunting.value:
            weights["Reset Point"] = 0
        return weights

    def _create_filler_items(self, count: int) -> None:
        """Create filler items with normalized percentage distribution.

        1.9.0 redesign: 17 typed fillers replacing the 8 generic ones.
        DLL pre-rolls magnitudes (gold 1-10000, xp 1-250000) and specific
        item picks (charm/set/unique) at character creation, so AP can
        place generic categories while the DLL still delivers something
        concrete the spoiler file can name.

        Weights live in _build_filler_weights so this bulk fill and the
        single-pick get_filler_item_name path can never disagree about
        the active distribution. """
        weights = self._build_filler_weights()

        # 1.9.0 fix — strip 0-weight rows BEFORE the distribution loop.
        # The "last type gets all remaining" rule was accidentally giving
        # leftover slots to disabled categories (e.g. Reset Point landing
        # in skill_hunting=false seeds, traps landing in traps_enabled=
        # false seeds). Active list is already filtered, so the last item
        # in the sorted-by-weight order is always a real candidate.
        active_weights = {k: v for k, v in weights.items() if v > 0}
        total_weight = sum(active_weights.values())
        if total_weight == 0:
            # All gates closed (no active categories) — fall back to Gold.
            for _ in range(count):
                self.multiworld.itempool.append(self.create_item("Gold"))
            return

        # Calculate counts per type using only active categories.
        remaining = count
        sorted_types = sorted(active_weights.keys(),
                              key=lambda k: active_weights[k], reverse=True)

        for i, filler_name in enumerate(sorted_types):
            if i == len(sorted_types) - 1:
                # Last (still active) type gets all remaining for rounding.
                filler_count = remaining
            else:
                filler_count = round(count * active_weights[filler_name] / total_weight)
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

        1.9.0 — Goal=3 (Collection): the actual win condition fires
        from the DLL when the collection book completes; for AP fill
        purposes we use Eve of Destruction Normal as a placeholder
        (always trivially reachable through skill-hunt fill).

        Victory = Eve of Destruction on the chosen difficulty.
        """
        goal_diff = self.options.goal.value  # 0/1/2/3
        # Collection mode (3) maps to Normal for the AP victory location.
        if goal_diff == 3:
            goal_diff = 0
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
            "goal":              self.options.goal.value,  # 0=Normal, 1=NM, 2=Hell, 3=Collection
            # Goal=Collection — granular per-item bitmasks. The DLL
            # parses these into setsTargeted[32] / runesTargeted[33] /
            # specialsTargeted[10] arrays. Bit N of the mask = toggle
            # value for item N.
            "collection_sets_mask_lo":     _build_coll_mask("collect_set_",     self.options, 0,  16),
            "collection_sets_mask_hi":     _build_coll_mask("collect_set_",     self.options, 16, 32),
            "collection_runes_mask_lo":    _build_coll_mask("collect_rune_",    self.options, 0,  16, _kind="rune"),
            "collection_runes_mask_md":    _build_coll_mask("collect_rune_",    self.options, 16, 32, _kind="rune"),
            "collection_runes_mask_hi":    _build_coll_mask("collect_rune_",    self.options, 32, 33, _kind="rune"),
            "collection_specials_mask":    _build_coll_mask("collect_special_", self.options, 0,  10, _kind="special"),
            "collection_target_gems":      self.options.collection_target_gems.value,
            "collection_gold_target":      self.options.collection_gold_target.value,
            # 1.9.2 — Custom goal (only meaningful when goal=4 / custom).
            # Empty CSV + 0 gold = trivially complete = falls back to
            # Full Normal in the DLL completion check.
            "custom_goal_gold_target":     self.options.custom_goal_gold_target.value,
            "custom_goal_targets_csv":     ",".join(sorted(
                self.options.custom_goal_targets.value)),
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
            "monster_shuffle":  self.options.monster_shuffle.value,
            "boss_shuffle":     self.options.boss_shuffle.value,
            # 1.9.0: System 1 — dead-end cave entrance shuffle (Pool A:
            # Acts 1+2, Pool B: Acts 3+4+5). DLL applies on character load
            # via ApplyEntranceShuffle, frozen into per-char state file.
            "entrance_shuffle": self.options.entrance_shuffle.value,
            # 1.8.4: filler toggles — bridge writes to ap_settings.dat,
            # DLL forces g_fillerTrapPct=0 when traps_enabled=0
            "traps_enabled":    self.options.traps_enabled.value,
            # 1.9.0: Bonus check categories (opt-in, filler-only).
            # DLL hooks shrine/urn/barrel/chest interactions and fires
            # the matching AP location via the escalating-chance helper
            # (10% -> 100%, reset per hit). Per-difficulty quotas:
            # shrines 50, urns/barrels 100, chests 200, set pickups 127,
            # gold milestones 7+5+5=17. See locations.py BONUS_BASE_*.
            "check_shrines":         self.options.check_shrines.value,
            "check_urns":            self.options.check_urns.value,
            "check_barrels":         self.options.check_barrels.value,
            "check_chests":          self.options.check_chests.value,
            "check_set_pickups":     self.options.check_set_pickups.value,
            "check_gold_milestones": self.options.check_gold_milestones.value,
            # 1.9.2: Six new check categories on top of bonus checks.
            # See locations.py EXTRA_BASE_* and the DLL's
            # d2arch_extrachecks.c module. Categories 4-6 (NPC/RW/Cube)
            # ship apworld locations + AP self-release wiring in 1.9.2;
            # DLL detection hooks land in 1.9.3.
            "check_cow_level":         self.options.check_cow_level.value,
            "check_merc_milestones":   self.options.check_merc_milestones.value,
            "check_hellforge_runes":   self.options.check_hellforge_runes.value,
            "check_npc_dialogue":      self.options.check_npc_dialogue.value,
            "check_runeword_crafting": self.options.check_runeword_crafting.value,
            "check_cube_recipes":      self.options.check_cube_recipes.value,
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
