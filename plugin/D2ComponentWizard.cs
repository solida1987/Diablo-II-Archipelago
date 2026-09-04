using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace LauncherV2.Plugins.DiabloII;

// Walks the player through the third-party components Diablo II needs, one at
// a time, asking about each before anything is fetched.
//
// WHY ONE WINDOW PER COMPONENT rather than a tick-list and a single "Install":
// the rulebook requires the player to press yes for each thing that gets
// downloaded, and a tick-list turns four separate decisions into one
// half-read click. Each screen names the author, the licence and the exact
// file, because "some files are needed" is not something anyone can consent to.
//
// Already-satisfied components are skipped entirely -- including ones the
// player already has in their own Diablo II folder, which are copied across
// instead of downloaded. Nobody should be asked to fetch what they own.
public sealed class D2ComponentWizard : Window
{
    private readonly string _gameDir;
    private readonly Func<string, bool> _tryAdoptFromOriginal;
    private readonly Func<string, bool> _isAlreadyInstalled;
    private readonly IReadOnlyList<D2Components.Component> _queue;

    private int _index;                       // which component we are on
    private CancellationTokenSource? _cts;

    // --- chrome ---
    private static readonly Brush Bg      = new SolidColorBrush(Color.FromRgb(0x11, 0x14, 0x1C));
    private static readonly Brush Card    = new SolidColorBrush(Color.FromRgb(0x1A, 0x1E, 0x2A));
    private static readonly Brush Text    = new SolidColorBrush(Color.FromRgb(0xCC, 0xD0, 0xE0));
    private static readonly Brush Muted   = new SolidColorBrush(Color.FromRgb(0x72, 0x7A, 0x99));
    private static readonly Brush Gold    = new SolidColorBrush(Color.FromRgb(0xE8, 0xA0, 0x18));
    private static readonly Brush Green   = new SolidColorBrush(Color.FromRgb(0x4A, 0xDE, 0x80));
    private static readonly Brush Red     = new SolidColorBrush(Color.FromRgb(0xE5, 0x5A, 0x5A));

    private readonly TextBlock _stepLabel  = new() { Foreground = Muted, FontSize = 11 };
    private readonly TextBlock _title      = new() { Foreground = Gold, FontSize = 20, FontWeight = FontWeights.Bold };
    private readonly TextBlock _byline     = new() { Foreground = Muted, FontSize = 12, Margin = new Thickness(0,2,0,12) };
    private readonly TextBlock _what       = new() { Foreground = Text, FontSize = 13, TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _why        = new() { Foreground = Muted, FontSize = 12, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0,10,0,0) };
    private readonly TextBlock _fileLine   = new() { Foreground = Muted, FontSize = 11, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0,12,0,0) };
    private readonly TextBlock _status     = new() { Foreground = Muted, FontSize = 12, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0,10,0,0) };

    private readonly ProgressBar _overall  = new() { Height = 6, Minimum = 0, Maximum = 1, Foreground = Gold };
    private readonly ProgressBar _current  = new() { Height = 14, Minimum = 0, Maximum = 1, Foreground = Green };
    private readonly TextBlock _currentTxt = new() { Foreground = Muted, FontSize = 11 };

    private readonly Button _auto   = Btn("Download and install", true);
    private readonly Button _manual = Btn("I'll do it myself", false);
    private readonly Button _skip   = Btn("Skip this one", false);
    private readonly Button _close  = Btn("Close", false);

    /// `tryAdoptFromOriginal` is D2Plugin.TryAdoptFromOriginal — the player's own
    /// Diablo II folder is checked before anything is downloaded.
    public D2ComponentWizard(Window? owner, string gameDir,
                             Func<string, bool> tryAdoptFromOriginal,
                             Func<string, bool> isAlreadyInstalled)
    {
        _gameDir = gameDir;
        _tryAdoptFromOriginal = tryAdoptFromOriginal;
        _isAlreadyInstalled = isAlreadyInstalled;

        // Build the queue ONCE, skipping what is already there. A component
        // that appears and then says "already installed" is just noise.
        _queue = D2Components.All.Where(c => !isAlreadyInstalled(c.Key)).ToList();

        Owner = owner;
        Title = "Diablo II — components needed";
        Width = 640; SizeToContent = SizeToContent.Height;
        WindowStartupLocation = owner != null
            ? WindowStartupLocation.CenterOwner : WindowStartupLocation.CenterScreen;
        Background = Bg;
        ResizeMode = ResizeMode.NoResize;

        Content = BuildLayout();
        _auto.Click   += async (_, _) => await RunAutoAsync();
        _manual.Click += (_, _) => ShowManual();
        _skip.Click   += (_, _) => Next();
        _close.Click  += (_, _) => Close();

        ShowCurrent();
    }

    private static Button Btn(string text, bool primary) => new()
    {
        Content = text, Padding = new Thickness(16, 8, 16, 8),
        Margin = new Thickness(0, 0, 8, 0), MinWidth = 140,
        Background = primary ? new SolidColorBrush(Color.FromRgb(0x2A, 0x36, 0x52))
                             : new SolidColorBrush(Color.FromRgb(0x1E, 0x22, 0x30)),
        Foreground = primary ? Gold : Text,
        BorderBrush = new SolidColorBrush(Color.FromRgb(0x32, 0x3A, 0x50)),
        Cursor = System.Windows.Input.Cursors.Hand,
    };

    private UIElement BuildLayout()
    {
        var root = new StackPanel { Margin = new Thickness(22) };
        root.Children.Add(_stepLabel);
        root.Children.Add(_overall);

        var card = new Border
        {
            Background = Card, CornerRadius = new CornerRadius(6),
            Padding = new Thickness(18), Margin = new Thickness(0, 14, 0, 14),
        };
        var inner = new StackPanel();
        inner.Children.Add(_title);
        inner.Children.Add(_byline);
        inner.Children.Add(_what);
        inner.Children.Add(_why);
        inner.Children.Add(_fileLine);
        inner.Children.Add(_status);
        card.Child = inner;
        root.Children.Add(card);

        root.Children.Add(_currentTxt);
        root.Children.Add(_current);

        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal, Margin = new Thickness(0, 16, 0, 0),
        };
        buttons.Children.Add(_auto);
        buttons.Children.Add(_manual);
        buttons.Children.Add(_skip);
        buttons.Children.Add(_close);
        root.Children.Add(buttons);
        return root;
    }

    private D2Components.Component? Current
        => _index >= 0 && _index < _queue.Count ? _queue[_index] : null;

    private void ShowCurrent()
    {
        if (Current is not { } c) { ShowDone(); return; }

        _stepLabel.Text = $"Step {_index + 1} of {_queue.Count}";
        _overall.Value  = _queue.Count == 0 ? 1 : (double)_index / _queue.Count;

        _title.Text  = c.Name + (c.RequiredToPlay ? "  —  required" : "  —  optional");
        _title.Foreground = c.RequiredToPlay ? Gold : Text;
        _byline.Text = $"by {c.Author}   ·   {c.Licence}   ·   {c.RepoUrl}";
        _what.Text   = c.WhatItIs;
        _why.Text    = c.WhyNeeded;

        _fileLine.Text =
            "This downloads " + string.Join(", ", c.Files.Where(f => f.Required).Select(f => f.SaveAs))
          + $"\nfrom {c.Author}'s own GitHub releases — nothing is fetched from us, "
          + "and nothing is sent anywhere.";

        _status.Text = "";
        _current.Value = 0;
        _currentTxt.Text = "";

        _auto.IsEnabled = _manual.IsEnabled = _skip.IsEnabled = true;
        _auto.Content = "Download and install";
        _skip.Content = c.RequiredToPlay ? "Skip (the game will not start)" : "Skip this one";
        _close.Content = "Cancel";
    }

    private void ShowDone()
    {
        _stepLabel.Text = "Done";
        _overall.Value = 1;
        _title.Text = "Everything is in place";
        _title.Foreground = Green;
        _byline.Text = "";
        _what.Text = "Diablo II has what it needs. Press Play when you are ready.";
        _why.Text = "";
        _fileLine.Text = "";
        _status.Text = "";
        _current.Value = 0;
        _currentTxt.Text = "";
        _auto.Visibility = _manual.Visibility = _skip.Visibility = Visibility.Collapsed;
        _close.Content = "Close";
    }

    private void Next()
    {
        _index++;
        ShowCurrent();
    }

    private void ShowManual()
    {
        if (Current is not { } c) return;
        _status.Foreground = Text;
        _status.Text = c.ManualSteps + "\n\nGame folder: " + _gameDir;

        try { Process.Start(new ProcessStartInfo(c.ReleasesUrl) { UseShellExecute = true }); }
        catch { /* no browser is not our failure to fix */ }
        try { Process.Start(new ProcessStartInfo("explorer.exe", $"\"{_gameDir}\"")); }
        catch { }

        // The same button becomes "check again", so the player is not sent
        // hunting for a different control after doing the work by hand.
        _auto.Content = "I've done it — check again";
    }

    private async Task RunAutoAsync()
    {
        if (Current is not { } c) return;

        // Whatever the button currently says, look first: the player may have
        // just copied the files in by hand, and downloading over the top of
        // that would be rude and slow.
        if (_isAlreadyInstalled(c.Key))
        {
            _status.Foreground = Green;
            _status.Text = $"{c.Name} is in place.";
            await Task.Delay(600);
            Next();
            return;
        }

        // Before downloading: does the player already have it in their own
        // Diablo II installation? Copying beats fetching 64 MB again.
        if (_tryAdoptFromOriginal(c.Key))
        {
            _status.Foreground = Green;
            _status.Text = $"Found {c.Name} in your own Diablo II folder and copied it "
                         + "across — no download needed.";
            await Task.Delay(900);
            Next();
            return;
        }

        _auto.IsEnabled = _manual.IsEnabled = _skip.IsEnabled = false;
        _status.Foreground = Muted;
        _status.Text = "";
        _cts = new CancellationTokenSource();

        var progress = new Progress<D2ComponentInstaller.Progress>(p =>
        {
            _currentTxt.Text = p.Fraction is { } f
                ? $"{p.Stage}   {p.BytesDone / 1048576.0:F1} / {p.BytesTotal / 1048576.0:F1} MB"
                : p.Stage;
            _current.Value = p.Fraction ?? 0;
        });

        var result = await D2ComponentInstaller.InstallAsync(
            c, _gameDir, progress, _cts.Token);

        _current.Value = result.Success ? 1 : 0;
        _currentTxt.Text = "";
        _status.Foreground = result.Success ? Green : Red;
        _status.Text = result.Message;

        _auto.IsEnabled = _manual.IsEnabled = _skip.IsEnabled = true;

        if (result.Success)
        {
            await Task.Delay(700);
            Next();
        }
        else
        {
            // A failed download is not a dead end: the manual route is still
            // there, and it is the same files from the same page.
            _auto.Content = "Try again";
        }
    }
}
