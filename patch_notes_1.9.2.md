# Diablo II Archipelago — Beta 1.9.2

In development (no public release yet — version files still read
"Beta 1.9.1"). The big one this cycle: six new opt-in AP check
categories on top of the 1.9.0 bonus checks, push the location budget
past 2,100 with everything enabled. Plus a developer-menu Mons tab and
a smarter monster shuffle.

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

### Six new check categories (opt-in)

Six independent toggleable categories add up to 293 new AP locations.
Each category has its own ON/OFF button on the title screen and its own
option in the apworld YAML. All are OFF by default — turn on only what
you want. Standalone players can also flip them in `Game/Archipelago/
d2arch.ini` under `[settings]`.

- **Cow Level** (9 slots) — first entry to the Moo Moo Farm per
  difficulty (3), Cow King kills per difficulty (3), and lifetime
  cow-kill milestones at 100 / 500 / 1000 (3).

- **Mercenary Milestones** (6 slots) — first hire (1), 5 / 10 / 25 / 50
  lifetime resurrections (4), first time your merc reaches level 30 or
  higher (1).

- **Hellforge & High Runes** (12 slots) — using the Hellforge per
  difficulty (3) and first pickup of each high-rune tier (Pul-Gul,
  Vex-Ber, Jah-Zod) per difficulty (9).

- **NPC Dialogue** (81 slots) — first dialogue with each major NPC
  (Akara, Charsi, Gheed, Kashya, Cain, Atma, Drognan, Fara, Lysander,
  Asheara, Hratli, Ormus, Tyrael, Halbu, Jamella, Anya, Larzuk, Malah,
  Nihlathak, Qual-Kehk, Warriv, Meshif, Jerhyn, Greiz, Elzix, Alkor)
  per difficulty (27 NPCs × 3 difficulties = 81 slots). Detection runs
  per game tick — when the active NPC dialogue (D2Client UIVar 0x06)
  transitions to a new NPC unit, we look up its hcIdx and fire the
  matching slot.

- **Runeword Crafting** (50 slots) — fired sequentially (1st, 2nd, ...,
  50th) on each successful runeword craft, detected via the existing
  IFLAG_RUNEWORD 0→1 transition in `Coll_ProcessItem`. The counter
  persists across saves so a game crash doesn't restart it.

- **Cube Recipes** (135 slots) — fired sequentially (1st, 2nd, ...,
  135th) on each successful Horadric Cube transmute, detected via the
  existing TradeBtn_Hook on case 24 (TRADEBTN_TRANSMUTE) when the
  trampoline returns non-zero (recipe matched). Failed transmutes
  (no matching recipe) don't bump the counter.

The F1 Overview gets a new **Extra Checks** section that mirrors the
Bonus Checks section: one row per enabled category showing
`got / total`, with the totals folded into the page-bottom Total
counter. The F1 Logbook (page 9) gets the same row block.

In standalone (no AP) mode each slot delivers a per-character
pre-rolled reward (gold / XP / stat / skill / reset / trap /
boss-loot / charm / set / unique drop) using the same weighted
catalog as the quest + bonus pre-rolls. The reward is listed in the
per-character spoiler file (`Game/Save/d2arch_spoiler_<character>.txt`)
so you can preview what each slot will grant before triggering it.
In multiworld mode, the check is sent to the AP server like any
other location.

### Dev menu — Mons tab

Press Ctrl+V → click the new **Mons** tab (rightmost) for individual-
monster spawning grouped under four sub-tabs:

- **SuperUniques** — scrollable list of all 66 vanilla SuperUniques,
  click one to spawn at your cursor
- **Bosses** — 5 buttons for the act bosses (Andariel, Duriel,
  Mephisto, Diablo, Baal)
- **Normal** — scrollable list of all 400 player-killable monsters
  from MonStats.txt, click one to spawn
- **Random** — placeholder for upcoming weighted-roll spawn buttons

Both scrollable lists support mouse-wheel scrolling. Spawn buttons
route through the existing uber-spawn pipeline so the menu also
serves as a test bench for monster behavior tuning.

### Smarter monster shuffle

Monster shuffle (when enabled in your YAML) now respects a runtime ban
list of about 205 monster rows that were previously eligible to swap
out and would crash the game or break quest progression. Filtered
categories include:

- Spawner monsters (maggot mothers, fetish shamans, Lister's pack)
  whose AI scripts spawn child units that don't exist in the new slot
- Quest bosses tied to specific scripts (Mephisto, Diablo, Baal,
  Lilith, Izual, Hephasto, Council members)
- Special-AI / terrain-bound monsters (Worldstone Keep guards, Tyrael's
  shrine attackers, dummies)
- Player pets / summons (Necromancer skeletons, Druid wolves, golems)
- NPCs and vendors (Cain, Akara, Charsi, etc.)
- Decoration / unused placeholder rows

Belt-and-suspenders with the existing data baked into the 20 shuffle
preset `.dat` files — both layers are active so the runtime filter
catches anything that slipped through the data layer.

---

## Bug fixes

(Fixes accumulate here as bug reports come in.)

---

## Known issues carried from 1.9.1

- The skill description text still shows the `r-tier` placeholder for
  some unique-class skills — needs MPQ string-extraction work which is
  scheduled for a later release.

- The F1 Quest counter on some characters shows 9 fewer than expected;
  the discrepancy is cosmetic only (the underlying quest state is
  correct).

- F1 Collection counter (`C N/205`) does not count items received from
  the AP server — only items physically picked up. This is intentional
  for now; an inventory-pickup hook may be added later.
