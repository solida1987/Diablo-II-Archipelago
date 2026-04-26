# Diablo II Archipelago - Beta 1.8.4

## Download
Download **launcher_package.zip**, extract to a folder, run **Diablo II Archipelago.exe**.
The launcher downloads and installs the game automatically.

## IMPORTANT: Windows SmartScreen
Click **"More info"** then **"Run anyway"** on the blue warning screen.

## Requirements
- Windows 10/11, .NET 8 Runtime
- An existing Diablo II + Lord of Destruction installation (Classic, NOT Resurrected)

## New Features in 1.8.4
- **Traps toggle on the title screen.** New "Traps" button in the Shuffle column lets you opt out of trap fillers entirely. When toggled OFF, the filler-distribution roller drops trap weight to 0 and proportionally redistributes that share to gold / stat points / skill points / reset points / boss-loot drops, so the pool still adds up to 100% — no empty slots, no broken filler ratios. Setting persists in `d2arch.ini` as `TrapsEnabled=0/1` (default ON for existing installs).

## Bug Fixes in 1.8.4

### Launcher
- (none yet)

### Game Crashes / Critical
- **Fixed: ACCESS_VIOLATION on title screen / startup.** Two latent bugs in `InitAPI` could land the game on the desktop before the menu rendered, depending on ASLR layout for the run:
  1. Ordinal `10042` in D2Common is the **address of** the `sgptDataTables` global pointer, not a function. Old code cast it to a function pointer and called it — execution jumped into data, ran the pointer's bytes as x86 instructions, and could write to random memory before SEH unwound. Fix: dereference the pointer (matches the pattern already used in `d2arch_shuffle.c`).
  2. The D2DebugGame hook installation had no failure path: a failed `VirtualProtect` would still let the `memcpy` patch fire, and an already-patched prologue would be smashed. Now wrapped in `__try`/`__except`, the prologue is sanity-checked before patching, and `VirtualProtect` failures abort the install cleanly with a log line instead of corrupting the page.
- **Fixed: re-entry on `D2Arch_Init` / `InitAPI` corrupted hook bytes.** Both functions now have a one-shot guard. If `DllPostLoadHook` (D2.Detours) and the launcher's bootstrap thread both fire `D2Arch_Init`, the second call is a no-op instead of a re-patch over already-patched memory. Symptom this fixes: the same `mov [edi], eax` instruction crashing repeatedly in the crash log with EDI pointing at the D2DebugGame export.
- **Fixed: duplicate `MainThread` corrupted DrawGameUI / D2DebugGame / GAME_UpdateClients trampolines.** `d2arch_early.txt` revealed the launcher path was producing two `MainThread` invocations — both ran Phase 1, both called `TitleSettings_CreateButtons` (creating 102 buttons instead of 51), and both hit the un-guarded DrawGameUI / GAME_UpdateClients hook installs at the end of the thread. The second install reads our own already-patched `E9 …` bytes as if they were the original prologue, so the new trampoline starts with a JMP back into our hook → "call original" recurses, blows the stack, and EIP ends up jumping into random data (the title-screen `D2Common+0x96A20` ACCESS_VIOLATIONs). The first attempt at a fix used `static volatile LONG` + `InterlockedExchange`, but that didn't work because `D2Archipelago.dll` is loaded TWICE in the same process (once from `Game/D2Archipelago.dll`, once from `Game/patch/D2Archipelago.dll` via D2.Detours) — each image has its own static. Final fix uses **named kernel mutexes** (`D2Arch_MainThread_pid<PID>`, `D2Arch_Init_pid<PID>`) which live in the per-process namespace and ARE shared across DLL images. Whichever copy races to `CreateMutexA` first wins; the loser sees `ERROR_ALREADY_EXISTS` and bails. PID is in the name so two game processes (dual-install play) don't block each other. Every hook install also has a prologue check (`E9` / `CC` / `00` → skip) and a `VirtualProtect`-failure-aborts path so a duplicate call is a no-op rather than a memory-smash.

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

### In-Game Notifications
- **Sender attribution on incoming item banners.** When another player finds a skill for you in the multiworld, the unlock banner now reads `AP: <skill> unlocked! (from <sender slot>)` instead of a generic "AP server" attribution. Self-finds and unknown-sender fall back to the original banner so the panel doesn't get noisy with your own name. Bridge writes sender into `ap_unlocks.dat` using a new `unlock=<id>|<sender>` line format (old `unlock=<id>` is still parsed correctly).
- **Recipient attribution on outgoing check banners.** Sending a check that belongs to another player now shows `<quest name>  ->  <recipient slot>` — e.g. `Den of Evil  ->  test-d2-p1`. Checks for your own slot show `Check: <quest name>` (green Item-Log entry) instead of the old generic "Check sent to AP server!" banner. Lookup uses the per-location-owner table the bridge already writes (`ap_location_owners.dat`), so no new files are introduced.
- **Removed the noisy "Connected! Click Single Player to start." banner.** The green Connect button already signals AP authentication; the redundant banner is gone. The diagnostic log line is preserved.

### Orphaned Save Cleanup
- (none yet)

### Quest System
- (none yet)

### Rewards / Traps / Loot
- (none yet)

### Quests / Shuffle
- (none yet)

### Archipelago Bridge / Multiworld
- **Fixed: checks were not reaching the Archipelago server when characters were created or switched mid-session.** The bridge would latch onto whichever `d2arch_checks_*.dat` existed at first poll and never notice a new character writing a different file. Symptom: completed quests, kills and zones were marked as "Check sent" in-game and written to the local checks file, but never appeared on the AP server, no items came back from progression, and the multiworld stayed silent. Bridge now rescans by modification time on every poll and follows the active character's file automatically.
- **Fixed: skill mapping became stale when characters changed.** Each character has its own skill-pool seed and starter set, but the bridge only loaded the skills database once at connect. After a character switch the bridge would interpret incoming AP item IDs against the wrong character's mapping. Likely root cause for the reported "Grim Ward gave no skill, Double Swing gave Find Potion" mismatch. Skills are now re-loaded from the active character's state file when a character switch is detected.
- **Fixed: new characters in the same slot received zero items (no starter skills, no gate keys, nothing).** The bridge's "already-processed location" dedup was a single global file (`d2arch_bridge_locations.dat`) shared across every character that ever connected through the same install. After one character had run and items had been applied to its `.d2s`, every subsequent character on the same slot would see all items skipped on connect — even the 6 starter skills — because the AP server's `ReceivedItems` replay would land on locations the bridge already considered "done". Symptom: brand-new characters showed up with nothing, despite the server log showing items being delivered. Dedup is now per-character (`d2arch_bridge_locations_<char>.dat`) and the bridge sends an explicit `Sync` request when it detects a character switch, so the AP server replays `ReceivedItems` and the new character receives every item the slot owns. Existing global dedup files are left untouched (ignored, not deleted) so the change is non-destructive.
- **Fixed: only 1 of 6 starter skills came through on character creation.** AP delivers all 6 starter skills with the same special location ID `-2` ("starter inventory"). The bridge's location-based dedup would mark `-2` as processed after the first starter item, then dedup-skip the remaining 5 — so a fresh character would end up with 1 random skill instead of 6. Dedup now only fires on **positive** location IDs (real check locations); starter inventory bypasses location dedup and falls back to the DLL's per-character `d2arch_applied_<char>.dat` for item-id dedup. Stale `-2` entries in existing per-char dedup files were also cleaned up so previously-affected characters can now receive the skills they missed.

### Skill Text
- (none yet)

### Release Pipeline / EULA
- (none yet)

## Known Issues
- Known issues from 1.8.3 still apply until explicitly resolved. See `patch_notes_1.8.3.md`.

## Includes all fixes from 1.8.3 and earlier
