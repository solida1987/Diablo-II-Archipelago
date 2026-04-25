# Diablo II Archipelago - Beta 1.8.1

## Download
Download **launcher_package.zip**, extract to a folder, run **Diablo II Archipelago.exe**.
The launcher downloads and installs the game automatically.

## IMPORTANT: Windows SmartScreen
Click **"More info"** then **"Run anyway"** on the blue warning screen.

## Requirements
- Windows 10/11, .NET 8 Runtime
- An existing Diablo II + Lord of Destruction installation (Classic, NOT Resurrected)

## Bug Fixes in 1.8.1

### Launcher
- (none yet)

### Game Crashes / Critical
- (none yet)

### Stash / Inventory
- (none yet)

### Skill System
- (none yet)

### Zone Locking
- (none yet)

### Boss Spawning
- (none yet)

### Game Modes / Settings
- (none yet)

### Title Screen / Settings UI
- (none yet)

### Orphaned Save Cleanup
- (none yet)

### Quest System
- (none yet)

### Rewards / Traps / Loot
- (none yet)

### Quests / Shuffle
- (none yet)

### Archipelago Bridge / Multiworld
- **Generation no longer fails for default settings.** The apworld's `create_regions` was missing the skill-hunt branch (regions and locations were never created when Zone Locking was OFF), causing `KeyError: 'Eve of Destruction'` and "Player had N more items than locations" during YAML generation. Fixed in `regions.py`.

### Skill Text
- (none yet)

### Release Pipeline / EULA
- **AP Bridge now ships with fresh installs.** `_pack_game.py` was excluding the `ap_bridge_dist/` folder (containing the frozen `ap_bridge.exe`) from `game_package.zip`, so launcher-installed copies had no bridge binary — the DLL logged `AP Bridge: not found (tried ap_bridge_dist/ and root)`. The exclusion was removed; the bridge is now included in the package.

## Known Issues
- Known issues from 1.8.0 still apply until explicitly resolved. See `patch_notes_1.8.0.md`.

## Includes all fixes from 1.8.0 and earlier
