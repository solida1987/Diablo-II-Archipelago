# Diablo II Archipelago — Settings & Logic Guide

This is the complete reference for every setting the apworld + in-game
panel exposes, with explanations of how they interact and how the
underlying Archipelago fill logic places items.

Read this if you want to understand:

- What each YAML setting actually does
- Which combinations make sense for solo vs multiworld
- How AP decides what items go where (sphere logic)
- What the four Goal modes mean for both gameplay and fill

---

## 0. Setting up the apworld + generating a YAML

If this is your first time, here's the order:

1. **Get the apworld.** Our launcher (Diablo II Archipelago.exe) writes
   `diablo2_archipelago.apworld` into the folder you set as
   "Archipelago Custom Worlds Folder" in the launcher's Settings page.
   Default is `%ProgramData%\Archipelago\custom_worlds\` if you haven't
   changed it. The file gets re-deployed every time you click PLAY,
   so it's always up to date with the DLL you're running.

2. **Drop the apworld into Archipelago.** If the launcher's custom-
   worlds folder is the same as Archipelago's `custom_worlds/`, you're
   already done. Otherwise, copy `diablo2_archipelago.apworld` into
   `<Archipelago install>\custom_worlds\` (or double-click the file —
   ArchipelagoLauncher.exe will install it for you).

3. **Generate your YAML.** Run `ArchipelagoLauncher.exe`, click
   **Options Creator**, pick "Diablo II Archipelago" from the dropdown,
   set the options you want (or paste a profile from section 1 below),
   and save the YAML to your `Players/` folder.

4. **Generate the multiworld.** Click **Generate** in the launcher, OR
   run `ArchipelagoGenerate.exe`. The output `.zip` lands in `output/`.

5. **Host or upload.** Solo: run `ArchipelagoServer.exe <output.zip>`.
   Multiworld: upload the zip at https://archipelago.gg/uploads.

6. **Connect from the game.** In the in-game F1 → AP page, type the
   server address (e.g. `archipelago.gg:38281`), your slot name from
   the YAML, and the room password if any. Wait for the GREEN
   indicator before clicking Single Player.

If you just want to try the mod without Archipelago, skip steps 3-6
and play in standalone mode — the DLL works without an AP server,
quest rewards just deliver locally instead of going to the multiworld.

---

## 1. Quick start — four preset profiles

If you don't want to read 30 pages, pick a profile:

### "2-hour async-friendly run"

```yaml
skill_hunting: true
zone_locking: false
goal: full_normal
xp_multiplier: 5
quest_hunting: true
quest_kill_zones: false
quest_exploration: false
quest_waypoints: true
quest_level_milestones: false
collect_set_*: false   # see Collection section to disable in bulk
collect_rune_*: false
collection_target_gems: false
```

Designed to be beatable in roughly 2 hours by an experienced player.
Story quests + waypoints + hunt quests stay on (these give you skills).
Kill-zone, exploration, and milestone quests are off so you don't have
to grind. Collection goal is off — your win condition is **kill Baal
Normal**. XP is 5× so you outscale the early acts. ~80 locations,
fast generation, no zone gating, no boss/monster shuffle. Best for
async multiworlds where one player's Diablo II can't hold up the rest.

### "Just play D2 with skill randomization" (default)

```yaml
skill_hunting: true
zone_locking: false
goal: full_normal
```

You play vanilla D2 progression (Acts 1→5, kill Baal Normal). Quests
hand out randomized skills from any class. ~223 locations, light AP
gating, fast generation. Best for solo.

### "Hardcore zone-locked progression"

```yaml
skill_hunting: true
zone_locking: true
goal: full_hell
```

Each act is split into 4-5 mini-regions ("R1..R5") gated by gate-keys
that boss-kills drop. You can't enter Stony Field until you've found
the Act 1 Gate 1 Key. 722 locations across all three difficulties.
~16-19 spheres of progression — feels like a metroidvania. Best for
zone explorers.

### "Pokédex completionist (Goal=Collection)"

```yaml
skill_hunting: true
zone_locking: false
goal: collection
collection_target_gems: true
collection_gold_target: 1000000
# Plus 75 individual collect_set_*, collect_rune_*, collect_special_*
# toggles to pick exactly which items count.
```

Win condition is "fill the F1 Collection book" (some subset of
sets/runes/gems/specials). Difficulty progression is optional.
Each toggled item that you collect fires an AP check. 333 locations
when all 75 toggles are on, fewer if you trim the list.

---

## 2. The two game-mode toggles

These are the most important settings. They stack — both can be ON.

### `skill_hunting` (default: `true`)

When ON, all 210 D2 skills (7 classes × 30 each) become items in the
multiworld pool. Quests hand out random skills from this pool, and
you start with **6 random precollected skills** (so you can survive
the first few minutes).

Skills are **classified as "useful"**, not "progression". They make
your character stronger but don't gate AP fill — you can win the
seed without them, just slower.

Effect on the pool:

- Skill items occupy ~204 of the ~223 default Normal locations
  (210 minus 6 starting) → very dense skill distribution
- If `zone_locking` is also ON, gate keys take priority and skills
  fill the rest
- Trap skills (Assassin Wake of Fire / Death Sentry / etc.) are
  **always excluded** to prevent a character-invisible bug

When OFF, skills stay tier-1/tier-2 progression items. Locations
that would have held skills still hold them, but AP fill treats them
as gating items. This makes the game more linear (you need specific
skills to unlock zones).

### `zone_locking` (default: `false`)

When ON, the world is gated by **18 boss-kill gate-keys per
difficulty** (4 per act except Act 4 which has 2). Each gate-boss
drop sends a check; receiving the gate key from the multiworld opens
the next mini-region in that act.

Per-act gate layout:

| Act | Gates | Mini-regions |
|-----|-------|--------------|
| 1   | 4     | R1..R5       |
| 2   | 4     | R1..R5       |
| 3   | 4     | R1..R5       |
| 4   | 2     | R1..R3       |
| 5   | 4     | R1..R5       |

Total: 18 gates × 3 difficulties = **54 gate-key items**.

Effect on the pool:

- Each gate kill becomes a location (54 total at full Hell)
- 18-54 progression items added (one key per played difficulty)
- AP fill places these strictly so each gate is reachable when
  needed — Normal Gate Keys land in starting region, Hell Gate Keys
  in later spheres, etc.

When OFF, no gate keys exist. You progress through D2 normally:
beat Andariel → Act 2, beat Duriel → Act 3, etc.

### Combined modes

| skill_hunting | zone_locking | What you get                                                                 |
|---------------|--------------|------------------------------------------------------------------------------|
| ON            | OFF          | Default: skill randomizer with light gating                                  |
| ON            | ON           | Hybrid: skills + zone gates (most progression, most spheres)                 |
| OFF           | ON           | Pure zone-key chase (fewer items, gate-keys are the only progression)        |
| OFF           | OFF          | Pure quest randomizer (skills as progression, no zone gates) — rarely useful |

---

## 3. Goal mode (`goal`)

Determines the win condition. Five options as of 1.9.2:

### `full_normal` (0) — beat Baal Normal

- Difficulty scope: Normal only
- Locations: ~223 (Acts 1-5 Normal)
- Win = "Eve of Destruction" location is reached

### `full_nightmare` (1) — beat Baal Normal AND Nightmare

- Difficulty scope: Normal + Nightmare
- Locations: ~446 (each location duplicated with `(Nightmare)` suffix)
- Win = "Eve of Destruction (Nightmare)" reached

### `full_hell` (2) — beat Baal on all 3 difficulties

- Difficulty scope: Normal + Nightmare + Hell
- Locations: ~668-722 depending on zone_locking
- Win = "Eve of Destruction (Hell)" reached
- Longest seed; most spheres

### `gold_collection` (3) — reach a lifetime-gold target

*Renamed in 1.9.2 from `collection`. The old name still parses as
an alias for backward compatibility.*

- Difficulty scope: Normal-only quest locations
- Locations: ~225 (the F1 Collection page no longer drives goal —
  Collection toggles below still control whether their items show
  in the F1 book and on the AP location list, but win condition is
  pure gold)
- Win = lifetime gold counter on F1 Collection page reaches the
  `collection_gold_target` setting (any value 0..100M)
- **Difficulty progression is optional** — play any difficulty
- Set `collection_gold_target` > 0 or the goal trivially completes

### `custom` (4) — build your own win condition (1.9.2)

AP-side only — standalone defaults to Full Normal because the
title-screen UI doesn't have room for the 54-checkbox picker.

- Pick any combination of 54 individual targets (10 subsystems,
  15 act bosses × diff, 7 cow king + ubers, 10 super-uniques,
  12 bulk object/check completions) plus an optional
  `custom_goal_gold_target`
- Goal completes when **ALL selected targets** are achieved AND
  lifetime gold reaches the optional gold target
- Empty selection + 0 gold = trivially complete (falls back to
  Full Normal behaviour)
- Difficulty scope: full pool (Normal+NM+Hell) generated for AP
  fill purposes since the user can pick targets across all 3 diffs
- See section 4b for the full target list

---

## 4. Gold Collection sub-settings

Only meaningful when `goal: gold_collection`.

### `collection_gold_target` (default: `1000000`)

Lifetime-gold threshold. Range: 0 to 100,000,000. The F1 Collection
page shows a monotonic gold counter that only counts gold pickup
(from ground) + quest-reward gold — vendor-sale revenue is
excluded. The goal requires reaching this value.

### `collection_target_gems` (moved 1.9.2)

In 1.9.2 this toggle moved out of the Goal section and into the
Collection — Gems group. It controls whether the 35 individual gem
pickups count toward the F1 Collection book + Custom Goal's
"include collection" subsystem. No longer a goal-mode-specific
setting.

---

## 4b. Custom Goal targets (1.9.2 — AP only)

Only meaningful when `goal: custom`. Each option below is a
standalone Toggle checkbox in the Options Creator. The goal
completes when **every checked target is achieved AND lifetime gold
reaches `custom_goal_gold_target`**.

The 54 toggles are organised into 5 collapsed groups in the
Options Creator UI:

### Subsystem includes (10 toggles)

Bulk completion gates — each one means "win requires the entire
subsystem to be complete":

- `custom_goal_subsystem_skill_hunting` — unlock every skill in
  your seeded skill pool (up to 210)
- `custom_goal_subsystem_collection` — fill the F1 Collection book
  per the existing collect_set_*/collect_rune_*/collect_special_*/
  gems toggles. Mix of items required is configured in those
  collection groups (so you can combine "Custom Goal: include
  Collection" with "only collect 5 specific runes")
- `custom_goal_subsystem_hunt_quests` — complete every Hunt
  super-unique quest across all 3 difficulties (~42 total)
- `custom_goal_subsystem_kill_zone_quests` — complete every
  kill-zone quest across all 3 difficulties
- `custom_goal_subsystem_exploration_quests` — every "Reach <area>"
  exploration quest across all 3 difficulties
- `custom_goal_subsystem_waypoints` — activate every waypoint
  across all 3 difficulties (~38 × 3 = 114)
- `custom_goal_subsystem_level_milestones` — reach every level
  milestone (5/10/15/.../99) across all 3 difficulties
- `custom_goal_subsystem_story_normal/nightmare/hell` — three
  separate toggles, one per difficulty, requiring every story
  quest on that difficulty (Den of Evil through Eve of Destruction)

### Act Boss kills (15 toggles)

`custom_goal_kill_<boss>_<diff>` for each of the 15
combinations:

- Andariel Normal/Nightmare/Hell
- Duriel Normal/Nightmare/Hell
- Mephisto Normal/Nightmare/Hell
- Diablo Normal/Nightmare/Hell
- Baal Normal/Nightmare/Hell

### Cow King + Pandemonium ubers (7 toggles)

- `custom_goal_kill_cow_king_normal/nightmare/hell` — Cow King in
  the Moo Moo Farm per difficulty
- `custom_goal_kill_uber_mephisto/diablo/baal` — individual Uber
  kills in the Pandemonium event
- `custom_goal_hellfire_torch_complete` — complete one full
  Pandemonium run (all 3 ubers + Hellfire Torch drop)

### Famous Super-Uniques (10 toggles)

`custom_goal_kill_<su>` for popular super-uniques:

Bishibosh (Cold Plains), Corpsefire (Den of Evil),
Rakanishu (Stony Field), Griswold (Tristram),
Pindleskin (Nihlathak's Temple), Nihlathak (Halls of Vaught),
The Summoner (Arcane Sanctuary), Radament (Sewers Level 3),
Izual (Plains of Despair), Council Member (Travincal)

### Bulk object/check completions (12 toggles)

For when you want "complete every X check" as your goal. Requires
the matching `check_*` toggle (see sections 9b/9c) to be ON or the
counters never bump:

- `custom_goal_all_shrines/urns/barrels/chests/set_pickups/
  gold_milestones` (1.9.0 bonus check categories)
- `custom_goal_all_cow_level_checks/merc_milestones/
  hellforge_runes/npc_dialogue/runeword_crafting/cube_recipes`
  (1.9.2 extra check categories)

### `custom_goal_gold_target` (default: `0`)

Range 0..100M. Optional gold threshold ON TOP of every selected
target. Set to 0 if you don't want gold to gate the goal.

### Win logic

`custom goal complete = (all required targets fired) AND
(lifetime gold >= custom_goal_gold_target)`. Empty target set + 0
gold = trivially complete = behaves like Full Normal (DLL falls
through to standard goal handling).

### `collect_set_*` (32 toggles, default: all `true`)

One toggle per vanilla set:

- 16 Classic sets: Civerb's, Hsarus, Cleglaw's, Iratha's, Isenhart's,
  Vidala's, Milabrega's, Cathan's, Tancred's, Sigon's, Infernal,
  Berserker's, Death's, Angelical, Arctic, Arcanna's
- 7 Class-locked LoD sets: Natalya's [Asn], Aldur's [Dru],
  Immortal King [Bar], Tal Rasha's [Sor], Griswold's [Pal],
  Trang-Oul's [Nec], M'avina's [Ama]
- 9 Generic LoD sets: Disciple, Heaven's Brethren, Orphan's Call,
  Hwanin's, Sazabi's, Bul-Kathos', Cow King's, Naj's, McAuley's

The set-collection check fires when the **whole set** is complete
(last piece picked up). Partial sets don't fire.

If you play a non-Assassin class, leaving Natalya's Odium ON means
you'll never finish that set on this character — the apworld won't
auto-disable class-locked sets. The DLL `Coll_AutoDeselectClassLockedSets()`
helper does this in standalone mode but not in AP-driven mode.

### `collect_rune_*` (33 toggles, default: all `true`)

One toggle per rune from El (lowest) to Zod (highest). The check
fires when you first pick up that specific rune. Note that some
runes are very rare drops (Cham, Zod) — leaving them ON makes the
seed long.

### `collect_special_*` (10 toggles, default: all `true`)

- `pk1` Key of Terror, `pk2` Key of Hate, `pk3` Key of Destruction
- `mbr` Mephisto's Brain, `dhn` Diablo's Horn, `bey` Baal's Eye
- `tes` Twisted Essence (Lord De Seis), `ceh` Charged Essence (Diablo),
  `bet` Burning Essence (Baal)
- `cm2` Hellfire Torch (the Pandemonium event reward)

Pandemonium Keys / Boss Organs / Essences are part of the Uber
Tristram event — see `patch_notes_1.9.0.md` for the recipe chain.

### Standalone mode override

When NOT connected to an AP server, the DLL ignores the YAML
toggles and treats **every collection slot** as a target — and
each pickup grants 500 gold as a built-in reward. The YAML toggles
only matter when generating a multiworld.

---

## 5. Quest type toggles

D2 has 5 categories of optional quests on top of the 39 main story
quests. Each toggle includes/excludes that whole category from the
location pool.

### `quest_hunting` (default: `true`)

SuperUnique boss hunts: Corpsefire, Bishibosh, Bonebreaker, etc.
**45 hunts × number of difficulties**. The Smith spawns in Act 1
Barracks (not Act 3 like in some patches). Hunts are progression
items because they reward skills/keys.

### `quest_kill_zones` (default: `true`)

"Clear X" zones — kill all monsters in a given area. Zones with
broken D2MOO pathfinding (Sewers L1/L2, Kurast Sewers L1/L2) are
removed. **~62 kill quests × difficulties**. These are filler-tier.

### `quest_exploration` (default: `true`)

"Enter X" zones — first time you set foot in an area. **~25 entries
× difficulties**. Filler-tier.

### `quest_waypoints` (default: `true`)

Waypoint activation. Waypoints with broken assignments (Sewers,
Frozen River) removed. **~27 waypoints × difficulties**. Filler-tier.

### `quest_level_milestones` (default: `true`)

"Reach Level X" milestones at 5/10/15/20/30 (Normal),
35/40/45/50/55 (Nightmare), 60/65/70/75 (Hell). **14 milestones**
total. Each has an access rule requiring an act-boss kill at the
appropriate difficulty so AP fill doesn't block early progression
on a high-level grind.

### Always-on: story quests

The 39 main-story quests (1 per quest tab × 6 quests × 5 acts +
adjustments) are **always included regardless of toggles** — the D2
engine requires them for progression and dialogue.

### Story-only mode

Setting all 5 quest toggles to `false` gives you ~27 locations
(story quests only). Useful for short test seeds or "speedrun
practice" mode. Skill pool gets capped to fit.

---

## 6. Skill class filter

### `skill_class_filter` (default: `all_classes`)

- `all_classes`: full 210-skill pool (minus Assassin traps)
- `custom`: only selected classes via the 7 `include_*` toggles below

### `include_amazon`, `include_sorceress`, `include_necromancer`, `include_paladin`, `include_barbarian`, `include_druid`, `include_assassin`

Each contributes 30 skills. Selecting fewer classes gives a smaller,
more focused pool — useful for class-themed runs.

If `skill_class_filter: custom` and ALL 7 toggles are off, the
apworld silently falls back to all_classes (avoids zero-skill seed).

**Assassin traps are always excluded**, even when Assassin is ON.
This is a 1.10f bug workaround — playing Wake of Fire on a non-Asn
class makes the character invisible.

### Pool sizing

The fill algorithm caps the pool so total items ≤ total locations.
If you pick 1 class (30 skills) but have 668 Hell-difficulty
locations, the rest of the locations get filler items.

If you pick 7 classes (210 skills) but only have 27 story-only
locations, only 27 skills make it into the pool — the rest are
discarded.

---

## 7. XP multiplier (`xp_multiplier`)

Range: 1-10. Multiplies XP gains from monster kills.

- 1 = vanilla 1.10f rates (very grindy at high levels)
- 5 = comfortable middle ground
- 10 = very fast leveling, useful for testing or short seeds

Affects only XP from kills — quest-reward XP and shrine XP are
vanilla.

---

## 8. Shuffles

### `monster_shuffle` (default: `false`) — **EXPERIMENTAL / unbalanced**

Shuffles monster types across zones at a per-character seed. Each
character gets a different layout. SuperUnique bosses keep their
zones (so hunting quests still work) but normal monster types swap.

⚠️ **Experimental:** difficulty scaling is currently uneven — you
can hit Hell-tier monster groups in Act 1 zones, or trivial Act 1
monsters in Act 5. Will be balanced before the stable release.

### `boss_shuffle` (default: `false`) — **EXPERIMENTAL**

Shuffles SuperUnique boss placements. Combined with monster_shuffle
this can radically change the feel of each act.

⚠️ **Experimental:** bosses keep their location's stats AND
resistances (e.g. whoever replaces Andariel inherits her -50% fire
resist). Only the boss's skills change. Some pairings can be
brutal — Act 1 Baal-clone keeps Baal's stats; Act 2 Diablo in the
tiny Duriel arena is very dangerous. Pair with `entrance_shuffle:
true` so you can grind higher-level zones for gear before the
gnarly fights.

### `entrance_shuffle` (default: `false`)

Shuffles the entrances of dead-end caves. Pool A (Acts 1+2) and
Pool B (Acts 3+4+5) are shuffled independently so progression stays
solvable. Town entrances and required story zones are excluded.

Same per-character determinism as the other shuffles — frozen at
character creation, baked into the per-character state file, AP
reconnects can't change it mid-run.

All three toggles are deterministic per character — a re-roll of
the same character gets the same shuffle.

---

## 9. Filler rewards (the loot pool)

Every quest location that doesn't carry a progression item (skill /
gate key) is filled with a "filler" reward. The 1.9.0 redesign
expanded the filler catalog from 8 generic items to 17 typed items
that the spoiler file can name precisely (which boss's loot, which
trap variant, which set/unique gets dropped).

### `traps_enabled` (default: `true`)

When ON, the four trap variants (`Monsters`, `Slow`, `Weaken`,
`Poison`) can land in the pool. When OFF, all four are removed and
the share is redistributed proportionally to the other filler types.

### Skill Hunting gates Reset Points

`Reset Point` (used to swap skills in/out of the player's skill pool)
only enters the pool when `skill_hunting=true`. With Skill Hunting
OFF there's no randomized pool to swap from, so Reset Points would
be inert — the apworld zeroes their weight and redistributes.

### Default weight table

Each row's weight is normalized against the sum of all enabled rows.
Specific magnitudes (gold amount, XP amount, which charm/set/unique
drops) are pre-rolled by the DLL at character creation and recorded
in the per-character spoiler file (`Game/Save/d2arch_spoiler_<char>.txt`).

Calibrated 2026-05-01: gold lowered, traps halved, drops tripled per
playtest feedback.

| Item | Weight | SH=ON % | SH=OFF % |
|---|---:|---:|---:|
| Gold (1-10K random) | 10 | 11.6% | 12.3% |
| Experience (1-250K random) | 15 | 17.4% | 18.5% |
| 5 Stat Points | 10 | 11.6% | 12.3% |
| Skill Point | 10 | 11.6% | 12.3% |
| Reset Point | 5 | 5.8% | **0%** (gated) |
| Trap: Monsters | 2 | 2.3% | 2.5% |
| Trap: Slow (Decrepify) | 1 | 1.2% | 1.2% |
| Trap: Weaken (Amp Damage) | 1 | 1.2% | 1.2% |
| Trap: Poison | 1 | 1.2% | 1.2% |
| Drop: Andariel Loot | 1 | 1.2% | 1.2% |
| Drop: Duriel Loot | 2 | 2.3% | 2.5% |
| Drop: Mephisto Loot | 2 | 2.3% | 2.5% |
| Drop: Diablo Loot | 1 | 1.2% | 1.2% |
| Drop: Baal Loot | 1 | 1.2% | 1.2% |
| Drop: Random Charm | 9 | 10.5% | 11.1% |
| Drop: Random Set Item | 9 | 10.5% | 11.1% |
| Drop: Random Unique | 6 | 7.0% | 7.4% |

When `traps_enabled=false` all four `Trap:` rows go to 0 and their
share is redistributed proportionally to the rest. Same for Reset
Point with `skill_hunting=false`.

### What you actually get

- **Gold** delivers a uniform random amount 1-10000 (DLL pre-rolls per
  location). The amount appears in the standalone spoiler file.
- **Experience** delivers 1-250000 XP via the same `fnAddStat` path
  the XP Multiplier feature uses.
- **5 Stat Points** / **Skill Point** / **Reset Point** are server-
  pending — applied to the character on the next safe tick.
- **Trap variants** trigger their respective effect (monster spawn,
  Decrepify, Amplify Damage, or Poison status).
- **Boss Loot drops** roll the named boss's Treasure Class at the
  player's level + 5 (ilvl floor 30). Capped 5 in queue with 3-second
  cooldown so accumulated drops don't carpet the floor.
- **Random Charm / Set / Unique** drops pre-roll a specific item at
  character creation. Items spawn unidentified — the player picks them
  up and ID's them like a real monster drop.

---

## 9b. Bonus check categories (1.9.0 — opt-in)

Six new categories add up to ~1494 extra AP locations driven by object
interactions, item pickups, and lifetime gold. All filler-only — AP
fill never places progression items here, so unfilled slots cannot
soft-lock the run. All default to `false` (opt-in).

### `check_shrines` / `check_urns` / `check_barrels` / `check_chests`

Per-difficulty quotas: shrines 50, urns 100, barrels 100, chests 200.

Each interaction (smashing an urn, activating a shrine, etc.) rolls an
escalating chance against the active slot's attempt counter:
- attempt 1 -> 10%, attempt 2 -> 20%, ..., attempt 10 -> 100% (guaranteed)
- Reset to 10% after each hit
- Average ~3.5 interactions per check, max 10

Once a category's per-difficulty quota is reached, no more rolls happen
for that category at that difficulty.

Goal-scope respects the active goal: `full_normal` only includes Normal
quotas; `full_nightmare` adds Nightmare; `full_hell` adds Hell.

### `check_set_pickups`

Up to 127 first-time-pickup checks for individual set pieces. Respects
the 32 per-set `collect_set_*` toggles (pieces of disabled sets don't
count). Works in any goal mode (not just Goal=Collection).

### `check_gold_milestones`

17 lifetime-gold thresholds:
- Normal (7): 10K, 100K, 200K, 400K, 800K, 1.8M, 3M
- Nightmare (5): 3.5M, 4M, 4.5M, 5M, 6M
- Hell (5): 7M, 8M, 9M, 10M, 12M

Triggered by the existing lifetime-gold counter (the same one Goal=
Collection uses for its `collection_gold_target`).

### Pool size impact

With all 6 toggles enabled and `goal=full_hell`, the location pool
grows by ~1494 locations (~3x the default ~500). Multiworld generation
takes ~30s longer per slot; other players' worlds end up filled with
more D2 fillers. Default OFF so the AP namespace doesn't bloat unless
the player opts in.

### Title-screen UI

6 new toggle buttons in column 2 of the title-screen settings panel:
Shrines / Urns / Barrels / Chests / Set Pickups / Gold MS. Persist
via `d2arch.ini [settings]` `Check*` keys for standalone. AP slot_data
overrides at character creation.

### F1 Statistics page

Page 8 (Statistics) gained an "AP CHECK PROGRESS" section that shows:
- Per-difficulty slot count for each enabled object category
- Set-piece pickup count (X / 127)
- Gold-milestone count + next threshold

Visible only when at least one bonus category is enabled.

### F1 Page 7 (Gold tab) milestone strip

When `check_gold_milestones=true`, the Collection page's Gold tab adds
a 4-column grid below the goal-target line listing all 17 milestones.
`✓` for milestones already crossed, `·` for remaining. Compact format
(`✓ 10K`, `✓ 100K`, `· 1.8M`, ...).

### Standalone bonus rewards

In standalone mode, every bonus slot pre-rolls a specific reward at
character creation using the same weighted catalog the per-quest
rewards use (gold 1-10K, xp 1-250K, traps, boss loot, charm/set/
unique drops). When a bonus check fires (e.g. you smash a shrine and
the escalating-chance roll consumes slot 5), the pre-rolled reward
gets delivered and a notification shows what you got.

The standalone spoiler file (`Game/Save/d2arch_spoiler_<char>.txt`)
appends a "Bonus Check Rewards" section after the per-quest table —
listing all 1494 bonus slot rewards so you can see what each shrine /
urn / barrel / chest / set piece / gold milestone will give before
you trigger it. Re-rolled deterministically from the character seed
on every character load.

In AP mode, the bonus locations get filled by the multiworld fill
algorithm (always filler-only — they're flagged EXCLUDED to prevent
progression items landing at slots whose escalating-chance roll
might never consume them).

### Title-screen layout customization

The 6 bonus toggle buttons each read their own X/Y from
`d2arch.ini [layout]`:

```ini
; Default — buttons stack from BonusX/BonusY using BonusSpacing
BonusX=460
BonusY=180
BonusSpacing=30

; Per-button override (uncomment to use)
; ShrinesX=460
; ShrinesY=180
; UrnsX=460
; UrnsY=210
; BarrelsX=460
; BarrelsY=240
; ChestsX=460
; ChestsY=270
; SetPickupsX=460
; SetPickupsY=300
; GoldMSX=460
; GoldMSY=330
```

Default places them in a 4th column at X=460 to avoid overlapping
the existing class+shuffle stack on the left and the quest+collection
stack on the right.

---

## 9c. Extra check categories (1.9.2 — opt-in)

Six MORE check categories on top of the 1.9.0 bonus checks. Each is
independently toggleable from the title screen and the apworld YAML.
All default OFF so existing characters don't get surprise side-quests
when they update.

### `check_cow_level` (9 slots, AP IDs 65300-65308)

Adds nine AP locations covering the Moo Moo Farm:

- **First entry per difficulty (3 slots)** — fired the first time you
  step into the Cow Level on Normal / Nightmare / Hell.
- **Cow King kill per difficulty (3 slots)** — fired when you kill the
  Cow King super-unique on each difficulty.
- **Lifetime cow-kill milestones (3 slots)** — fired when your
  character has killed 100 / 500 / 1,000 Hell Bovines (any difficulty).

In standalone mode each fire grants a flat 1,000 gold reward.

### `check_merc_milestones` (6 slots, 65310-65315)

Six AP locations rewarding mercenary investment:

- **First Mercenary Hired (1 slot)** — fired the first time you hire
  a merc from any of the four NPCs (Kashya / Greiz / Asheara / Qual-Kehk).
- **Resurrects 5 / 10 / 25 / 50 (4 slots)** — fired at each lifetime
  resurrection threshold.
- **Mercenary Reaches Level 30 (1 slot)** — fired the first time we
  observe `fnGetStat(pMerc, 12, 0) >= 30` on the per-tick merc poll.

### `check_hellforge_runes` (12 slots, 65320-65331)

Twelve AP locations covering Hellforge use and high-rune drops:

- **Hellforge Used per difficulty (3 slots)** — fired when you smash
  Mephisto's soulstone on the Hellforge in Hell Act 4 on each
  difficulty.
- **High Rune drops per tier per difficulty (9 slots)** — fired the
  first time you observe a rune from each of three tiers in your
  inventory:
  - Tier 0 = Pul / Um / Mal / Ist / Gul (r21–r25)
  - Tier 1 = Vex / Ohm / Lo / Sur / Ber (r26–r30)
  - Tier 2 = Jah / Cham / Zod (r31–r33)
  Each tier × each difficulty = 9 slots.

### `check_npc_dialogue` (81 slots, 65400-65480)

First dialogue with each major NPC across 3 difficulties (27 NPCs ×
3 = 81 slots). The NPC roster matches the act vendor list:

- **Act 1**: Akara, Charsi, Gheed, Kashya, Warriv, Cain
- **Act 2**: Atma, Drognan, Elzix, Fara, Greiz, Lysander, Meshif, Jerhyn
- **Act 3**: Alkor, Asheara, Hratli, Ormus, Cain (A3)
- **Act 4**: Tyrael, Halbu, Jamella
- **Act 5**: Anya, Larzuk, Malah, Nihlathak, Qual-Kehk

Detection runs per game tick — when D2Client UIVar 0x06 (the active
NPC dialogue unit pointer) transitions to a new NPC, we look up
its MonStats hcIdx and map to the npcIdx. Only the 27 NPCs above
fire checks; minor NPCs (act guards, decorative villagers) are
ignored. Cain's six hcIdx variants across acts fold into two
logical slots (A1 Cain pre-rescue, A3+ Cain post-rescue).

### `check_runeword_crafting` (50 slots, 65500-65549)

Sequential 1st through 50th runeword craft. Detection hangs off
the existing `Coll_ProcessItem` IFLAG_RUNEWORD 0→1 transition —
when a runeword is completed, we fire the next sequential slot.
The internal counter persists across saves so a game crash doesn't
restart it.

### `check_cube_recipes` (135 slots, 65600-65734)

Sequential 1st through 135th successful Horadric Cube transmute.
Detection extends the existing `TradeBtn_Hook` on case 24
(TRADEBTN_TRANSMUTE) — the trampoline returns non-zero on a
matched recipe (zero on no-match), so failed transmutes don't
bump the counter. Like runewords, the counter persists across saves.

### Summary

| Toggle key | Slots | Range | Detection |
|---|---|---|---|
| `check_cow_level` | 9 | 65300-65308 | Area enter / Cow King kill / lifetime kill counter |
| `check_merc_milestones` | 6 | 65310-65315 | Per-tick merc poll (hire / unitId-change / level-30) |
| `check_hellforge_runes` | 12 | 65320-65331 | OperateHandler case 49 / Coll_ProcessItem rune slot |
| `check_npc_dialogue` | 81 | 65400-65480 | Per-tick UIVar 0x06 poll, hcIdx → NPC mapping |
| `check_runeword_crafting` | 50 | 65500-65549 | Coll_ProcessItem IFLAG_RUNEWORD 0→1 transition |
| `check_cube_recipes` | 135 | 65600-65734 | TradeBtn_Hook on successful TRANSMUTE |

Total: **293 new locations** when all six are enabled. Combined with
the 1.9.0 bonus categories you can push past 2,100 total locations
on a single character.

The F1 Overview page gets a new "EXTRA CHECKS" section that shows
your progress per enabled category, and the Item Log on the AP page
appends the same rows to its "Total Checks" sum.

---

## 10. Death link (`death_link`)

Standard AP DeathLink. When ON, your character dying broadcasts a
death event to other DeathLink-enabled players in the multiworld;
their characters die too. When OFF, deaths stay local.

This is purely a multiworld setting — has no effect in solo seeds.

---

## 11. How AP fill works (sphere logic)

When you generate a multiworld, AP runs a **fill algorithm** that
places every item into a location while respecting access rules.
Spheres are the output: sphere 0 is "items you have at start",
sphere 1 is "items reachable using only sphere-0 items", etc.

### Region structure

The apworld defines **regions** that group locations. Different
modes use different region trees:

**Skill Hunt mode (zone_locking=false):**
```
Menu -> Act 1 -> Act 2 -> Act 3 -> Act 4 -> Act 5
```
Act-to-act transitions require the previous act's boss kill (any
difficulty for goal=NM/Hell). All locations within an act go to
that act's region.

**Zone Lock mode (zone_locking=true):**
```
Menu -> Open Areas (host)
       └── Each location has a per-location access rule
Menu -> A1D0R1 -> A1D0R2 -> A1D0R3 -> A1D0R4 -> A1D0R5
   (gate-1 key) (gate-2 key) (gate-3) (gate-4)
Menu -> A1D1R1 -> ... (Nightmare, after Normal Baal)
Menu -> A1D2R1 -> ... (Hell, after NM Baal)
... and similar trees for Acts 2-5
```

Quest locations sit in `Open Areas` with custom access rules that
mirror the gate-key requirements. Gate-kill check locations sit in
the gate region they unlock.

### Access rules at a glance

For a location at (act=N, region=R, diff=D), the access rule is:

- All gate keys (G=1..R-1) at difficulty D
- Previous-act boss kill at difficulty D (act-transition)
- Previous-difficulty Baal kill (difficulty-transition)

**1.9.0 fix**: zones in `ALWAYS_OPEN_ZONES` (Halls of Anguish/Death/
Vaught, Nihlathak's Temple) now also get diff-only / prev-act-only
gating. Without this, AP fill could put a Normal Gate Key at
"Clear Halls of Vaught (Hell)" and softlock the seed. Verified fixed
across the 17-test suite.

### Item classification

- **Progression**: gates other content. AP fill places these first
  in early spheres. (Gate keys, story quests when not skill-hunting.)
- **Useful**: nice to have but optional. (Skills in skill_hunting
  mode.) Filled after progression.
- **Filler**: gold, stat points, etc. Filled last to round out the
  pool.
- **Trap**: like filler but causes a negative effect.

### Sphere counts (typical)

| Config | Spheres |
|---|---|
| Skill Hunt + Normal | 1 (everything reachable from start) |
| Skill Hunt + Hell | 1 (boss-connections still loose) |
| ZoneLock + Normal | 5 |
| ZoneLock + NM | 7-10 |
| ZoneLock + Hell | 16-19 |
| 10-player MW (mixed) | 30-40 |

Sphere=1 doesn't mean "broken" — it means there are no progression
locks (skills are useful, not gating). The seed is still winnable
because the goal location is reachable.

---

## 12. Multiworld behavior

In a multiworld, the apworld inserts D2 items into other slots'
locations and vice versa.

### Item flow

- D2 progression items (gate keys when ZoneLock=ON, story-quest
  rewards when SkillHunt=OFF) can land in **any** other slot
- D2 filler items (gold, stat points) can also land elsewhere
- D2 skills (when SkillHunt=ON) are useful items — they can land
  elsewhere but aren't required for win

### Pacing concerns

If you mix slot configs like:

- Slot A: SkillHunt + Hell goal
- Slot B: ZoneLock + Normal goal

AP fill may place Slot B's "Act 1 Gate 2 Key (Normal)" at one of
Slot A's Hell-difficulty locations. AP's logic considers it
"reachable" because Slot A reaches Hell through their own progression,
but in real time Slot B may be stuck in Act 1 R2 for hours waiting
for Slot A to reach Hell.

**Mitigation**: set `progression_balancing: 80-99` on each slot
that depends on others. AP will then push gate keys earlier.

### Tested combinations

The 1.9.0 release validated:

- 4-player MW (D2 + ChecksFinder + VVVVVV + Yacht Dice) — 7 spheres
- 9-player MW with 6 D2 variants (default/Hell/Collection/MinimalColl/
  Runes/ZoneLock) + 3 other games — 9 spheres
- 10-player MW with 2 D2 (default + Hell+Zone) + 8 other games
  (Hollow Knight, Stardew Valley, A Hat in Time, etc.) — 34 spheres

All seeds generated successfully with no fill exceptions.

---

## 13. Recommended setups

### Solo, beginner-friendly
```yaml
skill_hunting: true
zone_locking: false
goal: full_normal
xp_multiplier: 5
traps_enabled: true
```
~30-60 minute seed. Easy entry, lots of filler reward.

### Solo, full game
```yaml
skill_hunting: true
zone_locking: true
goal: full_hell
xp_multiplier: 3
traps_enabled: true
```
Multi-day seed. 722 locations, 16-19 spheres of metroidvania
progression. Recommended progression-balancing default (50).

### Solo, completionist
```yaml
skill_hunting: true
zone_locking: false
goal: collection
collection_target_gems: true
collection_gold_target: 0
# Disable class-locked sets if not playing those classes
```
Open-ended; play at your own pace. The DLL fires goal-complete the
moment your collection book is full.

### 4-8 player multiworld, balanced
- Half the players: `goal: full_normal` (faster)
- Half: `goal: full_hell` (longer)
- Mix `zone_locking` on/off
- All set `progression_balancing: 80`

### "Skill themed" run
```yaml
skill_class_filter: custom
include_sorceress: true
include_paladin: true
include_barbarian: true
# rest false
```
Sorc/Pal/Bar themed pool — 90 skills. Forces those classes' kits.

### Speedrun practice
```yaml
quest_hunting: true
quest_kill_zones: false
quest_exploration: false
quest_waypoints: false
quest_level_milestones: false
```
Story + hunts only (~140 locations). Less side-grinding.

---

## 14. Settings reference table

| Setting | Type | Default | Range / Options | Affects |
|---|---|---|---|---|
| `skill_hunting` | toggle | true | true/false | Skill randomization mode |
| `zone_locking` | toggle | false | true/false | Gate-key progression |
| `goal` | choice | full_normal | full_normal/full_nightmare/full_hell/gold_collection/custom | Win condition (1.9.2 added `custom`) |
| `collection_target_gems` | toggle | true | true/false | Gems count toward F1 Collection book + custom_goal subsystem_collection |
| `collection_gold_target` | range | 1000000 | 0..100M | Lifetime gold target (only for goal=gold_collection) |
| `custom_goal_gold_target` | range | 0 | 0..100M | 1.9.2 — Lifetime gold req on top of custom goal targets |
| `custom_goal_subsystem_*` (10) | toggle | false | true/false | 1.9.2 — Custom goal: include skill_hunting / collection / hunt_quests / kill_zone_quests / exploration_quests / waypoints / level_milestones / story_normal / story_nightmare / story_hell |
| `custom_goal_kill_<boss>_<diff>` (15) | toggle | false | true/false | 1.9.2 — Custom goal act-boss kills (Andariel/Duriel/Mephisto/Diablo/Baal × 3 diff) |
| `custom_goal_kill_cow_king_<diff>` (3) | toggle | false | true/false | 1.9.2 — Custom goal Cow King × 3 diff |
| `custom_goal_kill_uber_<name>` (3) | toggle | false | true/false | 1.9.2 — Custom goal Uber Mephisto/Diablo/Baal |
| `custom_goal_hellfire_torch_complete` | toggle | false | true/false | 1.9.2 — Custom goal full Pandemonium run |
| `custom_goal_kill_<su>` (10) | toggle | false | true/false | 1.9.2 — Custom goal Bishibosh/Corpsefire/Rakanishu/Griswold/Pindleskin/Nihlathak/Summoner/Radament/Izual/Council |
| `custom_goal_all_<bonus>` (6) | toggle | false | true/false | 1.9.2 — Custom goal bulk bonus check completions |
| `custom_goal_all_<extra>` (6) | toggle | false | true/false | 1.9.2 — Custom goal bulk extra check completions |
| `collect_set_*` (32) | toggle | true | true/false | Per-set Collection target |
| `collect_rune_*` (33) | toggle | true | true/false | Per-rune Collection target |
| `collect_special_*` (10) | toggle | true | true/false | Per-special Collection target |
| `quest_hunting` | toggle | true | true/false | Hunt quests in pool |
| `quest_kill_zones` | toggle | true | true/false | Kill quests in pool |
| `quest_exploration` | toggle | true | true/false | Area quests in pool |
| `quest_waypoints` | toggle | true | true/false | Waypoint quests in pool |
| `quest_level_milestones` | toggle | true | true/false | Level milestones in pool |
| `skill_class_filter` | choice | all_classes | all_classes/custom | Skill pool composition |
| `include_amazon` | toggle | true | true/false | Custom-mode class toggle |
| `include_sorceress` | toggle | true | true/false | Custom-mode class toggle |
| `include_necromancer` | toggle | true | true/false | Custom-mode class toggle |
| `include_paladin` | toggle | true | true/false | Custom-mode class toggle |
| `include_barbarian` | toggle | true | true/false | Custom-mode class toggle |
| `include_druid` | toggle | true | true/false | Custom-mode class toggle |
| `include_assassin` | toggle | true | true/false | Custom-mode class toggle |
| `xp_multiplier` | range | 1 | 1..10 | XP gain rate |
| `monster_shuffle` | toggle | false | true/false | Per-character monster type shuffle |
| `boss_shuffle` | toggle | false | true/false | Per-character SuperUnique boss shuffle |
| `traps_enabled` | toggle | true | true/false | Trap filler items |
| `check_shrines` | toggle | false | true/false | 1.9.0 — 50 shrine checks/diff |
| `check_urns` | toggle | false | true/false | 1.9.0 — 100 urn checks/diff |
| `check_barrels` | toggle | false | true/false | 1.9.0 — 100 barrel checks/diff |
| `check_chests` | toggle | false | true/false | 1.9.0 — 200 chest checks/diff |
| `check_set_pickups` | toggle | false | true/false | 1.9.0 — 127 set-piece checks |
| `check_gold_milestones` | toggle | false | true/false | 1.9.0 — 17 gold-milestone checks |
| `check_cow_level` | toggle | false | true/false | 1.9.2 — 9 cow-level slots |
| `check_merc_milestones` | toggle | false | true/false | 1.9.2 — 6 mercenary slots |
| `check_hellforge_runes` | toggle | false | true/false | 1.9.2 — 12 Hellforge+High Rune slots |
| `check_npc_dialogue` | toggle | false | true/false | 1.9.2 — 81 NPC slots (DLL detection shipped in 1.9.2) |
| `check_runeword_crafting` | toggle | false | true/false | 1.9.2 — 50 runeword slots (DLL detection shipped in 1.9.2) |
| `check_cube_recipes` | toggle | false | true/false | 1.9.2 — 135 cube slots (DLL detection shipped in 1.9.2) |
| `death_link` | toggle | false | true/false | DeathLink broadcast |

### AP-internal settings (set by Archipelago itself, not user)

| Setting | Range | Effect |
|---|---|---|
| `progression_balancing` | 0..99 | Higher = progression items pushed earlier in spheres |
| `accessibility` | full / minimal / locations | Whether all checks must be reachable |

---

## 15. Item ID ranges (for reference)

These are the AP item-ID and location-ID ranges D2 uses. Useful if
you're writing a tracker or a custom client.

### Items

| Range | Type | Count |
|---|---|---|
| 45000-45209 | Skill items (D2 skill_id offset) | 210 |
| 45500-45507 | Filler items | 8 |
| 46001-46035 | Zone keys (legacy mode, deprecated) | 35 |
| 46101-46118 | Gate Keys Normal (Act × Gate slot) | 18 |
| 46121-46138 | Gate Keys Nightmare | 18 |
| 46141-46158 | Gate Keys Hell | 18 |

### Locations

| Range | Type | Count |
|---|---|---|
| 42000-42xxx | Normal-difficulty quests | ~99 |
| 43000-43xxx | Nightmare-difficulty quests (offset +1000) | ~99 |
| 44000-44xxx | Hell-difficulty quests (offset +2000) | ~99 |
| 47000-47053 | Gate-clear locations Normal | 18 |
| 48000-48053 | Gate-clear locations Nightmare | 18 |
| 49000-49053 | Gate-clear locations Hell | 18 |
| 50000-50031 | Collection: 32 sets | 32 |
| 50032-50064 | Collection: 33 runes | 33 |
| 50065-50099 | Collection: 35 gems | 35 |
| 50100-50109 | Collection: 10 specials | 10 |
| 60000-60149 | 1.9.0 — Shrines (50 × 3 diff) | 150 |
| 60200-60499 | 1.9.0 — Urns (100 × 3 diff) | 300 |
| 60500-60799 | 1.9.0 — Barrels (100 × 3 diff) | 300 |
| 60800-61399 | 1.9.0 — Chests (200 × 3 diff) | 600 |
| 65000-65016 | 1.9.0 — Gold milestones (7+5+5) | 17 |
| 65100-65226 | 1.9.0 — Set piece pickups | 127 |
| 65300-65308 | 1.9.2 — Cow Level expansion | 9 |
| 65310-65315 | 1.9.2 — Mercenary milestones | 6 |
| 65320-65331 | 1.9.2 — Hellforge + High Runes | 12 |
| 65400-65480 | 1.9.2 — NPC Dialogue (27 × 3 diff) | 81 |
| 65500-65549 | 1.9.2 — Runeword Crafting | 50 |
| 65600-65734 | 1.9.2 — Cube Recipes | 135 |

---

## 16. FAQ

**Q: Why does my Goal=Collection seed show only 1 sphere?**
A: Collection mode treats difficulty scope as Normal-only for AP
fill. The actual goal-complete check fires from the DLL when your
collection book is full — AP can't model "you have to collect 110
specific items" as access rules, so it uses Eve of Destruction
Normal as a placeholder reachable goal location. The single sphere
is correct — there's no progression to gate.

**Q: I set `zone_locking: true` but I can walk into any zone in-game.**
A: The gate-key system is enforced by the DLL (the AP bridge
forwards each gate-key item to the game). If the AP server isn't
connected, the DLL falls back to standalone mode and ignores the
gate-key requirement. Make sure your launcher shows GREEN before
starting Single Player.

**Q: Why was my Collection: Hellfire Torch never collected?**
A: The Hellfire Torch only drops from the Pandemonium event Phase 2
(after killing all 3 ubers in Hell). You need to assemble the
Pandemonium chain first. See `patch_notes_1.9.0.md` Pandemonium
section.

**Q: Can I change Goal mid-character?**
A: No. The Goal is **frozen at character creation** (per
`feedback_settings_isolation.md`). To switch goals, make a new
character — the new char will read your current YAML/INI settings.

**Q: What if I disable all 7 class toggles in Custom mode?**
A: The apworld silently falls back to All Classes. You won't get a
zero-skill seed.

**Q: Does the apworld need internet to generate?**
A: No, generation is fully offline (just `ArchipelagoGenerate.exe
--player_files_path`). Internet is only needed for hosting the
multiworld server during play.

**Q: Where did the apworld go? I can't find it after the latest update.**
A: Our launcher writes it to whichever folder you set as
"Archipelago Custom Worlds Folder" in the launcher's Settings page.
Default is `%ProgramData%\Archipelago\custom_worlds\`. The file
gets re-deployed every time you click PLAY. Generating a YAML still
works the normal Archipelago way: `ArchipelagoLauncher.exe →
Options Creator → save to Players/ → Generate`. See section 0
above for the full step-by-step.

**Q: Can this game be finished in ~2 hours?**
A: Yes, with the right settings. Use the "2-hour async-friendly run"
profile in section 1 — `goal: full_normal`, `xp_multiplier: 5`,
disable kill-zone / exploration / milestone quests, no Collection
goal. Skill Hunting stays on. ~80 locations, fast generation, no
zone gating. Fits comfortably in a 1.5-2.5 hour window for an
experienced player; longer if you're rusty.

**Q: What's the maximum XP multiplier safe for vanilla balance?**
A: 5x is a comfortable middle ground; 10x makes everything past
Act 2 trivial. Keep at 1-3x if you want vanilla difficulty pacing.

---

## 17. Related documents

- `patch_notes_1.9.0.md` — full release notes for the 1.9.0 cycle
- `feedback_collection_design.md` — design rationale for the F1
  Collection feature
- `feedback_zone_locking_arch.md` — zone-locking gate-boss / kill
  persistence architecture
- `feedback_settings_isolation.md` — per-character settings freeze
  rules
- `Research/SYSTEM_2_F1_COLLECTION_PAGE_2026-04-30.md` — Collection
  page implementation spec
- `Research/STK_MULTITAB_FULL_RECIPE_2026-04-29.md` — STK stash
  internals (related but separate from this guide)

---

*Document version: 1.9.5 | Last updated: 2026-05-11*
*For questions or bug reports: see the Discord link in the launcher.*
