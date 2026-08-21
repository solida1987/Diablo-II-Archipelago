using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace LauncherV2.Plugins.DiabloII;

//
// The item-side counterpart to the Map tab's per-check buttons: every item
// this world can contain, each with "where is it?" and "give it to me".

// Why a searchable list rather than buttons on the Items tab's grid: that grid
// shows item EVENTS (this was received, that was sent).
// in" button off an event answers a question nobody asks — the item already
// moved. What you actually want is "I am stuck, give me the Act 2 key", which
// means starting from the full catalogue and finding the one item you need.
// The catalogue is the datapackage's item_name_to_id for our own game, so it
// is exactly the set of names the server will accept.

// !hint costs points and is public; !getitem is free but marked as a cheat.
public sealed class D2ItemActionDialog : Window
{
    private static readonly Brush Bg    = new SolidColorBrush(Color.FromRgb(0x14, 0x17, 0x20));
    private static readonly Brush Panel = new SolidColorBrush(Color.FromRgb(0x10, 0x13, 0x20));
    private static readonly Brush Muted = new SolidColorBrush(Color.FromRgb(0x9A, 0xA3, 0xBF));
    private static readonly Brush Gold  = new SolidColorBrush(Color.FromRgb(0xE6, 0xB8, 0x4C));
    private static readonly Brush Red   = new SolidColorBrush(Color.FromRgb(0xE0, 0x5A, 0x4F));
    private static readonly Brush Green = new SolidColorBrush(Color.FromRgb(0x57, 0xC7, 0x6B));

    private readonly List<string> _items;
    private readonly List<string> _locations;
    private readonly Func<string, Task> _send;
    private readonly string _slotName;
    private readonly int _hintPoints;
    private readonly int _hintCost;

    // false = items (!hint / !getitem), true = checks (!hint_location /
    // send_location). Two catalogues, two command pairs, one window — the
    // question "where is my key" and "what is in that check" are the same
    // impulse from opposite ends.
    private bool _checkMode;

    private readonly TextBox    _search = new();
    private readonly StackPanel _list   = new();
    private readonly TextBlock  _count  = new() { Foreground = Muted, FontSize = 11 };
    private readonly Button     _tabItems  = new();
    private readonly Button     _tabChecks = new();

    public static void ShowFor(Window? owner, IEnumerable<string> itemNames,
                               IEnumerable<string> locationNames, string slotName,
                               Func<string, Task> send, int hintPoints, int hintCostPoints)
    {
        var dlg = new D2ItemActionDialog(itemNames, locationNames, slotName, send,
                                         hintPoints, hintCostPoints) { Owner = owner };
        dlg.ShowDialog();
    }

    private D2ItemActionDialog(IEnumerable<string> itemNames, IEnumerable<string> locationNames,
                               string slotName, Func<string, Task> send,
                               int hintPoints, int hintCostPoints)
    {
        static List<string> Clean(IEnumerable<string> src) =>
            src.Where(n => !string.IsNullOrWhiteSpace(n))
               .Distinct(StringComparer.OrdinalIgnoreCase)
               .OrderBy(n => n, StringComparer.OrdinalIgnoreCase)
               .ToList();

        _items      = Clean(itemNames);
        _locations  = Clean(locationNames);
        _slotName   = slotName ?? "";
        _send       = send;
        _hintPoints = hintPoints;
        _hintCost   = hintCostPoints;

        Title = "Hint or cheat";
        Background = Bg;
        Width = 620; Height = 560;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;

        var root = new DockPanel { Margin = new Thickness(16) };

        var head = new StackPanel { Margin = new Thickness(0, 0, 0, 10) };
        head.Children.Add(new TextBlock
        {
            Text = "Hint or cheat", Foreground = Gold,
            FontSize = 17, FontWeight = FontWeights.Bold,
        });

        // Mode switch. The Map tab already covers the ~810 quest checks that
        // belong to an area; the other ~1900 (shrines, urns, barrels, chests,
        // cow/merc/rune/NPC checks, gate kills) have no area at all and can
        // therefore never appear on a map.
        var tabs = new StackPanel
        { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 8, 0, 0) };
        StyleTab(_tabItems,  $"Items ({_items.Count})");
        StyleTab(_tabChecks, $"Checks ({_locations.Count})");
        _tabItems.Click  += (_, _) => SetMode(false);
        _tabChecks.Click += (_, _) => SetMode(true);
        tabs.Children.Add(_tabItems);
        tabs.Children.Add(_tabChecks);
        head.Children.Add(tabs);

        head.Children.Add(new TextBlock
        {
            Text = _hintCost <= 0
                 ? $"Hint points: {_hintPoints}   ·   hints are free"
                 : $"Hint points: {_hintPoints}   ·   a hint costs {_hintCost}",
            Foreground = _hintPoints >= _hintCost ? Green : Muted,
            FontSize = 12, Margin = new Thickness(0, 4, 0, 0),
        });
        DockPanel.SetDock(head, Dock.Top);
        root.Children.Add(head);

        _search.Background = Panel;
        _search.Foreground = Brushes.White;
        _search.BorderBrush = new SolidColorBrush(Color.FromRgb(0x2A, 0x30, 0x50));
        _search.BorderThickness = new Thickness(1);
        _search.Padding = new Thickness(7, 5, 7, 5);
        _search.FontSize = 13;
        _search.TextChanged += (_, _) => Rebuild();
        DockPanel.SetDock(_search, Dock.Top);
        root.Children.Add(_search);

        _count.Margin = new Thickness(0, 6, 0, 6);
        DockPanel.SetDock(_count, Dock.Top);
        root.Children.Add(_count);

        var scroll = new ScrollViewer
        {
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            Background = Panel, Content = _list, Padding = new Thickness(8),
        };
        root.Children.Add(scroll);

        Content = root;
        Loaded += (_, _) => _search.Focus();
        SetMode(false);
    }

    private void SetMode(bool checks)
    {
        _checkMode = checks;
        PaintTab(_tabItems,  !checks);
        PaintTab(_tabChecks,  checks);
        Rebuild();
    }

    // Cap what is DRAWN, not what is searched.
    // the D2 package); the check list is 2730 and a few thousand WPF panels is
    // a visible stall. The count line says plainly when the view is truncated
    // so nobody concludes their check does not exist.
    private const int MaxRows = 400;

    private void Rebuild()
    {
        _list.Children.Clear();
        var source = _checkMode ? _locations : _items;
        string noun = _checkMode ? "check" : "item";
        string q = _search.Text.Trim();
        var hits = q.Length == 0
            ? source
            : source.Where(n => n.Contains(q, StringComparison.OrdinalIgnoreCase)).ToList();

        _count.Text = hits.Count > MaxRows
            ? $"{hits.Count} {noun}s match — showing the first {MaxRows}. Type to narrow it down."
            : $"{hits.Count} {noun}{(hits.Count == 1 ? "" : "s")}";

        foreach (var name in hits.Take(MaxRows))
            _list.Children.Add(Row(name));

        if (hits.Count == 0)
            _list.Children.Add(new TextBlock
            {
                Text = "Nothing matches that.", Foreground = Muted,
                FontSize = 13, Margin = new Thickness(2, 6, 0, 0),
            });
    }

    private UIElement Row(string name)
    {
        var row = new DockPanel { LastChildFill = true, Margin = new Thickness(0, 2, 0, 2) };

        var cheat = _checkMode
            ? Small("Cheat", Red,
                "Mark this check as found. The item goes to whoever it belongs to, "
                + "exactly as if you had picked it up. Needs the room admin password.")
            : Small("Cheat in", Red,
                "Give yourself this item with !getitem. Only works if the host enabled "
                + "item cheating. You get a COPY — the real one stays where it was placed.");
        cheat.Click += (_, _) => DoCheat(name);
        DockPanel.SetDock(cheat, Dock.Right);
        row.Children.Add(cheat);

        string asks = _checkMode ? "Ask what is in this check." : "Ask where this item is.";
        bool free = _hintCost <= 0;
        bool affordable = free || _hintPoints >= _hintCost;
        var hint = Small(free ? "Hint" : $"Hint {_hintCost}p", affordable ? Gold : Muted,
            free
                ? asks + " Hints are free in this room."
                : affordable
                    ? asks + $" Costs {_hintCost} of your {_hintPoints} points."
                    : $"Needs {_hintCost} hint points — you have {_hintPoints}.");
        hint.IsEnabled = affordable;
        hint.Opacity   = affordable ? 1.0 : 0.55;
        hint.Click += (_, _) => DoHint(name);
        DockPanel.SetDock(hint, Dock.Right);
        row.Children.Add(hint);

        row.Children.Add(new TextBlock
        {
            Text = name, Foreground = Brushes.White, FontSize = 13,
            TextTrimming = TextTrimming.CharacterEllipsis,
            VerticalAlignment = VerticalAlignment.Center,
        });
        return row;
    }

    private static Button Small(string label, Brush fg, string tip) => new()
    {
        Content = label, FontSize = 10, Padding = new Thickness(8, 2, 8, 2),
        Margin = new Thickness(6, 0, 0, 0), Foreground = fg,
        Background = new SolidColorBrush(Color.FromRgb(0x1A, 0x1E, 0x30)),
        BorderBrush = new SolidColorBrush(Color.FromRgb(0x2A, 0x30, 0x50)),
        BorderThickness = new Thickness(1), Cursor = System.Windows.Input.Cursors.Hand,
        VerticalAlignment = VerticalAlignment.Center, ToolTip = tip,
    };

    private void DoHint(string name)
    {
        string q = _checkMode ? $"Hint: what is in “{name}”?" : $"Hint: where is “{name}”?";
        if (!D2ApActionDialogs.ConfirmHint(this, q, _hintCost, _hintPoints)) return;
        _ = _send(_checkMode ? $"!hint_location {name}" : $"!hint {name}");
    }

    private void DoCheat(string name)
    {
        if (!_checkMode)
        {
            if (!D2ApActionDialogs.ConfirmPlain(this, "Cheat in this item?",
                    $"Give yourself “{name}”.",
                    "This is a copy — the real item stays wherever the seed put it, still "
                    + "waiting to be found. Only works if the host enabled item cheating; "
                    + "if not, the server will say so in the message log."))
                return;
            _ = _send($"!getitem {name}");
            return;
        }

        // Forcing a check goes through the admin interface, and the server's
        // send_location takes the player as a SINGLE token — a slot name with a
        // space in it cannot be expressed at all.
        // command that would quietly address someone else.
        if (_slotName.Length == 0 || _slotName.Contains(' '))
        {
            MessageBox.Show(this,
                _slotName.Length == 0
                    ? "The launcher does not know your slot name yet — reconnect and try again."
                    : $"Your slot name (“{_slotName}”) contains a space, and the server's "
                      + "send_location command takes the player name as a single word. "
                      + "Use the server console directly for this one.",
                "Cannot cheat this check", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        string? pw = D2ApActionDialogs.ConfirmCheat(this, "Cheat this check?",
            $"Mark “{name}” as found.",
            "This sends the item to its real owner and cannot be undone. "
            + "Everyone in the multiworld sees it.");
        if (pw == null) return;
        _ = _send($"!admin login {pw}");
        _ = _send($"!admin send_location {_slotName} {name}");
    }

    // --- mode tabs ---

    private static void StyleTab(Button b, string label)
    {
        b.Content = label;
        b.FontSize = 11;
        b.Padding = new Thickness(11, 3, 11, 3);
        b.Margin = new Thickness(0, 0, 6, 0);
        b.BorderBrush = new SolidColorBrush(Color.FromRgb(0x2A, 0x30, 0x50));
        b.BorderThickness = new Thickness(1);
        b.Cursor = System.Windows.Input.Cursors.Hand;
    }

    private static void PaintTab(Button b, bool active)
    {
        b.Background = active ? Gold : new SolidColorBrush(Color.FromRgb(0x1A, 0x1E, 0x30));
        b.Foreground = active ? Brushes.Black : Muted;
        b.FontWeight = active ? FontWeights.SemiBold : FontWeights.Normal;
    }
}
