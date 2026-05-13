# Diablo II Archipelago — Beta 1.9.8

Critical hot-fix release on top of Beta 1.9.7. Fixes "Game version is
not supported!" error that 1.9.7 caused for fresh installs.

**If you got "Game version is not supported!" on 1.9.7** — this
release fixes it.

---

## What was broken in 1.9.7

To shrink the download size we added `Game.exe` to the launcher's
"copy from your D2 install" list — but that turned out to be the
wrong call. Here's why:

**Diablo II 1.10f and 1.14 use binary-incompatible `Game.exe` files.**
The 1.10f version is 90 KB. The 1.14 version (which most modern
Diablo II installs are patched to) is 3.5 MB. Our entire mod is
built around 1.10f, so when the launcher copied `Game.exe` from a
user's 1.14 install, the result was an install where `Game.exe` was
the wrong version. D2.DetoursLauncher then refused to launch it
with "Game version is not supported!".

This affected anyone with a modern (1.14-patched) Diablo II install,
which is essentially everyone — Blizzard's auto-patcher upgraded all
installs to 1.14 over the years.

## What 1.9.8 fixes

`Game.exe` is back in the game package (we ship the 1.10f version).
The launcher no longer tries to copy `Game.exe` from your install —
it always uses our shipped 1.10f version. Same applies to all
`D2*.dll` files (Client / Common / Game / Win / Launch / Storm /
Fog etc.) — these are 1.10f binaries the mod is built around and
they ship in the game package.

The big Blizzard data MPQs (`d2char.mpq`, `d2data.mpq`, `d2sfx.mpq`,
plus the original 11 small files) are still copied from your D2
install — those are version-INDEPENDENT and 1.14 versions work fine
with 1.10f code.

## Net result

- **Game package size:** unchanged (~92 MB) — we ship the same
  1.10f binaries as 1.9.4 and earlier
- **Files copied from your D2 install:** 14 (the data MPQs +
  language file + codec libraries) — same as 1.9.7
- **`Game version is not supported!`:** gone. Fresh installs work.

## Lesson learned

In the 1.9.5 cycle a pre-release audit flagged `Game.exe` as a
"Blizzard binary" and we removed it from the package thinking we
were fixing an EULA issue. But 1.10f-specific binaries are exactly
what our mod IS — the audit was right that they have Blizzard
import strings, but wrong that they shouldn't ship. We've shipped
those same 1.10f binaries since the project started; the mod cannot
function without them.

The actual EULA-sensitive files are the large data MPQs
(`d2char.mpq`, `d2data.mpq`, `d2sfx.mpq`, etc. — totalling ~580 MB),
which we correctly do NOT ship and copy from the user's install.
Game.exe (90 KB loader) belongs with our package.

---

## Install / update

1. Download **launcher_package.zip** from this release (overwrite
   your existing launcher)
2. Run **Diablo II Archipelago.exe**
3. Click **More info → Run anyway** on SmartScreen
4. The launcher downloads the game and copies the original Diablo II
   data files for you

You need an existing Diablo II + Lord of Destruction install
(Classic, not Resurrected). Windows 10/11 with the .NET 8 Runtime.

### Existing 1.9.6 / 1.9.7 users

Click **UPDATE GAME** in the launcher — it will download the missing
`Game.exe` (and any other missing 1.10f binaries) automatically.
Your save files and settings are preserved.

### If you got an unsupported-version error on 1.9.7

Either:
- Click **UPDATE GAME** in the launcher to download 1.9.8's correct
  files, OR
- Download fresh `launcher_package.zip` from this release and start
  over with a clean install folder

---

## Apworld update

`world_version: 1.9.8`. If you use it in the Archipelago Launcher's
`custom_worlds/` folder, replace your local copy.

---

## Everything from 1.9.5 + 1.9.6 + 1.9.7

1.9.8 includes everything from those three releases — see their
patch notes for the full list of community bug fixes.

Highlights:
- Items no longer disappear when clicking a stash tab while
  holding an item
- Boss-loot rewards are equippable
- F1 Collection counts AP-delivered items + mercenary equipment
- 8 cross-class animation softlocks fixed
- AP bridge auto-recovers from crashes / hangs
- Bridge no longer crashes on a corrupted state file
- AP connection error reasons now visible
- Tower Cellar entrance shuffle landing fix
- Sacrifice skill drains life properly
- Archipelago Launcher YAML generator no longer crashes
- Game.exe and missing 1.10f binaries now properly shipped

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
Discord.

---

*Build artifacts: `D2Archipelago.dll`, `D2Arch_Launcher.exe`,
`ap_bridge.exe`, `diablo2_archipelago.apworld` (world_version
1.9.8), `game_manifest.json` (version Beta-1.9.8) — all stamped
Beta 1.9.8. Game package now includes 1.10f `Game.exe`.*
