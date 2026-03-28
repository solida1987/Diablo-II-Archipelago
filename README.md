# Diablo II Archipelago

A randomizer mod for Diablo II: Lord of Destruction (1.10f) with [Archipelago](https://archipelago.gg/) multiworld support.

Randomizes skill unlocks across a quest system spanning all 5 Acts. Complete quests to earn skills from any of the 7 character classes. Play solo with custom settings or connect to an Archipelago multiworld server for cross-game randomization.

---

## Features

- **210 Skills** from all 7 classes (Amazon, Sorceress, Necromancer, Paladin, Barbarian, Druid, Assassin) randomized into a quest reward pool
- **227 Quests** across 5 Acts: Story, Super Unique Hunting, Zone Clears, Exploration, Waypoints, Level Milestones
- **Expanded Inventory**: 10x8 inventory, 10x10 stash, 10x8 cube
- **Skill Editor** (F1): Assign unlocked skills to your build with 3 tabs and 10 slots per tab
- **Quest Log** (F2): Track progress with Main/Side quest tabs, scrollbar, and per-difficulty tracking
- **Quest Tracker HUD** (F3): Shows current objectives and goal progress
- **Trap System**: Certain quests spawn dangerous Super Unique monsters near you
- **Reset Points**: Earned from quests, used to reassign skills
- **Tier System**: T1 (Level 1+), T2 (Level 20+), T3 (Level 40+) skill gating
- **Archipelago Integration**: Full multiworld support with item sending/receiving, DeathLink, and goal tracking
- **Configurable Settings**: Quest types, skill pool size, filler distribution, and more

---

## Installation

### Requirements
- Diablo II + Lord of Destruction (original installation required)
- Windows 10 or later

### Steps
1. Download the latest release ZIP from [Releases](https://github.com/solida1987/Diablo-II-Archipelago/releases)
2. Extract the ZIP anywhere on your computer
3. **Run D2ArchSetup.exe as Administrator** (right-click > Run as administrator)
4. Click Browse and select your Diablo II installation folder
5. Click Install
6. Launch with **Play Archipelago.exe**

The installer copies required files from your Diablo II installation. Your original game is not modified.

---

## How to Play

### Singleplayer
1. Run **Play Archipelago.exe**
2. Select **Singleplayer**
3. Configure game settings (goal scope, quest types, skill pool, filler distribution)
4. Click **Play**
5. Create a new character or continue an existing one

### Archipelago Multiworld
1. Install the **.apworld** file (found in files/Archipelago/) into your Archipelago installation
2. Generate a multiworld with your YAML settings
3. Host or connect to an AP server
4. Run **Play Archipelago.exe**
5. Select **Archipelago**
6. Enter server address, slot name, and password
7. Click **Play**

The console window shows AP connection status and events in real-time.

---

## Controls

| Key | Action |
|-----|--------|
| Configurable | Open/Close Skill Editor (default F1) |
| Configurable | Open/Close Quest Log (default F2) |
| Configurable | Toggle Quest Tracker HUD (default F3) |
| Configurable | Toggle Quickcast (default F4) |
| Configurable | Zone Map (default F5, Zone Explorer mode) |
| ESC | Close any open panel |
| Shift+P | Toggle packet logging (debug) |

All keybindings are configurable in the launcher. Controller support available.

---

## AP World Options (YAML)

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| game_mode | Choice | skill_hunt | Skill Hunt (skills are progression) / Zone Explorer (zone keys are progression) |
| goal | Choice | full_game_normal | Combined act+difficulty: Act 1-5 x Normal/Nightmare/Hell (15 options) |
| quest_story | Toggle | true | Include story quests |
| quest_hunting | Toggle | true | Include Super Unique hunting quests |
| quest_kill_zones | Toggle | true | Include zone clear quests |
| quest_exploration | Toggle | true | Include area entry quests |
| quest_waypoints | Toggle | true | Include waypoint quests |
| quest_level_milestones | Toggle | true | Include level milestone quests |
| skill_pool_size | 20-210 | 210 | Number of skills in the item pool |
| starting_skills | 1-20 | 6 | Skills unlocked at start |
| filler_gold_pct | 0-100 | 30 | Gold filler weight |
| filler_stat_pts_pct | 0-100 | 15 | Stat point filler weight |
| filler_skill_pts_pct | 0-100 | 15 | Skill point filler weight |
| filler_trap_pct | 0-100 | 15 | Trap filler weight |
| filler_reset_pts_pct | 0-100 | 25 | Reset point filler weight |
| death_link | Toggle | false | Enable DeathLink |
| monster_shuffle | Toggle | false | Shuffle all monster types across areas |
| boss_shuffle | Toggle | false | Shuffle all SuperUnique bosses across areas |
| shop_shuffle | Toggle | false | Shuffle vendor inventories across acts |

---

## Built With

- [D2MOO](https://github.com/nicodoctor/D2MOO) - Diablo II open-source reimplementation for D2Common, D2Game, and Fog DLLs
- [D2.Detours](https://github.com/nicodoctor/D2.Detours) - DLL patching and injection framework
- [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw) - Graphics wrapper for windowed mode on modern Windows
- [Archipelago](https://archipelago.gg/) - Cross-game multiworld randomizer framework

## Credits

- **solida** - Project lead, game systems, quest design, AP integration
- **D2MOO Team** - Open-source Diablo II reimplementation
- **Archipelago Community** - Multiworld framework and support
- **Diablo II Modding Community** - Research, tools, and documentation

## License

This project is a modification for Diablo II: Lord of Destruction. A legal copy of the original game is required to play.
