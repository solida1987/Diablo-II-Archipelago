# Version Propagation — places that consume D2ARCH_VERSION_DISPLAY

## Source of truth
- `Tools/Archipelago/src/d2arch_version.h`
  - `D2ARCH_VERSION_MAJOR / MINOR / PATCH` (numeric)
  - `D2ARCH_VERSION_STRING` (e.g. `"BETA_1_8_5"` for filenames)
  - `D2ARCH_VERSION_DISPLAY` (e.g. `"Beta 1.8.5"` for UI)
  - `D2ARCH_VERSION_DISPLAY_W` (wide-string equivalent)

## Automatic — picks up the macro on rebuild
- `Tools/Archipelago/src/d2arch_bootstrap.c` → produces **D2Arch_Launcher.exe**
  - Console banner `"  Diablo II Archipelago - %s\n"`
  - **MUST rebuild** via `_build_bootstrap.bat` after every version bump.
  - Deploy: `Game/D2Arch_Launcher.exe`
- `Tools/Archipelago/src/d2arch.c` (single-TU build) → produces **D2Archipelago.dll**
  - In-game banner, F1/F4 panels, version-patch overlay (`VersionPatchApply`),
    skill-tree welcome string, etc. all read `D2ARCH_VERSION_DISPLAY`.
  - Build via `build_now.bat`.
  - Deploy: `Game/D2Archipelago.dll` + `Tools/D2Archipelago.dll`
    + `Game/patch/D2Archipelago.dll` + (build dir self-deploy).
- `Tools/Archipelago/src/d2arch_versionpatch.c`
  - Memory patch that overwrites D2's hardcoded `"v 1.10c"` string in
    D2Client.dll/D2Launch.dll with `D2ARCH_VERSION_DISPLAY`.

## Manual — text files that the macro does NOT reach
On every version bump, do these by hand:

- `patch_notes_<X>.md` — create a new file (or finalize the in-flight one)
  with the matching version. **Per `feedback_version_policy.md`, version
  stays the same across in-flight fixes — only bump on actual upload.**
- `Game/Archipelago/news.txt` — add `=== Beta X.Y.Z - New Features ===`
  section at the top. Old sections stay below for history.
- `cdkey_validate.h` — `VERSION_STRING` if present.
- `Game/START.bat` — window title + echo header (currently shows version
  in `title` line).
- `game_manifest.json` (root + Game/) — regenerated at release time, but
  the manifest header may carry a version label.
- `launcher_version.txt` — if shipped, update.

## Build/deploy checklist on every version bump
1. Edit `d2arch_version.h` — bump `D2ARCH_VERSION_PATCH` (or higher).
2. Run `Tools/Archipelago/src/build_now.bat` — rebuilds `D2Archipelago.dll`.
3. Run `Tools/Archipelago/src/_build_bootstrap.bat` — rebuilds
   `D2Arch_Launcher.exe`. **EASY TO FORGET — the launcher banner is
   the visible one when the user double-clicks the launcher.**
4. Build the bridge: `py -m PyInstaller ap_bridge.spec --noconfirm`.
5. Deploy DLL to: `Tools/`, `Game/`, `Game/patch/`.
6. Deploy bootstrap: `Game/D2Arch_Launcher.exe`.
7. Deploy bridge: copy `dist/ap_bridge/*` into `Game/ap_bridge_dist/`.
8. Edit `news.txt` + `patch_notes_<X>.md` for human-readable changelog.

## Historical landmines (don't repeat)
- 1.8.1: bootstrap rebuilt forgotten — launcher console kept showing
  "Beta 1.8.1" through 1.8.2/1.8.3/1.8.4/1.8.5 dev because nobody ran
  `_build_bootstrap.bat` until 1.8.5 caught it.
- 1.7.x: `Tools/_backup/D2ArchLauncher/MainForm.cs` had hardcoded
  `Beta 1.7.1` strings — those won't update from the macro because that
  C# project doesn't include the C header. Either port the launcher to
  read the version from a shared file, or just edit by hand each time.
  (Currently the C# launcher isn't shipping — d2arch_bootstrap.c is the
  active launcher.)
- DetoursLauncher binary historically showed `Beta 1.6.0` — source at
  `Tools/_backup/Archipelago_backup_160/src/injector.c:62`. Replaced by
  `d2arch_bootstrap.c` in 1.8.0; old binary should not be re-shipped.
