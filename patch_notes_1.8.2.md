# Diablo II Archipelago - Beta 1.8.2

## Download
Download **launcher_package.zip**, extract to a folder, run **Diablo II Archipelago.exe**.
The launcher downloads and installs the game automatically.

## IMPORTANT: Windows SmartScreen
Click **"More info"** then **"Run anyway"** on the blue warning screen.

## Requirements
- Windows 10/11, .NET 8 Runtime
- An existing Diablo II + Lord of Destruction installation (Classic, NOT Resurrected)

## New Features in 1.8.2

### Zone Locking — major rework
- **F4 tracker fully redesigned.** New 520x500 panel with status badges per gate (`[DONE]` / `[PEND]` / `[NOW ]` / `[----]`), boss name, zone name, act-name headers, "→ R2/R3/…/act boss" pointers and a current-target highlight. Complete map of where each gate boss is and which region it unlocks.
- **Gate-boss kill state is now persistent.** Killing a gate boss is permanent — no respawn after quit + reload, no respawn on re-entering the zone. Saved to `d2arch_state_<char>.dat`.
- **Standalone Zone Locking is fully playable now.** When AP is disconnected, killing a gate boss immediately grants the gate key locally and opens the next region. Previously only worked with an AP server connected.
- **Per-character random gate layouts in standalone mode.** Each new offline character rolls its own (act × difficulty) preload selection, mirroring what the apworld does in `generate_early()`. No more identical Corpsefire / Radament / Web Mage layout in Normal, Nightmare and Hell.
- **"SOMETHING UNUSUAL" banner.** Big red centered text when the mod spawns something extra in the current area (Treasure Cow today, future variants later). Generic so it doesn't spoil the surprise.

### Title screen
- **Connect button turns green** the moment the AP server authenticates so it's visually obvious you're now using server slot_data.

## Bug Fixes in 1.8.2

### Game Crashes / Critical
- **Skill points no longer migrate to wrong skills after relogging.** Three independent bugs were causing invested skill points to scatter across unrelated skills (most visibly on Barbarians in class-only mode). Fixed: (1) `InsertSkillInClassList` no longer reorders the class-skill-list on every click; (2) reinvest consumer writes per-button cache files at the original `btnIdx`, not a compact array index; (3) `ResetD2SFile` now reads from the `.d2s` skill section as the single source of truth instead of trusting the per-button cache files (which could be polluted by earlier sessions).
- Stale per-button level-cache files (`d2arch_fireball_<char>.dat`, `d2arch_skill<N>_<char>.dat`) are now wiped at character load and rebuilt from the authoritative `.d2s` skill bytes during the reinvest pipeline.

### Skill System
- **Barbarian: missing skill in class-only layout fixed.** Spear Mastery was tier-1 in the skill DB, giving the Barb 11 T1 / 9 T2 / 10 T3 skills against a fixed 10-per-tier layout — the 11th T1 was silently dropped, leaving an empty cell. Spear Mastery moved to T2 so the Barb panel now shows all 30 native skills (10 / 10 / 10).
- **Skill investment error message corrected.** Trying to invest in a skill below your level used to flash "No skill points available!" because the level check fell through into the points-zero path. The message now reads `You are not high enough level (need X, you are Y)` and the points-zero message only fires when you actually have zero points.
- **Skill level cap honoured during reinvest.** `PatchAllSkillAnimations` runs before reinvest fires (it already did) and now also raises wMaxLvl=99 before fnAddSkill, so saved levels above 20 don't silently revert to 20 after a relog.

### Zone Locking
- Gate-boss kill detection works even when AP is disconnected (was AP-only; now grants the key locally).
- `CustomBoss_AppendGateBosses` skips gates whose boss has already been killed on the current difficulty — prevents respawn after a quit + reload cycle.
- `g_gateKeyReceived` and `g_gateBossKilled` are now zero-cleared on character switch. Previously a save-file with no entries inherited whatever was in memory from the prior character.

### Title Screen / Settings UI
- **XP Multiplier setting now actually applies.** `TitleSettings_Save` was writing the `XPMultiplier` INI key twice in a row — first with the correct picker value, then immediately overwriting it with a stale `1`. The duplicate write is removed.

### Game Modes / Settings
- **Strict per-character settings isolation.** Once a character is created, its randomization settings are read ONLY from `d2arch_state_<char>.dat`. The title-screen UI, `d2arch.ini` and any `ap_settings.dat` from the active AP session are ignored at load time for existing characters. New characters still capture from the active source (AP slot_data if connected, UI otherwise) and bake those values once. Re-baking the per-char file mid-session is not possible — the only way to change a character's randomization settings is to delete and recreate that character.
- **`ap_settings.dat` no longer leaks into offline characters.** `LoadAPSettings` now requires `g_apConnected == TRUE` before reading the AP slot_data file. When AP is offline the title-screen `d2arch.ini` is the single source of truth.
- **Stale `ap_settings.dat` is scrubbed at every standalone launch.** A previous session's slot_data left over from an alt-F4 / crash no longer pollutes the next character creation.
- All 31 per-character randomization globals are explicitly reset to safe defaults before `LoadStateFile` runs, so a character file missing fields can't inherit values from the previously-loaded character.

### Archipelago Bridge / Multiworld
- F1 disconnect / reconnect to a different AP server only changes the connection — randomization settings stay frozen on the character.

### Release Pipeline / EULA
- Version strings consolidated under `D2ARCH_VERSION_DISPLAY` so future patch bumps no longer require touching `d2arch_versionpatch.c`'s byte-pattern table.

## Known Issues
- Known issues from 1.8.1 still apply until explicitly resolved. See `patch_notes_1.8.1.md`.

## Includes all fixes from 1.8.1 and earlier
