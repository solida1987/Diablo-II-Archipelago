@echo off
title Diablo II Archipelago - GitHub Release
echo ============================================
echo   Diablo II Archipelago - GitHub Release
echo ============================================
echo.

set ROOT=%~dp0
set REL=%ROOT%Release\Diablo II Archipelago
set VERSION=v0.2.0
set ZIPNAME=Diablo-II-Archipelago-%VERSION%.zip

cd /d "%ROOT%"

:: ============================================
:: Step 1: Build release package
:: ============================================
echo [1/8] Building release package...
call "%ROOT%build_release.bat" >nul 2>&1
if not exist "%REL%\Play Archipelago.exe" (
    echo   ERROR: Release build failed!
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
echo [4/8] Staging new source files...
git add Archipelago/src/*.c
git add Archipelago/src/*.py
git add Archipelago/src/*.bat
git add Archipelago/src/*.rc
git add Archipelago/src/*.ico
git add Archipelago/src/*.manifest
git add Archipelago/src/*.h
git add Archipelago/data/
git add Archipelago/lib/
git add Archipelago/tools/
git add Archipelago/files/
git add Archipelago/build/*.dll
git add Archipelago/build/*.exe
git add "Archipelago/acc Temp/"
git add apworld/
git add data/
git add build_release.bat
git add github_release.bat
git add .gitignore
echo   Done.
echo.

:: ============================================
:: Step 5: Commit
:: ============================================
echo [5/8] Committing...
git commit -m "Release %VERSION% - Quest system, AP Bridge GUI, skill randomizer"
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
gh release create %VERSION% "%ROOT%%ZIPNAME%" --title "%VERSION% - Quest System, AP Bridge GUI, Skill Randomizer" --notes "## Diablo II Archipelago %VERSION%

### New Features
- Quest system with all 5 Acts (story quests + kill quests per area)
- AP Bridge GUI - visual connection monitor with reconnect
- Skill randomizer - 60 random skills per playthrough seeded per character
- Quest tracker HUD (toggle with F3)
- Quest Log UI (F2) with Act tabs and scrolling
- Skill Editor with scrollbar and skill descriptions
- Filler rewards: random gold, stat points, skill points
- Character delete button with save cleanup
- Auto-cleanup of orphaned save files

### Installation
1. Download and extract the ZIP
2. Run D2ArchSetup.exe
3. Point it to your Diablo II installation
4. Launch with Play Archipelago.exe"
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
