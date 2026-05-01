# Diablo II Archipelago — Beta 1.9.1

> Dev-only stub. The version stays at 1.9.1 throughout the dev cycle —
> all fixes accumulate here until the upload day, then the file becomes
> the public release notes. See `feedback_version_policy.md`.

> Style rule: write entries player-facing — what was broken, what works
> now, in plain English. No function offsets, no savedLen quibbles, no
> sidecar migration mechanics. (Those go in commit messages.)

---

## Install / update

1. Download **launcher_package.zip** from this release
2. Extract anywhere, run **Diablo II Archipelago.exe**
3. Click **More info → Run anyway** on the SmartScreen warning
4. The launcher downloads the game for you

You need an existing Diablo II + Lord of Destruction install (Classic,
not Resurrected). Windows 10/11 with the .NET 8 Runtime.

---

## New features

- **Holy Bolt buffed.** The Paladin's Holy Bolt now damages demons in
  addition to undead, AND pierces through enemies (no longer dies on
  the first hit). The skill still passes through allies and heals
  them. Affects player casts as well as the Holy Bolt proc on the
  Principle runeword and the Holy Bolt aura on the Rift runeword.

- **Hustle weapon — Fanaticism aura restored.** The "Level 1
  Fanaticism Aura When Equipped" property that was dropped from
  Hustle (weapon) in 1.9.0 (because it displayed as "an evil force"
  due to the same wrong-skill-id bug that broke 8 other runewords —
  see Bug fixes) is back, now pointing to the correct aura skill.

- **Dev menu — new Loot tab.** Press Ctrl+V → click the new **Loot**
  tab (rightmost) for individual-item spawn buttons grouped under five
  sub-tabs:
  - **Sets** — scrollable list of all 127 set pieces, click one to
    spawn that specific piece
  - **Uniques** — scrollable list of every enabled unique (~357),
    click one to spawn it
  - **Runes** — 33 individual rune buttons (r01 El through r33 Zod)
  - **Gems** — 35 individual gem buttons (7 colors × 5 grades)
  - **Misc** — every Pandemonium key/organ/essence, Hellfire Torch,
    Token of Absolution, all 3 charm sizes, tomes, scrolls, the cube,
    every healing/mana/rejuv/specialty potion
  
  Sets and Uniques pages support mouse-wheel scrolling and a draggable
  scrollbar on the right. Set + unique buttons route through the same
  delivery code path the Archipelago server uses for received items,
  so the menu doubles as an end-to-end test bench for AP loot.

---

## Bug fixes

- **8 endgame runewords had their aura silently broken.** The D2R
  2.4 / 2.6 backports were authored using numeric skill IDs that
  pointed to the wrong skill — e.g. Infinity's "Conviction aura
  level 12" was actually pointing to Shiver Armor (id 50 instead of
  the correct Conviction id 123). D2 sees a non-aura skill in an
  aura property and falls back to the placeholder text "an evil
  force." Every affected runeword's aura now points to the right
  paladin/druid aura skill:
  - **Oath** — was Iron Golem → **Heart of Wolverine aura** lvl 16
  - **Dragon** — was Sacrifice → **Holy Fire aura** lvl 14
  - **Dream** — was Fist of the Heavens → **Holy Shock aura** lvl 15
  - **Ice** — was Teleport → **Holy Freeze aura** lvl 18
  - **Infinity** — was Shiver Armor → **Conviction aura** lvl 12
  - **Last Wish** — was Poison Nova → **Might aura** lvl 17
  - **Phoenix** — was Lightning Mastery → **Redemption aura** lvl 10-14
  - **Rift** — was Fire Arrow → **Holy Bolt** lvl 1

- **AP-delivered random uniques now spawn the correct item.** A parser
  bug in the unique catalog loader was silently using the wrong column
  on every row that had an empty cell to its left (most of
  UniqueItems.txt — `ladder` is empty for nearly every row). The base
  code field ended up holding the numeric `cost mult` value instead
  of the 3-letter item code, so every "Drop: random unique" reward
  failed to spawn (logged as `code=0x20202035 FAILED (inv full?)`).
  Fixed the loader to preserve empty TSV fields. Affects:
  - AP server "Drop: Random Unique" deliveries (item id 45521)
  - Bonus check unique rewards (shrines, urns, barrels, chests)
  - Quest reward unique drops

---

## Known issues

- **F1 Quest counter** may show 9 quests fewer than the expected total
  in some configurations (carried over from 1.9.0 — needs root-cause
  investigation; might be catalog entries with quest IDs > 999 or a
  subtype filter edge case).
- **F1 Collection counter** (`C N/205`) on the F3 tracker counts only
  items physically picked up, not AP-delivered set/unique drops in
  inventory. Design choice for now; could add an inventory-pickup
  hook.

---

## Includes everything from 1.9.0 and earlier
