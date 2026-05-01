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

(none yet)

---

## Bug fixes

(none yet)

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
