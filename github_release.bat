@echo off
title Diablo II Archipelago - GitHub Release beta-1.3.0
echo ============================================
echo   Diablo II Archipelago - GitHub Release
echo ============================================
echo.

set ROOT=%~dp0
set REL=%ROOT%Release\Diablo II Archipelago
set VERSION=beta-1.3.0
set ZIPNAME=Diablo-II-Archipelago-%VERSION%.zip

:: Git root is one level above D2MOO + AP
set GITROOT=%ROOT%..\
cd /d "%ROOT%"

:: ============================================
:: Step 1: Build release package
:: ============================================
echo [1/9] Building release package...
call "%ROOT%build_release.bat" >nul 2>&1
if not exist "%REL%\D2ArchSetup.exe" (
    echo   ERROR: Release build failed! D2ArchSetup.exe not found.
    pause
    exit /b 1
)
echo   Done.
echo.

:: ============================================
:: Step 2: Generate README.md
:: ============================================
echo [2/9] Generating README.md...
(
echo # Diablo II Archipelago
echo.
echo A randomizer mod for Diablo II: Lord of Destruction ^(1.10f^) with [Archipelago](https://archipelago.gg/^) multiworld support.
echo.
echo Randomizes skill unlocks across a quest system spanning all 5 Acts. Complete quests to earn skills from any of the 7 character classes. Play solo with custom settings or connect to an Archipelago multiworld server for cross-game randomization.
echo.
echo ---
echo.
echo ## Features
echo.
echo - **210 Skills** from all 7 classes ^(Amazon, Sorceress, Necromancer, Paladin, Barbarian, Druid, Assassin^) randomized into a quest reward pool
echo - **227 Quests** across 5 Acts: Story, Super Unique Hunting, Zone Clears, Exploration, Waypoints, Level Milestones
echo - **Expanded Inventory**: 10x8 inventory, 10x10 stash, 10x8 cube
echo - **Skill Editor** ^(F1^): Assign unlocked skills to your build with 3 tabs and 10 slots per tab
echo - **Quest Log** ^(F2^): Track progress with Main/Side quest tabs, scrollbar, and per-difficulty tracking
echo - **Quest Tracker HUD** ^(F3^): Shows current objectives and goal progress
echo - **Trap System**: Certain quests spawn dangerous Super Unique monsters near you
echo - **Reset Points**: Earned from quests, used to reassign skills
echo - **Tier System**: T1 ^(Level 1+^), T2 ^(Level 20+^), T3 ^(Level 40+^) skill gating
echo - **Archipelago Integration**: Full multiworld support with item sending/receiving, DeathLink, and goal tracking
echo - **Configurable Settings**: Quest types, skill pool size, filler distribution, and more
echo.
echo ---
echo.
echo ## Installation
echo.
echo ### Requirements
echo - Diablo II + Lord of Destruction ^(original installation required^)
echo - Windows 10 or later
echo.
echo ### Steps
echo 1. Download the latest release ZIP from [Releases](https://github.com/solida1987/Diablo-II-Archipelago/releases^)
echo 2. Extract the ZIP anywhere on your computer
echo 3. Run **D2ArchSetup.exe**
echo 4. Click Browse and select your Diablo II installation folder
echo 5. Click Install
echo 6. Launch with **Play Archipelago.exe**
echo.
echo The installer copies required files from your Diablo II installation. Your original game is not modified.
echo.
echo ---
echo.
echo ## How to Play
echo.
echo ### Singleplayer
echo 1. Run **Play Archipelago.exe**
echo 2. Select **Singleplayer**
echo 3. Configure game settings ^(goal scope, quest types, skill pool, filler distribution^)
echo 4. Click **Play**
echo 5. Create a new character or continue an existing one
echo.
echo ### Archipelago Multiworld
echo 1. Install the **.apworld** file ^(found in files/Archipelago/^) into your Archipelago installation
echo 2. Generate a multiworld with your YAML settings
echo 3. Host or connect to an AP server
echo 4. Run **Play Archipelago.exe**
echo 5. Select **Archipelago**
echo 6. Enter server address, slot name, and password
echo 7. Click **Play**
echo.
echo The console window shows AP connection status and events in real-time.
echo.
echo ---
echo.
echo ## Controls
echo.
echo ^| Key ^| Action ^|
echo ^|-----^|--------^|
echo ^| Key ^| Action ^|
echo ^| Configurable ^| Open/Close Skill Editor ^(default F1^) ^|
echo ^| Configurable ^| Open/Close Quest Log ^(default F2^) ^|
echo ^| Configurable ^| Toggle Quest Tracker HUD ^(default F3^) ^|
echo ^| Configurable ^| Toggle Quickcast ^(default F4^) ^|
echo ^| Configurable ^| Zone Map ^(default F5, Zone Explorer mode^) ^|
echo ^| ESC ^| Close any open panel ^|
echo ^| Shift+P ^| Toggle packet logging ^(debug^) ^|
echo.
echo All keybindings are configurable in the launcher. Controller support available.
echo.
echo ---
echo.
echo ## AP World Options ^(YAML^)
echo.
echo ^| Option ^| Type ^| Default ^| Description ^|
echo ^|--------^|------^|---------^|-------------^|
echo ^| game_mode ^| Choice ^| skill_hunt ^| Skill Hunt ^(skills are progression^) / Zone Explorer ^(zone keys are progression^) ^|
echo ^| goal ^| Choice ^| full_game_normal ^| Combined act+difficulty: Act 1-5 x Normal/Nightmare/Hell ^(15 options^) ^|
echo ^| quest_story ^| Toggle ^| true ^| Include story quests ^|
echo ^| quest_hunting ^| Toggle ^| true ^| Include Super Unique hunting quests ^|
echo ^| quest_kill_zones ^| Toggle ^| true ^| Include zone clear quests ^|
echo ^| quest_exploration ^| Toggle ^| true ^| Include area entry quests ^|
echo ^| quest_waypoints ^| Toggle ^| true ^| Include waypoint quests ^|
echo ^| quest_level_milestones ^| Toggle ^| true ^| Include level milestone quests ^|
echo ^| skill_pool_size ^| 20-210 ^| 210 ^| Number of skills in the item pool ^|
echo ^| starting_skills ^| 1-20 ^| 6 ^| Skills unlocked at start ^|
echo ^| filler_gold_pct ^| 0-100 ^| 30 ^| Gold filler weight ^|
echo ^| filler_stat_pts_pct ^| 0-100 ^| 15 ^| Stat point filler weight ^|
echo ^| filler_skill_pts_pct ^| 0-100 ^| 15 ^| Skill point filler weight ^|
echo ^| filler_trap_pct ^| 0-100 ^| 15 ^| Trap filler weight ^|
echo ^| filler_reset_pts_pct ^| 0-100 ^| 25 ^| Reset point filler weight ^|
echo ^| death_link ^| Toggle ^| false ^| Enable DeathLink ^|
echo ^| monster_shuffle ^| Toggle ^| false ^| Shuffle all monster types across areas ^|
echo ^| boss_shuffle ^| Toggle ^| false ^| Shuffle all SuperUnique bosses across areas ^|
echo.
echo ---
echo.
echo ## Built With
echo.
echo - [D2MOO](https://github.com/nicodoctor/D2MOO^) - Diablo II open-source reimplementation for D2Common, D2Game, and Fog DLLs
echo - [D2.Detours](https://github.com/nicodoctor/D2.Detours^) - DLL patching and injection framework
echo - [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw^) - Graphics wrapper for windowed mode on modern Windows
echo - [Archipelago](https://archipelago.gg/^) - Cross-game multiworld randomizer framework
echo.
echo ## Credits
echo.
echo - **solida** - Project lead, game systems, quest design, AP integration
echo - **D2MOO Team** - Open-source Diablo II reimplementation
echo - **Archipelago Community** - Multiworld framework and support
echo - **Diablo II Modding Community** - Research, tools, and documentation
echo.
echo ## License
echo.
echo This project is a modification for Diablo II: Lord of Destruction. A legal copy of the original game is required to play.
) > "%GITROOT%README.md"
:: Also copy LICENSE to git root
if exist "%ROOT%LICENSE" copy /Y "%ROOT%LICENSE" "%GITROOT%LICENSE" >nul
echo   Done.
echo.

:: ============================================
:: Step 3: Create ZIP file
:: ============================================
echo [3/9] Creating ZIP file...
if exist "%GITROOT%%ZIPNAME%" del /Q "%GITROOT%%ZIPNAME%"
cd /d "%ROOT%Release"
powershell -Command "Compress-Archive -Path 'Diablo II Archipelago' -DestinationPath '%GITROOT%%ZIPNAME%' -Force"
cd /d "%ROOT%"
if exist "%GITROOT%%ZIPNAME%" (
    echo   Created: %ZIPNAME%
) else (
    echo   ERROR: ZIP creation failed!
    pause
    exit /b 1
)
echo.

:: ============================================
:: Step 4: Copy files to git root (flat structure)
:: ============================================
echo [4/9] Copying files to git root (flat structure)...
cd /d "%GITROOT%"

:: Copy folder structures
xcopy /E /Y /I "%ROOT%Archipelago\d2arch.ini" "%GITROOT%Archipelago\" >nul 2>&1
xcopy /E /Y /I "%ROOT%Archipelago\skill_icon_map.dat" "%GITROOT%Archipelago\" >nul 2>&1
xcopy /E /Y /I "%ROOT%data\global\excel\*.txt" "%GITROOT%data\global\excel\" >nul 2>&1
xcopy /E /Y /I "%ROOT%data\global\ui\SPELLS\*.DC6" "%GITROOT%data\global\ui\SPELLS\" >nul 2>&1
xcopy /E /Y /I "%ROOT%data\global\ui\panel\*.dc6" "%GITROOT%data\global\ui\panel\" >nul 2>&1
xcopy /E /Y /I "%ROOT%patch\*.dll" "%GITROOT%patch\" >nul 2>&1
copy /Y "%ROOT%build_release.bat" "%GITROOT%build_release.bat" >nul 2>&1
copy /Y "%ROOT%github_release.bat" "%GITROOT%github_release.bat" >nul 2>&1
copy /Y "%ROOT%ddraw.ini" "%GITROOT%ddraw.ini" >nul 2>&1
echo   Done.
echo.

:: ============================================
:: Step 5: Remove old tracked files and re-stage
:: NO source code, NO .claude/, NO memory/, NO AI files
:: ============================================
echo [5/9] Staging files...
cd /d "%GITROOT%"
git rm -r --cached . >nul 2>&1

:: Stage flat structure at root
git add README.md
git add LICENSE
git add .gitignore 2>nul
git add Archipelago/d2arch.ini
git add Archipelago/skill_icon_map.dat
git add data/global/excel/*.txt
git add data/global/ui/SPELLS/*.DC6
git add data/global/ui/panel/*.dc6
git add patch/*.dll
git add build_release.bat
git add github_release.bat
git add ddraw.ini
echo   Done.
echo.

:: ============================================
:: Step 6: Commit
:: ============================================
echo [6/9] Committing...
cd /d "%GITROOT%"
git commit -m "Release %VERSION% - Quest flag detection, skill mapping, expanded inventory, bugfixes"
if errorlevel 1 (
    echo   ERROR: Commit failed!
    pause
    exit /b 1
)
echo   Done.
echo.

:: ============================================
:: Step 7: Tag
:: ============================================
echo [7/9] Tagging %VERSION%...
cd /d "%GITROOT%"
git tag -d %VERSION% >nul 2>&1
git tag %VERSION%
echo   Done.
echo.

:: ============================================
:: Step 8: Push to GitHub
:: ============================================
echo [8/9] Pushing to GitHub...
cd /d "%GITROOT%"
git push origin main --tags --force
if errorlevel 1 (
    echo   ERROR: Push failed!
    pause
    exit /b 1
)
echo   Done.
echo.

:: ============================================
:: Step 9: Create GitHub Release with ZIP
:: ============================================
echo [9/9] Creating GitHub release...
cd /d "%GITROOT%"
gh release delete %VERSION% -y >nul 2>&1
gh release create %VERSION% "%GITROOT%%ZIPNAME%" --title "%VERSION% - Diablo II Archipelago" --notes "## Diablo II Archipelago %VERSION%

### New Game Mode: Zone Explorer
- **Zone Explorer mode**: Zones are locked behind progression keys. Unlock zone keys to access new areas
- **35 zone keys** across all 5 acts with prerequisite-based dependency graph
- **Randomized zone unlock order**: Each character seed creates a unique labyrinth path through the game
- **Reactive teleport-back**: Players entering locked zones are warped to town via LEVEL_WarpUnit
- **Zone Map UI (F5)**: Shows all zones with OPEN/LOCKED status, difficulty tabs, scrollbar
- **Per-character settings**: Each character preserves its own game mode, goal, quest toggles, and filler distribution

### Combined Goal + Difficulty Setting
- **15 goal options**: Choose act scope (1-5) AND difficulty (Normal/Nightmare/Hell) in one dropdown
- Example: 'Acts 1-2 Nightmare' = play Acts 1-2 on Normal AND Nightmare, beat Duriel on NM to win
- Replaces separate goal_scope and difficulty_scope settings

### Customizable Keybindings
- **Full keybinding support**: All 18 keybindings configurable in launcher (click button, press key)
- **5 custom keys**: Skill Editor, Quest Log, Tracker, Quickcast, Zone Map
- **13 vanilla D2 keys**: Inventory, Character, Spellbook, Automap, Run/Walk, Belt slots, etc.
- **Vanilla key remapping**: D2's built-in keys can be rebound to any key
- Keybindings saved per-installation in d2arch.ini

### Controller Support (XInput)
- **Enable Controller** checkbox in launcher
- **Left stick**: Move mouse cursor with dead zone and acceleration
- **A/B buttons**: Left/right click
- **X/Y/LB/RB**: Belt slots 1-4
- **D-Pad**: Inventory, Character, Skill Editor, Quest Log
- **Triggers**: LT = show items (Alt), RT = stand still (Shift)
- **Start/Back**: ESC / Automap

### Bugfixes
- **Skill point rewards**: Fixed skill points using cosmetic SetStat instead of .d2s modification
- **Boss shuffle revert**: Fixed bosses staying shuffled when disabled — now always restores originals first
- **Hunting quests**: Removed forced disable when monster shuffle is active
- **Filler distribution**: DLL now reads user's filler percentage settings instead of hardcoded values
- **Skill names fixed**: Werebear, Werewolf, Poison Creeper, Lycanthropy, Decoy corrected
- **Monster density**: Increased density for small dungeon areas (Tower, Maggot Lair, Glacial Caves, etc.)
- **Per-character persistence**: Settings, seed, and quest state now fully per-character
- **AP/standalone sync**: All settings flow correctly through both AP and standalone pipelines
- **starting_gold removed**: Gold is earned through gameplay, not given at start

### Installation
1. Download and extract the ZIP below
2. Run **D2ArchSetup.exe**
3. Browse to your Diablo II installation folder and click Install
4. Launch with **Play Archipelago.exe**

### Requirements
- Diablo II + Lord of Destruction (original installation required)
- Windows 10 or later

### Credits
- solida - Project lead
- D2MOO Team - Open-source D2 reimplementation
- Archipelago Community - Multiworld framework"
if errorlevel 1 (
    echo   WARNING: gh release failed. Upload ZIP manually at:
    echo   https://github.com/solida1987/Diablo-II-Archipelago/releases/new
)
echo.

:: ============================================
:: Done
:: ============================================
echo ============================================
echo   RELEASE COMPLETE!
echo ============================================
echo.
echo   Repository: https://github.com/solida1987/Diablo-II-Archipelago
echo   Release:    https://github.com/solida1987/Diablo-II-Archipelago/releases/tag/%VERSION%
echo   ZIP:        %ZIPNAME%
echo.
echo ============================================
pause
