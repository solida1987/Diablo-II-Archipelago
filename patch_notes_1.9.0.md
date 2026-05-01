# Diablo II Archipelago — Beta 1.9.0

**Headline features:** entrance shuffle, Pandemonium event with Hellfire
Torch, three new stackable stash tabs, F1 collection tracker with a new
Goal mode, 41 backported runewords, D2R 3.0 skill values, and 1494 new
optional AP check slots.

---

## Install / update

1. Download **launcher_package.zip** from this release
2. Extract anywhere, run **Diablo II Archipelago.exe**
3. Click **More info → Run anyway** on the SmartScreen warning
4. The launcher downloads the game for you

You need an existing Diablo II + Lord of Destruction install (Classic,
not Resurrected). Windows 10/11 with the .NET 8 Runtime.

---

## New features

### Entrance shuffle (new game mode)

A new title-screen toggle (`Entrance`) shuffles the entrances of every
dead-end cave and dungeon in the game. Walk into Den of Evil and you
might end up in Maggot Lair instead; exit returns you to your original
entrance.

- **Pool A** (mixed Act 1+2): Den of Evil, Cave, Crypt, Mausoleum,
  Hole, Pit, Tower Cellar, Tristram, A2 Sewers, Stony Tomb, Halls of
  the Dead, Maggot Lair, Ancient Tunnels, Claw Viper Temple, all 7 Tal
  Rasha tombs — 21 sets total
- **Pool B** (mixed Act 3+4+5 + Cow Level): Spider Cave/Cavern, Swampy
  Pit, Flayer Dungeon, A3 Sewers, 6 tome temples, Crystallized
  Cavern, Glacial Caves, Nihlathak chain, Cellar of Pity, Echo
  Chamber, Cow Level — 17 sets total
- Multi-floor dungeons (e.g. Cave L1+L2, Tower Cellar L1..L5) move as
  one set, so internal navigation stays vanilla
- Every shuffled cave uses Hell-difficulty room counts at every
  difficulty — Normal/Nightmare dungeons feel as big as Hell ones
- The seed is stable per character (from your AP slot or your name
  hash if you play standalone), so each character keeps the same map
  for life

If you die in a shuffled cave that physically lives in an act you
haven't reached yet, you're warped back to your highest-unlocked
town instead of getting stuck.

Available as the apworld YAML option `entrance_shuffle: true` so
multiworld hosts can enforce it.

### Pandemonium event (Uber Tristram in 1.10f)

The 1.11+ Pandemonium event is now playable in our 1.10f base. We
can't ship the original Pandemonium maps (they don't exist in 1.10f
and can't be redistributed), but the full gameplay loop is here.

**Items**

- 3 Pandemonium Keys (`pk1` Terror, `pk2` Hate, `pk3` Destruction)
  drop from random Hell-difficulty monsters in the event/cow loot table
- 3 Boss Organs (`mbr` Mephisto's Brain, `dhn` Diablo's Horn, `bey`
  Baal's Eye) drop from Hell Mephisto / Diablo / Baal — ~85% on
  first kill, ~50% on replays
- Hellfire Torch (`cm2`, large 2x3 charm) drops from the finale recipe

**Recipes** (all must be cubed **outside town** — transmuting in town
shows "Cannot use Pandemonium recipe in town" and refuses)

- **Mini Uber recipe**: cube the 3 keys → spawns Uber Lilith + Uber
  Duriel + Uber Izual within 5 tiles of you. Practice round.
- **Finale recipe**: cube the 3 organs → spawns Uber Mephisto + Uber
  Diablo + Uber Baal. Kill all three in the same fight to drop the
  Hellfire Torch.

All 6 Ubers are level 110 with greatly buffed HP, damage, and
resistances. Bring an endgame build.

The vanilla Cow Portal recipe (Wirt's Leg + Tome of TP) still works
in town as before.

A `Pand` tab in the Ctrl+V dev menu lets you spawn individual ubers
or full trios for practice without burning a recipe set.

### Stackable stash tabs (Consumables / Runes / Gems)

A second column of stash tabs appears next to the existing P column.
Three new gold tabs hold "stackable" items — anything where every
drop is interchangeable with every other drop of the same code.

- **CON — Consumables (39 cells)**: 5 healing pots, 5 mana pots,
  rejuvs, stamina/antidote/thawing, TP/ID scrolls + tomes, gas/oil
  pots, skeleton key, arrows + bolts, 3 uber keys, 3 boss organs,
  4 essences, Token of Absolution
- **RUN — Runes (33 cells)**: every rune El..Zod
- **GEM — Gems (35 cells)**: 7 colors × 5 grades

**How to use**

- **Deposit**: shift+right-click an item in your backpack while an
  STK tab is active. Mismatched items bounce.
- **Pickup**: left-click a populated cell — one item per click
  (vanilla "grab the whole stack" is intentionally not available).
  Cursor must be empty.
- Each cell stacks up to 999. Rejected drops flash a red border
  for 300ms; hovering a populated cell shows a tooltip with the
  item name and count.

Charms, throwing weapons with affixes, and quest items are excluded
because they have rolled per-instance state that would be lost in a
stack. 107 distinct stackable item types are supported.

The tabs respect the existing AP/SH dual-mode rule: STK_AP variants
are visible in AP mode, STK_SH in standalone (or post-goal AP).
Storage is per-character for AP tabs, account-wide for SH tabs.

### F1 Collection book — five new pages

A new always-on Pokédex tracker added to the F1 book. Press F1, use
the top tab bar to navigate to:

- **Sets I / II** — all 32 vanilla sets, with class-lock badges and
  per-piece progress
- **Runes** — all 33 runes (El..Zod)
- **Gems** — 5 grades × 7 colors
- **Specials & Gold** — 3 Pandemonium Keys, 3 Boss Organs, 3
  Essences, Hellfire Torch + lifetime gold counter

**Anti-cheat detection**: a slot only counts as collected when the
item was both (a) observed on the ground as a fresh world drop and
(b) seen in your inventory afterwards. Vendor purchases, cube
outputs, items already in inventory at character load, and items
withdrawn from any stash are tagged as "tainted" and never auto-mark.
Items dropped by other players in MP also fail the check.

Once collected, slots stay marked forever for that character — even
if you later sell, drop, or stash the item. Each first-find is
timestamped and shown in the cell's tooltip.

A search box at the top of every Collection page filters cells by
name. A 1.5-second gold-pulse animation plays on the cell when a new
slot is collected.

Items already in your stash from before 1.9.0 get auto-credited the
first time you open the tab they're in — no need to physically pick
them up again.

### New goal mode: Collection

Set `Goal=3` (or pick "Collection" on the title screen) and the
character wins when every targeted collection set, rune, gem, and
special is complete.

The apworld exposes 75 individual per-item toggles so YAML authors
can pick exactly what counts:

- 32 toggles for individual sets (`collect_set_civerbs`, etc.)
- 33 toggles for individual runes (`collect_rune_el` ..
  `collect_rune_zod`)
- 10 toggles for specials (3 keys, 3 organs, 3 essences, Torch)
- 1 toggle for all gems
- 1 lifetime-gold target (range 0..100M)

Class-locked sets the player can't equip are auto-deselected at
character creation, so an Amazon doesn't need to chase Sorc/Necro/etc.
sets to win.

In AP mode, every collected target also fires a multiworld check
that returns a filler item from another player. In standalone, each
collected target grants a flat 500g reward.

### Bonus check categories (six new opt-in categories)

Up to **1494 extra AP check slots** when all categories are enabled.
All filler-only — AP fill never places progression items here. Default
is OFF — opt in via title-screen toggles or YAML.

| Category | Quota / difficulty | Mechanic |
|---|---:|---|
| Shrines | 50 | escalating-chance roll on activation |
| Urns / Jars | 100 | smash to roll |
| Barrels | 100 | smash to roll |
| Chests | 200 | open to roll |
| Set Piece Pickups | up to 127 | first-time pickup |
| Gold Milestones | 17 | lifetime gold thresholds |

The escalating roll starts at 10% on attempt 1 and rises by 10% each
miss, hitting 100% on attempt 10. Average ~3.5 interactions per slot.
Goal scope is respected (a `full_normal` seed only pulls Normal-tier
slots).

In standalone mode each bonus slot pre-rolls a real reward at
character creation (gold, xp, traps, boss loot, charm/set/unique
drops) and emits it via the same pipeline as quest rewards. The
`d2arch_spoiler_<char>.txt` file lists every slot's pre-rolled reward.

Title-screen toggles: Shrines / Urns / Barrels / Chests / Set
Pickups / Gold MS — placed in column 4, fully repositionable via
`d2arch.ini`.

### 41 backported runewords

The mod's runeword pool grew from 64 → 105. Every backport is pure
data — no DLL rebuild needed. Existing characters can craft them
the next time they log in.

- **Patch 1.11 wave 1 — 7 runewords** (filled vanilla 1.10f
  placeholders): Bone, Enlightenment, Myth, Peace, Principle, Rain,
  Treachery
- **Patch 1.11 wave 2 — 21 runewords**: Brand, Edge, Faith, Harmony,
  Ice, Insight, Infinity, Obedience, Pride, Rift, Death,
  Destruction, **Grief**, Last Wish, Lawbringer, Oath, Fortitude,
  Dragon, Dream, Phoenix, Spirit
- **D2R 2.4 — 7 runewords**: Plague, Pattern, Mist, Obsession,
  Wisdom, Unbending Will, Flickering Flame
- **D2R 2.6 — 6 runewords**: Cure, Hearth, Hustle (armor + weapon),
  Mosaic, Metamorphosis. Mosaic and Metamorphosis are simplified
  because their D2R-specific charge/state mechanics don't exist in
  1.10f — power level is preserved via permanent stats.

Seventeen test recipes in the Cube let you convert vendor items
into specific runes / sockets so you can craft and try these
runewords without farming for hours (e.g. Tome of TP → Shael, Tome
of ID → Thul, Stamina pot → 3-socket Light Plate). All test
recipes are prefixed `TEST:` in their description.

### D2R 3.0 skill rebalance (210 skills)

All 210 vanilla character skills (Amazon / Assassin / Barbarian /
Druid / Necromancer / Paladin / Sorceress, 30 each) updated to D2R
patch 3.0 values: mana costs, per-level damage, hit-shift, synergy
percentages, and prerequisite chains.

Skills with multiple synergies that scale at different rates (e.g.
Holy Fire: Resist Fire +21%, Salvation +10%) now show the actual
per-synergy percentage in tooltips instead of one shared bonus.

D2R 2.4-3.0 skill renames are reflected in tooltips and the F1 Skill
Editor: Fire Ball → Fireball, Sword Mastery → Blade Mastery, Pole
Arm Mastery → Polearm Mastery, Sanctuary → Sanctuary Aura.

The Warlock class from D2R 3.0's "Reign of the Warlock" expansion is
deliberately not included — that would need new client/skill-tab
work beyond a data port.

### Token of Absolution

The 1.13c Token (`tof`) is now in 1.10f. Cube the token to reset all
skill and stat allocations — a no-quest-cost respec. Available from
the STK Consumables tab and the Ctrl+V dev menu.

### F1 book — full redesign

The book now has a horizontal tab bar across the top with 11 tabs:
**Skills · Overview · Quests · Zones · Sets I · Sets II · Runes ·
Gems · Special · Logbook · AP**. The Zones tab only shows when
zone-locking is enabled. Next/Back walk visible tabs in order.

- **NEW Overview page** — one-screen summary of every check the
  character is hunting (Quests, Skills, Bonus, Collection, Zones,
  Goal status). Skips lines for disabled categories.
- **F4 Zone Tracker folded into the book** as page 3 — the
  standalone modal still works, but pressing F4 now opens the book
  to the Zones tab. Per-act and per-difficulty filters, gate
  status badges, mouse-wheel scroll, AP key location detail panel.
- **F3 in-game tracker — new Checks block**: shows compact totals
  per category (`Q 12/40  S 6/40  C 8/205` / `B 23/1.7K  Z 2/18`).
  Categories suppressed when toggled off.
- **AP page Item Log** rewritten to show per-category check
  counts instead of one aggregated number.
- **Quest page** redesigned in Logbook style — plain text rows
  with status colors (green=Done, gold=selected, white=hover, tan
  default), right-aligned status column, tighter row height.
- **Uniform readable text color** — every dim-grey label across
  the F1 book is now tan instead of grey for contrast on the dark
  parchment.

### Live INI hot-reload (no relog needed)

`d2arch.ini` is now polled every 500 ms. Edit a layout coordinate or
toggle, save, and the change appears in-game on the next paint —
no need to relog the character. Works for the F1 book, F3 tracker,
F4 zones modal, notifications, search box, tooltip, and gold strip.
Page 0 (Skill Editor) is intentionally not hot-reloaded.

### `d2arch.ini` reorganized by screen

The INI is now grouped under clearly-labelled banners:

```
TITLE SCREEN — MAIN MENU
F1 SHARED                (book frame, top tab bar, Next/Back)
F1 PAGE 0 — SKILL EDITOR
F1 PAGE 1 — OVERVIEW
F1 PAGE 2 — QUESTS
F1 PAGE 3 — ZONES
F1 PAGES 4-8 — COLLECTION
F1 PAGE 9 — LOGBOOK
F1 PAGE 10 — AP CONNECTION + ITEM LOG
F2 QUEST LOG
F3 TRACKER + NOTIFICATIONS
F4 ZONES MODAL (fallback)
F8 STASH / SKILL TREE
```

No values changed. Search any banner to jump.

### New launcher (1.5.x)

The launcher chrome got a complete art and layout pass with the new
**Iron Frame** kit — stone-wall background, painted iron-bound page
chrome, gold action bar, and a procedurally-generated emerald flame
seam between the masthead and content area. Window grew from
950×580 → 1040×660.

A new **Settings & Logic Guide** page explains every YAML option,
the four Goal modes, and the AP fill / sphere logic — fetched from
the repo on each launch.

A **pre-launch installation verifier** runs when you click PLAY: it
checks every file in the manifest matches the expected size and
re-downloads the bad ones automatically. After 3 failed attempts in
a row it pops a warning explaining the most likely causes
(antivirus, SmartScreen, firewall) with mitigations.

### Quality of life

- **Tomes stack to 999** (Tome of Town Portal and Tome of Identify) —
  one tome lasts a character lifetime
- **Skeleton Key stacks to 99** (was 12)
- **Pandemonium Keys, Boss Organs, Essences** all stackable to 99
  (were non-stackable)
- **Dev menu (Ctrl+V) redesigned** — 3-column grid with section
  headers (CHARACTER / TRAPS / BOSS LOOT / RUNES / BASES / GOODIES /
  PAND). New options: +1000 Gold, +5 Stat Pts, +5 Skill Pts, +10
  Lvl XP, Heal Full, Unlock All Skills, all 33 runes split into
  pickup-sized batches, all 35 gems, 3-socket bases, recipe pots

---

## Bug fixes

### Game-breaking

- **Socketed items now persist across stash tab swap.** Runewords
  formed from runes in a stash tab no longer lost their socket
  fillers when the tab was swapped away — the parent item kept
  the runeword bit but stats reverted to base, and after a
  save/load round-trip the weapon could vanish entirely. Tested
  fix: Hustle Flamberge survives arbitrary tab swaps and logout
  cycles with all 3 runes intact.
- **AP Connect no longer flips characters into AP mode unless the
  server actually authenticates.** Previously a click that timed
  out or was made with an unreachable server still moved the
  character onto the AP1 stash tab and locked starting skills as
  if the YAML said `starting_skills=0`. Now you stay fully
  standalone unless authentication completes.
- **F1 click-through bug fixed.** Clicks at coordinates inside the
  legacy AP panel rect were silently firing Connect even mid-game
  with the stash open — testers accidentally flipped their
  characters onto AP settings while placing items.
- **Dev menu (Ctrl+V) no longer crashes** when opening the stash.
  A bad function-hook trampoline was corrupting registers on the
  shared trade-button dispatcher.
- **STK deposit no longer leaves a "ghost" item icon stuck on
  the cursor** — the item was correctly removed from inventory
  but stayed registered in the cursor's render list.

### AP / multiworld

- **Item delivery works in town again.** AP-delivered items were
  silently dropped if the player was standing in town. Now they
  always deliver — only spawned monster traps still require you
  to leave town first.
- **AP-delivered items are always usable at any character level.**
  Required level on every delivered item is set to 1, so a level
  10 character can equip a Sazabi's Mental Sheath (req level 34)
  the moment it arrives.
- **Loot quality fixed.** Set/unique items used to come through
  as low-tier basic items because the spawn used item-level 30.
  Now they spawn at item-level 85 — Sazabi's Mental Sheath
  arrives as the actual set item, boss loot tables roll their
  full signature drops.
- **AP self-release now updates local quest counters.** Releasing
  your own check (in-game or via WebSocket admin command) now
  marks the quest as complete in the F1 quest list and F3
  tracker counter, matching what the AP server sees.
- **Bonus check counters reflect the AP server view.** After a
  full release the Item Log shows `Shrines: 50/50` instead of
  `0/50`. Counters back-fill at character load by replaying the
  bridge's per-character dedup file.
- **F3 tracker is scope-aware.** A Goal=Normal character no
  longer shows totals like `B 584/1.4K` — it now correctly shows
  `B 584/584`.
- **Gate keys are correctly named in bridge logs.** Were logging
  as "Skill ID 1109" with nonsense names; now log as
  `Gate Key #N (Normal/NM/Hell)`.
- **F1 Zones page** has a new `[OPEN]` status (light gold) for
  gates whose key arrived via AP but whose boss is not yet
  killed — previously these showed as `[----]` Locked, which
  was misleading.
- **Filler distribution leak fix.** The "last category gets all
  remaining slots" rule in the apworld occasionally placed Reset
  Points in seeds with `skill_hunting=false`, or Trap items in
  seeds with `traps_enabled=false`. 0-weight categories are now
  stripped before the distribution loop.
- **Bonus check locations are EXCLUDED from progression fill.**
  AP fill never places skills or gate keys at slots whose
  escalating-chance roll might never trigger.
- **Goal=Collection no longer crashes apworld generation.**
  `goal=3` was overflowing a 3-element diff_names array; now
  treated as Normal-only difficulty scope for fill purposes
  while the DLL handles the actual collection-complete win.
- **Hell-zone access rules now apply** to always-open Act 5 zones
  (Halls of Anguish/Death/Vaught, Nihlathak) — previously a
  Normal Gate Key could be placed at e.g. "Clear Halls of
  Vaught (Hell)", soft-locking the Normal gate.
- **Bridge resync hack removed.** A short-lived mechanism to
  re-replay all items at character load was causing stackable
  filler (gold/XP/stat points) to re-apply on every login.

### F1 Collection / Goal=Collection

- **75 individual per-item toggles** replace the original 4
  all-or-nothing sets/runes/gems/specials toggles, with a full AP
  location/check pipeline so each collected item becomes a
  multiworld check.
- **Cube transmutes** of every kind (gem upgrades, runeword
  socketing, rerolls, Cow Portal, Pandemonium recipes) now
  correctly count toward the cube counter.
- **Inventory items pre-1.9.0** get a one-time grandfather pass
  on character load so they stay credited as collected.
- **Vendor and runeword and identify hooks** added — the
  Statistics page now has accurate `itemsBoughtFromVendor`,
  `itemsSoldToVendor`, `gambledItems`, `runewordsCreated`,
  and `itemsIdentified` counters.

### Bonus checks polish

- **Per-set toggles gate set piece pickups.** Pieces of sets the
  player toggled off via `collect_set_*` no longer count toward
  the 127 pickup checks.
- **Title-screen layout fully configurable.** Bonus toggles
  position via `d2arch.ini [layout] BonusX/Y/Spacing` (auto-stack)
  or per-button (`ShrinesX/Y`, `UrnsX/Y`, etc.). Closes a layout
  overlap bug some users hit with custom column positions.
- **Per-character toggle persistence.** The 6 bonus toggles now
  freeze per-character at creation and ride along with the
  character file — characters created before the user enabled
  bonus toggles in `d2arch.ini` no longer lose them on reload.
- **Skill Hunting OFF — skill pool persistence fixed.** A SH=OFF
  character correctly showed 0 hunted skills on first login but
  ballooned to all 210 on subsequent logins. The character's
  pool kind (hunt vs. class-only) is now saved alongside the
  assignments.

### Game settings

- **Per-character settings persistence** extended to cover the new
  `entrance_shuffle` flag. Settings freeze at character creation
  and restore on subsequent loads, same isolation pattern as
  existing toggles.

---

## Known issues

- **Quest triggers stay tied to the original cave.** If Den of Evil
  gets shuffled to e.g. Maggot Lair, killing all monsters in the
  Maggot Lair destination won't fire Den of Evil's quest. Act
  bosses still progress — only side-quest XP is lost. Accepted
  trade-off for v1.
- **Walking out of a shuffled cave** warps you to the natural
  surface zone's default spawn (waypoint or zone center), not to
  the cave's entrance position.
- **Hell-only Pandemonium event portals** (Matron's Den, Forgotten
  Sands, Furnace of Pain) are not in the entrance shuffle pool —
  shuffling them in would let low-level chars walk into Uber
  bosses. May add a Hell-only sub-pool later.
- **Hustle weapon's "Level 1 Fanaticism Aura" property** is
  intentionally dropped — the underlying skill-name lookup
  resolves to "an evil force" on 1.10f. The runeword's other
  bonuses are intact.
- **F1 Quest counter** may show 9 quests fewer than the expected
  total in some configurations — investigation deferred.
- **F1 Collection counter** (`C N/205`) counts only items you
  physically picked up — AP-delivered set/unique drops in
  inventory don't count. By design for now.
- All other known issues from 1.8.5 still apply unless noted above.

---

## Includes everything from 1.8.5 and earlier
