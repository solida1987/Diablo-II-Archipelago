using System;
using System.Threading.Tasks;
using System.Windows;

namespace LauncherV2.Plugins.DiabloII;

// Defender blocked the mod (it injects into a running game) and the
// one-click exclusion fix. The wording is this game's problem, not the
// launcher's.
internal static class D2AntivirusFix
{
    // Only offer the exclusion once per launcher session.
    //
    // If a launch still fails as an AV block after the player already dealt
    // with the exclusion, the exclusion is not the fix — and re-prompting
    // hides the real error behind a dialog that has already been answered.
    private static bool _offeredThisSession;

    public static async Task<bool> OfferAsync(Window owner, D2Plugin d2,
                                              Exception failure,
                                              Action<string> log)
    {
        if (!IsAntivirusBlock(failure)) return false;

        if (_offeredThisSession)
        {
            log("[Defender] Still blocked after an exclusion was already offered this "
                + "session — showing the underlying error instead of re-prompting.");
            return false;
        }
        _offeredThisSession = true;

        string gameDir = d2.GameDirectory;
        var ask = MessageBox.Show(owner,
            "Windows Defender blocked the mod from starting (false positive):\n\n" +
            failure.Message +
            "\n\nThe mod injects into Diablo II, which Defender flags as suspicious. " +
            "I can add the game folder to Defender's exclusion list so it stops:\n\n" +
            gameDir +
            "\n\nWindows will ask for administrator permission. Add the exclusion now?",
            "Add Windows Defender exclusion?",
            MessageBoxButton.YesNo, MessageBoxImage.Warning);

        // Handled either way — the player was asked and said no.
        if (ask != MessageBoxResult.Yes) return true;

        bool ok = await Task.Run(() => d2.AddDefenderExclusion());
        if (ok)
        {
            log("[Defender] Exclusion added for " + gameDir);
            MessageBox.Show(owner,
                "Added to Windows Defender's exclusions. Click Play / Launch again to start the game.",
                "Exclusion added", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        else
        {
            MessageBox.Show(owner,
                "Couldn't add the exclusion automatically (the admin prompt may have been " +
                "declined, or a third-party antivirus is active). Add it manually:\n\n" +
                "Windows Security → Virus & threat protection → Manage settings → " +
                "Exclusions → Add an exclusion → Folder:\n\n" + gameDir,
                "Add the exclusion manually", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        return true;
    }

    // True ONLY on Win32 codes 225/226 (ERROR_VIRUS_*). Matching the word
    // "virus" in messages looped forever: our own error text contains
    // "antivirus".
    private static bool IsAntivirusBlock(Exception ex)
    {
        for (Exception? e = ex; e != null; e = e.InnerException)
            if (e is System.ComponentModel.Win32Exception w32 &&
                (w32.NativeErrorCode == 225 || w32.NativeErrorCode == 226))
                return true;
        return false;
    }
}
