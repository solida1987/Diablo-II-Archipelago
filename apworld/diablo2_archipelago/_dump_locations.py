"""Dump location_table from the apworld to d2_locations.json for the launcher's
standalone tracker. Stubs the AP BaseClasses import (locations.py only uses
ItemClassification as a value). Run from this folder: python _dump_locations.py

Emits TWO tables so the launcher tracker matches an AP DataPackage:
  - location_name_to_id : every location name -> AP id (all difficulties)
  - location_name_groups: { "Category": [names...] } so the tracker groups
    checks into the SAME categories AP would (Hunt / Clear Zones / Shrines / …)
    instead of dumping everything into a single "Other" bucket.

Categories are derived by AP-id range (the layout is documented in locations.py),
and quests are sub-classified by quest_id -> quest_type so Story / Hunt / Clear /
Exploration / Waypoints / Level Milestones each get their own card."""
import sys, os, json, types, importlib.util

bc = types.ModuleType("BaseClasses")
class ItemClassification:
    progression = 1
    useful = 2
    filler = 4
    trap = 8
    skip_balancing = 16
    progression_skip_balancing = 17
bc.ItemClassification = ItemClassification
sys.modules["BaseClasses"] = bc

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("d2locations", os.path.join(here, "locations.py"))
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

table = mod.location_table  # name -> ap id

# ── quest_id -> quest_type, so 42000-46999 quest ids classify correctly ──────
# Regular quests carry their type in ALL_ACT_LOCATIONS; level milestones live in
# the LEVEL_MILESTONES_* lists (treated as their own "level" type). Quest ids are
# globally unique across these sets (verified: hunts/kills never collide with
# milestone ids 78-83 / 180-184 / 282-285), so a flat map is unambiguous.
qid_type = {}
for act_locs in mod.ALL_ACT_LOCATIONS:
    for qid, name, qtype, cls in act_locs:
        qid_type[qid] = qtype
for lst in (mod.LEVEL_MILESTONES_NORMAL,
            mod.LEVEL_MILESTONES_NIGHTMARE,
            mod.LEVEL_MILESTONES_HELL):
    for entry in lst:
        qid_type[entry[0]] = "level"

QTYPE_GROUP = {
    "story":    "Story Quests",
    "hunt":     "Hunt",
    "kill":     "Clear Zones",
    "area":     "Exploration",
    "waypoint": "Waypoints",
    "level":    "Level Milestones",
}


def classify(locid: int) -> str:
    L = locid
    # Quests + level milestones: LOCATION_BASE 42000, id = base + qid + diff*1000
    if 42000 <= L < 47000:
        rel  = L - 42000
        diff = rel // 1000
        qid  = rel - diff * 1000
        return QTYPE_GROUP.get(qid_type.get(qid), "Story Quests")
    # Gate-boss kills (zone locking): 47000 + diff*1000 + act*10 + gate
    if 47000 <= L < 50000:  return "Zone Gates"
    # Collection (Goal=Collection): 50000-50109
    if 50000 <= L < 50200:  return "Collection"
    # Bonus object checks
    if 60000 <= L < 60150:  return "Shrines"            # 60000 + 0..149  (50×3)
    if 60200 <= L < 60500:  return "Urns"               # 60200 + 0..299  (100×3)
    if 60500 <= L < 60800:  return "Barrels"            # 60500 + 0..299  (100×3)
    if 60800 <= L < 61400:  return "Chests"             # 60800 + 0..599  (200×3)
    if 65000 <= L < 65100:  return "Gold Milestones"    # 65000 + 0..16
    if 65100 <= L < 65300:  return "Set Pickups"        # 65100 + 0..126
    # Extra checks
    if 65300 <= L < 65310:  return "Cow Level"          # 65300-65308
    if 65310 <= L < 65320:  return "Mercenary"          # 65310-65315
    if 65320 <= L < 65400:  return "Hellforge & Runes"  # 65320-65331
    if 65400 <= L < 65500:  return "NPC Dialogue"       # 65400-65480 (27×3)
    if 65500 <= L < 65600:  return "Runewords"          # 65500-65549
    if 65600 <= L < 65800:  return "Cube Recipes"       # 65600-65734
    return "Other"


groups: dict[str, list[str]] = {}
for name, locid in table.items():
    groups.setdefault(classify(int(locid)), []).append(name)

# Sort names inside each group for stable, readable output.
for g in groups:
    groups[g].sort()

data = {
    "game": "Diablo II Archipelago",
    "location_name_to_id": {name: int(i) for name, i in table.items()},
    "location_name_groups": groups,
}

dst = os.path.normpath(os.path.join(here, "..", "..", "Archipelago", "d2_locations.json"))
with open(dst, "w", encoding="utf-8") as f:
    json.dump(data, f, ensure_ascii=False, separators=(",", ":"))

print(f"wrote {len(table)} locations in {len(groups)} groups -> {dst}")
for g in sorted(groups, key=lambda k: (-len(groups[k]), k)):
    print(f"  {g:20s} {len(groups[g])}")
