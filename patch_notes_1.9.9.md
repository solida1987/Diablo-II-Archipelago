# Diablo II Archipelago — Beta 1.9.9

*Dev cycle in progress. Patch notes accumulate here as bug fixes land.*

---

## Install / update

(Filled in at release time.)

---

## What 1.9.9 fixes

### Custom Goal now generates the right number of locations

When you picked **Goal = Custom** with a narrow target (for example
"kill Andariel on Normal only"), the apworld used to generate the full
~700-location pool spanning all 5 acts and all 3 difficulties. In
single-player that was just wasteful — you'd never visit those NM/Hell
zones. In **multiworld** it was actively broken: other players'
progression items could get placed at your out-of-scope locations, and
when you completed your goal, those items stayed stuck until the host
ran `!release` manually.

**1.9.9 fix:** the apworld now computes the seed scope from the union
of the Custom Goal toggles you actually selected, and only generates
locations for difficulties and acts your character will reach.

Concrete impact (single-slot generation, no other options changed):

| Custom Goal selection                       | Pre-1.9.9 | 1.9.9 |
|---|---|---|
| `kill_andariel_normal` only                  | ~700      |  70   |
| `kill_baal_normal` only                      | ~700      | 223   |
| `subsystem_story_normal` only                | ~700      | 223   |
| `kill_diablo_nightmare` only                 | ~700      | 446   |
| `kill_andariel_normal + kill_baal_hell`      | ~700      | 668   |
| `kill_baal_hell` only                        | ~700      | 668   |
| Empty Custom Goal (no toggles)               | ~700      |  70   |

The 668-location case is the intended "full" scope — when your toggle
selection actually requires Hell access, the full pool is correct.
Multi-difficulty goals automatically force the full 5-act pool on
lower difficulties because D2 hard-codes act/diff transitions on
beating Baal (so you physically traverse the full game on Normal +
Nightmare before reaching anything on Hell).

**Multiworld safety:** with the scope shrunk to what you'll actually
visit, other players' items can no longer get placed at locations you
have no reason to visit. `!release` is no longer a mandatory step
after winning a narrow Custom Goal.

### Goal = Custom can now actually complete (was broken since 1.9.2)

A separate, more critical bug: the DLL's settings parser at
`d2arch_ap.c:912` was mapping `goal=4` (Custom) to `g_apGoal = 4 % 3 = 1`
(Full Nightmare). This meant `CustomGoal_IsComplete()` — the function
that ANDs your selected sub-targets together and triggers the AP win
signal — could **never fire** in AP mode. Players using Custom Goal
since 1.9.2 (when it shipped) were silently being given a Full
Nightmare seed regardless of what custom targets they picked. The
goal showed as "Custom" in the F1 menu but completion ran on Baal NM.

The 1.9.9 fix adds explicit `goal=4` handling to the parser:
```c
if (ival == 3)       g_apGoal = 3;          /* Collection mode */
else if (ival == 4)  g_apGoal = 4;          /* Custom Goal mode (NEW) */
else                 g_apGoal = (ival <= 2) ? ival : (ival % 3);
```

Combined with the scope fix above and the Pandemonium event wiring
below, Custom Goal is now fully functional end-to-end for the first
time since it was added.

### Pandemonium uber kills now register for Custom Goal

The DLL functions that record uber kills toward Custom Goal targets
existed since 1.9.2 (`CustomGoal_OnUberKilled`,
`CustomGoal_OnHellfireTorchComplete`) but had no callers — they were
dead code. So if you picked `kill_uber_mephisto`, `kill_uber_diablo`,
`kill_uber_baal`, or `hellfire_torch_complete` as Custom Goal targets,
they could never fire even if you killed those bosses in-game.

The 1.9.9 fix wires these into `Ubers_OnUnitDeathScan` in
`d2arch_ubers.c`:
- Each finale uber kill fires its corresponding Custom Goal bit
- The full Pandemonium completion (all 3 ubers + Torch drop) fires
  the `hellfire_torch_complete` bit

### Planned for this cycle (TODO)

(Move items from "Planned" to a real "## Bug fixes" section as they land.)

---

## Known issues — still under investigation

These bugs need user-side info (screenshots/logs) before we can fix:
- **Game crash after reaching Act 3 in AP mode** (Teddie) — multiple
  suspected causes; awaiting `d2arch.log` from a crash event
- **Skill page no info display** (Maegis) — three plausible causes,
  awaiting F1 tooltip screenshot
- **Champion enemies no names** (Maegis) — three plausible causes,
  awaiting screenshot showing whether prefix or basename is missing

---

*Build artifacts will be listed at release time. Cycle started 2026-05-13
on top of Beta 1.9.8 baseline.*
