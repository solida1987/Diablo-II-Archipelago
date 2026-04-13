# Diablo II Archipelago - Beta 1.7.0

## Download
Download **launcher_package.zip**, extract to a folder, run **Diablo II Archipelago.exe**.
The launcher downloads and installs the game automatically.

## IMPORTANT: Windows SmartScreen
Click **"More info"** then **"Run anyway"** on the blue warning screen.

## IMPORTANT: Character Switching
**Always fully close and restart the game before creating a new character.**

## Requirements
- Windows 10/11, .NET 8 Runtime
- An existing Diablo II + Lord of Destruction installation (the launcher will ask you to point to it)

## New Features in 1.7.0
- **All skills work on all classes** — runtime animation patching replaces class-specific S1/S2/S3/S4 modes with generic Cast animation, so Assassin traps, Druid shapeshifting, Amazon dodges and Paladin smite all work on any character without invisibility
- **No weapon restrictions** — all weapon type requirements removed at runtime. Assassin martial arts work without claws, Amazon javelin skills work without spears, Paladin Smite works without a shield. Every skill works with whatever gear you have equipped
- **Assassin trap filter removed** — all 210 skills are always in the pool, no more IPlayAssassin setting needed
- **Trap system with 4 trap types** — filler quests now trigger random traps: Monster Ambush, Decrepify (slow), Amplify Damage (double damage taken), or Poison (HP drain over time)
- **Trap duration scales with level** — 10 seconds per 10 character levels (level 1-10 = 10s, level 21-30 = 30s, etc.)
- **Boss loot drops as filler reward** — completing filler quests can now drop loot from Andariel, Duriel, Mephisto, Diablo or Baal on the ground next to you. Item level scales with character level
- **Draggable Dev Tools menu** — Ctrl+V menu can be dragged anywhere. Includes loot drops for all 5 act bosses and Unlock All Skills button

## Bug Fixes in 1.7.0
- **Cross-class skill animations fixed** — skills with class-specific animations (Assassin traps, Druid werewolf, Amazon passives, Paladin smite) no longer make the character invisible when used by other classes
- **Weapon type restrictions removed** — h2h (claws), spea (spear), jave (javelin), knif (dagger), shld (shield), thro (throwing) requirements all cleared at runtime for cross-class compatibility
- **Monster spawn parameters fixed** — SpawnMonster now uses correct animation mode and search radius, resolving all spawn failures
- **Trap curses auto-expire** — all curse effects automatically remove themselves after their duration, no more permanent debuffs
- **Poison ticks correctly** — poison trap drains HP every 0.5 seconds instead of a single instant hit
- **Curse visual + mechanical sync** — curse icon and stat penalty applied simultaneously
- **Skill tree icons persist after full game restart** — fixed race condition where panel reset cleared levels before reinvest could restore them
- **Skill level files no longer zeroed on save** — removed broken stat 107 read that was overwriting level files with zeros
- **Source code split into 17 modular files** — reduces risk of breaking unrelated systems when editing

## Launcher Changes in 1.7.0
- **CD key requirement removed** — the launcher no longer asks for CD keys
- **Original D2 installation required** — the launcher now asks you to point to your existing Diablo II installation folder. Original game files (MPQ archives, D2.LNG, etc.) are copied from your install automatically
- **Original Blizzard files removed from distribution** — 11 unmodified game files are no longer included in downloads (EULA compliance). They are sourced from your own D2 install instead
- **Auto-detection of D2 install** — the launcher checks common install paths and Windows Registry automatically
- **"Original Diablo II Installation" setting** — new field in Settings to browse for your D2 folder
- **Launcher version bumped to 1.3.0**

## Includes all 1.6.3 fixes
- Connection tab input leak fixed
- Skill tooltip damage corrected
- Zone Explorer keys preserved on reload
- Skill tree positions fixed for Tier 1 slots
- Skill replacement fully fixed with point refund
- Assigned skills hidden from pool list
- Monster shuffle spawn fix for minion types
- All class names and 18 skill names corrected
- All 210 skill tooltips now show descriptions
- Skill points no longer lost or duplicated on relog

## Known Issues
- **"An Evil Force" text** on 7 cross-class skills (Blade Sentinel/Fury/Shield, Werewolf, Multiple Shot, Strafe, Lightning Bolt)
- **"Treasure Cow" tooltip** on 3 poison skills (Rabies, Poison Javelin, Plague Javelin)
- **Whirlwind + Bow/Crossbow softlock** — do not use Whirlwind with ranged weapons
- **Javelin skills with Bow** — animation breaks when using javelin skills with a bow equipped
- **Animation flicker** on Blade Fury, Double Swing/Throw, Leap Attack
- **Druid/Assassin skill icons** may show incorrect icons (DC6 frame order mismatch)
- **Martial Arts with weapons** — Fists of Fire, Claws of Thunder, Blades of Ice, Dragon Claw may require unarmed
- Skill tooltips still missing detailed information for some skills
- Always restart game between characters
- Multiplayer/Multiworld not yet fully supported — checks may not send consistently
- Monster shuffle can make some quests impossible (Den of Evil, hunt quests)
