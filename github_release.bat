@echo off
title Diablo II Archipelago - GitHub Release beta-1.4.0
echo ============================================
echo   Diablo II Archipelago - GitHub Release
echo ============================================
echo.

set ROOT=%~dp0
set REL=%ROOT%Release\Diablo II Archipelago
set VERSION=beta-1.4.0
set ZIPNAME=Diablo-II-Archipelago-%VERSION%.zip

:: Git root is one level above D2MOO + AP
set GITROOT=%ROOT%..\
cd /d "%ROOT%"

:: ============================================
:: Step 1: Build release package
:: ============================================
echo [1/9] Building release package...
call "%ROOT%build_release.bat"
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
:: Use >> append instead of ( ) block to avoid batch parsing issues with special chars
> "%GITROOT%README.md" echo # Diablo II Archipelago
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo A randomizer mod for Diablo II: Lord of Destruction ^(1.10f^) with [Archipelago](https://archipelago.gg/^) multiworld support.
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo Randomizes skill unlocks across a quest system spanning all 5 Acts. Complete quests to earn skills from any of the 7 character classes. Play solo with custom settings or connect to an Archipelago multiworld server for cross-game randomization.
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ---
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ## Features
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo - **210 Skills** from all 7 classes ^(Amazon, Sorceress, Necromancer, Paladin, Barbarian, Druid, Assassin^) randomized into a quest reward pool
>> "%GITROOT%README.md" echo - **227 Quests** across 5 Acts: Story, Super Unique Hunting, Zone Clears, Exploration, Waypoints, Level Milestones
>> "%GITROOT%README.md" echo - **Expanded Inventory**: 10x8 inventory, 10x10 stash, 10x8 cube
>> "%GITROOT%README.md" echo - **Skill Editor** ^(F1^): Assign unlocked skills to your build with 3 tabs and 10 slots per tab
>> "%GITROOT%README.md" echo - **Quest Log** ^(F2^): Track progress with Main/Side quest tabs, scrollbar, and per-difficulty tracking
>> "%GITROOT%README.md" echo - **Quest Tracker HUD** ^(F3^): Shows current objectives and goal progress
>> "%GITROOT%README.md" echo - **Trap System**: Certain quests spawn dangerous Super Unique monsters near you
>> "%GITROOT%README.md" echo - **Reset Points**: Earned from quests, used to reassign skills
>> "%GITROOT%README.md" echo - **Tier System**: T1 ^(Level 1+^), T2 ^(Level 20+^), T3 ^(Level 40+^) skill gating
>> "%GITROOT%README.md" echo - **Archipelago Integration**: Full multiworld support with item sending/receiving, DeathLink, and goal tracking
>> "%GITROOT%README.md" echo - **Configurable Settings**: Quest types, skill pool size, filler distribution, and more
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ---
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ## Installation
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ### Requirements
>> "%GITROOT%README.md" echo - Diablo II + Lord of Destruction ^(original installation required^)
>> "%GITROOT%README.md" echo - Windows 10 or later
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ### Steps
>> "%GITROOT%README.md" echo 1. Download the latest release ZIP from [Releases](https://github.com/solida1987/Diablo-II-Archipelago/releases^)
>> "%GITROOT%README.md" echo 2. Extract the ZIP anywhere on your computer
>> "%GITROOT%README.md" echo 3. **Run D2ArchSetup.exe as Administrator** ^(right-click ^> Run as administrator^)
>> "%GITROOT%README.md" echo 4. Click Browse and select your Diablo II installation folder
>> "%GITROOT%README.md" echo 5. Click Install
>> "%GITROOT%README.md" echo 6. Launch with **Play Archipelago.exe**
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo The installer copies required files from your Diablo II installation. Your original game is not modified.
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ---
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ## How to Play
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ### Singleplayer
>> "%GITROOT%README.md" echo 1. Run **Play Archipelago.exe**
>> "%GITROOT%README.md" echo 2. Select **Singleplayer**
>> "%GITROOT%README.md" echo 3. Configure game settings ^(goal scope, quest types, skill pool, filler distribution^)
>> "%GITROOT%README.md" echo 4. Click **Play**
>> "%GITROOT%README.md" echo 5. Create a new character or continue an existing one
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ### Archipelago Multiworld
>> "%GITROOT%README.md" echo 1. Install the **.apworld** file ^(found in files/Archipelago/^) into your Archipelago installation
>> "%GITROOT%README.md" echo 2. Generate a multiworld with your YAML settings
>> "%GITROOT%README.md" echo 3. Host or connect to an AP server
>> "%GITROOT%README.md" echo 4. Run **Play Archipelago.exe**
>> "%GITROOT%README.md" echo 5. Select **Archipelago**
>> "%GITROOT%README.md" echo 6. Enter server address, slot name, and password
>> "%GITROOT%README.md" echo 7. Click **Play**
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo The console window shows AP connection status and events in real-time.
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ---
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ## Controls
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ^| Key ^| Action ^|
>> "%GITROOT%README.md" echo ^|-----^|--------^|
>> "%GITROOT%README.md" echo ^| Configurable ^| Open/Close Skill Editor ^(default F1^) ^|
>> "%GITROOT%README.md" echo ^| Configurable ^| Open/Close Quest Log ^(default F2^) ^|
>> "%GITROOT%README.md" echo ^| Configurable ^| Toggle Quest Tracker HUD ^(default F3^) ^|
>> "%GITROOT%README.md" echo ^| Configurable ^| Toggle Quickcast ^(default F4^) ^|
>> "%GITROOT%README.md" echo ^| Configurable ^| Zone Map ^(default F5, Zone Explorer mode^) ^|
>> "%GITROOT%README.md" echo ^| ESC ^| Close any open panel ^|
>> "%GITROOT%README.md" echo ^| Shift+P ^| Toggle packet logging ^(debug^) ^|
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo All keybindings are configurable in the launcher. Controller support available.
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ---
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ## AP World Options ^(YAML^)
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ^| Option ^| Type ^| Default ^| Description ^|
>> "%GITROOT%README.md" echo ^|--------^|------^|---------^|-------------^|
>> "%GITROOT%README.md" echo ^| game_mode ^| Choice ^| skill_hunt ^| Skill Hunt ^(skills are progression^) / Zone Explorer ^(zone keys are progression^) ^|
>> "%GITROOT%README.md" echo ^| goal ^| Choice ^| full_game_normal ^| Combined act+difficulty: Act 1-5 x Normal/Nightmare/Hell ^(15 options^) ^|
>> "%GITROOT%README.md" echo ^| quest_story ^| Toggle ^| true ^| Include story quests ^|
>> "%GITROOT%README.md" echo ^| quest_hunting ^| Toggle ^| true ^| Include Super Unique hunting quests ^|
>> "%GITROOT%README.md" echo ^| quest_kill_zones ^| Toggle ^| true ^| Include zone clear quests ^|
>> "%GITROOT%README.md" echo ^| quest_exploration ^| Toggle ^| true ^| Include area entry quests ^|
>> "%GITROOT%README.md" echo ^| quest_waypoints ^| Toggle ^| true ^| Include waypoint quests ^|
>> "%GITROOT%README.md" echo ^| quest_level_milestones ^| Toggle ^| true ^| Include level milestone quests ^|
>> "%GITROOT%README.md" echo ^| skill_pool_size ^| 20-210 ^| 210 ^| Number of skills in the item pool ^|
>> "%GITROOT%README.md" echo ^| starting_skills ^| 1-20 ^| 6 ^| Skills unlocked at start ^|
>> "%GITROOT%README.md" echo ^| filler_gold_pct ^| 0-100 ^| 30 ^| Gold filler weight ^|
>> "%GITROOT%README.md" echo ^| filler_stat_pts_pct ^| 0-100 ^| 15 ^| Stat point filler weight ^|
>> "%GITROOT%README.md" echo ^| filler_skill_pts_pct ^| 0-100 ^| 15 ^| Skill point filler weight ^|
>> "%GITROOT%README.md" echo ^| filler_trap_pct ^| 0-100 ^| 15 ^| Trap filler weight ^|
>> "%GITROOT%README.md" echo ^| filler_reset_pts_pct ^| 0-100 ^| 25 ^| Reset point filler weight ^|
>> "%GITROOT%README.md" echo ^| death_link ^| Toggle ^| false ^| Enable DeathLink ^|
>> "%GITROOT%README.md" echo ^| monster_shuffle ^| Toggle ^| false ^| Shuffle all monster types across areas ^|
>> "%GITROOT%README.md" echo ^| boss_shuffle ^| Toggle ^| false ^| Shuffle all SuperUnique bosses across areas ^|
>> "%GITROOT%README.md" echo ^| shop_shuffle ^| Toggle ^| false ^| Shuffle vendor inventories across acts ^|
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ---
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ## Built With
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo - [D2MOO](https://github.com/nicodoctor/D2MOO^) - Diablo II open-source reimplementation for D2Common, D2Game, and Fog DLLs
>> "%GITROOT%README.md" echo - [D2.Detours](https://github.com/nicodoctor/D2.Detours^) - DLL patching and injection framework
>> "%GITROOT%README.md" echo - [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw^) - Graphics wrapper for windowed mode on modern Windows
>> "%GITROOT%README.md" echo - [Archipelago](https://archipelago.gg/^) - Cross-game multiworld randomizer framework
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ## Credits
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo - **solida** - Project lead, game systems, quest design, AP integration
>> "%GITROOT%README.md" echo - **D2MOO Team** - Open-source Diablo II reimplementation
>> "%GITROOT%README.md" echo - **Archipelago Community** - Multiworld framework and support
>> "%GITROOT%README.md" echo - **Diablo II Modding Community** - Research, tools, and documentation
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo ## License
>> "%GITROOT%README.md" echo.
>> "%GITROOT%README.md" echo This project is a modification for Diablo II: Lord of Destruction. A legal copy of the original game is required to play.
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
:: String table for custom names
if exist "%ROOT%data\local\LNG\ENG\patchstring.tbl" (
    mkdir "%GITROOT%data\local\LNG\ENG" >nul 2>&1
    copy /Y "%ROOT%data\local\LNG\ENG\patchstring.tbl" "%GITROOT%data\local\LNG\ENG\" >nul 2>&1
)
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
git add data/local/LNG/ENG/patchstring.tbl 2>nul
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
:: Write release notes to temp file to avoid batch parsing issues with special chars
set "NOTESFILE=%TEMP%\d2arch_release_notes.md"
> "%NOTESFILE%" echo ## Diablo II Archipelago %VERSION%
>> "%NOTESFILE%" echo.
>> "%NOTESFILE%" echo ### Custom Monsters
>> "%NOTESFILE%" echo - **Treasure Cow**: Custom monster with boss-tier loot table
>> "%NOTESFILE%" echo - Spawns rarely across 36 areas in all 5 acts
>> "%NOTESFILE%" echo - Custom name via patchstring.tbl string table system
>> "%NOTESFILE%" echo - Drops 7 items per kill including gems and boss loot
>> "%NOTESFILE%" echo.
>> "%NOTESFILE%" echo ### XP Multiplier
>> "%NOTESFILE%" echo - **1x to 5x XP slider** in launcher settings
>> "%NOTESFILE%" echo - Divides XP requirements AND increases ExpRatio
>> "%NOTESFILE%" echo.
>> "%NOTESFILE%" echo ### Shop Shuffle
>> "%NOTESFILE%" echo - **Randomize vendor inventories** across all acts
>> "%NOTESFILE%" echo.
>> "%NOTESFILE%" echo ### Antivirus Fix
>> "%NOTESFILE%" echo - **Monster shuffle rewritten in C** - no more false positives
>> "%NOTESFILE%" echo.
>> "%NOTESFILE%" echo ### Bugfixes
>> "%NOTESFILE%" echo - Flamespike hunt removed - does not exist in LoD
>> "%NOTESFILE%" echo - Missing quests added: Cave L2, UG Passage L2, Hole L1/L2
>> "%NOTESFILE%" echo - Menu button repositioned - no longer covers HP bar
>> "%NOTESFILE%" echo - Quickcast crash fix
>> "%NOTESFILE%" echo - Controller skill assignment fix
>> "%NOTESFILE%" echo - Monster shuffle default behavior fix
>> "%NOTESFILE%" echo - .bin cache clearing on every launch
>> "%NOTESFILE%" echo - XP multiplier now divides XP requirements
>> "%NOTESFILE%" echo.
>> "%NOTESFILE%" echo ### Previous Features
>> "%NOTESFILE%" echo - Zone Explorer game mode with 35 zone keys
>> "%NOTESFILE%" echo - Combined Goal + Difficulty - 15 options
>> "%NOTESFILE%" echo - Customizable keybindings - 18 keys
>> "%NOTESFILE%" echo - Controller support - XInput gamepad
>> "%NOTESFILE%" echo - Per-character settings persistence
>> "%NOTESFILE%" echo - Boss shuffle, monster shuffle, shop shuffle
>> "%NOTESFILE%" echo.
>> "%NOTESFILE%" echo ### Installation
>> "%NOTESFILE%" echo 1. Download and extract the ZIP below
>> "%NOTESFILE%" echo 2. **Run D2ArchSetup.exe as Administrator**
>> "%NOTESFILE%" echo 3. Browse to your Diablo II folder and click Install
>> "%NOTESFILE%" echo 4. Launch with **Play Archipelago.exe**
gh release create %VERSION% "%GITROOT%%ZIPNAME%" --title "%VERSION% - Diablo II Archipelago" --notes-file "%NOTESFILE%"
if errorlevel 1 (
    echo   WARNING: gh release failed. Upload ZIP manually at:
    echo   https://github.com/solida1987/Diablo-II-Archipelago/releases/new
)
del "%NOTESFILE%" >nul 2>&1
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
