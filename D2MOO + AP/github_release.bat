@echo off
title Diablo II Archipelago - GitHub Release v0.3.0
echo ============================================
echo   Diablo II Archipelago - GitHub Release
echo ============================================
echo.

set ROOT=%~dp0
set REL=%ROOT%Release\Diablo II Archipelago
set VERSION=v0.3.0
set ZIPNAME=Diablo-II-Archipelago-%VERSION%.zip

cd /d "%ROOT%"

:: ============================================
:: Step 1: Build release package
:: ============================================
echo [1/8] Building release package...
call "%ROOT%build_release.bat" >nul 2>&1
if not exist "%REL%\D2ArchSetup.exe" (
    echo   ERROR: Release build failed! D2ArchSetup.exe not found.
    pause
    exit /b 1
)
echo   Done.
echo.

:: ============================================
:: Step 2: Create ZIP file
:: ============================================
echo [2/8] Creating ZIP file...
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
:: Step 3: Remove ALL old tracked files from git
:: ============================================
echo [3/8] Removing all old files from git...
git rm -r --cached . >nul 2>&1
echo   Done.
echo.

:: ============================================
:: Step 4: Stage only the correct files
:: ============================================
echo [4/8] Staging source files...
:: Only config and data — NO source code, NO research docs
git add Archipelago/d2arch.ini
git add Archipelago/skill_icon_map.dat
git add data/global/excel/*.txt
git add data/global/ui/SPELLS/*.DC6
git add patch/*.dll
git add build_release.bat
git add github_release.bat
git add .gitignore 2>nul
echo   Done.
echo.

:: ============================================
:: Step 5: Commit
:: ============================================
echo [5/8] Committing...
git commit -m "Release %VERSION% - D2MOO Edition: traps, quest overhaul, installer"
if errorlevel 1 (
    echo   ERROR: Commit failed!
    pause
    exit /b 1
)
echo   Done.
echo.

:: ============================================
:: Step 6: Tag
:: ============================================
echo [6/8] Tagging %VERSION%...
git tag -d %VERSION% >nul 2>&1
git tag %VERSION%
echo   Done.
echo.

:: ============================================
:: Step 7: Push to GitHub
:: ============================================
echo [7/8] Pushing to GitHub...
git push origin main --tags --force
if errorlevel 1 (
    echo   ERROR: Push failed!
    pause
    exit /b 1
)
echo   Done.
echo.

:: ============================================
:: Step 8: Create GitHub Release with ZIP
:: ============================================
echo [8/8] Creating GitHub release...
gh release delete %VERSION% -y >nul 2>&1
gh release create %VERSION% "%ROOT%%ZIPNAME%" --title "%VERSION% - D2MOO Edition: Traps, Quest Overhaul, Installer" --notes "## Diablo II Archipelago %VERSION% (D2MOO Edition)

### Major Changes
- Rebuilt on D2MOO + D2.Detours framework (replaces PlugY)
- New installer (D2ArchSetup.exe) with auto-detection
- 231 quests across 5 Acts x 3 difficulties
- Trap system: Super Uniques spawn on quest completion
- SuperUnique hunting quests (all acts)
- Level milestone quests (5, 10, 15... 50)
- Area enter quests (exploration)
- Filler rewards: gold, stat points, skill points, reset points, traps
- Reset Points system for skill removal
- Quest Log with Main/Side quest tabs and scrollbar
- D2 debug menu (Shift+0) with monster spawning
- Display mode selector (windowed/fullscreen)
- MPQ auto-fix for modern D2 installs

### Controls
- F1: Skill Editor
- F2: Quest Log
- F3: Quest Tracker
- ESC: Close panels
- Shift+0: Debug Menu

### Installation
1. Download and extract the ZIP
2. Run D2ArchSetup.exe
3. Point it to your Diablo II installation
4. Click Install
5. Launch with Play Archipelago.exe"
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
