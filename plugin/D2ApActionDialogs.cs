using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace LauncherV2.Plugins.DiabloII;

//
// The two "are you sure?" gates in front of anything that spends hint points
// or bypasses the game.

// Both actions are irreversible from the launcher's side — the server has no
// undo for a spent hint or a forced check — so neither is ever one click away.
// They are deliberately different dialogs rather than one generic one:

// Hints cost earned points; cheats (!getitem) are free but marked in chat.
internal static class D2ApActionDialogs
{
    private static readonly Brush Bg     = new SolidColorBrush(Color.FromRgb(0x14, 0x17, 0x20));
    private static readonly Brush Panel  = new SolidColorBrush(Color.FromRgb(0x10, 0x13, 0x20));
    private static readonly Brush Muted  = new SolidColorBrush(Color.FromRgb(0x9A, 0xA3, 0xBF));
    private static readonly Brush Gold   = new SolidColorBrush(Color.FromRgb(0xE6, 0xB8, 0x4C));
    private static readonly Brush Danger = new SolidColorBrush(Color.FromRgb(0xE0, 0x5A, 0x4F));

    // Ask before spending points on a hint.
    public static bool ConfirmHint(Window? owner, string what, int cost, int points)
    {
        var body = new StackPanel();
        body.Children.Add(Text(what, 14, FontWeights.SemiBold, Brushes.White));
        if (cost <= 0)
        {
            // hint_cost of 0 means the host made hints free, not that the price
            // is unknown. Still confirm — a hint is a spoiler either way.
            body.Children.Add(Text("Hints are free in this room.",
                                   13, FontWeights.Normal, Gold, new Thickness(0, 10, 0, 0)));
        }
        else
        {
            body.Children.Add(Text($"Costs {cost} hint point{(cost == 1 ? "" : "s")}.",
                                   13, FontWeights.Normal, Gold, new Thickness(0, 10, 0, 0)));
            body.Children.Add(Text($"You have {points} — {points - cost} left afterwards.",
                                   12, FontWeights.Normal, Muted, new Thickness(0, 2, 0, 0)));
        }
        body.Children.Add(Text("The answer appears in the Archipelago message log.",
                               11, FontWeights.Normal, Muted, new Thickness(0, 10, 0, 0)));
        return Show(owner, cost <= 0 ? "Reveal this?" : "Buy this hint?", body,
                    cost <= 0 ? "Get hint" : "Buy hint", Gold) == true;
    }

    // Ask before forcing something through the server, and collect the room's
    // admin password. Returns the password, or null if the user backed out.
    // The caller must not cache the returned value.
    public static string? ConfirmCheat(Window? owner, string title, string what, string warning)
    {
        var pw = new PasswordBox
        {
            Background = Panel, Foreground = Brushes.White,
            BorderBrush = new SolidColorBrush(Color.FromRgb(0x2A, 0x30, 0x50)),
            BorderThickness = new Thickness(1), Padding = new Thickness(7, 5, 7, 5),
            FontSize = 13, Margin = new Thickness(0, 4, 0, 0),
        };

        var body = new StackPanel();
        body.Children.Add(Text(what, 14, FontWeights.SemiBold, Brushes.White));
        body.Children.Add(Text(warning, 12, FontWeights.Normal, Danger, new Thickness(0, 10, 0, 0)));
        body.Children.Add(Text("Room admin password", 11, FontWeights.Bold, Muted,
                               new Thickness(0, 16, 0, 0)));
        body.Children.Add(pw);
        body.Children.Add(Text("Asked every time and never saved.",
                               11, FontWeights.Normal, Muted, new Thickness(0, 5, 0, 0)));

        pw.Loaded += (_, _) => pw.Focus();
        bool? ok = Show(owner, title, body, "Yes, do it", Danger);
        if (ok != true) return null;
        string entered = pw.Password;
        return string.IsNullOrEmpty(entered) ? null : entered;
    }

    // Same shape as ConfirmCheat but without a password — used where the
    // command needs no admin rights (a plain client cheat the server has
    // explicitly enabled).
    public static bool ConfirmPlain(Window? owner, string title, string what, string warning)
    {
        var body = new StackPanel();
        body.Children.Add(Text(what, 14, FontWeights.SemiBold, Brushes.White));
        body.Children.Add(Text(warning, 12, FontWeights.Normal, Danger, new Thickness(0, 10, 0, 0)));
        return Show(owner, title, body, "Yes, do it", Danger) == true;
    }

    // --- shared shell ---

    private static bool? Show(Window? owner, string title, UIElement body,
                              string okText, Brush okColour)
    {
        var dlg = new Window
        {
            Title = title, Owner = owner, Background = Bg,
            SizeToContent = SizeToContent.Height, Width = 430,
            WindowStartupLocation = owner == null
                ? WindowStartupLocation.CenterScreen
                : WindowStartupLocation.CenterOwner,
            ResizeMode = ResizeMode.NoResize, ShowInTaskbar = false,
        };

        var root = new StackPanel { Margin = new Thickness(20) };
        root.Children.Add(Text(title, 17, FontWeights.Bold, Gold, new Thickness(0, 0, 0, 12)));
        root.Children.Add(body);

        var row = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(0, 20, 0, 0),
        };
        var cancel = new Button { Content = "Cancel", Padding = new Thickness(14, 5, 14, 5), IsCancel = true };
        var ok = new Button
        {
            Content = okText, Padding = new Thickness(14, 5, 14, 5),
            Margin = new Thickness(10, 0, 0, 0), IsDefault = true,
            Background = okColour, Foreground = Brushes.Black, FontWeight = FontWeights.SemiBold,
        };
        ok.Click     += (_, _) => { dlg.DialogResult = true;  };
        cancel.Click += (_, _) => { dlg.DialogResult = false; };
        row.Children.Add(cancel);
        row.Children.Add(ok);
        root.Children.Add(row);

        dlg.Content = root;
        return dlg.ShowDialog();
    }

    private static TextBlock Text(string t, double size, FontWeight w, Brush fg,
                                  Thickness? margin = null) => new()
    {
        Text = t, FontSize = size, FontWeight = w, Foreground = fg,
        TextWrapping = TextWrapping.Wrap, Margin = margin ?? new Thickness(0),
    };
}
