@echo off
title Diablo II Archipelago - GitHub Release beta-1.0.0
echo ============================================
echo   Diablo II Archipelago - GitHub Release
echo ============================================
echo.

set ROOT=%~dp0
set REL=%ROOT%Release\Diablo II Archipelago
set VERSION=beta-1.0.0
set ZIPNAME=Diablo-II-Archipelago-%VERSION%.zip

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
echo - **231 Quests** across 5 Acts: Story, Super Unique Hunting, Zone Clears, Exploration, Waypoints, Level Milestones
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
echo ^| F1 ^| Open/Close Skill Editor ^|
echo ^| F2 ^| Open/Close Quest Log ^|
echo ^| F3 ^| Toggle Quest Tracker HUD ^|
echo ^| ESC ^| Close any open panel ^|
echo.
echo ---
echo.
echo ## AP World Options ^(YAML^)
echo.
echo ^| Option ^| Type ^| Default ^| Description ^|
echo ^|--------^|------^|---------^|-------------^|
echo ^| goal_scope ^| Choice ^| full_game ^| Act 1 only / Acts 1-2 / Acts 1-3 / Acts 1-4 / Full Game ^|
echo ^| difficulty_scope ^| Choice ^| normal_only ^| Normal / Normal+Nightmare / All Three ^|
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
echo ^| starting_gold ^| 0-50000 ^| 0 ^| Starting gold amount ^|
echo ^| death_link ^| Toggle ^| false ^| Enable DeathLink ^|
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
) > "%ROOT%README.md"
echo   Done.
echo.

:: ============================================
:: Step 3: Create ZIP file
:: ============================================
echo [3/9] Creating ZIP file...
if exist "%ROOT%%ZIPNAME%" del /Q "%ROOT%%ZIPNAME%"
cd /d "%ROOT%Release"
powershell -Command "Compress-Archive -Path 'Diablo II Archipelago' -DestinationPath '%ROOT%%ZIPNAME%' -Force"
cd /d "%ROOT%"
if exist "%ZIPNAME%" (
    echo   Created: %ZIPNAME%
) else (
    echo   ERROR: ZIP creation failed!
    pause
    exit /b 1
)
echo.

:: ============================================
:: Step 4: Remove ALL old tracked files from git
:: ============================================
echo [4/9] Removing all old files from git...
git rm -r --cached . >nul 2>&1
echo   Done.
echo.

:: ============================================
:: Step 5: Stage only the correct files
:: ============================================
echo [5/9] Staging files...
git add README.md
git add Archipelago/d2arch.ini
git add Archipelago/skill_icon_map.dat
git add data/global/excel/*.txt
git add data/global/ui/SPELLS/*.DC6
git add patch/*.dll
git add build_release.bat
git add github_release.bat
git add ddraw.ini
git add .gitignore 2>nul
echo   Done.
echo.

:: ============================================
:: Step 6: Commit
:: ============================================
echo [6/9] Committing...
git commit -m "Release %VERSION% - Full AP integration, configurable settings, 231 quests"
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
git tag -d %VERSION% >nul 2>&1
git tag %VERSION%
echo   Done.
echo.

:: ============================================
:: Step 8: Push to GitHub
:: ============================================
echo [8/9] Pushing to GitHub...
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
gh release delete %VERSION% -y >nul 2>&1
gh release create %VERSION% "%ROOT%%ZIPNAME%" --title "%VERSION% - Diablo II Archipelago" --notes "## Diablo II Archipelago %VERSION%

First public beta with full Archipelago multiworld support.

### Highlights
- 231 quests across all 5 Acts (Story, Hunting, Zone Clears, Exploration, Waypoints, Level Milestones)
- 210 skills from all 7 classes randomized into quest rewards
- Full Archipelago integration: connect to AP servers, send/receive items, DeathLink
- Configurable launcher with game settings (standalone) and AP connection
- Quest Log with player name display for multiworld checks
- Tier-gated skill system (T1: Lv1, T2: Lv20, T3: Lv40)
- Trap system spawning Super Unique monsters
- Reset Points for skill reassignment

### Installation
1. Download and extract the ZIP below
2. Run **D2ArchSetup.exe**
3. Browse to your Diablo II installation folder and click Install
4. Launch with **Play Archipelago.exe**

### Singleplayer
- Select Singleplayer in the launcher
- Configure quest types, skill pool, and filler distribution
- Click Play and create a character

### Archipelago Multiworld
- Install the .apworld (in files/Archipelago/) to your AP installation
- Generate a multiworld with your YAML
- Select Archipelago in the launcher, enter server details, click Play
- Console window shows real-time AP events

### AP World Options
See the [README](https://github.com/solida1987/Diablo-II-Archipelago/blob/main/README.md) for all configurable YAML options.

### Controls
| Key | Action |
|-----|--------|
| F1 | Skill Editor |
| F2 | Quest Log |
| F3 | Quest Tracker |
| ESC | Close panels |

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
