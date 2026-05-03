# Diablo II Archipelago — Beta 1.9.4

Bug fix release — addresses ALL outstanding community-reported issues
from the 1.9.2 / 1.9.3 release window. No new gameplay features. Rift
Dungeon system remains deferred — research and prototype assets stay
on disk for a future major version.

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

- **First-of-each AP filler item duplicated.** Maegis: "first magic
  charm gave 2, first unique gave 2". The bridge can re-send the same
  filler ID during connect/reconnect; both receives queued separate
  drops. Fix: defensive dedup on AP filler IDs 45519 (charm) / 45520
  (set) / 45521 (unique) — duplicate receives within a 5-second
  window are suppressed. Genuine multiple drops via separate AP
  locations still work.

- **AP-delivered items not equippable.** Maegis: received Spirit
  Shroud from a check but couldn't equip it. Prior versions only
  stripped the level requirement (stat 92), leaving STR (stat 91)
  and DEX (stat 93) requirements blocking equip on low-level chars.
  Fix: strip ALL three requirement stats on every AP / quest-reward
  / cheat-menu item delivery.

- **F1 Collection tracker didn't count AP-delivered set/rune/special
  items.** Maegis: "checks gave me arcana + hwanin pieces but tracker
  shows 0/X". Vanilla code only registered items the player picked
  up off the ground; bDroppable=0 deliveries land directly in
  inventory and bypass the pickup hook. Fix: Coll_ProcessItem is now
  called on every successful AP-delivered item so the GUID table and
  per-set/rune/special counters update correctly.

- **Den of Evil "awesome loot" fired twice.** Maegis. The dedup flag
  on OnQuestComplete was set AFTER the type-toggle check, leaving a
  window where parallel detector paths (CheckQuestFlags +
  ScanMonsters + boss-kill walk all fire in the same tick for some
  quests) could both pass the dedup check. Fix: set the
  g_questCompleted flag IMMEDIATELY at function entry, before any
  reward processing.

- **Skill Hunting OFF still sent skills as filler rewards.** Maegis:
  "game still trying to send me skills despite skill hunt being off".
  The reward roller's weight table gave REWARD_SKILL weight=10
  unconditionally. Fix: gate the weight on g_skillHuntingOn —
  weight=10 when ON, weight=0 when OFF.

- **Auto-completed checks on first character entry.** Maegis: got
  level milestone rewards 5..30 instantly + a Warriv NPC check fired
  invisibly on first game entry. Two separate fixes:
  - Level milestones now silent-backfill on first tick after load
    (already-met milestones marked completed without firing rewards;
    new level-ups still fire normally during play).
  - NPC dialogue poll is gated for the first 5 seconds after each
    character load AND the per-NPC "near tick" counter is reset, so
    spawning near Warriv at game start no longer fires his slot.

- **Stash quick-shift+rclick captured merchant belt-fill clicks.**
  koivuklapi: shift+rclick on potion in merchant gave items from
  stash instead of filling belt. Fix: StashQuickMoveToStash now
  checks D2's gpUIState for NPC shop / multiplayer trade windows
  open and defers to D2's normal handler when either is active.

- **Hidden stash / loose rocks not counted as chests.** Maegis.
  Vanilla operateFn IDs 36 (LooseRock) and 60 (TristramCoffin loot
  variant) were missing from the chest detection list. Hidden
  stashes already use operateFn 1 (Casket) which IS recognised, so
  those should have been counting — confirm in next test session.

- **Tome shift-buy gold sink.** Maegis: "I bought 300+ scrolls with
  28k gold". Misc.txt maxstack for Town Portal Book and Identify
  Book reduced from 999 → 100. Buying out a stack now costs roughly
  9,500 gold instead of 95,000.

- **"The Minataur" typo.** Maegis: vanilla unique item name had a
  Blizzard typo. Fixed to "The Minotaur" in our UniqueItems.txt.

- **Ctrl+V in main menu blocked input** (carried from 1.9.3).

- **YAML template generation crash on AP 0.6.7+** (carried from 1.9.3).

- **Level milestone reward dump on character load** (carried from 1.9.3).

## Known issues — not fixed in 1.9.4 (deferred)

- **Throwable potion stash stack count display + withdrawal off.**
  koivuklapi: stash showed x1 for a 21-stack and withdrawing gave 18.
  The STK system stack handling needs deeper debug work to ensure
  stack-count display, deposit-stacking, and withdrawal counts stay
  in sync. Workaround: don't shift+rclick-deposit throwable potions
  into the STK tab — drop them into a regular stash tab instead.

- **AP bridge log spam — repeated "DEDUP: skipping already-processed".**
  koivuklapi: bridge log floods with one line per re-sent item on
  reconnect. The throttle lives in the compiled `ap_bridge.exe`; a
  rebuild from `old/Tools/Archipelago/src/ap_bridge.py` is needed to
  ship the fix. Doesn't affect gameplay.

---

## Known issues carried forward

- Skill description text shows the `r-tier` placeholder for some
  unique-class skills — needs MPQ string-extraction work which is
  scheduled for a later release.
- F1 Quest counter on some characters shows 9 fewer than expected;
  the discrepancy is cosmetic only.
