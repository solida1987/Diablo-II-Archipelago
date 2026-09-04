# Installing Diablo II Archipelago

This mod does not run on its own. It is a **plugin** for the Multiworld
Launcher, and the launcher is a separate download.

That is deliberate, and worth thirty seconds of your time: the launcher ships
with no games in it at all, and every game — including this one — arrives as a
file you fetch and add yourself. You will always know what you put on your own
machine.

---

## What you need

| | |
|---|---|
| **Your own copy of Diablo II: Lord of Destruction** | The mod is built from your original game files. It never modifies your install; it copies what it needs into its own folder. |
| **Multiworld Launcher 3.3.0 or newer** | The host program. → [Download it here](https://github.com/solida1987/Multiworld-Launcher/releases/latest) |
| **This plugin** | The `.londonplugin` file from [this project's releases](https://github.com/solida1987/Diablo-II-Archipelago/releases/latest) |
| **Windows 10 or 11** | No separate runtime needed; the launcher bundles everything. |

---

## Step 1 — Get the launcher

Go to **[Multiworld Launcher](https://github.com/solida1987/Multiworld-Launcher)**
and read what it is before you download it. The short version: it installs and
updates game integrations, holds the Archipelago connection, and shows your
checks and items live while you play.

Download `launcher_package.zip` from its
[latest release](https://github.com/solida1987/Multiworld-Launcher/releases/latest),
extract it somewhere you have write access — Desktop or Documents is fine — and
run `Multiworld Launcher.exe`.

**The library will be empty.** That is correct. Nothing is wrong.

> Windows SmartScreen may warn you the first time. The launcher is unsigned;
> unrecognised programs are flagged by default. Its full source is public, and
> how you respond to a warning from your own security software is your call.

## Step 2 — Get this plugin

Download the `.londonplugin` file from
[this project's latest release](https://github.com/solida1987/Diablo-II-Archipelago/releases/latest).

There are two channels, and you want the first one unless you know otherwise:

- **`diablo2_archipelago-*.londonplugin`** — the stable channel.
- **`diablo2_archipelago_experimental-*.londonplugin`** — aggressive testing.
  Separate install, separate saves, separate everything. Things break here on
  purpose.

You can have both installed at once; they do not touch each other.

## Step 3 — Add it to the launcher

1. Click **Add plugin…** in the launcher.
2. Pick the `.londonplugin` file you downloaded.
3. **Read the dialog.** It tells you who published it, what it declares it will
   do, and the SHA-256 of the file you picked. The approve button is disabled
   for a few seconds so that reading is the default, not an obstacle.
4. Approve it.

Diablo II appears in the library on the left immediately.

> Your approval is bound to the **contents** of what got installed, not to the
> name. If those files change afterwards, the plugin stops loading until you
> look at it again. Approving "Diablo II" once does not hand a blank cheque to
> whatever later arrives under that name — including from me.

## Step 4 — Install the game

Click Diablo II in the library, then **Install**.

The launcher will ask where your own Classic Diablo II: Lord of Destruction is
installed — it tries to find it first, and only asks if it cannot. It copies
the original game files it needs into its own folder under `Games/`.

**Your original install is never modified.** Not patched, not moved, not
written to.

## Step 5 — The pieces Diablo II still needs

Diablo II 1.10f draws through DirectDraw, which modern Windows no longer
provides properly, so it needs a graphics wrapper before it can open a window
at all. That wrapper is somebody else's work and is not shipped here.

Press **Get missing components** on the game's page — or just press Play, and
you will be offered the same thing. You get one screen per component, naming
who wrote it, its licence, the exact files, and the project's own GitHub page:

| | |
|---|---|
| **D2GL** — bayaraa, GPL-3.0 | The graphics wrapper. **Required** — nothing starts without it. |
| **SGD2FreeRes** — Mir Drualga, AGPL-3.0 | Optional. Unlocks resolutions above 800x600. |
| **DSOAL** — Chris Robinson (kcat), LGPL-2.1 | Optional. Restores positional and environmental audio. |

Nothing is downloaded until you press yes to that particular component, and
each one is fetched from its own project's GitHub release — not from here. If
you already have any of them in your own Diablo II folder, they are copied
across instead of downloaded.

You can decline all of it and install them by hand; **I'll do it myself** shows
you the link and the steps.

## Step 6 — Play

- **Play** connects to an Archipelago multiworld — enter the server address and
  your slot name in the sidebar first.
- **Launch Standalone** plays a randomised run with no server and no multiworld.

`Create YAML` builds your Archipelago settings file without a text editor.
`Check seed` reads a generated seed's spoiler and tells you whether it can
actually be finished.

---

## Updating

The launcher updates itself on start. This plugin does not — download the newer
`.londonplugin` and add it again the same way. You will be shown the consent
dialog once more, because the file changed, which is the point.

The **game** updates separately from inside the launcher, as an optional
button, so a game update never blocks you from playing.

---

## If something goes wrong

**The plugin does not appear after adding it.** The launcher's log says why —
check the status bar and the log panel. The usual cause is that the file
changed after it was approved.

**Windows Defender removed something.** The mod injects into a running game,
which antivirus treats as suspicious. The launcher detects this specific case
and offers to add an exclusion for the game folder in one click.

**Anything else.** Use **Collect logs** on the game's page. It gathers
everything worth having into one zip you can attach to a report.

---

## The rules

This mod follows [Archipelago's content rules](https://archipelago.gg). No
copyrighted game files are distributed with it — that is why it is built from
your own copy.

Responsibility for this integration following those rules is mine, not the
launcher's. The launcher is a host program; it did not write this game and does
not vouch for it.
