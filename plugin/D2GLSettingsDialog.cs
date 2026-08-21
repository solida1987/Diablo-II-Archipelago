using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace LauncherV2.Plugins.DiabloII;

//
// D2GL (Glide / OpenGL renderer) graphics settings, surfaced in the launcher
// so the user can set fullscreen / window size / v-sync / FPS caps etc.
// game starts, instead of only via the in-game Ctrl+O menu.
// d2gl.json (authoritative); we write that and keep the human-readable
// d2gl.ini in sync.
// are exposed — everything else in both files is preserved untouched.
//
internal sealed class D2GLConfig
{
    public bool Fullscreen;
    public int  Width  = 1600;
    public int  Height = 1200;
    public bool Centered = true;
    public bool UnlockCursor;
    public bool VSync = true;
    public bool ForegroundFpsActive;
    public int  ForegroundFpsValue = 60;
    public bool BackgroundFpsActive = true;
    public int  BackgroundFpsValue = 25;
    public bool AutoMinimize;
    public bool DarkMode = true;
    public bool SkipIntro;
    public bool ShowFps;

    private static string JsonPath(string gameDir) => Path.Combine(gameDir, "d2gl.json");
    private static string IniPath(string gameDir)  => Path.Combine(gameDir, "d2gl.ini");

    // Load from d2gl.json (authoritative); falls back to d2gl.ini, then defaults.
    public static D2GLConfig Load(string gameDir)
    {
        var c = new D2GLConfig();
        try
        {
            string jp = JsonPath(gameDir);
            if (File.Exists(jp) && JsonNode.Parse(File.ReadAllText(jp)) is JsonObject root)
            {
                if (root["screen"] is JsonObject s)
                {
                    c.Fullscreen          = Bool(s, "window_fullscreen",     c.Fullscreen);
                    c.Width               = Int (s, "window_size_width",     c.Width);
                    c.Height              = Int (s, "window_size_height",    c.Height);
                    c.Centered            = Bool(s, "window_centered",       c.Centered);
                    c.UnlockCursor        = Bool(s, "unlock_cursor",         c.UnlockCursor);
                    c.VSync               = Bool(s, "window_vsync",          c.VSync);
                    c.ForegroundFpsActive = Bool(s, "foreground_fps_active", c.ForegroundFpsActive);
                    c.ForegroundFpsValue  = Int (s, "foreground_fps_value",  c.ForegroundFpsValue);
                    c.BackgroundFpsActive = Bool(s, "background_fps_active", c.BackgroundFpsActive);
                    c.BackgroundFpsValue  = Int (s, "background_fps_value",  c.BackgroundFpsValue);
                    c.AutoMinimize        = Bool(s, "auto_minimize",         c.AutoMinimize);
                    c.DarkMode            = Bool(s, "window_dark_mode",      c.DarkMode);
                }
                if (root["features"] is JsonObject f)
                {
                    c.SkipIntro = Bool(f, "skip_intro", c.SkipIntro);
                    c.ShowFps   = Bool(f, "show_fps",   c.ShowFps);
                }
                return c;
            }
        }
        catch { /* fall through to ini / defaults */ }

        // INI fallback (older D2GL / json absent).
        try
        {
            string ip = IniPath(gameDir);
            if (File.Exists(ip))
            {
                var ini = ReadIni(File.ReadAllLines(ip));
                c.Fullscreen          = IniBool(ini, "Screen", "fullscreen",          c.Fullscreen);
                c.Width               = IniInt (ini, "Screen", "window_width",        c.Width);
                c.Height              = IniInt (ini, "Screen", "window_height",       c.Height);
                c.Centered            = IniBool(ini, "Screen", "centered_window",     c.Centered);
                c.UnlockCursor        = IniBool(ini, "Screen", "unlock_cursor",       c.UnlockCursor);
                c.VSync               = IniBool(ini, "Screen", "vsync",               c.VSync);
                c.ForegroundFpsActive = IniBool(ini, "Screen", "foreground_fps",      c.ForegroundFpsActive);
                c.ForegroundFpsValue  = IniInt (ini, "Screen", "foreground_fps_value",c.ForegroundFpsValue);
                c.BackgroundFpsActive = IniBool(ini, "Screen", "background_fps",      c.BackgroundFpsActive);
                c.BackgroundFpsValue  = IniInt (ini, "Screen", "background_fps_value",c.BackgroundFpsValue);
                c.AutoMinimize        = IniBool(ini, "Screen", "auto_minimize",       c.AutoMinimize);
                c.DarkMode            = IniBool(ini, "Screen", "dark_mode",           c.DarkMode);
                c.SkipIntro           = IniBool(ini, "Feature", "skip_intro",         c.SkipIntro);
                c.ShowFps             = IniBool(ini, "Feature", "show_fps",           c.ShowFps);
            }
        }
        catch { /* defaults */ }
        return c;
    }

    // Persist to d2gl.json (authoritative) AND keep d2gl.ini in sync, preserving
    // every other key/section in both files.
    public void Save(string gameDir)
    {
        // d2gl.json — modify the screen/features objects in place so the rest
        // (graphics, other, …) is preserved exactly.
        try
        {
            string jp = JsonPath(gameDir);
            JsonObject root = (File.Exists(jp) ? JsonNode.Parse(File.ReadAllText(jp)) : null)
                              as JsonObject ?? new JsonObject();

            JsonObject screen;
            if (root["screen"] is JsonObject es) screen = es;
            else { screen = new JsonObject(); root["screen"] = screen; }
            screen["window_fullscreen"]     = Fullscreen;
            screen["window_size_width"]     = Width;
            screen["window_size_height"]    = Height;
            screen["window_centered"]       = Centered;
            screen["unlock_cursor"]         = UnlockCursor;
            screen["window_vsync"]          = VSync;
            screen["foreground_fps_active"] = ForegroundFpsActive;
            screen["foreground_fps_value"]  = ForegroundFpsValue;
            screen["background_fps_active"] = BackgroundFpsActive;
            screen["background_fps_value"]  = BackgroundFpsValue;
            screen["auto_minimize"]         = AutoMinimize;
            screen["window_dark_mode"]      = DarkMode;

            JsonObject feat;
            if (root["features"] is JsonObject ef) feat = ef;
            else { feat = new JsonObject(); root["features"] = feat; }
            feat["skip_intro"] = SkipIntro;
            feat["show_fps"]   = ShowFps;

            File.WriteAllText(jp, root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        }
        catch { /* non-fatal */ }

        // d2gl.ini — replace the matching values in place (keeps comments/structure).
        try
        {
            string ip = IniPath(gameDir);
            if (File.Exists(ip))
            {
                var lines = new List<string>(File.ReadAllLines(ip));
                SetIni(lines, "Screen", "fullscreen",           B(Fullscreen));
                SetIni(lines, "Screen", "window_width",         Width.ToString(CultureInfo.InvariantCulture));
                SetIni(lines, "Screen", "window_height",        Height.ToString(CultureInfo.InvariantCulture));
                SetIni(lines, "Screen", "centered_window",      B(Centered));
                SetIni(lines, "Screen", "unlock_cursor",        B(UnlockCursor));
                SetIni(lines, "Screen", "vsync",                B(VSync));
                SetIni(lines, "Screen", "foreground_fps",       B(ForegroundFpsActive));
                SetIni(lines, "Screen", "foreground_fps_value", ForegroundFpsValue.ToString(CultureInfo.InvariantCulture));
                SetIni(lines, "Screen", "background_fps",       B(BackgroundFpsActive));
                SetIni(lines, "Screen", "background_fps_value", BackgroundFpsValue.ToString(CultureInfo.InvariantCulture));
                SetIni(lines, "Screen", "auto_minimize",        B(AutoMinimize));
                SetIni(lines, "Screen", "dark_mode",            B(DarkMode));
                SetIni(lines, "Feature", "skip_intro",          B(SkipIntro));
                SetIni(lines, "Feature", "show_fps",            B(ShowFps));
                File.WriteAllLines(ip, lines);
            }
        }
        catch { /* non-fatal */ }
    }

    private static string B(bool v) => v ? "true" : "false";

    // --- JSON helpers ---
    private static bool Bool(JsonObject o, string k, bool def)
    {
        try { return o[k] is JsonNode n ? n.GetValue<bool>() : def; } catch { return def; }
    }
    private static int Int(JsonObject o, string k, int def)
    {
        try { return o[k] is JsonNode n ? n.GetValue<int>() : def; } catch { return def; }
    }

    // --- INI helpers ---
    private static Dictionary<string, string> ReadIni(IEnumerable<string> lines)
    {
        var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        string cur = "";
        foreach (string raw in lines)
        {
            string t = raw.Trim();
            if (t.Length == 0 || t.StartsWith(";")) continue;
            if (t.StartsWith("[") && t.EndsWith("]")) { cur = t.Substring(1, t.Length - 2).Trim(); continue; }
            int eq = t.IndexOf('=');
            if (eq <= 0) continue;
            map[cur + "/" + t.Substring(0, eq).Trim()] = t[(eq + 1)..].Trim();
        }
        return map;
    }
    private static bool IniBool(Dictionary<string, string> m, string sec, string key, bool def)
        => m.TryGetValue(sec + "/" + key, out var v) ? v.Equals("true", StringComparison.OrdinalIgnoreCase) : def;
    private static int IniInt(Dictionary<string, string> m, string sec, string key, int def)
        => m.TryGetValue(sec + "/" + key, out var v) && int.TryParse(v, out int n) ? n : def;

    private static void SetIni(List<string> lines, string section, string key, string value)
    {
        string cur = "";
        for (int i = 0; i < lines.Count; i++)
        {
            string t = lines[i].Trim();
            if (t.StartsWith("[") && t.EndsWith("]")) { cur = t.Substring(1, t.Length - 2).Trim(); continue; }
            if (t.StartsWith(";") || !string.Equals(cur, section, StringComparison.OrdinalIgnoreCase)) continue;
            int eq = t.IndexOf('=');
            if (eq <= 0) continue;
            if (string.Equals(t.Substring(0, eq).Trim(), key, StringComparison.OrdinalIgnoreCase))
            {
                lines[i] = key + "=" + value;
                return;
            }
        }
    }
}

// The dark settings dialog for D2GLConfig — built in code to match
// the rest of the D2 plugin UI.
internal sealed class D2GLSettingsDialog : Window
{
    private static readonly Brush Muted  = Frozen(0x72, 0x7A, 0x99);
    private static readonly Brush Fg      = Frozen(0xCC, 0xD0, 0xE0);
    private static readonly Brush InputBg = Frozen(0x0C, 0x10, 0x20);
    private static readonly Brush BtnBg   = Frozen(0x1A, 0x1E, 0x30);
    private static readonly Brush Border  = Frozen(0x2A, 0x30, 0x50);
    private static readonly Brush Accent  = Frozen(0x7A, 0x10, 0x10);

    private readonly string _gameDir;
    private readonly D2GLConfig _c;

    private CheckBox _full = null!, _centered = null!, _unlock = null!, _vsync = null!,
                     _fgFps = null!, _bgFps = null!, _autoMin = null!, _dark = null!,
                     _skipIntro = null!, _showFps = null!;
    private TextBox  _w = null!, _h = null!, _fgVal = null!, _bgVal = null!;

    public static void ShowFor(Window? owner, string gameDir)
    {
        var dlg = new D2GLSettingsDialog(gameDir) { Owner = owner };
        dlg.ShowDialog();
    }

    private D2GLSettingsDialog(string gameDir)
    {
        _gameDir = gameDir;
        _c = D2GLConfig.Load(gameDir);

        Title = "Diablo II — Graphics (D2GL)";
        Width = 460;
        SizeToContent = SizeToContent.Height;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Background = new SolidColorBrush(Color.FromRgb(0x07, 0x0A, 0x14));
        ResizeMode = ResizeMode.NoResize;

        var root = new StackPanel { Margin = new Thickness(20, 16, 20, 16) };

        root.Children.Add(new TextBlock
        {
            Text = "D2GL graphics", FontSize = 17, FontWeight = FontWeights.Bold, Foreground = Fg,
        });
        root.Children.Add(new TextBlock
        {
            Text = "Set the renderer's window + display options here instead of the in-game " +
                   "Ctrl+O menu. Saved to d2gl.json (and d2gl.ini) — applied next launch.",
            FontSize = 11, Foreground = Muted, TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 4, 0, 12),
        });

        root.Children.Add(Section("WINDOW"));
        _full     = Check("Fullscreen (windowed if off)", _c.Fullscreen);
        root.Children.Add(_full);

        var sizeRow = new DockPanel { Margin = new Thickness(0, 2, 0, 6) };
        sizeRow.Children.Add(new TextBlock { Text = "Window size", Foreground = Fg, FontSize = 12,
            Width = 150, VerticalAlignment = VerticalAlignment.Center });
        _w = Num(_c.Width, 70);
        _h = Num(_c.Height, 70);
        sizeRow.Children.Add(_w);
        sizeRow.Children.Add(new TextBlock { Text = " × ", Foreground = Muted, FontSize = 12,
            VerticalAlignment = VerticalAlignment.Center });
        sizeRow.Children.Add(_h);
        sizeRow.Children.Add(new TextBlock { Text = "  (ignored in fullscreen)", Foreground = Muted,
            FontSize = 10, VerticalAlignment = VerticalAlignment.Center });
        root.Children.Add(sizeRow);

        _centered = Check("Centered window", _c.Centered);          root.Children.Add(_centered);
        _unlock   = Check("Unlock cursor (not locked to window)", _c.UnlockCursor); root.Children.Add(_unlock);
        _dark     = Check("Dark window title bar", _c.DarkMode);    root.Children.Add(_dark);
        _autoMin  = Check("Auto-minimize when fullscreen loses focus", _c.AutoMinimize); root.Children.Add(_autoMin);

        root.Children.Add(Section("FRAME RATE"));
        _vsync = Check("V-Sync (adapts to screen refresh)", _c.VSync); root.Children.Add(_vsync);
        _fgFps = Check("Cap foreground FPS (V-Sync must be off)", _c.ForegroundFpsActive);
        _fgVal = Num(_c.ForegroundFpsValue, 60);
        root.Children.Add(FpsRow(_fgFps, _fgVal));
        _bgFps = Check("Cap background FPS (when inactive)", _c.BackgroundFpsActive);
        _bgVal = Num(_c.BackgroundFpsValue, 60);
        root.Children.Add(FpsRow(_bgFps, _bgVal));

        root.Children.Add(Section("MISC"));
        _skipIntro = Check("Skip intro videos", _c.SkipIntro); root.Children.Add(_skipIntro);
        _showFps   = Check("Show FPS counter", _c.ShowFps);    root.Children.Add(_showFps);

        // Buttons
        var bar = new DockPanel { Margin = new Thickness(0, 16, 0, 0), LastChildFill = false };
        var cancel = new Button { Content = "Cancel", Width = 100, Height = 32,
            Background = BtnBg, Foreground = Fg, BorderBrush = Border, Margin = new Thickness(0, 0, 8, 0) };
        cancel.Click += (_, _) => { DialogResult = false; Close(); };
        var save = new Button { Content = "Save", Width = 130, Height = 32,
            Background = Accent, Foreground = Brushes.White, BorderBrush = Border, FontWeight = FontWeights.SemiBold };
        save.Click += OnSave;
        DockPanel.SetDock(cancel, Dock.Right);
        DockPanel.SetDock(save, Dock.Right);
        bar.Children.Add(save);
        bar.Children.Add(cancel);
        root.Children.Add(bar);

        Content = root;
    }

    private void OnSave(object sender, RoutedEventArgs e)
    {
        _c.Fullscreen          = _full.IsChecked == true;
        _c.Width               = ParseInt(_w, _c.Width);
        _c.Height              = ParseInt(_h, _c.Height);
        _c.Centered            = _centered.IsChecked == true;
        _c.UnlockCursor        = _unlock.IsChecked == true;
        _c.DarkMode            = _dark.IsChecked == true;
        _c.AutoMinimize        = _autoMin.IsChecked == true;
        _c.VSync               = _vsync.IsChecked == true;
        _c.ForegroundFpsActive = _fgFps.IsChecked == true;
        _c.ForegroundFpsValue  = ParseInt(_fgVal, _c.ForegroundFpsValue);
        _c.BackgroundFpsActive = _bgFps.IsChecked == true;
        _c.BackgroundFpsValue  = ParseInt(_bgVal, _c.BackgroundFpsValue);
        _c.SkipIntro           = _skipIntro.IsChecked == true;
        _c.ShowFps             = _showFps.IsChecked == true;
        _c.Save(_gameDir);
        DialogResult = true;
        Close();
    }

    private static int ParseInt(TextBox t, int fallback)
        => int.TryParse(t.Text.Trim(), out int v) && v > 0 ? v : fallback;

    private DockPanel FpsRow(CheckBox box, TextBox val)
    {
        var row = new DockPanel { Margin = new Thickness(0, 0, 0, 2) };
        val.Width = 60;
        DockPanel.SetDock(val, Dock.Right);
        row.Children.Add(val);
        box.Margin = new Thickness(0);
        row.Children.Add(box);
        return row;
    }

    private TextBlock Section(string text) => new()
    {
        Text = text, FontSize = 10, FontWeight = FontWeights.SemiBold, Foreground = Muted,
        Margin = new Thickness(0, 12, 0, 6),
    };

    private CheckBox Check(string text, bool isChecked) => new()
    {
        Content = text, IsChecked = isChecked, Foreground = Fg, FontSize = 12,
        Margin = new Thickness(0, 0, 0, 6),
    };

    private TextBox Num(int value, double width) => new()
    {
        Text = value.ToString(CultureInfo.InvariantCulture), Width = width, FontSize = 12,
        Background = InputBg, Foreground = Fg, BorderBrush = Border,
        Padding = new Thickness(4, 3, 4, 3), TextAlignment = TextAlignment.Center,
    };

    private static Brush Frozen(byte r, byte g, byte b)
    {
        var br = new SolidColorBrush(Color.FromRgb(r, g, b));
        br.Freeze();
        return br;
    }
}
