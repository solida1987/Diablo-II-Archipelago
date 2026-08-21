using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using LauncherV2.UI.Controls;

namespace LauncherV2.Plugins.DiabloII;

// "Unbug Save" — save repairs for known game bugs. The launcher owns the
// button; everything about WHAT it does lives here.
internal static class D2UnbugMenu
{
    // One repair: what it is called, what it warns about, whether it applies
    // to a given save, and what it does to one.
    private sealed record Repair(
        string Title,
        string Describe,
        Func<string, bool> Applies,
        Func<string, string?> Run);   // null on success, else why not

    private static List<Repair> RepairsFor(D2Plugin d2) => new()
    {
        new Repair(
            "Remove a stuck Iron Golem",
            "An Iron Golem stored in the save can make a character impossible to "
          + "load (\"unable to enter game, bad hireable\"). This removes the stored "
          + "golem so the character works again. Nothing else about the character "
          + "changes, and a backup is kept next to the save.",
            d2.SaveHasIronGolem,
            d2.RepairIronGolemSave),

        new Repair(
            "Generate a new map",
            "Some characters get an invisible wall blocking the way, and relogging "
          + "never clears it because the world layout is stored in the character. "
          + "This rolls a fresh layout. Level, items and quests are untouched, and "
          + "a backup is kept next to the save.",
            _ => true,                      // always offerable
            d2.RegenerateMapForSave),
    };

    // Every .d2s under the game's save tree. Characters live in per-seed
    // subfolders as well as the root Save folder, so this walks the whole tree.
    private static List<string> FindCharacterSaves(D2Plugin d2)
    {
        var found = new List<string>();
        try
        {
            if (string.IsNullOrEmpty(d2.GameDirectory)) return found;
            string saveRoot = Path.Combine(d2.GameDirectory, "Save");
            if (!Directory.Exists(saveRoot)) return found;
            found.AddRange(Directory.GetFiles(saveRoot, "*.d2s", SearchOption.AllDirectories));
            found.Sort(StringComparer.OrdinalIgnoreCase);
        }
        catch { /* a missing or locked save folder just means nothing to offer */ }
        return found;
    }

    // Open the repair menu under the mouse. Called from the plugin's
    // GameCommand, which is why it takes the owning window rather than a
    // placement target: the launcher owns the button and we do not see it.
    public static void Show(Window owner, D2Plugin d2, Action<string> log)
    {
        var repairs = RepairsFor(d2);
        var saves   = FindCharacterSaves(d2);

        var menu = new ContextMenu { PlacementTarget = owner };

        if (saves.Count == 0)
        {
            menu.Items.Add(new MenuItem { Header = "No characters found", IsEnabled = false });
        }
        else
        {
            foreach (var repair in repairs)
            {
                var top = new MenuItem { Header = repair.Title };
                foreach (string save in saves)
                {
                    // A repair that throws while deciding whether it applies is
                    // a repair that does not apply -- never one that crashes
                    // the menu it is being listed in.
                    bool applies;
                    try { applies = repair.Applies(save); } catch { applies = false; }

                    var item = new MenuItem
                    {
                        Header    = Path.GetFileNameWithoutExtension(save)
                                    + (applies ? "" : "   (not affected)"),
                        IsEnabled = applies,
                        Tag       = save,
                    };
                    string captured = save;
                    item.Click += (_, __) => Run(owner, repair, captured, log);
                    top.Items.Add(item);
                }
                menu.Items.Add(top);
            }
        }

        menu.IsOpen = true;
    }

    private static void Run(Window owner, Repair repair, string savePath, Action<string> log)
    {
        string name = Path.GetFileNameWithoutExtension(savePath);
        if (!ConfirmDialog.Show(owner, repair.Title,
                $"Character: {name}\n\n{repair.Describe}\n\nRun it now?",
                "Repair", "Cancel"))
            return;

        string? error;
        try { error = repair.Run(savePath); }
        catch (Exception ex) { error = ex.Message; }

        if (error == null)
        {
            log($"[Unbug] {repair.Title} — done for '{name}'.");
            ConfirmDialog.ShowInfo(owner, "Done",
                $"'{name}' has been repaired. A backup of the original was saved next to it.");
        }
        else
        {
            log($"[Unbug] {repair.Title} — not applied to '{name}': {error}");
            ConfirmDialog.ShowInfo(owner, "Nothing was changed", error);
        }
    }
}
