# Diablo II Archipelago - Beta 1.8.5

## Download
Download **launcher_package.zip**, extract to a folder, run **Diablo II Archipelago.exe**.
The launcher downloads and installs the game automatically.

## IMPORTANT: Windows SmartScreen
Click **"More info"** then **"Run anyway"** on the blue warning screen.

## Requirements
- Windows 10/11, .NET 8 Runtime
- An existing Diablo II + Lord of Destruction installation (Classic, NOT Resurrected)

## New Features in 1.8.5

- **Instant Connect-button feedback.** The Connect button on the title
  screen now flips green the moment the AP server authenticates instead
  of waiting until you've gone in-game and back out. PollAPStatus is
  driven from both the in-game render hook *and* the title-screen
  EndScene hook now.
- **All title-screen settings buttons reflect AP slot_data when
  connected.** Goal, XP multiplier, Class filters, Quest toggles,
  Shuffle (Monster/Boss), Traps, Skill Hunting, Zone Locking — every
  button auto-updates to show the AP server's actual values the
  instant the connection authenticates and stays locked until you
  disconnect. You can see at a glance what the server has configured
  before clicking Single Player.
- **Goal/XP dropdowns are also locked when AP is connected.**
  Previously only the toggle buttons blocked clicks; the dropdowns
  let you change a value the server would just overwrite. Now
  consistent with the rest of the UI.
- **Dedicated Treasure-Cow log file.** Every cow-spawn decision is
  recorded to `Game/Archipelago/treasure_cow.log` with a wall-clock
  timestamp. Includes a 10-second heartbeat so you can verify the
  spawn function is being called at all, plus per-decision lines for
  every roll, skip-reason and spawn outcome. Same lines mirrored to
  `d2arch.log` with a `[TC]` prefix.
- **AP gate-key location data plumbing.** The bridge writes
  `ap_item_locations.dat` whenever it scouts your missing locations.
  The DLL reads this file as the foundation for the upcoming
  F4-tracker overlay that shows where each gate key is hiding.
  Currently captures keys placed in your own world; cross-world keys
  (the typical multiworld case) require hint data — planned for a
  later 1.8.5 sub-fix.

## Bug Fixes in 1.8.5

### Archipelago Bridge / Multiworld
- **Items dropped because of dedup collisions across players** — the bridge
  was using a single integer (`location_id`) as the dedup key, which meant
  two different senders' items with the same location number were silently
  collapsed and one was lost. The dedup key is now a tuple
  `(sender_slot, location_id)`, so every distinct sender→receiver pairing
  is preserved. This was responsible for ~52 silently-dropped items in a
  recent multiworld test run.
- **Bridge now retains item info from `LocationInfo` responses** — previously
  the bridge logged only the recipient player; the actual item ID was thrown
  away. Items are now persisted to `ap_item_locations.dat`.
- **Gate-boss kills now actually reach the AP server.** Before 1.8.5 the
  DLL wrote `WriteAPCommand("location N")` for each gate kill, but the
  bridge's command dispatcher only handles `connect`/`disconnect`
  actions — every gate-kill location check was silently dropped, leaving
  the AP server unaware and therefore never echoing back the gate-key
  item. The kills are now serialised through the same
  `d2arch_checks_<char>.dat` pipeline that quest completions use, which
  the bridge already polls and uploads. Multiworld Zone-Locking runs
  no longer dead-end after the first gate.
- **DLL now understands all bridge status strings.** Previously
  `connected` and `reconnecting` from the bridge were silently dropped —
  the UI would lie about the connection state during reconnects. The
  status panel now shows `Authenticating...` for the brief
  `connected`-but-not-yet-authenticated window and `Reconnecting...`
  while the bridge is in retry-backoff.

### Boss Spawning / Zone Locking
- **Gate-boss kill detection no longer requires the kill to happen in the
  exact gate zone.** D2's 1-per-game SuperUnique guard could prevent our
  gate-boss spawn at the configured gate zone whenever vanilla map
  placement had already instantiated the same `hcIdx` elsewhere — for
  example a Bishibosh gate in the Mausoleum versus the canonical Bishibosh
  in Cave Level 2. Killing the canonical SuperUnique anywhere in the
  difficulty now satisfies the gate. `hcIdx` is unique within each
  preload's four gates, so this can't false-trigger a sibling gate.

### Launcher
- **Update button now respects the version dropdown.** Previously the
  state machine always compared the latest GitHub release against the
  installed version on disk, which meant picking a previously-installed
  older build from the dropdown still showed "UPDATE GAME" — and clicking
  did download the dropdown choice, but the label lied. The launcher now
  recomputes its state on every dropdown change: selected matches
  installed → PLAY, selected differs → UPDATE GAME (which downloads the
  selected release tag, including downgrades).
- **Stale-file cleanup on update.** When the launcher updates an
  existing install via the manifest path (`Update Game` button), it
  now deletes files in mod-managed directories (`patch/`,
  `ap_bridge_dist/`, `apworld/`) and the small allow-list of top-level
  mod binaries that are not in the new manifest. Previously, files
  that one release shipped and the next removed lingered forever and
  could break the new build — the historical workaround was a fresh
  install / manual delete. User-managed paths (saves, character state
  files, `d2arch.ini`, original Blizzard files) are never touched.

### Multiworld Generation (apworld)
- **Level milestones are now sphere-correctly gated.** A
  `Level 30 (Normal)` milestone could previously hold an early-act
  progression item like `Act 1 Gate 2 Key (Normal)` because the
  location had no access rule, leading to "I need this key to leave
  Region 1 but the key is at a Level 30 location and I can only level
  in Region 1". Each level milestone now requires reaching an
  appropriate prior boss (`Sisters to the Slaughter` for L20, `The
  Guardian` for L30, previous-difficulty `Eve of Destruction` for L35+
  and L60+), keeping milestones in later spheres than the gate keys
  they could otherwise block.

### Settings UI / Title Screen
- **Connect-button now turns green the moment the AP server
  authenticates, even on the title screen.** Pre-1.8.5 the button stayed
  red until the player went into a character and back out. The polling
  for AP status was previously only wired into the in-game render hook
  (`DrawGameUI`), which doesn't fire on the title screen. Polling now
  runs from `WndProc` which fires on every Windows message — paint,
  mouse-move, etc. — so the cell-file swap to green happens within
  ~2 seconds of bridge authentication, regardless of where the player is.
- **Zone-Locking soft-lock recovery (race-window + already-stuck chars).**
  Players reported "I set zone_locking: false in my YAML but my zones
  are still locked AND the keys never generated." Root cause: if the
  player clicked Single Player BEFORE the Connect button turned green,
  the per-character state file got baked from the local `d2arch.ini`
  (which often had `ZoneLocking=1` from a previous standalone run)
  instead of the AP server's `slot_data` (which correctly said
  `zone_locking=0`). The apworld then correctly skipped generating gate
  keys, but the DLL froze the wrong value forever — locked zones with
  no keys in the multiworld = soft-lock. Two fixes:
  1. **Race-window detection.** Character creation no longer freezes
     settings when `g_apMode=TRUE && g_apConnected=FALSE` (still
     authenticating). The freeze is deferred until `PollAPStatus`
     observes the FALSE→TRUE auth transition; at that moment the DLL
     re-runs `LoadAPSettings` (now reading `ap_settings.dat`), rewrites
     the per-character state file, and only THEN freezes.
  2. **Already-stuck-character reconcile.** When an existing AP
     character is loaded (or AP authenticates on the menu while no
     character is loaded yet), the DLL compares the per-character
     state's `zone_locking` value with `ap_settings.dat`. If the
     character says ON but the multiworld says OFF, the DLL forces
     `zone_locking=OFF`, clears the stale lock map, persists the
     correction, and shows "Zone Locking auto-disabled (matches AP
     server)". This rescues players who got bitten by the original
     race window in 1.8.4 or earlier.
- **Title-screen Goal label fixed for 1.8.x encoding.** F1 page-2 and
  the title-screen Goal dropdown both rendered the wrong text after
  the 1.8.0 simplification (15 entries → 3 difficulty scopes). The
  dropdown text and the `Goal: …` label now correctly map
  `g_apGoal=0/1/2` to "Full Game (Normal/Nightmare/Hell)".
- **Stale `Goal=14` (legacy 0..14 encoding) in older `d2arch.ini`
  files is now normalized to 0..2 on load** so a leftover INI from
  a 1.7.x install doesn't drag a new character into broken
  goal-completion logic.
- **The visibility loop now resyncs settings buttons on title-screen
  re-entry when AP is still authenticated.** Pre-1.8.5 buttons
  recreated on menu return would briefly display the stale
  `d2arch.ini` values until PollAPStatus' next transition fired —
  visible flicker.

### Rewards / Traps / Loot
- **Traps no longer evaporate while shopping** — the trap-spawn step ran
  every game tick and decremented `g_pendingTrapSpawn` even when the player
  was in a town area, which meant any trap that arrived while you were
  visiting Akara or the stash was silently consumed and lost forever. The
  spawner now defers in-town traps and only fires them once the player
  steps out of the town tileset, with a 5-minute watchdog so a queue
  cannot accumulate indefinitely if the player parks in town and quits.
- **Treasure Cows now re-roll while you stay in an area.** The previous
  one-roll-per-area behaviour combined with the default 25% chance meant
  ~42% of acts could finish without ever spawning a cow even if the player
  spent fifteen minutes there. The roll now repeats every 60 seconds while
  you are in the same outdoor area, and the default chance was raised
  to 40% in `treasure_cows.dat`. Cumulative spawn probability over a
  typical 5-minute area visit is now ≈92%.

## Known Issues / Pending in 1.8.5
- F4 tracker does not yet render per-gate AP key locations. The data
  pipeline is wired up (bridge → file → DLL global), but the UI render is
  still pending and will land in a later 1.8.5 sub-fix before upload.
- `ap_item_locations.dat` only captures keys placed in *your own* world.
  Cross-world key placements (the typical multiworld case) require hint
  data and will be added before upload.
- Known issues from 1.8.4 still apply until explicitly resolved. See
  `patch_notes_1.8.4.md`.

## Version policy
The version stays at **1.8.5** for every fix landed in this development
window. The version number will only change when a release is actually
uploaded to GitHub. All upcoming fixes — F4 render, hint-based cross-world
key tracking, and anything else that surfaces during testing — will be
listed under this same 1.8.5 file before that upload happens.

## Includes all fixes from 1.8.4 and earlier
