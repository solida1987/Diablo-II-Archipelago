# D2Arch_Launcher.exe / Bootstrap

## What it is

A **silent bootstrap** that injects `D2Archipelago.dll` into `Game.exe`.
Invoked internally by the C# launcher (`launcher/Diablo II Archipelago.exe`).

**The user never sees this process** — it runs with a hidden window and exits as soon as injection is complete.

## Why it exists

D2 1.10f's `Game.exe` doesn't know anything about `D2Archipelago.dll`. Something has to load the DLL into the game's process memory so the hooks activate. That's this binary.

## The chain

```
User double-clicks → launcher/Diablo II Archipelago.exe      (C# UI)
                        ↓ Process.Start()
                     Game/D2Arch_Launcher.exe                (this bootstrap)
                        ↓ CreateProcessA()
                     Game/D2.DetoursLauncher.exe Game.exe -- (D2.Detours)
                        ↓ launches
                     Game.exe                                (vanilla D2)
                        ↓ waits for PID, then
                     CreateRemoteThread + LoadLibraryA
                        ↓ injects
                     D2Archipelago.dll now loaded in Game.exe's process
```

## Files

| File | Purpose |
|---|---|
| `d2arch_bootstrap.c` | Source code (110 lines) |
| `_build_bootstrap.bat` | Build script |
| `Game/D2Arch_Launcher.exe` | Deployed binary (121,344 bytes) |

## Building

```
cd Tools/Archipelago/src
_build_bootstrap.bat
cp D2Arch_Launcher.exe ../../../Game/D2Arch_Launcher.exe
```

Rebuild only when the injection logic needs to change. The version string inside the binary is independent of the DLL version, but should be bumped to match each release for diagnostic clarity.

## History

The source was originally at `Tools/_backup/Archipelago_backup_160/src/injector.c` (and got lost there during the 1.6.0 → 1.7.0 src folder refactor). The binary in `Game/` was the only remaining artifact, which made it look "orphaned" during the 1.8.0 audit.

In 1.8.0 cleanup: promoted the source back to `Tools/Archipelago/src/d2arch_bootstrap.c` with version bumped to 1.8.0. Binary is byte-size-identical to the 1.6.0 Apr-8 original (121,344 bytes) — only the version string inside differs.

## Do NOT confuse with

- `Tools/Archipelago/skal_maaske_slettes/dead_launcher/launcher.c` — a DIFFERENT launcher with a full settings dialog UI. Produces a 400 KB binary. Legacy / unused. NOT the source for this bootstrap.
- `launcher/Diablo II Archipelago.exe` — the C# UI launcher. Invokes this bootstrap, does NOT replace it.
- `Game/D2.DetoursLauncher.exe` — D2.Detours framework helper. Used BY this bootstrap, does NOT replace it.
