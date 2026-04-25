# Diablo II Archipelago - Beta 1.8.3

## Download
Download **launcher_package.zip**, extract to a folder, run **Diablo II Archipelago.exe**.
The launcher downloads and installs the game automatically.

## IMPORTANT: Windows SmartScreen
Click **"More info"** then **"Run anyway"** on the blue warning screen.

## Requirements
- Windows 10/11, .NET 8 Runtime
- An existing Diablo II + Lord of Destruction installation (Classic, NOT Resurrected)

## New Features in 1.8.3
- (none — this is a hot-fix release for 1.8.2 issues reported in multiplayer/long-running games)

## Bug Fixes in 1.8.3

### Game Crashes / Critical
- **Connect button no longer stuck on "Disconnected" after AP authentication.** A regression introduced in 1.8.2's strict-settings-isolation fix created a chicken-and-egg in the AP connection flow: `g_apMode` was only set TRUE inside `LoadAPSettings`, which only ran when `g_apConnected` was TRUE — but `g_apConnected` was only set inside `PollAPStatus`, which early-returned when `g_apMode` was FALSE. Result: the bridge would authenticate successfully, the AP server's text-tracker would show the player online, items would flow, but the in-game F1 panel kept showing "Disconnected" and the Connect button stayed red. Clicking Connect now sets `g_apMode = TRUE` immediately so `PollAPStatus` starts polling `ap_status.dat` and the rest of the flow falls into place.

### Zone Locking
- **Mid-game upgrades now reconcile gate-key state from D2's actual quest progress.** Players who upgraded from a pre-1.8.2 build whose per-character state file never recorded gate-key receipts (or recorded them under a different setting) reported being stuck unable to enter Act 3+ regions even though they had already killed Andariel and Duriel. Two reconciliation passes fix this:
  1. **At character load** — after `LoadChecks` reads the per-char state file, if any of the act-boss quests (Sisters to the Slaughter, Seven Tombs, The Guardian, Terror's End, Eve of Destruction) are completed in the state file, every gate key for that act and all earlier acts is implicitly granted via `UnlockGateKey()`. Locks are removed and the state file is re-saved with the correct entries.
  2. **Every game tick** — after `CheckQuestFlags` reads D2's authoritative server-side quest flags via `fnGetQuestState`, the same act-boss → gate-key inference runs. This catches the case where the per-char state file is empty but the .d2s itself shows the player has progressed. Within a few game ticks of character load the relevant gates auto-unlock.
- **Gate keys are now exempt from AP-item dedup.** The dedup table that prevents double-applying additive AP items (gold, stat points) was also blocking gate-key re-application on AP reconnect. Since `UnlockGateKey()` is idempotent, gate-keys (apId 46101–46158) now bypass dedup so every AP replay re-syncs `g_zoneLocked[]`. Useful for: multiworld games where another player gives you a key while you're offline; mid-game DLL upgrades that lost the in-memory lock state; AP reconnects after a crash.

### Archipelago Bridge / Multiworld
- F1 disconnect/reconnect to a different AP server now correctly transitions the in-game state without leaving the Connect button red.

## Known Issues
- Known issues from 1.8.2 still apply until explicitly resolved. See `patch_notes_1.8.2.md`.

## Includes all fixes from 1.8.2 and earlier
