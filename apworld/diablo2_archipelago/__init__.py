"""
Diablo II Archipelago - World Definition

Randomizes skill unlocks across Diablo II's quest system.
Up to 210 skills (7 classes x 30) are shuffled into quest reward locations
spanning up to 5 acts across up to 3 difficulties.
"""
from typing import Any

from BaseClasses import ItemClassification, Tutorial
from Options import OptionError
from worlds.AutoWorld import World, WebWorld

from .items import (
    item_table, FILLER_ITEMS, ALL_SKILL_ITEMS, ALL_SKILL_ITEMS_WITH_TRAPS,
    ITEM_BASE, CLASS_SKILLS, ASSASSIN_TRAP_SKILLS, GATE_KEY_ITEMS, GATE_COPIES_BY_ACT,
    TIER1_SKILL_IDS, SKILL_ID_BY_NAME,
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
    _CUSTOM_GOAL_DEFS,
    compute_custom_goal_scope,
    custom_goal_target_location,
)
from .locations import COLL_LOCATIONS, COLL_LOC_BASE
from .regions import create_regions
from .keyfill import place_gate_keys_in_act_spheres
from .tierfill import place_tiered_fillers


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
    # 1.9.9: bumped from 3 to 4 to invalidate cached generation
    # behaviour. Goal=4 (Custom) now uses scope-aware location
    # generation (was: always full 700-location pool) — same
    # location IDs but the active set per slot differs by selected
    # toggles, so clients should re-fetch to see the right set.
    data_version = 4

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

        1.9.9 — Goal=4 (Custom): scope is now computed from the union
        of selected sub-target toggles. Single-target goals like "kill
        Andariel Normal" generate ~50 locations (Act 1 Normal only)
        instead of the previous ~700. Multi-difficulty goals force
        max_act=5 because reaching higher diffs requires beating Baal
        on every lower diff (D2's hard-coded act/diff transitions).
        See options.compute_custom_goal_scope() for the rule details.
        """
        goal = self.options.goal.value  # 0-4
        if goal == 3:
            max_act = 5                 # full pool
            num_difficulties = 1        # Collection mode: Normal-only quest locations
        elif goal == 4:
            # 1.9.9: shrink scope to selected Custom Goal toggles instead
            # of always generating the full 700-location 3-diff pool. See
            # compute_custom_goal_scope() in options.py for the rule.
            scope_act, scope_diff = compute_custom_goal_scope(self.options)
            max_act = scope_act
            num_difficulties = scope_diff + 1
        else:
            max_act = 5                 # 0/1/2: full game
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

        # 1.9.0 — Collection locations. 110 IDs broken down as 32 sets +
        # 33 runes + 35 gems + 10 specials. Each item is conditionally
        # included based on its individual toggle in the YAML. The `kind`
        # field on each COLL_LOCATIONS entry tells us which option-group
        # the item belongs to.
        #
        # 3.9.7 — included under EVERY goal, not only Goal=Collection.
        # The collect_* toggles shape the F1 Collection book and its
        # checks; Goal=Collection additionally wins by completing the
        # targeted set. The DLL fires these under any goal since 3.9.7
        # (it used to parse the masks and then ignore them unless
        # goal==3, so "collection: gems only" changed nothing on a
        # Full Hell run).
        if True:
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

    def pre_fill(self) -> None:
        # 3.5.4 - zone locking makes this world strictly serial, so its gate keys
        # get placed act-by-act into the sphere of the act they open instead of
        # being left to the general fill, which was free to bury an Act 1 key in
        # another player's Nightmare Act 5. keyfill.py carries the full story.
        #
        # 3.7.3 — that pass is also what keeps the keys inside this world, so
        # the "let my keys travel" option is a decision about whether to run it
        # at all. Turning it on hands the keys to the general fill, which is
        # free to put them in other people's games: still solvable, but you may
        # have nothing to do until somebody else plays. The default keeps the
        # guarantee; opting out is the player's call, not ours.
        if (self.options.zone_locking.value
                and not self.options.gate_keys_in_other_worlds.value):
            place_gate_keys_in_act_spheres(self)

        # Keys first, always. They are progression and need the pick of the
        # reachable locations; tiered filler is the thing that can be handed
        # back without consequence, so it takes what is left.
        if self.options.tiered_filler_depth.value:
            place_tiered_fillers(self)

    def generate_early(self) -> None:
        """1.8.0 — pick 15 preload IDs (5 acts × 3 difficulties) per slot.
        Baked here so the same seed always produces the same layout.
        Exposed via fill_slot_data; not user-configurable.
        """
        # 3.3.5 — refuse the one setting that makes a character with no
        # skills at all. With the class filter on and every class switched
        # off there is nothing to put in the pool, and with skill hunting on
        # the game does not grant the class natives either, so the player
        # starts and stays skill-less. Generation used to accept this and
        # hand out 250 filler items.
        if (bool(self.options.skill_class_filter.value)
                and bool(self.options.skill_hunting.value)):
            classes = ("amazon", "sorceress", "necromancer", "paladin",
                       "barbarian", "druid", "assassin")
            if not any(getattr(self.options, f"include_{c}").value
                       for c in classes):
                raise OptionError(
                    f"Diablo II Archipelago ({self.player_name}): Skill Class "
                    "Filter is on but no class is included, so no skills can "
                    "be placed and Skill Hunting would leave the character "
                    "with none. Include at least one class, or turn the "
                    "filter off.")

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
            # Goal=4 (Custom) — 1.9.9: scope-aware, mirrors get_active_locations.
            goal_val = self.options.goal.value
            # 1.9.10 — also track max_act so we don't add gate keys for acts
            # the player won't physically traverse (narrow custom goals).
            scope_max_act = 5  # default: full game
            if goal_val == 3:
                num_difficulties = 1
            elif goal_val == 4:
                # 1.9.9 — match get_active_locations() scope so we don't
                # add gate keys for difficulties the player won't visit.
                # 1.9.10 — also match max_act for the same reason. With
                # a narrow custom goal like "kill Andariel Normal", scope
                # is (max_act=1, max_diff=0); only 4 Act 1 Normal gate keys
                # are physically usable; adding the other 14 Normal keys
                # (Acts 2-5) blows the progression budget when many EXCLUDED
                # check categories are also on. Found via 1.9.10 validation
                # mega-test failure mega_673.
                _scope_act, _scope_diff = compute_custom_goal_scope(self.options)
                scope_max_act = _scope_act
                num_difficulties = _scope_diff + 1
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
                # 1.9.10 — skip gate keys for acts beyond scope. The
                # corresponding gate-location pool is also capped by
                # max_act in get_active_locations / create_regions, so
                # these keys would have no destination location to unlock.
                if act > scope_max_act:
                    continue
                # V5 - progressive: create one copy per gate in this act.
                copies = GATE_COPIES_BY_ACT.get(act, 1)
                for _copy in range(copies):
                    zone_keys_in_pool.append((ap_id, name, classification))
                key_item = self.create_item(name)
                # V5 (T4) - Nightmare/Hell gate keys are progression_skip_balancing.
                # All 54 keys used to be plain progression, forming a 54-link serial
                # chain that AP's progression balancing cannot flatten, so early D2
                # keys kept landing in very late spheres of other games (Smawsh: an
                # Act 3 key behind Factorio's 9th science pack). skip_balancing tells
                # the filler not to spread these across the whole multiworld sphere
                # range; the Normal keys stay fully balanced because they are the ones
                # that actually gate the player's early game.
                # 3.5.4 - ALL gate keys skip balancing now, not just NM/Hell.
                # pre_fill already pins each key to the sphere of the act it
                # opens, which is a stronger guarantee than balancing can offer;
                # letting balancing shuffle them afterwards only undoes that. It
                # also broke generation outright: with the keys threaded through
                # their acts, balance_multiworld_progression tried to flatten a
                # chain it cannot flatten and died with "Not all required items
                # reachable" on 2 of 4 test seeds. Skipping it fixes both.
                key_item.classification = ItemClassification.progression_skip_balancing
                self.multiworld.itempool.append(key_item)
                for _copy in range(copies - 1):
                    extra = self.create_item(name)
                    extra.classification = ItemClassification.progression_skip_balancing
                    self.multiworld.itempool.append(extra)
                # 3.5.4 - placement of these keys is no longer left to the general
                # fill. `pre_fill` (keyfill.py) puts each act's keys in the sphere
                # that act belongs to: Act 1 Normal in sphere 1 ... Act 5 Hell in
                # sphere 15. They may still land in other games - they just may not
                # land deeper than the act they open. See keyfill.py for the seed
                # that forced this and why progression balancing cannot cover it.
                #
                # Nothing to register here any more: the old early_items nudge only
                # reached the FIRST Act 1 key and said nothing about the other 53.

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

            # NATIVE-ONLY skills are never placed as AP items. They are tied to
            # class-specific animations (Smite, javelin throws, werewolf bites,
            # assassin kicks/claws/charge-ups) and CANNOT be used by another
            # class — so a non-native receiver got a check it could never use,
            # and the in-game skill list (which excludes them) never matched the
            # placed pool. We drop ALL native-only here (the seed only contains
            # cross-class-usable skills) and the DLL grants each character its
            # OWN native-only skills free at creation. Keep NATIVE_ONLY_SKILL_IDS
            # in sync with d2arch_skills.c.
            from .items import NATIVE_ONLY_SKILL_IDS
            available_skills = [s for s in available_skills if s[0] not in NATIVE_ONLY_SKILL_IDS]

            # 2.8.1 (Fix 7) — NO all-classes-off fallback. Previously, selecting
            # the custom class filter but leaving every class unchecked silently
            # fell back to ALL skills, so testers who deselected classes still
            # received skills from those classes. Honour the filter literally:
            # nothing selected -> no skill items placed.

            pool_size = len(available_skills)
        else:
            # All classes mode — every cross-class-usable skill (210 minus the 17
            # native-only). Native-only skills are never AP items (see above); the
            # DLL grants the character's own native-only skills free at creation,
            # so the placed pool always equals what the character can actually use.
            from .items import NATIVE_ONLY_SKILL_IDS
            available_skills = [s for s in ALL_SKILL_ITEMS if s[0] not in NATIVE_ONLY_SKILL_IDS]
            pool_size = len(available_skills)

        # 2.8.1 (Fix 7) — Skill Hunting OFF: place NO skill items at all. When
        # SH=OFF the DLL initializes every class-native skill at character
        # creation, so AP-delivered skill items are purely cosmetic — testers
        # reported receiving unwanted "skill filler" in SH=OFF seeds. Empty the
        # pool so these locations fill with real filler (gold/potions/runes/etc.)
        # instead of redundant skills.
        if not skill_hunting:
            available_skills = []
            pool_size = 0

        # Optional cap on how many skills exist at all, split across tiers.
        pool_cap = self.options.skill_pool_size.value
        if pool_cap > 0 and len(available_skills) > pool_cap:
            available_skills = self._limit_pool_by_tier(available_skills, pool_cap)
            pool_size = len(available_skills)
        self._resolved_skill_pool = len(available_skills)

        # 3.5.5 — how many skills you begin with is a real setting again, and it
        # is the SAME setting standalone has always had (the DLL already reads
        # `starting_skills` out of slot_data; only the apworld never sent one and
        # hardcoded 6). Parity was the whole point: the two modes should not
        # disagree about how a run opens.
        starting = self.options.starting_skills.value

        # 1.9.10 — cap pool so total NON-EXCLUDABLE items fit in NON-EXCLUDED
        # locations. AP's fill algorithm enforces:
        #   - PROGRESSION + USEFUL items can only land at non-EXCLUDED locations
        #   - FILLER + TRAP items can land anywhere (excluded or not)
        # Pre-1.9.10 cap only ensured total_items ≤ total_locations. When
        # many check categories were toggled ON (lots of EXCLUDED bonus/
        # collection locations), the SH=ON case would generate 210 USEFUL
        # skills + 18-54 PROGRESSION gate keys = 228+ non-excludable items
        # competing for the tiny non-EXCLUDED location pool — FillError.
        # Found via 1.9.10 exhaustive validation (162 random YAML configs
        # failed with the old cap, all pass with this fix).
        #
        # New formula: count EXCLUDED locations from active set, subtract
        # from cap so skill pool only fills the available non-excluded slots.
        # When SH=OFF skills are FILLER (excludable), so they bypass the
        # tighter cap and fill EXCLUDED slots freely.
        EXCLUDED_QUEST_TYPES = ("bonus_object", "bonus_gold", "bonus_setpickup",
                                "extra_cow", "extra_merc", "extra_hfrunes",
                                "extra_npc", "extra_runeword", "extra_cube",
                                "collection")
        excluded_count = sum(1 for (_, _, qtype, _, _, _) in active_locations
                             if qtype in EXCLUDED_QUEST_TYPES)
        # 3.3.5 — with entrance shuffle + zone locking both on, create_regions
        # also marks every check in a movable dungeon EXCLUDED. Count those
        # here too: if the budget and the region code disagree about how many
        # slots can take a real item, fill runs out of room and generation
        # dies on an item/location mismatch.
        from .regions import shuffle_excluded_quest_ids
        shuffle_excluded = shuffle_excluded_quest_ids(
            bool(self.options.entrance_shuffle.value), bool(zone_locking))
        if shuffle_excluded:
            excluded_count += sum(
                1 for (qid, _, qtype, _, _, _) in active_locations
                if qtype not in EXCLUDED_QUEST_TYPES and qid in shuffle_excluded)
        non_excluded_count = location_count - excluded_count

        # Locked boss tokens (pre-placed by create_regions) take slots that cannot
        # receive random items. Subtract them from the available budget so the
        # pool size + locked tokens == total locations.
        locked_tokens = getattr(self, '_locked_token_count', 0)

        if skill_hunting:
            # SH=ON: skills are USEFUL, must fit non-excluded budget.
            # Boss-token locations are non-excluded story quests, so subtract them.
            max_skills_in_pool = non_excluded_count - len(zone_keys_in_pool) - locked_tokens
        else:
            # SH=OFF: skills are FILLER, can fill anywhere including EXCLUDED.
            max_skills_in_pool = location_count - len(zone_keys_in_pool) - locked_tokens

        # 3.3.5 — leave room for the player's own exclude_locations. Every
        # YAML has that option (it comes from PerGameCommonOptions), and
        # Archipelago applies it AFTER create_items: it flips those locations
        # to EXCLUDED, which can only hold filler. The cap above frequently
        # lands exactly on the budget, so a single excluded location was
        # enough to turn a working config into "not enough filler items".
        # Reserving the slack up front costs a few skills and nothing else.
        try:
            user_excluded = len(self.options.exclude_locations.value)
        except AttributeError:
            user_excluded = 0
        max_skills_in_pool -= user_excluded

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
        #
        # 1.9.10 — Skill Hunting OFF: skills are FILLER (was: progression,
        # which was wrong). When SH=OFF the DLL initializes 30 class-native
        # skills at character creation; AP-delivered skill items don't gate
        # any actual progression (they're cosmetic, since the player already
        # has all class skills). Marking them progression was producing:
        #   (a) "Not enough locations for progression items" FillErrors in
        #       configs with many EXCLUDED check categories — 210 skills+keys
        #       trying to fit into a small non-EXCLUDED quest pool.
        #   (b) "Not enough filler items for excluded locations" FillErrors —
        #       because skills hogged the filler-slot budget despite not
        #       actually being filler-class.
        # Fix: SH=OFF skills become FILLER (excludable). Found via 1.9.10
        # exhaustive validation: 162/562 random YAMLs failed with the old
        # rule, all rebalance to passing with this fix.
        skill_items = []
        for d2_id, name, classification in ordered_pool:
            item = self.create_item(name)
            if skill_hunting:
                # SH=ON: skills are USEFUL (player needs them but no specific
                # one gates progression in our logic graph)
                item.classification = ItemClassification.useful
            else:
                # SH=OFF: skills are FILLER. DLL gives the 30 class-native
                # skills automatically at char creation; AP-delivered skill
                # items are cosmetic only and can safely fill EXCLUDED slots.
                item.classification = ItemClassification.filler
            skill_items.append(item)

        # Pre-place starting skills as "start inventory" so the player has them
        # immediately.
        #
        # 3.5.5 — TIER 1 ONLY. `skill_items` is shuffled across all three tiers,
        # so taking the first N handed out whatever came up: a run could open on
        # Frozen Orb or Blessed Hammer, which is not an opening, it is an ending.
        # Standalone never did this — its unlock loop only ever touches the T1
        # array — so this also closes a real AP-vs-standalone split.
        #
        # Preference order, not a filter: tier-1 items are taken first, and only
        # if there are not enough of them (a class filter can leave very few)
        # does the rest of the pool make up the difference. Fewer starting
        # skills than asked for is acceptable; a start that cannot happen is not.
        tier1 = [it for it in skill_items if SKILL_ID_BY_NAME.get(it.name) in TIER1_SKILL_IDS]
        self.random.shuffle(tier1)
        chosen = tier1[:starting]
        if len(chosen) < starting:
            rest = [it for it in skill_items if it not in chosen]
            chosen += rest[:starting - len(chosen)]
        chosen_ids = {id(it) for it in chosen}
        for item in skill_items:
            if id(item) in chosen_ids:
                self.multiworld.push_precollected(item)
            else:
                self.multiworld.itempool.append(item)

        # --- Filler items ---
        # 2.8.1 (Fix 7) — clamp at 0: when the skill pool is empty (SH=OFF, or a
        # custom filter with no classes), len(skill_items) < starting, so the
        # bare subtraction goes negative and would over-count filler_needed by
        # up to `starting`, overflowing the location pool.
        items_in_pool = max(0, len(skill_items) - starting)  # skills actually in the pool (not precollected)
        items_in_pool += len(zone_keys_in_pool)  # zone keys also occupy pool slots
        filler_needed = location_count - items_in_pool - locked_tokens
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

    def _limit_pool_by_tier(self, skills, cap):
        """Take `cap` skills at random, spread evenly over the three tiers.

        A flat sample would be lopsided: tier 1 has the cheap openers and tier 3
        the payoffs, and a pool that happened to be mostly tier 3 would be
        unplayable early. Splitting the quota keeps every seed opening on
        something usable. Whatever the class filter already removed is simply
        absent from `skills`, so the filter is honoured by construction.
        """
        from .items import TIER1_SKILL_IDS, TIER2_SKILL_IDS, TIER3_SKILL_IDS
        buckets = [
            [s for s in skills if s[0] in TIER1_SKILL_IDS],
            [s for s in skills if s[0] in TIER2_SKILL_IDS],
            [s for s in skills if s[0] in TIER3_SKILL_IDS],
        ]
        # Anything the tier table does not know about (a skill added to the
        # game but not yet to the tables) must not silently vanish.
        known = {id(s) for b in buckets for s in b}
        leftovers = [s for s in skills if id(s) not in known]

        chosen = []
        want = min(cap, len(skills))
        # Largest-remainder split, so 31 becomes 11/10/10 rather than 10/10/10
        # and a lost skill.
        sizes = [len(b) for b in buckets]
        total = sum(sizes)
        if total == 0:
            return skills[:want]
        quota, rem = [], []
        for n in sizes:
            quota.append(want * n // total)
            rem.append((want * n) % total)
        while sum(quota) < want:
            best = max(range(3), key=lambda t: (rem[t], sizes[t] - quota[t]))
            if quota[best] >= sizes[best]:
                rem[best] = -1
                if all(r < 0 for r in rem):
                    break
                continue
            quota[best] += 1
            rem[best] = -1
        for b, q in zip(buckets, quota):
            picked = list(b)
            self.random.shuffle(picked)
            chosen.extend(picked[:q])
        # If a tier ran dry, top up from anything still unused.
        if len(chosen) < want:
            used = {id(s) for s in chosen}
            rest = [s for s in skills if id(s) not in used]
            self.random.shuffle(rest)
            chosen.extend(rest[:want - len(chosen)])
        self.random.shuffle(chosen)
        return chosen

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
            "Random Rune":                6,
            "Random Gem":                 6,
            "Rejuvenation Potions":       5,
            "Full Rejuvenation Potions":  3,
            "Greater Healing Potions":    5,
            "Greater Mana Potions":       5,
            "Throwing Potions":           4,
            "Random Jewel":               3,
            "Tome of Town Portal":        3,
            "Tome of Identify":           3,
            "Skeleton Keys":              4,
            "Utility Potions":            3,
            # Tiered variants. Deliberately thin: their scarcity is meant to
            # come from tierfill.py placing them deep, and a fat weight here
            # would undo that by simply making more of them.
            "Random Rune (Mid)":          4,
            "Random Rune (High)":         2,
            "Flawless Gem":               4,
            "Perfect Gem":                2,
            "Small Charm":                4,
            "Large Charm":                3,
            "Grand Charm":                2,
            # Discrete point sizes, weighted so the small ones dominate.
            "1 Stat Point":               6,
            "3 Stat Points":              5,
            "10 Stat Points":             2,
            "2 Skill Points":             4,
            "3 Skill Points":             2,
            "3 Reset Points":             2,
            "Trap: Barrel Field":         2,
            "Trap: Urn Garden":           2,
            "Trap: False Treasure":       2,
            "Trap: Junk Backpack":        2,
            # Slot-filtered gear. Kept individually thin on purpose: sixteen
            # entries at the old "Drop: Random Set Item" weight of 9 would be
            # 144 weight of gear on their own and drown everything else.
            "Drop: Set Armor":            2,
            "Drop: Set Helm":             2,
            "Drop: Set Shield":           2,
            "Drop: Set Gloves":           2,
            "Drop: Set Boots":            2,
            "Drop: Set Belt":             2,
            "Drop: Set Weapon":           2,
            "Drop: Set Jewelry":          2,
            "Drop: Unique Armor":         2,
            "Drop: Unique Helm":          2,
            "Drop: Unique Shield":        2,
            "Drop: Unique Gloves":        2,
            "Drop: Unique Boots":         2,
            "Drop: Unique Belt":          2,
            "Drop: Unique Weapon":        2,
            "Drop: Unique Jewelry":       2,
        }
        if not self.options.traps_enabled.value:
            for trap_name in ("Trap: Monsters", "Trap: Slow",
                              "Trap: Weaken", "Trap: Poison",
                              "Trap: Barrel Field", "Trap: Urn Garden",
                              "Trap: False Treasure", "Trap: Junk Backpack"):
                weights[trap_name] = 0
        if not self.options.skill_hunting.value:
            weights["Reset Point"] = 0
            weights["3 Reset Points"] = 0
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

        1.9.9 — Goal=4 (Custom): build the completion lambda from the
        union of selected sub-target toggles. AND-of-reachable across
        every selected toggle's bound AP location (or proxy for
        subsystem/bulk toggles that don't bind to a single AP location).
        The DLL is the source of truth for the actual goal-complete
        check (CustomGoal_IsComplete walks the required-vs-fired
        bitmap); the AP-side condition just needs to be reachable so
        AP fill knows the slot is solvable.

        Victory = Eve of Destruction on the chosen difficulty.
        """
        # 1.9.9 — Goal=4 (Custom) handled separately from the simple-
        # boss-kill logic below. Builds the completion lambda from the
        # union of selected sub-target toggles.
        if self.options.goal.value == 4:
            self._set_rules_custom_goal()
            return

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

    def _set_rules_custom_goal(self) -> None:
        """1.9.9 — Build completion_condition for Goal=4 (Custom Goal).

        Walks every enabled custom_goal_* toggle, looks up the AP
        location it binds to (via custom_goal_target_location() — that
        function falls back to a subsystem/bulk proxy when there is no
        1:1 binding), and ANDs them all together via state.can_reach_location.

        Validates that every resolved location actually exists in the
        slot's active location set (so a toggle that requires
        check_cow_level=true doesn't crash AP fill if the user enabled
        kill_cow_king_normal without check_cow_level).

        If no toggles resolve to a real location (e.g. user picked only
        bulk targets but disabled the underlying check_* options), falls
        back to "Sisters to the Slaughter" (Andariel Normal) as a trivial
        completion condition. This means the seed will instantly be
        winnable AP-side; the DLL's CustomGoal_IsComplete still gates
        the actual in-game win, so standalone behaviour is unchanged.
        """
        # Build a set of all locations the slot will actually create.
        # Used to filter out custom-goal targets whose underlying
        # category was toggled OFF.
        active_loc_names = {
            name for (_, name, _, _, _, _) in self.get_active_locations()
        }

        target_locs: list[str] = []
        for csv_tok, field, _disp, _doc in _CUSTOM_GOAL_DEFS:
            opt = getattr(self.options, field, None)
            if opt is None or not opt.value:
                continue
            ap_loc = custom_goal_target_location(csv_tok)
            if not ap_loc:
                continue  # unknown token — silently skip (defensive)
            if ap_loc not in active_loc_names:
                # Toggle's target location wasn't generated. E.g. user
                # picked kill_cow_king_normal but check_cow_level=false
                # so "Cow King Killed" doesn't exist. Skip — the DLL
                # will gate this target on its own; we just can't
                # express it AP-side without the location.
                continue
            target_locs.append(ap_loc)

        # Dedupe while preserving order — multiple toggles can share
        # the same proxy location (e.g. all 4 Pandemonium targets all
        # proxy to "Eve of Destruction (Hell)").
        seen: set[str] = set()
        target_locs = [l for l in target_locs if not (l in seen or seen.add(l))]

        if not target_locs:
            # User selected no resolvable targets (e.g. only gold target
            # set, or selected only bulk subsystems whose check_* options
            # are off). Trivial Andariel Normal completion so AP fill
            # has SOMETHING to evaluate. The DLL still gates the actual
            # win on the CustomGoal_IsComplete bitmap, so this is safe.
            target_locs = ["Sisters to the Slaughter"]
            if "Sisters to the Slaughter" not in active_loc_names:
                # Truly degenerate case — no Act 1 quests at all? Pick
                # any single active progression-class location as the
                # fallback so completion_condition isn't dangling.
                fallback = next(iter(active_loc_names), None)
                if fallback:
                    target_locs = [fallback]

        # Final completion lambda: AND every selected target reachable.
        def custom_goal_complete(state, locs=tuple(target_locs), p=self.player):
            for loc in locs:
                if not state.can_reach_location(loc, p):
                    return False
            return True

        self.multiworld.completion_condition[self.player] = custom_goal_complete

    def fill_slot_data(self) -> dict[str, Any]:
        """Data sent to the client/bridge. Bridge writes to ap_settings.dat.
        1.8.0 — Simplified: only user-facing options + auto-generated preloads.
        Internal values (skill pool=210, starting=6, filler defaults) are
        hardcoded on the DLL side and not transmitted. """
        slot_data = {
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
            # CSV is built from the 54 individual Custom Goal Toggle
            # options (one per target/subsystem). Empty CSV + 0 gold =
            # trivially complete = falls back to Full Normal in the
            # DLL completion check. CSV ordering matches _CUSTOM_GOAL_DEFS
            # so the DLL parser sees a stable token order.
            "custom_goal_gold_target":     self.options.custom_goal_gold_target.value,
            "custom_goal_targets_csv":     ",".join(
                csv_tok
                for (csv_tok, field, _disp, _doc) in _CUSTOM_GOAL_DEFS
                if hasattr(self.options, field) and getattr(self.options, field).value
            ),
            "death_link":        self.options.death_link.value,
            # Quest toggles
            "quest_story":            1,  # always ON — engine-required
            "quest_hunting":          self.options.quest_hunting.value,
            "quest_kill_zones":       self.options.quest_kill_zones.value,
            "quest_exploration":      self.options.quest_exploration.value,
            "quest_waypoints":        self.options.quest_waypoints.value,
            "quest_level_milestones": self.options.quest_level_milestones.value,
            # XP + shuffles. 2.1 — the launcher applies monster/super-unique/shop
            # shuffle + item reqs via the seed-bound data files (it reads these from
            # slot_data); the mod still does the act-boss cosmetic swap from
            # boss_shuffle: the legacy option is REMOVED from the option set;
            # shuffles. (i_play_assassin removed — no-op in DLL.)
            "xp_multiplier":   self.options.xp_multiplier.value,
            # 3.5.5 — the DLL has always parsed `starting_skills` from slot_data
            # (d2arch_ap.c); the apworld simply never sent one, so AP silently
            # used the in-game default while standalone used the player's.
            "starting_skills": self.options.starting_skills.value,
            # Each gold/XP reward rolls a random amount in [min, max] (DLL).
            "monster_shuffle":  self.options.monster_shuffle.value,
            "superunique_shuffle": (
                self.options.superunique_shuffle.value
            ),
            # TEMPORARILY DISABLED — forced to 0 regardless of the YAML.
            #
            # A shuffled act boss has been seen giving up its attack for good
            # once the player breaks line of sight (BetaHub #13), which leaves
            # a seed that cannot be finished. The mod's swap code is untouched;
            # this is the gate. Both keys go out as 0 because the mod reads
            # `boss_shuffle` for the swap itself and the launcher reads
            # `act_boss_shuffle` — leaving either one live would half-enable it.
            #
            # To re-enable: restore the two `or` expressions below, and put
            # ActBossShuffle back in the "Shuffles" option group.
            "act_boss_shuffle": 0,
            "boss_shuffle": 0,
            "shop_shuffle":     self.options.shop_shuffle.value,
            "item_level_reqs":  self.options.item_level_reqs.value,
            "item_stats_reqs":  self.options.item_stats_reqs.value,
            # 2.x one-chest: per-seed stash isolation (launcher reads this from
            # slot_data and writes [settings] StashIsolated for the DLL).
            "stash_isolated":   self.options.stash_isolated.value,
            # 2.x — Monster Revive Trap. DLL reads monster_trap from ap_settings.dat;
            # kill a trapped monster → 8 hostile copies spawn at the death spot.
            "monster_trap":     self.options.monster_trap.value,
            # 1.9.0: System 1 — dead-end cave entrance shuffle (Pool A:
            # Acts 1+2, Pool B: Acts 3+4+5). DLL applies on character load
            # via ApplyEntranceShuffle, frozen into per-char state file.
            # 2.x — combined mode (zone_locking + entrance_shuffle BOTH on) is
            # now SUPPORTED. Previously auto-disabled here because gate keys were
            # tied to a super-unique in a fixed zone and the shuffle moved that
            # zone out of reach. The DLL key-conditions layer (d2arch_keycond.c)
            # decouples them: in combined mode each gate is cleared by a
            # topology-independent CONDITION (gold / level / shrines / barrels /
            # urns / chests), which is satisfiable regardless of where the
            # shuffle put any zone. Gate-key ITEMS remain ItemClassification.
            # progression (items.py), so AP fill still places them logically
            # across all players' worlds. We therefore pass entrance_shuffle
            # through verbatim and let the DLL gate combined behaviour.
            "entrance_shuffle": self.options.entrance_shuffle.value,
            # 1.8.4: filler toggles — bridge writes to ap_settings.dat,
            # DLL forces g_fillerTrapPct=0 when traps_enabled=0
            "traps_enabled":    self.options.traps_enabled.value,
            # The DLL uses this for the STANDALONE reward roll only; under AP
            # the placement has already happened in pre_fill. Sent anyway so a
            # player who switches a seed to standalone gets the same curve.
            "tiered_filler_depth": self.options.tiered_filler_depth.value,
            # 1.9.10 — Skill level requirement toggle (Maegis #2). When 0,
            # editor "+" button bypasses vanilla Skills.txt reqlevel check.
            "skill_level_reqs": self.options.skill_level_reqs.value,
            "skill_max_level":  self.options.skill_max_level.value,
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
        resolved = getattr(self, "_resolved_skill_pool", 0)
        if resolved > 0:
            slot_data["skill_pool_size"] = resolved
        return slot_data


from BaseClasses import Item, Location


class Diablo2ArchipelagoItem(Item):
    game = "Diablo II Archipelago"


class Diablo2ArchipelagoLocation(Location):
    game = "Diablo II Archipelago"
