using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.IO.Pipes;
using System.Linq;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using Microsoft.Win32;
using LauncherV2.Core;

namespace LauncherV2.Plugins.DiabloII;

// IGamePlugin for "Diablo II Archipelago"

// KEY CHANGE from V1 launcher architecture:
// D2Arch_Launcher.exe (standalone C injector) → called by C# launcher
// → did VirtualAllocEx + CreateRemoteThread from a SEPARATE PROCESS
// → AV scanners: "small standalone injector" = high-risk heuristic

// Injection lives HERE, inside D2Plugin (C# P/Invoke inside the main
// launcher process). The 154MB WPF app with proper PE metadata and
// (eventually) a code signature doing CreateRemoteThread is dramatically
// less suspicious than a 40KB standalone injector blob.
// D2Arch_Launcher.exe bootstrap is ELIMINATED for V2.

// IPC CHANGE:
// D2 DLL ↔ TCP socket ↔ ap_bridge.py ↔ AP server (Python subprocess)
// D2 DLL ↔ Named pipe ↔ D2Plugin (C# in launcher) ↔ ApClient ↔ AP server
// (No Python, no subprocess, no TCP port conflict)

// NOTE: The D2 DLL (D2Archipelago.dll) must be updated to open the named
// pipe client instead of its current TCP socket to ap_bridge.py.
// That DLL-side change is tracked in TODO_V2_DLL_BRIDGE.md.

// Unsealed so each channel can be its own entry type -- see the bottom of
// this file. A packaged plugin is constructed by the loader through a
// parameterless constructor, so "which channel" cannot be a flag someone sets
// from outside; it has to be the class itself.
public class D2Plugin : IGamePlugin
{
    // --- Identity ---

    public string GameId      => Experimental ? "diablo2_archipelago_experimental" : "diablo2_archipelago";
    public string DisplayName => Experimental ? "Diablo II: LoD (Experimental)"   : "Diablo II: Lord of Destruction";
    public string Subtitle    => Experimental ? "Randomiser Mod — Experimental"   : "Randomiser Mod";
    // Icon reuses the stable art for both channels (no separate experimental icon).
    public string IconPath    => Path.Combine(AppContext.BaseDirectory, "Assets", "diablo2_archipelago.png");

    // --- What the launcher can do, and where the base game comes from ---
    //
    // These four answers used to sit in the launcher's catalogue file. They
    // belong to the game: the launcher installs the mod on top of a copy the
    // player already owns, and nothing here is free.

    public InstallCapability InstallCapability => InstallCapability.AutoMod;
    public bool   IsFreeGame  => false;
    public string PurchaseUrl => "https://us.battle.net/shop/en/product/diablo-ii";
    public string WebsiteUrl  => Experimental
        ? "https://github.com/solida1987/Diablo-II-Archipelago-experimental"
        : "https://github.com/solida1987/Diablo-II-Archipelago";

    // --- Version state ---

    public string? InstalledVersion  { get; private set; }
    public string? AvailableVersion  { get; private set; }
    public bool    IsInstalled       => InstalledVersion != null;
    public bool    IsRunning         { get; private set; }

    // --- Configuration (set by caller before use) ---

    private string _gameDirectory = string.Empty;

    // Root directory of the D2 Archipelago installation.
    // Setter re-loads the persisted AP item resume index — the index file
    // lives under <GameDirectory>\Archipelago\, and the directory is assigned
    // AFTER construction (object initializer in App.xaml.cs), so loading in
    // the constructor would always read a non-existent relative path and
    // leave the index at 0 (re-delivering the full item history every run).
    public string GameDirectory
    {
        get => _gameDirectory;
        set
        {
            _gameDirectory = value;
            LoadApIndex();
        }
    }

    // Where the player's own Classic D2:LoD lives — used ONLY as the source to
    // copy the Blizzard MPQs from, never written to. Persisted in this plugin's
    // own settings bag.
    private string? _originalD2Directory;

    public string OriginalD2Directory
    {
        get => _originalD2Directory
               ??= PluginSettings.Get(GameId, SettingBaseFolder) ?? string.Empty;
        set
        {
            _originalD2Directory = value ?? string.Empty;
            PluginSettings.Set(GameId, SettingBaseFolder, _originalD2Directory);
        }
    }

    // True when OriginalD2Directory points at a valid Classic D2 + LoD folder.
    public bool IsOriginalD2Configured =>
        !string.IsNullOrEmpty(OriginalD2Directory) &&
        ValidateExistingInstall(OriginalD2Directory) == null;

    // --- Internal state ---

    private Process?                _gameProcess;
    private NamedPipeServerStream?  _pipe;
    private CancellationTokenSource? _pipeCts;

    // TRUE while the current game session was launched through the AP flow
    // (LaunchAsync); FALSE for standalone (LaunchStandaloneAsync) and when no
    // game is running. Standalone sessions ALSO open a pipe (CHECK-tracker
    // streaming), so "_pipe.IsConnected" alone is NOT an AP-session test:
    // without this gate, AP items and STATE:CONNECTED pushes leaked into
    // standalone games — granting multiworld items to an offline character
    // AND advancing _apItemIndex so the real AP game never got those items.
    private bool _sessionIsAp;

    // Serialises all pipe writes (ITEM: from ReceiveItemsAsync, STATE: from
    // OnApStateChanged) — PipeStream does not guarantee atomic interleaving
    // of concurrent writers, and the DLL frames messages by newline.
    private readonly SemaphoreSlim _pipeWriteLock = new(1, 1);

    // Last AP connection state seen via OnApStateChanged.
    // current state to the DLL right after it connects to the pipe (the
    // state may have changed to Connected BEFORE the game was launched, in
    // which case no further OnApStateChanged call would ever inform it).
    private ApConnectionState _lastApState = ApConnectionState.Disconnected;

    // AP item resume index — persisted to disk so we survive restarts
    private int _apItemIndex;

    // Character the item stream was last rewound for.
    // not the character, so each one that plays the slot gets the full stream
    // replayed once and its own per-character record filters the duplicates.
    private string? _apCharName;

    // --- AP bridge events ---

    public event Action<long[]>? LocationsChecked;

    // "RECV:<text>" from the DLL in standalone — a reward notification formatted
    // "<location>: <reward>".
    // tab so a solo run shows what each check granted (no AP server involved).
    public event Action<string>? StandaloneItemReceived;

    // The game's full active location universe (standalone only) — the DLL
    // streams it as MISSING: once the pipe connects so the tracker can show
    // unchecked locations + per-category totals like an AP session.
    // IGamePlugin (D2-specific); the host wires it via a cast.
    public event Action<long[]>? LocationsMissing;
    public event Action<int>?    GameExited;

    // Fired when the DLL sends a "GOAL" message (player completed the goal).
    // Caller should send StatusUpdate(ApClientStatus.Goal) to the AP server.
    public event Action? GoalCompleted;

    // Anything this plugin does on its own that belongs in the launcher's log.
    // The launcher subscribes when it registers the plugin.
    public event Action<string>? LogLine;

    // Invoked when the DLL reports the local player died ("DEATH:<cause>").
    // Assignment-style (not an event) so the per-launch re-wiring in the UI
    // layer stays idempotent — a second launch overwrites, never stacks.
    // Caller forwards to ApClient.SendDeathLinkAsync IF DeathLink is enabled.
    public Action<string>? OnPlayerDied { get; set; }

    // --- UI-layer injections (assignment-style = idempotent re-wiring) ---

    // Resolves an AP player slot to its display name for ITEM v2 messages.
    // The player map lives in the UI layer; null = plain ITEM:<id> fallback.
    // IMPORTANT: for the player's OWN items this must return the slot name —
    // the DLL compares it against [ap] SlotName for self-release detection.
    public Func<int, string>? ResolvePlayerName { get; set; }

    // Supplies the current AP slot_data (null when not connected).
    // write ap_settings.dat BEFORE the STATE:CONNECTED push so the DLL never
    // applies the connected state ahead of the settings file.
    public Func<JsonElement?>? GetSlotData { get; set; }

    // Supplies the current AP room seed name (null when not connected).
    // launch path derives a stable per-world seed from this so its data-file
    // randomization is reproducible across relaunches of the same multiworld.
    public Func<string?>? GetSeedName { get; set; }

    // Invoked once the game's pipe has attached (post-launch).
    // this to ApClient.SyncAsync() so the AP server re-sends the full item
    // stream from index 0 — re-delivering any items received while the player
    // was still sitting in the launcher (notably the PRECOLLECTED STARTING
    // SKILLS at item index 0), which ReceiveItemsAsync had to drop because the
    // pipe wasn't connected yet.
    // items are harmless. Assignment-style for idempotent per-launch re-wiring.
    public Func<Task>? RequestApResync { get; set; }

    // --- Gate-key locator wiring (set by the UI layer, which owns ApClient) ---

    // Scout our own locations. create_as_hint stays 0 on the far side: this is
    // a free lookup, it must never spend the player's hint points behind their
    // back.
    public Func<long[], Task>? RequestLocationScouts { get; set; }

    // AP location id → the name the player knows it by.
    //
    // Was a delegate the launcher assigned, which made the launcher hold the
    // apworld's id formula and its difficulty names. The knowledge is ours,
    // and now so is the lookup -- kept as a Func so the call sites that pass
    // it around do not change.
    public Func<long, string?> ResolveLocationName { get; }
        = D2LocationNames.Resolve;

    // Locations we have NOT sent yet — the only ones that can still hold a key.
    public Func<long[]>? GetUncheckedLocations { get; set; }

    // Locations already sent, so a key we have collected is not offered again.
    public Func<long[]>? GetCheckedLocations { get; set; }

    // Our own AP player slot, to tell our keys from other people's.
    public Func<int>? GetOwnSlot { get; set; }

    // How many copies of each progressive act key have arrived. Counted from
    // the item stream rather than asked for, because the server replays that
    // stream from index 0 on every reconnect — so the count is self-healing.
    private readonly Dictionary<long, int> _gateKeyCopies = new();

    // Last scout reply, kept so a later item delivery can recompute without
    // asking the server again.
    private ApNetworkItem[] _scoutedOwnLocations = Array.Empty<ApNetworkItem>();

    // --- GitHub API ---

    private static readonly HttpClient _http = new()
    {
        Timeout = TimeSpan.FromMinutes(30),
    };

    // Cached latest tag (e.g.
    // releases/latest redirect, which is served by GitHub's CDN and is not counted
    // against the REST API rate limit (60 req/hour unauthenticated).
    private string? _cachedLatestTag;

    static D2Plugin()
    {
        // GitHub API requires a User-Agent header; requests without one are rejected
        // or aggressively rate-limited (HTTP 429).
        _http.DefaultRequestHeaders.UserAgent.TryParseAdd("Archipelago-Launcher/2.0");
    }

    private const string GITHUB_OWNER    = "solida1987";

    // EXPERIMENTAL channel.
    // experimental build: it installs from the experimental repo into its own
    // separate folder (Games/diablo2_archipelago_experimental) and shows as its
    // own entry, fully isolated from the stable install.
    // initializer on the second registration in App.xaml.cs.
    public bool Experimental { get; protected init; } = false;

    private string GitHubRepo => Experimental
        ? "Diablo-II-Archipelago-experimental"
        : "Diablo-II-Archipelago";

    // We resolve the latest tag via the releases/latest REDIRECT (302 → tag URL)
    // instead of the GitHub REST API.
    // rate limit entirely — redirect responses are served by GitHub's CDN and are
    // not counted against the API quota.
    private string GH_LATEST_PAGE   => $"https://github.com/{GITHUB_OWNER}/{GitHubRepo}/releases/latest";
    private string GH_DOWNLOAD_BASE => $"https://github.com/{GITHUB_OWNER}/{GitHubRepo}/releases/download";
    private string GH_RELEASES_API  => $"https://api.github.com/repos/{GITHUB_OWNER}/{GitHubRepo}/releases";

    // --- File skip lists (mirrors V1 GameDownloader.cs — must stay in sync) ---
    
    // 8-script sync invariant: changes here must ALSO land in
    // _pack_game.py, generate_manifest.py, regen_manifest_from_git.py,
    // prerelease_check.bat, _audit_manifest.py, MainForm.cs ORIGINAL_D2_FILES,
    // AND .gitignore (V1 list — in V2 the launcher replaces GameDownloader.cs)

    // Original Blizzard files — NEVER downloaded, always copied from the player's own install.
    private static readonly HashSet<string> ORIGINAL_D2_FILES =
        new(StringComparer.OrdinalIgnoreCase)
    {
        "D2.LNG", "SmackW32.dll", "binkw32.dll",
        "d2exp.mpq", "d2music.mpq", "d2speech.mpq", "d2video.mpq",
        "d2xmusic.mpq", "d2xtalk.mpq", "d2xvideo.mpq",
        "ijl11.dll", "d2char.mpq", "d2data.mpq", "d2sfx.mpq",
        // The 1.10f engine — Blizzard's, so the player supplies these too.
        "Bnclient.dll", "D2CMP.dll", "D2Client.dll", "D2Common.dll",
        "D2DDraw.dll", "D2Direct3D.dll", "D2Game.dll", "D2Gdi.dll",
        "D2gfx.dll", "D2Glide.dll", "D2Lang.dll", "D2Launch.dll",
        "D2MCPClient.dll", "D2Multi.dll", "D2Net.dll", "D2sound.dll",
        "D2Win.dll", "Diablo II.exe", "Fog.dll", "Game.exe",
        "Patch_D2.mpq", "Storm.dll",
        // default.key is deliberately absent: it holds the player's key
        // bindings and the game writes it on first run, so a fresh
        // installation does not have one to copy.
    };

    // Exact byte sizes of the 1.10f engine files. The mod hooks fixed addresses
    // inside these binaries, so ANY other patch level (1.13c, 1.14b, …) loads
    // and then crashes in ways that look like an antivirus block.
    //
    // Two jobs: refuse a source folder that is not 1.10f, and refuse to copy a
    // wrong-version file over a correct one during an update.
    private static readonly Dictionary<string, long> D2_110F_SIZES =
        new(StringComparer.OrdinalIgnoreCase)
    {
        ["Bnclient.dll"]    = 131137,   ["D2CMP.dll"]        = 155710,
        ["D2Client.dll"]    = 1085505,  ["D2Common.dll"]     = 725057,
        ["D2DDraw.dll"]     = 81984,    ["D2Direct3D.dll"]   = 110659,
        ["D2Game.dll"]      = 1159231,  ["D2Gdi.dll"]        = 53310,
        ["D2gfx.dll"]       = 81982,    ["D2Glide.dll"]      = 94272,
        ["D2Lang.dll"]      = 77887,    ["D2Launch.dll"]     = 200769,
        ["D2MCPClient.dll"] = 49220,    ["D2Multi.dll"]      = 155712,
        ["D2Net.dll"]       = 53310,    ["D2sound.dll"]      = 86080,
        ["D2Win.dll"]       = 151614,   ["Diablo II.exe"]    = 36864,
        ["Fog.dll"]         = 188476,
        ["Patch_D2.mpq"]    = 1997247,  ["Storm.dll"]        = 266302,
    };

    // Game.exe is checked by VERSION (1.10), not size — different retail media
    // carry different but equally valid binaries.
    private const int GAME_EXE_MAJOR = 1, GAME_EXE_MINOR = 0,
                      GAME_EXE_BUILD = 10, GAME_EXE_REVISION = 39;

    private static bool IsGameExe110f(string path)
    {
        try
        {
            var v = System.Diagnostics.FileVersionInfo.GetVersionInfo(path);
            return v.FileMajorPart   == GAME_EXE_MAJOR
                && v.FileMinorPart   == GAME_EXE_MINOR
                && v.FileBuildPart   == GAME_EXE_BUILD
                && v.FilePrivatePart == GAME_EXE_REVISION;
        }
        catch { return false; }
    }

    // What the player's Diablo II actually IS, in words they can check against
    // their own main menu.
    //
    // Telling somebody "this is not 1.10f" and stopping there is the complaint
    // this answers: they cannot act on it without first working out what they
    // do have. Two shapes exist, and they are told apart by which executable
    // carries the engine:
    //
    //   1.13c and older  Game.exe holds the version (1.10f reads 1,0,10,39 --
    //                    the build field is the patch number)
    //   1.14 and newer   Game.exe is gone; Blizzard merged the engine into
    //                    Diablo II.exe, which is why none of the D2*.dll files
    //                    are found either
    //
    // Returns null only when neither executable is there, i.e. the folder is
    // not a Diablo II install at all -- a case the caller already reports.
    /// What the player has: how to say it, and how it compares to 1.10.
    /// Patch is 0 when we could not tell.
    public readonly record struct D2Version(string Text, int Patch);

    public static D2Version DetectD2Version(string folder)
    {
        if (string.IsNullOrEmpty(folder)) return new D2Version("", 0);

        string gameExe = Path.Combine(folder, "Game.exe");
        if (File.Exists(gameExe))
        {
            try
            {
                var v = System.Diagnostics.FileVersionInfo.GetVersionInfo(gameExe);
                if (v.FileMajorPart == 1 && v.FileBuildPart > 0)
                {
                    // 39 is the only revision we can name with certainty: it is
                    // the one this mod is built against and verified on.
                    string letter = (v.FileBuildPart == 10 && v.FilePrivatePart == 39) ? "f" : "";
                    // Diablo II writes these with the leading zero -- 1.09, not
                    // 1.9 -- and a player checking their own main menu against
                    // this should see the same thing they see there.
                    string num = v.FileBuildPart < 10 ? $"0{v.FileBuildPart}" : $"{v.FileBuildPart}";
                    return new D2Version($"1.{num}{letter}", v.FileBuildPart);
                }
            }
            catch { }
        }

        // No Game.exe at all. That layout only exists from 1.14, where Blizzard
        // merged the engine into Diablo II.exe -- which is also why none of the
        // D2*.dll files are there. The exact number matters less than the fact
        // that it is newer than 1.10, so treat the shape itself as the answer.
        string launcher = Path.Combine(folder, "Diablo II.exe");
        if (File.Exists(launcher))
        {
            try
            {
                // The 1.10 stub is 36 KB and reports a meaningless 1,0,0,2 --
                // only the merged executable is large enough to be the game.
                if (new FileInfo(launcher).Length > 1_000_000)
                {
                    var v = System.Diagnostics.FileVersionInfo.GetVersionInfo(launcher);
                    string s = (v.FileVersion ?? "").Replace(", ", ".").Trim();
                    return new D2Version(s.Length > 0 ? s : "1.14 or newer", 14);
                }
            }
            catch { }
        }
        return new D2Version("", 0);
    }

    public static string? DescribeD2Version(string folder)
    {
        var v = DetectD2Version(folder);
        return v.Patch == 0 ? null : v.Text;
    }

    // Blizzard's own 1.10 patch, and how to know you got the real one.
    private const string PatchUrl =
        "http://ftp.blizzard.com/pub/diablo2exp/patches/PC/LODPatch_110.exe";
    private const string PatchSha =
        "e32fd0298f24ac335563a00edf8af4fdfe95013f9eb640e4b05515c17faeb805";

    // The version problem, and -- this is the part that matters -- which way
    // they need to go.
    //
    // The two directions are not the same problem, and answering them with one
    // sentence helped nobody. Blizzard's patch contains DIFFERENCES against the
    // 1.09 files, not whole files, so:
    //
    //   too old (1.09)   one download away, and we can hand them the link
    //   too new (1.13+)  the patch has nothing to apply, because the 1.09 files
    //                    it needs are not there. No tool can fix that, and we
    //                    should not pretend otherwise.
    private static string WrongVersionMessage(string folder)
    {
        var v = DetectD2Version(folder);

        // Cannot tell: say so. Guessing a direction here would send somebody
        // shopping for discs they may not need, or patching something that
        // cannot take the patch.
        if (v.Patch == 0)
            return "That folder does not look like a Diablo II installation this "
                 + "mod can read a version from.\n\n"
                 + "Point the launcher at your Classic Diablo II + Lord of "
                 + "Destruction folder — the one holding d2data.mpq. This mod "
                 + "needs that install to be at patch 1.10f; the versions and "
                 + "how to get there are written up here:\n"
                 + "https://github.com/solida1987/Diablo-II-Archipelago/blob/main/DOWNGRADING.md";

        string head = $"Your Diablo II is version {v.Text}. This mod needs 1.10f.\n\n";

        if (v.Patch < 10)
            return head
              + "That is OLDER than this mod needs, which is the easy direction: "
              + "Blizzard's own 1.10 patch takes you straight there.\n\n"
              + "1. Download it from Blizzard:\n   " + PatchUrl + "\n"
              + "2. Check you got the real file — its SHA-256 is\n   " + PatchSha + "\n"
              + "3. Run it and point it at your Diablo II folder\n"
              + "4. The main menu should then read 1.10. Come back here and "
              + "install again.";

        return head
          + "That is NEWER than this mod needs, and there is no patch back down. "
          + "Blizzard's 1.10 patch holds differences measured against the 1.09 "
          + "files, not whole files — on 1.13c or 1.14 those originals are gone, "
          + "so it has nothing to apply. From 1.14 the engine DLLs are merged "
          + "into the main executable and simply do not exist any more.\n\n"
          + "The way to 1.10f is a fresh install from original Diablo II + Lord "
          + "of Destruction discs, which leaves you on 1.09, and then Blizzard's "
          + "1.10 patch. The full write-up is here:\n"
          + "https://github.com/solida1987/Diablo-II-Archipelago/blob/main/DOWNGRADING.md";
    }

    // True when the file is an engine file at the 1.10f patch level.
    private static bool Is110fFile(string path, string name)
    {
        if (name.Equals("Game.exe", StringComparison.OrdinalIgnoreCase))
            return File.Exists(path) && IsGameExe110f(path);
        if (!D2_110F_SIZES.TryGetValue(name, out long want)) return false;
        try { return new FileInfo(path).Length == want; }
        catch { return false; }
    }

    // Every file the launcher copies out of the player's own installation and
    // then verifies as 1.10f — the size-checked ones plus Game.exe.
    private static IEnumerable<string> EngineFiles()
        => D2_110F_SIZES.Keys.Concat(new[] { "Game.exe" });

    // User-editable config files: present-is-valid; never clobber even if size differs.
    private static readonly HashSet<string> USER_EDITABLE_FILES =
        new(StringComparer.OrdinalIgnoreCase)
    {
        "d2gl.ini", "D2Hackmap.ini", "d2sigma.ini",
        "d2gl_userkeys.ini", "keymap.ini", "d2arch.ini",
    };

    // Files written exclusively by the launcher after install — skip download
    // AND size-check so a stale manifest entry never resets them.
    private static readonly HashSet<string> LAUNCHER_MANAGED_FILES =
        new(StringComparer.OrdinalIgnoreCase)
    {
        "version.dat",
    };

    // Engine-regenerated caches: D2 recompiles the .txt tables into .bin at
    // startup, so .bin files are excluded from integrity checks.
    private static bool IsRegeneratedCache(string relPath)
    {
        string p = relPath.Replace('\\', '/');
        return p.StartsWith("data/global/excel/", StringComparison.OrdinalIgnoreCase)
            && p.EndsWith(".bin", StringComparison.OrdinalIgnoreCase);
    }

    // --- Original-game detection ---

    // True when the required original D2 data files are present in GameDirectory.
    // Used by the UI install flow to know when to show the "select D2 folder" picker.
    public bool HasOriginalGameFiles()
    {
        if (string.IsNullOrEmpty(GameDirectory)) return false;
        foreach (string f in new[] { "d2data.mpq", "d2char.mpq", "d2exp.mpq" })
            if (File.Exists(Path.Combine(GameDirectory, f))) return true;
        return false;
    }

    // Validate that a folder is the user's ORIGINAL Classic Diablo II + LoD
    // install (the copy source).
    // Rejects Diablo II: Resurrected (binary-incompatible) and an existing AP
    // install, and requires every Blizzard data file the mod needs.
    public string? ValidateExistingInstall(string folder)
    {
        if (string.IsNullOrEmpty(folder) || !Directory.Exists(folder))
            return "That folder does not exist.";

        if (File.Exists(Path.Combine(folder, "D2R.exe")) ||
            Directory.Exists(Path.Combine(folder, "ClassicMode")))
            return "That is Diablo II: Resurrected. Archipelago needs the CLASSIC " +
                   "Diablo II + Lord of Destruction install instead.";

        if (File.Exists(Path.Combine(folder, "D2Archipelago.dll")))
            return "That folder is an existing Archipelago install, not your original " +
                   "Diablo II. Point to the folder where you originally installed " +
                   "Classic Diablo II + LoD (the one containing d2data.mpq).";

        // The game data itself (MPQs and their helpers).
        var engine = new HashSet<string>(EngineFiles(), StringComparer.OrdinalIgnoreCase);
        var missingData = ORIGINAL_D2_FILES
            .Where(f => !engine.Contains(f))
            .Where(f => !File.Exists(Path.Combine(folder, f)))
            .ToList();
        if (missingData.Count > 0)
            return "That folder is missing required Diablo II files (" +
                   string.Join(", ", missingData) + "). Select your Classic Diablo II + " +
                   "Lord of Destruction install folder (typically " +
                   "C:\\Program Files (x86)\\Diablo II).";

        // The engine itself must be patch level 1.10f. Report this as one
        // version problem rather than a list of file names: on 1.14 the separate
        // DLLs do not exist at all, which would otherwise print twenty names and
        // explain nothing.
        var wrongVersion = EngineFiles()
            .Where(f => !Is110fFile(Path.Combine(folder, f), f))
            .ToList();
        if (wrongVersion.Count > 0)
            return WrongVersionMessage(folder);

        return null;
    }

    // A wrong-version Game.exe in the install folder is replaced from the player's original.
    public string? ValidateEngineVersion()
    {
        if (string.IsNullOrEmpty(GameDirectory)) return null;
        string gameExe = Path.Combine(GameDirectory, "Game.exe");
        if (!File.Exists(gameExe)) return null;   // existence is a separate check
        if (IsGameExe110f(gameExe)) return null;

        string found;
        try { found = System.Diagnostics.FileVersionInfo.GetVersionInfo(gameExe).FileVersion ?? "unknown"; }
        catch { return null; }                     // don't block launch on transient IO
        return
            $"The Game.exe in your install folder is version {found}, but this mod " +
            "requires Diablo II 1.10f.\n\n" +
            "This almost always means the mod was installed into an existing Diablo II " +
            "1.13c/1.14 folder, so a retail Game.exe is launched instead of the 1.10f one " +
            "— it crashes on start and can look like an antivirus block.\n\n" +
            "Fix: use the Play tab to repair / re-install, with the launcher pointed at a " +
            "Diablo II installation patched to 1.10f. Do not patch the install to 1.14.";
    }

    // ── L6 (T1): regenerate a character's map ────────────────────────────────
    
    // The "black wall" reports (Freedom in Arreat Plateau, Envy in Act 5) are a
    // map-generation fault bound to the character's stored map seed: single-player
    // reuses that seed forever, so relogging never helps, while hosting the game
    // over TCP/IP rolls a fresh one — which is exactly why that workaround fixed it.
    
    // This re-rolls the map seed in the .d2s so the player gets a brand new world
    // layout while keeping the character itself (level, items, quests) untouched.
    
    // Verified against a real save before shipping: the checksum routine below
    // reproduces the stored checksum of an existing .d2s byte-for-byte, so the
    // rewritten file is accepted by the game.
    private const int D2S_MAPSEED_OFFSET  = 0x00AB;   // 4 bytes, map/world seed
    private const int D2S_CHECKSUM_OFFSET = 0x000C;   // 4 bytes

    // D2's .d2s checksum: zero the checksum field, then for every byte
    // rotate the running total left by one and add the byte.
    private static uint ComputeD2sChecksum(byte[] data)
    {
        byte saved0 = data[D2S_CHECKSUM_OFFSET],     saved1 = data[D2S_CHECKSUM_OFFSET + 1];
        byte saved2 = data[D2S_CHECKSUM_OFFSET + 2], saved3 = data[D2S_CHECKSUM_OFFSET + 3];
        data[D2S_CHECKSUM_OFFSET] = data[D2S_CHECKSUM_OFFSET + 1] =
        data[D2S_CHECKSUM_OFFSET + 2] = data[D2S_CHECKSUM_OFFSET + 3] = 0;

        uint sum = 0;
        foreach (byte b in data)
        {
            uint carry = (sum & 0x80000000u) != 0 ? 1u : 0u;
            sum = ((sum << 1) | carry) + b;
        }

        data[D2S_CHECKSUM_OFFSET] = saved0; data[D2S_CHECKSUM_OFFSET + 1] = saved1;
        data[D2S_CHECKSUM_OFFSET + 2] = saved2; data[D2S_CHECKSUM_OFFSET + 3] = saved3;
        return sum;
    }

    // Re-roll the map seed of one character save.
    // human-readable reason why nothing was changed.
    // everything except the world layout.
    public string? RegenerateMapForSave(string d2sPath)
    {
        try
        {
            if (!File.Exists(d2sPath)) return "That character file no longer exists.";
            byte[] data = File.ReadAllBytes(d2sPath);
            if (data.Length < 0x150) return "That file is too small to be a Diablo II save.";
            if (!(data[0] == 0x55 && data[1] == 0xAA && data[2] == 0x55 && data[3] == 0xAA))
                return "That file is not a Diablo II save (bad signature).";

            // Refuse to touch a save whose checksum we cannot already reproduce —
            // that would mean our understanding of the format does not match this
            // file, and rewriting it could corrupt the character.
            uint stored = BitConverter.ToUInt32(data, D2S_CHECKSUM_OFFSET);
            if (ComputeD2sChecksum(data) != stored)
                return "This save's checksum does not match the expected format, so it "
                     + "was left untouched. Please report the character name.";

            string bak = d2sPath + "." + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".bak";
            File.Copy(d2sPath, bak, overwrite: false);

            uint newSeed = unchecked((uint)Guid.NewGuid().GetHashCode());
            if (newSeed == 0) newSeed = 0x5EED1234u;
            BitConverter.GetBytes(newSeed).CopyTo(data, D2S_MAPSEED_OFFSET);
            BitConverter.GetBytes(ComputeD2sChecksum(data)).CopyTo(data, D2S_CHECKSUM_OFFSET);

            string tmp = d2sPath + ".tmp";
            File.WriteAllBytes(tmp, data);
            File.Move(tmp, d2sPath, overwrite: true);
            return null;
        }
        catch (Exception ex)
        {
            return "Could not regenerate the map: " + ex.Message;
        }
    }

    // --- Iron Golem save rescue ---
    
    // A character saved with an Iron Golem can become permanently unloadable
    // ("bad hireable"); the repair strips the stored golem, with a backup.
    
    // The DLL now prevents new occurrences and transparently loads bricked
    // characters. This is the offline rescue for saves that predate that fix,
    // or for players running an older build: strip the stored golem.

    // True if this save carries a serialized Iron Golem (the payload that can
    // make it unloadable). False if there is none, or the file can't be read.
    public bool SaveHasIronGolem(string d2sPath)
    {
        try { return FindGolemFlagOffset(File.ReadAllBytes(d2sPath)) is int off && File.ReadAllBytes(d2sPath)[off] != 0; }
        catch { return false; }
    }

    // Locate the golem flag byte: the "kf" marker that follows the last "jf"
    // (mercenary item list) marker.
    // we expect — callers must then leave the file alone.
    private static int? FindGolemFlagOffset(byte[] d)
    {
        int lastJf = -1, lastKf = -1;
        for (int i = 0x150; i < d.Length - 2; i++)
        {
            if (d[i] == (byte)'j' && d[i + 1] == (byte)'f') lastJf = i;
            else if (d[i] == (byte)'k' && d[i + 1] == (byte)'f') lastKf = i;
        }
        if (lastKf < 0 || lastKf < lastJf) return null;   // no golem section at all
        int flag = lastKf + 2;
        if (flag >= d.Length) return null;                // truncated
        if (d[flag] == 0 && flag + 1 != d.Length) return null; // trailing junk: don't touch
        return flag;
    }

    // Remove a stored Iron Golem from a character save so the game will load it
    // again. Returns null on success, or a human-readable reason nothing changed.
    // Everything else about the character is preserved; a backup is taken first.
    public string? RepairIronGolemSave(string d2sPath)
    {
        try
        {
            if (!File.Exists(d2sPath)) return "That character file no longer exists.";
            byte[] data = File.ReadAllBytes(d2sPath);
            if (data.Length < 0x150) return "That file is too small to be a Diablo II save.";
            if (!(data[0] == 0x55 && data[1] == 0xAA && data[2] == 0x55 && data[3] == 0xAA))
                return "That file is not a Diablo II save (bad signature).";

            // Same safety rule as the map regenerator: if we cannot already
            // reproduce this file's checksum, our format assumptions do not hold
            // for it and rewriting could corrupt the character.
            uint stored = BitConverter.ToUInt32(data, D2S_CHECKSUM_OFFSET);
            if (ComputeD2sChecksum(data) != stored)
                return "This save's checksum does not match the expected format, so it "
                     + "was left untouched. Please report the character name.";

            if (FindGolemFlagOffset(data) is not int flag)
                return "Could not identify the golem section in this save, so nothing "
                     + "was changed. Please report the character name.";
            if (data[flag] == 0) return "This character has no stored Iron Golem — nothing to repair.";

            string bak = d2sPath + "." + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".golem.bak";
            File.Copy(d2sPath, bak, overwrite: false);

            // Clear the flag and drop everything after it (the golem item is the
            // final structure in the file), then fix size + checksum.
            byte[] fixedData = new byte[flag + 1];
            Array.Copy(data, fixedData, flag + 1);
            fixedData[flag] = 0;
            BitConverter.GetBytes((uint)fixedData.Length).CopyTo(fixedData, 0x0008);
            BitConverter.GetBytes(ComputeD2sChecksum(fixedData)).CopyTo(fixedData, D2S_CHECKSUM_OFFSET);

            string tmp = d2sPath + ".tmp";
            File.WriteAllBytes(tmp, fixedData);
            File.Move(tmp, d2sPath, overwrite: true);
            return null;
        }
        catch (Exception ex)
        {
            return "Could not repair the save: " + ex.Message;
        }
    }

    // Copy the original Blizzard data files (the copyrighted MPQs etc.) from the
    // user's own Diablo II install (OriginalD2Directory) into the mod install
    // dir (GameDirectory). These are NEVER downloaded from GitHub. Returns the
    // list of files that could not be copied (empty = all present).
    private List<string> CopyOriginalD2Files()
    {
        var missing = new List<string>();
        bool haveSource = !string.IsNullOrEmpty(OriginalD2Directory)
                          && Directory.Exists(OriginalD2Directory);

        Directory.CreateDirectory(GameDirectory);
        foreach (string file in ORIGINAL_D2_FILES)
        {
            string dst = Path.Combine(GameDirectory, file);

            bool isEngine = D2_110F_SIZES.ContainsKey(file)
                         || file.Equals("Game.exe", StringComparison.OrdinalIgnoreCase);

            // Already in place and correct: leave it alone. This is what makes
            // the update path safe — an engine file that is already the right
            // 1.10f build must never be replaced from a source folder that may
            // have been patched to a newer version since installation.
            if (File.Exists(dst) && (!isEngine || Is110fFile(dst, file)))
                continue;

            if (!haveSource) { missing.Add(file); continue; }

            string src = Path.Combine(OriginalD2Directory, file);
            if (!File.Exists(src)) { missing.Add(file); continue; }

            // An engine file at the wrong patch level would produce an install
            // that starts and then crashes, so refuse it rather than copy it.
            if (isEngine && !Is110fFile(src, file))
            {
                missing.Add(file);
                continue;
            }

            try { File.Copy(src, dst, overwrite: true); }
            catch { missing.Add(file); }
        }
        return missing;
    }

    // Best-effort auto-detection of the user's Classic Diablo II install via the
    // registry (Blizzard / GOG / Uninstall keys) and common paths.
    // first folder that passes ValidateExistingInstall, or null if none found.
    public string? AutoDetectOriginalD2()
    {
        var candidates = new List<string>();
        void TryReg(RegistryKey root, string sub, string val)
        {
            try
            {
                using var key = root.OpenSubKey(sub);
                if (key?.GetValue(val) is string p && !string.IsNullOrWhiteSpace(p))
                    candidates.Add(p.TrimEnd('\\', '/'));
            }
            catch { }
        }
        TryReg(Registry.CurrentUser,  @"Software\Blizzard Entertainment\Diablo II", "InstallPath");
        TryReg(Registry.LocalMachine, @"SOFTWARE\Blizzard Entertainment\Diablo II", "InstallPath");
        TryReg(Registry.LocalMachine, @"SOFTWARE\WOW6432Node\Blizzard Entertainment\Diablo II", "InstallPath");
        TryReg(Registry.LocalMachine, @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Diablo II", "InstallLocation");
        TryReg(Registry.LocalMachine, @"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Diablo II", "InstallLocation");
        TryReg(Registry.LocalMachine, @"SOFTWARE\GOG.com\Games\1435828550", "path");
        TryReg(Registry.LocalMachine, @"SOFTWARE\WOW6432Node\GOG.com\Games\1435828550", "path");
        candidates.Add(@"C:\Program Files (x86)\Diablo II");
        candidates.Add(@"C:\Program Files\Diablo II");
        candidates.Add(@"C:\GOG Games\Diablo II");
        candidates.Add(@"C:\Games\Diablo II");
        candidates.Add(@"D:\Diablo II");

        foreach (string c in candidates)
            if (ValidateExistingInstall(c) == null) return c;
        return null;
    }

    // --- Constructor ---

    public D2Plugin()
    {
        _http.DefaultRequestHeaders.UserAgent.TryParseAdd("D2Arch-Launcher/2.0");
        // AP item resume index is loaded by the GameDirectory setter — the
        // directory is not known yet at construction time.
    }

    // --- Version check ---

    // Resolve the latest release tag via the releases/latest redirect.
    // Does NOT use the GitHub REST API — immune to the 60 req/hour rate limit.
    private async Task<string?> FetchLatestTagAsync(CancellationToken ct)
    {
        if (_cachedLatestTag != null) return _cachedLatestTag;
        try
        {
            // GitHub redirects /releases/latest → /releases/tag/<tag> (HTTP 302).
            // AllowAutoRedirect=false lets us read the Location header directly.
            using var req = new HttpRequestMessage(HttpMethod.Head, GH_LATEST_PAGE);
            using var handler = new HttpClientHandler { AllowAutoRedirect = false };
            using var client  = new HttpClient(handler, disposeHandler: true);
            client.DefaultRequestHeaders.UserAgent.TryParseAdd("Archipelago-Launcher/2.0");
            client.Timeout = TimeSpan.FromSeconds(15);
            using var resp = await client.SendAsync(req, ct);

            string? location = resp.Headers.Location?.ToString();
            if (string.IsNullOrEmpty(location)) return null;

            // Extract tag from ".../releases/tag/<tag>"
            const string marker = "/releases/tag/";
            int idx = location.IndexOf(marker, StringComparison.Ordinal);
            if (idx < 0) return null;

            _cachedLatestTag = location[(idx + marker.Length)..].TrimEnd('/');
            return _cachedLatestTag;
        }
        catch { return null; }
    }

    // Build a direct CDN download URL for a known asset filename.
    // Instance method (was static) — GH_DOWNLOAD_BASE is now channel-aware.
    private string DownloadUrl(string tag, string filename)
        => $"{GH_DOWNLOAD_BASE}/{tag}/{filename}";

    public async Task CheckForUpdateAsync(CancellationToken ct = default)
    {
        try
        {
            string versionDat = Path.Combine(GameDirectory, "Archipelago", "version.dat");
            InstalledVersion = File.Exists(versionDat)
                ? File.ReadAllText(versionDat).Trim()
                : null;

            AvailableVersion = await FetchLatestTagAsync(ct);
        }
        catch
        {
            AvailableVersion = null;
        }
    }

    // --- Install / update ---

    // Full install or incremental update.
    // • Fresh install: downloads game_package.zip from the GitHub release.
    // • Update: downloads only changed files via game_manifest.json.
    public async Task InstallOrUpdateAsync(
        IProgress<(int Pct, string Msg)> progress,
        CancellationToken ct = default)
    {
        progress.Report((0, "Fetching release info..."));

        string? tag = await FetchLatestTagAsync(ct)
            ?? throw new InvalidOperationException(
                "Could not reach GitHub: check your internet connection and try again.");

        // Build direct CDN download URLs — no REST API call needed.
        string packageUrl  = DownloadUrl(tag, "game_package.zip");
        string manifestUrl = DownloadUrl(tag, "game_manifest.json");
        string apworldUrl  = DownloadUrl(tag, "diablo2_archipelago.apworld");

        if (!IsInstalled)
        {
            // Fresh install: full ZIP download + extract.
            await InstallFromZipAsync(packageUrl, apworldUrl, tag, progress, ct);
        }
        else
        {
            // Update: manifest tells us WHAT changed; the release ZIP provides the
            // bytes (same reliable source as a fresh install).
            await InstallFromManifestAsync(manifestUrl, packageUrl, apworldUrl, tag, progress, ct);
        }

        // The per-seed randomizer works from a "pristine" snapshot of the data
        // files, so re-randomising never compounds earlier edits.
        D2DataFiles.InvalidateBackup(GameDirectory);

        RemoveRetiredFiles();
    }

    // Files the mod used to ship and must NOT be left behind on update.
    
    // The updater unpacks over the existing install, so a file that stops
    // being shipped simply stays there forever.
    // harmless; for an engine dll inside patch\ it is not — D2.Detours loads
    // every dll in that folder and redirects the game's exports into it.
    
    // patch\D2Common.dll was the D2MOO D2Common rebuild.
    // of D2Common's exported surface, including the two functions the regular
    // monster-spawn path depends on (and that champion/boss placement does
    // not) — the cause of zones that held only champions, superuniques and
    // traps. Removed in game version 3.2.7 / EX 1.0.50 after an A/B test.
    private static readonly string[] RetiredFiles =
    {
        Path.Combine("patch", "D2Common.dll"),
    };

    private void RemoveRetiredFiles()
    {
        foreach (string rel in RetiredFiles)
        {
            try
            {
                string full = Path.Combine(GameDirectory, rel);
                if (!File.Exists(full)) continue;
                File.SetAttributes(full, FileAttributes.Normal);
                File.Delete(full);
                Debug.WriteLine($"[D2] Removed retired file: {rel}");
            }
            catch (Exception ex)
            {
                // The bootstrap deletes it again before every launch, so a
                // locked file here is a delay, not a failure.
                Debug.WriteLine($"[D2] Could not remove {rel}: {ex.Message}");
            }
        }
    }

    // --- Repair (antivirus deletes D2Arch_Launcher.exe) ---

    // Mod binaries that MUST exist for the game to launch.
    // Windows Defender — frequently quarantines D2Arch_Launcher.exe (the 32-bit
    // injector) as a false positive; the Repair flow restores exactly these.
    private static readonly string[] CriticalModFiles =
    {
        "D2Arch_Launcher.exe",
        "D2Archipelago.dll",
        @"patch\D2Archipelago.dll",
    };

    // Critical mod files currently missing from the install (paths relative to
    // GameDirectory). Empty list = nothing to repair. Used by the launch-time
    // Repair prompt so a quarantined file is a one-click fix, not a dead end.
    private List<string> GetMissingCriticalFilesCore()
    {
        var missing = new List<string>();
        if (!Directory.Exists(GameDirectory)) return missing;   // not installed → not a "repair" case
        foreach (string rel in CriticalModFiles)
            if (!File.Exists(Path.Combine(GameDirectory, rel)))
                missing.Add(rel);
        return missing;
    }

    // Re-download the mod package and restore ONLY the missing critical files,
    // without touching the rest of the install.
    public async Task<int> RepairMissingFilesAsync(
        IProgress<(int Pct, string Msg)> progress, CancellationToken ct = default)
    {
        var missing = GetMissingCriticalFiles();
        if (missing.Count == 0) return 0;

        progress.Report((0, "Checking release..."));
        string? tag = await FetchLatestTagAsync(ct)
            ?? throw new InvalidOperationException(
                "Could not reach GitHub to download the missing files — check your internet connection.");
        string packageUrl = DownloadUrl(tag, "game_package.zip");

        string tempZip = Path.Combine(Path.GetTempPath(), "d2arch_repair.zip");
        try
        {
            progress.Report((5, "Downloading mod files..."));
            await DownloadZipWithRetryAsync(packageUrl, tempZip, 5, 85, progress, ct);
            ct.ThrowIfCancellationRequested();

            progress.Report((88, "Restoring missing files..."));
            var wanted = new HashSet<string>(
                missing.Select(m => m.Replace('\\', '/')), StringComparer.OrdinalIgnoreCase);

            int restored = 0;
            using var zip = ZipFile.OpenRead(tempZip);
            foreach (var entry in zip.Entries)
            {
                if (string.IsNullOrEmpty(entry.Name)) continue;
                if (!wanted.Contains(entry.FullName.Replace('\\', '/'))) continue;
                string dest = Path.Combine(GameDirectory, entry.FullName.Replace('/', '\\'));
                string? dir = Path.GetDirectoryName(dest);
                if (dir != null) Directory.CreateDirectory(dir);
                entry.ExtractToFile(dest, overwrite: true);
                restored++;
            }
            progress.Report((100, $"Restored {restored} file(s)."));
            return restored;
        }
        finally { try { File.Delete(tempZip); } catch { /* ignore */ } }
    }

    // Add the install folder to Windows Defender's exclusion list so it stops
    // flagging D2Arch_Launcher.exe as a false positive (the mod injects into D2,
    // which Defender treats as suspicious).
    // PowerShell (UAC prompt).
    // Returns false if the user declined UAC or Defender cmdlets aren't available
    // (e.g. a third-party antivirus is the active provider).
    public bool AddDefenderExclusion()
    {
        try
        {
            string psPath = GameDirectory.Replace("'", "''");
            var psi = new ProcessStartInfo
            {
                FileName  = "powershell.exe",
                Arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command " +
                            $"\"Add-MpPreference -ExclusionPath '{psPath}'; " +
                            "Add-MpPreference -ExclusionProcess 'D2Arch_Launcher.exe'\"",
                UseShellExecute = true,   // required for Verb=runas
                Verb            = "runas",
                CreateNoWindow  = true,
                WindowStyle     = ProcessWindowStyle.Hidden,
            };
            using var p = Process.Start(psi);
            if (p == null) return false;
            p.WaitForExit(30000);
            return p.HasExited && p.ExitCode == 0;
        }
        catch
        {
            return false;
        }
    }

    private async Task InstallFromZipAsync(
        string packageUrl, string? apworldUrl, string? actualTag,
        IProgress<(int, string)> progress, CancellationToken ct)
    {
        string tempZip = Path.Combine(Path.GetTempPath(), "d2arch_install.zip");
        try
        {
            progress.Report((5, "Downloading game package..."));
            await DownloadZipWithRetryAsync(packageUrl, tempZip, 5, 80, progress, ct);

            // A cancelled install must THROW, not return — a silent return made
            // the caller report "Installation complete!" over a partial extract
            // and stamp version.dat below (P2-3).
            ct.ThrowIfCancellationRequested();

            progress.Report((82, "Extracting game files..."));
            Directory.CreateDirectory(GameDirectory);

            using var zip = ZipFile.OpenRead(tempZip);
            int total = zip.Entries.Count, done = 0;
            foreach (var entry in zip.Entries)
            {
                ct.ThrowIfCancellationRequested();
                if (string.IsNullOrEmpty(entry.Name)) continue;

                string dest = Path.Combine(GameDirectory, entry.FullName.Replace('/', '\\'));
                string? dir = Path.GetDirectoryName(dest);
                if (dir != null) Directory.CreateDirectory(dir);
                entry.ExtractToFile(dest, overwrite: true);

                done++;
                if (done % 20 == 0)
                    progress.Report((82 + done * 15 / total, $"Extracting... ({done}/{total})"));
            }

            if (apworldUrl != null)
            {
                progress.Report((97, "Downloading AP World..."));
                string apworldDir = Path.Combine(GameDirectory, "apworld");
                Directory.CreateDirectory(apworldDir);
                await DownloadSimpleAsync(apworldUrl,
                    Path.Combine(apworldDir, "diablo2_archipelago.apworld"), ct);
            }

            progress.Report((98, "Cleaning data cache..."));
            DeleteBinCache();
            Directory.CreateDirectory(Path.Combine(GameDirectory, "save"));

            // Copy the copyrighted Blizzard data files from the player's own
            // Diablo II install into the mod folder — these are never downloaded.
            // The game cannot run without them, so a failure aborts BEFORE
            // version.dat is written (IsInstalled stays false so a retry works).
            progress.Report((99, "Copying original Diablo II files..."));
            var missingOriginals = CopyOriginalD2Files();
            if (missingOriginals.Count > 0)
            {
                var engineSet = new HashSet<string>(EngineFiles(), StringComparer.OrdinalIgnoreCase);
                bool engineProblem = missingOriginals.Any(engineSet.Contains);

                // A wrong version is not "some files are missing". Listing
                // twenty DLL names to somebody on 1.14 -- where those files
                // genuinely do not exist -- reads as a broken download and
                // sends them looking in the wrong place entirely.
                throw new InvalidOperationException(
                    engineProblem
                        ? WrongVersionMessage(OriginalD2Directory ?? "")
                        : "Could not copy these original Diablo II files from your "
                        + "installation: " + string.Join(", ", missingOriginals) + ".\n\n"
                        + "Open Settings and point the launcher at your Classic Diablo II "
                        + "+ Lord of Destruction folder (the one containing d2data.mpq), "
                        + "then install again.");
            }

            // The copies above are the player's own untouched libraries, and
            // Diablo II will not load a modded install as they stand. Apply the
            // mod's byte edits to them here; the game cannot start without it.
            progress.Report((99, "Applying engine patches..."));
            var patchProblems = D2EnginePatch.Apply(GameDirectory);
            if (patchProblems.Count > 0)
                throw new InvalidOperationException(
                    "Could not apply the engine patches:\n\n" +
                    string.Join("\n", patchProblems) +
                    "\n\nThe mod cannot run without them. This normally means the " +
                    "Diablo II installation the files came from is not 1.10f.");

            progress.Report((99, "Writing version..."));
            WriteVersionDat(actualTag);

            progress.Report((100, "Installation complete!"));
        }
        finally
        {
            try { if (File.Exists(tempZip)) File.Delete(tempZip); } catch { }
        }
    }

    private async Task InstallFromManifestAsync(
        string manifestUrl, string packageUrl, string? apworldUrl, string? actualTagHint,
        IProgress<(int, string)> progress, CancellationToken ct)
    {
        progress.Report((5, "Downloading manifest..."));
        string manifestJson = await _http.GetStringAsync(manifestUrl, ct);

        // Cache manifest locally for offline verify
        try
        {
            Directory.CreateDirectory(GameDirectory);
            File.WriteAllText(Path.Combine(GameDirectory, "game_manifest.json"), manifestJson);
        }
        catch { }

        // Extract the version tag from the manifest (more reliable than release tag)
        string? manifestVersion = actualTagHint;
        JsonFindString(manifestJson, "version", ref manifestVersion);

        // NOTE (P2-3): version.dat is NOT stamped up front any more — an early
        // stamp made a failed/cancelled update report the new version forever
        // ("up to date" over stale files).
        // exit: the 0-files-to-download path below (which preserves the V1
        // "stuck at Beta 1.9.4" self-heal) and the all-downloads-OK path.

        using var doc = JsonDocument.Parse(manifestJson);
        var root = doc.RootElement;

        // Build download list and manifest path set (for orphan cleanup)
        var toDownload = new List<(string path, string sha256, long size)>();
        var manifestPaths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        if (root.TryGetProperty("files", out var files))
        {
            progress.Report((10, "Comparing local files..."));
            int total = files.GetArrayLength(), checked_ = 0;

            foreach (var entry in files.EnumerateArray())
            {
                ct.ThrowIfCancellationRequested();

                string path   = entry.GetProperty("path").GetString() ?? "";
                string sha256 = entry.TryGetProperty("sha256", out var s) ? s.GetString() ?? "" : "";
                long   size   = entry.TryGetProperty("size",   out var z) ? z.GetInt64()        : 0;

                if (path.Length > 0)
                    manifestPaths.Add(path.Replace('\\', '/'));

                string fileName = Path.GetFileName(path);
                if (ORIGINAL_D2_FILES.Contains(fileName))      goto next;
                if (LAUNCHER_MANAGED_FILES.Contains(fileName)) goto next;
                if (IsRegeneratedCache(path))                  goto next;

                string local = Path.Combine(GameDirectory, path.Replace('/', '\\'));
                bool needsDownload = true;
                if (File.Exists(local) && sha256.Length > 0 &&
                    ComputeSha256(local).Equals(sha256, StringComparison.OrdinalIgnoreCase))
                {
                    needsDownload = false;
                }
                if (needsDownload) toDownload.Add((path, sha256, size));

                next:
                checked_++;
                if (checked_ % 10 == 0)
                    progress.Report((10 + checked_ * 20 / total,
                        $"Checking files... ({checked_}/{total})"));
            }
        }

        if (toDownload.Count == 0)
        {
            // Self-heal: even with nothing to download the stamp is refreshed,
            // so a stale version.dat on an otherwise-correct install recovers
            // (the V1 "stuck at Beta 1.9.4" lesson — both exit paths stamp).
            WriteVersionDat(manifestVersion);
            progress.Report((100, "All files up to date!"));
            return;
        }

        // Updates apply from the release ZIP — the same source a fresh install uses.
        progress.Report((30, $"Downloading update ({toDownload.Count} changed file(s))..."));

        string tempZip = Path.Combine(Path.GetTempPath(), "d2arch_update.zip");
        int updated = 0, orphans = 0;
        try
        {
            await DownloadZipWithRetryAsync(packageUrl, tempZip, 30, 85, progress, ct);
            ct.ThrowIfCancellationRequested();

            var wanted = new HashSet<string>(
                toDownload.Select(t => t.path.Replace('\\', '/')),
                StringComparer.OrdinalIgnoreCase);

            progress.Report((86, "Applying update..."));
            using var zip = ZipFile.OpenRead(tempZip);
            foreach (var entry in zip.Entries)
            {
                ct.ThrowIfCancellationRequested();
                if (string.IsNullOrEmpty(entry.Name)) continue;

                string rel = entry.FullName.Replace('\\', '/');
                if (!wanted.Contains(rel)) continue;   // unchanged — leave it

                string dest = Path.Combine(GameDirectory, rel.Replace('/', '\\'));
                string? dir = Path.GetDirectoryName(dest);
                if (dir != null) Directory.CreateDirectory(dir);
                entry.ExtractToFile(dest, overwrite: true);
                updated++;

                if (updated % 20 == 0)
                    progress.Report((86 + updated * 8 / Math.Max(1, wanted.Count),
                        $"Applying update... ({updated}/{wanted.Count})"));
            }
        }
        finally
        {
            try { if (File.Exists(tempZip)) File.Delete(tempZip); } catch { }
        }

        DeleteBinCache();

        // Remove orphan files (present locally but removed from the manifest).
        orphans = DeleteOrphans(manifestPaths);

        if (apworldUrl != null)
        {
            progress.Report((96, "Downloading AP World..."));
            try
            {
                string apworldDir = Path.Combine(GameDirectory, "apworld");
                Directory.CreateDirectory(apworldDir);
                await DownloadSimpleAsync(apworldUrl,
                    Path.Combine(apworldDir, "diablo2_archipelago.apworld"), ct);
            }
            catch { /* non-fatal — apworld update failure doesn't block the game */ }
        }

        // Heal: ensure the original Blizzard data files are present (an update to
        // an existing install should already have them; best-effort re-copy here).
        CopyOriginalD2Files();

        // …and that the engine patches are still in place. A file that was
        // re-copied from the player's installation comes back unpatched, and the
        // game refuses to start without them.
        D2EnginePatch.Apply(GameDirectory);

        // Only now that the ZIP extracted cleanly do we stamp the new version —
        // a failed ZIP download throws above, so a broken update never stamps
        // (the next check still offers the update).
        WriteVersionDat(manifestVersion);

        string msg = $"Updated {updated} file(s).";
        if (orphans > 0) msg += $" Removed {orphans} stale file(s).";

        progress.Report((100, msg));
    }

    public async Task<bool> VerifyInstallAsync(CancellationToken ct = default)
    {
        // Thin wrapper over the detailed scan so the bool gate and the per-file
        // report can never diverge.
        var problems = await ScanInstallProblemsAsync(ct);
        return problems == null || problems.Count == 0;
    }

    // One install-integrity problem: a manifest-listed file that is missing or the
    // wrong size, with a human-readable reason.
    public readonly record struct InstallProblem(string RelPath, string Reason);

    // Detailed integrity scan.
    // exclusion sets the fast verify uses) that is missing or the wrong size, each
    // with a reason — so the launcher can tell the user EXACTLY what is wrong
    // instead of a vague "some files may be missing".
    // the install is healthy, and null when integrity cannot be determined
    // (offline + no cached manifest); callers MUST NOT treat null as a failure.
    // Fast size-only check (SHA256 over ~355 files was 5-15 s in V1 — rejected).
    public async Task<List<InstallProblem>?> ScanInstallProblemsAsync(CancellationToken ct = default)
    {
        string manifestPath = Path.Combine(GameDirectory, "game_manifest.json");
        string? manifestJson = null;

        if (File.Exists(manifestPath))
        {
            try { manifestJson = await File.ReadAllTextAsync(manifestPath, ct); }
            catch { }
        }

        if (manifestJson == null)
        {
            // Fetch from GitHub if no local copy — uses CDN redirect, no API quota.
            try
            {
                string? tag = await FetchLatestTagAsync(ct);
                if (tag != null)
                {
                    string mUrl = DownloadUrl(tag, "game_manifest.json");
                    manifestJson = await _http.GetStringAsync(mUrl, ct);
                    try { await File.WriteAllTextAsync(manifestPath, manifestJson, ct); } catch { }
                }
            }
            catch { return null; } // offline — cannot determine
        }

        if (manifestJson == null) return null;

        var problems = new List<InstallProblem>();
        try
        {
            using var doc = JsonDocument.Parse(manifestJson);
            if (!doc.RootElement.TryGetProperty("files", out var files)) return problems;

            foreach (var entry in files.EnumerateArray())
            {
                ct.ThrowIfCancellationRequested();

                string path = entry.GetProperty("path").GetString() ?? "";
                if (path.Length == 0) continue;

                string fileName = Path.GetFileName(path);
                if (ORIGINAL_D2_FILES.Contains(fileName))      continue;
                if (LAUNCHER_MANAGED_FILES.Contains(fileName)) continue;
                if (IsRegeneratedCache(path))                  continue;

                string localPath = Path.Combine(GameDirectory, path.Replace('/', '\\'));
                if (!File.Exists(localPath)) { problems.Add(new InstallProblem(path, "missing")); continue; }

                if (USER_EDITABLE_FILES.Contains(fileName)) continue; // present = valid

                long expectedSize = 0;
                if (entry.TryGetProperty("size", out var sizeEl)) expectedSize = sizeEl.GetInt64();
                if (expectedSize > 0)
                {
                    long actual = new FileInfo(localPath).Length;
                    if (actual != expectedSize)
                        problems.Add(new InstallProblem(
                            path, $"wrong size: {actual:n0} bytes, expected {expectedSize:n0}"));
                }
            }
        }
        catch (OperationCanceledException) { throw; }
        catch { return null; } // malformed manifest — don't block launch
        return problems;
    }

    // Repair = re-download the release package, extract only the named files.
    public async Task<(List<string> Restored, List<string> NotInPackage)> RepairFilesAsync(
        IEnumerable<string> relPaths,
        IProgress<(int, string)> progress,
        CancellationToken ct = default)
    {
        var wanted = new HashSet<string>(
            relPaths.Select(p => p.Replace('\\', '/')), StringComparer.OrdinalIgnoreCase);
        var restored = new List<string>();
        if (wanted.Count == 0) return (restored, new List<string>());

        string? tag = await FetchLatestTagAsync(ct)
            ?? throw new InvalidOperationException(
                "Could not reach the download server — check your internet connection.");
        string packageUrl = DownloadUrl(tag, "game_package.zip");

        string tempZip = Path.Combine(Path.GetTempPath(), "d2arch_verify_repair.zip");
        try
        {
            await DownloadZipWithRetryAsync(packageUrl, tempZip, 0, 90, progress, ct);
            ct.ThrowIfCancellationRequested();

            using var zip = ZipFile.OpenRead(tempZip);
            foreach (var entry in zip.Entries)
            {
                if (string.IsNullOrEmpty(entry.Name)) continue;
                string norm = entry.FullName.Replace('\\', '/');
                if (!wanted.Contains(norm)) continue;
                string dest = Path.Combine(GameDirectory, entry.FullName.Replace('/', '\\'));
                string? dir = Path.GetDirectoryName(dest);
                if (dir != null) Directory.CreateDirectory(dir);
                entry.ExtractToFile(dest, overwrite: true);
                restored.Add(norm);
                wanted.Remove(norm);
            }
            progress.Report((100, $"Restored {restored.Count} file(s)."));
        }
        finally { try { File.Delete(tempZip); } catch { } }

        // Repaired managed excel tables must also refresh their pristine snapshots.
        foreach (string r in restored)
            if (r.StartsWith("data/global/excel/", StringComparison.OrdinalIgnoreCase))
                D2DataFiles.RefreshBackupFile(GameDirectory, Path.GetFileName(r));

        return (restored, wanted.ToList()); // leftover = not present in the package
    }

    // --- Launch ---

    public async Task LaunchAsync(ApSession session, CancellationToken ct = default)
    {
        // verify the live folder holds the mod's 1.10f engine before doing
        // anything else. A retail 1.14 Game.exe would crash on inject and feed the
        // Defender loop, so abort early with a targeted message.
        {
            string? engineErr = ValidateEngineVersion();
            if (engineErr != null) throw new InvalidOperationException(engineErr);
        }

        // Character isolation: every AP slot gets its own save tree under
        // Save\seed_<key>\, so slots never share a character list.
        long   apWorldKey = StableApSeed(session);
        string apSaveDir  = ResolveApSaveFolder(apWorldKey, session);
        PinSavePathRegistry(apSaveDir);

        // Defensive teardown: any pipe left over from a previous session must
        // be fully gone before a new server stream is created.
        DisposePipe();
        _sessionIsAp = true;   // this session may receive AP items/STATE pushes

        // Fresh session = fresh item stream.
        // server's full-history replay reaches THIS session's character(s);
        // the DLL's per-char dedup eats any true duplicates.
        _apItemIndex = 0;
        _apIndexSeedKey = null;

        // 1. Write AP credentials + the pipe name to d2arch.ini so the DLL can
        // read them. The DLL discovers the pipe via [ap] PipeName=<name>
        // (bare name, no \\.\pipe\ prefix — the DLL adds that itself).
        // Per-launch random suffix: even if an old server stream lingers
        // (GC-pending after a crash), the new launch never collides with it.
        string pipeName =
            $"d2arch_v2_{Environment.ProcessId}_{Guid.NewGuid().ToString("N")[..8]}";
        WriteApCredentials(session, pipeName, apWorldKey);

        // AP parity: apply the same seed-bound txt randomization the standalone
        // path uses (monster / super-unique / shop shuffle + skill/item reqs), driven
        // by the AP slot_data + a stable per-world seed, with the same progress bar.
        // The DLL then skips its own monster/super-unique shuffle (LauncherDataShuffle)
        // but still does the act-boss cosmetic swap.
        // shuffle) if slot_data isn't available yet.
        await ApplyApDataTablesAsync(session, apWorldKey);

        // 2. Start named pipe server BEFORE launching Game.exe so the DLL
        // can connect immediately after injection.
        // client reads a byte stream and frames messages by '\n', so the
        // server must NOT use Message transmission mode.
        var pipe = new NamedPipeServerStream(pipeName, PipeDirection.InOut,
                       maxNumberOfServerInstances: 1,
                       transmissionMode: PipeTransmissionMode.Byte,
                       options: PipeOptions.Asynchronous);
        _pipe    = pipe;
        _pipeCts = new CancellationTokenSource();

        // 3. Launch Game.exe through the 32-bit bootstrap (D2Arch_Launcher.exe),
        // which sets DIABLO2_PATCH (so D2.Detours loads the patched DLLs),
        // launches the game via D2.Detours, and injects D2Archipelago.dll.
        // The 64-bit launcher cannot inject into the 32-bit game itself.
        // D2ARCH_AP_MODE keeps ap_settings.dat alive for the slot-data
        // hand-off (the DLL scrubs it at startup otherwise).
        int launchCompleted = 0;   // 1 once the pipe handshake succeeded
        int exitRaised      = 0;   // GameExited must fire at most once
        var deathCts        = new CancellationTokenSource();
        Process? proc       = null;

        void RaiseExited()
        {
            if (Interlocked.Exchange(ref exitRaised, 1) == 1) return;
            IsRunning = false;
            ScrubApPassword();   // session over — blank the ini password (P3-20)
            if (_apAppliedDataTables)
            {
                _apAppliedDataTables = false;
                // Reset the seed-patched tables (with the "Nulstiller" bar), same as
                // the standalone exit handler — never leave the install patched.
                D2RandomizeProgress.RunRestoreWithProgress(GameDirectory);
            }
            int code = 0;
            try { code = proc?.ExitCode ?? 0; } catch { /* handle already gone */ }
            GameExited?.Invoke(code);
        }

        try
        {
            // Bootstrap inject (up to ~60 s) + pipe handshake, bounded by one
            // linked token: caller cancel + plugin teardown + process death +
            // a 90 s overall ceiling.
            using var launchCts = CancellationTokenSource.CreateLinkedTokenSource(
                ct, _pipeCts.Token, deathCts.Token);
            launchCts.CancelAfter(TimeSpan.FromSeconds(90));

            proc = await RunBootstrapAndFindGameAsync(apMode: true, launchCts.Token, apSaveDir);
            _gameProcess = proc;
            proc.EnableRaisingEvents = true;
            proc.Exited += (_, _) =>
            {
                IsRunning = false;
                try { deathCts.Cancel(); } catch (ObjectDisposedException) { }
                if (Volatile.Read(ref launchCompleted) == 1)
                    RaiseExited();
            };

            // Game.exe is modded + running; wait for the DLL to open our pipe.
            await pipe.WaitForConnectionAsync(launchCts.Token);
            // The Map tab's buttons follow the session, not whatever was true
            // when the tab happened to be opened.
            RefreshMapActionContext();
        }
        catch (Exception ex)
        {
            // The launch is dead either way — never leave a half-modded
            // Game.exe running behind a failed bridge.
            try { proc?.Kill(entireProcessTree: true); } catch { }
            DisposePipe();
            IsRunning = false;

            if (ct.IsCancellationRequested)
                throw new OperationCanceledException("Game launch was cancelled.", ct);

            if (ex is OperationCanceledException)
            {
                if (deathCts.IsCancellationRequested)
                    throw new InvalidOperationException(
                        "The game closed during startup before the mod could connect. " +
                        "Check your antivirus — add D2Archipelago.dll, Game.exe and the " +
                        "launcher to its exclusion list, then try again.");
                throw new InvalidOperationException(
                    "The game started but the mod never connected to the launcher. " +
                    "This usually means antivirus blocked the mod — add D2Archipelago.dll " +
                    "and the launcher to your antivirus exclusions, then try again.");
            }
            throw;   // bootstrap / injection errors already carry actionable text
        }
        finally
        {
            deathCts.Dispose();
        }

        IsRunning = true;
        Volatile.Write(ref launchCompleted, 1);

        // Closes the race where the process died between the pipe handshake
        // and the flag write above (the Exited handler saw launchCompleted=0).
        if (proc is { HasExited: true }) RaiseExited();

        // Push the current AP state immediately: if the AP server connected
        // before the game was up, the DLL would otherwise never receive a
        // STATE: message (OnApStateChanged only fires on transitions).
        if (_lastApState == ApConnectionState.Connected)
        {
            await WritePipeLineAsync("STATE:CONNECTED", ct);

            // The game just attached.
            // still in the launcher (notably the precollected STARTING SKILLS at
            // index 0) were dropped by ReceiveItemsAsync because the pipe wasn't
            // connected yet. Now that it is, ask the server to resend the full
            // item stream from index 0 so those items reach the DLL.
            
            // Fire the resync from a BACKGROUND task with a readiness delay, NOT
            // inline: the DLL needs a moment after STATE:CONNECTED to stand up its
            // AP subsystem + skill tree.
            // that startup and only some precollected skills get applied (testers
            // saw 3/6, 5/6). We re-sync a few times at increasing delays so even a
            // slow startup catches every item; once they've all landed the index
            // makes each repeat a no-op, so extra syncs are harmless.
            if (RequestApResync != null)
            {
                var resyncCt = _pipeCts?.Token ?? CancellationToken.None;
                _ = Task.Run(async () =>
                {
                    try
                    {
                        foreach (int delayMs in new[] { 2000, 3500, 6000 })
                        {
                            await Task.Delay(delayMs, resyncCt);
                            if (_pipe?.IsConnected != true) return; // game gone
                            await RequestApResync();
                        }
                    }
                    catch { /* resync is best-effort; cancellation = game exited */ }
                }, resyncCt);
            }
        }

        _ = Task.Run(() => PipeLoopAsync(_pipeCts.Token));
    }

    public Task StopAsync()
    {
        DisposePipe();
        try { _gameProcess?.Kill(entireProcessTree: true); } catch { }
        IsRunning = false;
        ScrubApPassword();   // plaintext credentials die with the session (P3-20)
        return Task.CompletedTask;
    }

    // Blank the [ap] Password in d2arch.ini once the session ends (P3-20) —
    // the plaintext room password should not outlive the session on disk.
    // Safe timing: the DLL reads the ini only at game startup, and every
    // launch rewrites the credentials first (WriteApCredentials).
    private void ScrubApPassword()
    {
        try
        {
            string ini = Path.Combine(GameDirectory, "Archipelago", "d2arch.ini");
            if (!File.Exists(ini)) return;
            var lines = new List<string>(File.ReadAllLines(ini));
            SetIniSectionValue(lines, "ap", "Password", "");
            File.WriteAllLines(ini, lines);
        }
        catch { /* best effort — the next launch overwrites the file anyway */ }
    }

    // Tear down the named pipe + its CTS.
    // more than once — every step is null-swapped and exception-tolerant.
    // Without this, the single-instance server stream of a finished session
    // lingers until GC and the next launch's server creation fails
    // nondeterministically (UnauthorizedAccessException/IOException).
    private void DisposePipe()
    {
        _sessionIsAp = false;  // session over (or being replaced) — stop AP pushes
        var cts = _pipeCts;
        _pipeCts = null;
        if (cts != null)
        {
            try { cts.Cancel(); } catch (ObjectDisposedException) { }
            cts.Dispose();
        }

        var pipe = _pipe;
        _pipe = null;
        if (pipe != null)
        {
            try { pipe.Dispose(); } catch { }
        }
    }

    // --- AP data-table parity (mirror the standalone txt randomization) ---

    // Set once we've applied seed-patched tables for an AP session, so the exit
    // handler knows to reset the install (and only then shows the reset bar).
    private bool _apAppliedDataTables;

    // Run the standalone seed-bound txt randomization for an AP world: read the
    // toggles from slot_data, derive a stable per-world seed, generate+apply the
    // tables (with the progress bar), and flag the DLL to skip its own monster/
    // super-unique shuffle.
    // launch) it does nothing and the DLL keeps doing the shuffle at runtime.
    private async Task ApplyApDataTablesAsync(ApSession session, long worldKey)
    {
        try
        {
            if (GetSlotData?.Invoke() is not JsonElement sd || sd.ValueKind != JsonValueKind.Object)
                return;

            var settings = D2RandomizerSettings.FromSlotData(sd);
            long seed = worldKey;
            string saveFolder = new D2SeedLibrary(GameDirectory).SeedFolder(seed);
            Directory.CreateDirectory(saveFolder);

            // Tell the DLL the launcher owns the monster/super-unique shuffle this
            // session (it still does the act-boss cosmetic swap itself).
            // d2arch.ini [settings]; the DLL reads it in both AP and offline modes.
            try
            {
                string ini = Path.Combine(GameDirectory, "Archipelago", "d2arch.ini");
                var lines = File.Exists(ini)
                    ? new List<string>(File.ReadAllLines(ini)) : new List<string>();
                SetIniSectionValue(lines, "settings", "LauncherDataShuffle", "1");
                // 2.x one-chest: give the DLL this AP world's per-seed key (the same
                // stable FNV value the data-file randomization uses) + the stash
                // isolation choice from slot_data, so the chest keys per-seed in AP.
                SetIniSectionValue(lines, "settings", "SeedKey", seed.ToString());
                SetIniSectionValue(lines, "settings", "StashIsolated", settings.StashIsolated ? "1" : "0");
                File.WriteAllLines(ini, lines);
            }
            catch { /* non-fatal */ }

            await D2RandomizeProgress.RunApplyAsync(settings, seed, saveFolder, GameDirectory);
            _apAppliedDataTables = true;
        }
        catch { /* non-fatal — fall back to the DLL's runtime shuffle */ }
    }

    // Stable, reproducible per-world seed for AP data-file randomization: derived
    // from the AP room seed name (falling back to the server) + the slot name, so
    // the same multiworld always yields the same local cosmetic randomization.
    private long StableApSeed(ApSession session)
    {
        string seedName = GetSeedName?.Invoke() ?? "";
        string basis = (string.IsNullOrEmpty(seedName) ? session.ServerUri : seedName)
                       + "|" + session.SlotName;
        ulong h = 1469598103934665603UL;          // FNV-1a 64-bit
        foreach (char c in basis) { h ^= c; h *= 1099511628211UL; }
        return (long)(h & 0x7FFFFFFFFFFFFFFFUL);
    }

    // --- Standalone launch (no AP connection) ---

    public bool SupportsStandalone => true;

    // Launch a SOLO randomized run — no Archipelago server.
    // dialog (same options the apworld exposes), writes the choices to
    // d2arch.ini [settings], then launches the game through the 32-bit
    // D2Arch_Launcher.exe bootstrap (which injects D2Archipelago.dll — the
    // 64-bit launcher cannot inject into the 32-bit game itself) WITHOUT a pipe
    // and WITHOUT D2ARCH_AP_MODE, so the mod runs offline and applies the local
    // randomization. This is what makes standalone actually randomize.
    public async Task LaunchStandaloneAsync(CancellationToken ct = default)
    {
        if (!IsInstalled)
            throw new InvalidOperationException("Diablo II: Lord of Destruction is not installed.");

        string gameExe   = Path.Combine(GameDirectory, "Game.exe");
        string bootstrap = Path.Combine(GameDirectory, "D2Arch_Launcher.exe");
        string dllPath   = Path.Combine(GameDirectory, "D2Archipelago.dll");
        if (!File.Exists(gameExe))
            throw new InvalidOperationException(
                $"Game.exe not found at:\n{gameExe}\n\nVerify your install directory in Settings.");
        // reject a retail (1.13c/1.14) Game.exe in the live folder before launch.
        {
            string? engineErr = ValidateEngineVersion();
            if (engineErr != null) throw new InvalidOperationException(engineErr);
        }
        if (!File.Exists(bootstrap) || !File.Exists(dllPath))
            throw new InvalidOperationException(
                "The Archipelago mod files are missing from the install folder " +
                "(D2Arch_Launcher.exe / D2Archipelago.dll). Re-install the game " +
                "from the Play tab, then try again.");

        // 1. Randomizer + seed library dialog (UI thread — still synchronous here
        // before the first await).
        // chosen options) or LOADS a previous one.
        var lib = new D2SeedLibrary(GameDirectory);
        var choice = D2StandaloneSettingsDialog.ShowAndGet(
            Application.Current?.MainWindow, ReadStandaloneSettings(), lib,
            Experimental);   // experimental-only rows stay out of the stable dialog
        if (choice == null)
            throw new OperationCanceledException("Standalone launch cancelled.");

        // 2. Resolve the world: ShuffleSeed must match the seed so the mod
        // reproduces it; persist the seed's settings + isolate its characters
        // in its own save folder.
        var settings = choice.Settings;
        settings.Seed = choice.Seed;
        lib.SaveMeta(new D2SeedInfo { Seed = choice.Seed, Settings = settings });
        string saveFolder = lib.SeedFolder(choice.Seed);
        Directory.CreateDirectory(saveFolder);

        // Seed-bound data tables.
        // (skill/item level + stat requirements baked in per its settings, default
        // or not) into its folder, then overlay onto the live install so the game
        // loads the seed's tables.
        // install afterwards; ApplySeed also restores-then-overlays so a prior crash
        // never leaves the folder patched.
        // backup → generate → apply → confirm so it's visible the world is randomized.
        await D2RandomizeProgress.RunApplyAsync(settings, choice.Seed, saveFolder, GameDirectory);

        // 3. Persist the chosen options. We write a PipeName (but blank server/
        // slot/pass) so the mod opens the launcher pipe and streams CHECK:
        // for the TRACKER — the DLL sends checks whenever the pipe is open,
        // independent of any AP connection.
        // the mod stays offline and grants rewards locally (true standalone).
        string pipeName =
            $"d2arch_v2_{Environment.ProcessId}_{Guid.NewGuid().ToString("N")[..8]}";
        WriteStandaloneSettings(settings, pipeName);

        // Open the pipe BEFORE launch so the DLL can connect at init.
        DisposePipe();
        var pipe = new NamedPipeServerStream(pipeName, PipeDirection.InOut,
                       maxNumberOfServerInstances: 1,
                       transmissionMode: PipeTransmissionMode.Byte,
                       options: PipeOptions.Asynchronous);
        _pipe    = pipe;
        _pipeCts = new CancellationTokenSource();

        // 4. Launch + inject via the 32-bit bootstrap (which pins the per-seed
        // save folder via D2ARCH_SAVE_PATH), then attach to Game.exe.
        Process game = await RunBootstrapAndFindGameAsync(apMode: false, ct, saveFolder);
        _gameProcess = game;

        // Capture the local `game` for the exit handler — reading the field
        // would throw if a relaunch replaced it before this process exits.
        game.EnableRaisingEvents = true;
        game.Exited += (_, _) =>
        {
            IsRunning = false;
            DisposePipe();   // tear down the tracker pipe with the game
            // reset seed-patched data tables, with a small "nulstiller" bar
            // (falls back to a silent restore if the app is already shutting down).
            D2RandomizeProgress.RunRestoreWithProgress(GameDirectory);
            int code = 0;
            try { code = game.ExitCode; } catch { /* handle already gone */ }
            GameExited?.Invoke(code);
        };

        IsRunning = true;
        if (game.HasExited) { IsRunning = false; DisposePipe(); return; }

        // 5. Pump the DLL's CHECK: messages to the launcher's tracker. Best-effort
        // on a background task: a pipe that never connects must NOT break the
        // already-running game — the tracker just stays empty that session.
        var pipeCts = _pipeCts;
        _ = Task.Run(async () =>
        {
            try
            {
                using var pcts = CancellationTokenSource.CreateLinkedTokenSource(pipeCts.Token);
                pcts.CancelAfter(TimeSpan.FromSeconds(30));
                await pipe.WaitForConnectionAsync(pcts.Token);
                RefreshMapActionContext();
                await PipeLoopAsync(pipeCts.Token);
            }
            catch { /* no tracker this session */ }
        });
    }

    // --- AP bridge: items in, checks out ---

    // Write one protocol line ("ITEM:123", "STATE:CONNECTED", ...) to the
    // pipe with the '\n' terminator the DLL frames on.
    // through here so messages never interleave mid-line.
    // a broken pipe (game exited) is swallowed — the read loop handles
    // the disconnect. Returns true only when the line was actually written
    // AND flushed, so callers (notably ReceiveItemsAsync) can avoid marking an
    // item delivered when the pipe dropped mid-batch.
    private async Task<bool> WritePipeLineAsync(string line, CancellationToken ct = default)
    {
        var pipe = _pipe;
        if (pipe?.IsConnected != true) return false;

        byte[] msg = Encoding.UTF8.GetBytes(line + "\n");
        await _pipeWriteLock.WaitAsync(ct);
        try
        {
            await pipe.WriteAsync(msg, ct);
            await pipe.FlushAsync(ct);
            return true;
        }
        catch (IOException)              { /* pipe broken — game exited */ }
        catch (ObjectDisposedException)  { /* pipe torn down */ }
        catch (InvalidOperationException){ /* pipe no longer connected */ }
        finally
        {
            _pipeWriteLock.Release();
        }
        return false;
    }

    public async Task ReceiveItemsAsync(ApNetworkItem[] items, int index,
        CancellationToken ct = default)
    {
        // Load the index for THIS room's seed (handles the player having switched
        // seeds since the plugin was constructed) before reading it.
        EnsureApIndexForCurrentSeed();

        // Count act keys BEFORE any of the early returns below. Those returns
        // are about whether the GAME is ready to be written to, which has
        // nothing to do with what the server has granted us — and the tracker
        // must know the true count even while the pipe is down.
        if (items is { Length: > 0 })
        {
            if (index == 0) _gateKeyCopies.Clear();     // full replay, recount
            foreach (var it in items)
            {
                if (!D2GateKeyLocator.IsGateKey(it.ItemId)) continue;
                _gateKeyCopies.TryGetValue(it.ItemId, out int n);
                _gateKeyCopies[it.ItemId] = n + 1;
            }
        }

        // AP resume contract: the server replays the FULL item history from
        // index 0 on every connect/Sync.
        // received and deliver only the remainder — a blanket
        // "index < _apItemIndex → return" would silently drop never-delivered
        // items riding at the tail of the same catch-up batch.
        int skip = Math.Max(0, _apItemIndex - index);
        if (skip >= items.Length) return;   // every item is an already-seen duplicate

        // Items only count as consumed while the game is attached — else they redeliver.
        if (!_sessionIsAp) return;
        if (_pipe?.IsConnected != true) return;

        // ITEM v2: "ITEM:<id>|<sender>|<locationId>" — sender resolved to a name here.
        int delivered = 0;
        foreach (var item in items.Skip(skip))
        {
            bool ok;
            if (ResolvePlayerName != null)
            {
                string sender = SanitizePipeField(
                    ResolvePlayerName(item.Player) ?? $"Player {item.Player}");
                ok = await WritePipeLineAsync(
                    $"ITEM:{item.ItemId}|{sender}|{item.LocationId}", ct);
            }
            else
            {
                ok = await WritePipeLineAsync($"ITEM:{item.ItemId}", ct);
            }
            if (!ok) break;
            delivered++;
        }

        if (delivered > 0)
        {
            // Highest contiguous index actually delivered = batch start + the
            // already-seen prefix + what we just wrote.
            _apItemIndex = Math.Max(_apItemIndex, index + skip + delivered);
            PersistApIndex();
        }
    }

    public void OnApStateChanged(ApConnectionState state)
    {
        _lastApState = state;

        // Order matters: the DLL's LoadAPSettings only reads ap_settings.dat
        // once it believes AP is connected — so the file must be on disk
        // BEFORE the STATE:CONNECTED push reaches the game.
        if (state == ApConnectionState.Connected &&
            GetSlotData?.Invoke() is JsonElement sd)
            WriteApSettingsFile(sd);

        // Tell the DLL so it can flip its in-game connection indicator.
        // Protocol only knows CONNECTED/DISCONNECTED — Connecting/Error
        // map to DISCONNECTED (the DLL treats them as "not usable yet").
        // Gated on _sessionIsAp: pushing STATE:CONNECTED into a STANDALONE
        // session's tracker pipe flipped the DLL into AP mode mid-offline-run.
        if (_sessionIsAp)
            _ = WritePipeLineAsync(state == ApConnectionState.Connected
                ? "STATE:CONNECTED"
                : "STATE:DISCONNECTED");

        if (state == ApConnectionState.Connected && _sessionIsAp)
        {
            // Drop anything we learned about the previous room first. A
            // placement is only true for the seed it came from, and a leftover
            // line would send the player to a location holding something else.
            _scoutedOwnLocations = Array.Empty<ApNetworkItem>();
            _ = WritePipeLineAsync("KEYLOC:RESET");
            _ = ScoutForGateKeysAsync();
        }

        try
        {
            string flagPath = Path.Combine(GameDirectory, "Archipelago", "ap_connected.flag");
            File.WriteAllText(flagPath, state == ApConnectionState.Connected ? "1" : "0");
        }
        catch { }
    }

    // --- Gate-key locator ---

    // Called by the UI layer when a LocationScouts reply lands.
    public void OnLocationInfo(ApNetworkItem[] items)
    {
        _scoutedOwnLocations = items ?? Array.Empty<ApNetworkItem>();
        _ = PushGateKeyHintsAsync();
    }

    // Runs after a scout reply and after each item batch: collecting a key
    // moves the answer on to the act's next gate, so the tracker would
    // otherwise keep naming a location the player already emptied.
    private async Task PushGateKeyHintsAsync()
    {
        if (!_sessionIsAp) return;
        if (_pipe?.IsConnected != true) return;
        // ResolveLocationName used to be checked here too, back when the
        // launcher had to hand it over and might not have yet.
        if (GetOwnSlot is null) return;

        List<GateKeyHint> hints;
        try
        {
            hints = D2GateKeyLocator.Build(
                _gateKeyCopies,
                _scoutedOwnLocations,
                GetCheckedLocations?.Invoke() ?? Array.Empty<long>(),
                GetOwnSlot(),
                ResolveLocationName);
        }
        catch
        {
            return;   // a broken lookup must never take the pipe down
        }

        foreach (var h in hints)
        {
            string where = SanitizePipeField(h.Where);
            if (!await WritePipeLineAsync($"KEYLOC:{h.Difficulty}|{h.Slot}|{where}"))
                break;
        }
    }

    // Ask the server what sits at every location we have not sent yet.
    // Only those can still be holding a key, so scouting the rest would be
    // noise — and a smaller scout is a smaller self-spoiler.
    private async Task ScoutForGateKeysAsync()
    {
        if (RequestLocationScouts is null || GetUncheckedLocations is null) return;
        long[] ids;
        try { ids = GetUncheckedLocations() ?? Array.Empty<long>(); }
        catch { return; }
        if (ids.Length == 0) return;
        try { await RequestLocationScouts(ids); } catch { }
    }

    // --- Named pipe receive loop ---

    private async Task PipeLoopAsync(CancellationToken ct)
    {
        // Byte-mode pipe: one ReadAsync can deliver a partial message, or
        // several messages back-to-back.
        // on complete '\n'-terminated lines; whatever trails the last '\n'
        // stays in the buffer for the next read.
        
        // The pipe instance is captured here: the loop must read from — and
        // finally dispose — ITS OWN pipe even if a relaunch already swapped
        // the _pipe field to a fresh server stream.
        var pipe    = _pipe;
        var buf     = new byte[4096];
        var pending = new StringBuilder();

        try
        {
            while (!ct.IsCancellationRequested && pipe?.IsConnected == true)
            {
                int n = await pipe.ReadAsync(buf, ct);
                if (n == 0) break;

                pending.Append(Encoding.UTF8.GetString(buf, 0, n));

                while (true)
                {
                    string all = pending.ToString();
                    int nl = all.IndexOf('\n');
                    if (nl < 0) break;

                    string msg = all[..nl].TrimEnd('\r');
                    pending.Clear();
                    pending.Append(all[(nl + 1)..]);

                    if (msg.Length == 0) continue;

                    // A faulting subscriber (UI-layer handler) must never kill
                    // this loop — that would silently drop every further CHECK
                    // and GOAL from the game for the rest of the session.
                    try { HandlePipeMessage(msg); }
                    catch (Exception ex)
                    {
                        Debug.WriteLine(
                            $"[D2Plugin] Pipe message handler failed for '{msg}': {ex}");
                    }
                }
            }
        }
        catch (OperationCanceledException) { /* clean shutdown */ }
        catch (IOException)                { /* pipe disconnected — game exited */ }
        catch (ObjectDisposedException)    { /* pipe torn down mid-read */ }
        finally
        {
            // Dispose our own stream so the next launch can create a fresh
            // single-instance server without colliding with this one.
            try { pipe?.Dispose(); } catch { }
            if (ReferenceEquals(_pipe, pipe)) _pipe = null;
        }
    }

    // Dispatch one complete newline-framed message from the DLL.
    private void HandlePipeMessage(string msg)
    {
        // "CHECK:<locationId1>,<locationId2>,..."
        if (msg.StartsWith("CHECK:", StringComparison.Ordinal))
        {
            var ids = new List<long>();
            foreach (string p in msg[6..].Split(',', StringSplitOptions.RemoveEmptyEntries))
                if (long.TryParse(p.Trim(), out long id))
                    ids.Add(id);
            if (ids.Count > 0)
            {
                LocationsChecked?.Invoke(ids.ToArray());
                foreach (var id in ids) { _mapChecked.Add(id); _mapActive.Add(id); }
                _mapControl?.SetLocations(_mapActive, _mapChecked);   // per-area checklist
            }
        }
        // "MISSING:<id,...>" — the game's FULL active location universe (sent in
        // standalone so the tracker shows unchecked locations + totals like AP).
        else if (msg.StartsWith("MISSING:", StringComparison.Ordinal))
        {
            var ids = new List<long>();
            foreach (string p in msg[8..].Split(',', StringSplitOptions.RemoveEmptyEntries))
                if (long.TryParse(p.Trim(), out long id))
                    ids.Add(id);
            if (ids.Count > 0)
            {
                LocationsMissing?.Invoke(ids.ToArray());
                foreach (var id in ids) _mapActive.Add(id);
                _mapControl?.SetLocations(_mapActive, _mapChecked);   // per-area checklist universe
            }
        }
        // "RECV:<text>" — standalone reward notification ("<location>: <reward>"),
        // surfaced in the tracker's Received tab so solo runs show what each check gave.
        else if (msg.StartsWith("RECV:", StringComparison.Ordinal))
        {
            string text = msg[5..].Trim();
            if (text.Length > 0) StandaloneItemReceived?.Invoke(text);
        }
        // "POS:<levelId>|<x>|<y>" — live player position for the map tracker dot.
        else if (msg.StartsWith("POS:", StringComparison.Ordinal))
        {
            var parts = msg[4..].Split('|');
            if (parts.Length == 3 &&
                int.TryParse(parts[0], out int lvl) &&
                int.TryParse(parts[1], out int x) &&
                int.TryParse(parts[2], out int y))
            {
                _lastMapPos = new D2PlayerPos { LevelId = lvl, X = x, Y = y };
                _mapControl?.SetPlayer(_lastMapPos);   // SetPlayer marshals to the UI thread
            }
        }
        // "CHAR:<name>" — a character loaded in the game.
        
        // Items belong to the SLOT, so every character played in it is entitled
        // to everything the slot has received.
        // record per character, so replaying the whole stream at a new character
        // is safe: it takes what it is missing and ignores the rest.
        
        // What was missing is the rewind.
        // delivered and only reset at launch, so making a second character at
        // the character-select screen left it at the end of the stream and that
        // character received nothing at all until the game was fully restarted.
        else if (msg.StartsWith("CHAR:", StringComparison.Ordinal))
        {
            string charName = msg[5..].Trim();
            if (charName.Length > 0 && charName != _apCharName)
            {
                _apCharName  = charName;
                _apItemIndex = 0;
                PersistApIndex();
                var resync = RequestApResync;      // the same hook the post-attach resync uses
                if (resync != null) _ = Task.Run(async () => { try { await resync(); } catch { } });
            }
        }
        // "GOAL" — player completed the Archipelago goal
        else if (msg == "GOAL")
        {
            // Caller wires GoalCompleted → ApClient.SendStatusUpdateAsync(Goal)
            GoalCompleted?.Invoke();
        }
        // "DEATH:<cause>" — local player died (DeathLink send-side).
        // The UI layer forwards to the AP server only when DeathLink is on.
        else if (msg.StartsWith("DEATH:", StringComparison.Ordinal))
        {
            OnPlayerDied?.Invoke(msg[6..].Trim());
        }
        // "LOG:<text>" — diagnostic from DLL
        // (handled externally by wiring PrintMessage on ApClient)
    }

    // --- DeathLink + slot-data bridge (launcher → game) ---

    // Forward a DeathLink death into the game. The DLL shows the notification;
    // the actual kill ships behind d2arch.ini DeathLinkReceive=1.
    public Task SendDeathLinkToGameAsync(string source, string cause)
        => WritePipeLineAsync(
            $"DEATHLINK:{SanitizePipeField(source)}|{SanitizePipeField(cause)}");

    // Write AP slot_data to Archipelago/ap_settings.dat in the exact format
    // the V1 Python bridge used: flat "key=value" lines, no sections, no
    // whitespace around '=', UTF-8 without BOM (a BOM would break the first
    // key's column-0 sscanf match), atomic replace.
    // prefers this file over the d2arch.ini [settings] fallback — without
    // it, characters created in a V2 session would freeze LOCAL ini settings
    // instead of the multiworld seed's.
    public void WriteApSettingsFile(JsonElement slotData)
    {
        try
        {
            if (slotData.ValueKind != JsonValueKind.Object) return;

            var sb = new StringBuilder();
            foreach (var prop in slotData.EnumerateObject())
            {
                // The DLL parses values with sscanf("%d") — every flag must be
                // a decimal integer. JSON bools map to 1/0; nulls are skipped
                // (absent key = DLL default, same as V1 never writing it).
                if (prop.Value.ValueKind == JsonValueKind.Null) continue;
                sb.Append(prop.Name).Append('=')
                  .Append(FormatSlotValue(prop.Value)).Append('\n');
            }

            string archDir = Path.Combine(GameDirectory, "Archipelago");
            Directory.CreateDirectory(archDir);
            string finalPath = Path.Combine(archDir, "ap_settings.dat");
            string tempPath  = finalPath + ".tmp";
            File.WriteAllText(tempPath, sb.ToString());   // UTF-8, no BOM
            File.Move(tempPath, finalPath, overwrite: true);
        }
        catch { /* non-fatal — DLL falls back to d2arch.ini [settings] */ }
    }

    // Slot values as the DLL's integer-only parser expects them:
    // bools → 1/0, numbers/strings raw (seed's big int stays raw digits).
    private static string FormatSlotValue(JsonElement value) => value.ValueKind switch
    {
        JsonValueKind.True   => "1",
        JsonValueKind.False  => "0",
        JsonValueKind.String => value.GetString() ?? "",
        _                    => value.GetRawText(),   // numbers / arrays / objects
    };

    // Pipe fields are |-separated and newline-framed — strip both from
    // display strings (plus CR) so a hostile slot name can't break framing.
    private static string SanitizePipeField(string s)
    {
        if (string.IsNullOrEmpty(s)) return "";
        return s.Replace("|", "/").Replace("\n", " ").Replace("\r", " ").Trim();
    }

    // --- Map tracker ---

    public bool SupportsMapTracker => true;

    private D2MapTrackerControl? _mapControl;
    private D2PlayerPos?         _lastMapPos;   // last POS seen before the panel existed
    private readonly HashSet<long> _mapChecked = new();   // checked location ids (CHECK:)
    private readonly HashSet<long> _mapActive  = new();   // run's location universe (MISSING:)

    // The launcher hosts this under the Map tab.
    // pipeline (SetWorld/SetPlayer — fed from the DLL's per-area map export +
    // the POS pipe messages) keeps updating one persistent control.
    public UIElement? CreateMapTrackerPanel()
    {
        if (_mapControl == null)
        {
            _mapControl = new D2MapTrackerControl();
            if (_lastMapPos != null) _mapControl.SetPlayer(_lastMapPos);
        }
        // Seed the checklist with anything already tracked this session.
        _mapControl.SetLocations(_mapActive, _mapChecked);
        // Point it at the DLL's live map-export folder so it fills in (and shows
        // the "you are here" dot) as the player explores.
        if (!string.IsNullOrEmpty(GameDirectory))
            _mapControl.SetMapSource(Path.Combine(GameDirectory, "Archipelago", "map"));
        // Re-apply whatever the AP session last told us.
        // and can be opened long after connecting, so the context has to be
        // stored rather than pushed once.
        ApplyApActionContext();
        return _mapControl;
    }

    // --- Hint / cheat context for the Map tab ---
    // Held here rather than in the control because the control is created on
    // demand: connect, then open the Map tab, and the control would otherwise
    // start with no idea whether a session exists.
    private bool   _apActConnected;
    private string _apActSlot = "";
    private int    _apActPoints, _apActCost;
    private Func<string, Task>? _apActSend;

    // Called by the launcher whenever the AP session's hint state changes
    // (connect, disconnect, points spent or earned).
    // --- IGamePlugin: antivirus ---
    public Task<bool> TryHandleAntivirusBlockAsync(Window owner, Exception failure)
        => D2AntivirusFix.OfferAsync(owner, this, failure,
                                     line => LogLine?.Invoke(line));

    public IReadOnlyList<string> GetMissingCriticalFiles()
        => GetMissingCriticalFilesCore();

    public string? MissingCriticalFilesCause =>
        "This is almost always your antivirus (especially Windows Defender) " +
        "removing D2Arch_Launcher.exe as a false positive.";

    public Task<int> RepairMissingCriticalFilesAsync(
            IProgress<(int Pct, string Msg)> progress)
        => RepairMissingFilesAsync(progress);

    // --- IGamePlugin: DeathLink, receive side ---
    public Task OnDeathLinkReceivedAsync(string source, string cause)
        => IsRunning ? SendDeathLinkToGameAsync(source, cause) : Task.CompletedTask;

    // --- IGamePlugin: the seed's settings ---
    //
    // The launcher used to call WriteApSettingsFile through a type check AND
    // OnSlotData right after it, on the same line of the same handler. They
    // were the same hand-off written twice; this is the one that stays.
    public void OnSlotData(System.Text.Json.JsonElement slotData)
        => WriteApSettingsFile(slotData);

    // --- IGamePlugin: the live Archipelago connection ---
    // The delegate fields above are filled from one IApServices hand-off.

    private IApServices? _apServices;

    public void OnApServicesAttached(IApServices? services)
    {
        // Unhook the previous session before taking a new one, or scout
        // replies from a finished session keep arriving.
        if (_apServices != null)
            _apServices.LocationsScouted -= OnLocationInfo;

        _apServices = services;

        if (services == null)
        {
            ResolvePlayerName     = null;
            GetSlotData           = null;
            GetSeedName           = null;
            GetOwnSlot            = null;
            RequestApResync       = null;
            RequestLocationScouts = null;
            GetCheckedLocations   = null;
            GetUncheckedLocations = null;
            OnPlayerDied          = null;
            return;
        }

        ResolvePlayerName     = services.ResolvePlayerName;
        GetSlotData           = () => services.SlotData;
        GetSeedName           = () => services.SeedName;
        GetOwnSlot            = () => services.OwnSlot;
        RequestApResync       = services.ResyncAsync;
        RequestLocationScouts = ids => services.ScoutLocationsAsync(ids);
        GetCheckedLocations   = services.CheckedLocations;
        GetUncheckedLocations = services.UncheckedLocations;
        OnPlayerDied          = cause => services.ReportDeath(cause);

        services.LocationsScouted += OnLocationInfo;
    }

    // --- IGamePlugin: the base game ---
    //
    // The mod installs into its own folder (Games/diablo2_archipelago) and
    // copies the original Blizzard data files out of the player's Classic
    // Diablo II. The original is never written to.

    public BaseGameFolderRequest? NeedsBaseGameFolder()
    {
        if (IsOriginalD2Configured) return null;

        // Try to find it before asking. Nobody should be sent hunting for a
        // folder we could have located ourselves.
        if (AutoDetectOriginalD2() is { } found)
        {
            SetBaseGameFolder(found);
            return null;
        }

        return new BaseGameFolderRequest(
            "Locate your original Diablo II",
            "Diablo II Archipelago is built from your own copy of Diablo II: " +
            "Lord of Destruction. Select the folder where Classic Diablo II is " +
            "installed — the launcher copies the needed game files from there " +
            "into its own folder and never modifies your original install.",
            "Select your Classic Diablo II: Lord of Destruction folder",
            @"C:\Program Files (x86)");
    }

    public bool HasBaseGameFiles() => HasOriginalGameFiles();

    public void SetBaseGameFolder(string folder)
    {
        // The setter persists it to this plugin's own settings bag, which is
        // also where the getter reads it back on the next start.
        OriginalD2Directory = folder;
    }

    // --- IGamePlugin: the live Archipelago session ---
    //
    // Was SetApActionContext(bool, string, int, int, Func) -- five positional
    // arguments where four of them were only meaningful if the first was true.
    // One nullable record says the same thing and cannot be half-filled.
    public void OnApSessionChanged(ApSessionContext? session)
    {
        _apActConnected = session != null;
        _apActSlot      = session?.SlotName ?? "";
        _apActPoints    = session?.HintPoints ?? 0;
        _apActCost      = session?.HintCostPoints ?? 0;
        _apActSend      = session?.Send;
        ApplyApActionContext();
    }

    // The Items tab's "hint or cheat an item" dialog. Everything it needs
    // comes from the session, so the launcher hands it over and stays out.
    public Action<Window, ApSessionContext>? ItemActions => (owner, ap) =>
        D2ItemActionDialog.ShowFor(
            owner, ap.ItemNames, ap.LocationNamesInSeed, ap.SlotName,
            ap.Send, ap.HintPoints, ap.HintCostPoints);

    private void ApplyApActionContext()
    {
        if (_mapControl == null) return;
        _mapControl.SendServerCommand = _apActSend;
        _mapControl.SetApContext(_apActConnected, _apActSlot, _apActPoints, _apActCost);
        ApplyStandaloneContext();
    }

    // A standalone game is one with a live pipe that is NOT an AP session.
    // That is the same test SendItemsAsync uses to keep AP items out of an
    // offline character, so the two can never disagree about which mode is up.
    private void ApplyStandaloneContext()
    {
        if (_mapControl == null) return;
        bool alone = _pipe?.IsConnected == true && !_sessionIsAp;
        _mapControl.ForceCheckLocal = alone
            ? (qid, diff) => WritePipeLineAsync($"FORCECHECK:{qid}|{diff}",
                                                CancellationToken.None)
            : null;
        string? saveDir = string.IsNullOrEmpty(GameDirectory)
            ? null : Path.Combine(GameDirectory, "Save");
        _mapControl.SetStandaloneContext(alone, saveDir);
    }

    // Called when a game attaches or detaches so the Map tab's buttons follow
    // the session rather than whatever was true when the tab was opened.
    public void RefreshMapActionContext() => ApplyApActionContext();

    // --- The DirectDraw wrapper (cnc-ddraw), supplied by the PLAYER ---
    // Required to start, never shipped: copied from the player's own D2 folder,
    // otherwise launch is blocked with a download link.

    public const string RendererDownloadUrl = "https://github.com/FunkyFr3sh/cnc-ddraw/releases/latest";
    private const string RendererFile = "ddraw.dll";

    // Is a DirectDraw wrapper in place in the mod's game folder?
    public bool HasRenderer =>
        !string.IsNullOrEmpty(GameDirectory) &&
        File.Exists(Path.Combine(GameDirectory, RendererFile));

    // Copy the player's own ddraw.dll across if their Diablo II folder has one.
    // Anyone running 1.10 on a modern machine almost certainly put a wrapper
    // there already. True when a copy was made.
    public bool TryAdoptRendererFromOriginal()
    {
        if (HasRenderer) return false;
        if (string.IsNullOrEmpty(OriginalD2Directory)) return false;

        string src = Path.Combine(OriginalD2Directory, RendererFile);
        if (!File.Exists(src)) return false;

        try
        {
            Directory.CreateDirectory(GameDirectory);
            File.Copy(src, Path.Combine(GameDirectory, RendererFile), overwrite: false);
            // ddraw.ini is its settings file and is worth bringing along, but its
            // absence is harmless — cnc-ddraw falls back to its own defaults.
            string ini = Path.Combine(OriginalD2Directory, "ddraw.ini");
            string iniDst = Path.Combine(GameDirectory, "ddraw.ini");
            if (File.Exists(ini) && !File.Exists(iniDst))
                File.Copy(ini, iniDst, overwrite: false);
            Debug.WriteLine("[D2] Renderer adopted from the player's own D2 folder");
            return true;
        }
        catch (Exception ex)
        {
            Debug.WriteLine("[D2] could not adopt renderer: " + ex.Message);
            return false;
        }
    }

    // Null when the game can start. Otherwise the reason it cannot, written for
    // the player rather than for us.
    public string? RendererBlocker()
    {
        if (HasRenderer) return null;
        if (TryAdoptRendererFromOriginal()) return null;

        return
            "Diablo II cannot start without a DirectDraw wrapper.\n\n" +
            "The 1.10f engine is from 2003 and its own DirectDraw no longer " +
            "initialises on Windows 10 or 11 — without a wrapper the game stops " +
            "with \"Error 22\" before the main menu.\n\n" +
            "This mod does not include one. Install cnc-ddraw yourself:\n\n" +
            "1. Download cnc-ddraw from\n   " + RendererDownloadUrl + "\n" +
            "2. Open the zip and copy ddraw.dll (and ddraw.ini if it is there)\n" +
            "   into this folder:\n   " + GameDirectory + "\n" +
            "3. Press Play again.\n\n" +
            "cnc-ddraw is free and open source (MIT) by FunkyFr3sh. If your own " +
            "Diablo II folder already has a ddraw.dll, put that one here instead " +
            "and the launcher will find it.";
    }

    // --- Copy protection on the player's own Game.exe ---

    // True when Game.exe still carries its original disc copy protection, so
    // the player is told the game will ask for the disc instead of reading it
    // as a broken mod. We do not remove protection, ship an executable without
    // it, or say where to find one. Detected by reading SafeDisc's own PE
    // section names -- reading the file, not altering anything.
    public static bool HasDiscProtection(string gameExePath)
    {
        try
        {
            using var fs = new FileStream(gameExePath, FileMode.Open, FileAccess.Read);
            using var br = new BinaryReader(fs);
            fs.Seek(0x3C, SeekOrigin.Begin);
            int pe = br.ReadInt32();
            if (pe <= 0 || pe + 24 > fs.Length) return false;
            fs.Seek(pe + 6, SeekOrigin.Begin);
            int sections = br.ReadUInt16();
            fs.Seek(pe + 20, SeekOrigin.Begin);
            int optSize = br.ReadUInt16();
            long table = pe + 24 + optSize;
            for (int i = 0; i < sections; i++)
            {
                fs.Seek(table + i * 40, SeekOrigin.Begin);
                string n = new string(br.ReadChars(8)).TrimEnd('\0').Trim();
                // stxt371 / stxt774 are SafeDisc's; Diablo II's discs used it.
                if (n.StartsWith("stxt", StringComparison.OrdinalIgnoreCase))
                    return true;
            }
            return false;
        }
        catch (Exception ex)
        {
            Debug.WriteLine("[D2] copy-protection check failed: " + ex.Message);
            return false;   // never block a launch on a failed inspection
        }
    }


    // --- Optional add-ons the player installs themselves ---

    // Is a CD/DVD drive ready — a real disc or a mounted image?
    private static bool DiscPresent()
    {
        try
        {
            return DriveInfo.GetDrives().Any(d => d.DriveType == DriveType.CDRom && d.IsReady);
        }
        catch (Exception ex)
        {
            Debug.WriteLine("[D2] drive scan failed: " + ex.Message);
            return true;   // cannot tell: never block a launch on a failed check
        }
    }

    // How badly the game wants a component.
    public enum AddonNeed
    {
        // The game cannot start without it.
        Required,
        // The game runs fine; this only makes it nicer.
        Optional,
    }

    // One component as we can see it on disk.
    //   Installed  -- its files are in the mod's game folder
    //   Active     -- installed AND able to do anything. The two differ for
    //                 SGD2FreeRes, which only D2GL loads: present without D2GL
    //                 it just sits there, and calling that "installed" sends
    //                 someone hunting a bug that is a missing second download.
    //   InOriginal -- absent here but present in the player's own Diablo II,
    //                 so we can copy it across instead of asking for a download
    public readonly record struct AddonStatus(
        string Name, string Adds, string Url, AddonNeed Need,
        bool Installed, bool Active, bool InOriginal,
        string Status, string? Advice);

    // The files that prove a component is there. First entry is the one that
    // has to exist; the rest travel with it and are copied when found.
    private static readonly (string Name, string[] Files)[] AddonFiles =
    {
        ("D2GL",        new[] { "glide3x.dll", "ddraw.dll", "d2gl.mpq" }),
        ("SGD2FreeRes", new[] { "SGD2FreeRes.dll", "SGD2FreeRes.mpq" }),
        ("DSOAL",       new[] { "dsound.dll", "dsoal-aldrv.dll", "alsoft.ini" }),
    };

    private bool InGameDir(string f) =>
        !string.IsNullOrEmpty(GameDirectory) && File.Exists(Path.Combine(GameDirectory, f));

    private bool InOriginalDir(string f) =>
        !string.IsNullOrEmpty(OriginalD2Directory) &&
        File.Exists(Path.Combine(OriginalD2Directory, f));

    // Copy a component across from the player's own Diablo II folder.
    // Anyone who has played 1.10 on a modern machine probably already has
    // some of these; asking them to download what they own is pointless.
    public bool TryAdoptFromOriginal(string addonName)
    {
        var entry = AddonFiles.FirstOrDefault(a => a.Name == addonName);
        if (entry.Files == null || string.IsNullOrEmpty(OriginalD2Directory)) return false;
        if (!InOriginalDir(entry.Files[0])) return false;

        bool any = false;
        try
        {
            Directory.CreateDirectory(GameDirectory);
            foreach (string f in entry.Files)
            {
                string src = Path.Combine(OriginalD2Directory, f);
                string dst = Path.Combine(GameDirectory, f);
                if (File.Exists(src) && !File.Exists(dst))
                {
                    File.Copy(src, dst);
                    any = true;
                }
            }
            if (any) Debug.WriteLine("[D2] " + addonName + " adopted from the player's own D2 folder");
        }
        catch (Exception ex)
        {
            Debug.WriteLine("[D2] could not adopt " + addonName + ": " + ex.Message);
        }
        return any;
    }

    // Every component, required first. Nothing is cached -- the whole point is
    // that the player drops files in while the launcher is open.
    public IReadOnlyList<AddonStatus> DetectAddons()
    {
        var list = new List<AddonStatus>();

        // Required: something has to stand in for DirectDraw. Either cnc-ddraw's
        // ddraw.dll or D2GL's -- both are wrappers, so either satisfies this.
        bool renderer = InGameDir(RendererFile);
        bool rendererInOriginal = !renderer && InOriginalDir(RendererFile);
        list.Add(new AddonStatus(
            "DirectDraw wrapper",
            "Required — the 1.10f engine cannot open a window on modern Windows without one",
            RendererDownloadUrl, AddonNeed.Required,
            renderer, renderer, rendererInOriginal,
            renderer        ? "Installed"
          : rendererInOriginal ? "Found in your own Diablo II folder — will be copied across"
                          : "MISSING — the game cannot start",
            renderer ? null
                     : "Install cnc-ddraw, or D2GL below (its ddraw.dll is a wrapper too)."));

        // The disc. Only asked when the player's own Game.exe still carries its
        // copy protection -- if theirs does not, the question never arises and
        // the badge is simply green. A mounted ISO shows up as a CD-ROM drive
        // exactly like a physical disc, so both count.
        string exe = string.IsNullOrEmpty(GameDirectory)
            ? "" : Path.Combine(GameDirectory, "Game.exe");
        bool needsDisc = !string.IsNullOrEmpty(exe) && File.Exists(exe) && HasDiscProtection(exe);
        bool discReady = !needsDisc || DiscPresent();
        list.Add(new AddonStatus(
            "Game disc", "Diablo II asks for the disc when the executable is disc-protected",
            "", AddonNeed.Required,
            discReady, discReady, false,
            !needsDisc  ? "Not needed"
          : discReady   ? "Disc detected"
                        : "MISSING — put your Diablo II disc in the drive, or mount the image",
            discReady ? null
                      : "Your Game.exe is the original disc-protected build, so Diablo II "
                      + "will ask for the CD. Insert the disc or mount your image and press Play again."));

        bool glide = InGameDir("glide3x.dll");
        list.Add(new AddonStatus(
            "D2GL", "HD rendering, widescreen, higher frame rates",
            "https://github.com/bayaraa/d2gl/releases/latest", AddonNeed.Optional,
            glide, glide, !glide && InOriginalDir("glide3x.dll"),
            glide ? "Installed" : "Not installed — original size, no HD",
            glide ? null : "Copy glide3x.dll, ddraw.dll and d2gl.mpq into the game folder."));

        bool free = InGameDir("SGD2FreeRes.dll");
        list.Add(new AddonStatus(
            "SGD2FreeRes", "Unlocks resolutions the original game does not offer",
            "https://github.com/mir-diablo-ii-tools/SlashGaming-Diablo-II-Free-Resolution/releases/latest",
            AddonNeed.Optional, free, free && glide, !free && InOriginalDir("SGD2FreeRes.dll"),
            !free ? "Not installed — original resolution only"
          : glide ? "Installed"
                  : "Installed but inactive — nothing is loading it",
            !free ? "Take the \"Vanilla\" download, not \"Modders\"."
          : glide ? null
                  : "D2GL is what loads it. Install D2GL as well."));

        bool dsoal = InGameDir("dsound.dll") && InGameDir("dsoal-aldrv.dll");
        list.Add(new AddonStatus(
            "DSOAL", "Restores the original 3D positional audio",
            "https://github.com/kcat/dsoal/releases/latest", AddonNeed.Optional,
            dsoal, dsoal, !dsoal && InOriginalDir("dsound.dll"),
            dsoal ? "Installed" : "Not installed — standard audio",
            dsoal ? null : "Both dsound.dll and dsoal-aldrv.dll are needed, from the Win32 folder."));

        return list;
    }

    // Bring across anything the player already has, then report.
    public IReadOnlyList<AddonStatus> DetectAddonsAdopting()
    {
        if (!InGameDir(RendererFile)) TryAdoptRendererFromOriginal();
        foreach (var (name, files) in AddonFiles)
            if (!InGameDir(files[0])) TryAdoptFromOriginal(name);
        return DetectAddons();
    }

    // True when something Required is missing — Play must be blocked.
    public bool MissingRequirement =>
        DetectAddons().Any(a => a.Need == AddonNeed.Required && !a.Active);

    // --- IGamePlugin.DetectComponents ---
    // Active (not Installed) becomes Present: SGD2FreeRes on disk without D2GL
    // to load it does nothing, and calling that "present" misleads.

    private static GameComponent ToComponent(AddonStatus a) => new(
        Name:   a.Name,
        Present: a.Active,
        Need:   a.Need == AddonNeed.Required ? ComponentNeed.Required
                                             : ComponentNeed.Optional,
        Status: a.Status,
        Advice: a.Advice,
        Url:    string.IsNullOrEmpty(a.Url) ? null : a.Url);

    public IReadOnlyList<GameComponent> DetectComponents()
        => DetectAddons().Select(ToComponent).ToList();

    public IReadOnlyList<GameComponent> DetectComponentsAdopting()
        => DetectAddonsAdopting().Select(ToComponent).ToList();

    // --- IGamePlugin locations ---

    // With no AP server, the active universe is purely a function of the
    // settings just written: a standalone run plays every difficulty
    // (g_apMode=FALSE in the mod), so nothing about a live session changes it.
    public long[] GetStandaloneLocationUniverse()
        => GetLocationDataPackage() is { } data
               ? D2LocationUniverse.ComputeActiveIds(GetStandaloneSettings(), data)
               : Array.Empty<long>();

    // --- IGamePlugin achievements ---

    // ⚠ FROZEN, and DIFFERENT PER CHANNEL. The stable channel shipped as
    // "d2_install", "d2_checks_10" and so on; the experimental channel never
    // had a short prefix and uses its GameId. achievements.json stores earned
    // ids verbatim, so giving both channels the same prefix would merge two
    // separate sets of progress, and changing either one un-earns everything
    // a player has.
    public string AchievementIdPrefix => Experimental ? GameId : "d2";

    // Diablo can send a DeathLink death, so the DeathLink achievement is
    // reachable here.
    public bool SendsDeathLink => true;

    public (string Title, string Description, string Icon)? GoalAchievement =>
        ("Lord of Destruction",
         "Slay Baal and his Prime Evils and bring peace to Sanctuary.",
         "⚔️");

    // The id carries the channel prefix for the same reason the ladders do:
    // two plugins returning the same achievement id would be one achievement
    // that either channel could earn for the other.
    public IReadOnlyList<GameAchievement> ExtraAchievements => new[]
    {
        new GameAchievement(
            AchievementIdPrefix + "_speed", "Speed Demon",
            "Complete a Diablo II: Lord of Destruction run in under 4 hours.",
            "🔴", "gold",
            store => store.FastestGoal(GameId) is { } t
                     && t <= TimeSpan.FromHours(4)),
    };

    // --- IGamePlugin: about this game ---

    // Ships beside the launcher today; moves into the plugin package when
    // this becomes a .londonplugin, and only this line changes.
    public string? HeaderArtPath => Path.Combine(
        AppContext.BaseDirectory, "Assets", "Heroes", GameId + "_hero.png");


    // The vanishing waypoint menu: the client's quest records go stale and the
    // menu drops every act tab. Auto-resend since v3.7.4 should prevent it; the
    // card stays as the player-facing fallback.
    public IReadOnlyList<KnownIssue> KnownIssues => new[]
    {
        new KnownIssue(
            "Your waypoints disappear from the waypoint menu — every act except " +
            "Act 1 goes missing at once, including waypoints you already activated.",
            "Nothing has been lost from your save. The game's menu loses track of " +
            "which acts you have reached; the game version from v3.7.4 / EX-1.1.12 " +
            "onwards refreshes this automatically, so you should rarely see it.",
            "If it still happens: talk to one or two town NPCs — any of them, in " +
            "any act. The menu comes straight back with every act intact."),
    };

    public IReadOnlyList<GameCredit> Credits => new[]
    {
        new GameCredit("Game by", "Blizzard Entertainment"),
        new GameCredit("AP integration by", "solida1987"),
        new GameCredit("Evil minion bookkeeping & answer man", "Maegis", Highlight: true),
    };

    // --- IGamePlugin commands ---
    //
    // Three buttons the launcher draws without knowing what any of them are.
    // Two need nothing installed: you check a seed and you write a YAML
    // BEFORE you own the game, which is exactly when someone is trying to
    // join a multiworld a friend has already generated.

    public IReadOnlyList<GameCommand> GetCommands() => new[]
    {
        new GameCommand(
            "✎  Create YAML",
            "Build your Archipelago YAML here and save it — no text editor needed",
            // The channel matters: the experimental apworld knows options the
            // stable one does not, and offering those under the stable game
            // writes a YAML its apworld rejects at generation time.
            owner => D2YamlDialog.ShowFor(owner, Experimental),
            NeedsInstall: false),

        new GameCommand(
            "✔  Check seed",
            "Check a generated seed for dead ends before you play it",
            owner => D2SeedCheckDialog.ShowFor(owner, GameDirectory),
            NeedsInstall: false),

        new GameCommand(
            "🛠  Unbug Save  ▾",
            "Fix a character that a known game bug has broken",
            owner => D2UnbugMenu.Show(owner, this, line => LogLine?.Invoke(line))),

        new GameCommand(
            "⬇  Get missing components",
            "Walk through the third-party pieces Diablo II needs, one at a time",
            owner => ShowComponentWizard(owner)),
    };

    // --- IGamePlugin: the guided component walkthrough ---

    public bool HasComponentSetup => true;

    public void ShowComponentSetup(Window? owner) => ShowComponentWizard(owner);

    /// Opens the component walkthrough. Also the place install/repair sends
    /// people, so there is exactly ONE screen that fetches these things.
    public void ShowComponentWizard(Window? owner)
    {
        // The wizard's first move is to look in the player's own Diablo II, so
        // it needs to know where that is. Asking here rather than inside the
        // wizard keeps the folder-picking in the one place that already knows
        // how to validate a D2 folder.
        if (!IsOriginalD2Configured && AutoDetectOriginalD2() is { } found)
            SetBaseGameFolder(found);

        var w = new D2ComponentWizard(
            owner, GameDirectory,
            tryAdoptFromOriginal: TryAdoptComponent,
            isAlreadyInstalled:   IsComponentInstalled);
        w.ShowDialog();
    }

    /// A component counts as installed when its FIRST file is in the game
    /// folder -- the same rule DetectAddons uses, so the wizard and the badges
    /// can never disagree about what is there.
    public bool IsComponentInstalled(string key)
    {
        var c = D2Components.ByKey(key);
        if (c is null) return false;
        var first = c.Files.FirstOrDefault(f => f.Required);
        return first is not null && InGameDir(first.SaveAs);
    }

    /// Bridge from the wizard's component keys to the older addon names used by
    /// TryAdoptFromOriginal. Two vocabularies for the same three things is one
    /// too many, but renaming the older one would touch the saved settings.
    private bool TryAdoptComponent(string key) => key switch
    {
        "d2gl"        => TryAdoptFromOriginal("D2GL"),
        "sgd2freeres" => TryAdoptFromOriginal("SGD2FreeRes"),
        "dsoal"       => TryAdoptFromOriginal("DSOAL"),
        _             => false,
    };

    // --- IGamePlugin install problems and repair ---
    //
    // Thin adapters over the two methods above them. InstallProblem here has
    // a RelPath because everything in this plugin is relative to the mod
    // folder; the interface calls it Path because a plugin that does not work
    // that way should not have to pretend it does.

    async Task<IReadOnlyList<IGamePlugin.InstallProblem>?>
        IGamePlugin.ScanInstallProblemsAsync(CancellationToken ct)
    {
        var found = await ScanInstallProblemsAsync(ct);
        return found?.Select(p => new IGamePlugin.InstallProblem(p.RelPath, p.Reason))
                     .ToList();
    }

    async Task<(IReadOnlyList<string> Restored, IReadOnlyList<string> Unrepairable)>
        IGamePlugin.RepairFilesAsync(IEnumerable<string> paths,
                                     IProgress<(int Pct, string Msg)> progress,
                                     CancellationToken ct)
    {
        var (restored, notInPackage) = await RepairFilesAsync(paths, progress, ct);
        return (restored, notInPackage);
    }


    // --- Settings UI ---

    public UIElement? CreateSettingsPanel()
    {
        var muted  = new SolidColorBrush(Color.FromRgb(0x72, 0x7A, 0x99));
        var fg     = new SolidColorBrush(Color.FromRgb(0xCC, 0xD0, 0xE0));
        var panel  = new StackPanel { Margin = new Thickness(0, 0, 0, 20) };

        // --- Section: Install directory ---
        panel.Children.Add(new TextBlock
        {
            Text = "ORIGINAL DIABLO II (your own copy)", FontSize = 10, FontWeight = FontWeights.SemiBold,
            Foreground = muted, Margin = new Thickness(0, 0, 0, 8),
        });
        panel.Children.Add(new TextBlock
        {
            Text         = "The folder where your own Classic Diablo II: Lord of Destruction is " +
                           "installed. The launcher copies the original game files from here into " +
                           "its own install folder — your copy is never modified.",
            FontSize     = 11, Foreground = muted,
            TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 8),
        });

        var dirRow   = new DockPanel { Margin = new Thickness(0, 0, 0, 16) };
        var dirBox   = new TextBox
        {
            Text        = OriginalD2Directory,
            IsReadOnly  = true,
            FontSize    = 12,
            Margin      = new Thickness(0, 0, 8, 0),
            Background  = new SolidColorBrush(Color.FromRgb(0x0C, 0x10, 0x20)),
            Foreground  = fg,
            BorderBrush = new SolidColorBrush(Color.FromRgb(0x1E, 0x22, 0x33)),
        };
        var dirBtn = new Button
        {
            Content = "Browse...", Width = 90,
            Padding = new Thickness(0, 6, 0, 6),
            Background  = new SolidColorBrush(Color.FromRgb(0x1A, 0x1E, 0x30)),
            Foreground  = fg,
            BorderBrush = new SolidColorBrush(Color.FromRgb(0x2A, 0x30, 0x50)),
        };
        dirBtn.Click += (_, _) =>
        {
            // Picks the user's OWN Classic Diablo II folder (the copy source) —
            // never the mod install dir.
            var dlg = new Microsoft.Win32.OpenFolderDialog
            {
                Title            = "Select your Classic Diablo II: Lord of Destruction folder",
                InitialDirectory = Directory.Exists(OriginalD2Directory) ? OriginalD2Directory
                                   : @"C:\Program Files (x86)",
            };
            if (dlg.ShowDialog() == true)
            {
                string? err = ValidateExistingInstall(dlg.FolderName);
                if (err != null)
                {
                    System.Windows.MessageBox.Show(err, "Folder not recognized",
                        System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Warning);
                    return;
                }
                OriginalD2Directory = dlg.FolderName;
                dirBox.Text         = dlg.FolderName;
                SaveD2Settings();
            }
        };
        DockPanel.SetDock(dirBtn, Dock.Right);
        dirRow.Children.Add(dirBtn);
        dirRow.Children.Add(dirBox);
        panel.Children.Add(dirRow);

        // "Open install folder" shortcut
        var openFolderBtn = new Button
        {
            Content             = "📁  Open Install Folder",
            HorizontalAlignment = HorizontalAlignment.Left,
            Padding             = new Thickness(10, 6, 10, 6),
            Background          = new SolidColorBrush(Color.FromRgb(0x1A, 0x1E, 0x30)),
            Foreground          = fg,
            BorderBrush         = new SolidColorBrush(Color.FromRgb(0x2A, 0x30, 0x50)),
            FontSize            = 12,
            Cursor              = System.Windows.Input.Cursors.Hand,
            Margin              = new Thickness(0, 0, 0, 16),
        };
        openFolderBtn.Click += (_, _) =>
        {
            if (Directory.Exists(GameDirectory))
                System.Diagnostics.Process.Start(
                    new System.Diagnostics.ProcessStartInfo("explorer.exe", $"\"{GameDirectory}\"")
                    { UseShellExecute = true });
        };
        panel.Children.Add(openFolderBtn);

        // --- Section: Version info ---
        panel.Children.Add(new TextBlock
        {
            Text = "VERSION INFO", FontSize = 10, FontWeight = FontWeights.SemiBold,
            Foreground = muted, Margin = new Thickness(0, 0, 0, 8),
        });
        panel.Children.Add(new TextBlock
        {
            Text     = $"Installed: {InstalledVersion ?? "Not installed"}",
            FontSize = 12, Foreground = fg, Margin = new Thickness(0, 0, 0, 4),
        });
        panel.Children.Add(new TextBlock
        {
            Text     = $"Latest available: {AvailableVersion ?? "Unknown (not checked yet)"}",
            FontSize = 12, Foreground = muted, Margin = new Thickness(0, 0, 0, 16),
        });

        // --- Section: Optional add-ons ---
        // These are not shipped with the mod (GPL/AGPL/LGPL), so the player
        // installs them by hand. That makes "did it work?" a real question, and
        // the launcher is the only thing in a position to answer it: it reads
        // the same folder the game will read, at the same moment.
        panel.Children.Add(new TextBlock
        {
            Text = "OPTIONAL ADD-ONS", FontSize = 10, FontWeight = FontWeights.SemiBold,
            Foreground = muted, Margin = new Thickness(0, 0, 0, 8),
        });
        panel.Children.Add(new TextBlock
        {
            Text = "These are made by other people and are not included with the mod — " +
                   "you download them yourself and copy them into the game folder. " +
                   "Everything below is optional; the game runs without all three.",
            FontSize = 11, Foreground = muted, TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 0, 0, 8),
        });

        var addonList = new StackPanel { Margin = new Thickness(0, 0, 0, 8) };
        panel.Children.Add(addonList);

        // Rebuilt rather than created once: the player alt-tabs out, drops the
        // files in and comes back, and the panel has to be telling the truth
        // when they do.
        void RebuildAddonRows()
        {
            addonList.Children.Clear();
            foreach (var addon in DetectAddons())
            {
                var okBrush   = new SolidColorBrush(Color.FromRgb(0x5C, 0xC8, 0x7A));
                var warnBrush = new SolidColorBrush(Color.FromRgb(0xD8, 0xA8, 0x3C));
                var dot = addon.Active ? "●" : addon.Installed ? "▲" : "○";

                var row = new StackPanel { Margin = new Thickness(0, 0, 0, 8) };
                var head = new StackPanel { Orientation = Orientation.Horizontal };
                head.Children.Add(new TextBlock
                {
                    Text = dot, FontSize = 12, Width = 16,
                    Foreground = addon.Active ? okBrush : addon.Installed ? warnBrush : muted,
                });
                head.Children.Add(new TextBlock
                {
                    Text = addon.Name, FontSize = 12, FontWeight = FontWeights.SemiBold,
                    Foreground = fg, Width = 110,
                });
                head.Children.Add(new TextBlock
                {
                    Text = addon.Status, FontSize = 12,
                    Foreground = addon.Installed && !addon.Active ? warnBrush : muted,
                    TextWrapping = TextWrapping.Wrap, MaxWidth = 360,
                });
                row.Children.Add(head);

                if (addon.Advice != null)
                {
                    var advice = new TextBlock
                    {
                        FontSize = 11, Foreground = muted, TextWrapping = TextWrapping.Wrap,
                        Margin = new Thickness(16, 2, 0, 0), MaxWidth = 470,
                    };
                    advice.Inlines.Add(addon.Advice + "  ");
                    var link = new System.Windows.Documents.Hyperlink(
                        new System.Windows.Documents.Run(addon.Url))
                    { NavigateUri = new Uri(addon.Url) };
                    link.RequestNavigate += (_, e) =>
                    {
                        try
                        {
                            System.Diagnostics.Process.Start(new ProcessStartInfo(e.Uri.AbsoluteUri)
                                { UseShellExecute = true });
                        }
                        catch (Exception ex) { Debug.WriteLine("[D2] add-on link failed: " + ex.Message); }
                        e.Handled = true;
                    };
                    advice.Inlines.Add(link);
                    row.Children.Add(advice);
                }
                addonList.Children.Add(row);
            }
        }
        RebuildAddonRows();

        var recheckBtn = new Button
        {
            Content             = "⟳  Check again",
            HorizontalAlignment = HorizontalAlignment.Left,
            Padding             = new Thickness(10, 6, 10, 6),
            Background          = new SolidColorBrush(Color.FromRgb(0x1A, 0x1E, 0x30)),
            Foreground          = fg,
            BorderBrush         = new SolidColorBrush(Color.FromRgb(0x2A, 0x30, 0x50)),
            FontSize            = 12,
            Cursor              = System.Windows.Input.Cursors.Hand,
            Margin              = new Thickness(0, 0, 0, 16),
        };
        recheckBtn.Click += (_, _) => RebuildAddonRows();
        panel.Children.Add(recheckBtn);

        // --- Section: Launch options ---
        panel.Children.Add(new TextBlock
        {
            Text = "LAUNCH OPTIONS", FontSize = 10, FontWeight = FontWeights.SemiBold,
            Foreground = muted, Margin = new Thickness(0, 0, 0, 8),
        });

        // Our own settings live in the plugin bag, keyed by GameId. They used
        // to be three fields on the launcher's settings type named after this
        // game -- which works while there is one game and it is compiled in.
        var chkWindowed = new CheckBox
        {
            Content   = "Windowed mode  (-w)",
            IsChecked = PluginSettings.GetBool(GameId, SettingWindowed),
            Foreground = fg, FontSize = 12,
            Margin    = new Thickness(0, 0, 0, 6),
        };
        chkWindowed.Checked   += (_, _) => PluginSettings.SetBool(GameId, SettingWindowed, true);
        chkWindowed.Unchecked += (_, _) => PluginSettings.SetBool(GameId, SettingWindowed, false);
        panel.Children.Add(chkWindowed);

        var chkNoSound = new CheckBox
        {
            Content   = "Disable music/sound  (-ns)",
            IsChecked = PluginSettings.GetBool(GameId, SettingNoSound),
            Foreground = fg, FontSize = 12,
            Margin    = new Thickness(0, 0, 0, 16),
        };
        chkNoSound.Checked   += (_, _) => PluginSettings.SetBool(GameId, SettingNoSound, true);
        chkNoSound.Unchecked += (_, _) => PluginSettings.SetBool(GameId, SettingNoSound, false);
        panel.Children.Add(chkNoSound);


        // --- Section: Graphics (D2GL) ---
        panel.Children.Add(new TextBlock
        {
            Text = "GRAPHICS (D2GL)", FontSize = 10, FontWeight = FontWeights.SemiBold,
            Foreground = muted, Margin = new Thickness(0, 0, 0, 8),
        });
        panel.Children.Add(new TextBlock
        {
            Text = "Fullscreen, window size, v-sync, FPS caps and more — set them here " +
                   "before launch instead of the in-game Ctrl+O menu.",
            FontSize = 11, Foreground = muted, TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 0, 0, 8),
        });
        // The settings file is ours and always present, so these controls work
        // whether or not D2GL is installed. Say so, rather than let someone
        // change a setting and wonder why the game looks identical.
        if (!DetectAddons().Any(a => a.Name == "D2GL" && a.Installed))
        {
            panel.Children.Add(new TextBlock
            {
                Text = "D2GL is not installed, so these settings have nothing to apply to yet. " +
                       "They are saved and take effect the moment you install it.",
                FontSize = 11, Foreground = new SolidColorBrush(Color.FromRgb(0xD8, 0xA8, 0x3C)),
                TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 8),
            });
        }
        var gfxBtn = new Button
        {
            Content             = "🖵  Graphics settings…",
            HorizontalAlignment = HorizontalAlignment.Left,
            Padding             = new Thickness(10, 6, 10, 6),
            Background          = new SolidColorBrush(Color.FromRgb(0x1A, 0x1E, 0x30)),
            Foreground          = fg,
            BorderBrush         = new SolidColorBrush(Color.FromRgb(0x2A, 0x30, 0x50)),
            FontSize            = 12,
            Cursor              = System.Windows.Input.Cursors.Hand,
            Margin              = new Thickness(0, 0, 0, 16),
        };
        gfxBtn.Click += (_, _) =>
        {
            if (!Directory.Exists(GameDirectory))
            {
                System.Windows.MessageBox.Show(
                    "Install Diablo II from the Play tab first — the graphics config " +
                    "lives in the game folder.", "Not installed yet",
                    System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Information);
                return;
            }
            D2GLSettingsDialog.ShowFor(Application.Current?.MainWindow, GameDirectory);
        };
        panel.Children.Add(gfxBtn);


        // --- Section: Links ---
        panel.Children.Add(new TextBlock
        {
            Text = "LINKS", FontSize = 10, FontWeight = FontWeights.SemiBold,
            Foreground = muted, Margin = new Thickness(0, 0, 0, 8),
        });

        foreach (var (label, url) in new[]
        {
            ("GitHub Repository ↗",    $"https://github.com/{GITHUB_OWNER}/{GitHubRepo}"),
            ("GitHub Releases ↗",      $"https://github.com/{GITHUB_OWNER}/{GitHubRepo}/releases"),
            ("Archipelago Official ↗", "https://archipelago.gg"),
        })
        {
            var linkBtn = new Button
            {
                Content     = label,
                HorizontalAlignment = HorizontalAlignment.Left,
                Padding     = new Thickness(0, 2, 0, 2),
                Background  = Brushes.Transparent,
                BorderThickness = new Thickness(0),
                Foreground  = new SolidColorBrush(Color.FromRgb(0x60, 0x9A, 0xFF)),
                FontSize    = 12,
                Cursor      = System.Windows.Input.Cursors.Hand,
                Margin      = new Thickness(0, 0, 0, 4),
            };
            string capturedUrl = url;
            linkBtn.Click += (_, _) =>
            {
                try { System.Diagnostics.Process.Start(
                    new System.Diagnostics.ProcessStartInfo(capturedUrl) { UseShellExecute = true }); }
                catch { /* ignore */ }
            };
            panel.Children.Add(linkBtn);
        }

        return panel;
    }

    // This Archipelago slot's own save folder, with any characters that already
    // belong to it moved in from the shared root.
    // on any failure — a launch must never be blocked by house-keeping.
    private string ResolveApSaveFolder(long key, ApSession session)
    {
        string root = Path.Combine(GameDirectory, "save");
        try
        {
            var    lib    = new D2SeedLibrary(GameDirectory);
            string folder = lib.SeedFolder(key);
            Directory.CreateDirectory(folder);
            lib.MarkApWorld(key, GetSeedName?.Invoke() ?? "", session.SlotName);
            MigrateLooseCharacters(root, folder, key, session.SlotName);
            return folder;
        }
        catch { return root; }
    }

    // Move the characters that belong to this slot out of the shared root and
    // into its folder, so isolation does not read as "my characters are gone"
    // for anyone who started playing before it existed.
    
    // Three stamps identify a character; name alone is not unique across slots.
    private void MigrateLooseCharacters(string root, string target, long key, string slotName)
    {
        if (!Directory.Exists(root)) return;
        // The DLL reads ap_slot with a 31-character field width, so a longer
        // slot name is stored truncated and must be compared truncated.
        string slotStamp = slotName.Length > 31 ? slotName[..31] : slotName;
        uint   expected  = unchecked((uint)((ulong)key ^ ((ulong)key >> 32)));

        foreach (string d2s in Directory.GetFiles(root, "*.d2s"))
        {
            string name = Path.GetFileNameWithoutExtension(d2s);
            if (string.IsNullOrEmpty(name)) continue;
            if (File.Exists(Path.Combine(target, name + ".d2s"))) continue;  // never clobber

            bool? claim = null;
            if (ReadStamp(root, $"d2arch_state_{name}.dat", "seed=") is string s &&
                uint.TryParse(s, out uint seed) && seed == expected)
                claim = true;
            else if (ReadStamp(root, $"d2arch_applied_{name}.dat", "seed_key=") is string k)
                claim = k == key.ToString(CultureInfo.InvariantCulture);
            else if (ReadStamp(root, $"d2arch_ap_{name}.dat", "ap_slot=") is string ap)
                claim = string.Equals(ap, slotStamp, StringComparison.Ordinal);

            if (claim != true) continue;

            // A character is a whole file set: <name>.d2s/.key/.ma0/.map plus every
            // sidecar the mod keys on the name (ap_stash_<name>.dat, d2arch_state_
            // <name>.dat, backups, ...).
            // stops "Gates" from also matching "Gates2".
            foreach (string f in Directory.GetFiles(root))
            {
                string fn = Path.GetFileName(f);
                if (!fn.StartsWith(name + ".", StringComparison.OrdinalIgnoreCase) &&
                    !fn.Contains("_" + name + ".", StringComparison.OrdinalIgnoreCase))
                    continue;
                try { File.Move(f, Path.Combine(target, fn)); }
                catch { /* locked or already there — leave that one behind */ }
            }
        }
    }

    // First value of prefix in a small key=value sidecar,
    // or null when the file or the key is absent.
    private static string? ReadStamp(string dir, string file, string prefix)
    {
        try
        {
            string p = Path.Combine(dir, file);
            if (!File.Exists(p)) return null;
            foreach (string line in File.ReadLines(p))
                if (line.StartsWith(prefix, StringComparison.Ordinal))
                    return line[prefix.Length..].Trim();
        }
        catch { /* unreadable — treat as unstamped */ }
        return null;
    }

    // Pin D2 1.10f save path in registry so characters are always saved to our
    // folder regardless of where a previous install wrote the keys — the shared
    // <GameDirectory>\save\, or a seed folder under it when the session isolates
    // its characters. D2 1.10f reads HKCU\Software\Blizzard Entertainment\
    // Diablo II\Save Path and NewSavePath on every launch.
    private void PinSavePathRegistry(string? folder = null)
    {
        try
        {
            string savePath = string.IsNullOrEmpty(folder)
                ? Path.Combine(GameDirectory, "save") : folder!;
            Directory.CreateDirectory(savePath);

            using var key = Registry.CurrentUser.CreateSubKey(
                @"Software\Blizzard Entertainment\Diablo II", writable: true);
            key.SetValue("Save Path",    savePath, RegistryValueKind.String);
            key.SetValue("NewSavePath",  savePath, RegistryValueKind.String);
        }
        catch
        {
            // Non-fatal — D2 will fall back to its own default save location.
        }
    }

    // Keys in this plugin's settings bag. Migrated from the launcher's old
    // d2_windowed / d2_no_sound / diablo2_path fields -- see PluginSettings.
    private const string SettingWindowed   = "windowed";
    private const string SettingNoSound    = "no_sound";
    private const string SettingBaseFolder = "original_game_folder";

    // The extra command-line flags the player asked for.
    private string BuildExtraLaunchArgs()
    {
        var sb = new StringBuilder();
        if (PluginSettings.GetBool(GameId, SettingWindowed)) sb.Append(" -w");
        if (PluginSettings.GetBool(GameId, SettingNoSound))  sb.Append(" -ns");
        return sb.ToString();
    }

    private void SaveD2Settings()
    {
        // Persist where the player's own Diablo II lives, in our own bag.
        PluginSettings.Set(GameId, SettingBaseFolder, OriginalD2Directory);

        // Also keep the per-plugin file for backward-compat / third-party tooling.
        string dir = Path.Combine(AppContext.BaseDirectory, "Data");
        Directory.CreateDirectory(dir);
        File.WriteAllText(
            Path.Combine(dir, "d2_settings.json"),
            JsonSerializer.Serialize(new { game_directory = GameDirectory }));
    }

    // (LoadD2Settings was removed — dead code, P3-14. The plugin settings bag
    // is the authoritative load path; the d2_settings.json written above stays
    // as a one-way export for third-party tooling.)

    // --- IGamePlugin: catalog + news ---

    public string Description =>
        "A full Archipelago randomiser mod for Diablo II Lord of Destruction. " +
        "Randomises skills, quests, zones and items across a multiworld. " +
        "Play solo or in a multiworld with friends on any AP server.";

    public string? VideoPreviewUrl  => null;
    public string[] ScreenshotUrls => Array.Empty<string>();
    public string   ApWorldName    => "Diablo II Archipelago";
    public string   ThemeAccentColor => "#7A1010";   // blood-red
    // State-aware (RefreshHeaderBadges rebuilds when requirement state changes):
    // the badge disappears once the user has pointed the launcher at a valid
    // original install — the requirement is then satisfied, leaving only the
    // green "✓ Installed" badge. It names the patch level because that is the
    // part people get wrong: any other version fails to install.
    public string[] GameBadges =>
        IsOriginalD2Configured ? Array.Empty<string>() : new[] { "Requires D2: LoD 1.10f" };

    public async Task<NewsItem[]> GetNewsAsync(CancellationToken ct = default)
    {
        try
        {
            string json = await _http.GetStringAsync(
                "https://api.github.com/repos/solida1987/Diablo-II-Archipelago/releases", ct);
            using var doc  = JsonDocument.Parse(json);
            var items = new List<NewsItem>();
            foreach (var el in doc.RootElement.EnumerateArray())
            {
                items.Add(new NewsItem(
                    Title:   el.TryGetProperty("name",         out var n) ? n.GetString()! : "",
                    Body:    el.TryGetProperty("body",         out var b) ? b.GetString()! : "",
                    Version: el.TryGetProperty("tag_name",     out var t) ? t.GetString()! : "",
                    Date:    el.TryGetProperty("published_at", out var d)
                                 ? DateTimeOffset.Parse(d.GetString()!)
                                 : DateTimeOffset.MinValue,
                    Url:     el.TryGetProperty("html_url",     out var u) ? u.GetString()  : null
                ));
                if (items.Count >= 20) break;
            }
            return items.ToArray();
        }
        catch { return Array.Empty<NewsItem>(); }
    }

    // --- Helpers: AP credentials → d2arch.ini ---

    private void WriteApCredentials(ApSession session, string pipeName, long worldKey)
    {
        string ini = Path.Combine(GameDirectory, "Archipelago", "d2arch.ini");
        try
        {
            // A missing ini (partial install / manual cleanup) used to silently
            // skip the write — the DLL then had no server/slot/pipe name and
            // the player saw an unexplained in-game "not connected" (P2-14).
            // Create it: SetIniSectionValue appends the [ap] section + keys to
            // an empty file, which is a valid minimal config for the DLL
            // (other sections simply fall back to their defaults).
            List<string> lines;
            if (File.Exists(ini))
            {
                lines = new List<string>(File.ReadAllLines(ini));
            }
            else
            {
                Directory.CreateDirectory(
                    Path.GetDirectoryName(ini) ?? GameDirectory);
                lines = new List<string>();
            }

            // Strip scheme prefix from server URI for the DLL's TCP parser
            string serverEntry = session.ServerUri.Contains("://")
                ? session.ServerUri.Split("://", 2)[1]
                : session.ServerUri;

            SetIniSectionValue(lines, "ap", "ServerIP", serverEntry);
            SetIniSectionValue(lines, "ap", "SlotName", session.SlotName);
            SetIniSectionValue(lines, "ap", "Password", session.Password ?? "");
            // Pipe discovery for the DLL — bare name, no \\.\pipe\ prefix.
            // V1 DLLs ignore the key; V2 DLLs switch to the pipe transport.
            SetIniSectionValue(lines, "ap", "PipeName", pipeName);

            // overwrite the standalone seed keys on every AP launch, so a value
            // left behind by an earlier STANDALONE launch cannot key this session's
            // stash isolation, seed-ledger and delivery dedup to the wrong world.
            
            // SeedKey must stay set here — a blank key made the DLL fall back to a shared save tree.
            SetIniSectionValue(lines, "settings", "SeedKey",
                worldKey.ToString(CultureInfo.InvariantCulture));
            SetIniSectionValue(lines, "settings", "ShuffleSeed", "0");

            // Launcher owns the AP connection + randomization → mod hides its
            // in-game title overlay + F1 AP login panel (read from disk, so it
            // works even if the D2ARCH_LAUNCHER env var doesn't propagate).
            SetIniSectionValue(lines, "launcher", "HideInGameUI", "1");

            WriteLootFilterSection(lines);

            File.WriteAllLines(ini, lines);
        }
        catch { /* non-fatal — user can set manually in d2arch.ini */ }
    }

    // Display preference — written on both launch paths.
    private static void WriteLootFilterSection(List<string> lines)
    {
        SetIniSectionValue(lines, "LootFilter", "Enabled", "0");
        SetIniSectionValue(lines, "LootFilter", "Diagnostic", "0");
    }

    // Write <key>= into a specific [section] of an ini file (in-memory).
    // Adds the section and/or key if they don't exist.
    private static void SetIniSectionValue(
        List<string> lines, string section, string key, string value)
    {
        string header  = $"[{section}]";
        string newLine = $"{key}={value}";

        // Find section header
        int sectionIdx = -1;
        for (int i = 0; i < lines.Count; i++)
        {
            if (lines[i].Trim().Equals(header, StringComparison.OrdinalIgnoreCase))
            { sectionIdx = i; break; }
        }

        if (sectionIdx < 0)
        {
            // Section missing — append
            if (lines.Count > 0 && !string.IsNullOrWhiteSpace(lines[^1]))
                lines.Add("");
            lines.Add(header);
            lines.Add(newLine);
            return;
        }

        // Search within section for existing key
        for (int i = sectionIdx + 1; i < lines.Count; i++)
        {
            string t = lines[i].Trim();
            if (t.StartsWith('[')) break; // next section

            int eq = t.IndexOf('=');
            if (eq > 0 && t[..eq].Trim().Equals(key, StringComparison.OrdinalIgnoreCase))
            { lines[i] = newLine; return; }
        }

        // Key not found — insert right after the section header
        lines.Insert(sectionIdx + 1, newLine);
    }

    // Read <key> from [section] of an in-memory ini (mirrors the scan in
    // SetIniSectionValue). Returns the trimmed value, or null if the section
    // or key is absent.
    private static string? ReadIniSectionValue(IReadOnlyList<string> lines, string section, string key)
    {
        string header = $"[{section}]";
        int sectionIdx = -1;
        for (int i = 0; i < lines.Count; i++)
        {
            if (lines[i].Trim().Equals(header, StringComparison.OrdinalIgnoreCase))
            { sectionIdx = i; break; }
        }
        if (sectionIdx < 0) return null;

        for (int i = sectionIdx + 1; i < lines.Count; i++)
        {
            string t = lines[i].Trim();
            if (t.StartsWith('[')) break;            // next section
            int eq = t.IndexOf('=');
            if (eq > 0 && t[..eq].Trim().Equals(key, StringComparison.OrdinalIgnoreCase))
                return t[(eq + 1)..].Trim();
        }
        return null;
    }

    private string D2IniPath => Path.Combine(GameDirectory, "Archipelago", "d2arch.ini");

    // The seed's location table, written into the game folder at install time.
    // Absent until the game is installed, and null then — the tracker draws
    // nothing rather than an empty world.
    public JsonElement? GetLocationDataPackage()
    {
        try
        {
            string p = Path.Combine(GameDirectory, "Archipelago", "d2_locations.json");
            if (!File.Exists(p)) return null;
            using var doc = JsonDocument.Parse(File.ReadAllText(p));
            return doc.RootElement.Clone();
        }
        catch { return null; }
    }

    private D2RandomizerSettings ReadStandaloneSettings()
    {
        try
        {
            string ini = D2IniPath;
            if (!File.Exists(ini)) return new D2RandomizerSettings();
            var lines = File.ReadAllLines(ini);
            return D2RandomizerSettings.FromIni(key => ReadIniSectionValue(lines, "settings", key));
        }
        catch { return new D2RandomizerSettings(); }
    }

    // The standalone randomizer options currently on disk (d2arch.ini
    // [settings]) — exactly what the mod reads this session.
    // + GetLocationDataPackage() to derive the standalone tracker's full active
    // location universe (D2LocationUniverse), since there's no AP server to
    // deliver one. Read AFTER LaunchStandaloneAsync, so it reflects the just-
    // written launch settings.
    public D2RandomizerSettings GetStandaloneSettings() => ReadStandaloneSettings();

    // Persist the chosen randomizer options to d2arch.ini [settings] and blank
    // the [ap] section. The mod's LoadAPSettings reads [settings] as its single
    // source of truth whenever AP is offline, so a standalone (DLL-injected,
    // no-pipe) launch randomizes exactly per these values.
    // guarantees the DLL never tries to reach a stale pipe/server.
    private void WriteStandaloneSettings(D2RandomizerSettings s, string pipeName)
    {
        string ini = D2IniPath;
        List<string> lines;
        if (File.Exists(ini))
        {
            lines = new List<string>(File.ReadAllLines(ini));
        }
        else
        {
            Directory.CreateDirectory(Path.GetDirectoryName(ini) ?? GameDirectory);
            lines = new List<string>();
        }

        foreach (var kv in s.ToIniPairs())
            SetIniSectionValue(lines, "settings", kv.Key, kv.Value);

        // Standalone: write the PipeName so the mod streams CHECK: to the
        // launcher's tracker, but blank server/slot/pass so it never tries to
        // reach an AP server (the launcher never sends STATE:CONNECTED either).
        SetIniSectionValue(lines, "ap", "PipeName", pipeName);
        SetIniSectionValue(lines, "ap", "ServerIP", "");
        SetIniSectionValue(lines, "ap", "SlotName", "");
        SetIniSectionValue(lines, "ap", "Password", "");

        // Launcher owns randomization → mod hides its in-game title overlay +
        // F1 AP login panel (read from disk = reliable regardless of env vars).
        SetIniSectionValue(lines, "launcher", "HideInGameUI", "1");

        WriteLootFilterSection(lines);

        File.WriteAllLines(ini, lines);
    }

    // --- Launch via the 32-bit bootstrap (the ONLY reliable injector) ---
    
    // The launcher is 64-bit, Game.exe is 32-bit — injection goes through the
    // 32-bit DetoursLauncher helper.

    // Run D2Arch_Launcher.exe (kills stale Game.exe, sets DIABLO2_PATCH,
    // launches + injects), wait for it to finish, then return the live
    // Game.exe process. apMode sets D2ARCH_AP_MODE so the
    // DLL keeps ap_settings.dat (AP) instead of scrubbing it (standalone).
    private async Task<Process> RunBootstrapAndFindGameAsync(
        bool apMode, CancellationToken ct, string? saveFolder = null)
    {
        string bootstrap = Path.Combine(GameDirectory, "D2Arch_Launcher.exe");
        if (!File.Exists(bootstrap))
            throw new InvalidOperationException(
                "D2Arch_Launcher.exe is missing from the install folder — re-install the " +
                "game from the Play tab, then try again.");

        // Map tracker — the dumps must not survive a SEED change (a new layout would
        // render on top of stale rooms), but they absolutely must survive a save &
        // quit on the SAME seed.
        // every relaunch threw the player's exploration away — reported as "the map
        // feature doesn't remember a player's map exploration from previous save and
        // quit". Now the folder is stamped with the seed it belongs to and only wiped
        // when that stamp disagrees.
        
        // saveFolder is already the per-seed save directory in BOTH modes (AP passes
        // apSaveDir, standalone passes its own seed folder), so it is the identity
        // this needs — no new plumbing.
        // apart, so it keeps the old unconditional wipe.
        try
        {
            string mapDir   = Path.Combine(GameDirectory, "Archipelago", "map");
            string stampFile = Path.Combine(mapDir, "seed.id");
            string stamp     = saveFolder ?? "";

            bool keep = stamp.Length > 0
                     && File.Exists(stampFile)
                     && string.Equals(File.ReadAllText(stampFile).Trim(), stamp,
                                      StringComparison.OrdinalIgnoreCase);

            if (!keep)
            {
                if (Directory.Exists(mapDir)) Directory.Delete(mapDir, recursive: true);
                if (stamp.Length > 0)
                {
                    Directory.CreateDirectory(mapDir);
                    File.WriteAllText(stampFile, stamp);
                }
            }
        }
        catch { /* non-fatal — stale rooms at worst */ }

        // -3dfx selects the Glide renderer; without a wrapper present it must not be passed.
        bool haveGlide = File.Exists(Path.Combine(GameDirectory, "glide3x.dll"));
        string renderer = haveGlide ? "-3dfx " : string.Empty;
        Debug.WriteLine(haveGlide
            ? "[D2] Renderer: Glide (glide3x.dll found - D2GL is installed)"
            : "[D2] Renderer: DirectDraw (no glide3x.dll - install D2GL for HD)");

        string extraArgs = BuildExtraLaunchArgs();
        var psi = new ProcessStartInfo
        {
            FileName         = bootstrap,
            Arguments        = $"{renderer}-direct -txt{extraArgs}",
            WorkingDirectory = GameDirectory,
            UseShellExecute  = false,
            CreateNoWindow   = true,
        };
        if (apMode) psi.Environment["D2ARCH_AP_MODE"] = "1";
        // The bootstrap sets DIABLO2_PATCH itself; set it here too so the
        // dependency is explicit and survives any future refactor of the exe.
        psi.Environment["DIABLO2_PATCH"] = Path.Combine(GameDirectory, "patch");
        // The launcher now owns randomization (it writes d2arch.ini [settings])
        // and the AP connection (the pipe), so tell the mod to hide its in-game
        // title-screen overlay (the old toggles + Server/Slot/Pass/Connect panel).
        psi.Environment["D2ARCH_LAUNCHER"] = "1";
        // Per-seed character isolation: the bootstrap pins D2's save path here,
        // so D2's character-select only shows characters created under this seed.
        if (!string.IsNullOrEmpty(saveFolder))
            psi.Environment["D2ARCH_SAVE_PATH"] = saveFolder;

        var boot = Process.Start(psi)
            ?? throw new InvalidOperationException("Failed to start the mod loader (D2Arch_Launcher.exe).");

        // The bootstrap exits the moment injection completes (or fails).
        // internal waits cap at ~15s for Game.exe + 3 inject retries, so 60s is
        // a safe ceiling that still surfaces a hung AV scan.
        using (var bootCts = CancellationTokenSource.CreateLinkedTokenSource(ct))
        {
            bootCts.CancelAfter(TimeSpan.FromSeconds(60));
            try { await boot.WaitForExitAsync(bootCts.Token); }
            catch (OperationCanceledException) when (!ct.IsCancellationRequested)
            {
                try { boot.Kill(entireProcessTree: true); } catch { }
                throw new InvalidOperationException(
                    "The mod loader did not finish within 60 seconds — usually antivirus " +
                    "blocking DLL injection. Add the game folder and the launcher to your " +
                    "antivirus exclusions, then try again.");
            }
        }

        if (boot.ExitCode != 0)
            throw new InvalidOperationException(
                $"The randomizer mod could not be injected into the game (loader exit code {boot.ExitCode}). " +
                "This is almost always antivirus blocking DLL injection — add D2Archipelago.dll, " +
                "Game.exe, D2Arch_Launcher.exe and the launcher to your antivirus exclusions, then try again.");

        // Bootstrap succeeded → Game.exe is running + modded.
        int pid = await FindGamePidByImagePathAsync(ct);
        return Process.GetProcessById(pid);
    }

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern bool QueryFullProcessImageNameW(
        IntPtr hProcess, int flags, System.Text.StringBuilder exeName, ref int size);

    private const uint PROCESS_QUERY_LIMITED_INFORMATION = 0x1000;

    // Find the Game.exe living under GameDirectory.
    // QueryFullProcessImageName (works cross-bitness, unlike Process.MainModule
    // which a 64-bit process can't reliably read on a 32-bit target).
    // briefly because the bootstrap may exit a beat before the OS finishes
    // surfacing the process.
    private async Task<int> FindGamePidByImagePathAsync(CancellationToken ct, int timeoutMs = 10_000)
    {
        string root;
        try   { root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(GameDirectory)); }
        catch { root = GameDirectory; }

        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline && !ct.IsCancellationRequested)
        {
            foreach (var p in Process.GetProcessesByName("Game"))
            {
                IntPtr h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, p.Id);
                if (h == IntPtr.Zero) continue;
                try
                {
                    var sb  = new System.Text.StringBuilder(1024);
                    int cap = sb.Capacity;
                    if (QueryFullProcessImageNameW(h, 0, sb, ref cap))
                    {
                        string full = Path.GetFullPath(sb.ToString());
                        if (full.StartsWith(root + Path.DirectorySeparatorChar,
                                            StringComparison.OrdinalIgnoreCase))
                            return p.Id;
                    }
                }
                catch { /* process vanished — skip */ }
                finally { CloseHandle(h); }
            }
            await Task.Delay(200, ct);
        }
        throw new InvalidOperationException(
            "The game started but could not be located under the install folder. " +
            "If it is running, close it and try again.");
    }

    // --- Helpers: wait for Game.exe process ---

    // Find the Game.exe spawned by OUR DetoursLauncher — not any other running D2.
    private async Task<int> WaitForGamePidAsync(CancellationToken ct, int timeoutMs = 15_000)
    {
        // Process.StartTime reports LOCAL time; small slack absorbs clock skew
        // between Process.Start and this call.
        var launchedAfter = DateTime.Now - TimeSpan.FromSeconds(3);

        string expectedRoot;
        try   { expectedRoot = Path.TrimEndingDirectorySeparator(Path.GetFullPath(GameDirectory)); }
        catch { expectedRoot = GameDirectory; }

        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline && !ct.IsCancellationRequested)
        {
            foreach (var p in Process.GetProcessesByName("Game"))
            {
                try
                {
                    string? exePath = null;
                    try { exePath = p.MainModule?.FileName; }
                    catch (System.ComponentModel.Win32Exception) { /* access denied — use fallback */ }
                    catch (InvalidOperationException)            { continue; /* already exited */ }

                    if (exePath != null)
                    {
                        string fullExe = Path.GetFullPath(exePath);
                        if (fullExe.StartsWith(expectedRoot + Path.DirectorySeparatorChar,
                                               StringComparison.OrdinalIgnoreCase))
                            return p.Id;
                        continue;   // some OTHER "Game" process — never inject into it
                    }

                    // Image path unreadable → start-time fallback.
                    if (p.StartTime >= launchedAfter)
                        return p.Id;
                }
                catch { /* process vanished mid-query — skip it */ }
            }
            await Task.Delay(200, ct);
        }
        throw new TimeoutException(
            "Game.exe did not appear under the install folder within 15 seconds.");
    }

    // --- DLL injection via P/Invoke ---
    
    // Runs from inside the main 154MB launcher process — much less suspicious
    // to AV heuristics than a tiny standalone injector binary.
    // AV long-term fix: code-sign the launcher exe with an EV certificate.

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint access, bool inherit, int pid);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddr,
        uint dwSize, uint flAllocType, uint flProtect);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBase,
        byte[] buf, uint size, out int written);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr GetProcAddress(IntPtr hModule, string procName);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr GetModuleHandleA(string lpModuleName);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr CreateRemoteThread(IntPtr hProcess, IntPtr lpAttr,
        uint dwStackSize, IntPtr lpStartAddr, IntPtr lpParam,
        uint dwCreationFlags, out uint lpThreadId);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(IntPtr hObject, uint dwMs);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr hObject);

    private const uint PROCESS_ALL_ACCESS = 0x1F0FFF;
    private const uint MEM_COMMIT_RESERVE = 0x3000;
    private const uint PAGE_READWRITE     = 0x04;

    private async Task InjectDllAsync(int pid, string dllPath, CancellationToken ct)
    {
        await Task.Run(() =>
        {
            IntPtr hProcess = OpenProcess(PROCESS_ALL_ACCESS, false, pid);
            if (hProcess == IntPtr.Zero)
                throw new InvalidOperationException(
                    $"OpenProcess failed (err {Marshal.GetLastWin32Error()}). " +
                    "Add the launcher to your antivirus exclusion list.");
            try
            {
                byte[] pathBytes = Encoding.ASCII.GetBytes(dllPath + "\0");
                IntPtr remoteMem = VirtualAllocEx(hProcess, IntPtr.Zero,
                    (uint)pathBytes.Length, MEM_COMMIT_RESERVE, PAGE_READWRITE);

                if (remoteMem == IntPtr.Zero)
                    throw new InvalidOperationException(
                        $"VirtualAllocEx failed (err {Marshal.GetLastWin32Error()}).");

                WriteProcessMemory(hProcess, remoteMem, pathBytes,
                    (uint)pathBytes.Length, out _);

                IntPtr kernel32 = GetModuleHandleA("kernel32.dll");
                IntPtr loadLib  = GetProcAddress(kernel32, "LoadLibraryA");

                IntPtr hThread = CreateRemoteThread(hProcess, IntPtr.Zero, 0,
                    loadLib, remoteMem, 0, out _);

                if (hThread == IntPtr.Zero)
                    throw new InvalidOperationException(
                        $"CreateRemoteThread failed (err {Marshal.GetLastWin32Error()}). " +
                        "AV is blocking injection — add Game.exe and launcher to exclusions.");

                WaitForSingleObject(hThread, 10_000);
                CloseHandle(hThread);
            }
            finally
            {
                CloseHandle(hProcess);
            }
        }, ct);
    }

    // --- Helpers: download ---

    private async Task DownloadWithProgressAsync(
        string url, string destPath,
        int startPct, int endPct,
        IProgress<(int, string)> progress,
        CancellationToken ct)
    {
        using var resp = await _http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead, ct);
        resp.EnsureSuccessStatusCode();
        long? total = resp.Content.Headers.ContentLength;

        await using var stream = await resp.Content.ReadAsStreamAsync(ct);
        await using var file   = new FileStream(destPath, FileMode.Create);

        var buf = new byte[81920];
        long bytesRead = 0;
        var  lastReport = DateTime.MinValue;

        while (true)
        {
            int read = await stream.ReadAsync(buf, ct);
            if (read <= 0 || ct.IsCancellationRequested) break;
            await file.WriteAsync(buf.AsMemory(0, read), ct);
            bytesRead += read;

            if ((DateTime.Now - lastReport).TotalMilliseconds > 250)
            {
                lastReport = DateTime.Now;
                double frac = total.HasValue ? (double)bytesRead / total.Value : 0;
                int pct = startPct + (int)(frac * (endPct - startPct));
                string sizeStr = total.HasValue
                    ? $"{bytesRead / 1_048_576}MB / {total.Value / 1_048_576}MB"
                    : $"{bytesRead / 1_048_576}MB";
                progress.Report((pct, "Downloading... " + sizeStr));
            }
        }
    }

    private async Task DownloadSimpleAsync(string url, string destPath, CancellationToken ct)
        => await File.WriteAllBytesAsync(destPath, await _http.GetByteArrayAsync(url, ct), ct);

    // Exponential backoff with jitter: 0.5s, 1s, 2s, 4s, 8s … capped at 10s.
    private static int BackoffMs(int retry)
        => (int)Math.Min(10000, 500 * Math.Pow(2, retry)) + Random.Shared.Next(0, 400);

    // Download a (large) file to disk with progress, retrying the whole transfer
    // on a transient failure.
    // update, so a dropped connection mid-download self-corrects instead of
    // failing the install.
    private async Task DownloadZipWithRetryAsync(
        string url, string destPath, int startPct, int endPct,
        IProgress<(int, string)> progress, CancellationToken ct, int attempts = 4)
    {
        for (int retry = 0; ; retry++)
        {
            try
            {
                await DownloadWithProgressAsync(url, destPath, startPct, endPct, progress, ct);
                return;
            }
            catch (OperationCanceledException) { throw; }
            catch when (retry < attempts - 1)
            {
                progress.Report((startPct,
                    $"Download interrupted — retrying ({retry + 1}/{attempts - 1})..."));
                await Task.Delay(BackoffMs(retry), ct);
            }
        }
    }

    // --- Helpers: file operations ---

    private static string ComputeSha256(string filePath)
    {
        using var sha  = SHA256.Create();
        using var file = File.OpenRead(filePath);
        return BitConverter.ToString(sha.ComputeHash(file)).Replace("-", "").ToLower();
    }

    private void DeleteBinCache()
    {
        string excelDir = Path.Combine(GameDirectory, "data", "global", "excel");
        if (!Directory.Exists(excelDir)) return;
        foreach (string f in Directory.GetFiles(excelDir, "*.bin"))
            try { File.Delete(f); } catch { }
    }

    private int DeleteOrphans(HashSet<string> manifestPaths)
    {
        int deleted = 0;
        string[] modDirs = { "patch", "ap_bridge_dist", "apworld" };
        foreach (string dirName in modDirs)
        {
            string fullDir = Path.Combine(GameDirectory, dirName);
            if (!Directory.Exists(fullDir)) continue;
            foreach (string filePath in Directory.GetFiles(fullDir, "*", SearchOption.AllDirectories))
            {
                string rel = Path.GetRelativePath(GameDirectory, filePath).Replace('\\', '/');
                if (!manifestPaths.Contains(rel))
                    try { File.Delete(filePath); deleted++; } catch { }
            }
        }

        // Top-level mod files only (never blind-scan the game root)
        HashSet<string> knownTopFiles = new(StringComparer.OrdinalIgnoreCase)
        {
            "D2Archipelago.dll", "D2Archipelago.exp", "D2Archipelago.lib",
            "D2.DetoursLauncher.exe", "Detours.dll",
        };
        foreach (string fileName in knownTopFiles)
        {
            string full = Path.Combine(GameDirectory, fileName);
            if (File.Exists(full) && !manifestPaths.Contains(fileName))
                try { File.Delete(full); deleted++; } catch { }
        }
        return deleted;
    }

    private void WriteVersionDat(string? version)
    {
        if (string.IsNullOrEmpty(version)) return;
        try
        {
            string dir = Path.Combine(GameDirectory, "Archipelago");
            Directory.CreateDirectory(dir);
            File.WriteAllText(Path.Combine(dir, "version.dat"), version.Trim());
            InstalledVersion = version.Trim();
        }
        catch { }
    }

    private static void JsonFindString(string json, string key, ref string? result)
    {
        string marker = "\"" + key + "\"";
        int i = json.IndexOf(marker);
        if (i < 0) return;
        i = json.IndexOf('"', i + marker.Length);
        if (i < 0) return;
        i++;
        int end = json.IndexOf('"', i);
        if (end > i) result = json[i..end];
    }

    // --- Helpers: AP index persistence ---

    // The received-items index is PER-SEED (per AP room), not per-install: every
    // seed starts its own item stream at 0.
    // player who started a new seed inherited the previous seed's index, so the
    // new seed's early items — notably the precollected starting skills at index
    // 0 — were all skipped.
    // legacy global name only when no seed is known.
    private string? _apIndexSeedKey;   // seed the in-memory _apItemIndex belongs to

    private string ApIndexPath
    {
        get
        {
            string? seed = GetSeedName?.Invoke();
            string suffix = string.IsNullOrEmpty(seed) ? "" : "_" + StableSeedKey(seed!);
            return Path.Combine(GameDirectory, "Archipelago", $"ap_item_index{suffix}.dat");
        }
    }

    // Filesystem-safe stable key for a seed name (FNV-1a, 8 hex chars).
    private static string StableSeedKey(string seed)
    {
        uint h = 2166136261u;
        foreach (char c in seed) { h ^= c; h *= 16777619u; }
        return h.ToString("x8");
    }

    // Reload _apItemIndex from the file for the current seed if the seed changed
    // since we last loaded.
    private void EnsureApIndexForCurrentSeed()
    {
        string key = GetSeedName?.Invoke() ?? "";
        if (_apIndexSeedKey == key) return;
        _apIndexSeedKey = key;
        LoadApIndex();
    }

    private void PersistApIndex()
    {
        // index is SESSION-ONLY — still written to disk purely as a
        // diagnostic breadcrumb; LoadApIndex never reads it back.
        try { File.WriteAllText(ApIndexPath, _apItemIndex.ToString()); } catch { }
    }

    private void LoadApIndex()
    {
        // ALWAYS start a session at index 0 — the server replays the full item
        // history, and the dedup index decides what is genuinely new.
        _apItemIndex = 0;
    }
}

// --- Entry types, one per channel ---
// The loader constructs the class named in plugin.json via a parameterless
// ctor, so the channel is a TYPE, not a flag someone sets from outside.

// The stable channel.
public sealed class DiabloIIArchipelago : D2Plugin
{
    public DiabloIIArchipelago()
    {
        // GameId reads Experimental, and GameDirectory's setter loads the AP
        // resume index off disk -- so the order here is not cosmetic.
        GameDirectory = SettingsStore.DefaultGamePath(GameId);
    }
}

// The experimental channel: a separate install, a separate save tree, a
// separate GitHub repo. Fully isolated from the stable channel above -- for
// aggressive testing only.
public sealed class DiabloIIArchipelagoExperimental : D2Plugin
{
    public DiabloIIArchipelagoExperimental()
    {
        Experimental  = true;
        GameDirectory = SettingsStore.DefaultGamePath(GameId);
    }
}
