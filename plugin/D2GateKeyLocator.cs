using System;
using System.Collections.Generic;
using LauncherV2.Core;

namespace LauncherV2.Plugins.DiabloII;

// Works out WHERE a missing gate key sits, via free scouts
// (create_as_hint=0 — never the player's hint points). Only unchecked
// locations are scouted.

public readonly record struct GateKeyHint(
    int Difficulty,     // 0 Normal, 1 Nightmare, 2 Hell
    int Slot,           // flattened gate slot 0..17, matching the DLL
    string Where);      // finished text for the tracker row

public static class D2GateKeyLocator
{
    // Mirrors items.py GATE_KEY_AP_BASE.
    private static readonly int[] ApBase = { 46101, 46121, 46141 };

    // Mirrors items.py slot_by_act_gate: the act's FIRST gate slot, which is
    // also the act's item id offset.
    private static readonly Dictionary<int, int> FirstSlotByAct = new()
    {
        { 1, 0 }, { 2, 4 }, { 3, 8 }, { 4, 12 }, { 5, 14 },
    };

    // Mirrors items.py GATE_COPIES_BY_ACT. Act 4 only has two gates.
    private static readonly Dictionary<int, int> CopiesByAct = new()
    {
        { 1, 4 }, { 2, 4 }, { 3, 4 }, { 4, 2 }, { 5, 4 },
    };

    public const string NotInOurWorld = "another world - press Hint";

    // One hint per act/difficulty the player is still short of, built from the
    // keys received so far and what the server said sits at our own unchecked
    // locations.
    public static List<GateKeyHint> Build(
        IReadOnlyDictionary<long, int> copiesReceived,
        IReadOnlyCollection<ApNetworkItem> scouted,
        IReadOnlyCollection<long> checkedLocations,
        int ownSlot,
        Func<long, string?> resolveLocationName)
    {
        var hints = new List<GateKeyHint>();
        if (resolveLocationName is null) return hints;

        var alreadyChecked = checkedLocations as HashSet<long>
                             ?? new HashSet<long>(checkedLocations ?? Array.Empty<long>());

        // Our own still-unclaimed copies, grouped by which key they are.
        // Ordering by location id keeps the answer stable between runs — an
        // answer that moves around every reconnect reads as a bug.
        var byItem = new Dictionary<long, List<long>>();
        foreach (var s in scouted ?? Array.Empty<ApNetworkItem>())
        {
            if (s.Player != ownSlot) continue;              // not our item
            if (alreadyChecked.Contains(s.LocationId)) continue;
            if (!byItem.TryGetValue(s.ItemId, out var list))
                byItem[s.ItemId] = list = new List<long>();
            list.Add(s.LocationId);
        }
        foreach (var list in byItem.Values) list.Sort();

        for (int diff = 0; diff < 3; diff++)
        {
            foreach (var (act, firstSlot) in FirstSlotByAct)
            {
                long itemId = ApBase[diff] + firstSlot;
                int have = copiesReceived != null
                           && copiesReceived.TryGetValue(itemId, out int c) ? c : 0;
                int total = CopiesByAct[act];
                if (have >= total) continue;               // act fully unlocked

                // The next copy opens the next gate, so that is the row the
                // tracker will be drawing this text on.
                int slot = firstSlot + have;

                string where = NotInOurWorld;
                if (byItem.TryGetValue(itemId, out var locs) && locs.Count > 0)
                {
                    string? name = resolveLocationName(locs[0]);
                    if (!string.IsNullOrWhiteSpace(name))
                    {
                        where = name!;
                        if (locs.Count > 1) where += $" (+{locs.Count - 1} more)";
                    }
                }

                hints.Add(new GateKeyHint(diff, slot, where));
            }
        }

        return hints;
    }

    // True if this AP item id is one of the progressive act keys.
    public static bool IsGateKey(long itemId)
    {
        foreach (int b in ApBase)
            foreach (var kv in FirstSlotByAct)
                if (itemId == b + kv.Value) return true;
        return false;
    }
}
