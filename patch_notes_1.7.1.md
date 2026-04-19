# Diablo II Archipelago - Beta 1.7.1

## Download
Download **launcher_package.zip**, extract to a folder, run **Diablo II Archipelago.exe**.
The launcher downloads and installs the game automatically.

## IMPORTANT: Windows SmartScreen
Click **"More info"** then **"Run anyway"** on the blue warning screen.

## Requirements
- Windows 10/11, .NET 8 Runtime
- An existing Diablo II + Lord of Destruction installation (Classic, NOT Resurrected)

## Bug Fixes in 1.7.1

### Launcher (version 1.4.0)
- Fixed install button stuck on "INSTALL GAME" after successful copy (version.dat now always written)
- Fixed launcher update prompt looping forever (hardcoded version check replaced with constant)
- Fixed install path resetting on launcher update (HKLM + GOG + uninstall-key scan added)
- Fixed resolution settings saving catch-22 (settings now queue until game installed)
- Fixed silent partial-copy of Blizzard files (launcher now verifies all 11 files copied)
- Fixed D2 install validation being too loose (now validates all 11 required files)
- Added explicit rejection of Diablo II Resurrected installs
- Added SHA-256 integrity verification of launcher self-updates
- "Report Bug" link now points to GitHub Issues

### Game Crashes / Critical
- Fixed skill tree being empty after character load (ResetD2SFile no longer runs on every load)
- Fixed save-path corruption wiping character state (registry validation added)
- Fixed Whirlwind + bow/crossbow permanent softlock (animation patching now covers SQ/TH/KK modes)
- Fixed all Javelin skills breaking animation with bow equipped
- Fixed Martial Arts requiring unarmed despite weapon restriction removal
- Fixed character-switch state corruption (full global reset on player-gone)
- Fixed .d2s save file corruption on crash (atomic write + generational backup)
- Fixed memory leak on character switch (DC6 resources now freed correctly)
- Fixed XP multiplier integer overflow at high levels

### Rewards / Traps / Loot
- Fixed Amplify Damage curse reducing player's own min attack damage (was wrong stat ID)
- Fixed boss loot drops never firing in Archipelago multiplayer mode (new filler item 45507)
- Fixed loot and monster ambush traps firing in town
- Fixed random boss loot always picking Andariel (GetTickCount double-read bug)
- Fixed "Awesome loot incoming!" notification firing even when no items dropped
- Fixed curses stacking without protection (overlap detection + duration extension)
- Fixed pending loot drops lost on disconnect (now persisted to state file)
- Fixed low-level players getting useless junk from boss TC drops (ilvl floor)

### Quests / Shuffle
- Fixed monster shuffle making quest-critical targets unreachable (exclusion list added)
- Fixed Den of Evil being uncompleteable after shuffle
- Fixed boss TC IDs breaking if TreasureClassEx.txt is modified (name-based lookup)
- Added Nihlathak hunt quest to Act 5
- Fixed Spear Mastery tier inconsistency (now matches Pole Arm Mastery)

### Archipelago Bridge / Multiworld
- Fixed checks being lost when send fails (retry queue added)
- Fixed filler items re-granted on every reconnect (dedup persisted in state)
- Fixed goal completion triggering on wrong difficulty
- Fixed Archipelago-unlocked skills requiring relog to appear (tree rerender after unlock)
- Fixed zone keys vanishing on relog (persistence decoupled from game-mode flag)
- Fixed ap_unlocks.dat race condition between bridge write and DLL delete
- Added monster_shuffle / boss_shuffle slot_data fields to DLL settings parse
- Fixed APworld quest-area map fallback (was sending quests to open_region)

### Skill Text
- Fixed "An Evil Force" text on cross-class skills (patchstring.tbl hash collisions resolved)
- Fixed "Treasure Cow 164 Seconds" tooltip on poison skills (same root cause as above)

### Release Pipeline / EULA
- Release build now routes through _pack_game.py ensuring Blizzard-file exclusion
- Manifest generator no longer bundles crash/log files
- Release body now pulled from patch_notes_*.md instead of hardcoded string
- APworld is now a required release asset

## Known Issues
- Druid/Assassin skill icons may be incorrect (DC6 frame order analysis in progress)
- Multiplayer remains experimental

## Includes all fixes from 1.7.0 and earlier
