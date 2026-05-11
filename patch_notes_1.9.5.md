# Diablo II Archipelago — Beta 1.9.5

Bug fix + AP lifecycle hardening release. The top 5 architectural
gaps from the AP lifecycle audit are implemented and tested, and
the critical Archipelago Launcher YAML template generator crash
(blocked everyone with our apworld installed from generating
template options for ANY game) is fixed.

**Build artifacts (all stamped Beta 1.9.5):**
- `D2Archipelago.dll` — Mod DLL
- `D2Arch_Launcher.exe` — Bootstrap launcher
- `ap_bridge.exe` — Archipelago WebSocket bridge
- `diablo2_archipelago.apworld` — Multiworld plugin (world_version: 1.9.5)
- `game_manifest.json` — Release manifest (version: Beta-1.9.5)

The launcher (the C# `Diablo II Archipelago.exe`) stays at 1.5.3 —
it is versioned independently and did not change in this cycle.

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

**If you already have `diablo2_archipelago.apworld` in your Archipelago
Launcher's `custom_worlds/` folder (from 1.9.2 or earlier), you MUST
replace it.** The 1.9.4 game release shipped the fixed `.apworld` file
inside `game_package.zip` but did NOT auto-update the file in your
Archipelago Launcher install.

**To update:**
1. Open `C:\ProgramData\Archipelago\custom_worlds\` (or wherever your
   Archipelago Launcher lives — it's set via **Open Containing Folder**
   in the Launcher).
2. Delete `diablo2_archipelago.apworld`.
3. Copy the new `diablo2_archipelago.apworld` from this release's
   `Game/apworld/` folder.
4. Restart your Archipelago Launcher.

The new apworld reports `world_version: 1.9.5`, so a fresh launcher
load will see the correct version.

---

## Headline fix — Archipelago YAML generator no longer crashes

### Generate Template Options worked again for everyone

**Before (1.9.4):** If you had our `diablo2_archipelago.apworld`
installed in the Archipelago Launcher's `custom_worlds/` folder,
clicking **"Generate Template Options"** would crash the launcher
with an `AttributeError`. As a side effect, this didn't just block
template generation for our world — it blocked it for **every**
other game too, because the generator stops on the first crash.
Anyone with our apworld installed was stuck.

**After:** Our apworld now plays nicely with the Archipelago
Launcher. Verified:
- "Generate Template Options" produces YAMLs for all 95 games we
  tested.
- Our YAML template generates cleanly across all 10 goal modes
  (Full Normal / Nightmare / Hell, Gold Collection, and every
  Custom-goal sub-target combination).
- A 10-player multiworld with our world plus 8 other games
  (Hat in Time, Short Hike, ChecksFinder, Hollow Knight,
  Hylics 2, Stardew Valley, VVVVVV, Yacht Dice) generates a
  full output archive with spoiler.

The fix went in upstream in 1.9.3 but the 1.9.4 release ZIP still
shipped the broken extracted copy alongside it, so installs that
unzipped both versions kept seeing the bug. 1.9.5 ships a fully
consistent apworld build — only the fixed version is included.

---

## Community bug fixes — what was wrong, what we did

Everything below was reported by players in the community tracking
sheet. Each entry describes the symptom in plain language and what
the fix actually changes for you.

### Items disappearing when switching stash tabs (Maegis)

**Before:** If you were holding an item on your cursor and clicked
one of the multi-tab stash buttons, the item could vanish.

**After:** Clicking a stash tab while holding an item now politely
refuses the click and shows "Drop item before switching stash tabs".
Your item stays safely on your cursor. Drop it into a slot first
(or back into the open tab), then switch tabs as normal.

### Boss-loot rewards becoming unequippable (Maegis)

**Before:** AP-delivered boss-loot drops (Andariel / Duriel /
Mephisto / Diablo / Baal Loot rewards) sometimes had strength,
dexterity, or level requirements you couldn't meet, leaving the
item unusable.

**After:** All AP-delivered boss-loot drops now have their level,
strength, and dexterity requirements stripped to zero — you can
equip any of them regardless of your character level. Note: this
doesn't change *class* restrictions (a Necromancer-only wand still
can't be used on a Sorceress).

### F1 Collection page didn't count AP-delivered items (Maegis)

**Before:** When AP delivered a set piece, unique, rune, or special
item directly into your inventory, the F1 Collection tracker didn't
register it. You had to drop it on the ground and pick it back up
to make it count.

**After:** Items delivered by AP now count toward the Collection
goal the moment they arrive. The strict anti-cheat rule (which
requires items to be observed fresh-from-ground) is bypassed for
trusted AP deliveries.

### Checks sometimes not reaching the AP server (Maegis)

**Before:** A completed quest or other check could occasionally
fail to sync to the AP server — usually after an alt-F4 close or
during a character switch.

**After:** The check file is now written atomically (no chance of
the bridge reading a half-written file), and the full check set is
republished to the bridge automatically every time you load a
character. Any check that was completed but lost in transit will
be re-sent on next load.

### Barbarian skill softlocks (Maegis)

**Before:** Jumping into a non-Barbarian's skill pool, then casting
Jump Attack or Whirlwind, could lock the character in place — no
movement until the animation timed out (sometimes requiring an
alt-tab to recover).

**After:** Skills that depend on per-frame animation timing — Leap
Attack, Whirlwind, Double Swing, Double Throw, Dragon Claw, Dragon
Talon, Blade Fury, the three Assassin charge-ups, Amazon thrown
javelins, Druid Rabies/Hunger, and Paladin Smite — are now reserved
for their native class only. Your random skill pool will never
include one of these unless you're playing the class it belongs to.
Native classes still get all of their skills working exactly as
before (Barbarian still gets Whirlwind, Assassin still gets Dragon
Talon, etc.).

### Sacrifice no longer "free" (Maegis)

**Before:** The Paladin Sacrifice skill wasn't draining the
player's life on use — making it feel like a free Smite.

**After:** Sacrifice's self-damage was accidentally set to 3% in
the 1.9.0 data refresh (vanilla Diablo 2 1.10 uses 8%). Restored to
the original 8% — Sacrifice now properly costs life on every
successful hit, matching how the skill is supposed to play.

### Shared stash had random items in it on fresh installs (Maegis)

**Before:** New players opening their shared stash for the first
time found random test items in there. ("Clean up yo inventory
Solida!")

**After:** Three developer test files were accidentally being
packed into the game install. They've been removed from the build
pipeline. Fresh installs start with a clean, empty shared stash.

**Existing players:** If you already see these test items, delete
the files `Game/Archipelago/shared_stash.dat`,
`shared_stash_ser.dat`, and `shared_stash_stk.dat` to clear them.
Your real saved stash data lives in `Game/Save/` and is unaffected.

### Tower entry teleporting you to the wrong floor (Maegis)

**Before:** With Entrance Shuffle on, when the random shuffle sent
you to "Forgotten Tower", you'd appear on the Tower Level 2
stairs instead of the Level 1 entrance — making the tower harder
to navigate and skipping the first floor entirely.

**After:** Fixed a one-line data error in the entrance-shuffle
table. You now land properly at the Tower Level 1 door, just like
walking in from Black Marsh.

### Mercenary equipment not counting toward Collection (Maegis)

**Before:** Set pieces, uniques, and runewords equipped on your
mercenary (Insight polearm, Tomb Reaver, Andariel's Visage, etc.)
weren't registered by the F1 Collection tracker. You had to take
the items off the merc and pick them up yourself.

**After:** The Collection scan now also walks your mercenary's
three equipped slots (helm, armor, weapon). Anything wearable on
the merc counts immediately.

### Bridge log file filled up with spam (Koivuklapi)

**Before:** The Archipelago bridge log (`ap_bridge_log.txt`) grew
to hundreds of kilobytes per session with repeated "DEDUP skipping
already-processed" and "UNLOCK written" lines.

**After:** Three of the noisiest lines have been demoted to debug
level. The useful information (which items you got, who sent them,
which locations were checked) is still logged at INFO. Your log
file should now stay well under 100 KB per session even after long
play.

### YAML template generator crashed when our apworld was installed
(Lots of folks)

**Before (1.9.4):** If you had `diablo2_archipelago.apworld` in
your Archipelago Launcher's `custom_worlds/` folder, clicking
"Generate Template Options" crashed the launcher and blocked YAML
generation for **every** game, not just ours.

**After (1.9.5):** Fixed the dynamic option class factory. YAML
generation works for our world and every other AP world you have
installed. Verified across 95 games and a 10-player multiworld
generate test.

### Stale bridge after game close caused duplicate items (Maegis)

**Before:** Closing the game with Alt-F4 (or task-killing it) left
the `ap_bridge.exe` helper process running in the background.
When you launched the game again, a SECOND bridge would spawn —
and now both bridges were fighting over the same status / unlocks /
dedup files. Items you'd already received could re-fire, the dedup
file could be appended twice, and the on-screen status could read
data from the wrong bridge.

**After:** Before spawning a new bridge, we now sweep the process
list for any leftover `ap_bridge.exe` from a previous session and
terminate it cleanly first. Also: when the bridge is stopped
normally, we wait up to 1 second for it to actually exit before
releasing the handle — so a fast restart never races against the
dying bridge's still-open file handles. No more ghost bridges.

### AP connection errors were invisible (Leo Melo)

**Before:** If your password was wrong, your slot name was a typo,
or the server was offline, the title-screen Connect button stayed
on "Connecting..." forever with no indication of what was wrong.
The bridge log file had the actual error reason, but you had to go
hunting in `Game/Archipelago/ap_bridge_log.txt` to find it.

**After:** The bridge already writes error reasons (`InvalidSlot`,
`InvalidPassword`, TLS handshake failures, etc.) to its status
file. Now the DLL reads those, shows the reason in red text on the
F1 Archipelago page beneath the status line, and fires a one-shot
ShowNotify on screen the moment the error happens ("AP Refused:
InvalidPassword" / "AP Error: Connection refused by server" /
etc.). No more guessing.

### Mercenary now scanned for Collection goal (Maegis)

**Before:** Items equipped on your mercenary (Insight runeword
polearm, Tomb Reaver, Andariel's Visage, set pieces on Act 5 merc,
etc.) didn't count toward the F1 Collection goal. You had to
unequip them from the merc and pick them up yourself to register.

**After:** The Collection scan now also walks your mercenary's
three equipped slots (helm / armor / weapon) on every check. Merc
loadout counts toward the goal immediately.

### Forgotten Tower entry — Entrance Shuffle destination fix (Maegis)

**Before:** When Entrance Shuffle routed you to "Forgotten Tower"
(via, say, walking into The Pit), you landed on the Tower Level 2
staircase instead of the Level 1 entrance. You'd then have to walk
DOWN to find Level 1 — which was both confusing and skipped the
first floor's enemies/loot.

**After:** Fixed a one-line table error in the shuffle data. You
now land at the Tower Level 1 door, exactly like walking in from
Black Marsh normally. Climbing up through levels 1→6 works as
intended again.

### Safer behaviour if quest state is briefly unreadable

**Before:** If the game's quest-state pointer was momentarily NULL
during a respawn (a very brief window), the entrance-shuffle code
defaulted to "assume Hell access" — and could let the player wander
into act 4/5 zones they hadn't unlocked, potentially writing
mismatched data to the save.

**After:** Changed the fallback to "assume Act 1 only" — strictly
safer. Worst case is a momentarily blank UI; previous behaviour
could corrupt quest state on a bad respawn.

---

## Known issues — still under investigation

These bugs from the community tracking sheet are not yet fixed and
will be addressed in a future release. If you hit any of them, a
screenshot or log file would help us pin them down faster:

- **Game crashes after reaching Act 3 in AP mode** (Teddie) — there
  are several possible causes and we couldn't reproduce it locally
  yet. If this hits you, please send us your `Game/Archipelago/
  d2arch.log` and a description of what happened just before the
  crash (which boss, which zone, etc.).
- **Skill page shows no damage numbers or synergy info** (Maegis) —
  three possible causes; a screenshot of the F1 skill tooltip would
  let us pin down which one.
- **Champion enemies (blue-text monsters) have no name** (Maegis) —
  please screenshot a champion when you find one so we can see
  whether the prefix word, the base name, or both are missing.
- **Some skills still show "an evil force" cross-class** — the
  string table for cross-class skill names needs a one-pass
  regeneration. Scheduled for a follow-up release.

The full priority list lives in the community AP D2 Tracking Sheet.

---

## Archipelago integration improvements

We did a full review of the AP connection system this cycle and
landed a lot of plumbing improvements. Here's what's better:

### Auto-recovery if the AP bridge ever stops responding

The bridge process now writes a "heartbeat" timestamp every 30
seconds. If the game stops seeing fresh heartbeats for two minutes,
it assumes the bridge is dead and restarts it automatically, then
reconnects to the AP server. You'll see a small on-screen message
("AP bridge died — restarting"). Your session continues without
needing to alt-tab out and restart the bridge yourself.

### On-screen "AP disconnected" / "AP reconnected" notifications

If your connection drops mid-game (network glitch, server restart,
etc.), you'll now see an on-screen notification right when it
happens. When the connection comes back up, you'll get a matching
"AP RECONNECTED — pending checks will sync" message so you know
your queued checks are on their way. Brief blips don't spam the
screen (we debounce by 30 seconds).

### Per-character AP spoiler + checklist files

Each AP character now gets two extra files saved alongside their
character file:
- **`d2arch_ap_spoiler_<char>.txt`** — full list of what items are
  at YOUR slot's locations, who they're going to, and which ones
  you've already received vs. still need to find.
- **`d2arch_ap_checklist_<char>.txt`** — append-only history of
  every item you've received, with timestamps and who sent it.
  Useful for verifying nothing got lost or for keeping a log of
  your run.

Both files update live as you play, and they correctly rebind to
the new character when you switch characters mid-session.

### Switching to a different AP server / slot no longer corrupts items

If you reuse a character with a different AP server or slot (e.g.,
starting a new multiworld), the game now detects the change and
automatically clears the old item-delivery history so the new
server's items can't collide with leftover markers from the previous
slot. You'll see "AP server/slot changed — clearing old item
history" when this happens.

### Force Reconnect button

A new "Force Reconnect" button shows up on the F1 Editor whenever
the bridge is trying to connect but stuck (e.g., server was briefly
down). Click it to skip the back-off wait (up to 60 seconds) and
retry immediately.

### Better error visibility

If the bridge fails to connect for a specific reason (wrong
password, typo'd slot name, server offline, etc.), you'll now see
the actual reason on the F1 Archipelago page (in red, beneath the
status line) and as an on-screen pop-up the moment it happens. No
more silent "Connecting..." forever with no idea why.

### Reduced log spam

The `ap_bridge_log.txt` file used to fill up with thousands of
repeated lines per session. We demoted three of the noisiest
debug-level messages so the log stays small and useful. Expect
roughly a 70% reduction in log file size.

### Things confirmed working by the audit

The audit also checked the basics and confirmed they're solid:
- Item delivery is properly deduplicated, so reconnects can't
  re-grant items you already received.
- Per-character settings are isolated — your other characters can't
  contaminate this one's saved AP options.
- Atomic file writes handle a mid-write crash safely (no half-
  written check files breaking the next launch).
- Checks complete in offline mode are queued and re-sent on
  reconnect.
- The AP server reconnect logic never gives up — 5 / 10 / 20 / 40 /
  60 second back-off, then retries forever.

The full audit report is committed to the repo under
`Research/AP_LIFECYCLE_AUDIT_2026-05-11.md` for the curious.
