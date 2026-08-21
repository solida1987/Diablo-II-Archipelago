using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using Microsoft.Win32;

namespace LauncherV2.Plugins.DiabloII;

//
// "Can this seed be finished?" — pick the spoiler (and optionally the YAML)
// and get a verdict.

// The spoiler is the file that matters: it is the only place the actual item
// placements exist, and without them there is nothing to verify.
// accepted as well because it is what players have on hand, and its settings
// can be sanity-checked on their own; but a YAML alone cannot answer "is this
// completable", and the dialog says so rather than pretending.
//
public sealed class D2SeedCheckDialog : Window
{
    private readonly TextBox _spoilerBox = MakePathBox();
    private readonly TextBox _yamlBox = MakePathBox();
    private readonly TextBox _output = new()
    {
        IsReadOnly = true, TextWrapping = TextWrapping.NoWrap, AcceptsReturn = true,
        VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
        HorizontalScrollBarVisibility = ScrollBarVisibility.Auto,
        FontFamily = new System.Windows.Media.FontFamily("Consolas"), FontSize = 12,
        Background = new SolidColorBrush(Color.FromRgb(0x10, 0x13, 0x20)),
        Foreground = new SolidColorBrush(Color.FromRgb(0xDD, 0xE2, 0xF0)),
        BorderThickness = new Thickness(0), Padding = new Thickness(10),
        MinHeight = 320,
    };
    private readonly TextBlock _verdict = new()
    {
        FontSize = 15, FontWeight = FontWeights.SemiBold,
        Margin = new Thickness(0, 0, 0, 10),
        Text = "Pick a spoiler file and press Check.",
        Foreground = new SolidColorBrush(Color.FromRgb(0x9A, 0xA3, 0xBF)),
    };

    private readonly string? _gameDir;

    public static void ShowFor(Window? owner, string? gameDirectory = null)
    {
        var dlg = new D2SeedCheckDialog(gameDirectory) { Owner = owner };
        dlg.ShowDialog();
    }

    // The world seed the game itself will use, straight out of the install's
    // ini -- the same value the mod reads at character creation, and the same
    // one the Map tab uses.
    // produced an entrance layout that matched the real game in 1 case out of
    // 31.
    private uint? ReadWorldSeed()
    {
        try
        {
            if (string.IsNullOrWhiteSpace(_gameDir)) return null;
            string ini = System.IO.Path.Combine(_gameDir!, "Archipelago", "d2arch.ini");
            if (!File.Exists(ini)) return null;
            string blob = File.ReadAllText(ini);
            string Val(string key)
            {
                var m = System.Text.RegularExpressions.Regex.Match(
                    blob, @"^\s*" + key + @"\s*=\s*(\S+)",
                    System.Text.RegularExpressions.RegexOptions.Multiline |
                    System.Text.RegularExpressions.RegexOptions.IgnoreCase);
                return m.Success ? m.Groups[1].Value.Trim() : "";
            }
            if (!ulong.TryParse(Val("SeedKey"), out ulong key) || key == 0)
                ulong.TryParse(Val("ShuffleSeed"), out key);
            if (key == 0) return null;
            return unchecked((uint)(key ^ (key >> 32)));
        }
        catch { return null; }
    }

    private static TextBox MakePathBox() => new()
    {
        IsReadOnly = true, FontSize = 12, Padding = new Thickness(8, 6, 8, 6),
        Background = new SolidColorBrush(Color.FromRgb(0x16, 0x1A, 0x2C)),
        Foreground = new SolidColorBrush(Color.FromRgb(0xDD, 0xE2, 0xF0)),
        BorderBrush = new SolidColorBrush(Color.FromRgb(0x2A, 0x30, 0x50)),
        VerticalContentAlignment = VerticalAlignment.Center,
    };

    private D2SeedCheckDialog(string? gameDirectory)
    {
        _gameDir = gameDirectory;
        Title = "Diablo II — seed check";
        Width = 900; Height = 700;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Background = new SolidColorBrush(Color.FromRgb(0x0C, 0x0F, 0x1A));

        var root = new StackPanel { Margin = new Thickness(18) };
        root.Children.Add(new TextBlock
        {
            Text = "Check a generated seed",
            FontSize = 18, FontWeight = FontWeights.Bold,
            Foreground = new SolidColorBrush(Color.FromRgb(0xE8, 0xEC, 0xF8)),
            Margin = new Thickness(0, 0, 0, 4),
        });
        root.Children.Add(new TextBlock
        {
            Text = "Rebuilds the world the game will actually build — including where "
                 + "entrance shuffle moves each dungeon — and walks it to see whether "
                 + "every gate key can really be reached.",
            TextWrapping = TextWrapping.Wrap, FontSize = 12,
            Foreground = new SolidColorBrush(Color.FromRgb(0x9A, 0xA3, 0xBF)),
            Margin = new Thickness(0, 0, 0, 16),
        });

        root.Children.Add(FileRow("Spoiler file (required)", _spoilerBox,
                                  "Spoiler|*.txt|All files|*.*"));
        root.Children.Add(FileRow("YAML file (optional)", _yamlBox,
                                  "YAML|*.yaml;*.yml|All files|*.*"));

        var checkBtn = new Button
        {
            Content = "Check seed", Padding = new Thickness(16, 8, 16, 8),
            HorizontalAlignment = HorizontalAlignment.Left, FontSize = 13,
            Margin = new Thickness(0, 6, 0, 16), Cursor = System.Windows.Input.Cursors.Hand,
            Background = new SolidColorBrush(Color.FromRgb(0x27, 0x52, 0x8A)),
            Foreground = Brushes.White,
            BorderBrush = new SolidColorBrush(Color.FromRgb(0x3A, 0x6E, 0xB0)),
        };
        checkBtn.Click += (_, _) => Run();
        root.Children.Add(checkBtn);

        root.Children.Add(_verdict);
        root.Children.Add(_output);
        Content = new ScrollViewer { Content = root,
                                     VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
    }

    private UIElement FileRow(string label, TextBox box, string filter)
    {
        var panel = new StackPanel { Margin = new Thickness(0, 0, 0, 10) };
        panel.Children.Add(new TextBlock
        {
            Text = label, FontSize = 11, Margin = new Thickness(0, 0, 0, 4),
            Foreground = new SolidColorBrush(Color.FromRgb(0x9A, 0xA3, 0xBF)),
        });
        var grid = new Grid();
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        Grid.SetColumn(box, 0);
        var browse = new Button
        {
            Content = "Browse…", Padding = new Thickness(12, 6, 12, 6),
            Margin = new Thickness(8, 0, 0, 0), Cursor = System.Windows.Input.Cursors.Hand,
            Background = new SolidColorBrush(Color.FromRgb(0x1A, 0x1E, 0x30)),
            Foreground = new SolidColorBrush(Color.FromRgb(0xDD, 0xE2, 0xF0)),
            BorderBrush = new SolidColorBrush(Color.FromRgb(0x2A, 0x30, 0x50)),
        };
        browse.Click += (_, _) =>
        {
            var dlg = new Microsoft.Win32.OpenFileDialog { Filter = filter };
            if (dlg.ShowDialog() == true) box.Text = dlg.FileName;
        };
        Grid.SetColumn(browse, 1);
        grid.Children.Add(box);
        grid.Children.Add(browse);
        panel.Children.Add(grid);
        return panel;
    }

    private void Run()
    {
        try
        {
            var sb = new StringBuilder();
            string yaml = _yamlBox.Text.Trim();
            if (yaml.Length > 0 && File.Exists(yaml))
            {
                var warnings = CheckYamlSettings(File.ReadAllText(yaml));
                sb.AppendLine("YAML settings");
                sb.AppendLine(warnings.Count == 0
                    ? "  nothing suspicious in the settings themselves."
                    : string.Join("\n", warnings.Select(w => "  " + w)));
                sb.AppendLine();
            }

            string spoiler = _spoilerBox.Text.Trim();
            if (spoiler.Length == 0 || !File.Exists(spoiler))
            {
                _verdict.Text = "No spoiler — settings checked only.";
                _verdict.Foreground = new SolidColorBrush(Color.FromRgb(0xE0, 0xB0, 0x50));
                sb.AppendLine("A spoiler file is required to answer \"can this seed be");
                sb.AppendLine("completed\": the item placements only exist there. Generate");
                sb.AppendLine("with spoiler level 2 or 3 and pick the _Spoiler.txt file.");
                _output.Text = sb.ToString();
                return;
            }

            var sp = D2SeedCheck.ParseSpoiler(spoiler, D2LogicTables.LocationQuest.Keys
                .SelectMany(n => new[] { n, n + " (Nightmare)", n + " (Hell)" }));
            if (sp.D2Slots.Count == 0)
            {
                _verdict.Text = "No Diablo II slot in that spoiler.";
                _verdict.Foreground = new SolidColorBrush(Color.FromRgb(0xE0, 0x70, 0x70));
                sb.AppendLine("The spoiler contains no player whose game is \"" +
                              D2SeedCheck.GameName + "\".");
                _output.Text = sb.ToString();
                return;
            }

            bool allOk = true;
            foreach (var slot in sp.D2Slots)
            {
                var rep = D2SeedCheck.Check(sp, slot, D2LogicTables.LocationQuest,
                                            ReadWorldSeed());
                allOk &= rep.Ok;
                sb.AppendLine(new string('─', 70));
                sb.AppendLine(D2SeedCheck.Format(rep));
            }
            if (sp.PlayerCount > sp.D2Slots.Count)
            {
                sb.AppendLine(new string('─', 70));
                sb.AppendLine($"This multiworld has {sp.PlayerCount} players; " +
                              $"{sp.D2Slots.Count} of them play Diablo II. Other games' " +
                              "logic is Archipelago's own business and was not re-checked.");
            }

            _verdict.Text = allOk
                ? "APPROVED — every Diablo II slot can be completed."
                : "NOT APPROVED — see the problems below.";
            _verdict.Foreground = new SolidColorBrush(allOk
                ? Color.FromRgb(0x60, 0xC0, 0x80) : Color.FromRgb(0xE0, 0x70, 0x70));
            _output.Text = sb.ToString();
        }
        catch (Exception ex)
        {
            _verdict.Text = "The check itself failed.";
            _verdict.Foreground = new SolidColorBrush(Color.FromRgb(0xE0, 0x70, 0x70));
            _output.Text = ex.ToString();
        }
    }

    // Settings that are legal but produce a game nobody wants.
    // small: the apworld refuses the truly broken combinations at generation
    // time now, so this is about the ones it still allows.
    private static List<string> CheckYamlSettings(string yaml)
    {
        var warn = new List<string>();
        bool Has(string key, string val) =>
            yaml.Contains(key + ":") &&
            yaml.Split('\n').Any(l => l.TrimStart().StartsWith(key + ":") &&
                                      l.Split(':').Last().Trim().Equals(val,
                                          StringComparison.OrdinalIgnoreCase));

        if (Has("zone_locking", "1") && Has("entrance_shuffle", "1"))
            warn.Add("zone locking + entrance shuffle: supported, and the check " +
                     "below takes the moved entrances into account.");
        if (Has("skill_hunting", "0") && Has("zone_locking", "0"))
            warn.Add("both game modes off — nothing is randomised into the pool, " +
                     "so every check hands out filler.");
        if (Has("accessibility", "minimal"))
            warn.Add("accessibility is 'minimal': Archipelago only guarantees the " +
                     "goal is reachable, not that every check is.");
        return warn;
    }
}
