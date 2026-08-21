using System;
using System.Collections.Generic;

namespace LauncherV2.Plugins.DiabloII;

// Location id -> display name. The apworld builds every id as
// 42000 + quest + 1000*difficulty, and D2LogicTables already carries
// name -> quest, so the reverse is derived rather than shipped twice.
internal static class D2LocationNames
{
    private const long Base = 42000;

    private static Dictionary<int, string>? _questToName;

    private static Dictionary<int, string> QuestToName()
    {
        if (_questToName != null) return _questToName;
        var map = new Dictionary<int, string>();
        foreach (var kv in D2LogicTables.LocationQuest)
        {
            // The table holds all three difficulties under decorated names
            // ("... (Nightmare)"); the plain one is the canonical label.
            if (kv.Key.EndsWith(")", StringComparison.Ordinal) &&
                (kv.Key.Contains("(Nightmare)", StringComparison.Ordinal) ||
                 kv.Key.Contains("(Hell)", StringComparison.Ordinal)))
                continue;
            map[kv.Value] = kv.Key;
        }
        _questToName = map;
        return map;
    }

    // Null for an id that is not one of ours — a location in someone else's
    // world, or a malformed id. The caller shows the raw number instead.
    public static string? Resolve(long locationId)
    {
        long n = locationId - Base;
        if (n < 0) return null;
        int diff  = (int)(n / 1000);
        int quest = (int)(n % 1000);
        if (diff < 0 || diff > 2) return null;
        if (!QuestToName().TryGetValue(quest, out string? name)) return null;
        return diff switch
        {
            1 => name + " (Nightmare)",
            2 => name + " (Hell)",
            _ => name,
        };
    }
}
