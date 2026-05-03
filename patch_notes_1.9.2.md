# Diablo II Archipelago — Beta 1.9.2

In development (no public release yet — version files still read
"Beta 1.9.1"). The big one this cycle: six new opt-in AP check
categories on top of the 1.9.0 bonus checks, a fully customisable
goal mode, restructured options panel, dev-menu Mons tab, smarter
monster shuffle. Total location budget pushed past 2,950 with
everything enabled.

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
Each category has its own ON/OFF button on the title screen and its
own option in the apworld YAML. All are OFF by default — turn on only
what you want. Standalone players can also flip them in
`Game/Archipelago/d2arch.ini` under `[settings]`.

- **Cow Level** (9 slots) — first entry to the Moo Moo Farm per
  difficulty (3), Cow King kills per difficulty (3), and lifetime
  cow-kill milestones at 100 / 500 / 1000 (3).

- **Mercenary Milestones** (6 slots) — first hire (1), 5 / 10 / 25 / 50
  lifetime resurrections (4), first time your merc reaches level 30 or
  higher (1).

- **Hellforge & High Runes** (12 slots) — using the Hellforge per
  difficulty (3) and first pickup of each high-rune tier (Pul-Gul,
  Vex-Ber, Jah-Zod) per difficulty (9).

- **NPC Dialogue** (81 slots) — talking to each major NPC per
  difficulty (27 NPCs × 3 difficulties = 81). Detection runs per
  game tick — when the player is within 4 tiles of a known NPC for
  ~1 second the matching slot fires.

- **Runeword Crafting** (50 slots) — fired sequentially (1st, 2nd,
  ..., 50th) on each successful runeword craft.

- **Cube Recipes** (135 slots) — fired sequentially (1st, 2nd, ...,
  135th) on each successful Horadric Cube transmute.

The F1 Overview gets a new **Extra Checks** section that mirrors the
Bonus Checks section: one row per enabled category showing
`got / total`, with the totals folded into the page-bottom Total
counter. The F1 Logbook (page 9) and the in-game F3 tracker overlay
get matching rows.

In standalone (no AP) mode each slot delivers a per-character
pre-rolled reward (gold / XP / stat / skill / reset / trap /
boss-loot / charm / set / unique drop) using the same weighted
catalog as the quest + bonus pre-rolls. The reward is listed in the
per-character spoiler file (`Game/Save/d2arch_spoiler_<character>.txt`)
so you can preview what each slot will grant before triggering it.

### Custom Goal mode (AP only)

A brand new `goal: custom` value lets you build your own win
condition from a 54-checkbox picker. Goal completes when **all
selected targets are achieved AND lifetime gold reaches your custom
gold target**. The 54 toggles split into 5 collapsed groups in the
Options Creator UI:

- **Subsystems** (10) — bulk includes for skill_hunting, collection,
  hunt_quests, kill_zone_quests, exploration_quests, waypoints,
  level_milestones, story_normal/nightmare/hell
- **Act Boss Kills** (15) — Andariel/Duriel/Mephisto/Diablo/Baal × 3
  difficulties
- **Cow King + Pandemonium Ubers** (7) — Cow King × 3 diff + Uber
  Mephisto/Diablo/Baal + full Hellfire Torch run
- **Famous Super-Uniques** (10) — Bishibosh, Corpsefire, Rakanishu,
  Griswold, Pindleskin, Nihlathak, Summoner, Radament, Izual, Council
- **Bulk Object/Check Targets** (12) — all_shrines/urns/barrels/
  chests/sets/gold-MS + 6 extras-bulk

The standalone spoiler file gets a new "Custom Goal Targets" section
that lists every required target with [X] fired vs [ ] pending markers
plus the gold target.

### Goal: Gold Collection (renamed from `collection`)

The old `goal: collection` mode has been split. The 1.9.2 name is
`gold_collection` — a pure lifetime-gold target with no other
requirements (was previously coupled to the F1 Collection book). The
old name still parses as an alias for backward compatibility.

The F1 Collection book is still tracked and the per-set/per-rune/
per-special collection toggles still work — they just don't drive
goal completion in this mode anymore. To use Collection as a goal,
use `goal: custom` with the `subsystem_collection` toggle ON; the
existing collect_set_*/collect_rune_*/etc. toggles then determine
which items are required.

### Structured options panel (Options Creator)

The Options Creator used to render every D2 option as a flat list
under "Game Options". 1.9.2 splits them into **17 logical categories**
mirroring how OpenTTD organises its options:

```
Game Mode → Goal & Win Condition → Quest Categories → Skill Class Filter
→ Difficulty & XP → Shuffles → Filler Items → Bonus Checks
→ Custom Goal — Subsystems → Custom Goal — Act Boss Kills
→ Custom Goal — Cow King + Pandemonium Ubers
→ Custom Goal — Famous Super-Uniques
→ Custom Goal — Bulk Object/Check Targets
→ Collection — Sets → Collection — Runes → Collection — Gems
→ Collection — Specials → Multiworld
```

Long-tail Collection groups (32 sets + 33 runes + 10 specials) and
all 5 Custom Goal groups are collapsed by default to keep the panel
compact for new users. Plus the `archipelago.json` manifest was
modernised so the Options Creator no longer warns about AP 0.7.0
compatibility.

### Standalone spoiler — grand totals

The per-character spoiler file (`Game/Save/d2arch_spoiler_<char>.txt`)
gets two new bottom-of-file sections:

- **Grand Total** — checks per category (Quest / Skill / Bonus /
  Extra / Collection / Zone) summed to a single TOTAL CHECKS line
  that matches the F1 Overview total exactly
- **Total Reward Mix (all sources)** — same per-reward-type
  breakdown as the existing Quest Reward Mix, but combined across
  Quest + Bonus + Extra reward pools so you can see how many
  gold/XP/stat/skill/trap/loot/drop rewards your entire pool will
  deliver

### Dev menu — Mons tab

Press Ctrl+V → click the new **Mons** tab (rightmost) for
individual-monster spawning grouped under four sub-tabs:

- **SuperUniques** — scrollable list of all 66 vanilla SuperUniques,
  click one to spawn at your cursor
- **Bosses** — 5 buttons for the act bosses (Andariel, Duriel,
  Mephisto, Diablo, Baal)
- **Normal** — scrollable list of all 400 player-killable monsters
  from MonStats.txt, click one to spawn
- **Random** — placeholder for upcoming weighted-roll spawn buttons

Both scrollable lists support mouse-wheel scrolling. Spawn buttons
route through the existing uber-spawn pipeline so the menu also
serves as a test bench for monster behaviour tuning.

### Smarter monster shuffle

Monster shuffle (when enabled in your YAML) now respects a runtime
ban list of about 205 monster rows that were previously eligible to
swap out and would crash the game or break quest progression.
Filtered categories include:

- Spawner monsters (maggot mothers, fetish shamans, Lister's pack)
  whose AI scripts spawn child units that don't exist in the new slot
- Quest bosses tied to specific scripts (Mephisto, Diablo, Baal,
  Lilith, Izual, Hephasto, Council members)
- Special-AI / terrain-bound monsters (Worldstone Keep guards,
  Tyrael's shrine attackers, dummies)
- Player pets / summons (Necromancer skeletons, Druid wolves, golems)
- NPCs and vendors (Cain, Akara, Charsi, etc.)
- Decoration / unused placeholder rows

Belt-and-suspenders with the existing data baked into the 20 shuffle
preset `.dat` files — both layers are active so the runtime filter
catches anything that slipped through the data layer.

---

## Bug fixes

- **Skill pool exhaustion fallback** — pre-existing standalone bug
  found via the new spoiler totals: when Skill Hunting was on and
  the seeded skill pool (max 210 unique skills) was fully unlocked,
  the next ~54 progression-quest completions silently delivered
  nothing (the for-loop walked the pool, found no un-unlocked entry,
  and exited without granting anything). Now each pool-overflow
  quest grants +1 Skill Point as a fallback. AP-mode players were
  unaffected — AP fill places filler items at the overflow slots.

- **NPC dialogue check txtId vs hcIdx** — first-pass NPC detection
  used `pMonsterData + 0x26 (hcIdx)` for NPC matching, but `hcIdx`
  is the SuperUnique field which is always 0 for normal NPCs. Lookup
  always returned -1 → no NPC checks ever fired. Fixed to use
  `unit + 0x04 (txtId)` which matches the MonStats.txt row IDs in
  the lookup table.

- **Hellforge case 49 fall-through** — restructure for the Cat 3
  Hellforge detection accidentally pulled cases 10..48 (all other
  quest objects) into the same body, making any quest-object
  interaction (Cain Gibbet, Charsi imbue, Anya scroll, etc.) falsely
  fire one Hellforge check per game. Split case 49 into its own
  block with break.

- **TradeBtn return-value capture for cube transmute detection** —
  the existing `g_charStats.cubeTransmutes` counter incremented on
  every cube-button click whether the recipe matched or not. Now it
  only increments when the trampoline returns non-zero (recipe
  matched), so the Cat 6 cube-recipe slot fires only on actual
  successful transmutes.

- **Item Log totals matched Overview totals** — the AP page Item Log
  summed quest + bonus + extra checks but missed Skill checks (210),
  Collection checks (205), Zone checks (54). Fixed: now sums all
  six categories and matches the F1 Overview "Checks: X / 2952"
  line exactly.

- **MAX_TITLE_BTNS bumped 64 → 96** — the 6 new Custom-Goal toggles
  pushed the title-screen button count past the old cap, dropping
  Zone Locking + the 5 quest toggles silently. Bumped the cap to 96
  to accommodate.

- **g_fnGetUIVar calling-convention fix** — the existing trampoline
  for D2Client GetUIVar was `__fastcall` but the function pointer
  was typed cdecl, so callers pushed the var-id on the stack while
  the trampoline read garbage from ECX. Caught by NPC dialogue
  diagnostic, fixed via correct calling convention on both the
  declaration and assignment cast. (Diagnostic was later removed
  in favour of a different NPC detection method, but the fix stays.)

- **Custom Goal alias support** — old YAMLs using `goal: collection`
  parse correctly via the `aliases = {"collection": 3}` map on the
  Choice option, so seeds shared from 1.9.0/1.9.1 still generate.

---

## Known issues carried from 1.9.1

- The skill description text still shows the `r-tier` placeholder
  for some unique-class skills — needs MPQ string-extraction work
  which is scheduled for a later release.

- The F1 Quest counter on some characters shows 9 fewer than
  expected; the discrepancy is cosmetic only (the underlying quest
  state is correct).

- F1 Collection counter (`C N/205`) does not count items received
  from the AP server — only items physically picked up. This is
  intentional for now; an inventory-pickup hook may be added later.

- The NPC speech text in the chat box (the floating "Welcome to the
  Rogue Encampment..." lines) is missing on some installs. This is
  pre-existing in 1.9.1 and unrelated to any 1.9.2 work; likely
  environmental (TBL/MPQ data files). The new Cat 4 NPC checks
  still fire correctly via the room-scan detection regardless of
  whether the speech text renders.
