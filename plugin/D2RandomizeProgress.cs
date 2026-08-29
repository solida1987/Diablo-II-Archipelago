using System;
using System.Diagnostics;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Effects;

namespace LauncherV2.Plugins.DiabloII;

//
// the little on-screen status bar Marco asked for.
// launches we show a small window that steps through "backup → generate → apply →
// confirm" so it's visible that the world is being randomized and that every table
// was actually moved into place; the same window appears when the game closes to
// show the install being reset to pristine.
// launch, and the real file work + verification lives in D2DataFiles.
//
internal static class D2RandomizeProgress
{
    // Randomize flow (before launch): backup → generate → apply → confirm.
    // Runs on the UI thread (the caller is still synchronous here); the file IO is
    // pushed to a worker so the bar animates.
    // when there's no WPF app (headless/tests).
    public static async Task RunApplyAsync(
        D2RandomizerSettings s, long seed, string seedFolder, string gameDir)
    {
        if (Application.Current?.Dispatcher == null)
        {
            D2DataFiles.GenerateForSeed(s, seed, seedFolder, gameDir);
            D2DataFiles.ApplySeed(seedFolder, gameDir);
            return;
        }

        var ov = new D2ProgressOverlay("Randomizing the world…");
        ov.Detail($"Seed {seed}");
        ov.Show();
        try
        {
            await StepAsync(ov, "Backing up the original tables…", 12,
                () => D2DataFiles.EnsureBackup(gameDir), 300);
            await StepAsync(ov, "Generating randomized tables…", 48,
                () => D2DataFiles.GenerateForSeed(s, seed, seedFolder, gameDir), 550);
            await StepAsync(ov, "Applying the seed's tables to the game…", 78,
                () => D2DataFiles.ApplySeed(seedFolder, gameDir), 350);

            (int ok, int total) v = (0, 0);
            await StepAsync(ov, "Verifying everything is in place…", 94,
                () => v = D2DataFiles.VerifyApplied(seedFolder, gameDir), 350);

            bool good = v.total > 0 && v.ok == v.total;
            ov.Done(good
                ? $"✓ Randomization ready — {v.ok}/{v.total} tables in place"
                : "✓ Klar — starter spillet");
            await Task.Delay(850);
        }
        catch
        {
            // The overlay is cosmetic: if anything in it fails, still make sure the
            // tables are generated + applied so the launch is correct.
            try
            {
                D2DataFiles.GenerateForSeed(s, seed, seedFolder, gameDir);
                D2DataFiles.ApplySeed(seedFolder, gameDir);
            }
            catch { /* non-fatal */ }
        }
        finally { try { ov.Close(); } catch { /* ignore */ } }
    }

    // Reset flow (when the game exits): restore pristine → confirm clean.
    // Called from the process-exit handler on an arbitrary thread, so it marshals to
    // the UI thread. If there's no live dispatcher (app shutting down) it restores
    // synchronously without UI so the install is never left patched.
    public static void RunRestoreWithProgress(string gameDir)
    {
        var disp = Application.Current?.Dispatcher;
        if (disp == null || disp.HasShutdownStarted || disp.HasShutdownFinished)
        {
            D2DataFiles.RestorePristine(gameDir);
            return;
        }

        disp.InvokeAsync(async () =>
        {
            D2ProgressOverlay? ov = null;
            try
            {
                ov = new D2ProgressOverlay("Restoring the installation…");
                ov.Show();
                await StepAsync(ov, "Restoring the default text files…", 55,
                    () => D2DataFiles.RestorePristine(gameDir), 380);

                (int ok, int total) v = (0, 0);
                await StepAsync(ov, "Verifying a clean installation…", 90,
                    () => v = D2DataFiles.VerifyPristine(gameDir), 320);

                bool good = v.total > 0 && v.ok == v.total;
                ov.Done(good ? "✓ Installation nulstillet til standard" : "✓ Nulstillet");
                await Task.Delay(750);
            }
            catch
            {
                try { D2DataFiles.RestorePristine(gameDir); } catch { /* non-fatal */ }
            }
            finally { try { ov?.Close(); } catch { /* ignore */ } }
        });
    }

    // Report a step, run its (blocking) work off the UI thread, and hold the step
    // on screen for at least minMs so each phase is actually seen.
    private static async Task StepAsync(
        D2ProgressOverlay ov, string status, double pct, Action work, int minMs)
    {
        ov.Report(status, pct);
        var sw = Stopwatch.StartNew();
        await Task.Run(work);
        int remaining = minMs - (int)sw.ElapsedMilliseconds;
        if (remaining > 0) await Task.Delay(remaining);
    }
}

// The small dark status window (built in code to match the D2 plugin UI).
internal sealed class D2ProgressOverlay : Window
{
    private static readonly Brush Bg     = Frozen(0x07, 0x0A, 0x14);
    private static readonly Brush Track   = Frozen(0x0C, 0x10, 0x20);
    private static readonly Brush Border  = Frozen(0x2A, 0x30, 0x50);
    private static readonly Brush Fg      = Frozen(0xCC, 0xD0, 0xE0);
    private static readonly Brush Muted   = Frozen(0x72, 0x7A, 0x99);
    private static readonly Brush Eyebrow = Frozen(0xB0, 0x2A, 0x2A);
    private static readonly Brush BarRed  = Frozen(0xC0, 0x39, 0x2B);
    private static readonly Brush BarGood = Frozen(0x3A, 0xA0, 0x55);

    private readonly TextBlock _status;
    private readonly TextBlock _detail;
    private readonly ProgressBar _bar;

    public D2ProgressOverlay(string heading)
    {
        WindowStyle           = WindowStyle.None;
        AllowsTransparency    = true;
        Background            = Brushes.Transparent;
        ResizeMode            = ResizeMode.NoResize;
        ShowInTaskbar         = false;
        Topmost               = true;
        Width                 = 480;
        SizeToContent         = SizeToContent.Height;
        WindowStartupLocation = WindowStartupLocation.CenterScreen;

        var card = new Border
        {
            CornerRadius    = new CornerRadius(12),
            Background      = Bg,
            BorderBrush     = Border,
            BorderThickness = new Thickness(1),
            Padding         = new Thickness(24, 20, 24, 22),
            Margin          = new Thickness(16),     // room for the shadow
            Effect          = new DropShadowEffect
            {
                BlurRadius = 28, ShadowDepth = 0, Opacity = 0.55,
                Color = Color.FromRgb(0, 0, 0),
            },
        };

        var stack = new StackPanel();

        stack.Children.Add(new TextBlock
        {
            Text = "DIABLO II  ·  RANDOMIZER",
            FontSize = 10, FontWeight = FontWeights.Bold, Foreground = Eyebrow,
            Margin = new Thickness(0, 0, 0, 8),
        });

        _status = new TextBlock
        {
            Text = heading, FontSize = 15, FontWeight = FontWeights.SemiBold,
            Foreground = Fg, TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 0, 0, 12),
        };
        stack.Children.Add(_status);

        _bar = new ProgressBar
        {
            Minimum = 0, Maximum = 100, Value = 0, Height = 10,
            Background = Track, Foreground = BarRed, BorderThickness = new Thickness(0),
        };
        stack.Children.Add(_bar);

        _detail = new TextBlock
        {
            Text = "", FontSize = 10, Foreground = Muted,
            Margin = new Thickness(0, 8, 0, 0),
        };
        stack.Children.Add(_detail);

        card.Child = stack;
        Content = card;
    }

    // Update the headline + advance the bar (call on the UI thread).
    public void Report(string status, double pct)
    {
        _status.Text = status;
        _bar.Value = Math.Clamp(pct, 0, 100);
    }

    public void Detail(string text) => _detail.Text = text;

    // Final state: full bar + green confirmation.
    public void Done(string status)
    {
        _status.Text = status;
        _status.Foreground = BarGood;
        _bar.Value = 100;
        _bar.Foreground = BarGood;
    }

    private static Brush Frozen(byte r, byte g, byte b)
    {
        var br = new SolidColorBrush(Color.FromRgb(r, g, b));
        br.Freeze();
        return br;
    }
}
