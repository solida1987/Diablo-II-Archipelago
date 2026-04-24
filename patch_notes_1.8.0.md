# Diablo II Archipelago - Beta 1.8.0

## Download
Download **launcher_package.zip**, extract to a folder, run **Diablo II Archipelago.exe**.
The launcher downloads and installs the game automatically.

## IMPORTANT: Windows SmartScreen
Click **"More info"** then **"Run anyway"** on the blue warning screen.

## Requirements
- Windows 10/11, .NET 8 Runtime
- An existing Diablo II + Lord of Destruction installation (Classic, NOT Resurrected)

## New Features in 1.8.0

### Multi-Tab Stash (20 tabs, cross-character shared storage)
- **10 shared stash tabs** (`P`, `SH2`–`SH10`) shared across every character on your install
- **10 AP stash tabs** (`P`, `AP2`–`AP10`) for AP-enabled characters — per-character, unlocked only on AP chars
- Post-goal AP characters get access to all 20 tabs
- "P" (Personal) tab = your character's private stash that lives in the `.d2s` save file
- Tabs 2–10 persist in sidecar files (`shared_stash_ser.dat` for SH, `ap_stash_ser_<char>.dat` for AP)
- Click any tab to switch what you see — items are drained/spawned via D2's native serialization so nothing is lost
- Auto-swap back to "P" when you close the stash, so your save file stays consistent

### Quick-Actions (Shift + Right-Click)
- **Shift+right-click a backpack item** → flies into the active stash tab's first free slot
- **Shift+right-click a stash item** → flies back to the first free inventory slot
- **Shift+right-click at a vendor** → sells the item directly (validated via D2's own sell function, not a packet hack)
- No more drag-and-drop needed for routine storage / selling

### Version Display
- "Beta 1.8.0" shown in main menu, in-game HUD, tab-map overlay, and launcher "Installed:"
- CMD window is hidden when launching from the launcher

### Bug Report / Support Links
- **Report Bug** button → GitHub Issues
- **Bug Form** button → Google Docs spreadsheet for longer feedback

### Zone Locking — Semi-Random Gated Progression (rewritten)
Zone Locking is no longer "all zones locked + 36 progression keys". It is now a semi-random gated system with a fixed number of gates per act and randomized boss placements per character.

- **Per-act gate counts (fixed):** Act 1 = 4, Act 2 = 4, Act 3 = 4, Act 4 = 2, Act 5 = 4. Total **18 gates per difficulty** (up to 54 across Normal + Nightmare + Hell).
- **3–4 preloads per act:** Each preload defines where the gate boss spawns for a given act. Act 1/2/3/5 have 4 preloads each; Act 4 has 3 (limited by its 6-zone layout).
- **Per-character randomization:** At character creation, 15 preload IDs (5 acts × 3 difficulties) are baked into the character's state file and frozen for the rest of that character's life. Two characters on the same AP seed share the same preload layout; new AP seeds roll a new layout.
- **Act transitions preserved:** You still need to defeat Andariel → Duriel → Mephisto → Diablo → Baal in order. Gates are *within* each act's progression, not between acts.
- **Difficulty transitions preserved:** Nightmare requires completing Normal Baal; Hell requires Nightmare Baal.
- **54 unique gate-boss encounters possible:** Gate bosses are superuniques like Corpsefire, Bishibosh, The Smith, The Countess, Shenk, Snapchip Shatter, Frozenstein — relocated to different zones per preload so no two seeds feel identical.
- **Gate-boss kill = AP location check:** Killing the gate boss in its spawn zone sends a location check to the AP server. In multiworld, the reward (a Gate Key) may go to any player's game.
- **Chaos Sanctum, Ancients, Baal subjects, and Hephasto are never used as gate bosses** so vanilla quest content remains intact.

### F4 Zone Map rewritten
The F4 zone-progression map now matches the gated system:
- 3 tabs for Normal / Nightmare / Hell (only those you're actually playing)
- Per-act "(X / Y)" counter showing how many gate keys received out of the act's total
- Per-gate LOCKED/OPEN status with region-unlock hint ("Gate N [LOCKED] — kill boss in Rn")

### AP options panel cleaned up
Removed deprecated/duplicated options from the APWorld configuration:
- **Removed:** `Game Mode` (deprecated — superseded by Skill Hunting + Zone Locking toggles)
- **Removed:** `Skill Pool Size` (always 210 internally; class filter handles restriction)
- **Removed:** `Starting Skills` (hardcoded to 6 — matches in-game default)
- **Removed:** 6 × `Filler: <Type> Weight` percentages (internal balance; DLL uses fixed defaults)
- **Removed:** 15 × `Act <N> Preload (<Diff>)` fields (AP auto-generates randomly per slot in `generate_early()`)
- **Removed:** `Story Quests` toggle (D2 engine-required; story quests always ON)
- **Removed:** `Shop Shuffle` toggle (no shuffle logic implemented in DLL)
- **Removed:** `I Play Assassin` toggle (reader was removed in 1.7.1; trap skills now always excluded from pool)
- **Simplified:** `Goal` from 15 options (Act 1 Normal … Full Game Hell) to **3 options** — Full Normal / Full Nightmare / Full Hell. Act scope always = full 5-act game; only difficulty varies.

The AP options panel now shows only what the in-game title-screen settings panel actually has: 19 user-facing toggles + standard AP DeathLink.

### DeathLink improvements
The DeathLink trap system got two usability fixes so it works sensibly in mixed-level multiworld seeds:
- **Level-matched trap monsters.** Previously every DeathLink trap spawned a random Baal Subject (SU 61-65, monster level 85) regardless of the player's character level — which instakilled low-level characters if any other player died mid-Act-1. The trap now reads the player's `STAT_LEVEL` and picks from 5 tiered SU pools: lvl 1-14 → Bishibosh/Rakanishu/Corpsefire/etc, lvl 15-29 → Fangskin/Bloodwitch/Coldworm/etc, lvl 30-49 → Endugu/Stormtree/Winged Death/etc, lvl 50-69 → Ancient Kaa/Dac Farren/Pindleskin/Snapchip/etc, lvl 70+ → Baal Subjects (old behavior preserved for end-game).
- **Trap notification shows who died.** Previously a generic `"AP TRAP! Monster incoming!"` popup. Now the bridge writes `ap_deathlink_event.dat` with the dying player's slot name, and the DLL reads it when processing the trap unlock so the popup reads `"DEATHLINK: <player> died - trap incoming!"`. Item Log entry is updated to `"DeathLink trap from <player>"`.

### Stash tab rendering fix
Fixed stash tab buttons (P / SH2..SH10 / AP2..AP10 column) appearing on the character screen (C-key), skill tree (T-key), and quest log (Q-key) panels. The old detection read D2Client's shared `screen_shift` global, which flips to `LEFT` for *any* left-side panel. The new detection reads D2Client's per-panel UI state array directly at RVA `0x11A6A8` (`gpUIState[0x19]` = UI_STASH) so the overlay only renders while the native stash is actually open in town. Verified by disassembly cross-reference of `D2Client_GetUIState` @ `0x6fb23230` and `D2Client_ToggleUI` @ `0x6fb23260`.

## Bug Fixes in 1.8.0

### Launcher
- CMD window no longer flashes on launch
- `launcher_config.ini` now displays version correctly (no stray dash)

### Game Crashes / Critical
- Fixed `Gfx.cpp line 627: ptGfxInfo` assertion when items were re-spawned from serialized bytes — items now use D2's proper place-into-player function so client-side graphics data is populated correctly

### Stash / Inventory
- Fixed tab button text position (was rendering above the colored box instead of inside)
- Fixed "ghost item" cells — clearing ppItems grid pointers explicitly so newly-placed items don't collide with previously-drained ones
- Fixed mouse-click detection for stash tab buttons (was using manually-scaled window coords; now uses the same `MouseX()/MouseY()` source that the F1 skill editor and other UI use, so clicks register correctly in all resolutions)
- Fixed inventory-record ID used for stash placement (was 28 = 800×600 menu variant; now 12 = LoD `Big Bank Page 1`, which is the actual in-game stash grid)
- Fixed item position read — uses `pStaticPath->tGameCoords.nX/nY` (D2's real grid coords) instead of `pExtraData.nNodePos` (which is a grid-type marker, not a coordinate)
- Fixed vendor-sell false-positive: shift+rclick at the stash no longer triggers the sell path (we now check that the interact unit is `UNIT_MONSTER`, not a stash chest which is `UNIT_OBJECT`)
- **Fixed stash tab buttons appearing on character screen (C-key panel)**. The old detection read D2's shared `screen_shift` global which flips to "LEFT" for *any* left-side panel (character sheet, skill tree, quest log, stash, …), so the tab column would render whenever any of those were open. The new detection reads D2Client's live `gpUIState[UI_STASH]` array directly (RVA 0x11A6A8, UI_STASH=0x19), which is truly stash-specific. Tabs now only appear while the native stash chest panel is open in town.

### Skill System
- **Skill level cap raised from 20 to 99** — you can now invest up to 99 base points in any skill. Previously D2 silently capped every skill at level 20 (or whatever `maxlvl` Skills.txt said), so extra clicks beyond 20 were lost. Fix patches `wMaxLvl` in the in-memory Skills.txt record to 99 for all 7 classes' skills at DLL init.
- Fixed post-reload level reset: skills invested past 20 used to snap back to 20 after exiting and re-entering the game, because the reinvest loop ran before the maxlvl patch took effect. The patch is now applied at the very start of reinvest processing so saved levels 21-99 restore correctly.
- Item +skill bonuses (e.g. "+3 to Fire Bolt" on gear, masteries) continue to stack on top of the base level and are clamped at player max (99) — unchanged from vanilla behaviour.
- Monsters are unaffected (their skills were already exempt from the cap check).

### Boss Spawning
- Temporarily disabled the default Blood Moor "Archipelago Test Boss" spawn (baseSU 61 = Colenzo, level 85) in `Game/Archipelago/custom_bosses.dat`. The Treasure Cow system is now the only active boss-spawn during a session. Uncomment line 76 in the config to re-enable.

### Game Modes — redesigned
- **GameMode dropdown replaced by two independent ON/OFF toggles**: `Skill Hunting` and `Zone Locking`. Both can be active simultaneously (hybrid mode grants zone key + bonus skill per progression quest).
- Toggles sit above the quest-type column on the title screen using wider (250×35) button graphics so the full labels fit.
- Runtime global `g_gameMode` (int) replaced by `g_skillHuntingOn` + `g_zoneLockingOn` (BOOL × 2); all 7 consumer sites (zone locking init, area-lock check, F4 tracker, menu item count, zone tracker render, quest reward logic, editor mode display) migrated.
- Apworld: new `SkillHunting` and `ZoneLocking` Toggle options. Legacy `GameMode` Choice kept for back-compat with older yamls — if set to `zone_explorer`, seeds `zone_locking=1 skill_hunting=0` automatically.
- Bridge: `ap_settings.dat` now emits `skill_hunting=N` + `zone_locking=N` + legacy `game_mode=N`. DLL parses all three with the new fields winning.

### Skill Hunting OFF — class-only skill layout
- When Skill Hunting is OFF and a new character is created, the skill system skips the randomized pool entirely. The character gets **exactly its class's 30 native skills**, pre-assigned to their tier positions in the 30-slot tree (tab 0 = tier 1, tab 1 = tier 2, tab 2 = tier 3).
- The editor's pool side (drag-drop area) stays empty — there's nothing to drag because every skill is already assigned. The tree side displays the class's standard skills in their natural positions.
- No cross-class skills appear; drag between slots is blocked; swap/remove is unreachable.
- Skill points can still be invested via the "+" buttons; skills level up normally.

### Skill level requirement enforcement
- Skill points can no longer be invested into a skill whose `reqlevel` in `Skills.txt` exceeds the character's current level. A notification pops up ("Required Level: X (you are level Y)") and the click is rejected. Matches vanilla D2 behaviour.
- Level is read from server player's STAT_LEVEL (12); reqlevel was already parsed into `g_skillExtra` at character load.

### Per-character settings baked at creation
- All 31 game-mode / shuffle / quest / filler / class-filter settings are now written to and read from `d2arch_state_<charname>.dat`. Previously only ~10 fields persisted per-character; the rest silently inherited from the title-screen INI each session.
- New `g_settingsFrozen` flag: set TRUE after `OnCharacterLoad` restores the per-char state, cleared on "Player gone" (exit to title screen). While TRUE, `LoadAPSettings` is a no-op — AP reconnects and title-screen toggle changes do not override the character's baked settings.
- Fields now persisted per character: `skill_hunting`, `zone_locking`, `monster_shuffle`, `boss_shuffle`, `shop_shuffle`, `xp_multiplier`, `class_filter`, `cls_amazon` through `cls_assassin` (all 7), `i_play_assassin`, `death_link`, `filler_loot_pct` (plus the 10 that already persisted: goal, starting_skills, skill_pool_size, 5 quest toggles, 5 of 6 filler percentages).

### Title Screen / Settings UI
- **Removed three dead toggle buttons**:
  - `Story` — D2's native main-story quests are engine-required; the toggle only hid them from the UI which was misleading. Story quests are now always-ON internally (never togglable).
  - `PlayAsn` — legacy 1.7.0 trap-filter flag, the reader was removed in 1.7.1, button had no effect.
  - `Shop` — shop-shuffle flag was parsed from slot_data but no shuffle logic ever existed. Removed until the feature is actually built.
- **Connect button now changes colour** to reflect AP status: red when disconnected, yellow when connected. No more guessing whether the bridge got through.
- **AP-override lockout**: while connected to AP, clicking any title-screen toggle shows "AP connected - settings locked (managed by server)" instead of silently flipping a value that gets overwritten by slot_data. Standalone mode is fully interactive as before.
- **Disconnect cleanup**: `ap_settings.dat` is now deleted automatically when the bridge disconnects (or the server drops the connection). This stops the "sticky AP mode" bug where a standalone character loaded after disconnect would still apply leftover AP settings from the previous session. Per-character AP bindings (`d2arch_ap_<char>.dat`) are kept so the character can auto-fill the server/slot fields on next connect.

### Orphaned Save Cleanup
- **Per-character file cleanup now actually works**. When a D2 character is deleted, the mod's associated save files in `Game/Archipelago/` are scrubbed.
- The old cleanup had three bugs:
  1. The file glob was `d2arch_*.dat` which silently ignored `ap_stash_<char>.dat` and `ap_stash_ser_<char>.dat` — so every test character's stash dump accumulated forever.
  2. A redundant "abort if no .d2s files in save dir" guard prevented cleanup from running after a full character wipe (since cleanup runs on character load, the directory always has at least one .d2s by the time we reach it — guard was defensive cosmetics).
  3. Cleanup only fired from `OnCharacterLoad` — if the user deleted their only character and exited without creating a new one, cleanup never ran at all and orphan files persisted.
- Fixes: glob is now `*.dat`, the known-prefix list now covers `d2arch_ap_`, `ap_stash_`, `ap_stash_ser_`, plus the pre-existing state/slots/checks/reinvest/fireball/skillN prefixes, and `CleanupOrphanedSaves()` is now also called from `D2Arch_Init` (DLL load) so it runs on every game launch, not only on character load.
- **Shared stash preserved**: `shared_stash.dat` and `shared_stash_ser.dat` (account-wide shared chest) do not match any per-character prefix, so they are never touched. Same safety for `ap_settings.dat`, `custom_bosses.dat`, `treasure_cows.dat`, `skill_data.dat`, and every other global config file.
- One-time scrub shipped with 1.8.0 removed 58 orphan files left over from test characters.

### Quest System (custom AP quests)
- **Quest-type toggles now actually disable quests**, not just hide them from UI. Previously, turning off `Hunting`/`KillZn`/`Explore`/`Waypnt`/`Levels` only filtered the tracker display; the quests still completed in the background, granted rewards, and (in AP mode) sent location checks to the server. That made the toggles effectively fake.
- All four consumer sites now honour `IsQuestTypeActive`:
  - **Tracker** (always did): hides rows for disabled types.
  - **Quest book** (F1 page 2): filtered list no longer shows disabled-type quests; empty tab displays "This quest type is disabled in settings."
  - **Act progress counters** (`Act I: N/M` in tracker and "Progress: N / M" in book): only active-type quests contribute to the total, so the numerator and denominator match the visible list.
  - **Quest completion** (`OnQuestComplete`): early-return when the type is disabled — no reward grant, no AP check sent, no completion flag set. The quest type is truly disabled end-to-end.
- Story-type quests are unaffected (`g_apQuestStory` is forced to TRUE since the toggle was removed — D2's engine requires main story quests regardless).

### Rewards / Traps / Loot
- (none yet — 1.7.1 fixes carried forward)

### Quests / Shuffle
- (none yet — 1.7.1 fixes carried forward)

### Archipelago Bridge / Multiworld

#### Gate-key item pool (NEW for 1.8.0 Zone Locking)
- 54 new AP items defined: `Act <N> Gate <M> Key (Normal/Nightmare/Hell)` with AP IDs 46101-46118 (Normal), 46121-46138 (Nightmare), 46141-46158 (Hell). All marked as `ItemClassification.progression` so the fill algorithm respects them as required progression.
- 54 new AP locations defined: `Act <N> Gate <M> Cleared (Normal/Nightmare/Hell)` with location IDs 47010-49053 (formula: `47000 + diff*1000 + act*10 + gate_idx`).
- Slot_data now exports 15 new integer fields: `actN_preload_<diff>` for N=1..5, diff=normal/nightmare/hell. The DLL reads these, bakes them into the character's state file at creation, and uses them to initialize the region lock table and gate-boss spawn table.

#### Gated region tree for AP logic
- APWorld `regions.py` now builds a per-(act, difficulty) region tree when zone_locking is enabled. Each region R1..R_{gates+1} connects to the next via a `state.has("Act N Gate M Key (Diff)", player)` access rule.
- **Act transitions:** Act N+1 R1 requires `can_reach_location` of Act N's boss quest (Sisters to the Slaughter / Seven Tombs / The Guardian / Terror's End). AP can no longer place early progression items in Act 5 zones assuming the player can teleport there.
- **Difficulty transitions:** Nightmare and Hell entry requires `can_reach_location` of Eve of Destruction on the previous difficulty. AP cannot place NM/Hell keys at sphere-0 locations assuming the player already has Nightmare access.
- **Quest location placement:** All ~400 quest locations are now placed in their physical region (e.g., "The Search for Cain" lives in Dark Wood which is in Act 1 R2, so AP knows you need Act 1 Gate 1 Key to reach the check). This prevents a whole class of multiworld BK-mode bugs where a progression item could be placed behind the very key it unlocks.

#### Item classification rules (1.8.0 final)
- **Zone Locking ON → Gate Keys are progression.** Required to reach gated regions.
- **Skill Hunting ON → Skills are *useful*** (not progression). Nice-to-have gameplay upgrades but never strictly required to complete the goal.
- **Neither toggle ON → Story quests are progression** (default items.py behavior). Standard single-player progression where the 6 story quests per act drive AP sphere logic.
- **Both ON (hybrid) → Gate Keys progression + Skills useful.** Most interesting mode for multiworld.

#### Boss-kill detection sends AP checks
The in-game SU-kill hook in `d2arch_gameloop.c` now matches killed superuniques against the active preload's gate roster. When `(hcIdx, current_area, current_difficulty)` matches a gate entry, the DLL writes `location <loc_id>` to the bridge, which forwards to the AP server. Received Gate Keys flow back through the existing `ap_unlocks.dat` poller and are dispatched to `UnlockGateKey()`.

### Skill Text
- (none yet — 1.7.1 fixes carried forward)

### Release Pipeline / EULA
- (none yet)

## Known Issues
- Mouse-to-grid calibration for quick-actions is hardcoded for 1068×600 resolution with the Ultimate Inventory mod installed. Other resolutions or mod combinations need recalibration (see `Research/MULTI_TAB_STASH_IMPLEMENTATION.md` section 7.4 for the procedure).
- The currently active non-P tab is not remembered across sessions — every login starts on "P". Items are preserved correctly; only the initial view resets.
- **Goal option still has 15 entries** (Act 1 Normal → Full Game Hell). Will be simplified to 3 (Full Normal / Full Nightmare / Full Hell) in a follow-up patch; existing 15-option encoding continues to work, just emits extra act-scope values that the new gate system ignores.
- **Zone-locking mid-session difficulty switch**: gate-key receipts (`g_gateKeyReceived`) are tracked per-difficulty. When you exit to lobby and create a new game on a different difficulty, `InitZoneLocks_FromPreloads()` re-applies the correct set of locks for the new difficulty. No known bugs, but worth being explicit about.
- **Multi-game AP placement can put a Gate 1 Key behind level milestones**: In mixed-game seeds, AP may place e.g. "Act 1 Gate 1 Key (Normal)" at another player's "Reach Level 20" check. AP considers "Reach Level 20" a sphere-0 location (levels can be earned anywhere), but physically the other player has to actually play up to level 20 before sending the key. This is standard AP multiworld behavior, not a D2-specific bug — but it can cause perceived slow progression early.

## Technical Documentation
- Full implementation details for the multi-tab stash (D2 function ordinals, struct offsets, calibrated coordinates, file formats, troubleshooting checklist) are in `Research/MULTI_TAB_STASH_IMPLEMENTATION.md`.
- Full design + per-preload spec for the gated zone-locking system is in `Research/zone_locking/` — 22 markdown files including `00_DESIGN.md` (framework + AP logic), `ZONE_LEVELS.md` (mlvl reference for all 132 zones), `INDEX.md` (master index), and 19 per-preload docs (`ACT1_LOAD_1.md` through `ACT5_LOAD_4.md`). The DLL-side data tables in `d2arch_preloads.c` are direct C translations of these docs.

## Includes all fixes from 1.7.1 and earlier
