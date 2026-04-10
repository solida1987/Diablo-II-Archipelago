# Diablo II Archipelago

A randomizer mod for Diablo II: Lord of Destruction (1.10f) with [Archipelago](https://archipelago.gg/) multiworld support.

Randomizes skill unlocks across a quest system spanning all 5 Acts and 3 difficulties. Complete quests to earn skills from any of the 7 character classes. Play solo with custom settings or connect to an Archipelago multiworld server for cross-game randomization.

---

## Download & Install

1. Download **launcher_package.zip** from [Releases](https://github.com/solida1987/Diablo-II-Archipelago/releases/latest)
2. Extract to a folder
3. Run **Diablo II Archipelago.exe**
4. The launcher downloads and installs the game automatically

### Requirements
- Windows 10/11
- .NET 8 Runtime
- Valid Diablo II + Lord of Destruction CD keys

### Windows SmartScreen
Click **"More info"** then **"Run anyway"** on the blue warning screen.

---

## Features

- **210 Skills** from all 7 classes randomized into a quest reward pool
- **276+ Quests** across 5 Acts and 3 difficulties: Story, Hunting, Kill Zones, Exploration, Waypoints, Level Milestones
- **Skill Editor** (F1 page 1): Drag and drop skills into 3 tiers with 10 slots each
- **Skill Tree** (S key): Spend skill points on assigned skills with tier-colored layout
- **Quest Book** (F1 page 2): Scrollable quest log with filter tabs and act/difficulty selection
- **AP Connection** (F1 page 3): Connect to Archipelago multiworld servers
- **Monster Shuffle**: 20 preset configurations randomize monster appearances and abilities
- **Boss Shuffle**: Act end bosses swap positions
- **XP Multiplier**: Configurable experience rate (1x-10x)
- **Expanded Inventory**: 10x8 inventory, 10x10 stash, 10x8 cube
- **Zone Explorer Mode**: Zone keys gate area access for exploration-focused gameplay
- **Trap System**: Filler quests spawn dangerous monsters near you
- **Reset Points**: Earned from quests, used to reassign skills in the editor
- **Tier System**: T1 (green), T2 (blue), T3 (orange) skill gating
- **Cheat Menu** (Ctrl+V): Debug tools for testing
- **Delta Updates**: Launcher downloads only changed files on update
- **Launcher Self-Update**: Launcher checks for new versions automatically

---

## How to Play

### Standalone (Singleplayer)
1. Open the launcher
2. Configure settings (goal, quest types, skill pool, monster shuffle, etc.)
3. Click **PLAY**
4. Create a new character

### Archipelago Multiworld
1. Open the launcher
2. Start a game, press F1, go to page 3 (AP Connection)
3. Enter server address, slot name, and password
4. Click **Connect**

**Important:** Always fully close and restart the game before creating a new character.

---

## Controls

| Key | Action |
|-----|--------|
| F1 | Skill Editor (page 1), Quest Book (page 2), AP Connection (page 3) |
| S | Open Skill Tree (spend skill points) |
| F3 | Toggle Quest Tracker HUD |
| F4 | Zone Map (Zone Explorer mode) |
| Ctrl+V | Cheat Menu |
| Ctrl+O | Graphics Settings (d2gl) |
| ESC | Close any open panel |

---

## Built With

- [D2MOO](https://github.com/nicodoctor/D2MOO) - Diablo II open-source reimplementation
- [D2.Detours](https://github.com/nicodoctor/D2.Detours) - DLL patching framework
- [d2gl](https://github.com/nicodoctor/d2gl) - HD graphics renderer
- [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw) - Graphics wrapper
- [Archipelago](https://archipelago.gg/) - Multiworld randomizer framework

## Credits

- **solida** - Project lead, game systems, quest design, AP integration
- **D2MOO Team** - Open-source Diablo II reimplementation
- **Archipelago Community** - Multiworld framework and support
- **Diablo II Modding Community** - Research, tools, and documentation

## License

This project is a modification for Diablo II: Lord of Destruction. A legal copy of the original game is required to play.
