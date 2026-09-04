# Getting Diablo II to 1.10

This mod runs on **Diablo II: Lord of Destruction 1.10**. Modern installs
update themselves to 1.14, so you will probably need to take a different route
than just installing and playing.

You need your own copy of the game. Nothing on this page replaces owning it.

---

## Which situation are you in?

| You have | Go to |
|---|---|
| The original CDs | **Route A** — install, then patch. This is the clean one |
| A 1.14 install and the CDs | **Route B** — remove it, then Route A |
| A 1.14 install and no discs | **Read the last section.** There is no route we can point you at |

---

## Route A — from disc to 1.10

### Step 1: install from your CDs

Install Diablo II and then Lord of Destruction from your original discs, using
your own CD key.

A disc install leaves you on **1.09**. It does not contact Blizzard's update
servers, so nothing pulls you forward.

Check the version in the bottom-right corner of the main menu before you carry
on. It should read 1.09 or 1.09b/1.09d.

### Step 2: run Blizzard's 1.10 patch

The patch comes straight from Blizzard's own FTP server:

**http://ftp.blizzard.com/pub/diablo2exp/patches/PC/LODPatch_110.exe**

That is a first-party address — Blizzard's file, served by Blizzard.

Run it, point it at your Diablo II folder, and let it finish. The main menu
should now read **1.10**.

### Step 3: point the launcher at it

Open the Multiworld Launcher, select Diablo II, and set your game folder. It
checks the version and tells you if something is off.

---

## Route B — you are already on 1.14

**Patches only move forward, and not because Blizzard forbids it.**

The patch does not contain files. It contains *differences* against the 1.09
files. Every entry inside it shares the same container header — `18 00 04 00` —
instead of being an executable, and none of the sizes match the finished
article:

| | inside the patch | the real 1.10f file |
|---|---|---|
| `Game.exe` | 824,448 | 90,112 |
| `D2Game.dll` | 853,293 | 1,159,231 |
| `D2Common.dll` | 404,202 | 725,057 |

That is also why the installer carries a condition called `FileVersionLessThan`.
A difference can only be applied to the exact version it was measured against.

So on 1.14 there is nothing for it to work with: the 1.09 originals are gone,
and from 1.14 the engine DLLs do not exist at all — Blizzard merged them into
the main executable. No tool can bridge that. Anything that claims to is
carrying Blizzard's game files with it.

So the route is:

1. Uninstall Diablo II completely
2. Delete anything left in the install folder
3. Follow **Route A** from your discs

Your save files live in `%USERPROFILE%\Saved Games\Diablo II\`, not in the
install folder, so a reinstall does not touch them. Back them up anyway.

---

## Why this is allowed

Blizzard's own legal FAQ says so:

> We allow non-commercial mirroring of our patches and demos, so long as you
> do not alter the patches or demos in any way, and all files included with
> the original patch or demo are present and intact.

— [Blizzard Legal FAQ](https://www.blizzard.com/en-us/legal/c1ae32ac-7ff9-4ac3-a03b-fc04b8697010/blizzard-legal-faq)

Blizzard also reserves the right to withdraw that permission at any time. If
they do, this page comes down.

Note what the permission covers: **patches**. It does not cover the game
itself, which is why every route on this page starts with your own discs.

### Check the file before you run it

This is the file Blizzard serves, verified 17 August 2026:

| | |
|---|---|
| Size | 5,122,687 bytes |
| SHA-256 | `e32fd0298f24ac335563a00edf8af4fdfe95013f9eb640e4b05515c17faeb805` |
| MD5 | `301207cafa6d422fa92a2eabe59e29c2` |

Check yours:

```powershell
Get-FileHash "LODPatch_110.exe" -Algorithm SHA256
```

If it doesn't match, don't run it.

That matters most if you end up on a mirror. Blizzard's FTP has gone quiet
before, and their FAQ permits non-commercial mirrors — but a mirror is only
worth using if the file is byte-for-byte the original. The hash is how you
know.

---

## Modding documentation

[The Phrozen Keep](https://d2mods.info/home.php) is where the Diablo II
modding community keeps its tools, its knowledge base and its forums, and it
is the source of most of what this project is built on. Their
[copyright policy](https://d2mods.info/forum/copyrights) is worth reading if
you want to understand what is and isn't fair game when modding this game.

It is a documentation and tools site — it does not host Blizzard's patches,
so it is not where you get the file in Step 2.

---

## If you have no discs

If your only copy is a modern digital one, we do not have a route for you, and
this is the one place on this page where that is a hard answer rather than a
cautious one. See Route B: a patch is made of differences against files a 1.14
install does not have, so no amount of tooling produces bytes that are not
there.

Tools exist that run several game versions side by side, and the community
uses them. They work by carrying Blizzard's own game files for each version —
which is exactly what a patch cannot give you, and Blizzard's permission
covers patches, not the game. That is why we do not link them, and why nothing
that does work will come from us.

What that leaves:

- Second-hand discs are common and cheap
- If you find another legal route, we would genuinely like to hear about it —
  open an issue

We would rather tell you we have no answer than point you somewhere we
haven't checked.

---

## Why 1.10 and not the current version

From 1.13 onwards Blizzard merged large parts of the game into fewer DLLs.
That makes it very hard to hook into the engine cleanly, which is exactly what
a randomizer of this kind has to do.

Practically the whole Diablo II modding community works below 1.13 for the
same reason. If a way is found to build this on a current version, it will
move — but that is not where the game is today.
