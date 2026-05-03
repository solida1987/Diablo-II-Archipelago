# Diablo II Archipelago — Beta 1.9.3

Bug fix release on top of 1.9.2. Same code base as 1.9.2 with
community-reported issues addressed. No new gameplay features in
this cycle.

The Endless Rift Dungeon system experimented with during 1.9.3 dev
has been deferred to 1.9.4 — it requires deeper data-table work
than fits a same-day patch and the underlying mechanics need
proper isolation from vanilla level state. Research and prototype
graphics are kept on disk for the 1.9.4 cycle (see
`Research/RIFT_REAL_DESIGN_v2.md` + `Tools/rift_graphics_png_prototype/`).

---

## Install / update

1. Download **launcher_package.zip** from this release
2. Extract anywhere, run **Diablo II Archipelago.exe**
3. Click **More info → Run anyway** on the SmartScreen warning
4. The launcher downloads the game for you

You need an existing Diablo II + Lord of Destruction install
(Classic, not Resurrected). Windows 10/11 with the .NET 8 Runtime.

---

## Bug fixes

- **Level milestone rewards fired all at once on character load.** A
  character entering a fresh AP session at e.g. level 30 with no per-
  character state file used to receive every level milestone reward
  from level 5 through 30 on the first tick (Maegis bug report:
  "it gave me all rewards from level 5 to 30 without me doing
  anything"). Root cause: `CheckLevelMilestones` ran every tick and
  fired `OnQuestComplete` for every milestone where `level >=
  quest->param` was true, with no distinction between "earned this
  tick" and "already earned in past sessions". Fix: on the first
  CheckLevelMilestones tick after each character load, silently
  backfill milestones <= current level into the completion flags
  without firing rewards. Subsequent ticks then fire only when the
  player crosses a NEW milestone threshold during play.

- **Ctrl+V in main menu blocked input.** Pressing Ctrl+V before
  entering a game opened an invisible cheat menu that captured
  mouse input — the player could not click anything until they
  re-pressed Ctrl+V (Thedragon005 bug report). Fix: gate the
  Ctrl+V hotkey on Player() != NULL so it's a no-op outside an
  active game session.

- **YAML template generation crashed** with `AttributeError: 'tuple'
  object has no attribute 'visibility'` on AP 0.6.7+. Players using
  the AP launcher's "Generate Template Options" button could not
  produce template YAMLs as long as `diablo2_archipelago.apworld`
  was in their custom_worlds folder. Root cause: the dynamic
  Toggle-class factory `_make_collect_toggle` returned a
  `(name, cls)` tuple, so the 75 collection-toggle lists
  (`_COLL_SET_CLASSES`, `_COLL_RUNE_CLASSES`,
  `_COLL_SPECIAL_CLASSES`) were lists of tuples — but those
  lists are also passed as the `options=` argument to
  `OptionGroup`. AP 0.6.7's `get_option_groups` iterates each
  group's options and reads `.visibility` on every entry.
  Tuples don't have that attribute, so the entire generator
  exploded. Fix: factory now returns the class directly (still
  registers it on the module by name); the three downstream
  `_FIELDS +=` zips were updated to match.

---

## Known issues carried from 1.9.2

- The skill description text still shows the `r-tier` placeholder
  for some unique-class skills — needs MPQ string-extraction work
  which is scheduled for a later release.

- The F1 Quest counter on some characters shows 9 fewer than
  expected; the discrepancy is cosmetic only (the underlying quest
  state is correct).

- F1 Collection counter (`C N/205`) does not count items received
  from the AP server — only items physically picked up. This is
  intentional for now; an inventory-pickup hook may be added later.

- The NPC speech text in the chat box (the floating "Welcome to the
  Rogue Encampment..." lines) is missing on some installs. This is
  pre-existing in 1.9.1/1.9.2 and unrelated to any release work;
  likely environmental (TBL/MPQ data files). The new Cat 4 NPC
  checks still fire correctly via the room-scan detection regardless
  of whether the speech text renders.
