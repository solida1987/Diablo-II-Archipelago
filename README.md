# Diablo II Archipelago

A randomizer mod for **Diablo II: Lord of Destruction (1.10f)** with [Archipelago](https://archipelago.gg/) multiworld support.

Randomizes skill unlocks across a quest system spanning all 5 Acts and 3 difficulties. Complete quests, hunts, zone clears and more to earn skills from any of the 7 character classes. Play solo with your own settings, or connect to an Archipelago multiworld server for cross-game randomization.

## The Multiworld Launcher is required

This mod is a **plugin** for the
[**Multiworld Launcher**](https://github.com/solida1987/Multiworld-Launcher)
and does not work without it. That is not packaging made awkward on purpose —
the launcher *is* the randomizer. It writes the seed, patches the data tables,
injects the mod, holds the Archipelago connection and tracks your checks.
Nothing installs or runs the mod on its own, and there is no in-game login to
deal with.

The launcher is a **separate download from its own project**, and it ships
with no games in it: every game arrives as a plugin file you fetch and add
yourself. Read what the launcher is on its page before you download it — you
are choosing to run it, so you should know what it does.

If you would rather not use a launcher, this mod is not for you — better to
know that before downloading than after.

---

## Download & Install

1. Download **launcher_package.zip** from the
   [Multiworld Launcher releases](https://github.com/solida1987/Multiworld-Launcher/releases/latest)
   (version **3.4.0 or newer** — the current plugin refuses to load on
   anything older, and says so), extract it anywhere you have write access,
   and run **`Multiworld Launcher.exe`**. The library will be empty — that is
   correct.
2. Download **`diablo2_archipelago-*.londonplugin`** from
   [this project's latest release](https://github.com/solida1987/Diablo-II-Archipelago/releases/latest).
3. In the launcher, click **Add plugin…**, pick the file, **read the dialog**
   — it shows who published the plugin, what it declares it will do, and the
   SHA-256 of the file — and approve it. Diablo II appears in the library.
4. Click Diablo II in the library, then **Install** — the launcher downloads
   the mod and installs it, using the required game files from your own
   Diablo II 1.10f installation.
5. The launcher keeps itself up to date. Mod updates are an optional button.
   A newer plugin is added the same way as the first one — the consent dialog
   appears again because the file changed, which is the point.

**INSTALL.md** on the release walks the same road in more detail.

> **One thing the launcher cannot supply for you:** a DirectDraw wrapper. The
> 2003 engine will not open a window on Windows 10 or 11 without one, so the
> launcher checks for it before it starts the game and stops with instructions
> if it is missing. Most people already have one and never notice —
> [details below](#required-a-directdraw-wrapper-cnc-ddraw).

### Requirements
- Windows 10 / 11
- A valid, legally-owned installation of **Diablo II** + **Lord of Destruction**, **patched to 1.10f**.
  Modern installs update themselves to 1.14 — **[DOWNGRADING.md](DOWNGRADING.md)**
  walks you through getting to 1.10 using your own discs and Blizzard's own
  patch server.
- **A DirectDraw wrapper — [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw/releases/latest).**
  The game cannot start without one on Windows 10 or 11. It is free, and if your
  own Diablo II folder already has a `ddraw.dll` the launcher takes care of it
  for you — see [Required: a DirectDraw wrapper](#required-a-directdraw-wrapper-cnc-ddraw) below.

**Why 1.10f specifically:** the mod hooks fixed addresses inside the 1.10f engine, so it cannot run on 1.13c or 1.14. Nothing belonging to Blizzard ships with this project — no game data and no engine binaries. On install, the launcher copies both out of your own installation, and it checks every engine file by exact size first, so pointing it at the wrong patch level fails with an explanation instead of a crash.

If your copy is 1.14 (the current Battle.net download), note that 1.14 merges the engine DLLs into the main executable, so those files are not present at all. You will need a 1.10f installation of your own before installing the mod.

### Getting your installation to 1.10f

This project does not distribute Blizzard's patches and does not link to any source for them — obtaining the 1.10f patch is up to you. The process itself is:

1. **Install Classic Diablo II + Lord of Destruction from your own copy** into its own folder. Keep it separate from any 1.14 installation you already have, so patching one does not disturb the other.
2. **Apply the 1.10f patch** to that folder.
3. **Check that it worked.** In a correct 1.10f folder you will see:
   - `Game.exe` at about **90 KB** (a 1.14 `Game.exe` is roughly 3.5 MB), and
   - the separate engine libraries next to it — `D2Client.dll`, `D2Game.dll`, `D2Common.dll`, `Storm.dll` and the rest. If those files are absent, the folder is still 1.14.
4. **Point the launcher at that folder** and install. The launcher verifies every engine file by exact size, so if something is off it tells you before installing rather than after.

### Required: a DirectDraw wrapper (cnc-ddraw)

**The game will not start without this one.** Diablo II 1.10f is from 2003 and
its own DirectDraw no longer initialises on Windows 10 or 11 — without a wrapper
it stops with *"Error 22: A critical error has occurred while initializing
DirectDraw"* before the main menu.

The mod does not include one. Install it yourself:

1. Download **cnc-ddraw** from
   https://github.com/FunkyFr3sh/cnc-ddraw/releases/latest
2. Open the zip and copy **`ddraw.dll`** (and `ddraw.ini` if present) into your
   **game folder** — the folder holding `Diablo II.exe`. The launcher opens it
   for you from **Open game folder**.
3. Press Play.

cnc-ddraw is free and open source (MIT) by FunkyFr3sh. If your own Diablo II
installation already has a `ddraw.dll`, the launcher copies that one across
automatically and there is nothing to download. If it cannot find one anywhere,
it stops before launching and tells you this rather than letting the game fail
with an error box that explains nothing.

### Optional: HD graphics, free resolution and 3D sound

Three well-known Diablo II community projects make the game look and sound
considerably better. **They are not distributed with this mod** — you download
them yourself, from their authors, under their own licences. That is deliberate:
they are licensed GPL-3.0, AGPL-3.0 and LGPL-2.1, and those licences place
conditions on *distributing* the software that this project is not in a position
to meet. Using them alongside this mod is entirely your right as a user; handing
them out is what carries obligations.

All three are optional. Skip them and the game still runs, just at the original
resolution and without 3D sound.

Everything goes in the **same place**: your game folder, the one holding
`Diablo II.exe`. The launcher opens it for you from **Open game folder**. No
subfolders, no installers — the files sit next to the game.

#### D2GL — HD rendering, widescreen, filtering, higher frame rates

Download: **https://github.com/bayaraa/d2gl/releases/latest**

The zip has everything at the top level. Copy these three into the game folder:

| File | |
|---|---|
| `ddraw.dll` | the renderer |
| `glide3x.dll` | what `-3dfx` loads — the launcher looks for this one |
| `d2gl.mpq` | its data, ~65 MB |

> **D2GL also satisfies the DirectDraw requirement.** Its `ddraw.dll` is a
> DirectDraw wrapper in its own right, so if you install D2GL you do not need
> cnc-ddraw as well — one or the other is enough.

#### SGD2FreeRes — removes the fixed-resolution limit

Download: **https://github.com/mir-diablo-ii-tools/SlashGaming-Diablo-II-Free-Resolution/releases/latest**

> **Take the "Vanilla" download, not "Modders".** Installing an add-on into a
> mod makes "Modders" look like the obvious choice, and it is the wrong one: that
> build ships its graphics in a `data/` folder you are expected to repack into
> `patch_d2.mpq` yourself. The Vanilla build works by dropping the files in,
> which is what this mod's settings are written for.

From the Vanilla zip, copy `SGD2FreeRes.dll` and `SGD2FreeRes.mpq` into the game
folder.

Note that SGD2FreeRes does nothing on its own — **D2GL is what loads it**,
through `load_dlls_late` in `d2gl.ini`. Install it without D2GL and its files sit
there doing nothing. The launcher says so if that happens, rather than leaving
you to guess.

#### DSOAL — restores the original 3D positional audio

Download: **https://github.com/kcat/dsoal/releases/latest**

This one takes the most steps, because the download is a zip inside a zip:

1. Extract `DSOAL.zip`. Inside is a single file, `DSOAL_r694.zip` (the number
   changes between releases).
2. Extract that one too. You now have two folders: `DSOAL` and `DSOAL+HRTF`.
   Either works — HRTF adds headphone positional audio. Pick one.
3. Open the **`Win32`** folder inside it.
4. Copy `dsound.dll`, `dsoal-aldrv.dll` and `alsoft.ini` into the game folder.

> **It must be `Win32`, not `Win64`.** Diablo II 1.10f is a 32-bit program, so
> the 64-bit build simply will not load — and on a modern machine `Win64` is the
> folder people reach for first. If your 3D sound does not work, this is almost
> always why.

#### After installing

Start the launcher and look at **Settings → Diablo II Archipelago**. The
**Optional add-ons** section lists all three and says which ones it can see, so
you can check the install landed before you play. There is a **Check again**
button, so you can drop files in with the launcher still open.

A complete install of all three looks like this in your game folder:

```
ddraw.dll          d2gl.mpq           SGD2FreeRes.mpq
glide3x.dll        SGD2FreeRes.dll    dsound.dll
dsoal-aldrv.dll    alsoft.ini
```

This mod ships tuned settings for them — `d2gl.ini`, `d2gl.json` and
`SGD2FreeResolution.json` — so once the files are in, the configuration is
already the one this mod expects. Those are settings files, not the programs.

Read each project's own licence and documentation. They are separate works by
separate authors; this project is not affiliated with them and does not speak
for them.

### Antivirus & Windows SmartScreen

Because the launcher is currently unsigned, Windows SmartScreen or some antivirus products may display a warning the first time you run it. Unrecognised, unsigned programs are flagged by default until they build up reputation.

The launcher's full source code is available in [its own repository](https://github.com/solida1987/Multiworld-Launcher) for inspection, and the executable has been submitted to Microsoft for review. SmartScreen reputation also builds over time as more people download it. *(A code-signing certificate would remove the warning immediately, but carries a significant recurring cost.)*

How you respond to a warning from your own security software is your decision — please make it based on the source code and on your own judgement.

---

## How to Play

Everything is driven from the launcher — pick **Diablo II Archipelago**, then choose one of:

### Standalone (singleplayer)
1. Click **Standalone**.
2. Choose your options (goal, quest categories, skill pool, collection/custom goal, shuffles, etc.) or **load a previous seed** from the list on the right.
3. Click launch and create/select a character. Each seed keeps its own characters.

### Archipelago multiworld
1. Enter your Archipelago room's **server address**, **slot name** and **password** in the launcher.
2. Click **AP Play**.
3. The launcher connects and launches the game already hooked up to the multiworld — checks, items, goal and (optional) DeathLink all flow through the launcher.

> **Tip:** the launcher's **Locations** tracker works in both modes — in standalone it shows the full location universe (every category, all difficulties) just like an AP session.

---

## Features

- **All 210 class skills** from the 7 classes randomized into a quest-reward pool, plus an optional experimental spell pack.
- **Hundreds of checks** across 5 Acts × 3 difficulties: Story, Hunts, Clear Zones, Exploration, Waypoints, Level Milestones — plus optional Shrines, Urns, Barrels, Chests, Set Pickups, Gold Milestones, Cow Level, Mercenary, Hellforge & Runes, NPC Dialogue, Runewords and Cube Recipes.
- **Goals:** finish on Normal / Nightmare / Hell, a **Collection** goal, or build your own **Custom** win condition.
- **Skill Editor** (key 5) and **Skill Tree** (S) — assign and spend points on your unlocked skills.
- **Quest Book** (key 6) — scrollable log with filter tabs and act/difficulty selection.
- **Monster Shuffle**, **Boss Shuffle**, **Shop Shuffle**, **Entrance Shuffle** (all optional).
- **Zone-Locking mode** — zone keys gate area access for exploration-focused runs.
- **XP multiplier**, expanded inventory/stash/cube, and settings ready for
  the optional HD renderer (see above).
- **Delta updates** — only changed files are downloaded when the game updates.

---

## Controls

| Key | Action |
|-----|--------|
| 5 | Skill Editor |
| 6 | Quest Book |
| 7 | Toggle Quest Tracker HUD |
| 8 | Zone Map (Zone-Locking mode) |
| S | Open Skill Tree (spend skill points) |
| F1–F8 | Skill hotkeys (bind with right-click, cycle with the mouse wheel) |
| Ctrl+O | Graphics settings — only if you installed D2GL |
| ESC | Close any open panel |

All panel keys can be rebound in the launcher's keybinding settings.

---

## Built With

- [D2MOO](https://github.com/ThePhrozenKeep/D2MOO) — open-source Diablo II reimplementation
- [D2.Detours](https://github.com/Lectem/D2.Detours) — DLL patching framework
- [d2gl](https://github.com/bayaraa/d2gl) — the HD renderer this mod is tuned
  for, installed separately by the player
- [Archipelago](https://archipelago.gg/) — multiworld randomizer framework

## Credits

- **solida1987** — project lead, game systems, quest design, AP integration
- **Maegis** — evil minion bookkeeping and answer man
- **D2MOO Team** — open-source Diablo II reimplementation
- **Archipelago Community** — multiworld framework and support
- **Diablo II Modding Community** — research, tools and documentation

## License

This project is a modification for Diablo II: Lord of Destruction. A legal copy of the original game is required to play.

## Archipelago Discord Notice

I have been permanently banned from the official Archipelago Discord server.
Because of this, please do not post or share links to this project on the
official Archipelago Discord, as this project is not permitted there.

For clarity, the ban was not related to malware, viruses, malicious code, or
any security issue with this project.

The moderation issues were related to:

* Copyright/distribution concerns involving game files in earlier versions of
  my projects. Those files were removed, the affected repositories and
  releases were cleaned up, and the distribution process was changed
  accordingly.
* Violations of the Discord server's own content rules, including
  links/content involving games that were restricted or considered 18+ under
  their server rules.

These issues relate to the official Archipelago Discord's moderation and
content policies.

Development and support for this project will continue independently outside
of the official Archipelago Discord.

---

## AI Usage Disclosure

Everything in this project was made by AI.

The code is AI.
The documentation is AI.
The artwork is AI.
I am AI.
My mother and father are also AI.

At this point, just assume everything is AI unless proven otherwise.

