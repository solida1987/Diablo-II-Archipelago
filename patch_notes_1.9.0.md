# Diablo II Archipelago - Beta 1.9.0

## Download
Download **launcher_package.zip**, extract to a folder, run **Diablo II Archipelago.exe**.
The launcher downloads and installs the game automatically.

## IMPORTANT: Windows SmartScreen
Click **"More info"** then **"Run anyway"** on the blue warning screen.

## Requirements
- Windows 10/11, .NET 8 Runtime
- An existing Diablo II + Lord of Destruction installation (Classic, NOT Resurrected)

## New Features in 1.9.0

### System 1 — Dead-End Cave Entrance Shuffle (NEW GAME MODE)
A new title-screen toggle `Entrance` shuffles entrances to all
dead-end caves and dungeons across the entire game. Walking into
"Den of Evil" might lead you to a different cave, and exiting that
cave returns you to where you originally clicked. Multi-floor
dungeons (Cave L1+L2, Tower Cellar L1..L5, etc.) move as a single
set so internal navigation stays vanilla.

**Pool A — Act 1+2 mixed (21 sets):**
- Den of Evil, Cave, Crypt, Mausoleum, Hole, Pit, Tower Cellar
- Tristram (portal), A2 Sewers, Stony Tomb, Halls of the Dead
- Maggot Lair, Ancient Tunnels, Claw Viper Temple
- All 7 Tal Rasha tombs

**Pool B — Act 3+4+5 mixed + Cow Level (17 sets):**
- Spider Cave, Spider Cavern, Swampy Pit, Flayer Dungeon
- A3 Sewers, 6 tome temples (Ruined Temple, Disused Fane, Forgotten
  Reliquary, Forgotten Temple, Ruined Fane, Disused Reliquary)
- Crystallized Cavern, Glacial Caves, Nihlathak chain
- Cow Level (constraint-based: only swaps with a non-quest Act 5 cave)
- Cellar of Pity, Echo Chamber

**How it works:**
- Mechanism: teleport-based using `LEVEL_WarpUnit` (same engine
  function as zone-locking) — works across acts.
- Sattolo permutation guarantees no fixed points (every cave
  always swaps to a different cave).
- Seed derived from AP slot_data when connected, or from character
  name hash for non-AP characters (so each character gets a unique
  shuffle that's stable per character).
- All maze dungeons use Hell-difficulty room counts in every
  difficulty (bigger maps everywhere — Normal/Nightmare dungeons
  feel grand instead of cramped).

### System 1 — Act-Town Lock (death-respawn safety)
If you die in a shuffled cave that physically lives in a higher act
than you've progressed to (e.g., shuffled into Maggot Lair from
Den of Evil's entrance, then die), D2 normally respawns you in that
act's town (Lut Gholein). The new act-town lock catches this: if
you haven't killed the prerequisite act boss, you're auto-warped
back to your highest-unlocked act's town with a notification.

Boss prerequisites:
- Act 2 town (Lut Gholein) requires Andariel killed
- Act 3 town (Kurast Docktown) requires Duriel killed
- Act 4 town (Pandemonium Fortress) requires Mephisto killed
- Act 5 town (Harrogath) requires Diablo killed

### Title Screen
- New `Entrance` toggle button in column 2 (between `Boss` and `Traps`)
- Persists via `d2arch.ini [settings] EntranceShuffle=`

### AP Integration
- New apworld option `entrance_shuffle: Toggle` (default false)
- Slot data field `entrance_shuffle` propagates to client
- Title-screen UI mirrors AP value when connected

### Pandemonium Event (Uber Tristram)
The Pandemonium Event from D2 1.11+ is now playable in 1.10f. No
authentic Pandemonium maps (those level files don't exist in 1.10f
and can't be redistributed), but the full gameplay loop is here:
hunt organs from Hell bosses, cube the recipes outside town, fight
the uber bosses, claim the Hellfire Torch.

#### Items

**Pandemonium Keys** (drop from Hell-difficulty random monsters,
event/cow level loot table — same source as 1.11+):
- `pk1` Key of Terror
- `pk2` Key of Hate
- `pk3` Key of Destruction

**Boss Organs** (drop from vanilla Hell-difficulty Prime Evils):
- `mbr` Mephisto's Brain — drops from **Hell Mephisto** (Durance L3)
  - First-kill quest drop: ~85% chance across the 7 boss picks
  - Replays: ~50% per kill
- `dhn` Diablo's Horn — drops from **Hell Diablo** (Chaos Sanctum)
  - First-kill: ~85%, replays: ~50%
- `bey` Baal's Eye — drops from **Hell Baal** (Throne of Destruction)
  - First-kill: ~85%, replays: ~50%

**Hellfire Torch** (`cm2`) — large 2x3 charm. Drops from the finale
recipe completion (see *Phase 2 Hellfire Torch drop* below).

#### Cube Recipes

All Pandemonium recipes only work **outside town**. Transmuting in
any town pops the on-screen notice "Cannot use Pandemonium recipe in
town - leave town first" and the cube refuses to consume the inputs.
This mirrors how 1.11+ scripts the recipe to fail in town.

**Mini Uber Recipe** (`pk1 + pk2 + pk3` cubed outside town):
The 3 keys are consumed and **Uber Lilith + Uber Duriel + Uber Izual**
spawn within 5 tiles of the player. They aggro immediately. Defeating
them in a single fight is the practice round before the finale.

**Finale Recipe** (`dhn + mbr + bey` cubed outside town):
The 3 organs are consumed and **Uber Mephisto + Uber Diablo + Uber Baal**
spawn within 5 tiles of the player. All three must die in this fight
to drop the Hellfire Torch.

**Vanilla Cow Portal recipe** still works exactly as before — Wirt's
Leg + Tome of Town Portal cubed in town opens the Cow Level portal.
The Pandemonium hook only activates when the cube contains the 3-key
or 3-organ combination; everything else falls through to the original
engine path.

#### Phase 2 — Hellfire Torch drop (NOW LIVE)

When all 3 prime evils from the Finale recipe die in the same
encounter, a Hellfire Torch (`cm2` Charm Large unique with random
class skill +1-6, +10-20 all stats, +10-20 all res, +8 light radius,
hit-skill Hydra) drops at the position of the last-killed boss with
a "Hellfire Torch has appeared!" notification.

Implementation: `d2arch_ubers.c` was moved earlier in the unity-build
include order so its `Ubers_OnUnitDeathScan` callback is reachable from
the unit-death walk in `d2arch_gameloop.c`. The walk's `txtId<700`
filter would otherwise skip our ubers (rows 704-712); a dedicated
branch checks the uber MonStats range and routes dead units into the
existing `u_uberList` / `u_finaleSpawnedCount` tracking. On the third
finale-set kill, `Ubers_FinaleAllDead` returns TRUE and
`Ubers_DropHellfireTorch` calls `QUESTS_CreateItem` with code `cm2`
at item-level 99, quality 7 (UNIQUE) — matching the existing cheat
menu path that's been used for testing since Phase 1. The unique
table's only `cm2` row is the Hellfire Torch.

Limitations:
- In-session only. Killing 1 of the 3 finale ubers, exiting, and
  restarting the game loses the in-memory tracking. Re-cube the recipe
  to get a fresh trio.
- Chain-cubing two finale recipes and killing all 6 ubers at once
  drops only 1 torch (the cumulative finale counter resets when it
  reaches the spawned count, not per trio).

Fallback: the Ctrl+V cheat menu's "Hellfire Torch" button still works
for testing builds against the ubers without grinding the recipe.

#### Uber Stats

All six ubers are level 110 with greatly buffed HP/damage/resistances
(per the new MonStats rows). Plan to bring an endgame Hell-mode build
— the mini ubers will one-shot anyone who walked in casually, and the
prime evils are tuned to require the same gear/setup as the equivalent
1.11+ event in vanilla.

#### Cheat Menu fallbacks

The Dev Tools (Ctrl+V) menu retains the direct-spawn buttons under
the **Pand** tab so you can practice individual ubers without burning
a recipe set:
- Lilith / Uber Duriel / Uber Izual / Uber Mephisto / Uber Diablo /
  Uber Baal — single-boss spawn
- Mini Uber Trio / Final Uber Trio — full encounter

Item-drop buttons in the **Items** tab cover the same things the cube
recipes consume: 3 Keys, 3 Organs, 4 Essences, Token of Absolution,
Hellfire Torch — useful when iterating without grinding Hell drops.

### Stash — Stackable Tabs (Consumables / Runes / Gems)
A new SECOND COLUMN of stash tabs appears next to the existing P
column when the chest is open. Three new gold/yellow tabs at the top
of that column hold "stackable" items: anything where every drop is
interchangeable with every other drop of the same code (potions,
scrolls, runes, gems, keys, organs, essences — 107 distinct item
types). Charms, throwing weapons with affixes, and quest items are
explicitly excluded — they have rolled per-instance state that would
be lost in a stack.

**The three tabs (button labels CON / RUN / GEM):**

- **STK_C — Consumables** (39 cells):
  - Row 0: hp1-hp5 (Lesser..Greater Healing Potion)
  - Row 1: mp1-mp5 (Lesser..Greater Mana Potion)
  - Row 2: rejuv s/l + stamina/antidote/thawing potions
  - Row 4: TP scroll + ID scroll + ToTP + ToID
  - Row 6: Gas potions (3 grades) + Oil potions (3 grades)
  - Row 8: Skeleton Key + Arrows quiver + Bolts quiver + 3 uber keys
  - Row 9: 3 boss organs + 4 essences + Token of Absolution

- **STK_R — Runes** (33 cells):
  - Row 1: r01-r10 Low Runes (El..Thul)
  - Row 3: r11-r20 Mid Runes (Amn..Lem)
  - Row 5: r21-r30 High Runes (Pul..Ber)
  - Row 7: r31-r33 Top Runes (Jah/Cham/Zod) centered

- **STK_G — Gems** (35 cells, 7 colors × 5 grades):
  - Rows 1-7 = Amethyst / Topaz / Sapphire / Emerald / Ruby / Diamond /
    Skull, columns 0-4 = Chipped / Flawed / Normal / Flawless / Perfect.

**Mirror of AP/SH dual-mode rule:** STK_AP variants are visible only
in AP mode; STK_SH variants visible only in standalone (or post-goal
AP). Same access matrix as the existing AP/SH columns. Underlying
storage:
- `ap_stash_stk_<charname>.dat` — per-character STK_AP tabs
- `shared_stash_stk.dat` — account-wide STK_SH tabs

Each cell stores ONE template item (the bitstream from the first
deposit) plus a count (1-999). Subsequent deposits of the same code
just increment the count.

**Rendering** — every cell has a designated `dwCode`. Designated cells
draw a translucent (trans50) ghost of the item's `inv*` DC6 from
`data\global\items\inv\`; cells outside the layout render as solid
black squares with no hit-test. Occupied cells overlay the opaque
icon + count text in the bottom-right corner (white < 100, yellow
100-998, gold 999 = max).

**Deposit** — shift+right-click an item in your backpack while an STK
tab is active. The item's `dwClassId` is matched against the active
layout's cells; on a match the item is serialized to template bytes
(via D2Common ord 10881), the count goes 0→1 (or N→N+1), and the
original is removed from the backpack. Mismatched items bounce.

**Pickup** — left-click on a populated STK cell while an STK tab is
active. A fresh item is spawned from the template via
D2Game `sub_6FC4EC10` (the same path used for tab-swap fills) and
placed on the player's cursor via `sub_6FC898F0`. The cell's count
decrements by 1. **One item per click** — vanilla "grab the whole
stack" is not available (intentional). Cursor must be empty.

**Grid coordinates** are tunable in `d2arch.ini` under `[Stash]`:
- `StkGridX` (default 232), `StkGridY` (default 143) — top-left of
  the in-tab 10x10 cell grid in screen pixels
- `StkCellW` / `StkCellH` (default 29) — cell dimensions

These match the empirically-calibrated stash grid origin used by
`StashQuickMoveToStash` for 1068x600 with the Ultimate Inventory mod;
adjust if the user's resolution differs.

**Pending polish** (not blocking core functionality):
- Tooltip insertion ("Tal Rune ×47" on hover)
- Drop validation with red-flash on stack-full / wrong-type
- Auto-consolidate when an item is dropped via vanilla left-click drag
  (currently shift+rclick is the supported deposit path)

### Patch 1.11 Runeword Backport (7 runewords)
The seven runewords Blizzard added in patch 1.11 are now usable in
our 1.10f base. All authored from documented data with skill/stat IDs
verified against our `Skills.txt` and `Misc.txt`. The placeholder
rows shipped in vanilla 1.10's `Runes.txt` (one per name, complete=0,
no runes/properties) have been filled in:

- **Bone** (Sol+Um+Um, body armor) — +2 Necro skills, +50 mana, +75 life,
  all res +30, dmg reduced 7, Bone Armor proc
- **Enlightenment** (Pul+Ral+Sol, body armor) — +2 Sorc skills, +1
  Warmth, +30% def, fire res +30, Blaze proc + Fire Ball proc
- **Myth** (Hel+Amn+Nef, body armor) — +2 Barb skills, +30 def vs
  missile, +10 life regen, all res +20, thorns 14, Howl + Taunt procs
- **Peace** (Shael+Thul+Amn, body armor) — +2 Amazon skills, +20% FHR,
  +2 Crit Strike, cold res +30, Slow Missiles + Valkyrie procs
- **Principle** (Ral+Gul+Eld, body armor) — +2 Pal skills, +50% dmg
  vs undead, +100-150 life, fire res +30, 100% Holy Bolt proc on strike
- **Rain** (Ort+Mal+Ith, body armor) — +2 Druid skills, +100-150 mana,
  lightning res +30, magic dmg reduce 7, Cyclone Armor + Twister procs
- **Treachery** (Shael+Thul+Lem, body armor) — +2 Assassin skills,
  +45% IAS, +20% FHR, cold res +30, +50% gold, Fade + Venom procs

### Patch 1.11 Runeword Backport — Wave 2 (21 runewords)
Twenty-one more vanilla-1.10f placeholder rows in `Runes.txt` (rows
shipped with `complete=0`, blank itypes/runes/properties) have been
authored with full property values from the patch 1.11 specification.
This includes the famous endgame runewords like Infinity, Last Wish,
Faith, and Phoenix that vanilla 1.10f preseeded but never enabled.
Grief is added on top of the placeholder set since vanilla 1.10f
never even reserved a row for it (slot Runeword171, an unused 1.11+
expansion row, was repurposed for it).

**Bow / Crossbow:**
- **Brand** (Jah+Lo+Mal+Gul) — +260% damage to undead, ignore target
  AC, fires Explosive Arrows lvl 3, +35% pierce, +280% damage to
  demons, +280 AR vs demons
- **Edge** (Tir+Tal+Amn) — Thorns aura lvl 15, +320-380% damage to
  undead, +280% damage and AR vs demons, +75 AR, -3 magic damage
  taken, +15 stamina regen
- **Faith** (Ohm+Jah+Lem+Eld) — Fanaticism aura lvl 12-15, +1-2
  random skills, +330% ED, +75 AR, +10-15 all res, +300% damage to
  demons, +8 magic absorb
- **Harmony** (Tir+Ith+Sol+Ko) — +200-275% ED, +6 min / +9 max
  damage, Vigor aura lvl 1-6, +1-2 random skills, +10 life regen,
  +20 mana regen
- **Ice** (Amn+Shael+Jah+Lo) — +140-210% ED, +25% cold damage,
  +20% cold absorb, Holy Freeze aura lvl 18, +380% damage to demons,
  +25% cold pierce
- **Insight** (Ral+Tir+Tal+Sol, also polearm/staff) — Meditation aura
  lvl 12-17, +1-6 Critical Strike, +200-260% ED, +180-250 AR, +2-6
  mana regen, +23% MF, +5 def

**Polearm / Spear:**
- **Infinity** (Ber+Mal+Ber+Ist, polearm) — +270-320% ED, Conviction
  aura lvl 12, hit-skill Chain Lightning lvl 35 (40%), +45% Crushing
  Blow, +50-55% lightning pierce, +35 vit, +30-35% magic resist
- **Obedience** (Hel+Ko+Thul+Eth+Fal) — hit-skill Enchant lvl 30
  (21%), +370% ED, +40% Crushing Blow, +200-300 def, +20-30 fire/all
  resist, ignore target AC
- **Pride** (Cham+Sur+Io+Lo) — hit-skill Fire Wall lvl 25 (17%),
  Concentration aura lvl 16-20, +260-300% ED, +1-600 lightning
  damage, +1 to all combat skills, +8-12 all res, +8-10 life regen
- **Rift** (Hel+Ko+Lem+Gul, polearm/scepter) — hit-skill Frozen Orb
  lvl 9 (16%), hit-skill Tornado lvl 11 (16%), +60-100 magic damage,
  +160-250 fire damage, +150-250 AR, Holy Bolt aura lvl 1, +20
  magic absorb

**One-handed Sword / Axe / Mace:**
- **Death** (Hel+El+Vex+Ort+Gul, sword/axe) — +300-385% ED, +50%
  Crushing Blow, +100% Deadly Strike, +12% lifesteal, +20 IAS, +50
  defense, hit-skill Chain Lightning lvl 25 (44%)
- **Destruction** (Vex+Lo+Ber+Jah+Ko, polearm/sword) — +350% ED,
  ignore AC, +20% Crushing Blow, +20% Open Wounds, +60% Deadly
  Strike, hit-skill Static Field + hit-skill Chain Lightning
- **Grief** (Eth+Tir+Lo+Mal+Ral, sword/axe) — +35% ED, +340-400
  flat damage, ignore AC, +220% damage to demons, +25 AR / +22%
  prevent monster heal, +20-25 vit, +5-10 life regen.
  *Newly added row — repurposes empty Runeword171 slot (vanilla
  1.10f never reserved one for Grief).*
- **Last Wish** (Jah+Mal+Jah+Sur+Jah+Ber, sword/hammer/axe) —
  +330-375% ED, hit-skill Charged Bolt 6%, get-hit Charged Bolt 10%,
  hit-skill Life Tap 6%, +60-70% Crushing Blow, Might aura lvl 17,
  ignore AC
- **Lawbringer** (Amn+Lem+Ko, sword/hammer/scepter) — hit-skill
  Decrepify lvl 20 (15%), +3-14 cold damage, +3-14 fire damage,
  +20% slow target, +7 magic absorb, +150-200% damage / AR vs undead
- **Oath** (Shael+Pul+Mal+Lum, sword/axe/mace) — +210-340% ED,
  indestructible, hit-skill Bone Spirit lvl 30 (20%), Heart of
  Wolverine charges + Iron Golem charges, Heart of Wolverine aura
  lvl 16, +30% MF

**Body Armor / Weapon (universal):**
- **Fortitude** (El+Sol+Dol+Lo, weapon or body armor) — +300% ED,
  +200% def, +100-200 life, +25-30 all res, +9-11 fire damage,
  hit-skill Chilling Armor lvl 20 (15%), +7 magic absorb

**Helm / Body Armor / Shield:**
- **Dragon** (Sur+Lo+Sol, body armor or shield) — +360% def, hit-
  skill Hydra lvl 20 (18%), Holy Fire aura lvl 14, +12 str, +50
  life, +5% mana
- **Dream** (Io+Jah+Pul, helm or shield) — hit-skill Confuse lvl 10
  (14%), Holy Shock aura lvl 15, +20% lightning thorns, +150-220%
  def, +20-30 all res, +10-20 life, +1 all skills
- **Phoenix** (Vex+Vex+Lo+Jah, weapon or shield) — +350-400% ED,
  Redemption aura lvl 10-14, hit-skill Blaze lvl 40 (22%), +15-21%
  fire absorb, +350% fire damage, -28 fire res penalty
  (intentional — matches 1.11 spec), ignore AC
- **Spirit** (Tal+Thul+Ort+Amn, sword or shield) — +2 all skills,
  +25-35% FCR, hit-skill Bone Armor lvl 5 (7%), +15 mana regen,
  +89-112 life, +35-55 cold res, +3-8 magic res

**Active runeword count after this wave: 90** (was 70 before).
1.11-era runewords now complete: 28 (7 from Wave 1 + 21 from Wave 2).
Combined with the D2R 2.4 (7) and D2R 2.6 (6) backports below, the
mod now ships **41 backported runewords** on top of the 64 native
1.10f runewords.

The change is pure data — only `Runes.txt` was modified, no DLL
rebuild required. Existing characters can craft these runewords
the next time they log in.

### Cube Recipes — runeword testing
Seventeen test recipes added to `cubemain.txt` so players can convert
common vendor items into the runes / sockets needed to craft 1.11
runewords without farming for hours:
- 15 single-input recipes: vendor scroll/potion → specific rune
  (Tome of TP→Shael, Tome of ID→Thul, Antidote→Lem, Stamina pot→
  3-socket Light Plate, Thawing pot→3-socket Crown, etc.)
- All recipes prefixed `TEST:` in the description for visibility

### Patch D2R 2.4 Runeword Backport (7 runewords)
Filled the existing placeholder rows in `Runes.txt` with full property
definitions sourced from D2R 2.4 patch notes:

- **Plague** (Cham+Shael+Um, sword/claw/dagger) — 25% Lower Resist
  proc on hit, 5% Poison Nova proc when struck, L13-17 Cleansing
  aura, +1-2 all skills, +20% IAS, +220-320% ED, 50% poison length
  reduce
- **Pattern** (Tal+Ort+Thul, claw) — +30% FHR, four-element damage
  adds, +6 Str/Dex, all res +15
- **Mist** (Cham+Shael+Gul+Thul+Ith, bow/crossbow) — L8-12
  Concentration aura, +3 all skills, +325-375% ED, +9-11 attributes,
  +24% MF, cannot be frozen
- **Obsession** (Zod+Ist+Lem+Lum+Io+Nef, staff) — +4 all skills,
  +65% FCR, indestructible, +60-70 all res, +16-22 attributes, +24%
  MF, +30 vitality
- **Wisdom** (Pul+Ith+Eld, helm) — +33% Pierce, 4-8% mana steal,
  +5 mana per kill, +1-2 all skills, +15% AR
- **Unbending Will** (Fal+Io+Ith+Eld+El+Hel, sword) — 8% Taunt
  proc on strike, +3 Barb Combat, +18% IAS, +300-350% ED, prevent
  monster heal, +10 Str/Vit
- **Flickering Flame** (Nef+Pul+Vex, helm) — L4-8 Resist Fire
  aura, +3 fire skills, 30% fire absorb, +1 all skills, 4% life
  steal, +30% fire resist

### Patch D2R 2.6 Runeword Backport (6 runewords)
Six D2R 2.6 runewords backported. Two of them (Mosaic and
Metamorphosis) are simplified because the underlying engine
mechanics they rely on (charge-save state, mark-on-attack states)
don't exist in 1.10f — the simplified versions deliver equivalent
power via permanent stats but lose the mechanic flavor.

- **Cure** (Shael+Io+Tal, helm) — L1 Cleansing aura, +75-100% ED,
  +5% max life, +40-60 poison resist, 50% poison length reduce
- **Hearth** (Shael+Io+Thul, helm) — +75-100% ED, +5% max life,
  +40-60 cold resist, 10-15% cold absorb, cannot be frozen, +10
  Str/Vit
- **Hustle (armor)** (Shael+Ko+Eld, body armor) — +65% FRW, +40%
  IAS, +20% FHR, +6 Vit, +10 Dex, 50% slower stamina drain,
  +10 all res. (D2R "Evade" stat doesn't exist in 1.10f, so the
  +6 Evade slot is mapped to +6 Vit instead.)
- **Hustle (weapon)** (Shael+Ko+Eld, weapon) — 5% chance level 1
  Burst of Speed on striking, +30% IAS, +180-200% ED, +75% damage
  to undead, +50 AR vs undead, +10 Dex. (Official D2R 2.6 spec
  also has "Level 1 Fanaticism Aura When Equipped" but our 1.10f
  project has a known bug R8/R10 where the aura skill name lookup
  resolves to "an evil force" instead of "Fanaticism", so the
  aura property is dropped to avoid showing broken text.)
- **Mosaic** (Mal+Gul+Thul+Amn, claw) — simplified version. The
  D2R "50% chance MA finishers don't consume charges" mechanic is
  D2R-only and cannot be ported to 1.10f. Replaced with permanent
  stats: +1-2 MA skills, +50% AR, +200% ED, all res +30, +15%
  Crushing Blow, 7% life steal, +50% MF
- **Metamorphosis** (Io+Cham+Fal, helm) — simplified version. The
  D2R "Mark of Wolf / Mark of Bear" state-on-shapeshift-attack
  mechanic doesn't exist in 1.10f. The mark effects are baked in
  as permanent stats: +5 Shape Shifting (Druid), +25% IAS, +40%
  max life, +25% Crushing Blow, +50-80% ED, +10 all attributes,
  all res +10

(Bulwark, Ground, Temper from D2R 2.6 not yet authored — sparse
public data on their full property lists. Will revisit if user
wants them.)

Note on stat stacking: each rune intrinsic bonus stacks on top
of the runeword's properties. For Hustle weapon, the in-game
tooltip therefore shows the combined totals — e.g. Eld socketed
into a weapon adds +75% damage vs undead and +50 AR vs undead
on its own, so the runeword's +75 / +50 add to those for a final
+150% / +100. Same pattern for Shael (+20% IAS) and Ko (+10 Dex).
The wiki tooltip lists only the runeword's contribution, not the
totals.

**Crash fix — D2R 2.6 runewords moved into vanilla rows:**
The first build added the 6 D2R 2.6 runewords as new rows
(Runeword171-176) appended to `Runes.txt`. D2 1.10f crashes when
forming any runeword whose row key has no matching string in
`expansionstring.tbl` — and only Runeword1-170 are mapped (170
keys total in the .tbl). The new rows had no `.tbl` entry, so
forming the runeword dereferenced a NULL string pointer.

Fix: relocated the 6 D2R 2.6 runewords into existing empty
vanilla placeholder rows (whose `.tbl` keys all exist) and
added display-name overrides to our patched `patchstring.tbl`:

| Runeword       | Vanilla row  | Was placeholder name | Now displays as |
|----------------|--------------|----------------------|-----------------|
| Hustle (armor) | Runeword50   | Hatred               | Hustle          |
| Hustle (weap)  | Runeword90   | Night                | Hustle          |
| Mosaic         | Runeword93   | Oblivion             | Mosaic          |
| Cure           | Runeword132  | Starlight            | Cure            |
| Hearth         | Runeword135  | Still Water          | Hearth          |
| Metamorphosis  | Runeword142  | Terror               | Metamorphosis   |

The 6 dangling Runeword171-176 rows are blanked (no itype/runes)
so they cannot match any item base.

### D2R 3.0 (Reign of the Warlock) Skill Values Port
All 210 vanilla character skills (Amazon, Assassin, Barbarian, Druid,
Necromancer, Paladin, Sorceress — 30 each) updated to D2R patch 3.0
values. Mana costs, per-level damage scaling, hitshift, synergy
percentages, and prerequisite chains all match the latest D2R balance.
The Warlock class (D2R 3.0's "Reign of the Warlock" expansion) is
deliberately excluded — that's a new class that would require
custom client/skill-tab work beyond a TXT/data port.

**Data sources used:**
- `blizzhackers/d2data` (GitHub) — raw D2R 3.0 JSON dump including
  `skills.json` (mana, lvlmana, manashift, HitShift, EMin/EMax with
  five per-level brackets, Param1..8 for per-synergy bonus %,
  reqlevel/reqskill1, EDmgSymPerCalc synergy formula).
- The synergy formula parser walks `skill('NAME'.blvl)*parN` tokens to
  pair each synergy name with the correct paramN bonus. Solves the
  multi-pct case (e.g. Holy Fire: Resist Fire +21%, Salvation +10%)
  that a single-paramN approach can't represent.

**What got updated (per skill):**
- `Game/data/global/excel/Skills.txt` — 357 rows updated with D2R 3.0
  values for safe value-only columns (mana, lvlmana, minmana,
  manashift, Param1..Param8, HitShift, EType, EMin, EMax, EMinLev1..5,
  EMaxLev1..5, reqlevel). Synergy formulas (calc1..calc6, EDmgSymPerCalc)
  intentionally left untouched — those are D2 calc syntax that's
  risky to swap and the synergy NAMES haven't changed; only the
  per-level % did, which lives in Param7/Param8 anyway.
- `Game/Archipelago/skill_data.dat` — our custom tooltip data, 210/210
  skills with per-synergy percentages encoded as
  `Name1:%1;Name2:%2;...` (extended from the previous single-pct
  format which couldn't represent multi-tier synergies).

**Skill renames in D2R 2.4-3.0** — string-table overrides added to
`patchstring.tbl`:

| 1.10f name | D2R name |
|------------|----------|
| Fire Ball | Fireball |
| Sword Mastery | Blade Mastery |
| Pole Arm Mastery | Polearm Mastery |
| Sanctuary | Sanctuary Aura |

The `Skills.txt` `skill` column still holds the 1.10f internal name
(safer for save-file compatibility); the rename only affects display
via `string.tbl`/`patchstring.tbl` lookup. ID-based matching means
both internal and display names route to the right skill.

**DLL parser extension:**
- New synergy field format `Name:%;Name:%;...` parsed alongside the
  legacy `(skill('X'.blvl))*par8` form (auto-detected by presence of
  `:` and absence of `skill(`).
- `SFD_MAX_SYN` bumped 4 → 6 to handle Ice Bolt's 5 synergies (Frost
  Nova, Ice Blast, Glacial Spike, Blizzard, Frozen Orb at 15% each).
- Tooltip shows per-synergy percentage instead of a shared bonus.

**Spelling/internal-name notes:**
- D2R JSON keeps original Blizzard internal names (`Wearwolf`,
  `Wearbear` typos preserved, `BloodGolem`/`IronGolem`/`FireGolem`
  one-word, `Dopplezon` for Decoy, `Eruption` for Fissure, etc.).
- Our generator maps these back to clean display names via an
  ID-stable mapping table — IDs match between 1.10f and D2R, only
  the display text differs.

### Token of Absolution
The 1.13c Token of Absolution (`tof` Misc item) is now in 1.10f. Cubing
the token resets all skill and stat allocations so a character can be
respec'd without any quest-cost. The token is added to:
- `Misc.txt` as a craftable consumable
- The Ctrl+V cheat menu under **CHARACTER** for testing
- The STK Consumables tab layout (row 9 anchor cell) so a stash
  full of tokens stays organized

### F1 Collection Page (NEW PAGES IN F1 BOOK)
A new always-on Pokédex-style tracker added to the F1 book.
Press F1, then Next/Back to navigate through the 5 new collection pages
(in addition to Skill Editor / Quest Book / AP Connection):

- **Sets Part 1** — first 16 vanilla sets (Civerb's, Hsarus', Cleglaw's,
  Iratha's, Isenhart's, Vidala's, Milabrega's, Cathan's, Tancred's,
  Sigon's, Infernal Tools, Berserker's, Death's, Angelical, Arctic,
  Arcanna's). Compact list view: set name, class-lock badge, piece
  icons inline, and "X / Y" progress on the right.
- **Sets Part 2** — remaining 16 sets (7 class-locked: Natalya's=Assassin,
  Aldur's=Druid, Immortal King=Barbarian, Tal Rasha's=Sorceress,
  Griswold's=Paladin, Trang-Oul's=Necromancer, M'avina's=Amazon, +
  9 generic LoD sets).
- **Runes** — all 33 runes (El..Zod) in 11×3 grid grouped Low/Mid/High.
- **Gems** — 5×7 grid: 5 grades (Chipped..Perfect) across × 7 colors
  (Amethyst/Topaz/Sapphire/Emerald/Ruby/Diamond/Skull) down.
- **Specials & Gold** — 5×2 grid of 10 special items (3 Pandemonium
  Keys, 3 Boss Organs, 3 Essences, Hellfire Torch) + a lifetime
  Gold-Earned counter.

**Detection (two-flag, anti-cheat)** — a slot is marked "collected"
only when BOTH of these are true for the same item instance:

  - **Flag A (legitimate drop)** — the item was observed on the
    ground with the engine's `IFLAG_NEWITEM` bit set. That bit is
    set by D2's loot generator when a monster, chest, barrel, or
    other in-world object spawns the item, and cleared the first
    time any player picks it up. So an item that satisfies Flag A
    is guaranteed to have come fresh from a world source — not from
    another player's drop, not from the shared stash.

  - **Flag B (in your inventory)** — the same item instance is
    subsequently seen in your character's inventory (backpack,
    equipped, belt, or cursor).

Items first observed in inventory (cube output, vendor purchase,
quest reward, items already in inv at character load, items
withdrawn from any stash including STK shared tabs) are locked as
TAINTED on first observation and never auto-mark, even if you later
drop and pick them back up. Items dropped by another player in MP
also fail Flag A because by the time you see them on the ground,
the engine has already cleared `IFLAG_NEWITEM`.

The classification tracks each item by its per-game `dwUnitId`
(unique within a session). Walks of on-ground items run every game
tick — cheap because the per-GUID dedup makes re-walking the same
items a no-op. The inventory walk that actually marks slots is
throttled to every 2 seconds.

**Sticky** — once a slot is marked, it stays marked forever for
that character. Selling, dropping, or stashing the item doesn't
reset the flag. Each first-found event is timestamped (Unix time)
and shown in the tooltip as "Collected YYYY-MM-DD".

**Cube transmute — full coverage** — A new hook on `sub_6FC91250`
(D2Game+0x61250, the trade-button dispatcher invoked by packet 0x4F)
catches button id 24 (TRADEBTN_TRANSMUTE) on every successful cube
recipe — gem upgrades, runeword socketing, rerolls, vanilla Cow
Portal, AND our Pandemonium recipes. Replaces the older partial hook
on `UberCowPortalHook` which only counted Cow-Portal-output recipes.
The Pandemonium-recipe hook still fires (for spawn logic) but no
longer increments `cubeTransmutes` to avoid double counting.

**apworld 1.9.0 — Goal=Collection + 5 sub-target options** — The
`Goal` choice now has a fourth option `option_collection = 3`
alongside `full_normal/nightmare/hell`. Five new options added:

- `CollectionTargetSets` (Toggle, default ON) — require all 32 vanilla sets
- `CollectionTargetRunes` (Toggle, default ON) — require all 33 runes
- `CollectionTargetGems` (Toggle, default ON) — require all 35 gems
- `CollectionTargetSpecials` (Toggle, default ON) — require 3 keys + 3 organs + 3 essences + Hellfire Torch
- `CollectionGoldTarget` (Range 0..100,000,000, default 0) — optional lifetime-gold threshold

All five are forwarded via `fill_slot_data` so the bridge writes them
to `ap_settings.dat`, and the DLL's existing slot-data parser already
reads these field names — they were wired in earlier in 1.9.0
development. apworld bundle size: 28,006 bytes.

**Class-locked set auto-deselect (Goal=Collection mode)** — A fresh
character on Goal=Collection no longer needs to collect the 6 sets
their class can't equip. When the default goal config initializes,
`Coll_AutoDeselectClassLockedSets()` reads the player's class and
unsets the targeting flag on every set whose `classLock` field
doesn't match. So an Amazon goes for M'avina + the 25 generic sets
(26 total), a Sorc goes for Tal Rasha + 25 generic, etc. Manual
override is still possible via `d2arch.ini [Settings] CollGoalSets`
key for the "play Sorc, complete museum" use case.

**Vendor + runeword + identify hooks (1.9.0 evening polish)** — three
more event hooks added to D2Game on top of the original object/item-use
pair:

- **Pkt 0x32 (BuyItemFromNpcBuffer)** at `D2Game+0x56AE0` — increments
  `itemsBoughtFromVendor` on every NPC purchase. The `nTransactionType`
  field at packet offset 0x0B distinguishes regular vendor (0) from
  gambling (non-zero), so the same hook also drives `gambledItems`.
- **Pkt 0x33 (SellItemToNpcBuffer)** at `D2Game+0x56B30` — increments
  `itemsSoldToVendor` on every sale. Sale gold revenue continues to be
  filtered out of `goldCollected` by the existing vendor-UI poll, so
  the two counters don't double-tell the same story.
- **Inv-walk flag-transition detection** — the existing `Coll_ProcessItem`
  GUID table now stores the previous tick's `pItemData->dwItemFlags`
  alongside the source classification. When `IFLAG_RUNEWORD` (0x04000000)
  flips 0→1 we increment `runewordsCreated`; when `IFLAG_IDENTIFIED`
  (0x00000001) flips 0→1 we increment `itemsIdentified`. No new packet
  hook needed — the existing 2-second inv scan picks both transitions
  up on the next tick.

Migration from v1 → v2 sidecars (one-time grace scan) — The
sidecar format was bumped from v1 to v2 to mark the strict-mode
upgrade. When a v1 sidecar (pre-1.9.0 strict-detection) is loaded,
the next inventory scan runs in legacy mode (no GUID check) so
items the player legitimately had at upgrade time stay credited.
The sidecar is then committed at v2 and strict mode applies for
the rest of the character's life. Brand-new characters skip the
grace pass and use strict detection from the first tick. Existing
already-marked slots are preserved verbatim across the migration.

**Set-piece detection** uses the item's set quality flag (dwQualityNo=5)
combined with its `dwFileIndex` (row in SetItems.txt) for precise
mapping — so picking up Civerb's Icon marks ONLY that slot, not every
amulet-base set piece in the catalog.

**Gold counter** — a monotonic lifetime counter that only ever
increases. Counts gold pickup from the ground and quest rewards.
Spending gold via vendors / repair / gambling does NOT decrement
the counter — even if your inventory gold drops to 0, the lifetime
counter keeps its peak total. (Caveat for v1: vendor sale revenue
is still counted because the vendor-UI detection hook isn't wired
yet; refinement queued for 1.9.x polish.)

**Visual style** — un-collected cells show a translucent ghost icon
(or a 3-char code fallback if the DC6 isn't available). Collected
cells show a solid icon with a gold-tinted backdrop. Hover any cell
for a tooltip with name + collection status + drop source + required
level.

**Per-character storage** — each character has its own
`ap_collections_<charname>.dat` sidecar in `Game/Archipelago/`.
Fresh characters start with an empty collection.

### Optional Win-Condition: Goal = Collection
A new goal type added alongside Normal / Nightmare / Hell. Set
`Goal=3` in `d2arch.ini` (or via the apworld option once it's wired
in 1.9.x) and the character wins when every targeted collection is
complete. Default targets when Goal=3 is selected on a fresh
character: every set + all runes + all gems + every special must be
collected. Configurable per-character (frozen at character creation,
matching the existing 1.8.0+ settings-isolation pattern).

When the goal fires, the standard "GOAL COMPLETE!" pipeline runs —
`ap_goal.dat` is written with `goal=complete\nsource=collection`
so the bridge / archipelago side can react to the same event it
already handles for difficulty-based goals.

### Quality of Life — Stack-size bumps for keys / organs / essences
Eleven items in `Misc.txt` had their stack sizes bumped so a typical
Pandemonium hoarder doesn't waste 30 inventory slots on duplicate
charms-of-the-game-loop:

- **Skeleton Key** (`key`): 12 → **99**
- **Pandemonium Keys** (`pk1`/`pk2`/`pk3`): non-stackable → **stack to 99**
- **Boss Organs** (`mbr` Mephisto's Brain / `dhn` Diablo's Horn / `bey` Baal's Eye): non-stackable → **stack to 99**
- **Essences** (`tes`/`bet`/`fed`/`ceh` — the 4 essences of the prime evils): non-stackable → **stack to 99**

Also adds the missing `stackable=1`, `minstack=1`, `spawnstack=1`
metadata so existing UI affordances (split-stack on shift+click, stack
display in tooltip, etc.) work for these items the same way they do
for runes/gems.

### Quality of Life — Tome stacks bumped 20 → 999
Tome of Town Portal (`tbk`) and Tome of Identify (`ibk`) stack
sizes raised from 20 to 999 in `Misc.txt`. One tome now lasts a
full character lifetime instead of needing constant Akara/Cain
refills. Combined with the STK_C tab's 999-stack tomes, you can
deposit a fully-charged book and pull off a single charge at a
time without ever managing stack overflow.

### Dev Tools (Ctrl+V cheat menu) — Redesign
- Full redesign: 3-column grid (~509×620 px panel) with section
  headers (CHARACTER / TRAPS / BOSS LOOT / RUNES / BASES / GOODIES)
  replaces the previous single-column scrollable strip that
  overflowed off-screen.
- Each row is 26 px tall with 5 px gap; layout is data-driven
  (one MENU[] table walked at draw time so reordering / adding
  buttons is one-line edits).
- Solid opaque backgrounds (transMode=1 for fnRect) — earlier
  attempt with transMode=5 produced an additive blend that was
  invisible against dark scenes, leaving floating button text.
- New cheat options:
  - **CHARACTER:** +1000 Gold, +5 Stat Pts, +5 Skill Pts, +10 Lvl XP,
    Heal Full (HP+MP+Stamina to max), Unlock All Skills.
  - **TRAPS:** Spawn SuperUnique, Spawn Monsters.
  - **BOSS LOOT:** Andariel / Duriel / Mephisto / Diablo / Baal.
  - **RUNES** (split into manageable pickup-sized batches):
    Cube only, Low Runes 1-10 (El..Thul), Mid Runes 11-20
    (Amn..Lem), High Runes 21-33 (Pul..Zod), All 33 Runes.
  - **BASES** (3-socket eligible): Body 3os, Helm 3os, Weapons 3os+,
    All Bases.
  - **GOODIES:** All 35 Gems (7 colors × 5 grades), Healing Pots,
    Mana Pots, Recipe Pots (Cube + Stamina + Thawing for the
    Light-Plate-3os and Crown-3os cube recipes).
- All item drops use `QUESTS_CreateItem` (D2Game+0x65DF0) with
  space-padded item codes (D2 hashes codes as `'box '` not `'box\0'`),
  null-padded fallback retained for safety.
- Unified dispatch: single `g_cheatItemCmd` flag (1..13) replaces
  the legacy three flags (`g_cheatTestRunes`, `g_cheatTestBases`,
  `g_cheatTestRunewords`); legacy flags kept reachable for
  backward compatibility but no longer wired to UI.

## Bug Fixes in 1.9.0

### Goal=Collection — granular per-item targeting + per-item AP checks
The Collection goal mode shipped earlier in 1.9.0 with 4 all-or-
nothing toggles (sets / runes / gems / specials). This update
replaces those with **75 individual per-item toggles** plus a
full AP location/check pipeline so each collected item becomes a
multiworld check that returns a filler item.

**apworld options** — 75 new Toggle classes generated dynamically:
- 32 individual `CollectSet<Name>` toggles (Civerb's, Sigon's, Tal
  Rasha's, ... M'avina's). Class-locked sets still default ON;
  the existing in-DLL auto-deselect (Phase 5.2) clears non-matching
  ones at character creation.
- 33 individual `CollectRune<Code>` toggles (CollectRuneEl through
  CollectRuneZod).
- 10 individual `CollectSpecial<Code>` toggles (3 Pandemonium Keys,
  3 Boss Organs, 3 Essences, Hellfire Torch).
- Gems remain a single `CollectionTargetGems` toggle (per-gem
  granularity was deemed unnecessary by user).
- `CollectionGoldTarget` Range unchanged.

**apworld locations** — 110 new collection location IDs at
50000-50109:
- 50000-50031 = "Collection: Set <Name>" (32 sets)
- 50032-50064 = "Collection: <Rune> Rune" (33 runes)
- 50065-50099 = "Collection: <Grade> <Color>" (35 gems)
- 50100-50109 = "Collection: <Item>" (10 specials)

Each enabled toggle conditionally adds the corresponding location
to the slot's location table, so disabled items don't appear in
the multiworld at all.

**slot_data encoding** — to keep the wire format compact, the 75
booleans get packed into 6 bitmask integers:
- `collection_sets_mask_lo/hi` (32 bits)
- `collection_runes_mask_lo/md/hi` (33 bits — needs 3 chunks
  because each chunk is a 16-bit int)
- `collection_specials_mask` (10 bits)

**DLL parser** — d2arch_ap.c parses the 6 bitmasks into new
`g_collGoalOverrideSetsMask` / `RunesMask` / `SpecialsMask`
globals. d2arch_collections.c applies them bit-by-bit to the
existing `g_collGoal.setsTargeted[32]` / `specialsTargeted[10]`
arrays plus the new `runesTargeted[33]` array (added to
`CollectionGoalConfig` for granular rune targeting — was a single
`allRunesTargeted` boolean before).

**Check firing — AP mode**:
`Coll_MarkSlotCollected` now calls `Coll_MaybeFireCheckForSlot()`
when `g_apGoal == 3` (Collection mode). The check fires only for
TARGETED items:
- Set pieces: only when the WHOLE set just became complete (last
  piece collected)
- Runes / gems / specials: per-item on first collection
- Each fire sets a bit in the new `s_collCheckFired[110]` flagset
- The next `WriteChecksFile` save flushes the flagset to
  `d2arch_checks_<charname>.dat` as `check=8000+offset` lines
  (8000 = 50000 - LOCATION_BASE 42000). Bridge picks up the file
  and forwards to the AP server, which echoes back the placed
  filler item via the existing item-receive pipeline.

**Standalone mode**:
Same `Coll_MaybeFireCheckForSlot` path, but instead of writing to
the checks file it bumps `g_pendingRewardGold += 500` for each
collection event. The existing reward-application tick then
grants the gold via `fnAddStat(STAT_GOLD, 500, 0)`. Per user spec:
standalone mode uses ALL items as targets (the per-item toggles
are AP-only) and grants a flat 500g reward per item.

**Backward compat**:
- v1 sidecars (pre-1.9.0) load with `runesTargeted[]` zeroed; the
  legacy `allRunesTargeted` flag is preserved for the goal-complete
  check via Coll_GoalConfigIsZero detection that triggers
  Coll_DefaultGoalAllOn re-init.
- Old INI keys (CollGoalSets / CollGoalRunes / etc.) deprecated;
  new INI keys (CollSetsMaskLo/Hi, CollRunesMaskLo/Md/Hi,
  CollSpecialsMask) provided as undocumented power-user overrides.

### F4 zone tracker — AP key location render
The bridge has been writing scouted gate-key locations to
`ap_item_locations.dat` since 1.8.5, parsed by `LoadAPItemLocations`
into `g_apItemLocation[diff][gate_slot]`. Until now the data was
collected but never displayed. Phase 9 wires it into the F4 zone
tracker:

- Each gate row now reserves a 2nd line below the boss/zone label.
  Row height grew from 18 to 30 px (panel scrolls so the extra
  ~216 px across 18 gates is invisible to the user).
- When AP mode is active AND the bridge has scouted a location
  for this slot, the 2nd line renders `key: loc <id> @ <player>`
  in small font (font 6).
- Color signals state: green (color 2) when the key has already
  been received, orange (color 8) when the key is still pending
  pickup. Standalone mode hides the line entirely.
- Indented at the same X as the boss/zone column (contentLeft + 100)
  so it visually belongs to the gate it describes.

New public accessor: `Quests_GetGateKeyLocStr(int diff, int slot)`
returns a const char* pointer or NULL. Called from
`d2arch_render.c`'s `RenderZoneTracker` per gate row. Internally
just does bounds-checking + empty-string detection on the existing
static `g_apItemLocation` array in `d2arch_quests.c`.

Currently only renders keys placed in the local player's own world
(the scout-target). Cross-world keys placed in other players'
worlds need the AP `!hint` mechanism to surface — that's a separate
follow-up that's out of scope for 1.9.0.

### F1 Collection — stash sidecar grandfather pass
Items deposited into your regular stash before 1.9.0's two-flag
detection was wired in stay un-collected (because the strict rule
needs Flag A — observed on ground with IFLAG_NEWITEM — which can't
be reconstructed from a sidecar). Phase 6 fixes this:

When the stash-load pipeline (`StashSwap_SerFill` in
`d2arch_stashlogic.c`) successfully spawns an item from
`ap_stash_ser_<charname>.dat` or `shared_stash_ser.dat` and places
it in the player's pInventory, it now also calls the new
`Coll_GrandfatherInvItem()` helper. That helper invokes the
existing `Coll_ProcessItem()` logic with `requireLegit=FALSE`,
which performs the catalog lookup + `Coll_MarkSlotCollected()`
without any GUID gating.

The reasoning: items in your OWN stash sidecars are trusted by
construction — the player put them there at deposit time, even if
the deposit happened pre-1.9.0 and isn't in our GUID classification
table. Bypassing the GUID check for this specific path
grandfathered-marks them.

Non-catalog items silently no-op (the catalog lookup returns
`slotIdx = -1`). Matching items get marked AND get the gold-flash
celebration (Phase 5.3) as a side effect since both follow the
same first-mark code path.

Items in tabs the player doesn't open this session stay un-collected
until the next swap. That's intentional — full-tab pre-scan would
require parsing the bitstream format directly (D2Common ord 10883)
which is invasive. The on-tab-open approach hits the same end state
within one or two stash visits.

### F1 Collection polish — gold-flash celebration + search filter
Two new UX touches on the F1 Collection book:

- **Gold-flash celebration on first detection.** When you pick up an
  item that satisfies a previously-uncollected slot, the cell pulses
  with a gold border for 1.5 seconds — a brief "got it!" reward. The
  pulse alternates 1↔3 px thickness at ~6 Hz so it visibly flickers
  rather than just sitting there. Implemented via a per-slot
  `s_collJustCollectedTick[COLL_NUM_SLOTS]` array set inside
  `Coll_MarkSlotCollected` and checked in `Coll_DrawCell`. Self-clears
  after the timer expires so the compare path doesn't run forever.
  In-memory only — the celebration is for the live moment, not
  something you'd see again on next session.

- **Search / filter box.** Type-to-filter input field at the top of
  every Collection page (page 8/9 layout, X=210 Y=48 W=180 H=18).
  Click the box to focus it, type a substring, and the cells whose
  `displayName` doesn't match get dimmed out (cells render as a flat
  grey rect, no icon). Set rows are entirely hidden when neither the
  set name nor any piece name matches. Backspace deletes the last
  char; Escape or Enter clears focus and lets the search persist for
  ongoing browsing. Click outside the box also drops focus.

  The keyboard hook lives in WndProc (`d2arch_main.c`'s WM_CHAR /
  WM_KEYDOWN handlers) gated on `g_editorOpen && g_editorPage in
  3..7 && s_collSearchFocus`. Printable ASCII chars (32..126) only;
  non-printable input is swallowed when focused so it doesn't leak
  to D2's normal input pipeline. Buffer is 39 chars max.

  Filter behavior is case-insensitive substring match. Empty buffer
  shows everything as before. Clearing the field via Backspace
  immediately restores all cells.

### STK polish — tooltip on hover + red-flash on rejected drop
Two visual feedback features added to the STK (stackable) tabs:

- **Hover tooltip** — when the mouse hovers over a populated STK
  cell, a small black-and-gold tooltip box appears near the cursor
  showing `<Item Name>  x<count>`. Empty cells silently skip (no
  point telling the user "nothing here"). Auto-clamps to screen
  bounds and flips below the cursor if there's no room above.
  Implemented in `StashUIRenderStkOverlay` after the cell-iteration
  loop, so the tooltip overlays everything else in the stash UI.

- **Red-flash on rejected deposit** — when a deposit attempt fails
  (wrong item type for the cell, or stack already at 999 / max),
  the rejected cell flashes a 2-px bright-red border for 300ms.
  Three failure paths are now instrumented:
    1. `StkDeposit` wrong-type rejection (shift+rclick path that
       routes through `StashQuick`)
    2. `StkDeposit` stack-full rejection (same path, count == 999)
    3. `StkClickPickup` cursor-deposit wrong-type rejection
       (left-click drop with cursor item where layout->dwCode
       doesn't match cursor item's classId)
    4. `StkClickPickup` cursor-deposit stack-full rejection

  All four call the new `Stk_FlashBadDrop(tabCategory, cellIdx)`
  helper which sets `g_stkBadDropTab/Cell/Tick`. The render path
  checks elapsed time against the 300ms threshold and draws the
  red border around the matching cell.

### Crash fix — TradeButton hook savedLen + relocation bug
The first attempt to hook `sub_6FC91250` (D2Game+0x61250, the trade-
button dispatcher used by the cube transmute counter) used a
conservative `savedLen=7` for the trampoline. That value happened to
land in the middle of the function's `mov esi, edx` instruction at
prologue offset 6-7. The trampoline's saved-bytes block ended with
the orphaned first byte of the mov (`8B`), which then combined with
the JMP-back instruction's leading `E9` byte to form an unintended
`mov ebp, ecx` that corrupted EBP before control returned to the
original function. Symptom: ACCESS_VIOLATION the moment the player
opened the stash (since the close-stash button shares the same
dispatcher).

The same bug class affected the `Pkt32_VendorBuy` and `Pkt33_VendorSell`
hooks at D2Game+0x56AE0 and +0x56B30. Their prologue is a short
size-check stub (`cmp [esp+8], 11h` followed by `jz +8`); savedLen=7
would have copied the relative `jz` into the trampoline, where the
EIP-relative offset becomes wrong post-relocation and points to
garbage memory instead of the function body.

Fix: all three hooks moved to `savedLen=5` which lands cleanly at
their natural prologue boundaries:
- TradeBtn: between `push ebp` (offset 4) and `push esi` (offset 5)
- Pkt32/Pkt33: between the `cmp` (5 bytes) and the `jz` (offset 5),
  leaving the `jz` intact in the original where its relative offset
  still resolves correctly. ZF is preserved across the JMP/return so
  the conditional branch fires correctly.

The OperateHandler and Pkt26 hooks correctly use savedLen=7 because
their prologues genuinely have a 4-byte instruction starting at byte
3 (so the natural boundary IS at byte 7). Pkt27 uses savedLen=5
because its prologue starts with a 1-byte push followed by the 4-byte
mov (boundary at byte 5).

Lesson logged: the choice of savedLen depends on the specific
prologue. There is no single "safe default." Always probe the bytes
first, identify instruction boundaries, AND check for relative jumps
that would mis-relocate.

### Stash / Inventory
- **Socketed items now persist across tab swap.** Multi-tab stash
  uses `ITEMS_SerializeItemToBitstream` (D2Common ord 10881) to drain
  a tab's items into bytes, and D2Game's `sub_6FC4EC10` to respawn
  them when the tab is reactivated. The serialize side correctly
  recurses into a parent item's `pInventory` (socketed runes/jewels/
  gems), so the bytestream contains parent + all children. But the
  spawn side called `ITEMS_DecodeItemFromBitstream` (ord 10882) with
  `pSocketedItemCount=NULL` — it parsed only the parent and left
  socketed children unprocessed in the bitstream. The respawned
  parent kept the runeword bit but lost every socket filler, which
  manifested as: a Hustle weapon that retained its runeword stats
  but had `+0%` rune intrinsics (no Shael IAS, no Eld damage-vs-undead,
  no Ko dex), and after a save/load round-trip the weapon vanished
  entirely because its on-disk record was a half-state the loader
  could not reconstruct.

  Fix: SerFill now mirrors D2's own `.d2s` item loader at
  `PlrSave.cpp:1426-1492` — the gold standard for runeword
  restoration that vanilla D2 has used since 1.10. After
  spawning + placing the parent, we read
  `D2ItemSaveStrc.nItemFileIndex` to get the socketed-child count,
  then for each child:
  1. Parse the next slice of the bitstream via `ITEMS_GetCompactItemDataFromBitstream` (D2Common ord 10883)
  2. Spawn the child via `sub_6FC4EC10` (D2Game RVA +0x1EC10)
  3. Attach the child to the parent's socket by re-using the same
     `sub_6FC898F0` (D2Game RVA +0x598F0) that placed the parent —
     when the 4th argument `pParentItem` is non-NULL, that single
     function routes through D2's internal socket-attach path with
     all the proper anim-mode transitions, runeword updates, and
     client-sync packets that D2 normally fires when loading items
     from .d2s.
  4. Advance the bitstream pointer by the child's parsed size

  This approach replaced an earlier attempt that called
  `D2GAME_ITEMSOCKET_PlaceItem` (RVA +0x497E0) directly. That
  function does work, but only if the filler has been transitioned
  to `IMODE_ONCURSOR` first, and skipping intermediate D2 state
  (refresh-inventory, packet sync, IFLAG_NEWITEM) leaves the client
  displaying empty sockets even when the server has them populated.
  Mirroring `PlrSave.cpp` exactly side-steps every one of those
  edge cases because we go through the same code path D2 itself uses.

  Socketed items (runewords, jeweled items, gemmed weapons) now
  survive arbitrary tab swaps and log-out/log-in cycles. Tested
  on a Hustle Flamberge: full Shael+Ko+Eld runeword formed,
  deposited on a SH tab, swapped to another tab and back,
  all 3 runes still socketed with correct rune intrinsics
  stacking on top of the runeword bonus.

- **STK tabs (Consumables/Runes/Gems): drag-and-drop deposit no
  longer leaves a "ghost" item icon stuck on the cursor.** The
  earlier code only cleared `pInventory->pCursorItem`, but the
  item unit stayed registered in D2Client's player-units hash so
  the cursor kept rendering it. The deposit path now calls
  `D2GAME_RemoveItemIfOnCursor` (D2 1.10f's own potion-from-cursor
  disposal — packet 0x42 + clear cursor field + remove unit from
  global hash) so the cursor returns to empty immediately. A
  belt-and-suspenders SetCursorItem(NULL) fallback runs only if
  the proper call can't resolve.

### Game Modes / Settings
- Per-character settings persistence extended to cover
  `entrance_shuffle` flag (frozen at character creation, restored
  on subsequent loads — same isolation pattern as existing toggles).

### Archipelago Bridge / Multiworld
- **Connect button no longer flips characters into AP mode unless the
  AP server actually authenticates.** Previously, clicking Connect
  set `g_apMode = TRUE` immediately so that PollAPStatus would start
  polling — which had the side-effect of routing every subsequent
  character-load decision (default stash tab, skill-pool init,
  settings source) down the AP path even when the bridge never
  authenticated. So a click that timed out or was made with an
  unreachable server still moved the player onto the AP1 stash tab
  and locked their starting skills as if the YAML had said
  `starting_skills=0`.

  - The "should we be polling the bridge status file?" decision is
    now its own flag (`g_apPolling`), set on the Connect click. The
    `g_apMode` flag — which gates every character-side AP decision
    — is now flipped TRUE only inside PollAPStatus on the
    `status=authenticated` transition.
  - As a consequence, the 1.8.5 race-window freeze deferral in
    `OnCharacterLoad` is gone: characters always freeze immediately
    at load time, because by then `g_apMode` is either definitively
    TRUE (auth happened) or definitively FALSE (no auth — the
    character is genuinely standalone).
  - End-user behavior: clicking Connect with a wrong/dead server
    address and then loading a character now produces a fully
    standalone character on the SH1 stash tab, with INI settings.
    Clicking Connect, waiting for the green button, then loading a
    character produces an AP character on the AP1 tab as before.

- **F1 panel click-through bug fixed.** The legacy AP-panel mouse
  click handler (`HandleAPPanelClick` in `d2arch_ap.c`) was wired to
  fire on every `WM_LBUTTONDOWN` regardless of whether the panel was
  visible. The keyboard handler had been correctly gated on
  `apPanelVisible` (only TRUE on the title screen) but the mouse
  handler had not. As a result, clicks at coordinates inside the
  legacy panel rect (default INI: `px=300, py=300`, button at
  `~320,366..490,386`) silently fired Connect even mid-game while
  the user had the stash open — picked up by a tester who, while
  placing items in the stash, accidentally flipped their character
  onto AP settings. The mouse handler now uses the same
  `apPanelVisible` check as the keyboard handler.

## Known Issues
- Quest triggers are tied to physical level-id. If a quest cave
  (e.g., Den of Evil) gets shuffled, killing all monsters in the
  shuffled destination won't fire the original cave's quest. The
  player can still progress through act bosses — they just lose
  those side-quest XP rewards. Accepted trade-off for v1.
- Walking out of a shuffled cave warps the player to the natural
  surface zone's default spawn point (waypoint or zone center),
  not to the cave entrance position. Limitation of `LEVEL_WarpUnit`
  on outdoor levels.
- Pandemonium event portals (Matron's Den / Forgotten Sands /
  Furnace of Pain — Hell-only via uber keys) are NOT in the
  shuffle pool. Adding them risks low-level chars hitting Uber
  bosses. May be added later as a Hell-only sub-pool.
- Bug R8/R10 still present: skill-name lookup on the Hustle
  weapon's "Level 1 Fanaticism Aura" property resolves to "an
  evil force"; the aura property has been dropped from the runeword
  to avoid showing broken text. Underlying skill-name lookup fix
  pending re-test on a tester rig.
- Other known issues from 1.8.5 still apply until explicitly
  resolved. See `patch_notes_1.8.5.md`.

## Backups taken on the way to 1.9.0
- `Backups/2026-04-28_clean_start_1.8.6/` — start of dev cycle
- `Backups/2026-04-28_entrance_shuffle_working/` — basic entrance shuffle
- `Backups/2026-04-28_entrance_shuffle_v2_complete/` — full pools + act-town lock
- `Backups/2026-04-28_runewords_111_working/` — patch 1.11 runeword backport verified in-game
- `Backups/2026-04-28_socket_fix/` — socketed items tab-swap fix
- `Backups/2026-04-28_tabbed_menu/` — Ctrl+V redesign milestone
- `Backups/2026-04-28_d2r_skills_full/` + `2026-04-28_d2r_skills_verified/` — D2R 3.0 skills port
- `Backups/2026-04-28_pandemonium_data/` + `_pandemonium_recipes/` + `_pandemonium_phase1/` — Pandemonium milestones
- `Backups/2026-04-29_ap_connect_fix/` — AP Connect race + F1 click-through fixes
- Numerous incremental snapshots through `2026-04-29_*` for STK ghost-render, withdraw-fix, persistence-fix, ghost-render-v2, real_dc6, lanczos3x, multi-cell, click-pickup, drag-drop fixes
- `Backups/2026-04-29_phase2_torch_drop/` — Hellfire Torch drop wired to finale uber kill scan
- `Backups/2026-04-29_stk_complete/` — STK system 100% confirmed working (4 flows pass)
- `Backups/2026-04-30_pre_collections_data/` → `_collections_data_done/` →
  `_pre_phase2_detection/` → `_phase2_detection_done/` → `_phase3_render_done/` →
  `_phase4_goal3_done/` → `_collection_v1_complete/` — staged build of the F1
  Collection feature (data tables → detection scan → render → Goal=3 → ship v1)
- `Backups/2026-04-30_pre_phase5_polish/` → `_gaps_2_and_4_done/` →
  `_gaps_1_2_4_5_done/` → `_phase5_polish_complete/` — Phase 5 polish gaps
  (DC6 invFile lookup, vendor-UI gold filter, title-screen Goal=Collection UI,
  apworld Collection option, STK sidecar scan)
- `Backups/2026-04-30_pre_dc6_validation_fix/` → `_pre_design_fix/` →
  `_design_fixes_done/` → `_pre_audit_fixes/` → `_audit_fixes_done/` —
  F1 Collection rendering hardening (DC6 version validation, design audit
  pass, layout-overlap fixes)
- `Backups/2026-04-30_two_page_layout/` → `_correct_book_bounds/` →
  `_full_width_layout/` → `_layout_polish/` → `_movable_centered_text/` →
  `_coll_buttons_repositioned/` → `_ini_tunable_layout/` → `_full_xy_control/` →
  `_clean_ini/` — F1 Collection layout iteration (book parchment bounds
  calibration, INI-tunable coordinates, full per-element X/Y exposure,
  duplicate-key cleanup) + Runeword Wave 2 authoring (`Runes.txt` from
  70 → 90 active runewords)
- `Backups/2026-04-30_collection_layout_LOCKED/` — user finalized all
  Collection-page coordinates; full `d2arch.ini` snapshot + `LOCKED_COORDS.md`
  reference saved as the canonical post-tuning state
- `Backups/2026-04-30_apworld_test_pass/` — apworld 17-test
  validation pass (single-game across all goal modes, multiworld with
  D2-only and mixed, granular collection toggle combinations,
  zone-locking variants); two apworld bugs fixed (Goal=Collection
  diff_names overflow + Hell-zone access rules for ALWAYS_OPEN_ZONES)

## apworld test pass (2026-04-30 night)

Generated 17 multiworlds across:

- Goal modes: Full Normal / Full Nightmare / Full Hell / Collection
- Mode toggles: skill_hunting on/off × zone_locking on/off
- Collection variants: all 75 toggles ON, high runes only (6 locs),
  no class-locked sets (-7 sets), Zod+Torch only (2 locs)
- Class filter: All Classes vs Custom (1-3 classes selected)
- Quest filter: all on, story+hunt only, story-only
- Multiworld: 3-game small (D2+CF+VVVVVV+Yacht), 9-game mixed
  (6×D2 variants + 3 other), 10-game large (HK+Stardew+ many + 2×D2)

Two bugs found and fixed:

- **Goal=Collection IndexError**: `goal=3` made `num_difficulties=4`,
  overflowing the 3-element `diff_names` array in
  `get_active_locations()` and `_build_gate_region_tree()`. Goal=3 now
  treats as Normal-only difficulty scope for AP fill purposes (DLL
  still handles the actual collection-complete win condition at
  runtime). One-line fix per call site.

- **Hell-zone access rules**: zones in `ALWAYS_OPEN_ZONES` (Halls of
  Anguish/Death/Vaught/Nihlathak in Act 5) bypassed all access rules
  in zone_locking mode. AP fill could place a Normal Gate Key at
  e.g. "Clear Halls of Vaught (Hell)", making the Normal gate
  unreachable until the player had already cleared Hell.
  `regions.py:create_regions` now applies a diff-only / prev-act-boss
  rule even for ALWAYS_OPEN_ZONES and unmapped quest IDs, ensuring
  Hell-difficulty locations always require NM Baal kill before AP
  considers them reachable. Verified: re-running the Hell+ZoneLock
  seed shows zero Normal Gate Keys placed at Nightmare/Hell-difficulty
  locations.

Total test runtime: ~3 minutes for the 17-seed sweep. All seeds
validated as winnable (no unsatisfiable goal locations, no missing
items, no fill exceptions).

## apworld follow-up (2026-05-01) — entrance_shuffle gap closed

Pre-release isolation audit caught that the apworld bundle was
missing the `entrance_shuffle` option even though TODO_1.9.0.md and
the System 1 design doc both marked it as wired up. The DLL side
was complete (`g_entranceShuffleEnabled` global, ap_settings.dat
parser at d2arch_ap.c:629, per-character save/load/freeze, undo on
character switch), but the apworld never offered the toggle to YAML
authors and never put `entrance_shuffle` into `slot_data`, so the
bridge had nothing to write into ap_settings.dat. In multiworld
hosts couldn't enforce the option and players who relied on the
title-screen toggle alone got cosmetic-only behavior with no AP
synchronization.

Fixed in three places:

- `options.py` — new `EntranceShuffle(Toggle)` class, default
  false, added to `_FIELDS`
- `__init__.py` — `entrance_shuffle` field added to `fill_slot_data`
- `Settings_Guide.md` — section 8 Shuffles now documents the
  toggle alongside `monster_shuffle` and `boss_shuffle`

Bridge was already auto-writing all slot_data keys to
ap_settings.dat (ap_bridge.py:879 — `f"{key}={val}\\n"` loop), so
no bridge change was needed. Per-character isolation also requires
no DLL change: the existing OnCharacterLoad pipeline captures
entrance_shuffle from ap_settings.dat for new chars, freezes via
`g_settingsFrozen`, persists into d2arch_state_<char>.dat, and
restores on subsequent loads — same pattern as monster_shuffle and
boss_shuffle.

Verified: generated two test seeds, one with `entrance_shuffle:
true` (spoiler shows "Entrance Shuffle: Yes") and one with the
field omitted (spoiler shows "Entrance Shuffle: No" — confirms
default is preserved for existing YAMLs). apworld bundle 33,497 →
33,673 bytes.

## Launcher 1.5.1 — Settings & Logic Guide page

Adds a new in-launcher page that explains every YAML setting, all
four Goal modes, the AP fill / sphere logic, and recommended
profiles for solo and multiworld. The goal is to cut down on the
"how do spheres work" / "what does Skill Hunting do" questions by
putting the answer one click away from the Play button.

### Source

`Settings_Guide.md` lives at the repo root and is fetched from
`https://raw.githubusercontent.com/solida1987/Diablo-II-Archipelago/main/Settings_Guide.md`
on each launcher startup. If the user is offline, the launcher
falls back to the local copy bundled in `launcher_package.zip` at
`launcher/Settings_Guide.md`. If both fail, a stub message points
the user to the online URL.

### Menu changes

The sidebar grew from 7 to 8 entries:

```
News
Patch Notes
Guide      ← new (page index 2)
Settings   ← was index 2, now 3
Game Log   ← was 3, now 4
AP Bridge  ← was 4, now 5
Discord    ← was 5, now 6
Exit       ← was 6, now 7
```

`last_page=2` in old launcher_config.ini files now resolves to
Guide instead of Settings — that's a one-time UX side-effect, not
a regression. New saved values in the 0..5 range round-trip
correctly.

### Version bump

`LAUNCHER_VERSION` 1.5.0 → 1.5.1. `launcher_version.txt` line 1
becomes `1.5.1`, line 2 is the SHA-256 of the new
`launcher_package.zip` (recomputed via
`Tools/_compute_launcher_sha.py`).

`create_release.bat` now also bundles `Settings_Guide.md` into the
launcher zip — the release flow remains unchanged (no extra steps
for the human; the zip just has one more file).

### Staging for upload

`Settings_Guide.md` (repo root) is **untracked-but-staged** —
ready to land in the next push. It joins the standard release file
list:
- `news.txt`, `game_manifest.json`, `patch_notes_X.X.X.md`
- `Settings_Guide.md` ← new
- `game/Archipelago/news.txt`, `game/Archipelago/version.dat`
- `game/D2Archipelago.dll`, `game/START.bat`
- `launcher/Diablo II Archipelago.exe`, `launcher/Settings_Guide.md`
- `launcher_version.txt`, `launcher_package.zip` (force-add)

## Launcher 1.5.1 — Pre-launch installation verifier

Adds a fast file-integrity check that runs the moment the user
clicks PLAY and before `Game.exe` is started. Before the change,
PLAY launched the game blindly — if the previous download had been
truncated by an antivirus product, or a file had been quarantined
post-download, the user would see a crash on startup with no
explanation.

### What the verifier does

1. Reads `<game_dir>/game_manifest.json` (cached locally during
   install/update; falls back to fetching the latest release's
   manifest from the GitHub API if the local copy is missing).
2. For every non-Blizzard file the manifest lists, checks that
   the file exists with the size the manifest claims.
3. Skips the 11 original Blizzard files (D2.LNG, the .mpq pack,
   binkw32.dll, etc.) since those are copied from the user's own
   D2 install and may legitimately differ from any reference size.
4. SHA-256 was tested but rejected — full hash checks added 5-15
   seconds to the launch click-through, which the user described as
   "for langsom" (too slow). Size-only catches the common failure
   modes (truncated downloads, quarantine-deleted files, 0-byte
   stubs left by broken transfers).

### Repair loop

If verification fails, the launcher downloads only the bad files
from `https://raw.githubusercontent.com/.../main/game/<path>`
(uses the repo as a CDN — no archive extraction needed). It then
re-verifies. The cycle runs up to 3 times in a single PLAY click.

A single `_consecutiveLaunchVerifyFailures` counter tracks how many
PLAY attempts in a row have ended with bad files still present. It
resets on the first successful launch.

### Antivirus warning

After 3 consecutive failed verify+repair cycles, the launcher pops
a `MessageBox.Show()` warning that explains the most likely cause
(antivirus / SmartScreen / firewall) and lists 4 mitigations:

- Temporarily disable AV, retry PLAY
- Add the launcher's game folder to AV exclusion list
- Confirm GitHub is reachable in a browser
- Use UPDATE GAME to redownload the full package

The warning's title is `Files keep failing to download` and its
icon is `MessageBoxIcon.Warning`. Posting a Game Log excerpt on
Discord is the suggested escalation path.

### UI changes during verification

The PLAY button label cycles `PLAY → VERIFYING... → REPAIRING... →
PLAY` so the user sees the launcher is working. Status-bar text
also reflects each attempt count. If the user is offline (manifest
unreachable), verification is skipped and the game launches as
before — we don't block offline players.

### Manifest caching

`InstallFromManifestAsync` now writes `game_manifest.json` to the
game directory after a successful manifest fetch, so subsequent
PLAY clicks can verify locally without a network round-trip.
`InstallFromReleaseAsync` already gets the manifest implicitly
because `game_package.zip` ships it at the archive root.

## Launcher 1.5.2 — Iron Frame redesign

Rewires the entire launcher chrome to the hand-painted Iron Frame
art kit (`graphics/panels/`, `graphics/textures/`, `graphics/icons/`
in the design pack). Window grew from **950×580 → 1040×660** to
give the new artwork breathing room.

### Visual changes

- **Stone-wall texture** painted as the whole-window background.
  Replaces the flat `#0F0C0A` rectangle with a subtle weathered-
  courtyard look; an alpha veil keeps it from competing with text.
- **Wider sidebar (165 → 230 px)** so the painted iron-bound page
  sits flush left without cropping its corner ornaments. Nav items
  now inset 42 px from the frame edge per the design HTML's
  `padding: 60px 28px 60px 42px`.
- **Iron arrow active indicator** — the active nav item now shows
  the painted `arrow.png` to its left and gold (#d4a04a) text. The
  old gold-border-on-left strip is gone; matches the design HTML's
  `.navitem.active::before { background: url('arrow.png') }`.
- **Action bar widened to 124 px tall** so it can host the new
  `bottom_bar.png` painted iron rail. Status text + bug-report
  buttons sit on the left, version + INSTALL/PLAY button stack on
  the right.
- **Color tokens calibrated** to the design pack's `tokens.css`:
  void `#0a0807`, stone `#1a1612`, gold `#d4a04a`, gold-hi
  `#f4c97a`, ink `#e8d9b8`, ink-soft `#b8a888`, ember `#c8541a`.
  Constants (`CL_VOID`, `CL_GOLD`, `CL_INK`, etc.) live near the
  top of `MainForm.cs` so future tweaks touch one place.
- **8 nav items still fit cleanly** — News, Patch Notes, Guide,
  Settings, Game Log, AP Bridge, Discord, Exit. The last item is
  flush-bottom-aligned with a flex spacer above it (matches the
  design HTML).

### Functional changes

None — every existing feature is preserved:

- Pre-launch installation verifier (1.5.1) still runs on PLAY click
- Settings & Logic Guide page still loads from raw.githubusercontent
- AP Bridge log + Game Log panes still update live
- Patch Notes still pulls the latest GitHub-release body
- News still pulls `news.txt` from main
- All 7 D2-config dropdowns + 3 folder pickers remain on the
  Settings page

### Files changed

- `Tools/_backup/D2ArchLauncher/MainForm.cs` — window size, layout
  constants, color tokens, OnPaint rewrite, CreateMenuRects rewrite,
  CreateContentArea/SettingsPanel/BottomBar repositioning
- `launcher/ui/parchment.png` — added (1.07 MB)
- `launcher/ui/stone_wall.png` — added (1.71 MB)
- `launcher/ui/wood_dark.png` — added (291 KB) — reserved for
  future panel surfaces
- `launcher/Diablo II Archipelago.exe` — rebuilt (675,041 bytes)
- `launcher_version.txt` — `1.5.1` → `1.5.2` + new SHA-256
- `launcher_package.zip` — repacked (5.5 MB, 26 files)

Smoke-tested by launching at 1040×660 + screenshotting; confirmed
the iron-bound logo renders at top, all 8 nav items show in the
sidebar (PATCH NOTES highlighted in gold), patch-notes content
flows in the right pane, and the bottom bar reads
"Click INSTALL GAME to begin" with the gold INSTALL GAME button
on the right.

### 1.5.2b — visual polish

User-reported follow-up after first 1.5.2 screenshot:

- **EXIT was flush-bottom-anchored** with a 100+ px void above it
  ("ligger mærkeligt i forhold til menuen"). Reverted to a clean
  sequential stack so the 8 nav items flow naturally from top to
  bottom: News, Patch Notes, Guide, Settings, Game Log, AP Bridge,
  Discord, Exit.
- **Status text** "Click INSTALL GAME to begin" + "Installed: …"
  was sitting on the divider line above the painted bottom bar.
  Pushed `statusY` from `barTop+24` to `barTop+42` so the text
  lands inside the readable stone band of `bottom_bar.png` (which
  paints iron edges on its top/bottom ~30 px each).
- **Bug Form / Report Bug** buttons looked like flat dark boxes
  pasted on top of the painted action bar. `MakeSmallButton` now
  wears `small_panel.png` as its `BackgroundImage` (with
  `ImageLayout.Stretch`) so they read as iron-and-stone plates
  with gold text — a coherent extension of the action-bar artwork
  instead of competing with it. Hover swaps text from `--gold`
  (#d4a04a) to `--gold-hi` (#f4c97a).

### 1.5.2c — bottom-bar legibility

Two small fixes after the user spotted them in the v1.5.2b
screenshot:

- **Status text shifted right** from `statusX = 32` to `64`. At 32
  the "Click INSTALL GAME to begin" string was clipping against
  the painted iron-corner ornament on the left edge of
  `bottom_bar.png`. 64 px clears the ornament cleanly.
- **Version sub-line color** was using `CL_INK_MUTE` (#7a6f5c)
  which was barely visible against the dark stone interior of the
  bottom bar. Bumped to `CL_INK_SOFT` (#b8a888) — the same softer
  secondary tone the design HTML uses for `.selected small`. Now
  reads as "Installed: …  ·  Selected: …" in legible parchment
  cream instead of disappearing into the artwork.

## Launcher 1.5.3 — Emerald flame seam

User-requested visual flourish: a band of green hellfire flames
rising from the seam between the masthead and the content area
("ved den røde streg, men de skal ligge bagved alting" — at the
red line, but behind everything).

### Implementation

Flames are generated **procedurally at startup** rather than
shipped as a PNG asset. `MainForm.BuildFlameStrip(width, height)`
returns a 32-bit alpha `Bitmap` containing:

1. A soft emerald-glow base band (vertical `LinearGradientBrush`
   from transparent green to bright `Color.FromArgb(180, 100, 255, 80)`
   over the bottom 55% of the strip).
2. ~75 individual flame tongues — each a Bezier-shaped
   `GraphicsPath` filled with a `PathGradientBrush` whose center
   sits 25% up from the base (mimicking the "burning brightest
   at the bottom" look of the reference image). Tongue widths
   vary 14-32 px, heights vary 55-95% of the strip, lateral
   wobble at the tip ±8 px.
3. ~60 tiny ember sparks (`FillEllipse` with bright
   `Color.FromArgb(180+, 200, 255, 180)` highlights) scattered
   along the bottom band to give the speckled texture the
   reference image has.

A deterministic `Random(0x71AE)` seed keeps the strip stable
across repaints (no flicker if the form invalidates twice in
quick succession).

### Render order in OnPaint

The flame strip is drawn **immediately after the stone-wall
background**, before any other chrome:

```
1. Clear void
2. stone_wall.png
3. _sprFlameStrip   ← NEW (behind everything)
4. Masthead band   (solid dark, then a vertical fade from
                    220-alpha → 0-alpha over its bottom 50 px
                    so flames glow through at the seam)
5. Logo, version label, close button
6. Iron divider line at y = BANNER_H
7. sidebar.png + nav items
8. bottom_bar.png + status text + bug buttons + PLAY
9. content_area.png frame
```

The masthead's `mastFadeStart = BANNER_H - 50` line sets where
the fade begins — solid stone above, transparent at the seam.
That's the only place the flames are visible: the sidebar /
content / bottom-bar all paint opaquely over their respective
regions, so the flames effectively "glow only at the masthead's
bottom edge".

### Files changed

- `Tools/_backup/D2ArchLauncher/MainForm.cs` — `_sprFlameStrip`
  field, `BuildFlameStrip()` + `DrawFlameTongue()` static helpers,
  OnPaint masthead-fade rewrite.
- `launcher/Diablo II Archipelago.exe` — rebuilt
- `launcher_package.zip` — repacked
- `launcher_version.txt` — `1.5.2` → `1.5.3` + SHA-256
  `84280244faa962ae…`

## Version policy
This is a sizeable feature drop combining:
- A new game mode (entrance shuffle)
- A whole event system (Pandemonium with mini + finale recipes + Hellfire Torch)
- A whole new stash column (3 stackable tabs with ghost rendering)
- 41 backported runewords (patch 1.11 wave 1+2 + D2R 2.4 + D2R 2.6)
- A complete skill rebalance (D2R 3.0 values across 210 skills)
- Two infrastructure fixes (socketed-items tab swap + AP Connect race)
- A redesigned cheat menu

Because the scope is more than a typical patch, the version steps
**from 1.8.5 → 1.9.0** rather than 1.8.5 → 1.8.6.

The version stays at **1.9.0** for every fix landed in this development
window. The version number will only change when a release is actually
uploaded to GitHub. All upcoming fixes will be listed under this same
1.9.0 file before that upload happens.

## Bonus check categories (NEW — opt-in)

Six new AP location categories added 2026-05-01. All filler-only;
default OFF (opt-in via title-screen toggles or YAML). Adds up to
~1494 extra check slots when all categories are enabled, per goal scope.

### Categories + quotas (per difficulty)

| Category | Quota | AP IDs | Mechanic |
|---|---:|---|---|
| Shrines | 50 | 60000-60149 | escalating-chance roll on activation |
| Urns/Jars | 100 | 60200-60499 | smash to roll |
| Barrels | 100 | 60500-60799 | smash to roll |
| Chests | 200 | 60800-61399 | open to roll (all chest types) |
| Set Piece Pickups | up to 127 | 65100-65226 | first-time pickup |
| Gold Milestones | 17 total | 65000-65016 | lifetime gold threshold |

### Escalating-chance roll

Each interaction rolls against the active slot's attempt counter:
attempt 1 = 10%, attempt 2 = 20%, ..., attempt 10 = 100% (guaranteed).
Resets to 10% after a hit. Average ~3.5 interactions per slot, max 10.
Slot index advances on each hit; once a category's per-difficulty
quota is reached, no more rolls happen.

### Goal-scope respected

A goal of `full_normal` only includes Normal-tier slots. `full_nightmare`
adds Nightmare's quota on top. `full_hell` adds Hell's quota on top.
Same gating logic as the existing quest categories.

### Filler-only classification

AP fill never places progression items at bonus locations. Even if the
escalating-chance roll never fills every slot (e.g. shrines run out
because D2 doesn't have 50 unique shrines per difficulty in a single
playthrough), the unfilled slots only contain filler items —
gold/xp/traps/drops — so nothing soft-locks.

### Set piece pickup

When `check_set_pickups=true`, the existing F1 Collection page's pickup
detection pipeline fires an extra AP check the first time the player
touches each unique set piece. Goal-mode independent — works in Quest
mode too (not just Goal=Collection). Respects per-set toggles via the
collect_set_* YAML options.

### Gold milestones

Threshold list:
- Normal: 10k, 100k, 200k, 400k, 800k, 1.8M, 3M (7 milestones)
- Nightmare: 3.5M, 4M, 4.5M, 5M, 6M (5 milestones)
- Hell: 7M, 8M, 9M, 10M, 12M (5 milestones)

Triggered by `Coll_AddGoldEarned` watcher — same lifetime gold counter
the Goal=Collection mode uses.

### Title-screen UI

6 new toggle buttons added to column 2 of the title-screen settings
panel (under the existing Monster/Boss/Entrance/Traps shuffles):
Shrines / Urns / Barrels / Chests / Set Pickups / Gold MS. Persist
via d2arch.ini `[settings]` Check* keys for standalone runs; AP
slot_data overrides at character creation.

### F1 Statistics page integration

Page 8 (Statistics / Logbook) gained an "AP CHECK PROGRESS" section
that renders only when at least one bonus category is enabled. Shows
per-difficulty consumed-slot counts (`N x/y NM x/y H x/y`) for each
object category, plus aggregated counters for Set Piece Pickups and
Gold Milestones (with the next gold threshold to chase).

### Per-character persistence

Bonus state lives in `Game/Save/d2arch_state_<char>.dat` alongside the
existing per-char state — survives crashes, character switches, AP
reconnects. Slot indices, attempt counters, and the fired bitmap are
serialized as `bonus_*=...` lines.

## Bonus check categories — polish & bug fixes (2026-05-01 evening)

Follow-up pass after the initial bonus-check ship. Tightens fill safety,
adds standalone reward parity with the quest system, makes the title-
screen layout configurable, and closes a filler-distribution leak that
was bypassing the gating toggles.

### Standalone bonus reward pre-roll

Bonus checks (shrines/urns/barrels/chests/set pickups/gold milestones)
now pre-roll their delivered reward at character creation, the same
way per-quest filler rewards already did. Replaces the prior flat
250-gold token reward with a real value drawn from the same weighted
catalog (gold 1-10K, xp 1-250K, traps, boss loot, charm/set/unique
drops). Each of the 1494 slots has its own deterministic reward
keyed off the character seed.

Implementation lives in `Bonus_PreRollAllRewards(seed)` (called from
`AssignAllRewards`) and `Bonus_DeliverStandalone(apId, tag)` (called
from `Bonus_FireApLocation` when AP is offline). Reuses the trap /
boss / specific-drop pipelines from `d2arch_quests.c` /
`d2arch_gameloop.c` so the in-game effect is identical to a quest
filler firing.

### Spoiler file extension

`Game/Save/d2arch_spoiler_<char>.txt` now appends a "Bonus Check
Rewards" section after the per-quest tables. Lists every shrine /
urn / barrel / chest slot per difficulty, every set piece pickup
(127 entries), and every gold milestone (17 entries) with the
specific reward each will deliver. Only emitted when at least one
bonus toggle is active.

Example lines:
```
  -- Shrines (50 / difficulty) --
    [Normal]
      Shrine #1   -> 4327 Gold
      Shrine #2   -> Drop: Vampire Gaze (Unique)
      Shrine #3   -> Trap: Slow (Decrepify)
```

### Per-set toggle gating for Set Piece Pickups

`Bonus_OnSetPiecePickup` now checks `Coll_IsSetTargeted(setIdx)`
before firing the AP location. Pieces of sets the player toggled
off via `collect_set_*` YAML options no longer count toward the
127 pickup checks. Mapping uses `Bonus_SetIdxFromPiece(pieceIdx)`
which mirrors the cumulative `firstSlot+pieceCount` table from
`g_collSets[]` in `d2arch_collections.c`.

### Filler distribution leak fix

`_create_filler_items` had a "last category gets all remaining slots"
rule that accidentally gave leftover slots to disabled categories:
- Reset Point landing in `skill_hunting=false` seeds
- Traps landing in `traps_enabled=false` seeds

Fix: strip 0-weight rows from the active list before the distribution
loop so the "last gets all" rule only ever picks a real candidate.

Verified across the full test matrix:
- Solo SH=OFF: 0 Reset Points in pool (was 1-2)
- Solo traps_off: 0 Trap items (was 1-2 in small samples)
- 10p MW with mixed SH/ZL configs: 0 leakage

### EXCLUDED LocationProgressType for bonus locations

All 1494 bonus check locations are marked `LocationProgressType.EXCLUDED`
at `create_location` time so AP fill never places progression items
(skills, gate keys) at slots whose escalating-chance roll may not
fire. Verified: 0 D2 skills and 0 D2 gate keys across all bonus
locations in solo + 10p MW tests.

### Title-screen layout — fully configurable

Each of the 6 bonus toggles (Shrines / Urns / Barrels / Chests /
Set Pickups / Gold MS) reads its own X/Y position from
`d2arch.ini [layout]`. Three modes:

1. **Auto-stack** — set `BonusX`, `BonusY`, `BonusSpacing` and the
   6 buttons stack vertically from that base.
2. **Per-button override** — set `<Name>X` and `<Name>Y` per button
   (e.g. `ShrinesX=460 ShrinesY=180`) for free placement.
3. **Default** — `BonusX=460, BonusY=180, BonusSpacing=30` places
   them in a 4th column between the existing class+shuffle stack
   on the left and the quest+collection stack on the right.

Closes the layout-overlap bug a user hit when their `Col1X=Col2X=5`
config caused the new buttons to render on top of the class column.

### F1 Page 7 (Gold tab) milestone strip

The Collection page's Gold tab gained a milestone visualisation
when `check_gold_milestones=true`. 17 milestones rendered in 4
columns below the goal target line, with `✓` markers for milestones
already crossed and compact format (`✓ 10K`, `· 1.8M`, `✓ 12M`).

### Bonus check accessors for UI

Added `Bonus_GetSlotCount`, `Bonus_GetQuota`, `Bonus_IsCategoryEnabled`,
`Bonus_IsGoldMilestoneFired`, `Bonus_CountFiredSetPickups`,
`Bonus_GetGoldMilestoneThreshold` so the F1 Statistics page (Page 8)
and the Gold tab can query bonus state without touching internals.

### Per-character persistence (state file)

`d2arch_state_<char>.dat` save/load extended with:
- `bonus_shrine_<diff>=<slot>,<attempt>` (per-diff slot counter + attempt)
- `bonus_urn_<diff>=<slot>,<attempt>`
- `bonus_barrel_<diff>=<slot>,<attempt>`
- `bonus_chest_<diff>=<slot>,<attempt>`
- `bonus_fired=<hex bitmap>` (5232 bits = 654 bytes covering all 1494 locations + gaps)

Reset bitmap + counters cleared on character switch via
`Bonus_ResetState()` called from `OnCharacterLoad`.

### Session 2026-05-01 — F1 Book redesign + live INI tunables

A multi-day pass over the in-game F1 book to make it navigable,
consistent, and tweakable without relogging. None of these touch
gameplay — purely UI/UX + INI surface area.

#### Top tab bar across every F1 page

Replaces the lone `Next` / `Back` buttons with a horizontal tab
strip at the top of the book. 11 tabs in user-facing order:
`Skills · Overview · Quests · Zones · Sets I · Sets II · Runes ·
Gems · Special · Logbook · AP`. The Zones tab is conditional on
`ZoneLocking=1`. Next / Back still work — they walk the visible
tab order rather than the raw `g_editorPage` value, so they skip
hidden tabs cleanly.

Every tab has its own INI block (button rect + per-tab text
offset). Defaults auto-distribute across `BarX`..`BarX+BarW`.

#### NEW Page 1 — Overview

A Logbook-style summary that gives one-screen visibility into all
checks the character is hunting. Two columns:

- **Left page**: Quests (total) · Skills (when SkillHunting=ON) ·
  Bonus checks (per category, only enabled ones)
- **Right page**: Collection (Set pieces / Runes / Gems / Specials)
  · Zones (per difficulty, when zone-locking) · Total · Goal
  status

Skips lines for categories that aren't enabled, so the page is
clean for runs that only use a subset.

#### Page 3 — F4 Zone Tracker folded into the book

The F4 standalone modal (heavy gold-bordered overlay) is now a
real page in the F1 book. Pressing F4 toggles the book directly
to the Zones tab (or closes if already there).

Redesigned to match the Quest page layout:
- Top-left: 5 act tabs (Act 1..5)
- Top-right: 3 difficulty tabs (Normal / Nightmare / Hell)
- Above the list: per-act header `Act N — Name (X / Y cleared)`
- Left page: scrollable gate list with status badges (`[DONE]` /
  `[PEND]` / `[NOW ]` / `[----]`), `G#`, boss name
- Right page: detail panel for the clicked gate — Status, Boss,
  Zone, Leads to, Key location (when AP-mode + scouted)
- Mouse wheel scrolls the list (uses shared `g_zoneTrackerScroll`)

Selection state is independent per (act, difficulty) tab combo.
`RenderZoneTracker_Body` was refactored to accept a `drawFrame`
flag so the F4 standalone fallback still works (and would render
without the modal chrome when the book is open).

#### F3 in-game tracker — Checks block

Added two compact lines under the per-act header showing total
progress per category:

```
Q 12/40  S 6/40  C 8/205
B 23/1.7K  Z 2/18
```

`K`-suffix on big totals so it fits in the 180 px tracker. Each
category suppressed when its toggle is off. Plugs the F3 coverage
gap (was Quests-only).

#### Page 10 — AP page Item Log overhaul

The right side of the AP page (the "Item Log" block) now lists
actual per-category check counts:

```
Checks (enabled categories):
  Quests:        47 / 80
  Skills:        12 / 30
  Shrines:       18 / 150
  ...
  Total Checks:  92 / 1726
  Goal: In Progress
```

Replaces the previous `Checks: %d / %d` aggregate that lumped
everything together. Reset Points line removed (already shown in
the Skill Manager). Header and body positions INI-tunable via
`[EditorItemLog]`.

#### Page 8 (Specials & Gold) — milestone strip layout fix

The Gold Milestone strip's previous horizontal layout had 7 cells
× 75 px = 525 px wide for Normal alone, way past the 254 px right-
page width. Switched to three vertical columns side-by-side
(Normal / Nightmare / Hell), each milestone stacked downward.
Markers stay ASCII (`+` fired, `-` pending) since D2's bitmap font
garbles `U+2713 / U+00B7`.

#### Quest page — Logbook-style row redesign

Dropped the heavy per-row tile (black background + green-on-done +
border). Quest list rows now plain text with status colors:

- Done = green
- Selected = gold
- Hover = white
- Default = tan (was dark grey)

Right-aligned status column (`Done` / `-` / `kills/req`) matches
the Logbook value column. Row height tightened from 38 → 22 (INI:
`[QuestPage] RowH`).

#### F1 book — uniform "dim text" color

Every dim-grey label (color 5) in the F1 book swapped to tan
(color 7) for readability on the dark parchment. Affects:

- Top tab bar inactive tabs
- Quest page filter / diff / detail labels
- Zones page Status / Boss / Zone / Leads to labels
- Item Log labels
- AP page field labels
- "Recent items received:" / "Quests:" / "Shuffle:" / etc.
- `< Empty >` slot text in skill editor

Page 0 (skill editor) was left untouched per scope; F2/F3/F4 not
in scope.

#### Search box + Specials & Gold title — moved left

User-driven: the Sets-page search box default `SearchX=170 → 130`
and the Specials & Gold page title `TitleX=440 → 280` to clear
visual crowding. INI values updated to match the new defaults.

#### Per-character bonus toggle persistence

The 6 bonus-check toggles (Shrines / Urns / Barrels / Chests /
Set Pickups / Gold Milestones) are now persisted into
`d2arch_state_<char>.dat` as `bonus_enabled=N,N,N,N,N,N`.

Without this, characters created BEFORE the user enabled bonus
toggles in `d2arch.ini` never picked them up on reload because
`g_settingsFrozen` blocked `LoadAPSettings` from reapplying. Now
the toggle state freezes per-character at creation and rides
along with the char file.

#### Skill Hunting OFF — skill pool persistence fix

Bug: a SH=OFF character would show 0 skills on first login
(correct), but logout/login balloon the pool to all 210 skills.

Root cause: `LoadStateFile` always called `InitSkillPool(seed)`
on the `assignments=` marker regardless of whether the char was
created with SH=ON or SH=OFF.

Fix: `SaveStateFile` now writes `pool_kind=hunt|class_only` and
`saved_class_id=N` BEFORE the `assignments=` marker. `LoadStateFile`
dispatches to `InitClassOnlySkills(classId)` for SH=OFF or
`InitSkillPool(seed)` for SH=ON. Backward-compat: missing
`pool_kind` defaults to `hunt` (preserves prior behaviour).

#### Live INI hot reload (no relog)

`d2arch.ini` is now polled for last-write-time once per 500 ms
(`IniHotReload_Tick` in `d2arch_helpers.c`). When the mtime
changes, a global `g_iniGen` counter advances. Every renderer
that loads INI values uses an `INI_HOT_RELOAD_GUARD(s_loaded)`
macro that clears its cache flag on gen mismatch — the next
paint re-reads the INI.

Cost: a single `GetFileAttributesExA()` + two `FILETIME`
compares per check. Imperceptible even on low-end hardware.

Sites covered:
- F1 top tab bar
- Page 1 Overview
- Page 2 Quests (positions, filter tabs, list, progress, level
  strip, Next/Back)
- Page 3 Zones
- Pages 4-8 Collection (already polled every paint)
- Search box, tooltip, Gold milestone strip
- Notifications + zone tracker (`RenderInitFromIni`)
- Item Log

Page 0 (Skill Editor) deliberately not wired — it loads cels +
INI together and a relog is an acceptable cost there.

#### `d2arch.ini` restructured by screen / page

The INI was reorganized so every section is grouped under a
clearly-labelled screen banner. Search any screen name to jump:

```
TITLE SCREEN — MAIN MENU
F1 SHARED                (book frame, top tab bar, Next/Back)
F1 PAGE 0 — SKILL EDITOR
F1 PAGE 1 — OVERVIEW
F1 PAGE 2 — QUESTS
F1 PAGE 3 — ZONES
F1 PAGES 4-8 — COLLECTION  (one section serves 5 pages,
                            sub-headers mark which page each
                            block drives)
F1 PAGE 9 — LOGBOOK
F1 PAGE 10 — AP CONNECTION + ITEM LOG
F2 QUEST LOG
F3 TRACKER + NOTIFICATIONS
F4 ZONES MODAL (fallback)
F8 STASH / SKILL TREE
```

No values changed. 88 new keys added (per-tab tab-bar overrides
already in the file plus the three new sections below).

#### New INI sections

- `[EditorOverview]` — Page 1 layout (TitleX/Y, LeftX/Y, RightX/Y,
  ValueOffX, RowH)
- `[EditorZonesPage]` — Page 3 layout (ActTab*, DiffTab*, List*,
  Detail*, RowH) — mirrors the `[QuestPage]` schema
- `[EditorItemLog]` — Page 10 right-block (HeaderX/Y, BodyX/Y, RowH)
- `[QuestPage] RowH=22` added (was hardcoded)

All defaults match the source defaults exactly so existing
behaviour is unchanged unless the user overrides.

### Session 2026-05-01 (late) — release-day testing + fixes

End-to-end audit of the AP-server → bridge → game item pipeline,
which surfaced and fixed a stack of bugs that had been hiding behind
each other.

#### AP self-release auto-complete (`AP SELF-COMPLETE`)

When the player releases their own check (in-game completion OR via
WebSocket admin command), the local `g_questCompleted[diff][qid]` flag
must be set so the F1 quest list, F3 tracker counters, and Overview
totals all reflect the change.

Bridge `write_unlock` now emits `unlock=<id>|<sender>|<loc>` (new
3-field format; legacy 1- and 2-field still parse). DLL `PollAPUnlocks`
extracts the location, and when sender == own slot AND loc is in the
quest range (42000-44999), decodes `(diff, qid)` and sets
`g_questCompleted[diff][qid] = TRUE`. Logs as
`AP SELF-COMPLETE: marked qid=N diff=D`.

#### Bonus check auto-track from AP unlocks

Same wire-up for bonus check locations (60000-65999). When an AP
unlock arrives whose loc is in the shrine/urn/barrel/chest/set-pickup/
gold-milestone range, the new `Bonus_OnAPItemReceived(apId)` helper:
1. Sets the fired bit in `g_bonusState.fired[]`
2. Bumps the per-difficulty slot counter for shrine/urn/barrel/chest

Result: the Item Log + Overview counters now reflect the AP-server
view (`Shrines: 50/50` after a full release), not just physical
in-game triggers (which would always show 0/N for self-released).

#### Backfill bonus counters at character load

For an existing character whose checks are already on the server but
weren't tracked locally before this fix, `OnCharacterLoad` now reads
the bridge's per-character dedup file directly and replays each bonus
location through `Bonus_OnAPItemReceived` — idempotent, so it just
back-populates the counters without re-firing rewards.

#### Loot delivery — town-skip removed, ilvl floor 30 → 85

User-driven: items must be delivered regardless of where the player
is standing (only monster-spawn traps need a town guard). Both the
boss-loot drain (`g_pendingLootDrop`) and specific-drop drain
(`Quests_PeekPendingDrop`) lost their `IsTown(area)` skip — the queue
no longer waits + then silently drops items in town.

ilvl floor bumped from 30 → 85 so:
- Set pieces spawn as ACTUAL set items (Sazabi's Mental Sheath
  needs req level 34; ilvl 30 fell back to a magic Sturdy Basinet)
- Random Unique drops get full unique TC roll
- Boss-loot TC (Andariel/Mephisto/Duriel) rolls full table including
  signature items, not just low-level potions/keys/gold

#### Specific-drop items go directly to inventory

`bDroppable=1 → bDroppable=0` so charm/set/unique items land in the
inventory if there's room (matches cheat menu / runes / gems
delivery). Boss-loot TC drops still spawn on the ground (TC system
constraint).

#### Item required level → 1

After spawn, `fnSetStat(item, 92, 1, 0)` (stat 92 = `item_levelreq`)
strips the natural required level so any character of level 1+ can
equip an AP-delivered item. The item retains all its set/unique
bonuses; only the equip restriction is lifted.

#### Bridge gate-key routing fix

Gate keys (range 46101-46158) used to fall through to the skill
branch in `process_item` and got logged as "Skill ID 1109" with a
nonsense item name. Added an explicit Gate Key branch that names them
correctly (`Gate Key #N (Normal/NM/Hell)`). DLL behaviour was
already correct via `GateKey_FromAPId` — fix is purely log clarity.

#### Status [OPEN] state for received-but-not-killed gates

F1 Zones page status logic added a 3rd state: when a gate key has
been received via AP but the boss is not yet killed, the gate now
shows `[OPEN]` (light gold) instead of `[----]` Locked. Detail panel
shows "Open (kill boss)" instead of "Locked".

#### F3 tracker scope-aware Bonus counter

The F3 tracker's `B X / Y` line was using `Bonus_GetQuota(c) * 3`,
producing things like `B 584 / 1.4K` for a Goal=Normal-only character.
Now multiplies by `(g_apDiffScope + 1)` so Goal=Normal shows
`B 584 / 584` — matches the Overview / Item Log totals.

#### Bridge resync hack removed

A short-lived `ap_resync.flag` mechanism was added earlier in the day
to force the bridge to re-replay all items at character load. It
caused stackable filler items (gold/XP/stat points) to re-apply on
every login because dedup was wiped each time. The mechanism was
removed; the existing per-character dedup file
(`Game/Save/d2arch_bridge_locations_<char>.dat`) is sufficient on its
own — it persists across game restarts and naturally lets the AP
server's reconnect-replay deliver only items not yet received by
this character.

#### Diagnostics retained

Two `OUTER DRAIN diag:` / `LOOT DRAIN diag:` / `SPEC DRAIN diag:`
log lines (throttled to once per 2 sec) stayed in. Cheap (only when
queue is non-empty), useful for catching future drain regressions.

## Includes all fixes from 1.8.5 and earlier
