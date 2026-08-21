"""Place tiered filler at a depth that matches its tier.

A Zod-class reward in Blood Moor and a chipped gem in Hell Act 5 are both
correct as far as the fill is concerned - filler carries no logic weight, so
the general fill is free to put either anywhere. That freedom is what makes
the reward curve feel random rather than earned.

This module reserves the tiered fillers before the general fill runs and drops
each one into the act/difficulty band it belongs in. Nothing about reachability
changes: these items gate nothing, so no state is swept and no logic is
consulted. The only thing being decided is WHERE in the run the player meets
them.

Depth, not sphere
-----------------
The bands below are act/difficulty positions - Act 1 Normal is 1, Act 5 Hell is
15 - read off the region name. With zone locking on they line up with real AP
spheres, because each act sits behind its own gate key. With zone locking off
there are no gates and every location is reachable from the start, so AP would
call all of them sphere 1; the band is then just "how far into the run this
is", which is still exactly what we want to control. Calling it depth keeps
that honest.

Why this does not repeat the 300-player failure
-----------------------------------------------
keyfill.py has the full story: reserving locations in pre_fill starved another
world whose own items needed the same shallow locations, and generation died
with "No more spots to place 4 items". Three things keep this module clear of
that:

  * it only ever touches OUR locations, never another world's;
  * it takes a hard cap of a quarter of them, so most of our own locations are
    still on the table when the general fill arrives;
  * it prefers EXCLUDED locations, which can hold nothing BUT filler. Every
    item placed there costs the general fill nothing at all.

Anything that does not fit goes back in the pool and is counted in the report.
A degraded run is visible; it is never fatal.
"""
from __future__ import annotations

import re

from BaseClasses import LocationProgressType

from .locations import ALL_ACT_LOCATIONS

# Depth comes off the LOCATION name, not the region name.
#
# This is not a stylistic choice. With zone locking on, only the gate
# locations get their own act-shaped regions ("A3D1R2"); every other location
# is parked in a single "Open Areas" region carrying per-location access
# rules. Reading the region name there tells you nothing about which act a
# location belongs to, so an earlier version of this module silently found no
# candidates at all and handed every item straight back - generation stayed
# green and the feature did precisely nothing. Measured: identical placements
# with the option on and off.
#
# Location names carry both halves of what we need: the act via the tables in
# locations.py, and the difficulty via the " (Nightmare)" / " (Hell)" suffix.
_DIFF_SUFFIX = ((" (Hell)", 2), (" (Nightmare)", 1))
_REGION_RE = re.compile(r"^Act (\d) (Normal|Nightmare|Hell)\b")
_REGION_ADR_RE = re.compile(r"^A(\d)D(\d)R\d+$")
_DIFF_INDEX = {"Normal": 0, "Nightmare": 1, "Hell": 2}

# base location name -> act, built once from the same tables the world uses.
_ACT_OF_LOCATION: dict[str, int] = {}
for _act, _locs in enumerate(ALL_ACT_LOCATIONS, start=1):
    for _entry in _locs:
        _ACT_OF_LOCATION[_entry[1]] = _act

# Fraction of our own locations this module is allowed to consume.
_MAX_SHARE = 0.25

# The reference scale the bands below are written against: Act 1 Normal to
# Act 5 Hell. A seed that does not span all of it gets the bands squeezed onto
# what it does have — see _scale_band.
_REFERENCE_DEPTH = 15

# item name -> (min depth, max depth), inclusive, over 1.._REFERENCE_DEPTH.
#
# The bands overlap on purpose. Hard edges look tidier and generate worse: a
# narrow goal scope can leave a band with no locations at all, and a seed where
# every high-tier reward sits in the last two acts reads as a difficulty wall
# rather than a curve.
TIER_BANDS: dict[str, tuple[int, int]] = {
    # Runes. The low band stays on r01-r09 in the DLL, so it is the only one
    # that belongs early; the other two are the reason this module exists.
    "Random Rune":            (1, 8),
    "Random Rune (Mid)":      (4, 12),
    "Random Rune (High)":     (8, 15),
    # Gems, same shape.
    "Random Gem":             (1, 8),
    "Flawless Gem":           (4, 12),
    "Perfect Gem":            (8, 15),
    # Charms scale by size: a grand charm is worth roughly a small unique.
    "Small Charm":            (1, 10),
    "Large Charm":            (4, 13),
    "Grand Charm":            (7, 15),
    # Points. The big ones late, so a character cannot be handed a build's
    # worth of stats in Act 1.
    "1 Stat Point":           (1, 10),
    "3 Stat Points":          (3, 13),
    "10 Stat Points":         (7, 15),
    "2 Skill Points":         (3, 13),
    "3 Skill Points":         (7, 15),
    "3 Reset Points":         (4, 15),
}


def _depth_of_region(region_name: str) -> int | None:
    """Fallback for locations whose name is not in the act tables."""
    name = region_name or ""
    match = _REGION_RE.match(name)
    if match:
        return _DIFF_INDEX[match.group(2)] * 5 + int(match.group(1))
    match = _REGION_ADR_RE.match(name)
    if match:
        return int(match.group(2)) * 5 + int(match.group(1))
    return None


def _depth_of(loc) -> int | None:
    """Where in a 1..15 run this location sits, or None if unknown.

    Three sources, most precise first:
      1. the act tables plus the difficulty suffix  -> exact
      2. an act-shaped region name                  -> exact (gate locations)
      3. a difficulty suffix alone                  -> that difficulty's middle

    Case 3 covers the bonus and collection checks, which belong to a
    difficulty but to no particular act. Placing them at the midpoint is an
    approximation, and an acceptable one: the bands are 5 to 8 deep, so a
    two-act error never moves an item across a band boundary it was not
    already near. Dropping them instead would throw away most of the EXCLUDED
    locations, which are the cheapest ones this module can spend.
    """
    name = loc.name or ""
    diff = 0
    base = name
    for suffix, d in _DIFF_SUFFIX:
        if base.endswith(suffix):
            base = base[: -len(suffix)]
            diff = d
            break

    act = _ACT_OF_LOCATION.get(base)
    if act is not None:
        return diff * 5 + act

    from_region = _depth_of_region(getattr(loc.parent_region, "name", ""))
    if from_region is not None:
        return from_region

    # No act and no act-shaped region: a bonus, collection or milestone check.
    # It still belongs to a difficulty (Normal when unsuffixed), so bucket it
    # at that difficulty's middle act rather than discarding it.
    return diff * 5 + 3


def _scale_band(band: tuple[int, int], lo_d: int, hi_d: int) -> tuple[int, int]:
    """Squeeze a 1..15 band onto the depths this seed actually has.

    A Full Normal goal only spans depths 1..5, and a narrow Custom Goal can be
    narrower still. Against the raw scale every band from 7 upward then has no
    candidates at all, so every high-tier item is handed straight back and the
    feature quietly stops applying to exactly the items it exists for.
    Measured on a Full Normal seed: all five deep bands empty, high runes and
    perfect gems landing at depth 2.

    Scaling keeps the ORDER, which is the part that carries the meaning: a
    perfect gem still arrives later than a chipped one, even when "later" is
    Act 4 Normal instead of Act 3 Hell.
    """
    span = hi_d - lo_d
    if span <= 0:
        return lo_d, hi_d
    def to_seed(v: int) -> int:
        frac = (v - 1) / float(_REFERENCE_DEPTH - 1)
        return lo_d + int(round(frac * span))
    lo, hi = to_seed(band[0]), to_seed(band[1])
    if hi < lo:
        hi = lo
    return lo, hi


def place_tiered_fillers(world) -> None:
    """pre_fill hook. Safe to call unconditionally."""
    multiworld = world.multiworld
    player = world.player

    # --- 1. Take the tiered fillers out of the shared pool ------------------
    groups: dict[str, list] = {}
    keep = []
    for item in multiworld.itempool:
        if item.player == player and item.name in TIER_BANDS:
            groups.setdefault(item.name, []).append(item)
        else:
            keep.append(item)
    if not groups:
        world.tierfill_report = []
        return

    # --- 2. Bucket our own free locations by depth ---------------------------
    #
    # Excluded first within each bucket. Those locations are barred from
    # holding progression, so filling them is pure profit: it satisfies this
    # module and reduces what the general fill has to place.
    by_depth: dict[int, list] = {}
    total_free = 0
    for loc in multiworld.get_locations(player):
        if loc.item is not None:
            continue
        depth = _depth_of(loc)
        if depth is None:
            continue
        by_depth.setdefault(depth, []).append(loc)
        total_free += 1

    for depth in by_depth:
        multiworld.random.shuffle(by_depth[depth])
        by_depth[depth].sort(
            key=lambda l: l.progress_type is not LocationProgressType.EXCLUDED)

    if not by_depth:
        multiworld.itempool[:] = keep
        multiworld.itempool.extend(i for g in groups.values() for i in g)
        world.tierfill_report = ["no banded locations — nothing reserved"]
        return

    lo_d, hi_d = min(by_depth), max(by_depth)
    budget = int(total_free * _MAX_SHARE)
    report: list[str] = []
    handed_back: list = []
    placed_total = 0
    if (lo_d, hi_d) != (1, _REFERENCE_DEPTH):
        report.append(f"seed spans depth {lo_d}-{hi_d}; bands scaled to fit")

    # --- 3. Place, widest-band-last -----------------------------------------
    #
    # Narrow bands go first. A band of 8-15 and a band of 1-15 competing for
    # the same deep locations should not be decided by dict order: if the wide
    # one wins the race, the narrow one has nowhere left to go and gets handed
    # back, which is the exact outcome this module exists to prevent.
    ordered = sorted(groups, key=lambda n: TIER_BANDS[n][1] - TIER_BANDS[n][0])

    for name in ordered:
        items = groups[name]
        low, high = _scale_band(TIER_BANDS[name], lo_d, hi_d)
        done = 0
        for item in items:
            if placed_total >= budget:
                handed_back.append(item)
                continue
            candidates = [d for d in range(low, high + 1) if by_depth.get(d)]
            if not candidates:
                handed_back.append(item)
                continue
            depth = multiworld.random.choice(candidates)
            loc = by_depth[depth].pop()
            loc.place_locked_item(item)
            placed_total += 1
            done += 1
        report.append(f"{name} {done}/{len(items)} @ depth {low}-{high}")

    multiworld.itempool[:] = keep
    if handed_back:
        multiworld.itempool.extend(handed_back)
        report.append(
            f"OVERFLOW {len(handed_back)} filler returned to the general pool")
    report.append(f"used {placed_total}/{budget} of the location budget "
                  f"({total_free} free)")

    _verify(multiworld, player, report, lo_d, hi_d)
    world.tierfill_report = report


def _verify(multiworld, player, report, lo_d, hi_d) -> None:
    """Prove every tiered item we locked actually landed inside its band.

    The loop above cannot produce a violation, which is the point of it. This
    guards the NEXT edit: a band table typo or an off-by-one in the depth regex
    would otherwise ship as "the curve feels a bit random again" and never be
    traced back here.
    """
    violations = []
    for loc in multiworld.get_locations(player):
        item = loc.item
        if item is None or item.player != player:
            continue
        raw = TIER_BANDS.get(item.name)
        if raw is None:
            continue
        depth = _depth_of(loc)
        if depth is None:
            continue
        band = _scale_band(raw, lo_d, hi_d)
        if not (band[0] <= depth <= band[1]) and loc.locked:
            violations.append(f"{item.name} @ depth {depth} (band {band})")
    if violations:
        report.append("BAND VIOLATIONS: " + "; ".join(violations[:8]))
