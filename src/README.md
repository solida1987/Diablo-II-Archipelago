# Source

The C source for the Diablo II Archipelago mod DLL (`D2Archipelago.dll`)
and the bootstrap launcher (`D2Arch_Launcher.exe`), plus the Archipelago
world in `../apworld/`.

The mod is a single translation unit: `d2arch.c` includes every other `.c`
file in a fixed order, so include order is load-bearing -- see the list at
the top of `d2arch.c`. Everything hooks Blizzard's original 1.10f
`D2Game.dll` / `D2Client.dll` at documented offsets; none of Blizzard's
code or data files are in this repository.

Built with MSVC via `build.bat` (DLL) and `_build_bootstrap.bat` (launcher
exe). Both deploy straight into `Game/`.
