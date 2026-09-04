using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using System.Windows.Threading;
// The project enables WindowsForms, so these names are ambiguous — pin them to WPF.
using UserControl = System.Windows.Controls.UserControl;
using ListBox     = System.Windows.Controls.ListBox;
using Rectangle   = System.Windows.Shapes.Rectangle;
using Path        = System.IO.Path;

namespace LauncherV2.Plugins.DiabloII;

// D2 Map Tracker — the launcher-side graphical map (PopTracker / maphack style).

// Fed by the injected DLL over the pipe; see docs/MAP_TRACKER.md.

// The control owns the model + rendering.
// (every area, all five acts) the moment it opens — areas you have not entered
// yet render as "not explored" but still list their checks and lock state — and
// fills in the real collision map as you walk.

// --- Data model ---

// One generated area (= one D2 level).
// walkable grid is row-major Width*Height with (0,0) at world tile (OriginX,
// OriginY) so areas can later be stitched into an act/world overview.
public sealed class D2MapArea
{
    public int    LevelId;
    public string Name = "";
    public int    Act;
    public int    Width;
    public int    Height;
    public int    OriginX;
    public int    OriginY;
    public bool[]? Walkable;                 // true = walkable floor (collision detail)
    public bool[]? Known;                    // true = room exists but not yet explored (dim block)

    public List<D2MapExit>  Exits    = new();
    public List<D2MapPoi>   Pois     = new();

    public bool At(int gx, int gy)
        => Walkable != null && gx >= 0 && gy >= 0 && gx < Width && gy < Height
           && Walkable[gy * Width + gx];
}

public sealed class D2MapExit  { public int TargetLevelId; public string TargetName = ""; public int X, Y; public bool Locked; }
public sealed class D2MapPoi   { public string Kind = ""; public string Label = ""; public int X, Y; } // waypoint / chest / shrine / barrel …

public sealed class D2MapWorld
{
    public long Seed;
    public Dictionary<int, D2MapArea> Areas = new();
}

public sealed class D2PlayerPos { public int LevelId; public int X, Y; }

// --- The control ---

// Self-contained WPF control: a full area list (grouped by act, coloured by
// lock state), a rendered collision map with a live player dot + object
// markers, and a per-area info panel (lock state, gate, checklist done/total).
// Thread-safe entry points marshal to the UI thread.
public sealed class D2MapTrackerControl : UserControl
{
    // Map palette — warm parchment floor on a dark "void", the readable maphack look.
    private static readonly Color VoidColor  = Color.FromRgb(0x0E, 0x0F, 0x13);
    private static readonly Color FloorColor = Color.FromRgb(0xC2, 0xB6, 0x96);  // explored, walkable
    private static readonly Color DimColor   = Color.FromRgb(0x33, 0x30, 0x2A);  // known room, unexplored
    private static readonly Brush Panel      = new SolidColorBrush(Color.FromRgb(0x1B, 0x1B, 0x1F));
    private static readonly Brush Muted      = new SolidColorBrush(Color.FromRgb(0x9A, 0x9A, 0xA6));
    private static readonly Brush Gold       = new SolidColorBrush(Color.FromRgb(0xE6, 0xB8, 0x4C));
    private static readonly Brush OpenGreen  = new SolidColorBrush(Color.FromRgb(0x57, 0xC7, 0x6B));
    private static readonly Brush LockedRed  = new SolidColorBrush(Color.FromRgb(0xE0, 0x5A, 0x4F));
    private static readonly Brush CheckDone  = new SolidColorBrush(Color.FromRgb(0x4C, 0xC2, 0x6A));
    private static readonly Brush CheckOpen  = new SolidColorBrush(Color.FromRgb(0xE0, 0x60, 0x50));
    private static readonly Brush PlayerDot  = new SolidColorBrush(Color.FromRgb(0x55, 0xC8, 0xFF));
    private static readonly Brush SelBg      = new SolidColorBrush(Color.FromRgb(0x2C, 0x2C, 0x36));

    private D2MapWorld   _world  = new();          // visited areas (real collision)
    private D2PlayerPos? _player;
    private int          _selectedLevelId = 1;
    private int          _diff = 0;                // 0=Normal 1=Nightmare 2=Hell (viewed difficulty)
    private bool         _showMarkers = true;

    // Live tracker state fed by D2Plugin.
    private readonly HashSet<long> _checkedIds = new();
    private readonly HashSet<long> _activeIds  = new();        // run's location universe (MISSING:)
    private Dictionary<long, string> _locNames = new();        // location id → name (d2_locations.json)
    private readonly HashSet<int>[]  _zoneLocked = { new(), new(), new() };  // per-diff locked area ids
    private bool _haveLockData;
    private bool _diffAutoSet;        // auto-jumped to the player's live difficulty once

    private readonly ListBox  _areaList   = new();
    private readonly Image    _mapImage   = new() { SnapsToDevicePixels = true };
    private readonly Canvas   _overlay    = new();
    private readonly Grid     _mapStack   = new();
    private readonly StackPanel _info     = new();
    private readonly TextBlock _emptyHint = new();
    private readonly StackPanel _diffBar  = new() { Orientation = System.Windows.Controls.Orientation.Horizontal };
    private readonly Button[]  _diffBtns  = new Button[3];

    // --- Status bar (P3 + P4) ---
    // The tab knew every one of these numbers and showed none of them: the area
    // list has per-area done/total and an R-band tag, but nothing said where you
    // are in the run as a whole, or how much of it you can actually reach right
    // now. Test D flagged both as missing.
    private readonly TextBlock _sphereLine = new();
    private readonly TextBlock _reachLine  = new();
    private readonly TextBlock _hintLine   = new();

    // --- AP actions: hint a check, or force it through ---
    // Set by MainWindow while an AP session is live.
    // to "buttons hidden" when not connected rather than to buttons that fail
    // on click — a disabled control the user cannot explain is worse than no
    // control at all.
    // Sends one raw chat line to the server (a "!command").
    public Func<string, Task>? SendServerCommand { get; set; }
    // Standalone equivalent of the Cheat button: (questId, difficulty) sent
    // down the pipe as FORCECHECK, which the mod runs through its own quest
    // completion path. Null when no standalone game is attached.
    public Func<int, int, Task>? ForceCheckLocal { get; set; }
    private bool   _apConnected;
    private bool   _standalone;
    private string _slotName = "";
    private int    _hintPoints;
    private int    _hintCostPoints;

    // Standalone reward spoiler: difficulty -> quest name -> reward text.
    // The mod writes it next to the .d2s (d2arch_spoiler_<char>.txt) and it
    // is deterministic per character, so it is exactly what a "hint" means
    // when there is no server to ask.
    private readonly Dictionary<string, string>[] _spoiler =
        { new(StringComparer.OrdinalIgnoreCase), new(StringComparer.OrdinalIgnoreCase),
          new(StringComparer.OrdinalIgnoreCase) };
    private bool _haveSpoiler;

    // Point the tracker at a standalone session.
    // writes the per-character spoiler.
    public void SetStandaloneContext(bool active, string? saveDir)
    {
        if (!Dispatcher.CheckAccess())
        { Dispatcher.Invoke(() => SetStandaloneContext(active, saveDir)); return; }
        _standalone = active;
        if (active && !string.IsNullOrEmpty(saveDir)) LoadSpoiler(saveDir!);
        UpdateStatusBar();
        DrawInfo();
    }

    // Parse every d2arch_spoiler_*.txt in the save folder.
    // " Normal " then indented
    // " <quest name> -> <reward>" lines.
    private void LoadSpoiler(string saveDir)
    {
        try
        {
            foreach (var d in _spoiler) d.Clear();
            _haveSpoiler = false;
            if (!Directory.Exists(saveDir)) return;
            var files = Directory.GetFiles(saveDir, "d2arch_spoiler_*.txt");
            if (files.Length == 0) return;
            // Newest character wins if several exist — it is the one being played.
            var newest = files.OrderByDescending(f => new FileInfo(f).LastWriteTimeUtc).First();
            int diff = -1;
            foreach (var raw in File.ReadLines(newest))
            {
                var line = raw.TrimEnd();
                if (line.Contains("==== Normal ===="))          diff = 0;
                else if (line.Contains("==== Nightmare ====")) diff = 1;
                else if (line.Contains("==== Hell ===="))      diff = 2;
                int arrow = line.IndexOf("->", StringComparison.Ordinal);
                if (diff < 0 || arrow <= 0) continue;
                string q = line[..arrow].Trim();
                string r = line[(arrow + 2)..].Trim();
                if (q.Length > 0 && r.Length > 0) _spoiler[diff][q] = r;
            }
            _haveSpoiler = _spoiler.Any(d => d.Count > 0);
        }
        catch { _haveSpoiler = false; }
    }

    // The reward a check will give in standalone, or null if unknown.
    // The checklist shows AP location names, which carry a difficulty suffix
    // the spoiler's quest names do not.
    private string? SpoilerRewardFor(string locationName)
    {
        if (!_haveSpoiler || _diff is < 0 or > 2) return null;
        string bare = Regex.Replace(locationName, @"\s*\((Normal|Nightmare|Hell)\)$", "").Trim();
        return _spoiler[_diff].TryGetValue(bare, out var r) ? r : null;
    }
    // 0 = Full Normal, 1 = Full Nightmare, 2 = Full Hell, 3 = Collection,
    // 4 = Custom. Only used to work out how many difficulties the run spans.
    private int _goal = 2;

    public D2MapTrackerControl()
    {
        Background = new SolidColorBrush(VoidColor);
        Build();
        RebuildAreaList();
        SelectArea(_selectedLevelId);
    }

    private void Build()
    {
        var root = new Grid();
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(248) });
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(320) });

        // --- Left: difficulty selector + area list ---
        _areaList.Background        = Panel;
        _areaList.BorderThickness   = new Thickness(0);
        _areaList.Foreground        = Brushes.White;
        ScrollViewer.SetHorizontalScrollBarVisibility(_areaList, ScrollBarVisibility.Disabled);
        _areaList.SelectionChanged += (_, _) =>
        {
            if (_areaList.SelectedItem is ListBoxItem li && li.Tag is int id) SelectArea(id);
        };

        var leftHost = new DockPanel { Background = Panel };

        string[] dn = { "Normal", "Nightmare", "Hell" };
        for (int i = 0; i < 3; i++)
        {
            int d = i;
            var b = new Button
            {
                Content = dn[i], Tag = d, FontSize = 11, Padding = new Thickness(6, 3, 6, 3),
                Margin = new Thickness(i == 0 ? 12 : 3, 8, i == 2 ? 12 : 0, 4),
                Background = Panel, Foreground = Muted, BorderThickness = new Thickness(0),
                Cursor = System.Windows.Input.Cursors.Hand,
            };
            b.Click += (_, _) => SetDifficulty(d);
            _diffBtns[i] = b;
            _diffBar.Children.Add(b);
        }
        DockPanel.SetDock(_diffBar, Dock.Top);
        leftHost.Children.Add(_diffBar);

        var leftHdr = new TextBlock
        {
            Text = "AREAS", Foreground = Muted, FontWeight = FontWeights.Bold,
            FontSize = 11, Margin = new Thickness(12, 6, 0, 6),
        };
        DockPanel.SetDock(leftHdr, Dock.Top);
        leftHost.Children.Add(leftHdr);

        var markerChk = new CheckBox
        {
            Content = "📍 Show markers", IsChecked = true,
            Foreground = Brushes.White, Margin = new Thickness(12, 6, 8, 10),
        };
        markerChk.Checked   += (_, _) => { _showMarkers = true;  DrawOverlay(); };
        markerChk.Unchecked += (_, _) => { _showMarkers = false; DrawOverlay(); };
        DockPanel.SetDock(markerChk, Dock.Bottom);
        leftHost.Children.Add(markerChk);

        leftHost.Children.Add(_areaList);
        Grid.SetColumn(leftHost, 0);
        root.Children.Add(leftHost);
        UpdateDiffButtons();

        // --- Center: the map ---
        _mapStack.Children.Add(_mapImage);
        _mapStack.Children.Add(_overlay);
        var viewbox = new Viewbox { Stretch = Stretch.Uniform, Child = _mapStack, Margin = new Thickness(8) };
        _emptyHint.Foreground          = Muted;
        _emptyHint.FontSize            = 14;
        _emptyHint.HorizontalAlignment = HorizontalAlignment.Center;
        _emptyHint.VerticalAlignment   = VerticalAlignment.Center;
        _emptyHint.TextAlignment       = TextAlignment.Center;
        var center = new Grid();
        center.Children.Add(viewbox);
        center.Children.Add(_emptyHint);
        Grid.SetColumn(center, 1);
        root.Children.Add(center);

        // --- Right: per-area info ---
        var infoScroll = new ScrollViewer
        {
            Background = Panel,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            Content = _info,
        };
        _info.Margin = new Thickness(14);
        Grid.SetColumn(infoScroll, 2);
        root.Children.Add(infoScroll);

        // --- Status bar across the top (P3 sphere + P4 reachable checks) ---
        _sphereLine.Foreground = Gold;
        _sphereLine.FontSize   = 12;
        _sphereLine.FontWeight = FontWeights.Bold;
        _sphereLine.VerticalAlignment = VerticalAlignment.Center;
        _reachLine.Foreground  = Muted;
        _reachLine.FontSize    = 12;
        _reachLine.VerticalAlignment = VerticalAlignment.Center;
        _reachLine.Margin      = new Thickness(18, 0, 0, 0);

        _hintLine.Foreground = Muted;
        _hintLine.FontSize   = 12;
        _hintLine.VerticalAlignment = VerticalAlignment.Center;
        _hintLine.Margin     = new Thickness(18, 0, 0, 0);

        var statusRow = new StackPanel { Orientation = System.Windows.Controls.Orientation.Horizontal };
        statusRow.Children.Add(_sphereLine);
        statusRow.Children.Add(_reachLine);
        statusRow.Children.Add(_hintLine);
        var statusBar = new Border
        {
            Background      = Panel,
            Padding         = new Thickness(14, 7, 14, 7),
            BorderBrush     = new SolidColorBrush(Color.FromRgb(0x2A, 0x2A, 0x32)),
            BorderThickness = new Thickness(0, 0, 0, 1),
            Child           = statusRow,
        };

        var outer = new DockPanel { LastChildFill = true };
        DockPanel.SetDock(statusBar, Dock.Top);
        outer.Children.Add(statusBar);
        outer.Children.Add(root);

        Content = outer;
        UpdateStatusBar();
    }

    // --- Status bar content ---

    // How many difficulties this run actually spans.
    // one-difficulty run, so its whole world is 5 spheres, not 15 — telling a
    // Normal-goal player they are on "sphere 3 of 15" would be a lie about how
    // much game is left. Collection and Custom goals can require anything, so
    // they get the full three.
    private int DiffsInRun => _goal is >= 0 and <= 2 ? _goal + 1 : 3;

    // Is act `act`'s gate `gate` (1..4, opening region gate+1) open at the
    // viewed difficulty? Judged from VANILLA regions on purpose: zonelock.dat
    // is keyed on them, because the DLL unlocks a whole vanilla region per key.
    // _entryRegion is the entrance-shuffled ORDER — right for sorting the list,
    // wrong for asking what a key opened.
    private bool GateOpen(int act, int gate)
    {
        foreach (var kv in D2LogicTables.ZoneRegion)
            if (kv.Value.Act == act && kv.Value.Region == gate + 1 && !IsLocked(kv.Key))
                return true;
        return false;
    }

    // Repaint the two status lines.
    // path; it walks the area catalogue once.
    private void UpdateStatusBar()
    {
        // Where "you" are: the live player if we have one, else whatever the
        // user is looking at — the tab is also used to plan while not in-game.
        int refArea = _player?.LevelId is int lv && D2Cat.ContainsKey(lv) ? lv : _selectedLevelId;
        int act = D2Cat.TryGetValue(refArea, out var cat) ? cat.Act : 1;
        if (act is < 1 or > 5) act = 1;

        int sphere = _diff * 5 + act;
        int total  = 5 * DiffsInRun;
        string[] dn = { "Normal", "Nightmare", "Hell" };

        // Gate count is per act and NOT uniform — Act 4 has 2, the rest 4.
        // Hardcoding 4 would have parked Act 4 at "2 of 4 gates open" forever,
        // which reads as "you are still locked out of half the act" at the exact
        // moment the player is walking into the Chaos Sanctuary.
        string gates = "";
        if (_haveLockData)
        {
            int n = D2LogicTables.GatesPerAct.TryGetValue(act, out int gp) ? gp : 4;
            int open = 0;
            for (int g = 1; g <= n; g++) if (GateOpen(act, g)) open++;
            gates = $"   ·   {open} of {n} gates open";
        }
        _sphereLine.Text = $"Sphere {sphere} of {total}   —   Act {act} {dn[_diff]}{gates}";

        // Reachable = not done, and its area is not locked.
        // everything still undone, so the pair answers "how much of what's left
        // can I actually go and do right now".
        int reach = 0, left = 0;
        foreach (var id in D2Cat.Keys)
        {
            bool locked = IsLocked(id);
            foreach (var c in ChecksFor(id))
            {
                if (c.Done) continue;
                left++;
                if (!locked) reach++;
            }
        }
        _reachLine.Text = left > 0
            ? $"Checks reachable now: {reach} of {left} left"
            : "All checks done";

        // Hint economy. Both numbers come straight from the server — points
        // from Connected/RoomUpdate, the price from hint_cost (a PERCENTAGE of
        // the slot's location count, which ApClient converts to real points).
        // Shown even at zero, because "you have none" is the answer to the
        // question the player is asking when they look here.
        // Say something even when offline.
        // whole hint/cheat feature look absent.
        _hintLine.Text = !_apConnected
            ? _standalone
                ? (_haveSpoiler ? "Standalone — hints are free"
                                : "Standalone — no reward spoiler yet")
                : "Hints: start a game"
            : _hintCostPoints <= 0
                ? $"Hint points: {_hintPoints}   ·   hints are free"
                : $"Hint points: {_hintPoints}   ·   a hint costs {_hintCostPoints}";
        _hintLine.Foreground = _apConnected && _hintPoints >= _hintCostPoints ? OpenGreen : Muted;
    }

    // Feed the live AP session state.
    // as the server sends updates; it only repaints.
    public void SetApContext(bool connected, string slotName, int hintPoints, int hintCostPoints)
    {
        if (!Dispatcher.CheckAccess())
        { Dispatcher.Invoke(() => SetApContext(connected, slotName, hintPoints, hintCostPoints)); return; }
        _apConnected    = connected;
        _slotName       = slotName ?? "";
        _hintPoints     = hintPoints;
        _hintCostPoints = hintCostPoints;
        UpdateStatusBar();
        DrawInfo();                 // the per-check buttons depend on all of it
    }

    // --- Public data API (thread-safe) ---

    public void SetWorld(D2MapWorld world)
    {
        if (!Dispatcher.CheckAccess()) { Dispatcher.Invoke(() => SetWorld(world)); return; }
        _world = world ?? new D2MapWorld();
        // Re-render the selected area (its collision may have just arrived).
        SelectArea(_selectedLevelId);
    }

    public void SetPlayer(D2PlayerPos pos)
    {
        if (!Dispatcher.CheckAccess()) { Dispatcher.Invoke(() => SetPlayer(pos)); return; }
        bool areaChanged = _player?.LevelId != pos?.LevelId;
        _player = pos;
        if (areaChanged && pos != null && D2Cat.ContainsKey(pos.LevelId))
        {
            RebuildAreaList();              // refresh the "▸ you are here" marker
            SelectArea(pos.LevelId);        // follow the player into the new area
        }
        else DrawOverlay();
    }

    // Feed the tracker's location state: the run's full universe (active) and the
    // checked subset. Drives the per-area checklist + each area's (done/total).
    public void SetLocations(IEnumerable<long>? active, IEnumerable<long>? checkedIds)
    {
        if (!Dispatcher.CheckAccess()) { Dispatcher.Invoke(() => SetLocations(active, checkedIds)); return; }
        if (active != null)     { _activeIds.Clear();  foreach (var i in active)     _activeIds.Add(i); }
        if (checkedIds != null) { _checkedIds.Clear(); foreach (var i in checkedIds) _checkedIds.Add(i); }
        RebuildAreaList();
        DrawInfo();
    }

    // ── Live map source (the DLL's per-room collision export) ────────────────

    private string?          _mapDir;
    // The seed's actual gate layout: area id -> (act, region you must reach to
    // get in). Without entrance shuffle that is the zone's own region; with it,
    // a dungeon is entered through whichever entrance the shuffle pointed at it,
    // so the list must follow THAT order, not the vanilla one.
    private Dictionary<int, (int Act, int Region)> _entryRegion = new();
    private bool _entranceShuffled;
    private DispatcherTimer?  _mapPoll;
    private long             _mapStamp;

    // Point the control at the DLL's map-export folder
    // (<GameDir>\Archipelago\map).
    // zonelock.dat and rebuilds as the player explores.
    public void SetMapSource(string mapDir)
    {
        if (!Dispatcher.CheckAccess()) { Dispatcher.Invoke(() => SetMapSource(mapDir)); return; }
        _mapDir = mapDir;
        EnsureObjCat(mapDir);
        LoadLocationNames(mapDir);             // <GameDir>\Archipelago\d2_locations.json
        LoadSuperUniqueShuffle(mapDir);        // must follow LoadLocationNames — it maps by hunt name
        LoadSeedLayout(mapDir);                // gate order + entrance shuffle
        LoadGoal(mapDir);                      // how many difficulties the run spans
        _mapStamp = -1;
        if (_mapPoll == null)
        {
            _mapPoll = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1.5) };
            _mapPoll.Tick += (_, _) => PollMapDir();
        }
        _mapPoll.Start();
        PollMapDir();
        // Repaint now that the names exist.
        // the checklist is built from, but nothing redrew afterwards: until
        // something else happened to refresh (clicking an area, switching
        // difficulty, zonelock.dat arriving) the info panel said "No tracked
        // checks in this area" for an area full of them.
        // the control offscreen — it never clicks anything, so it sat in
        // exactly the state a freshly opened tab does.
        RebuildAreaList();
        SelectArea(_selectedLevelId);
    }

    private void PollMapDir()
    {
        try
        {
            if (string.IsNullOrEmpty(_mapDir) || !Directory.Exists(_mapDir)) return;
            LoadZoneLocks(_mapDir);            // tiny file, reload every poll (keys unlock live)
            long stamp = 0;
            foreach (var f in Directory.GetFiles(_mapDir, "level_*.map"))
            {
                var fi = new FileInfo(f);
                stamp ^= fi.Length * 31 + fi.LastWriteTimeUtc.Ticks;
            }
            if (stamp == _mapStamp) return;
            _mapStamp = stamp;
            var world = BuildWorldFromDir(_mapDir);
            if (world.Areas.Count > 0) SetWorld(world);
        }
        catch { /* best-effort — never break the UI on a half-written file */ }
    }

    // Read the run's goal so the sphere counter knows how long the run is.
    // Two sources for the same reason entrance shuffle has two: standalone
    // keeps it in d2arch.ini as "Goal", an Archipelago slot gets it from
    // slot_data, which the bridge writes to ap_settings.dat as "goal".
    // only one would give every AP player the standalone default.
    private void LoadGoal(string mapDir)
    {
        try
        {
            string archDir = Path.GetDirectoryName(mapDir) ?? mapDir;

            string apCfg = Path.Combine(archDir, "ap_settings.dat");
            if (File.Exists(apCfg))
            {
                var m = Regex.Match(File.ReadAllText(apCfg), @"^\s*goal\s*=\s*(\d+)\s*$",
                                    RegexOptions.Multiline);
                if (m.Success && int.TryParse(m.Groups[1].Value, out int ap))
                { _goal = Math.Clamp(ap, 0, 4); return; }
            }

            string ini = Path.Combine(archDir, "d2arch.ini");
            if (!File.Exists(ini)) ini = Path.Combine(mapDir, "d2arch.ini");
            if (File.Exists(ini))
            {
                var m = Regex.Match(File.ReadAllText(ini), @"^\s*Goal\s*=\s*(\d+)",
                                    RegexOptions.Multiline | RegexOptions.IgnoreCase);
                if (m.Success && int.TryParse(m.Groups[1].Value, out int st))
                    _goal = Math.Clamp(st, 0, 4);
            }
        }
        catch { /* keep the default — a wrong total is better than no map tab */ }
    }

    // Super-unique shuffle: hunt quest id -> the name of the boss whose spawn
    // point that hunt's target now stands on. Empty when the shuffle is off.
    private Dictionary<int, string> _huntHost = new();
    // Hunt quest id -> the area it is ACTUALLY in this seed. Same as D2QuestArea
    // when the shuffle is off; the host's area when it is on.
    private Dictionary<int, int> _huntAreaOverride = new();

    // su_shuffle.dat — the same file the DLL reads, so tracker and game agree.
    private void LoadSuperUniqueShuffle(string mapDir)
    {
        _huntHost = new Dictionary<int, string>();
        _huntAreaOverride = new Dictionary<int, int>();
        try
        {
            string archDir = Path.GetFullPath(Path.Combine(mapDir, ".."));
            string p = Path.Combine(archDir, "su_shuffle.dat");
            if (!File.Exists(p)) return;   // shuffle off — static table is correct

            // hunt display name -> quest id, from the location names we already
            // loaded ("Hunt: Bishibosh" -> 8). Difficulty suffixes are stripped
            // because the shuffle is the same across all three.
            var huntQid = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
            foreach (var kv in _locNames)
            {
                string n = kv.Value;
                if (!n.StartsWith("Hunt: ", StringComparison.OrdinalIgnoreCase)) continue;
                foreach (string suffix in new[] { " (Nightmare)", " (Hell)" })
                    if (n.EndsWith(suffix, StringComparison.Ordinal))
                        n = n.Substring(0, n.Length - suffix.Length);
                // Location id = 42000 + questId + difficulty*1000.
                huntQid[n.Substring(6).Trim()] = (int)((kv.Key - 42000) % 1000);
            }

            foreach (string raw in File.ReadAllLines(p))
            {
                // hc=<hcIdx>|who=<identity>|at=<host spot>
                var parts = raw.Split('|');
                string who = "", at = "";
                foreach (string part in parts)
                {
                    if (part.StartsWith("who=", StringComparison.Ordinal)) who = part.Substring(4).Trim();
                    else if (part.StartsWith("at=", StringComparison.Ordinal)) at = part.Substring(3).Trim();
                }
                if (who.Length == 0 || at.Length == 0) continue;
                if (!huntQid.TryGetValue(who, out int movedQid)) continue;

                _huntHost[movedQid] = at;
                // The host's own area is where the moved identity now stands.
                if (huntQid.TryGetValue(at, out int hostQid)
                    && D2QuestArea.TryGetValue(hostQid, out int hostArea))
                    _huntAreaOverride[movedQid] = hostArea;
            }
        }
        catch { /* unreadable → fall back to the static table */ }
    }

    // Load <GameDir>\Archipelago\d2_locations.json → id→name (all difficulties).
    private void LoadLocationNames(string mapDir)
    {
        try
        {
            string archDir = Path.GetFullPath(Path.Combine(mapDir, ".."));
            string p = Path.Combine(archDir, "d2_locations.json");
            if (!File.Exists(p)) return;
            using var doc = JsonDocument.Parse(File.ReadAllText(p));
            if (!doc.RootElement.TryGetProperty("location_name_to_id", out var n2i)) return;
            var map = new Dictionary<long, string>();
            foreach (var prop in n2i.EnumerateObject())
                if (prop.Value.TryGetInt64(out long id)) map[id] = prop.Name;
            _locNames = map;
        }
        catch { /* missing/old install → checklist falls back to #id */ }
    }

    // Work out the order the game will really open up in, for THIS seed.
    
    // Areas sort in seed progression order, not vanilla id order.
    private void LoadSeedLayout(string mapDir)
    {
        try
        {
            _entryRegion = new Dictionary<int, (int, int)>();
            _entranceShuffled = false;
            foreach (var kv in D2LogicTables.ZoneRegion) _entryRegion[kv.Key] = kv.Value;
            foreach (var kv in D2LogicTables.PortalEntryRegion) _entryRegion[kv.Key] = kv.Value;

            // Archipelago\<...>: the ini lives beside the map data.
            string ini = Path.Combine(Path.GetDirectoryName(mapDir) ?? mapDir, "d2arch.ini");
            if (!File.Exists(ini)) ini = Path.Combine(mapDir, "d2arch.ini");
            if (!File.Exists(ini)) return;

            string blob = File.ReadAllText(ini);
            string Val(string key)
            {
                var m = Regex.Match(blob, @"^\s*" + key + @"\s*=\s*(\S+)",
                                    RegexOptions.Multiline | RegexOptions.IgnoreCase);
                return m.Success ? m.Groups[1].Value.Trim() : "";
            }
            // Standalone keeps the toggle in the ini; an Archipelago game gets it
            // from slot_data, which the bridge writes to ap_settings.dat as
            // "entrance_shuffle=1".
            // vanilla order for exactly the players who need the real one.
            bool shuffled = Val("EntranceShuffle") == "1";
            if (!shuffled)
            {
                string apCfg = Path.Combine(Path.GetDirectoryName(ini) ?? "", "ap_settings.dat");
                if (File.Exists(apCfg))
                    shuffled = Regex.IsMatch(File.ReadAllText(apCfg),
                                             @"^\s*entrance_shuffle\s*=\s*1\s*$",
                                             RegexOptions.Multiline);
            }
            if (!shuffled) return;

            ulong key = 0;
            if (!ulong.TryParse(Val("SeedKey"), out key) || key == 0)
                ulong.TryParse(Val("ShuffleSeed"), out key);
            if (key == 0) return;

            uint seed = unchecked((uint)(key ^ (key >> 32)));
            var map = D2SeedCheck.BuildShuffleMap(seed);
            var sets = D2LogicTables.DungeonSets;
            for (int from = 0; from < map.Length; from++)
            {
                int to = map[from];
                if (to == from || sets[from].Zones.Length == 0) continue;
                if (!D2LogicTables.ZoneRegion.TryGetValue(sets[from].Zones[0], out var er)) continue;
                foreach (int z in sets[to].Zones) _entryRegion[z] = er;
            }
            _entranceShuffled = true;
        }
        catch { /* no layout info → fall back to vanilla order */ }
    }

    // Load the DLL's live per-difficulty lock state (zonelock.dat).
    // "<diff>:<lockedId,lockedId,...>"; an area not listed is open.
    private void LoadZoneLocks(string mapDir)
    {
        try
        {
            string p = Path.Combine(mapDir, "zonelock.dat");
            if (!File.Exists(p)) { _haveLockData = false; return; }
            var lines = File.ReadAllLines(p);
            for (int d = 0; d < 3; d++) _zoneLocked[d].Clear();
            foreach (var line in lines)
            {
                if (line.StartsWith("CUR:", StringComparison.Ordinal))
                {
                    // Auto-jump to the player's live difficulty the first time only,
                    // then respect any manual selection.
                    if (!_diffAutoSet && int.TryParse(line.AsSpan(4).Trim(), out int cur) && cur is >= 0 and <= 2)
                    {
                        _diffAutoSet = true;
                        if (cur != _diff) { _diff = cur; UpdateDiffButtons(); }
                    }
                    continue;
                }
                int c = line.IndexOf(':');
                if (c <= 0 || !int.TryParse(line.AsSpan(0, c), out int d) || d < 0 || d > 2) continue;
                foreach (var tok in line[(c + 1)..].Split(',', StringSplitOptions.RemoveEmptyEntries))
                    if (int.TryParse(tok.Trim(), out int id)) _zoneLocked[d].Add(id);
            }
            _haveLockData = true;
            RebuildAreaList();
            SelectArea(_selectedLevelId);
        }
        catch { /* best-effort */ }
    }

    // --- Per-difficulty area state ---

    private void SetDifficulty(int d)
    {
        _diffAutoSet = true;            // user chose explicitly — stop auto-jumping
        if (d == _diff) return;
        _diff = d;
        UpdateDiffButtons();
        RebuildAreaList();
        SelectArea(_selectedLevelId);
    }

    private void UpdateDiffButtons()
    {
        for (int i = 0; i < 3; i++)
        {
            bool on = i == _diff;
            _diffBtns[i].Background = on ? Gold : Panel;
            _diffBtns[i].Foreground = on ? Brushes.Black : Muted;
            _diffBtns[i].FontWeight = on ? FontWeights.Bold : FontWeights.Normal;
        }
    }

    // True if the area is locked (red) at the viewed difficulty.
    // DLL's ground truth; when no lock data exists (plain standalone / not loaded)
    // every area reads as open.
    private bool IsLocked(int areaId)
        => _haveLockData && _zoneLocked[_diff].Contains(areaId);

    // Every check that belongs to an area at the viewed difficulty (kills, hunts,
    // area-entry/"connecting" quests, waypoints, story), with its checked state.
    // A check "exists" if d2_locations.json has its id — that's the canonical
    // full set, so ALL quest types show, not just the ones the run's MISSING:
    // universe happened to deliver.
    private List<(string Name, bool Done, int Qid)> ChecksFor(int areaId)
    {
        // The name table is the whole DATAPACKAGE; a seed is a subset of it.
        // Full Normal run has no Nightmare or Hell checks at all, so switching
        // the difficulty bar to Hell used to list checks that cannot exist —
        // permanently unticked, and (since the hint buttons) offering to spend
        // a click on a location the server answers "appears to not exist in
        // this multiworld" for.
        
        // _activeIds is the run's own universe (from MISSING:) — totals count
        // against it, not against everything the game could define.
        var list = new List<(string, bool, int)>();
        foreach (var kv in D2QuestArea)
        {
            // With the super-unique shuffle on, a hunt's target stands on some
            // other boss's spawn point, so the check belongs to THAT area. The
            // static table is only right when the shuffle did not move it.
            int area = _huntAreaOverride.TryGetValue(kv.Key, out int moved) ? moved : kv.Value;
            if (area != areaId) continue;
            long locId = 42000 + kv.Key + _diff * 1000;
            if (!_locNames.TryGetValue(locId, out var name)) continue;     // not a real check here/diff
            // Name the host too, so the panel answers "who do I kill" as well
            // as "where" — a shuffled boss wears its own name, not the host's.
            if (_huntHost.TryGetValue(kv.Key, out var host) && host.Length > 0)
                name = $"{name}  (at {host}'s spot)";
            list.Add((name, _checkedIds.Contains(locId), kv.Key));
        }
        return list.OrderBy(t => t.Item2).ThenBy(t => t.Item1, StringComparer.OrdinalIgnoreCase).ToList();
    }

    // --- Area list (left) — the full catalogue, grouped by act ---

    private void RebuildAreaList()
    {
        int keep = _selectedLevelId;
        _areaList.Items.Clear();
        int curAct = -1;
        // Order = the order this seed actually opens up in.
        int Band(int id) => _entryRegion.TryGetValue(id, out var ar) ? ar.Region : 99;
        foreach (var id in D2Cat.Keys
                     .OrderBy(k => D2Cat[k].Act)
                     .ThenBy(Band)
                     .ThenBy(k => k))
        {
            var (name, act) = D2Cat[id];
            if (act != curAct)
            {
                curAct = act;
                _areaList.Items.Add(new ListBoxItem
                {
                    Content = new TextBlock
                    {
                        Text = $"ACT {act}", Foreground = Muted, FontWeight = FontWeights.Bold,
                        FontSize = 10, Margin = new Thickness(2, 8, 0, 2),
                    },
                    IsHitTestVisible = false, Focusable = false, Padding = new Thickness(8, 0, 0, 0),
                });
            }

            bool locked = IsLocked(id);
            var checks = ChecksFor(id);
            int done = checks.Count(c => c.Done), total = checks.Count;
            bool here = _player?.LevelId == id;

            var row = new DockPanel { LastChildFill = true };
            var dot = new Ellipse { Width = 8, Height = 8, Fill = locked ? LockedRed : OpenGreen,
                                    VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 7, 0) };
            DockPanel.SetDock(dot, Dock.Left);
            row.Children.Add(dot);
            if (total > 0)
            {
                var prog = new TextBlock
                {
                    Text = $"{done}/{total}", Foreground = done >= total && total > 0 ? CheckDone : Muted,
                    FontSize = 11, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(6, 0, 0, 0),
                };
                DockPanel.SetDock(prog, Dock.Right);
                row.Children.Add(prog);
            }
            int band = Band(id);
            if (band is >= 1 and <= 5)
            {
                var tag = new TextBlock
                {
                    Text = "R" + band,
                    Foreground = Muted, FontSize = 10,
                    VerticalAlignment = VerticalAlignment.Center,
                    Margin = new Thickness(6, 0, 0, 0),
                    ToolTip = band == 1
                        ? "Open from the start of the act"
                        : $"Behind gate {band - 1} of this act",
                };
                DockPanel.SetDock(tag, Dock.Right);
                row.Children.Add(tag);
            }
            row.Children.Add(new TextBlock
            {
                Text = (here ? "▸ " : "") + name,
                Foreground = locked ? LockedRed : Brushes.White,
                FontWeight = here ? FontWeights.Bold : FontWeights.Normal,
                FontSize = 12, TextTrimming = TextTrimming.CharacterEllipsis,
                VerticalAlignment = VerticalAlignment.Center,
            });

            _areaList.Items.Add(new ListBoxItem
            {
                Content = row, Tag = id, Foreground = Brushes.White,
                Padding = new Thickness(8, 2, 6, 2),
                Background = id == keep ? SelBg : Brushes.Transparent,
            });
        }
        // Restore selection highlight without re-entrancy.
        foreach (var it in _areaList.Items)
            if (it is ListBoxItem li && li.Tag is int t && t == keep) { _areaList.SelectedItem = li; break; }
        UpdateStatusBar();
    }

    private void SelectArea(int levelId)
    {
        _selectedLevelId = levelId;
        foreach (var it in _areaList.Items)
            if (it is ListBoxItem li && li.Tag is int t)
                li.Background = t == levelId ? SelBg : Brushes.Transparent;

        DrawCollision();
        DrawOverlay();
        DrawInfo();
        UpdateStatusBar();
    }

    // --- Map rendering (collision + overlay) ---

    private D2MapArea? CurrentMap
        => _world.Areas.TryGetValue(_selectedLevelId, out var a) ? a : null;

    private void DrawCollision()
    {
        var a = CurrentMap;
        if (a?.Walkable == null || a.Width <= 0 || a.Height <= 0)
        {
            _mapImage.Source = null;
            _overlay.Children.Clear();
            _emptyHint.Text = D2Cat.TryGetValue(_selectedLevelId, out var c)
                ? $"{c.Name}\n\nNot explored yet —\nenter this area in-game to reveal the map."
                : "Not explored yet.";
            _emptyHint.Visibility = Visibility.Visible;
            return;
        }
        _emptyHint.Visibility = Visibility.Collapsed;

        int w = a.Width, h = a.Height;
        var bmp = new WriteableBitmap(w, h, 96, 96, PixelFormats.Bgra32, null);
        var px  = new byte[w * h * 4];
        var walk  = a.Walkable;
        var known = a.Known;
        for (int i = 0; i < w * h; i++)
        {
            Color cc = (walk  != null && walk[i])  ? FloorColor
                     : (known != null && known[i]) ? DimColor
                     :                                VoidColor;
            int o = i * 4;
            px[o + 0] = cc.B; px[o + 1] = cc.G; px[o + 2] = cc.R; px[o + 3] = 0xFF;
        }
        bmp.WritePixels(new Int32Rect(0, 0, w, h), px, w * 4, 0);
        _mapImage.Source = bmp;
        _mapStack.Width  = w;
        _mapStack.Height = h;
        RenderOptions.SetBitmapScalingMode(_mapImage, BitmapScalingMode.NearestNeighbor);
    }

    private void DrawOverlay()
    {
        var a = CurrentMap;
        _overlay.Children.Clear();
        if (a == null) return;
        _overlay.Width  = a.Width;
        _overlay.Height = a.Height;

        foreach (var e in a.Exits)
        {
            bool tlock = IsLocked(e.TargetLevelId);
            AddMarker(e.X - a.OriginX, e.Y - a.OriginY, tlock ? LockedRed : OpenGreen, 3.6,
                      $"→ {e.TargetName}{(tlock ? " (locked)" : "")}", square: true);
        }

        if (_showMarkers)
            foreach (var p in a.Pois)
                AddMarker(p.X - a.OriginX, p.Y - a.OriginY, MarkerBrush(p.Kind), 2.2, p.Kind);

        if (_player != null && _player.LevelId == a.LevelId)
        {
            double dx = _player.X - a.OriginX, dy = _player.Y - a.OriginY;
            var d = new Ellipse { Width = 5, Height = 5, Fill = PlayerDot, Stroke = Brushes.White, StrokeThickness = 0.5 };
            Canvas.SetLeft(d, dx - 2.5);
            Canvas.SetTop(d, dy - 2.5);
            _overlay.Children.Add(d);
        }
    }

    private void AddMarker(double x, double y, Brush fill, double r, string tip, bool square = false)
    {
        FrameworkElement m = square
            ? new Rectangle { Width = r * 2, Height = r * 2, Fill = fill }
            : new Ellipse   { Width = r * 2, Height = r * 2, Fill = fill };
        m.ToolTip = tip;
        Canvas.SetLeft(m, x - r);
        Canvas.SetTop(m, y - r);
        _overlay.Children.Add(m);
    }

    private static Brush MarkerBrush(string kind) => kind switch
    {
        "Shrine"   => new SolidColorBrush(Color.FromRgb(0x55, 0xC8, 0xFF)),
        "Chest"    => new SolidColorBrush(Color.FromRgb(0xE6, 0xB8, 0x4C)),
        "Waypoint" => new SolidColorBrush(Color.FromRgb(0x70, 0x90, 0xFF)),
        "Barrel"   => new SolidColorBrush(Color.FromRgb(0xC8, 0x80, 0x40)),
        "Urn"      => new SolidColorBrush(Color.FromRgb(0xA8, 0x88, 0x58)),
        "Well"     => new SolidColorBrush(Color.FromRgb(0x60, 0xB8, 0xC8)),
        _          => new SolidColorBrush(Color.FromRgb(0xC0, 0xA8, 0x70)),
    };

    // --- Info panel (right) — lock state + gate + checklist ---

    private void DrawInfo()
    {
        _info.Children.Clear();
        if (!D2Cat.TryGetValue(_selectedLevelId, out var cat)) return;

        _info.Children.Add(Header(cat.Name));

        bool locked = IsLocked(_selectedLevelId);
        if (_haveLockData)
            _info.Children.Add(Note(locked ? "🔴 Locked — no access yet" : "🟢 Open — accessible",
                                    locked ? LockedRed : OpenGreen));
        if (locked)
        {
            string gate = D2GateHint(_selectedLevelId);
            if (gate.Length > 0) _info.Children.Add(Body(gate, Muted));
        }

        var checks = ChecksFor(_selectedLevelId);
        int done = checks.Count(c => c.Done);
        _info.Children.Add(Note($"Checks: {done} / {checks.Count}",
                                checks.Count > 0 && done >= checks.Count ? CheckDone : Muted));

        if (CurrentMap == null)
            _info.Children.Add(Body("Map not revealed — enter this area in-game.", Muted));

        if (checks.Count > 0)
        {
            _info.Children.Add(SubHeader("Checklist"));
            foreach (var c in checks)
                _info.Children.Add(ChecklistRow(c.Name, c.Done, c.Qid));
        }
        else
        {
            _info.Children.Add(Body("No tracked checks in this area.", Muted));
        }

        // Connections discovered by walking between areas (entrance/exit dots).
        var map = CurrentMap;
        if (map != null && map.Exits.Count > 0)
        {
            _info.Children.Add(SubHeader("Leads to"));
            foreach (var e in map.Exits.GroupBy(e => e.TargetLevelId).Select(g => g.First())
                                       .OrderBy(e => e.TargetName, StringComparer.OrdinalIgnoreCase))
            {
                bool tlock = IsLocked(e.TargetLevelId);
                _info.Children.Add(Body((tlock ? "🔒 " : "→ ") + e.TargetName, tlock ? LockedRed : Brushes.White));
            }
        }
    }

    // One checklist line: the check, plus the two things you can do to it.
    
    // A checklist entry is a LOCATION, not an item, so the pair is
    // "!hint_location" (what is in here) and "send_location" (mark it found).
    // Using the item-side commands would be wrong in a way that is easy to
    // miss: !getitem on a location holding ANOTHER player's item hands you a
    // copy while the real one stays unfound, and the multiworld drifts apart.
    
    // Buttons are hidden entirely when not connected or when the check is
    // already done — there is nothing to hint or force, and a greyed button
    // with no explanation is just noise.
    private UIElement ChecklistRow(string name, bool done, int qid)
    {
        var text = new TextBlock
        {
            Text = (done ? "✔ " : "▢ ") + name,
            Foreground = done ? CheckDone : Brushes.White,
            FontSize = 13, TextWrapping = TextWrapping.Wrap,
            VerticalAlignment = VerticalAlignment.Center,
        };
        // A finished check has nothing left to hint or force — that row really
        // is just text.
        if (done)
        {
            text.Margin = new Thickness(0, 1, 0, 1);
            return text;
        }

        // Buttons stay without a session; only the send-commands need one.
        bool ap    = _apConnected && SendServerCommand != null;
        bool alone = !ap && _standalone && ForceCheckLocal != null;

        var row = new DockPanel { LastChildFill = true, Margin = new Thickness(0, 2, 0, 2) };

        var cheatEdge = new SolidColorBrush(Color.FromRgb(0xFF, 0x8A, 0x80));
        var cheat = ActionButton("Cheat", LockedRed, cheatEdge, ap || alone,
            ap
                ? "Mark this check as found. The item goes to whoever it belongs to, "
                  + "exactly as if you had picked it up. Needs the room admin password."
                : alone
                    ? "Complete this check in your standalone game — the mod grants "
                      + "the reward exactly as if you had cleared it."
                    : "Start a game (Archipelago or standalone) to use this.");
        cheat.Click += (_, _) => { if (ap) DoCheatLocation(name); else DoCheatLocal(name, qid); };
        DockPanel.SetDock(cheat, Dock.Right);
        row.Children.Add(cheat);

        // hint_cost can be 0, which means the host made hints free — not that
        // the price is unknown.
        bool free = _hintCostPoints <= 0;
        bool affordable = free || _hintPoints >= _hintCostPoints;
        string? reward = alone ? SpoilerRewardFor(name) : null;
        bool canHint = ap ? affordable : (alone && reward != null);
        string label = ap && !free ? $"Hint {_hintCostPoints}p" : "Hint";
        var hint = ActionButton(label, Gold,
            new SolidColorBrush(Color.FromRgb(0xFF, 0xDD, 0x95)), canHint,
            ap
                ? free
                    ? "Ask the server what is in this check. Hints are free in this room."
                    : affordable
                        ? $"Ask the server what is in this check. Costs {_hintCostPoints} of your {_hintPoints} points."
                        : $"Needs {_hintCostPoints} hint points — you have {_hintPoints}."
                : alone
                    ? reward != null
                        ? "Show what this check will give. Free — standalone rewards are "
                          + "fixed per character, so there is nothing to spend."
                        : "No reward spoiler for this character yet. Load the character "
                          + "once in game and it appears."
                    : "Start a game (Archipelago or standalone) to use this.");
        hint.Click += (_, _) => { if (ap) DoHintLocation(name); else DoHintLocal(name, reward); };
        DockPanel.SetDock(hint, Dock.Right);
        row.Children.Add(hint);

        row.Children.Add(text);
        return row;
    }

    // Filled, not outlined.
    // panel's own near-black (#1A1E30) and then dropped the whole control to
    // 50% opacity when disabled — the result was invisible against the
    // background, which is exactly what got reported.
    // MEANING (gold = spend points, red = irreversible) and contrast carries
    // the STATE, instead of contrast trying to do both.
    private static readonly Brush BtnDisabledBg   = new SolidColorBrush(Color.FromRgb(0x39, 0x3F, 0x55));
    private static readonly Brush BtnDisabledFg   = new SolidColorBrush(Color.FromRgb(0xD2, 0xD6, 0xE4));
    private static readonly Brush BtnDisabledEdge = new SolidColorBrush(Color.FromRgb(0x5A, 0x62, 0x80));

    private static Button ActionButton(string label, Brush fill, Brush edge, bool enabled,
                                       string tip) => new()
    {
        Content = label,
        FontSize = 11,
        FontWeight = FontWeights.SemiBold,
        Padding = new Thickness(9, 2, 9, 2),
        Margin = new Thickness(5, 0, 0, 0),
        MinWidth = 58,
        Background  = enabled ? fill : BtnDisabledBg,
        Foreground  = enabled ? Brushes.Black : BtnDisabledFg,
        BorderBrush = enabled ? edge : BtnDisabledEdge,
        BorderThickness = new Thickness(1),
        Cursor = System.Windows.Input.Cursors.Hand,
        VerticalAlignment = VerticalAlignment.Center,
        IsEnabled = enabled,
        ToolTip = tip,
    };

    private void DoHintLocation(string location)
    {
        var owner = Window.GetWindow(this);
        if (!D2ApActionDialogs.ConfirmHint(owner,
                $"Hint: what is in “{location}”?", _hintCostPoints, _hintPoints))
            return;
        Send($"!hint_location {location}");
    }

    private void DoCheatLocation(string location)
    {
        var owner = Window.GetWindow(this);
        if (_slotName.Length == 0)
        {
            MessageBox.Show(owner,
                "The launcher does not know your slot name yet — reconnect and try again.",
                "Cannot cheat this check", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        // The server's send_location takes the player as a SINGLE token, so a
        // slot name with a space in it cannot be expressed.
        // sending a command that would silently address the wrong player.
        if (_slotName.Contains(' '))
        {
            MessageBox.Show(owner,
                $"Your slot name (“{_slotName}”) contains a space, and the server's "
                + "send_location command takes the player name as a single word. "
                + "Use the server console directly for this one.",
                "Cannot cheat this check", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        string? pw = D2ApActionDialogs.ConfirmCheat(owner, "Cheat this check?",
            $"Mark “{location}” as found.",
            "This sends the item to its real owner and cannot be undone. "
            + "Everyone in the multiworld sees it.");
        if (pw == null) return;

        // Two lines, in order: authenticate, then act.
        // socket in sequence, so no handshake wait is needed.
        Send($"!admin login {pw}");
        Send($"!admin send_location {_slotName} {location}");
    }

    // Standalone hint: no server, no points, no round trip — the answer is
    // already on disk in the mod's per-character spoiler.
    private void DoHintLocal(string location, string? reward)
    {
        var owner = Window.GetWindow(this);
        if (reward == null)
        {
            MessageBox.Show(owner,
                "This character has no reward spoiler yet. The mod writes it next to "
                + "the save file the first time the character is loaded.",
                "Nothing to show", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        if (!D2ApActionDialogs.ConfirmHint(owner,
                $"“{location}” gives: {reward}", 0, 0)) return;
        MessageBox.Show(owner, $"{location}\n\n→ {reward}", "Standalone hint",
                        MessageBoxButton.OK, MessageBoxImage.Information);
    }

    // Standalone cheat: hand the quest to the mod, which runs it through its
    // own completion path so the reward is really granted.
    private void DoCheatLocal(string location, int qid)
    {
        var owner = Window.GetWindow(this);
        var force = ForceCheckLocal;
        if (force == null || qid <= 0) return;
        if (!D2ApActionDialogs.ConfirmPlain(owner, "Complete this check?",
                $"Mark “{location}” as done in your standalone game.",
                "The mod grants the reward as if you had cleared it. This cannot "
                + "be undone, and it only works while the game is running on this "
                + "difficulty."))
            return;
        _ = force(qid, _diff);
    }

    private void Send(string command)
    {
        var send = SendServerCommand;
        if (send == null) return;
        _ = send(command);       // replies land in the launcher's AP message feed
    }

    // Which gate/region unlocks a locked area (from the apworld's region map).
    private static string D2GateHint(int areaId)
    {
        if (!D2AreaRegion.TryGetValue(areaId, out var ar)) return "";
        if (ar.Region <= 1) return "Opens once you reach Act " + ar.Act + ".";
        return $"Needs Act {ar.Act} Gate {ar.Region - 1} (zone key).";
    }

    // --- Tiny UI helpers ---
    private static TextBlock Header(string t) => new()
    { Text = t, Foreground = Gold, FontSize = 18, FontWeight = FontWeights.Bold, Margin = new Thickness(0, 0, 0, 8), TextWrapping = TextWrapping.Wrap };
    private static TextBlock SubHeader(string t) => new()
    { Text = t.ToUpperInvariant(), Foreground = Muted, FontSize = 11, FontWeight = FontWeights.Bold, Margin = new Thickness(0, 12, 0, 4) };
    private static TextBlock Body(string t, Brush? fg = null) => new()
    { Text = t, Foreground = fg ?? Brushes.White, FontSize = 13, Margin = new Thickness(0, 1, 0, 1), TextWrapping = TextWrapping.Wrap };
    private static TextBlock Note(string t, Brush fg) => new()
    { Text = t, Foreground = fg, FontSize = 13, FontWeight = FontWeights.SemiBold, Margin = new Thickness(0, 2, 0, 2), TextWrapping = TextWrapping.Wrap };

    // --- DLL map-export parsing (collision + room bounds + objects) ---

    private static Dictionary<int, string>? s_objCat;

    private static void EnsureObjCat(string mapDir)
    {
        if (s_objCat != null) return;
        var map = new Dictionary<int, string>();
        try
        {
            string gameDir = Path.GetFullPath(Path.Combine(mapDir, "..", ".."));
            string objTxt  = Path.Combine(gameDir, "data", "global", "excel", "objects.txt");
            if (File.Exists(objTxt))
            {
                var lines = File.ReadAllLines(objTxt);
                if (lines.Length > 1)
                {
                    var hdr = lines[0].Split('\t');
                    int ni = Array.IndexOf(hdr, "Name"); if (ni < 0) ni = 0;
                    for (int row = 1; row < lines.Length; row++)
                    {
                        var cells = lines[row].Split('\t');
                        if (cells.Length <= ni) continue;
                        string? cat = CategorizeObject(cells[ni]);
                        if (cat != null) map[row - 1] = cat;   // txtFileNo = data-row index (0-based)
                    }
                }
            }
        }
        catch { /* no objects.txt → no object markers */ }
        s_objCat = map;
    }

    // Keep only interactable objects worth a marker; decorations (torches, fires,
    // ambient sound, dummies) return null = skipped, so the map stays clean.
    private static string? CategorizeObject(string name)
    {
        string n = name.ToLowerInvariant();
        if (n.Contains("dummy") || n.Contains("torch") || n.Contains("fire") ||
            n.Contains("brazier") || n.Contains("ambient") || n.Contains("light")) return null;
        if (n.Contains("shrine"))   return "Shrine";
        if (n.Contains("waypoint")) return "Waypoint";
        if (n.Contains("chest"))    return "Chest";
        if (n.Contains("barrel"))   return "Barrel";
        if (n.Contains("urn") || n.Contains("jar") || n.Contains("coffin")) return "Urn";
        if (n.Contains("stash"))    return "Stash";
        if (n.Contains("well"))     return "Well";
        if (n.Contains("cairn") || n.Contains("tome") || n.Contains("portal")) return "Special";
        return null;
    }

    private static D2MapWorld BuildWorldFromDir(string dir)
    {
        var world = new D2MapWorld();
        foreach (var path in Directory.GetFiles(dir, "level_*.map"))
        {
            string stem = Path.GetFileNameWithoutExtension(path);
            if (!stem.StartsWith("level_", StringComparison.Ordinal)) continue;
            if (!int.TryParse(stem.AsSpan(6), out int levelId)) continue;

            var roomsC = new List<(int px, int py, int sx, int sy, byte[] bits)>();
            var roomsR = new List<(int px, int py, int sx, int sy)>();
            var objs   = new List<(int id, int px, int py)>();
            var exits  = new List<(int x, int y, int target)>();
            string[] lines;
            try { lines = File.ReadAllLines(path); } catch { continue; }
            foreach (var line in lines)
            {
                var p = line.Split(',');
                if (p.Length < 2) continue;
                if (p[0] == "O")
                {
                    if (p.Length >= 4 && int.TryParse(p[1], out int oid) &&
                        int.TryParse(p[2], out int opx) && int.TryParse(p[3], out int opy))
                        objs.Add((oid, opx, opy));
                    continue;
                }
                if (p[0] == "X")                                  // X,x,y,targetLevelId (crossing)
                {
                    if (p.Length >= 4 && int.TryParse(p[1], out int ex) &&
                        int.TryParse(p[2], out int ey) && int.TryParse(p[3], out int et))
                        exits.Add((ex, ey, et));
                    continue;
                }
                if (p.Length < 6) continue;
                if (!int.TryParse(p[1], out int px) || !int.TryParse(p[2], out int py) ||
                    !int.TryParse(p[3], out int sx) || !int.TryParse(p[4], out int sy)) continue;
                if (sx <= 0 || sy <= 0 || sx > 2048 || sy > 2048) continue;
                if      (p[0] == "C") roomsC.Add((px, py, sx, sy, HexToBytes(p[5])));
                else if (p[0] == "R") roomsR.Add((px, py, sx, sy));
            }
            if (roomsC.Count == 0 && roomsR.Count == 0) continue;

            int minX = int.MaxValue, minY = int.MaxValue, maxX = int.MinValue, maxY = int.MinValue;
            void Extend(int px, int py, int sx, int sy)
            {
                if (px < minX) minX = px;
                if (py < minY) minY = py;
                if (px + sx > maxX) maxX = px + sx;
                if (py + sy > maxY) maxY = py + sy;
            }
            foreach (var r in roomsC) Extend(r.px, r.py, r.sx, r.sy);
            foreach (var r in roomsR) Extend(r.px, r.py, r.sx, r.sy);
            int w = maxX - minX, h = maxY - minY;
            if (w <= 0 || h <= 0 || w > 2048 || h > 2048) continue;

            var walk = new bool[w * h];
            var coll = new bool[w * h];
            var rect = new bool[w * h];
            foreach (var r in roomsC)
            {
                for (int cy = 0; cy < r.sy; cy++)
                for (int cx = 0; cx < r.sx; cx++)
                {
                    int gx = r.px - minX + cx, gy = r.py - minY + cy;
                    if (gx < 0 || gy < 0 || gx >= w || gy >= h) continue;
                    coll[gy * w + gx] = true;
                    int idx = cy * r.sx + cx;
                    if ((idx >> 3) < r.bits.Length && (r.bits[idx >> 3] & (1 << (idx & 7))) != 0)
                        walk[gy * w + gx] = true;
                }
            }
            foreach (var r in roomsR)
            {
                for (int cy = 0; cy < r.sy; cy++)
                for (int cx = 0; cx < r.sx; cx++)
                {
                    int gx = r.px - minX + cx, gy = r.py - minY + cy;
                    if (gx >= 0 && gy >= 0 && gx < w && gy < h) rect[gy * w + gx] = true;
                }
            }
            var known = new bool[w * h];
            for (int i = 0; i < w * h; i++) known[i] = rect[i] && !coll[i];

            var area = new D2MapArea
            {
                LevelId = levelId, Name = D2LevelName(levelId), Width = w, Height = h,
                Act = D2Cat.TryGetValue(levelId, out var c2) ? c2.Act : 0,
                OriginX = minX, OriginY = minY, Walkable = walk, Known = known,
            };
            if (s_objCat != null)
                foreach (var (id, opx, opy) in objs)
                    if (s_objCat.TryGetValue(id, out var cat))
                        area.Pois.Add(new D2MapPoi { Kind = cat, Label = cat, X = opx, Y = opy });
            foreach (var (ex, ey, et) in exits)
                area.Exits.Add(new D2MapExit { X = ex, Y = ey, TargetLevelId = et, TargetName = D2LevelName(et) });
            world.Areas[levelId] = area;
        }
        return world;
    }

    private static byte[] HexToBytes(string hex)
    {
        hex = hex.Trim();
        int n = hex.Length / 2;
        var b = new byte[n];
        for (int i = 0; i < n; i++)
            byte.TryParse(hex.AsSpan(i * 2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out b[i]);
        return b;
    }

    private static string D2LevelName(int id)
        => D2Cat.TryGetValue(id, out var c) ? c.Name : $"Level {id}";

    // --- Static D2 data (catalogue · check→area · region gating) ---

    // Every D2 area: id → (canonical name, act).
    // shows even before you enter — keyed by the in-game levelId (same id space
    // the DLL streams and the apworld gates on).
    private static readonly Dictionary<int, (string Name, int Act)> D2Cat = new()
    {
        [1]=("Rogue Encampment",1),[2]=("Blood Moor",1),[3]=("Cold Plains",1),[4]=("Stony Field",1),
        [5]=("Dark Wood",1),[6]=("Black Marsh",1),[7]=("Tamoe Highland",1),[8]=("Den of Evil",1),
        [9]=("Cave Level 1",1),[10]=("Underground Passage Level 1",1),[11]=("Hole Level 1",1),
        [12]=("Hole Level 2",1),[13]=("Cave Level 2",1),[14]=("Underground Passage Level 2",1),
        [15]=("Pit Level 1",1),[16]=("Pit Level 2",1),[17]=("Burial Grounds",1),[18]=("The Crypt",1),
        [19]=("The Mausoleum",1),[20]=("Forgotten Tower",1),[21]=("Tower Cellar Level 1",1),
        [22]=("Tower Cellar Level 2",1),[23]=("Tower Cellar Level 3",1),[24]=("Tower Cellar Level 4",1),
        [25]=("Tower Cellar Level 5",1),[26]=("Monastery Gate",1),[27]=("Outer Cloister",1),
        [28]=("Barracks",1),[29]=("Jail Level 1",1),[30]=("Jail Level 2",1),[31]=("Jail Level 3",1),
        [32]=("Inner Cloister",1),[33]=("Cathedral",1),[34]=("Catacombs Level 1",1),
        [35]=("Catacombs Level 2",1),[36]=("Catacombs Level 3",1),[37]=("Catacombs Level 4",1),
        [38]=("Tristram",1),[39]=("Moo Moo Farm",1),
        [40]=("Lut Gholein",2),[41]=("Rocky Waste",2),[42]=("Dry Hills",2),[43]=("Far Oasis",2),
        [44]=("Lost City",2),[45]=("Valley of Snakes",2),[46]=("Canyon of the Magi",2),
        [47]=("Sewers Level 1",2),[48]=("Sewers Level 2",2),[49]=("Sewers Level 3",2),
        [50]=("Harem Level 1",2),[51]=("Harem Level 2",2),[52]=("Palace Cellar Level 1",2),
        [53]=("Palace Cellar Level 2",2),[54]=("Palace Cellar Level 3",2),[55]=("Stony Tomb Level 1",2),
        [56]=("Halls of the Dead Level 1",2),[57]=("Halls of the Dead Level 2",2),
        [58]=("Claw Viper Temple Level 1",2),[59]=("Stony Tomb Level 2",2),
        [60]=("Halls of the Dead Level 3",2),[61]=("Claw Viper Temple Level 2",2),
        [62]=("Maggot Lair Level 1",2),[63]=("Maggot Lair Level 2",2),[64]=("Maggot Lair Level 3",2),
        [65]=("Ancient Tunnels",2),[66]=("Tal Rasha's Tomb 1",2),[67]=("Tal Rasha's Tomb 2",2),
        [68]=("Tal Rasha's Tomb 3",2),[69]=("Tal Rasha's Tomb 4",2),[70]=("Tal Rasha's Tomb 5",2),
        [71]=("Tal Rasha's Tomb 6",2),[72]=("Tal Rasha's Tomb 7",2),[73]=("Duriel's Lair",2),
        [74]=("Arcane Sanctuary",2),
        [75]=("Kurast Docks",3),[76]=("Spider Forest",3),[77]=("Great Marsh",3),[78]=("Flayer Jungle",3),
        [79]=("Lower Kurast",3),[80]=("Kurast Bazaar",3),[81]=("Upper Kurast",3),[82]=("Kurast Causeway",3),
        [83]=("Travincal",3),[84]=("Spider Cave",3),[85]=("Spider Cavern",3),[86]=("Swampy Pit Level 1",3),
        [87]=("Swampy Pit Level 2",3),[88]=("Flayer Dungeon Level 1",3),[89]=("Flayer Dungeon Level 2",3),
        [90]=("Swampy Pit Level 3",3),[91]=("Flayer Dungeon Level 3",3),[92]=("Sewers Level 1",3),
        [93]=("Sewers Level 2",3),[94]=("Ruined Temple",3),[95]=("Disused Fane",3),
        [96]=("Forgotten Reliquary",3),[97]=("Forgotten Temple",3),[98]=("Ruined Fane",3),
        [99]=("Disused Reliquary",3),[100]=("Durance of Hate Level 1",3),[101]=("Durance of Hate Level 2",3),
        [102]=("Durance of Hate Level 3",3),
        [103]=("The Pandemonium Fortress",4),[104]=("Outer Steppes",4),[105]=("Plains of Despair",4),
        [106]=("City of the Damned",4),[107]=("River of Flame",4),[108]=("Chaos Sanctuary",4),
        [109]=("Harrogath",5),[110]=("Bloody Foothills",5),[111]=("Frigid Highlands",5),
        [112]=("Arreat Plateau",5),[113]=("Crystalline Passage",5),[114]=("Frozen River",5),
        [115]=("Glacial Trail",5),[116]=("Drifter Cavern",5),[117]=("Frozen Tundra",5),
        [118]=("The Ancients' Way",5),[119]=("Icy Cellar",5),[120]=("Arreat Summit",5),
        [121]=("Nihlathak's Temple",5),[122]=("Halls of Anguish",5),[123]=("Halls of Pain",5),
        [124]=("Halls of Vaught",5),[125]=("Abaddon",5),[126]=("Pit of Acheron",5),
        [127]=("Infernal Pit",5),[128]=("Worldstone Keep Level 1",5),[129]=("Worldstone Keep Level 2",5),
        [130]=("Worldstone Keep Level 3",5),[131]=("Throne of Destruction",5),
        [132]=("The Worldstone Chamber",5),
    };

    // quest/check id -> area id.
    private static Dictionary<int, int> D2QuestArea => D2LogicTables.QuestZone;


    // area id → (act, region number).
    // act; region N (>1) needs that act's Gate N-1 key.
    // ACT_REGIONS — used only for the "needs Gate X" hint.
    private static readonly Dictionary<int, (int Act, int Region)> D2AreaRegion = BuildAreaRegion();

    private static Dictionary<int, (int, int)> BuildAreaRegion()
    {
        var d = new Dictionary<int, (int, int)>();
        void R(int act, int region, params int[] zones) { foreach (var z in zones) d[z] = (act, region); }
        R(1,1, 1,2,3,8,9,13,17,18,19);  R(1,2, 4,5,10,14,38);
        R(1,3, 6,7,11,12,15,16,20,21,22,23,24,25,26);  R(1,4, 27,28,29,30,31,32);  R(1,5, 33,34,35,36,37);
        R(2,1, 40,41,47,48,49,50);  R(2,2, 42,51,55,56,57,59,60);  R(2,3, 43,52,53,54,62,63,64);
        R(2,4, 44,45,58,61,65,74);  R(2,5, 46,66,67,68,69,70,71,72,73);
        R(3,1, 75,76,77,84,85);  R(3,2, 78,79,86,87,88,89,90,91);  R(3,3, 80,81,92,93,94,95,96,97);
        R(3,4, 82,83,98,99);  R(3,5, 100,101,102);
        R(4,1, 103,104,105);  R(4,2, 106,107);  R(4,3, 108);
        R(5,1, 109,110,111);  R(5,2, 112,113,114);  R(5,3, 115,116,117,118,119,120);
        R(5,4, 128,129);  R(5,5, 130,131,132);
        return d;
    }
}
