using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace LauncherV2.Plugins.DiabloII;

//
// Decide whether a generated Diablo II seed can actually be finished.

// Archipelago's generator proves the seed is beatable under the LOGIC MODEL it
// was given. This proves something the generator cannot: that the model matches
// the world the game will really build.
// matters — entrance shuffle physically relocates dungeons, and the model
// describes vanilla topology.
// shuffle on and you may come out somewhere else entirely; a gate key sitting
// in the Den is then only obtainable from wherever the shuffle put it.

// So the check rebuilds the shuffle exactly as the DLL will (same Sattolo
// permutation, same seed, same cow pre-placement), remaps every check to the
// entrance that now leads to it, and then sweeps: collect what is reachable,
// use it to open gates, repeat until nothing new appears.
// unreachable at the end is reported with the reason.
//
public static class D2SeedCheck
{
    public const string GameName = "Diablo II Archipelago";

    // --- result types ---

    public sealed class Problem
    {
        public string Severity = "";      // "blocker" | "warning"
        public string Title = "";
        public string Detail = "";
        public override string ToString() => $"[{Severity}] {Title}: {Detail}";
    }

    public sealed class Report
    {
        public bool Ok;
        public string SlotName = "";
        public string SeedName = "";
        public bool ZoneLocking;
        public bool EntranceShuffle;
        public string Goal = "";
        public int LocationCount;
        public int KeyCount;
        public int ReachableCount;
        // Items of ours that another player's world holds, and how many of
        // those are gate keys — the part this check cannot verify itself.
        public int ForeignItemCount;
        public int ForeignKeyCount;
        // True when entrance shuffle is on but the world seed was unavailable.
        public bool SeedUnknown;
        // Key-depth audit (zone locking only): how many of this slot's gate
        // keys AP's playthrough needs, and how many of those sit DEEPER than
        // the act they open — the "your Act 1 key is in someone's Act 5
        // Nightmare" lockout, judged from AP's own sphere numbers.
        public int KeysInPlaythrough;
        public int KeyDepthViolations;
        public List<Problem> Problems = new();
        // One entry per sphere: how many of this slot's checks open up there.
        public List<int> Spheres = new();
        // Human-readable entrance moves, no item information.
        public List<string> EntranceMoves = new();
        public List<string> Notes = new();
    }

    // ── the game's own permutation, reproduced ──────────────────────────────

    // Sattolo's algorithm with the DLL's LCG (d2arch_levelshuffle.c
    // BuildPermutation). Must stay bit-identical: a different permutation
    // would make this whole check confidently wrong.
    private static int[] BuildPermutation(int size, uint seed)
    {
        var perm = new int[size];
        for (int i = 0; i < size; i++) perm[i] = i;
        if (size < 2) return perm;
        unchecked
        {
            uint s = seed * 2654435761u;
            for (int i = size - 1; i > 0; i--)
            {
                s = s * 1103515245u + 12345u;
                int j = (int)((s >> 16) % (uint)i);
                (perm[i], perm[j]) = (perm[j], perm[i]);
            }
        }
        return perm;
    }

    // setIndex -> the set whose dungeon you actually arrive in.
    // Mirrors ApplyEntranceShuffle: pool A is acts 1+2, pool B is acts 3-5,
    // the cow pairing is pre-placed before pool B is permuted, and pinned sets
    // keep the identity mapping.
    public static int[] BuildShuffleMap(uint seed)
    {
        var sets = D2LogicTables.DungeonSets;
        int n = sets.Length;
        var map = new int[n];
        for (int i = 0; i < n; i++) map[i] = i;

        var poolA = new List<int>();
        var poolB = new List<int>();
        int cowIdx = -1;
        var cowEligible = new List<int>();
        for (int i = 0; i < n; i++)
        {
            if (sets[i].IsCow) cowIdx = i;
            if (sets[i].CowEligible) cowEligible.Add(i);
            if (sets[i].Pinned) continue;
            if (sets[i].Act >= 1 && sets[i].Act <= 2) poolA.Add(i);
            else if (sets[i].Act >= 3 && sets[i].Act <= 5) poolB.Add(i);
        }

        if (poolA.Count >= 2)
        {
            var permA = BuildPermutation(poolA.Count, seed);
            for (int i = 0; i < poolA.Count; i++) map[poolA[i]] = poolA[permA[i]];
        }

        int cowPartner = -1;
        if (cowIdx >= 0 && cowEligible.Count > 0)
        {
            unchecked
            {
                uint pickSeed = (seed * 1234567u) ^ 0xCAFEF00Du;
                cowPartner = cowEligible[(int)(pickSeed % (uint)cowEligible.Count)];
            }
            map[cowIdx] = cowPartner;
            map[cowPartner] = cowIdx;
        }

        var rest = poolB.Where(i => i != cowIdx && i != cowPartner).ToList();
        if (rest.Count >= 2)
        {
            var permB = BuildPermutation(rest.Count, seed ^ 0xA5A5A5A5u);
            for (int i = 0; i < rest.Count; i++) map[rest[i]] = rest[permB[i]];
        }
        return map;
    }

    // The launcher's stable per-slot seed — identical to D2Plugin.StableApSeed,
    // and what the DLL now uses as its master seed ([settings] SeedKey).
    public static uint ShuffleSeedFor(string seedName, string slotName)
    {
        string basis = seedName + "|" + slotName;
        ulong h = 14695981039346656037UL;
        foreach (char c in basis) { h ^= c; h *= 1099511628211UL; }
        long key = (long)(h & 0x7FFFFFFFFFFFFFFFUL);
        return unchecked((uint)((ulong)key ^ ((ulong)key >> 32)));
    }

    // --- spoiler parsing ---

    public sealed class Spoiler
    {
        public string SeedName = "";
        // slot name -> (location -> item) for Diablo II slots only.
        public Dictionary<string, Dictionary<string, string>> Placements = new();
        // slot name -> option name -> raw value, from the spoiler's own dump.
        public Dictionary<string, Dictionary<string, string>> Options = new();
        public List<string> D2Slots = new();
        // slot -> items destined for that slot but placed in another world.
        public Dictionary<string, List<string>> Foreign = new();
        // slot -> location -> who actually receives what sits there.
        // OUR location may belong to somebody else, and must not open OUR gates.
        public Dictionary<string, Dictionary<string, string>> Receiver = new();
        // Every player name in the multiworld, Diablo II or not.
        // tell a receiver tag "(D2P2)" from an item name that merely ends in
        // parentheses, such as "Progressive Act 2 Key (Normal)".
        public HashSet<string> AllPlayers = new(StringComparer.Ordinal);
        public int PlayerCount;

        // One entry per gate key that appears in the spoiler's Playthrough —
        // Archipelago's own sphere computation, which covers EVERY game in the
        // multiworld. Reading it is what lets the depth audit judge a key
        // sitting in DOOM or Factorio without knowing a thing about their
        // logic: AP already did that work when it wrote the section.
        public sealed class PlaythroughKey
        {
            public string Receiver = ""; public int Act; public string Diff = "";
            public int Sphere; public string Where = "";
        }
        public List<PlaythroughKey> PlaythroughKeys = new();
        // False when the spoiler was generated below level 3 — no Playthrough
        // section, so key depth cannot be audited and the report says so.
        public bool HasPlaythrough;
    }

    // A whole "Location: Progressive Act N Key (Diff) (receiver)" line,
    // anchored at the END. Anchoring there is the trick: the location half can
    // be any game's name with any punctuation ("Central Processing (E1M6) -
    // Mega Armor (Player2)"), but a key item is fully rigid, so matching the
    // tail identifies the line no matter what precedes it.
    private static readonly Regex KeyLineRe = new(
        @"^(?<loc>.+?): Progressive Act (?<act>\d) Key \((?<diff>Normal|Nightmare|Hell)\)(?: \((?<recv>[^()]+)\))?$",
        RegexOptions.Compiled);

    // A spoiler line is "Location: Item", and BOTH sides can contain a colon
    // ("Hunt: Corpsefire: Critical Strike").
    // line is "Slot: Location: Item".
    // hopeless; the location is matched against the names we know instead.
    public static Spoiler ParseSpoiler(string path, IEnumerable<string> knownLocations)
    {
        var known = new HashSet<string>(knownLocations, StringComparer.Ordinal);
        var text = File.ReadAllText(path).Replace("\r\n", "\n");
        var sp = new Spoiler();

        var mSeed = Regex.Match(text, @"^Archipelago Version .*?Seed:\s*(\S+)",
                                RegexOptions.Multiline);
        if (mSeed.Success) sp.SeedName = mSeed.Groups[1].Value.Trim();

        // The header is "Key:<padding>Value" for every setting, so keys and
        // values are split on the first colon and trimmed — never on position.
        static Dictionary<string, string> ReadOptions(string block)
        {
            var opts = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var line in block.Split('\n'))
            {
                int c = line.IndexOf(':');
                if (c <= 0) continue;
                var key = line[..c].Trim();
                if (key.Length == 0 || key.Contains(' ') && key.Length > 40) continue;
                opts[key] = line[(c + 1)..].Trim();
            }
            return opts;
        }

        var mPlayers = Regex.Match(text, @"^Players:\s*(\d+)", RegexOptions.Multiline);
        if (mPlayers.Success)
            sp.PlayerCount = int.Parse(mPlayers.Groups[1].Value, CultureInfo.InvariantCulture);

        // Multi-player spoilers repeat a "Player N: <name>" block per slot.
        foreach (Match m in Regex.Matches(
                     text, @"^Player \d+:[ \t]*(.+?)\n(.*?)(?=\nPlayer \d+:|\nLocations:|\Z)",
                     RegexOptions.Multiline | RegexOptions.Singleline))
        {
            string slot = m.Groups[1].Value.Trim();
            sp.AllPlayers.Add(slot);
            var opts = ReadOptions(m.Groups[2].Value);
            if (!opts.TryGetValue("Game", out var game) ||
                !string.Equals(game, GameName, StringComparison.Ordinal)) continue;
            sp.D2Slots.Add(slot);
            sp.Options[slot] = opts;
        }

        // A one-player spoiler has no Player header at all — the settings sit
        // directly under the version line.
        if (sp.D2Slots.Count == 0)
        {
            var head = text.Split("\nLocations:\n")[0];
            var opts = ReadOptions(head);
            if (opts.TryGetValue("Game", out var game) &&
                string.Equals(game, GameName, StringComparison.Ordinal))
            {
                string slot = opts.TryGetValue("Name", out var nm) && nm.Length > 0
                    ? nm : "Player 1";
                sp.D2Slots.Add(slot);
                sp.AllPlayers.Add(slot);
                sp.Options[slot] = opts;
                if (sp.PlayerCount == 0) sp.PlayerCount = 1;
            }
        }

        int start = text.IndexOf("\nLocations:\n", StringComparison.Ordinal);
        if (start < 0) return sp;
        string body = text[(start + "\nLocations:\n".Length)..];
        int end = body.IndexOf("\nPlaythrough:", StringComparison.Ordinal);
        if (end >= 0) body = body[..end];

        // Two line shapes, and mistaking one for the other fails silently:
        // one player "Clear Barracks: Drop: Random Charm"
        // multiworld "Clear Barracks (D2P1): Progressive Act 1 Key (Normal) (D2P2)"
        // The owner of the LOCATION and the receiver of the ITEM are each in
        // trailing parentheses.
        // own ("Hunt: Corpsefire", "Drop: Random Charm"), so the split point is
        // found by matching real location names, never by punctuation.
        foreach (var raw in body.Split('\n'))
        {
            var line = raw.Trim();
            if (line.Length == 0 || !line.Contains(": ")) continue;

            int cut = -1;
            string locName = "", owner = "";
            for (int i = line.Length - 2; i > 0; i--)
            {
                if (line[i] != ':' || line[i + 1] != ' ') continue;
                string left = line[..i], bare = left, own = "";
                if (left.EndsWith(")", StringComparison.Ordinal))
                {
                    int op = left.LastIndexOf(" (", StringComparison.Ordinal);
                    if (op > 0) { own = left[(op + 2)..^1]; bare = left[..op]; }
                }
                if (!known.Contains(bare)) continue;
                cut = i; locName = bare; owner = own;
                break;
            }

            string item = cut >= 0 ? line[(cut + 2)..].Trim() : "";
            string receiver = "";
            // Only a PLAYER NAME in trailing parentheses is a receiver tag.
            // Stripping every parenthetical turned "Progressive Act 2 Key
            // (Normal)" into "Progressive Act 2 Key" and the key stopped being
            // recognised at all — a single-player seed then reported zero keys.
            if (item.EndsWith(")", StringComparison.Ordinal))
            {
                int op = item.LastIndexOf(" (", StringComparison.Ordinal);
                if (op > 0 && sp.AllPlayers.Contains(item[(op + 2)..^1]))
                {
                    receiver = item[(op + 2)..^1];
                    item = item[..op];
                }
            }

            if (owner.Length == 0 && cut >= 0 && sp.D2Slots.Count == 1)
                owner = sp.D2Slots[0];
            if (receiver.Length == 0) receiver = owner;

            // The location belongs to a Diablo II slot: record what sits on it,
            // so that slot's walk can find it.
            if (cut >= 0 && owner.Length > 0 && sp.D2Slots.Contains(owner))
            {
                if (!sp.Placements.TryGetValue(owner, out var d))
                    sp.Placements[owner] = d = new Dictionary<string, string>(StringComparer.Ordinal);
                d[locName] = item;
                if (!sp.Receiver.TryGetValue(owner, out var rc))
                    sp.Receiver[owner] = rc = new Dictionary<string, string>(StringComparer.Ordinal);
                rc[locName] = receiver.Length > 0 ? receiver : owner;
            }

            // The ITEM belongs to a Diablo II slot other than the one owning
            // the location — another Diablo II player's world counts as
            // "elsewhere" just as much as a different game does.
            // can never find it, so it is recorded as foreign and treated as
            // obtainable; the report says how many there are.
            if (item.Length > 0 && receiver.Length > 0 &&
                sp.D2Slots.Contains(receiver) && receiver != owner)
            {
                if (!sp.Foreign.TryGetValue(receiver, out var lst))
                    sp.Foreign[receiver] = lst = new List<string>();
                lst.Add(item);
            }

            // Foreign location — belongs to another player's world; only ours are simulated.
            if (cut < 0)
            {
                var km = KeyLineRe.Match(line);
                if (km.Success && km.Groups["recv"].Success)
                {
                    string kr = km.Groups["recv"].Value;
                    if (sp.D2Slots.Contains(kr))
                    {
                        if (!sp.Foreign.TryGetValue(kr, out var lst2))
                            sp.Foreign[kr] = lst2 = new List<string>();
                        lst2.Add($"Progressive Act {km.Groups["act"].Value} Key ({km.Groups["diff"].Value})");
                    }
                }
            }
        }

        ParsePlaythrough(text, sp);
        return sp;
    }

    // The Playthrough section is "N: {" blocks, one line per required item,
    // written by AP's own sphere sweep across the WHOLE multiworld.
    // keys destined for a Diablo II slot are kept.
    // section are the copies AP proved it never needs — their depth cannot
    // hurt anyone, which is why auditing just this section is sufficient.
    private static void ParsePlaythrough(string text, Spoiler sp)
    {
        int pt = text.IndexOf("\nPlaythrough:", StringComparison.Ordinal);
        if (pt < 0) return;
        sp.HasPlaythrough = true;
        string body = text[(pt + "\nPlaythrough:".Length)..];
        int end = body.IndexOf("\nPaths:", StringComparison.Ordinal);
        if (end >= 0) body = body[..end];

        int sphere = -1;
        foreach (var raw in body.Split('\n'))
        {
            var line = raw.Trim();
            var mS = Regex.Match(line, @"^(\d+): \{");
            if (mS.Success)
            {
                sphere = int.Parse(mS.Groups[1].Value, CultureInfo.InvariantCulture);
                continue;
            }
            if (sphere < 0 || line.Length == 0) continue;
            var mK = KeyLineRe.Match(line);
            if (!mK.Success) continue;
            string recv = mK.Groups["recv"].Success ? mK.Groups["recv"].Value : "";
            // A single-player spoiler carries no receiver tags at all.
            if (recv.Length == 0 && sp.D2Slots.Count == 1) recv = sp.D2Slots[0];
            if (!sp.D2Slots.Contains(recv)) continue;
            sp.PlaythroughKeys.Add(new Spoiler.PlaythroughKey
            {
                Receiver = recv,
                Act = int.Parse(mK.Groups["act"].Value, CultureInfo.InvariantCulture),
                Diff = mK.Groups["diff"].Value,
                Sphere = sphere,
                Where = mK.Groups["loc"].Value,
            });
        }
    }

    // --- the sweep ---

    private static readonly Regex KeyRe =
        new(@"^Progressive Act (\d) Key \((Normal|Nightmare|Hell)\)$", RegexOptions.Compiled);

    // location name -> quest id, difficulty.
    // the plugin already ships (name suffix carries the difficulty).
    // shuffleSeed:
    // The world seed the GAME will use, read from the install's d2arch.ini
    // ([settings] SeedKey).
    
    // The master seed comes from the spoiler, not derived — it must match generation.
    public static Report Check(Spoiler sp, string slot,
                               IReadOnlyDictionary<string, int> locationQuestIds,
                               uint? shuffleSeed = null)
    {
        var r = new Report { SlotName = slot, SeedName = sp.SeedName };
        if (!sp.Placements.TryGetValue(slot, out var placements))
        {
            r.Problems.Add(new Problem
            {
                Severity = "blocker",
                Title = "No placements found for this slot",
                Detail = "The spoiler has no location lines for " + slot +
                         ". Is it the right spoiler, and was it generated with " +
                         "spoiler level 2 or 3?"
            });
            return r;
        }
        sp.Options.TryGetValue(slot, out var opts);
        opts ??= new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        bool Yes(string k) => opts.TryGetValue(k, out var v) &&
                              (v.Equals("Yes", StringComparison.OrdinalIgnoreCase) ||
                               v == "1" || v.Equals("true", StringComparison.OrdinalIgnoreCase));
        r.ZoneLocking = Yes("Zone Locking");
        r.EntranceShuffle = Yes("Entrance Shuffle");
        r.Goal = opts.TryGetValue("Goal", out var g) ? g : "(unknown)";
        r.LocationCount = placements.Count;

        // Where does each check physically sit, after any entrance shuffle?
        int[]? map = (r.EntranceShuffle && shuffleSeed.HasValue)
            ? BuildShuffleMap(shuffleSeed.Value)
            : null;
        if (r.EntranceShuffle && !shuffleSeed.HasValue)
        {
            r.SeedUnknown = true;
            r.Problems.Add(new Problem
            {
                Severity = "warning",
                Title    = "Entrance layout could not be checked",
                Detail   = "This seed uses entrance shuffle, and the layout is "
                         + "decided by the game from a seed stored in the "
                         + "install (Archipelago/d2arch.ini, SeedKey). It was "
                         + "not readable, so the checks below assume the normal "
                         + "map. Launch the game once with this slot and check "
                         + "again to get the real layout.",
            });
        }
        var zoneEntryRegion = BuildZoneEntryRegions(map, r);

        // Requirement per location: (act, keys needed).
        // With zone locking off there are no gates, so nothing is behind a
        // key and every check is reachable in the order the game presents it.
        // Applying the gate model anyway reported a perfectly normal seed as
        // impossible.
        var need = new Dictionary<string, (int Act, int Keys)>(StringComparer.Ordinal);
        foreach (var loc in r.ZoneLocking ? placements.Keys : Enumerable.Empty<string>())
        {
            string bare = Regex.Replace(loc, @" \((Nightmare|Hell)\)$", "");
            if (!locationQuestIds.TryGetValue(bare, out int qid)) continue;
            if (!D2LogicTables.QuestZone.TryGetValue(qid, out int zone)) continue;
            if (D2LogicTables.AlwaysOpenZones.Contains(zone)) continue;
            if (!zoneEntryRegion.TryGetValue(zone, out var ar)) continue;
            need[loc] = (ar.Act, Math.Max(0, ar.Region - 1));
        }

        // Sweep: everything with no requirement is sphere 0; each sphere hands
        // out the keys found in the previous one.
        var haveKeys = new Dictionary<(int Act, string Diff), int>();
        sp.Receiver.TryGetValue(slot, out var receivers);

        // Keys of ours sitting in another player's world start as held.
        // note in ParseSpoiler: whether THAT player can reach them is
        // Archipelago's business and it already proved the multiworld beatable.
        // What only we can check is a key hidden behind its own gate in OUR
        // world, and that stays checked exactly as before.
        if (sp.Foreign.TryGetValue(slot, out var foreign))
        {
            foreach (var item in foreign)
            {
                var fm = KeyRe.Match(item);
                if (!fm.Success) continue;
                var fk = (int.Parse(fm.Groups[1].Value, CultureInfo.InvariantCulture),
                          fm.Groups[2].Value);
                haveKeys.TryGetValue(fk, out int fc);
                haveKeys[fk] = fc + 1;
                r.ForeignKeyCount++;
            }
            r.ForeignItemCount = foreign.Count;
        }
        var got = new HashSet<string>(StringComparer.Ordinal);
        bool progressed = true;
        while (progressed)
        {
            progressed = false;
            int thisSphere = 0;
            var newlyFound = new List<(string Loc, string Item)>();
            foreach (var (loc, item) in placements)
            {
                if (got.Contains(loc)) continue;
                if (need.TryGetValue(loc, out var req) && req.Keys > 0)
                {
                    string diff = loc.EndsWith("(Hell)", StringComparison.Ordinal) ? "Hell"
                                : loc.EndsWith("(Nightmare)", StringComparison.Ordinal) ? "Nightmare"
                                : "Normal";
                    haveKeys.TryGetValue((req.Act, diff), out int have);
                    if (have < req.Keys) continue;
                }
                got.Add(loc);
                newlyFound.Add((loc, item));
                thisSphere++;
                progressed = true;
            }
            foreach (var (loc, item) in newlyFound)
            {
                // A key lying in our world but addressed to another player is
                // theirs; it opens nothing for us.
                if (receivers != null && receivers.TryGetValue(loc, out var rcv)
                    && rcv != slot) continue;
                var m = KeyRe.Match(item);
                if (!m.Success) continue;
                var k = (int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture),
                         m.Groups[2].Value);
                haveKeys.TryGetValue(k, out int c);
                haveKeys[k] = c + 1;
            }
            if (thisSphere > 0) r.Spheres.Add(thisSphere);
        }
        r.ReachableCount = got.Count;
        r.KeyCount = placements.Count(kv => KeyRe.IsMatch(kv.Value)
            && (receivers == null || !receivers.TryGetValue(kv.Key, out var rc2) || rc2 == slot));

        foreach (var (loc, _) in placements)
        {
            if (got.Contains(loc)) continue;
            need.TryGetValue(loc, out var req);
            string item = placements[loc];
            bool isKey = KeyRe.IsMatch(item);
            r.Problems.Add(new Problem
            {
                Severity = isKey ? "blocker" : "warning",
                Title = isKey ? "A gate key cannot be reached" : "A check cannot be reached",
                Detail = $"\"{loc}\" needs {req.Keys} Act {req.Act} key(s) to enter, " +
                         (isKey ? "and the key that opens the way is inside it."
                                : "and no route to it opens up.")
            });
        }

        // --- key depth: act-N keys must sit within sphere N (keyfill contract) ---
        
        // Judged from AP's own Playthrough spheres, so a key sitting in ANY
        // game is covered without modelling that game.
        // playthrough are copies AP never needs; their depth cannot strand
        // anyone.
        if (r.ZoneLocking)
        {
            if (!sp.HasPlaythrough)
            {
                r.Problems.Add(new Problem
                {
                    Severity = "warning",
                    Title    = "Key depth could not be audited",
                    Detail   = "The spoiler has no Playthrough section. Generate "
                             + "with spoiler level 3 to also verify that every "
                             + "gate key arrives no later than the act it opens.",
                });
            }
            else
            {
                foreach (var k in sp.PlaythroughKeys)
                {
                    if (!string.Equals(k.Receiver, slot, StringComparison.Ordinal))
                        continue;
                    r.KeysInPlaythrough++;
                    int diffIdx = k.Diff == "Hell" ? 2 : k.Diff == "Nightmare" ? 1 : 0;
                    int bound = diffIdx * 5 + k.Act;
                    if (k.Sphere <= bound) continue;
                    r.KeyDepthViolations++;
                    r.Problems.Add(new Problem
                    {
                        Severity = "blocker",
                        Title    = "A gate key sits too deep in the multiworld",
                        Detail   = $"Progressive Act {k.Act} Key ({k.Diff}) is first "
                                 + $"obtainable in sphere {k.Sphere}, but the act it "
                                 + $"opens is sphere {bound} at the latest. It sits at "
                                 + $"\"{k.Where}\" — this world stands still until "
                                 + "whoever owns that location gets there. Regenerate "
                                 + "the seed with the current apworld.",
                    });
                }
            }
        }

        r.Ok = !r.Problems.Any(p => p.Severity == "blocker");
        if (r.Ok && r.ReachableCount < r.LocationCount)
        {
            r.Notes.Add($"{r.LocationCount - r.ReachableCount} check(s) sit behind " +
                        "requirements this check does not model (level milestones, " +
                        "collection targets); they are not gate-locked.");
        }
        return r;
    }

    // zone -> the (act, region) you must actually be able to reach to get in.
    // Without entrance shuffle that is simply the zone's own region.
    // a dungeon is entered through whichever set's entrance now leads there,
    // so the requirement is that ENTRANCE's region — the inverse of the
    // shuffle map, because map[from] = to.
    private static Dictionary<int, (int Act, int Region)> BuildZoneEntryRegions(
        int[]? map, Report? r)
    {
        var result = new Dictionary<int, (int Act, int Region)>();
        foreach (var kv in D2LogicTables.ZoneRegion) result[kv.Key] = kv.Value;
        foreach (var kv in D2LogicTables.PortalEntryRegion) result[kv.Key] = kv.Value;
        if (map == null) return result;

        var sets = D2LogicTables.DungeonSets;
        for (int from = 0; from < map.Length; from++)
        {
            int to = map[from];
            if (to == from) continue;
            // Entering `from`'s entrance drops you in `to`'s dungeon, so every
            // zone of `to` now costs what `from`'s entrance costs.
            int entryZone = sets[from].Zones.Length > 0 ? sets[from].Zones[0] : -1;
            if (entryZone < 0 || !D2LogicTables.ZoneRegion.TryGetValue(entryZone, out var er))
                continue;
            foreach (int z in sets[to].Zones) result[z] = er;
            if (r != null)
                r.EntranceMoves.Add($"{sets[to].Name} is now entered from " +
                                    $"{sets[from].Name} (Act {er.Act}, region {er.Region})");
        }
        return result;
    }

    // --- presentation ---

    public static string Format(Report r)
    {
        var sb = new StringBuilder();
        sb.AppendLine(r.Ok
            ? (r.SeedUnknown
                ? "APPROVED for the normal map — entrance layout not checked."
                : "APPROVED — this seed can be completed.")
            : "NOT APPROVED — this seed cannot be completed.");
        sb.AppendLine();
        sb.AppendLine($"Slot            {r.SlotName}");
        sb.AppendLine($"Goal            {r.Goal}");
        sb.AppendLine($"Zone locking    {(r.ZoneLocking ? "on" : "off")}");
        sb.AppendLine($"Entrance shuffle{(r.EntranceShuffle ? " on" : " off")}");
        sb.AppendLine($"Checks          {r.ReachableCount} of {r.LocationCount} reachable");
        sb.AppendLine($"Gate keys       {r.KeyCount} in your own world");
        if (r.ForeignItemCount > 0)
            sb.AppendLine($"In other worlds {r.ForeignItemCount} of your items, "
                          + $"{r.ForeignKeyCount} of them gate keys");
        if (r.ZoneLocking)
            sb.AppendLine("Key depth       " + (r.KeysInPlaythrough > 0
                ? $"{r.KeysInPlaythrough} keys in AP's playthrough, " +
                  (r.KeyDepthViolations == 0
                      ? "every one within the act it opens"
                      : $"{r.KeyDepthViolations} TOO DEEP — see problems")
                : "not audited (spoiler has no Playthrough section)"));
        sb.AppendLine();
        sb.AppendLine($"Spheres         {r.Spheres.Count}");
        if (r.Spheres.Count > 0)
        {
            sb.AppendLine("  checks opening up per sphere (no item names):");
            sb.AppendLine("  " + string.Join(" -> ", r.Spheres));
            sb.AppendLine($"  biggest {r.Spheres.Max()}, smallest {r.Spheres.Min()}, " +
                          $"average {r.Spheres.Average():0.#}");
        }
        if (r.EntranceShuffle && r.EntranceMoves.Count > 0)
        {
            sb.AppendLine();
            sb.AppendLine($"Entrances moved {r.EntranceMoves.Count}");
            foreach (var m in r.EntranceMoves.Take(40)) sb.AppendLine("  " + m);
            if (r.EntranceMoves.Count > 40)
                sb.AppendLine($"  ... and {r.EntranceMoves.Count - 40} more");
        }
        if (r.Problems.Count > 0)
        {
            sb.AppendLine();
            sb.AppendLine("Problems");
            foreach (var p in r.Problems.OrderBy(p => p.Severity == "warning"))
                sb.AppendLine($"  {p.Title}\n    {p.Detail}");
        }
        if (r.ForeignKeyCount > 0)
        {
            sb.AppendLine();
            sb.AppendLine($"{r.ForeignKeyCount} of your gate keys sit in other players'");
            sb.AppendLine("worlds. That is fine BY ITSELF -- keys are meant to travel -- and");
            sb.AppendLine("the key-depth audit above verifies the part that is not automatic:");
            sb.AppendLine("that none of them sits deeper in the multiworld than the act it");
            sb.AppendLine("opens, so this world never has to wait for someone else's endgame.");
        }
        foreach (var n in r.Notes) { sb.AppendLine(); sb.AppendLine(n); }
        return sb.ToString();
    }
}
