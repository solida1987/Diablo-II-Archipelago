# -*- coding: utf-8 -*-
"""Place a zone-locked slot's gate keys in the sphere of the act they open.

WHY THIS EXISTS
---------------
Zone locking turns a slot into a strictly serial world: act N region 3 needs two
of that act's keys, region 5 needs four, and the act boss lives in region 5. The
rules in regions.py model that exactly. What nothing constrained was WHERE the
keys were allowed to land, and Archipelago is free to answer "in someone else's
endgame".

It did. Seed 36491296110575942106 (2026-08-08): a zone-locked slot held exactly
ONE Act 1 Normal key. The second sat on another player's Act 5 Normal location
and the third on their Act 5 NIGHTMARE. That slot had nine zones to play and
then nothing at all until a different person had finished the game twice. Legal
under Accessibility: Full -- every key WAS reachable, eventually -- and
completely unplayable.

Progression balancing cannot repair this. It nudges items between spheres using
per-player location counts; it has no notion that THIS world's sphere structure
collapses to nothing without its own keys.

THE RULE
--------
Act 1's keys belong in sphere 1, act 2's in sphere 2, on to act 5 in sphere 5;
then Nightmare act 1 opens sphere 6 and Hell act 5 closes sphere 15. A key may
never sit deeper than the act it unlocks.

Keys are placed in THIS world only. Letting them travel into other games was
tried and measurably broke 300-player seeds -- see section 2 of the code.

HOW
---
Runs in pre_fill, before the general fill has committed to anything. A
CollectionState is carried that holds NOTHING except this slot's own already
placed keys and its own act tokens. Keys are placed ONE AT A TIME in play order,
re-sweeping between each, so every key is committed to a location that is
reachable with strictly the keys before it.

Two consequences worth naming, because they are what make this a guarantee
rather than a heuristic:

  * The state is deliberately pessimistic about every other world: it grants
    none of their items. A location reachable in it is reachable in EVERY
    superset of it, so the bound holds no matter how the rest of the multiworld
    is filled afterwards.

  * Only our own locations are considered, so the pass cannot disturb another
    world's own pre_fill reservations, and it costs one scan of our world rather
    than one of the whole multiworld -- which matters when the multiworld has a
    few hundred thousand locations in it.

Entrance shuffle needs no special case and must not get one: with it on,
regions.py already marks every location in a relocatable dungeon EXCLUDED, and
EXCLUDED locations are filtered out below. The reachability tested here is the
shuffled world's, because it is read from the same rules that world was built
from.

Anything that does not fit -- a crowded early multiworld can run out of shallow
locations -- is handed back to the general pool rather than raising, and counted
in `world.keyfill_report` so a degraded generation is visible instead of silent.
"""
from __future__ import annotations

import re

from BaseClasses import CollectionState, LocationProgressType
from Fill import fill_restrictive

KEY_NAME_RE = re.compile(r"^Progressive Act (\d) Key \((Normal|Nightmare|Hell)\)$")
DIFF_INDEX = {"Normal": 0, "Nightmare": 1, "Hell": 2}
DIFF_TAG = ("N", "NM", "H")


def _sphere_label(diff: int, act: int) -> int:
    """Act 1 Normal -> 1 ... act 5 Hell -> 15."""
    return diff * 5 + act


def place_gate_keys_in_act_spheres(world) -> None:
    """pre_fill hook. Caller gates on zone_locking."""
    multiworld = world.multiworld
    player = world.player

    # --- 1. Take our own gate keys out of the shared item pool -------------
    groups: dict[tuple[int, int], list] = {}
    keep = []
    for item in multiworld.itempool:
        match = KEY_NAME_RE.match(item.name) if item.player == player else None
        if match:
            groups.setdefault(
                (DIFF_INDEX[match.group(2)], int(match.group(1))), []).append(item)
        else:
            keep.append(item)
    if not groups:
        return
    multiworld.itempool[:] = keep

    state = CollectionState(multiworld)
    own_locations = multiworld.get_locations(player)

    def sweep_own() -> None:
        # Sweeping is how a state picks up items already sitting on locations.
        # With no argument it sweeps ALL players, which quietly hands us every
        # other world's progression: their act tokens cascade their whole game
        # open and act 3 keys land happily in someone's Nightmare act 5 again.
        # Measured - it re-broke the exact seeds this module exists to fix.
        #
        # "Our own" has to mean the ITEM, not just the location. An earlier
        # slot's pre_fill may already have dropped ITS key into OUR world, and
        # collecting that would grant that player a region they have not earned
        # - the same optimism, one step removed. Measured: it put an Act 1 key
        # one sphere too deep, on another D2 slot's Dark Wood quest.
        #
        # Locations still empty carry advancement=False, so they cost nothing.
        state.sweep_for_advancements(
            [loc for loc in own_locations
             if loc.item is None or loc.item.player == player])

    sweep_own()

    # --- 2. OUR OWN LOCATIONS ONLY ------------------------------------------
    #
    # Earlier versions also placed keys into other worlds' sphere-1 locations,
    # so a key could travel into someone else's game. It reads well and Marco
    # explicitly allowed it — but it is not safe, and the scale test proved it:
    #
    #   300-player seed, 20 D2 slots, 36 A Link to the Past slots:
    #   Fill.FillError: No more spots to place 4 items.
    #     Unplaced: Small Key / Big Key / Map (Hyrule Castle) (Player 85)
    #     at alttp stage_pre_fill -> fill_dungeons_restrictive
    #
    # AP runs every world's pre_fill FIRST and the per-game stage_pre_fill
    # hooks AFTER. ALttP fills its dungeon items into its dungeon locations in
    # that later stage — and those locations are prime sphere-1 candidates, so
    # 20 D2 slots taking up to 54 keys each had already eaten them. The same
    # applies to any world that reserves its own locations for a later stage;
    # there is no flag that says "reserved", so there is no way to see it
    # coming. It passed at 200 players and broke at 300, which is exactly how
    # this class of bug reaches real events and not our test seeds.
    #
    # Keys therefore stay in OUR world. The sphere guarantee gets stronger, not
    # weaker: a key placed in our own act-N region is reachable exactly when act
    # N is, with no dependency on any other world at all. What is lost is the
    # cosmetic pleasure of finding a D2 key in someone's Celeste run — and that
    # was never the requirement. The requirement was "never deeper than the act
    # it opens".
    report: list[str] = []
    handed_back: list = []
    placements: list = []

    for diff, act in sorted(groups):
        items = groups[(diff, act)]
        sphere = _sphere_label(diff, act)
        done = 0

        # ALL of the act's keys go against the act's OPENING state, not one after
        # another. The per-key variant - key 2 allowed to sit behind key 1, key 3
        # behind key 2, sub-spheres 1.1..1.4 nested inside sphere 1 - spreads the
        # keys more prettily through the act, and it is the better end state. It
        # is NOT shipped yet: threading the keys deeper made this slot's own
        # progression a longer serial chain, and AP's balance_multiworld_progression
        # then died with "Not all required items reachable" on 2 of 4 test seeds.
        # Marking the keys progression_skip_balancing did not settle it. Until
        # that interaction is understood, the act's keys stay at its door: a
        # little crowded, provably safe. See the checker in Tools for the numbers.
        candidates = [
            loc for loc in own_locations
            if loc.item is None
            and loc.progress_type is not LocationProgressType.EXCLUDED
            and loc.can_reach(state)
        ]
        multiworld.random.shuffle(candidates)

        # fill_restrictive pops what it places off `pool`, so whatever survives
        # the call is what it could not fit.
        pool = list(items)
        fill_restrictive(
            multiworld, state, candidates, pool,
            single_player_placement=True,   # our world only — see section 2
            lock=True,                      # the general fill must not move them
            swap=False,                     # see below
            allow_partial=True,             # overflow is reported, never fatal
            name=f"D2 gate keys P{player} sphere {sphere}",
        )
        # swap=False matters. With swapping on, fill may relocate an already
        # placed item to make room, and the destination it picks is NOT drawn
        # from `candidates` - so a key could be swapped out of the shallow set we
        # just computed and land a sphere deeper. Measured: exactly one key of 72
        # escaped that way. Without swapping a placement can only ever be a
        # location we vetted, and overflow is reported instead.

        # Item.__eq__ compares (name, player) and every copy of a progressive key
        # shares both, so identity is the only way to tell placed from not.
        leftover_ids = {id(it) for it in pool}
        for idx, it in enumerate(it for it in items if id(it) not in leftover_ids):
            placements.append((diff, act, idx + 1, it, it.location))
            state.collect(it, prevent_sweep=True)
            done += 1
        sweep_own()

        handed_back.extend([it for it in items if id(it) in leftover_ids])
        report.append(f"A{act}{DIFF_TAG[diff]} {done}/{len(items)} @ sphere {sphere}")

    if handed_back:
        multiworld.itempool.extend(handed_back)
        report.append(f"OVERFLOW {len(handed_back)} key(s) returned to the general pool")

    _verify(multiworld, player, own_locations, placements)
    world.keyfill_report = report


def _verify(multiworld, player, own_locations, placements) -> None:
    """Re-derive the whole chain from an empty state and prove every key was
    obtainable at the point it was placed.

    The loop above cannot produce a violation - that is the point of it - so this
    guards the NEXT edit, not this one. It is cheap, it runs on every generation,
    and it turns "someone changed the fill and nobody noticed for three releases"
    into a generation that stops.

    Raising is deliberate. A key that is not obtainable in its own chain is a
    dead seed for that slot, and a dead seed caught at generation time costs
    minutes; caught by a player it costs an evening (seed 36491296110575942106).
    """
    state = CollectionState(multiworld)

    def sweep():
        state.sweep_for_advancements(
            [loc for loc in own_locations
             if loc.item is None or loc.item.player == player])

    sweep()
    for diff, act, index, item, location in placements:
        if location is None or not location.can_reach(state):
            where = "unplaced" if location is None else f"{location.name} ({location.player})"
            raise RuntimeError(
                f"Diablo II Archipelago (player {player}): gate key placement broke "
                f"its own ordering. '{item.name}' copy {index} sits at {where}, which "
                f"is not reachable with the {index - 1} earlier key(s) of act {act} "
                f"{DIFF_TAG[diff]}. Fix keyfill.py rather than shipping this seed.")
        state.collect(item, prevent_sweep=True)
        sweep()
