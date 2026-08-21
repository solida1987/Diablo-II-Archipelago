# Third-party notices

Diablo II Archipelago is assembled from its own code plus a few independent
open-source projects. This file lists every one of them: what it does, which
files it provides, who wrote it, and under what licence. The full licence text
of each is in [`licenses/`](licenses/) so it can be read here without
downloading anything.

**No files belonging to Blizzard Entertainment are distributed with this
project** — no game data and no engine binaries. Those come from the player's
own Diablo II installation. See [Engine patches](#engine-patches) below for the
only changes made to them, and the README for the 1.10f requirement.

---

## Bundled components

### D2.Detours — MIT
Loads a mod DLL into Diablo II and redirects the game's own functions to it.
This is the mechanism the whole mod runs on.

- **Files:** `D2.Detours.dll`, `D2.DetoursLauncher.exe`
- **Copyright:** © 2017 Lectem
- **Source:** https://github.com/Lectem/D2.Detours
- **Licence:** [`licenses/D2.Detours-MIT.txt`](licenses/D2.Detours-MIT.txt)

### D2MOO — MIT
An open-source re-implementation of Diablo II's game logic. The mod ships
D2MOO builds of two libraries, which the game loads in place of its own, plus
its debugger.

- **Files:** `patch/D2Game.dll`, `patch/Fog.dll`, `D2Debugger.dll`
- **Copyright:** © 2020–2025 The Phrozen Keep community
- **Source:** https://github.com/ThePhrozenKeep/D2MOO
- **Licence:** [`licenses/D2MOO-MIT.txt`](licenses/D2MOO-MIT.txt)

---

## Not distributed — installed by the player

Nothing in this list travels with the mod. The first is **required** — the game
cannot start without it — and the other three are optional.

| Component | Licence | What it does |
|---|---|---|
| **[cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw)** | MIT | **Required.** DirectDraw wrapper — Diablo II 1.10's own DirectDraw does not initialise on modern Windows and the game stops at "Error 22" without it |
| [d2gl](https://github.com/bayaraa/d2gl) | GPL-3.0 | Glide-to-OpenGL renderer — the HD graphics option |
| [SGD2FreeRes](https://github.com/mir-diablo-ii-tools/SlashGaming-Diablo-II-Free-Resolution) | AGPL-3.0-or-later | Unlocks resolutions the original game does not offer |
| [DSOAL](https://github.com/kcat/dsoal) | LGPL-2.1 | Restores the hardware-accelerated 3D audio the game was written for |

The three copyleft ones were bundled by mistake, and it is worth being plain
about why rather than quietly dropping them. cnc-ddraw is a different case: it
is MIT and could be shipped, but this project distributes nobody else's binaries
now, so the player installs that one too. The launcher looks for it, copies it
out of the player's own Diablo II folder if one is already there, and refuses to
start with instructions if it is not.

Each of these is an independent program that Diablo II loads in its own right.
None of them is linked into this project's code: the mod's own library,
`D2Archipelago.dll`, imports only from the Windows system libraries
`KERNEL32`, `USER32`, `ADVAPI32` and `XINPUT9_1_0`. That argument — that they
are separate works merely shipped side by side — is a real one, and it is the
argument this project was relying on.

But it is an argument, not a fact, and the GPL family defines *propagation*
broadly enough that reasonable people read the boundary differently. Putting a
GPL-3.0 renderer, an AGPL-3.0 resolution patch and an LGPL-2.1 audio driver
into the same download as this project asks a licence question that nobody
here can answer with certainty, and the cost of being wrong falls on the
people who wrote those components.

So the question is no longer asked. They are add-ons, not part of the mod, and
the mod runs without all three. The README explains what each does, links to
its author's own releases, and describes where the files go — the same way the
player already supplies their own copy of Diablo II. Downloading a program and
running it on your own machine carries none of these obligations;
redistributing it is what does.

The settings files `d2gl.ini`, `d2gl.json` and `SGD2FreeResolution.json` are
still included. Those are this project's own tuned configuration for those
tools, not the tools themselves, and they simply sit unused until the matching
component is installed.

---

## How the bundled components relate to this project's own code

The components listed under **Bundled components** are all under permissive
licences (MIT and BSD 2-clause) that impose no condition beyond
keeping the copyright notice and licence text — which this file and the
`licenses/` directory do.

Anyone redistributing this package must keep this file, the `licenses/` folder,
and the components' own copyright notices intact. The source of every component
is linked above.

---

## Engine patches

The mod changes 32 bytes across three of Diablo II's own libraries, applied by
the launcher to the player's own copies. The files themselves are never
distributed; what is distributed is the description of the changes, which lives
in the launcher's source in `Plugins/DiabloII/D2EnginePatch.cs`:

| File | Changes | Purpose |
|---|---|---|
| `Storm.dll` | 1 byte | Diablo II otherwise refuses to load a modded archive and stops with *"The file data is corrupt"* |
| `D2Glide.dll` | 2 bytes | Display handling the mod depends on |
| `D2Launch.dll` | 29 bytes | Main-menu adjustments |

Each edit records the exact bytes it expects to replace, and each file is
identified by the SHA-256 of both its unpatched and patched forms, so the
change is fully reproducible and verifiable from the source.
