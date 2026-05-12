# Diablo II Archipelago — Beta 1.9.6

Hot-fix release on top of Beta 1.9.5. Fixes a critical bug where the
Archipelago bridge could crash on startup if your character's saved
state file had even a single corrupted line in it — and re-issues
the manifest so the launcher can verify cleanly.

**If you're already on 1.9.5:** your launcher should pick this up
automatically when you click **UPDATE GAME** or relaunch.

**If you're stuck on the "Files keep failing to download" error:**
that's exactly what 1.9.6 fixes. Just download the new
`launcher_package.zip` from this release and overwrite your existing
launcher folder, OR delete `Game/game_manifest.json` from your
install and click UPDATE GAME again to fetch the fresh manifest.

---

## Install / update

1. Download **launcher_package.zip** from this release
2. Extract anywhere, run **Diablo II Archipelago.exe**
3. Click **More info → Run anyway** on the SmartScreen warning
4. The launcher downloads the game for you

You need an existing Diablo II + Lord of Destruction install
(Classic, not Resurrected). Windows 10/11 with the .NET 8 Runtime.

---

## ⚠ IMPORTANT: Apworld update required

The apworld bumped to `world_version: 1.9.6` in this release. If
you're using it in the Archipelago Launcher's `custom_worlds/`
folder, replace your local copy:

1. Open `C:\ProgramData\Archipelago\custom_worlds\` (or wherever
   your Archipelago Launcher lives)
2. Delete `diablo2_archipelago.apworld`
3. Copy the new `diablo2_archipelago.apworld` from this release
4. Restart your Archipelago Launcher

---

## What 1.9.6 fixes

### Bridge no longer crashes on a corrupted state file (Alphena)

**Before:** If your character's saved state file
(`d2arch_state_<char>.dat`) had even one malformed line — for
example a stray colon in a column that should only contain `0` or
`1` — the entire Archipelago bridge would crash on startup with a
Python `ValueError`. The DLL would helpfully respawn the bridge,
which would crash again, and again, and again. From the player's
perspective the game just refused to connect to AP, with no clear
reason why.

**After:** The bridge now logs and skips bad lines individually
instead of crashing on them. Even if your save file has a couple
of garbled lines, the bridge keeps running and loads everything
that IS valid. Bad lines get logged in `ap_bridge_log.txt` so
diagnostic info is still preserved. First five malformed lines
are logged, after that the spam is suppressed.

This single fix unblocks anyone who was getting "Files keep
failing to download" or "AP won't connect" with no obvious cause.

### Launcher manifest no longer mismatches after a hot-fix

**Before:** When we patched the bridge mid-1.9.5 and re-uploaded
the GitHub release assets, the launcher's cached manifest had old
SHA256 checksums that didn't match the new file bytes. After three
failed retries, the launcher gave up with "Files keep failing to
download". You couldn't get the fix because the launcher refused
to install it.

**After:** 1.9.6 ships as a clean version bump with a fresh
manifest from scratch. The launcher will see it as a new release
and download cleanly without any leftover SHA confusion.

---

## All fixes carried over from Beta 1.9.5

1.9.6 includes everything from 1.9.5 — see the full notes for
Beta 1.9.5 for the complete list of community bug fixes. Headlines:

- Items no longer disappear when clicking a stash tab while
  holding an item
- Boss-loot rewards (Andariel/Duriel/etc Loot) are now equippable
- F1 Collection page now counts AP-delivered items
- Mercenary equipment also counts toward Collection
- Sacrifice skill drains life properly again
- 8 cross-class animation softlocks fixed (Whirlwind / Leap Attack
  / Double Swing / Double Throw / Dragon Talon / Dragon Claw /
  Blade Fury / Amazon javelins / Druid Rabies-Hunger / Smite)
- AP bridge auto-recovers if it stops responding
- AP disconnect / reconnect on-screen notifications
- Per-character AP spoiler + checklist files
- Server / slot change detection + dedup wipe
- Force Reconnect button on F1 Editor
- AP connection error reasons now visible (no more silent
  "Connecting...")
- No more ghost ap_bridge.exe processes after game close
- Tower Cellar entrance shuffle landing spot fix
- Shared stash no longer ships with dev test items
- Bridge log file ~70% smaller
- Archipelago Launcher's "Generate Template Options" no longer
  crashes when our apworld is installed

---

## Known issues — still under investigation

- **Game crashes after Act 3 in AP mode** (Teddie) — multiple
  suspected causes; need a `d2arch.log` from the moment of crash.
- **Skill page shows no damage / synergy info** (Maegis) — need
  a screenshot of the F1 skill tooltip.
- **Champion enemies have no name** (Maegis) — please screenshot
  a champion when you find one.
- **Some skills still show "an evil force" cross-class** —
  scheduled for a future patch (requires regenerating the
  in-game string table).

If you hit any of these please send a screenshot or log file via
Discord — the more diagnostic info we have, the faster we can fix.

---

*Build artifacts: `D2Archipelago.dll`, `D2Arch_Launcher.exe`,
`ap_bridge.exe`, `diablo2_archipelago.apworld` (world_version
1.9.6), `game_manifest.json` (version Beta-1.9.6) — all stamped
Beta 1.9.6.*
