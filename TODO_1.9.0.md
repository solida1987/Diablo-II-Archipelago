# Beta 1.9.0 — TODO list

**Status (2026-04-30):** Beta 1.8.5 was released to GitHub on 2026-04-27
(commit `6cce091`). Local source is on **1.9.0 baseline** (DLL 501,248
bytes deployed, version stamp + comments rebranded from in-flight 1.8.6).
The dev cycle that started as "1.8.6" outgrew its scope and is being
shipped as a minor-version bump (1.9.0) instead.

**Public release window:** mid-next-week (target ~2026-05-07/08).
Nothing locks until then — keep iterating.

**Backups taken so far:**
`Backups/2026-04-28_clean_start_1.8.6/` (5GB clean baseline at start of
cycle), plus dozens of incremental backups through 2026-04-29 covering
every milestone.

---

## ✅ DONE in 1.9.0 (verified in code + data)

### System 1 — Dead-End Cave Entrance Shuffle (NEW GAME MODE)
- [x] Pool A (Act 1+2, 21 sets) and Pool B (Act 3+4+5 + Cow, 17 sets) authored
- [x] Sattolo permutation (no fixed points)
- [x] LEVEL_WarpUnit teleport hook
- [x] Hell-difficulty maze room counts patched everywhere
- [x] Act-town lock for death-respawn safety
- [x] Per-character isolation via `OnCharacterLoad` freeze
- [x] Per-character seed (AP slot_data or char-name hash)
- [x] Title-screen `Entrance` toggle button (column 2)
- [x] `d2arch.ini [settings] EntranceShuffle=` persistence
- [x] apworld `entrance_shuffle: Toggle` option propagating via slot_data

### Pandemonium Event
- [x] Mini Uber recipe (pk1+pk2+pk3 outside town → Lilith/Duriel/Izual)
- [x] Finale recipe (dhn+mbr+bey outside town → Mephisto/Diablo/Baal)
- [x] Cube hook via dispatch-table patch at D2Game+0xF9340 (DWORD-swap, NOT JMP detour)
- [x] In-town blocking with on-screen notice
- [x] Hell-only key drop (event/cow loot table reused)
- [x] Boss organ drops from Hell Prime Evils (~85% first-kill, ~50% replay)
- [x] All 6 ubers tuned to level 110 with buffed stats
- [x] Phase 2 Hellfire Torch drop wired to finale-uber kill scan
- [x] `Ubers_OnUnitDeathScan` callback in unit-death walk
- [x] Cheat menu Pand tab for direct spawn / event item testing

### STK (Stackable) Stash Tabs
- [x] STK_C Consumables tab (39 cells: potions/scrolls/keys/organs/essences)
- [x] STK_R Runes tab (33 cells: all 33 runes Low/Mid/High/Top)
- [x] STK_G Gems tab (35 cells: 7 colors × 5 grades)
- [x] AP variant (per-character `ap_stash_stk_<charname>.dat`)
- [x] SH variant (account-wide `shared_stash_stk.dat`)
- [x] Yellow/gold tab buttons with 3-letter labels
- [x] Ghost-image rendering via fnCelDraw mode 2 (trans50)
- [x] DC6 cel cache (lazy load + invalidate on char change)
- [x] Multi-cell footprint indicator (thin border)
- [x] Count overlay (white < 100, yellow 100-998, gold 999)
- [x] Shift+rclick deposit from backpack
- [x] Shift+rclick withdraw to cursor
- [x] Left-click pickup (one item at a time)
- [x] Drag-and-drop deposit (cursor → cell)
- [x] Cursor disposal via `D2GAME_RemoveItemIfOnCursor` (ghost-cursor fix)
- [x] Whitelist enforcement (107 stackable item codes)
- [x] Quest-item exclusion
- [x] 999 max enforcement
- [x] StashStackMeta sidecar I/O (load on char change, save on swap)

### Runeword Backports
- [x] 7 patch 1.11 runewords (Bone/Enlightenment/Myth/Peace/Principle/Rain/Treachery)
- [x] 7 D2R 2.4 runewords (Plague/Pattern/Mist/Obsession/Wisdom/Unbending Will/Flickering Flame)
- [x] 6 D2R 2.6 runewords (Cure/Hearth/Hustle armor+weap/Mosaic/Metamorphosis)
- [x] D2R 2.6 runewords moved into vanilla rows + patchstring.tbl overrides (crash fix)
- [x] 17 TEST: cube recipes for runeword testing in cubemain.txt

### D2R 3.0 Skill Values Port
- [x] All 210 vanilla skills updated in Skills.txt
- [x] All 210 entries updated in Game/Archipelago/skill_data.dat
- [x] Multi-synergy parser (handles Ice Bolt's 5 synergies, Holy Fire's 2, etc.)
- [x] SFD_MAX_SYN bumped 4 → 6
- [x] Skill renames via patchstring.tbl (Fireball, Blade Mastery, etc.)

### Token of Absolution
- [x] Added to Misc.txt as `tof`
- [x] Added to Ctrl+V cheat menu under CHARACTER section
- [x] Added to STK_C tab layout (row 9)

### Dev Tools (Ctrl+V) Cheat Menu Redesign
- [x] 3-column grid layout (~509×620 px) with section headers
- [x] CHARACTER / TRAPS / BOSS LOOT / RUNES / BASES / GOODIES sections
- [x] Solid opaque backgrounds (transMode=1 fix)
- [x] Unified `g_cheatItemCmd` dispatch
- [x] All boss-loot drops (Andariel..Baal)
- [x] Heal Full + Unlock All Skills + Stat/Skill Pts
- [x] Pand tab for uber spawns + event items
- [x] Spawn SuperUnique + Spawn Monsters

### Stash bug fix — socketed items survive tab swap
- [x] SerFill mirrors `PlrSave.cpp:1426-1492` (D2's own .d2s loader)
- [x] Reads `D2ItemSaveStrc.nItemFileIndex` for child count
- [x] Re-uses `sub_6FC898F0` with parent arg = D2's socket-attach path
- [x] Tested with Hustle Flamberge (Shael+Ko+Eld): runes survive tab swap + log cycle

### AP Bridge fixes
- [x] Connect button no longer flips characters into AP mode unless authenticated
- [x] `g_apPolling` separated from `g_apMode`
- [x] `g_apMode` flipped only on `status=authenticated` transition
- [x] F1 panel mouse handler now gated on `apPanelVisible` (no more click-through to Connect)
- [x] Race-window freeze deferral removed (no longer needed)

### Launcher 1.5.1 — Pre-launch installation verifier
- [x] `GameDownloader.VerifyInstallationAsync()` — fast file-size check
      against game_manifest.json
- [x] `GameDownloader.RepairFilesAsync()` — downloads only missing/wrong-size
      files from raw.githubusercontent
- [x] `LaunchGame()` runs verify+repair loop up to 3× before launching
- [x] After 3 consecutive failures: MessageBox warning suggests AV /
      SmartScreen / firewall as root cause
- [x] Manifest cached locally on every install/update for offline verify

### Launcher 1.5.1 — Settings & Logic Guide tab
- [x] New "Guide" menu item (8 tabs total: News / Patch Notes / Guide /
      Settings / Game Log / AP Bridge / Discord / Exit)
- [x] `Settings_Guide.md` — 25 KB, 701 lines, complete YAML / Goal /
      sphere-logic reference
- [x] Loaded from raw.githubusercontent on each launch with local
      fallback to launcher-bundled copy
- [x] Sits at repo root + `launcher/Settings_Guide.md` (in `launcher_package.zip`)

### Launcher 1.5.2 — Iron Frame visual redesign
- [x] Window enlarged 950×580 → 1040×660 (matches design pack target)
- [x] Stone-wall texture as window background
- [x] Sidebar widened to 230px, painted with `sidebar.png`
- [x] Active nav item shows `arrow.png` + gold text (replaces gold-bar)
- [x] Bottom action bar 124px tall using `bottom_bar.png`
- [x] `small_panel.png` painted plates behind Bug Form / Report Bug
- [x] All color tokens calibrated to `tokens.css` (gold #d4a04a, ink
      #e8d9b8, ember #c8541a, etc.)
- [x] Status text + version sub-line repositioned inside the bar interior
      with `CL_INK_SOFT` for the version line so it's actually readable

### Launcher 1.5.3 — Emerald flame seam
- [x] Procedural flame strip generated at startup (`BuildFlameStrip()`)
      — base glow + 75 Bezier flame tongues + ember sparks, deterministic
      seed for stable rendering
- [x] Painted behind the masthead, with the masthead darkening fading to
      transparent over its bottom 50px so flames glow through cleanly
- [x] Sidebar / content / bottom-bar all paint on top — flames only
      visible at the seam between masthead and content area

### apworld test pass (2026-04-30)
- [x] 17 multiworlds generated covering all goal modes, zone-locking
      variants, granular Collection toggle combinations, multiworld
      with mixed games (D2 + ChecksFinder/VVVVVV/Yacht/HK/Stardew/etc.)
- [x] Bug fixed: Goal=Collection (3) was overflowing the 3-element
      diff_names array — now treats as Normal-only difficulty scope
- [x] Bug fixed: ALWAYS_OPEN_ZONES (Halls of Anguish/Death/Vaught/
      Nihlathak's Temple) bypassed all access rules — now correctly
      gated by previous-difficulty Baal kill + previous-act boss kill

---

## 🚧 STILL TO DO before 1.9.0 ships

### System 2 — F1 Collection Page (REVISED 2026-04-30)
**Spec:** `Research/SYSTEM_2_F1_COLLECTION_PAGE_2026-04-30.md`

The 2026-04-28 stash-tab design was replaced. New approach is a
**read-only F1 book page** (page 3, after Skill Editor + Quest Book)
that always shows every collectible in the game as a grid of icons.
Ghost images for un-collected; solid icons for collected. Optional
Goal=3 mode layered on top.

User-confirmed scope (2026-04-30 locked decisions):
- ALWAYS-on F1 page, regardless of mode
- Categories: Sets / Runes / Gems / Special Items / Gold
- Auto-detection — scans inventory + stash + equipped + belt
- Goal=3 mode is OPTIONAL — player can use the page as a tracker
  without setting it as goal
- **Per-character flags** (each char has its own .dat)
- **First-found Unix timestamps stored per slot** (4 bytes × ~205)
- **Gold = lifetime-earned counter** (pickup + quest reward only;
  NOT vendor sales). Monotonic, never decreases. Display as
  "Total: 1,234,567 gp". Goal mode: "X / Y" (player picks target).
- **Drop-source + req-level shown in tooltip** (hardcoded lookup)
- **Goal collections frozen at character creation** (isolation
  pattern, matches feedback_settings_isolation.md)

#### Phase 1 — Data model + persistence ✅ DONE 2026-04-30
- [x] Author `d2arch_collections.c` with all collection definitions:
      - [x] 32 vanilla sets (127 set-pieces with `dwCode` mappings)
      - [x] 33 runes (r01..r33)
      - [x] 35 gems (per type+grade)
      - [x] 10 Special items (3 Pand keys + 3 organs + 3 essences + Hellfire Torch)
      - [ ] Token of Absolution slot (deferred — verify if `tof` should be
            collectible or just a consumable)
- [x] Define `CollectionFlag[]` (205 bits via flags[26] bitset)
- [x] Implement `ap_collections_<charname>.dat` sidecar I/O
- [x] Wire `Coll_LoadForCharacter` into `OnCharacterLoad`
- [x] Wire `Coll_SaveForCharacter` on unload + flag flip
- [x] Sanity-built (EXIT_CODE=0, DLL "Beta 1.9.0" stamped)

#### Phase 2 — Detection ✅ DONE 2026-04-30
- [x] Scan pInventory + equipped slots + belt (single linked list walk
      via D2Common ord 10277 + 10304) on tick
- [x] Set items routed via dwQuality==5 + dwFileIndex (1:1 with catalog
      order), non-set items via dwClassId map built lazily on first scan
- [ ] Stash sidecar scanning — ap_stash, shared_stash, ap_stash_stk,
      shared_stash_stk (deferred — items pass through inventory before
      deposit so they get marked while held; only edge case is items
      placed before 1.9.0 was installed)
- [ ] Hook `ITEMS_PickUpItem` for instant live updates (deferred —
      current 2-second polling is fine for tracker UX)
- [ ] Scan merc pInventory after merc-load (deferred)
- [x] Sticky flags (never un-collect)
- [x] Gold delta-watcher (approach A: first-tick latch + per-frame
      delta poll). Vendor-UI filter is stubbed — TODO Phase 5
      refines to exclude sale revenue.

#### Phase 3 — F1 Collection page rendering ✅ DONE 2026-04-30
- [x] Extended F1 page state from 3 pages (0-2) to 8 pages (0-7).
      Pages 3-7 dispatched to `Coll_RenderF1Page`. Next/Back buttons
      cap raised from `< 2` to `< 7`.
- [x] Per-page layouts:
      - Page 3: Sets Part 1 (sets 0-15 in compact list with inline pieces)
      - Page 4: Sets Part 2 (sets 16-31, includes 7 class-locked sets)
      - Page 5: Runes (11×3 grid with Low/Mid/High dividers)
      - Page 6: Gems (5×7 grid: 5 grades across × 7 colors down)
      - Page 7: Specials (5×2 grid) + lifetime Gold counter
- [x] Ghost-render unflagged cells (mode=2 trans50) — REUSES STK
      cel-load pattern. Falls back to 3-char text label if DC6 fails.
- [x] Solid-render flagged cells (mode=5) with gold-tinted backdrop.
- [x] Hover tooltip: name + status + drop source + req level +
      first-found Unix timestamp formatted as YYYY-MM-DD.
- [x] Per-page progress summary ("X / Y collected") + total at top-right.
- [x] Sanity-built EXIT_CODE=0 (DLL ~538KB, +23KB for Phase 2-4 code).

#### Phase 4 — Goal=3 (Collection) win-condition integration ✅ DONE 2026-04-30
- [x] Extended `g_apGoal` cap from 0..2 → 0..3 in `d2arch_ap.c`
      (both INI parse path and slot_data parse path).
- [x] Sidecar persists `CollectionGoalConfig` alongside state, so the
      goal targets are frozen per character (matches feedback_settings_isolation).
- [x] `Coll_IsGoalComplete()` checks all targeted sets + runes/gems/
      specials + gold target.
- [x] Win-condition fires in gameloop tick: sets `g_apGoalComplete`,
      writes `ap_goal.dat`, shows "COLLECTION GOAL COMPLETE!" notify.
      Reuses existing AP victory pipeline.
- [x] Default behavior when Goal=3 + empty config = "all targets on"
      (strictest — full collection required to win).
- [ ] Title-screen Goal=3 sub-config UI (checkbox tree). User must
      currently override defaults via INI for v1.
- [ ] apworld `Goal: Choice` with Collection option + CollectionTargets
      option (deferred — needed before public release).
- [ ] Reserve location IDs 49000-49063 (collection-complete) +
      item IDs 50000-50063 (Knowledge of Collection X) — deferred.

#### Phase 5 — Polish ✅ MOSTLY DONE 2026-04-30 (5/8 items)
- [ ] Gold-flash celebration on first detection per slot (deferred to v2)
- [x] Vendor-UI filter for gold tracking — checks UI_NPCSHOP (0x0C) and
      UI_MPTRADE (0x17) gpUIState entries; vendor open → skip credit
- [x] Authoritative DC6 invFile names — runtime lookup from sgptDataTables
      pSetItemsTxt (set pieces by name match) + pItemsTxt (runes/gems/
      specials by dwCode match). Override array per slot, lazy-built on
      first cel resolve. Falls back to catalog placeholder if not found.
- [ ] Search/filter box on collection pages (deferred to v2)
- [ ] Auto-deselect class-locked sets when wrong class picked goal (v2)
- [x] Stash sidecar scanning — STK tabs (Consumables/Runes/Gems) scanned
      via direct dwCode reads. Regular stash (ap_stash, shared_stash)
      deferred — those need bitstream parsing via ord 10883 (v2)
- [ ] Merc inventory scanning — deferred. Requires walking D2Client
      monster-unit hash table to find merc by owner GUID; no clean
      lookup function in 1.10f. Items typically pass through player
      inventory before being equipped on merc anyway, so this is
      a small edge case.
- [ ] Live ITEMS_PickUpItem hook — deferred. Current 2s polling is
      sufficient for tracker UX; live hook is nice-to-have.

#### Phase 4.5 — Goal=3 sub-config UI ✅ DONE 2026-04-30
- [x] Goal dropdown extended with index 15 = "Collection"
- [x] 4 toggles on title screen (Coll Set / Coll Runes / Coll Gems /
      Coll Spec) — defaults all-on
- [x] INI keys CollGoalSets / CollGoalRunes / CollGoalGems / CollGoalSpecials
      saved + loaded
- [x] g_apGoal=3 ↔ ts_Goal=15 conversion in both directions
- [x] DLL applies INI overrides to g_collGoal during Coll_LoadForCharacter

#### Phase 4.6 — apworld Goal=Collection ✅ DONE 2026-04-30
- [x] options.py: Goal Choice extended with `option_collection = 3`
- [x] options.py: 4 new toggles (CollectionTargetSets/Runes/Gems/Specials)
- [x] options.py: CollectionGoldTarget Range option (0..100M)
- [x] __init__.py fill_slot_data forwards all 5 fields to bridge
- [x] Bridge writes them to ap_settings.dat (no bridge changes needed —
      it iterates self.slot_data.items() so new keys auto-flow)
- [x] DLL parses 5 keys in d2arch_ap.c slot_data path → override globals
- [x] apworld repacked (27,957 bytes; old 27,403)

**Status:** Phase 1-4 (V1 minimum-viable) is shippable today. Phase 5
items are nice-to-have polish that can ship in 1.9.0 or 1.9.1
depending on time before the 2026-05-07/08 release window.

**Test plan (in-game verification needed):**
- Test 1: Load char, press F1, navigate Next 3 times → arrives on
  Sets Part 1. Pieces show as ghost icons or 3-char fallback labels.
- Test 2: Pick up a known set piece (e.g. cube up Sigon's pieces via
  cheat menu), wait 2s → corresponding cell turns gold + "✓ Collected"
  badge + tooltip shows "Collected YYYY-MM-DD".
- Test 3: Pick up gold from any source → "Lifetime Gold Earned"
  counter on Page 7 increments. Drop gold via vendor → counter
  does NOT decrement.
- Test 4: Set `Goal=3` in `d2arch.ini` → reload character → win
  condition checks. Use cheat menu to spawn every collectible → on
  the last one, "COLLECTION GOAL COMPLETE!" notification fires and
  `ap_goal.dat` says `goal=complete\nsource=collection`.
- Test 5: Two characters with separate names → each has independent
  collection state via `ap_collections_<charname>.dat`.

### STK polish (deferred — not blocking)
- [ ] Tooltip insertion ("Tal Rune ×47" on hover)
- [ ] Drop validation with red-flash on stack-full / wrong-type
- [ ] Auto-consolidate when an item is dropped via vanilla left-click drag
      (currently shift+rclick is the supported deposit path)

### Quality of Life backlog
**Spec:** `Research/QOL_BACKLOG_2026-04-30.md`

Already done in 1.9.0:
- [x] Tome of Town Portal stacks to 999 (was 20)
- [x] Tome of Identify stacks to 999 (was 20)

Pending for 1.9.0:
- [ ] Skeleton Key stacks to 99 (was 12)
- [ ] Pandemonium Keys / Boss Organs / Essences stack to 99 each
      (verify Misc.txt defaults)
- [ ] Token of Absolution as a quest-respec consumable (currently
      only in cheat menu; should be cubable like 1.13c)

Pending for later (see backlog doc for full list):
- Pet/merc HP bars
- Stash auto-arrange
- Charm inventory page
- Press-X-to-take-all on chests
- ...

### Phase 1 backport-plan (still pending)
From `Research/PATCH_BACKPORT_ANALYSIS_1_10_to_D2R_2026-04-28.md`,
ranked by ease/value:
- [ ] 12 set-buffs (D2R updated set bonuses for popular item sets)
- [ ] FE/IM/Willowisp monster-immunity nerfs
- [ ] High rune drop rate boost (TreasureClassEx)
- [ ] Bulwark / Ground / Temper from D2R 2.6 (sparse public data; revisit)

### Earlier deferred items
- [ ] Object-spawn implementation (Approach B from
  `Research/object_spawn_findings_2026-04-27.md` Part 3) — extra
  barrels in Blood Moor etc. Could share dt_patch infra with future
  System 1 cleanup.
- [ ] Bug R8/R10 retest ("an evil force" skill name lookup) on a
  tester rig (Maegis, etc.)
- [ ] F4 per-gate AP key location render (data plumbing exists
  from 1.8.5 but UI not wired)
- [ ] Hint-based cross-world key tracking in bridge
- [ ] Cow portal as a real custom skill (research done, not impl)
- [ ] True entrance shuffle via path-2 collision hook (vs. current
  warp-based approach)

---

## ⚙️ Release-blocker checklist (when 1.9.0 is ready to ship)

Per `feedback_release_process.md` and `feedback_version_policy.md`:
- [ ] All in-scope items above marked done
- [ ] DLL rebuilt with 1.9.0 version stamp baked in (currently
      d2arch_version.h says 1.9.0 but the deployed DLL was built
      while it was 1.8.6 — needs a rebuild via `_build_nopause.bat`)
- [ ] Bootstrap launcher rebuilt via `_build_bootstrap.bat`
      (per `feedback_launcher_rebuild.md` — banner reads
      `D2ARCH_VERSION_DISPLAY` at compile time, so the launcher
      currently shows "Beta 1.8.6")
- [ ] Launcher published (`dotnet publish`)
- [ ] `ap_bridge.exe` re-frozen if `ap_bridge.py` changed
- [ ] `apworld/diablo2_archipelago.apworld` repacked (last touched
      2026-04-28 — needs repack if any apworld files change)
- [ ] `game_manifest.json` regenerated
- [ ] `launcher_package.zip` SHA-256 → `launcher_version.txt`
- [ ] All 11 forbidden Blizzard files confirmed absent
      (per `feedback_blizzard_files.md`)
- [ ] `patch_notes_1.9.0.md` finalized
- [ ] `news.txt` updated with new features (DONE 2026-04-30)
- [ ] Repo cleanup before push:
      - [ ] 12 Screenshot001-012.png removed from Game/
      - [ ] skill_data.dat.before_d2r_* backup files removed
      - [ ] Game/Patch_D2.mpq.backup_pre_1.8.0 + d2data/d2exp variants removed
      - [ ] Game/Save/ ensure not tracked
      - [ ] Game/SessionLogs/ ensure not tracked
- [ ] User explicitly says "klar til release"
- [ ] **THEN** `git push` + `gh release create`

---

## 🔗 Memory rules in play

- `feedback_version_policy.md` — version stays at 1.9.0 across all
  in-flight fixes; only bumps when actually uploaded
- `feedback_no_ai_references.md` — NO Co-Authored-By in commits or
  public files
- `feedback_launcher_rebuild.md` — ALWAYS rebuild bootstrap launcher
  on release day
- `feedback_blizzard_files.md` — 11 Blizzard files forbidden in
  release packages
- `feedback_no_memory_hacking.md` — client-side memory hacking is
  cosmetic only (server-side patches via gpGame are fine)
- `feedback_settings_isolation.md` — existing chars read settings
  ONLY from per-char file; new chars freeze AP slot_data once
- `feedback_skill_persistence.md` — `.d2s` is authoritative
- `feedback_zone_locking_arch.md` — preloads, gate bosses, kill
  persistence, F4 tracker
- `feedback_english_game_text.md` — ALL in-game text English only

---

*This TODO is a living document — items will be checked off as
they're completed. Keep version at 1.9.0 until upload happens.*
