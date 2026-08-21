# Multiworld Launcher plugin — source

This is the source of `diablo2_archipelago-*.londonplugin`, the plugin that
teaches the [Multiworld Launcher](https://github.com/solida1987/Multiworld-Launcher)
how to install, launch and track this mod. One assembly serves both channels;
each manifest names its own entry class.

Build: `dotnet build -c Release` with the launcher checked out as a sibling
folder (the csproj references `..\Multiworld-Launcher\LauncherV2.csproj`).
Package: `pack_plugin.py` from the launcher's `Tools/`.

The launcher-side API is documented in the launcher's `PLUGIN_API.md`.
