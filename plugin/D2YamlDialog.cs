using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace LauncherV2.Plugins.DiabloII;

//
// Build an Archipelago settings file without opening a text editor.

// Joining a multiworld means handing the host a YAML, and writing one by hand
// means copying a template and hoping you spelled ~150 keys the way the apworld
// spells them. A wrong key does not warn you — it fails at generation time, in
// front of everyone waiting to start.

// So the form is not written by hand either.
// D2YamlOptions.g.cs, which Tools/gen_yaml_options.py exports FROM the apworld's
// own options.py. Add an option to the apworld, re-run the tool, and it appears
// here with its real key, its real default and its real help text.
// writes can only contain keys the apworld accepts.
//
internal sealed class D2YamlDialog : Window
{
    private readonly TextBox _name = new() { Text = Environment.UserName, MinWidth = 220 };
    private readonly Dictionary<string, Func<object>> _read = new();

    private static readonly Brush Ink   = new SolidColorBrush(Color.FromRgb(0xE8, 0xE0, 0xC8));
    private static readonly Brush Dim   = new SolidColorBrush(Color.FromRgb(0x9A, 0x92, 0x80));
    private static readonly Brush Gold  = new SolidColorBrush(Color.FromRgb(0xC8, 0xA2, 0x4B));
    private static readonly Brush Panel = new SolidColorBrush(Color.FromRgb(0x1A, 0x17, 0x14));

    // Which channel this dialog was opened for. Options that only the
    // experimental apworld knows are hidden for the stable game — offering
    // them there would produce a YAML the player's apworld rejects at
    // generation time, with an error that helps nobody.
    private readonly bool _experimental;

    // Stable sees only stable's options; experimental sees everything.
    private System.Collections.Generic.IEnumerable<D2YamlOption> VisibleOptions
        => D2YamlOptions.All.Where(o => _experimental || !o.ExpOnly);

    private D2YamlDialog(bool experimental)
    {
        _experimental = experimental;
        // "YAML" by name everywhere: it is the word every Archipelago player and
        // host actually uses ("send me your YAML"), so calling it a "settings
        // file" here just made people unsure whether this was the right button.
        Title = "Create an Archipelago YAML";
        Width = 780; Height = 720;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Background = Panel;

        var root = new DockPanel { Margin = new Thickness(14) };

        // header: the slot name, which is the one field that must be right
        var head = new StackPanel { Orientation = Orientation.Vertical };
        head.Children.Add(new TextBlock
        {
            Text = "Your YAML for Diablo II Archipelago",
            Foreground = Gold, FontSize = 17, FontWeight = FontWeights.Bold,
        });
        head.Children.Add(new TextBlock
        {
            Text = "Save the YAML and send it to whoever is generating the multiworld. "
                 + "Everything below is optional — the defaults are a complete game.",
            Foreground = Dim, TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 2, 0, 10),
        });
        var nameRow = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 10) };
        nameRow.Children.Add(new TextBlock
        {
            Text = "Player name", Foreground = Ink, VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(0, 0, 8, 0), FontWeight = FontWeights.Bold,
        });
        nameRow.Children.Add(_name);
        nameRow.Children.Add(new TextBlock
        {
            Text = "  this is the slot you connect as", Foreground = Dim,
            VerticalAlignment = VerticalAlignment.Center,
        });
        head.Children.Add(nameRow);
        DockPanel.SetDock(head, Dock.Top);
        root.Children.Add(head);

        // footer
        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(0, 10, 0, 0),
        };
        var save = new Button { Content = "Save YAML…", Padding = new Thickness(16, 6, 16, 6), IsDefault = true };
        var cancel = new Button { Content = "Cancel", Padding = new Thickness(16, 6, 16, 6), Margin = new Thickness(8, 0, 0, 0), IsCancel = true };
        save.Click += (_, _) => Save();
        buttons.Children.Add(save);
        buttons.Children.Add(cancel);
        DockPanel.SetDock(buttons, Dock.Bottom);
        root.Children.Add(buttons);

        // the options themselves, grouped exactly as the apworld groups them
        var stack = new StackPanel();
        foreach (var group in VisibleOptions.GroupBy(o => o.Group))
        {
            // The long tails (32 sets, 33 runes, 54 custom-goal toggles) start
            // closed: they are all "on" by default and most players never look.
            bool longTail = group.Count() > 12 || group.Key.StartsWith("Custom Goal", StringComparison.Ordinal);
            var inner = new StackPanel { Margin = new Thickness(10, 4, 0, 8) };
            foreach (var opt in group) inner.Children.Add(Row(opt));

            stack.Children.Add(new Expander
            {
                Header = $"{group.Key}  ({group.Count()})",
                Foreground = Gold, FontWeight = FontWeights.Bold,
                IsExpanded = !longTail,
                Margin = new Thickness(0, 0, 0, 4),
                Content = inner,
            });
        }
        root.Children.Add(new ScrollViewer
        {
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            Content = stack,
        });

        Content = root;
    }

    // One control per option, chosen by the kind the apworld declared.
    private UIElement Row(D2YamlOption o)
    {
        var box = new StackPanel { Margin = new Thickness(0, 3, 0, 3) };
        var line = new StackPanel { Orientation = Orientation.Horizontal };

        switch (o.Kind)
        {
            case "choice":
            {
                var cb = new ComboBox { MinWidth = 190, Margin = new Thickness(0, 0, 8, 0) };
                foreach (var c in o.Choices) cb.Items.Add(c.Label);
                cb.SelectedIndex = Math.Max(0, Array.FindIndex(o.Choices, c => c.Value == o.Default));
                _read[o.Key] = () => o.Choices[Math.Max(0, cb.SelectedIndex)].Value;
                line.Children.Add(cb);
                line.Children.Add(Label(o.Display));
                break;
            }
            case "range":
            {
                var tb = new TextBox
                {
                    Text = o.Default.ToString(CultureInfo.InvariantCulture),
                    Width = 90, Margin = new Thickness(0, 0, 8, 0),
                };
                _read[o.Key] = () =>
                {
                    // Out-of-range is rejected by the apworld with an error the
                    // player cannot act on, so clamp here instead.
                    if (!int.TryParse(tb.Text.Trim(), NumberStyles.Integer,
                                      CultureInfo.InvariantCulture, out int v)) v = o.Default;
                    return Math.Clamp(v, o.Min, o.Max);
                };
                line.Children.Add(tb);
                line.Children.Add(Label($"{o.Display}   ({o.Min}–{o.Max})"));
                break;
            }
            default:
            {
                var chk = new CheckBox
                {
                    IsChecked = o.Default != 0, Foreground = Ink,
                    Content = o.Display, VerticalAlignment = VerticalAlignment.Center,
                };
                _read[o.Key] = () => chk.IsChecked == true;
                line.Children.Add(chk);
                break;
            }
        }

        box.Children.Add(line);
        if (!string.IsNullOrWhiteSpace(o.Help))
        {
            box.Children.Add(new TextBlock
            {
                Text = Shorten(o.Help), Foreground = Dim, FontSize = 11,
                TextWrapping = TextWrapping.Wrap, Margin = new Thickness(2, 0, 0, 0),
                MaxWidth = 680,
            });
        }
        return box;
    }

    private static TextBlock Label(string t) => new()
    {
        Text = t, Foreground = Ink, VerticalAlignment = VerticalAlignment.Center,
    };

    private static string Shorten(string s) =>
        s.Length <= 150 ? s : s[..149].TrimEnd() + "…";

    // Emit the file. Written by hand rather than through a YAML library: the
    // shape is flat and fixed, and Archipelago is strict about it — two spaces
    // of indent, the game name as the section key, booleans as true/false.
    private void Save()
    {
        string player = _name.Text.Trim();
        if (player.Length == 0)
        {
            MessageBox.Show(this, "Give yourself a player name first — it is the slot you connect as.",
                            "Name required", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        var sb = new StringBuilder();
        sb.Append("name: ").Append(player).Append('\n');
        sb.Append("description: Created with the Multiworld Launcher\n");
        sb.Append("game: ").Append(D2YamlOptions.Game).Append('\n');
        sb.Append(D2YamlOptions.Game).Append(":\n");
        // Two settings every AP file carries; the apworld does not declare them.
        sb.Append("  progression_balancing: normal\n");
        sb.Append("  accessibility: full\n");
        foreach (var o in VisibleOptions)
        {
            if (!_read.TryGetValue(o.Key, out var read)) continue;
            object v = read();
            string text = v switch
            {
                bool b => b ? "true" : "false",
                int i  => i.ToString(CultureInfo.InvariantCulture),
                _      => v?.ToString() ?? "",
            };
            sb.Append("  ").Append(o.Key).Append(": ").Append(text).Append('\n');
        }

        var dlg = new Microsoft.Win32.SaveFileDialog
        {
            Title = "Save your YAML",
            FileName = SafeFileName(player) + ".yaml",
            Filter = "Archipelago YAML (*.yaml)|*.yaml|All files (*.*)|*.*",
            DefaultExt = ".yaml",
        };
        if (dlg.ShowDialog(this) != true) return;

        try
        {
            // LF, no BOM — what every other AP YAML in the wild uses.
            File.WriteAllText(dlg.FileName, sb.ToString(), new UTF8Encoding(false));
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, "Could not write the file:\n\n" + ex.Message,
                            "Save failed", MessageBoxButton.OK, MessageBoxImage.Error);
            return;
        }

        try { System.Diagnostics.Process.Start("explorer.exe", $"/select,\"{dlg.FileName}\""); }
        catch { /* the file is written either way */ }

        MessageBox.Show(this,
            $"Saved as {Path.GetFileName(dlg.FileName)}.\n\n" +
            "Send it to whoever is generating the multiworld.",
            "YAML saved", MessageBoxButton.OK, MessageBoxImage.Information);
        DialogResult = true;
    }

    private static string SafeFileName(string s)
    {
        foreach (char c in Path.GetInvalidFileNameChars()) s = s.Replace(c, '_');
        return s;
    }

    public static void ShowFor(Window? owner, bool experimental = false)
    {
        var d = new D2YamlDialog(experimental);
        if (owner != null) d.Owner = owner;
        d.ShowDialog();
    }
}
