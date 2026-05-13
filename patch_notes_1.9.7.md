# Diablo II Archipelago — Beta 1.9.7

Critical hot-fix release on top of Beta 1.9.6. Fixes the install
failure where the launcher couldn't set up the game because it
didn't know to copy four important files from your Diablo II
installation.

**If you got an error like `(D2.DetoursLauncher): The path Game.exe
does not point to a file`** when trying to launch the game after a
fresh 1.9.6 install — this is the release that fixes it.

---

## What was broken in 1.9.6

When we made the game package smaller in the 1.9.5 / 1.9.6 cycle,
we removed several files from the download (`Game.exe`,
`d2char.mpq`, `d2data.mpq`, `d2sfx.mpq`) because we don't have the
right to redistribute them — they belong to Blizzard. The plan was
that the launcher would copy these files from your existing Diablo
II install instead.

**The problem:** the launcher's "files to copy from your D2 install"
list was never updated to include those four files. So the launcher
happily downloaded the (smaller) game package, didn't copy the four
missing files, and then declared the install successful — but when
you tried to launch the game, it crashed because key files weren't
there.

This affected anyone doing a **fresh install** with 1.9.5 or 1.9.6.
Existing installs that already had those files from a previous
version were unaffected.

---

## What 1.9.7 fixes

The launcher's copy list now includes:

- `Game.exe` (Blizzard 1.10f game executable)
- `d2char.mpq` (character art assets)
- `d2data.mpq` (game data)
- `d2sfx.mpq` (sound effects)

When you install or update with the new 1.9.7 launcher, it will
copy all four files from your D2 install automatically. No more
"Game.exe does not point to a file" error.

### Existing players on 1.9.6

If you already got 1.9.6 installed and working, you don't need to
do anything special. The launcher update will fix the four files
silently in the background.

If you couldn't even get 1.9.6 to launch (the screenshot bug), the
new 1.9.7 launcher will recognize your install is incomplete and
copy the missing files on next update.

### New players

Just install normally. The new launcher knows what it needs.

---

## Install / update

1. Download **launcher_package.zip** from this release (and replace
   your existing launcher if you already have one)
2. Extract anywhere, run **Diablo II Archipelago.exe**
3. Click **More info → Run anyway** on the SmartScreen warning
4. The launcher downloads the game and copies the original Diablo II
   files for you

You need an existing Diablo II + Lord of Destruction install
(Classic, not Resurrected). Windows 10/11 with the .NET 8 Runtime.

---

## Apworld update

Apworld bumped to `world_version: 1.9.7`. If you use it in the
Archipelago Launcher's `custom_worlds/` folder, replace your local
copy with the one from this release.

---

## Everything from 1.9.5 + 1.9.6

1.9.7 includes everything from those two releases — the 15
community bug fixes, AP lifecycle hardening, the bridge parser
robustness fix, etc. See the 1.9.5 patch notes for the full list.

Headlines:
- Items no longer disappear when clicking a stash tab while
  holding an item
- Boss-loot rewards are equippable
- F1 Collection counts AP-delivered items
- Mercenary equipment counts toward Collection
- 8 cross-class animation softlocks fixed
- AP bridge auto-recovers from crashes / hangs
- Bridge no longer crashes on a corrupted state file
- AP connection error reasons now visible (no more silent stuck)
- Tower Cellar entrance shuffle landing fix
- Sacrifice skill drains life properly
- Archipelago Launcher YAML generator no longer crashes

---

## Known issues — still under investigation

- **Game crashes after Act 3 in AP mode** (Teddie) — need a
  `d2arch.log` from the moment of crash.
- **Skill page shows no damage / synergy info** (Maegis) — need
  a screenshot of the F1 skill tooltip.
- **Champion enemies have no name** (Maegis) — please screenshot
  a champion when you find one.
- **Some skills still show "an evil force" cross-class** —
  scheduled for a future patch.

If you hit any of these please send a screenshot or log file via
Discord — the more diagnostic info we have, the faster we can fix.

---

*Build artifacts: `D2Archipelago.dll`, `D2Arch_Launcher.exe`,
`ap_bridge.exe`, `diablo2_archipelago.apworld` (world_version
1.9.7), `game_manifest.json` (version Beta-1.9.7) — all stamped
Beta 1.9.7. C# launcher rebuilt with the 4-file copy list update.*
