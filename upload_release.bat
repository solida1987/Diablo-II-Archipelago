@echo off
setlocal enabledelayedexpansion

set VERSION=0.1.0-alpha
set RELEASE_NAME=DiabloII-Archipelago-v%VERSION%
set ZIP_FILE=build\%RELEASE_NAME%.zip
set REPO=solida1987/Diablo-II-Archipelago

echo ==========================================
echo  Diablo II Archipelago - Upload Release
echo  Version: %VERSION%
echo  Repo: %REPO%
echo ==========================================
echo.

:: Check if ZIP exists
if not exist "%ZIP_FILE%" (
    echo ERROR: %ZIP_FILE% not found.
    echo Run build_release.bat first!
    pause
    exit /b 1
)

:: Check gh CLI
gh --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: GitHub CLI ^(gh^) is not installed.
    echo Install from: https://cli.github.com/
    pause
    exit /b 1
)

:: Check auth
gh auth status >nul 2>&1
if errorlevel 1 (
    echo ERROR: Not logged in to GitHub CLI.
    echo Run: gh auth login
    pause
    exit /b 1
)

echo Uploading to GitHub...
echo.

:: Initialize git repo if needed
if not exist ".git" (
    echo Initializing git repository...
    git init
    git remote add origin https://github.com/%REPO%.git
)

:: Stage only launcher source files
git add .gitignore
git add requirements.txt
git add launcher/__init__.py
git add launcher/main.py
git add launcher/core/*.py
git add launcher/gui/*.py
git add launcher/data/vanilla_txt/*
git add launcher/data/icons_dc6/*
git add launcher/data/units_palette.dat
git add launcher/x64/StormLib.dll

:: Commit
git commit -m "Release v%VERSION%"

:: Push
git branch -M main
git push -u origin main --force

:: Create GitHub release with ZIP
echo.
echo Creating GitHub release v%VERSION%...
gh release create "v%VERSION%" "%ZIP_FILE%" --repo "%REPO%" --title "v%VERSION% - Alpha Release" --notes "## Diablo II Archipelago Randomizer v%VERSION%

### What's included
- Randomized skill trees (30 random skills from all 7 classes)
- Skill tree editor with drag-and-drop
- Area kill tracking with real-time detection
- Character profiles with save isolation
- Map reset functionality

### Setup
1. Install Python 3.10+ (add to PATH)
2. Download and extract the ZIP
3. Copy your D2 LOD 1.14b game files into the folder
4. Run Launch.bat

### Requirements
- Windows 10/11
- Python 3.10+
- Diablo II: Lord of Destruction (1.14b)"

if errorlevel 1 (
    echo.
    echo Release upload failed. Check errors above.
) else (
    echo.
    echo ==========================================
    echo  Release uploaded successfully!
    echo  https://github.com/%REPO%/releases/tag/v%VERSION%
    echo ==========================================
)

echo.
pause
