using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace LauncherV2.Plugins.DiabloII;

//
// One standalone "world": a concrete seed + the randomizer settings it was
// created with. Its characters live in their own save folder
// (Game\save\seed_<seed>\), so D2's character-select only shows
// the characters that belong to this seed.
//
public sealed class D2SeedInfo
{
    public long     Seed     { get; set; }
    public DateTime Created  { get; set; } = DateTime.Now;
    public D2RandomizerSettings Settings { get; set; } = new();

    // Character names (.d2s files) found in this seed's save folder.
    // Filled in by D2SeedLibrary.ListSeeds — not persisted.
    public List<string> Characters { get; set; } = new();
}

//
// What the standalone dialog hands back: either start a NEW seed (with the
// chosen settings) or LOAD an existing seed (replay its world + characters).
//
public sealed class D2StandaloneLaunchChoice
{
    public bool IsLoad { get; set; }
    public long Seed   { get; set; }
    public D2RandomizerSettings Settings { get; set; } = new();
}

//
// Manages the per-seed save folders under Game\save\.
// seed gets its own seed_<seed>\ directory with a
// seed_meta.json describing the settings it was generated with.
// launcher points D2's save path at the chosen seed folder before launch so
// only that seed's characters appear in-game.
//
public sealed class D2SeedLibrary
{
    private const string MetaFile     = "seed_meta.json";
    private const string ApMarkerFile = "ap_world.json";
    private static readonly JsonSerializerOptions JsonOpts = new() { WriteIndented = true };

    private readonly string _saveRoot;   // <GameDirectory>\save

    public D2SeedLibrary(string gameDirectory)
        => _saveRoot = Path.Combine(gameDirectory, "save");

    // An Archipelago slot's characters live in a seed folder too (same
    // isolation, keyed on room seed + slot name instead of a typed-in seed),
    // but the folder is NOT a standalone world: loading it from the seed
    // library would launch multiworld characters offline, and deleting it
    // there would wipe them.
    // folders out of ListSeeds.
    public void MarkApWorld(long seed, string seedName, string slotName)
    {
        try
        {
            EnsureFolder(seed);
            string p = Path.Combine(SeedFolder(seed), ApMarkerFile);
            if (File.Exists(p)) return;      // keep the original creation time
            File.WriteAllText(p, JsonSerializer.Serialize(
                new { seed_name = seedName, slot = slotName, created = DateTime.Now },
                JsonOpts));
        }
        catch { /* non-fatal — worst case the folder shows up in the library */ }
    }

    public bool IsApWorld(long seed)
        => File.Exists(Path.Combine(SeedFolder(seed), ApMarkerFile));

    // Absolute path of a seed's save folder (created on demand by EnsureFolder).
    public string SeedFolder(long seed)
        => Path.Combine(_saveRoot, "seed_" + seed.ToString(CultureInfo.InvariantCulture));

    public void EnsureFolder(long seed) => Directory.CreateDirectory(SeedFolder(seed));

    // Permanently delete a seed and ALL its characters (the whole save folder).
    // Robust against read-only / OneDrive-locked files: clears attributes and
    // deletes file-by-file as a fallback.
    // still present afterwards.
    public bool DeleteSeed(long seed)
    {
        string dir = SeedFolder(seed);
        if (!Directory.Exists(dir)) return true;

        try
        {
            foreach (var f in Directory.GetFiles(dir, "*", SearchOption.AllDirectories))
                try { File.SetAttributes(f, FileAttributes.Normal); } catch { /* best effort */ }
            Directory.Delete(dir, recursive: true);
        }
        catch
        {
            // Fallback: remove files individually, then the (now-empty) tree.
            try
            {
                foreach (var f in Directory.GetFiles(dir, "*", SearchOption.AllDirectories))
                    try { File.Delete(f); } catch { }
                Directory.Delete(dir, recursive: true);
            }
            catch { /* fall through to the existence check */ }
        }
        return !Directory.Exists(dir);
    }

    // All known seeds, newest first, each with its current character list.
    public List<D2SeedInfo> ListSeeds()
    {
        var list = new List<D2SeedInfo>();
        if (!Directory.Exists(_saveRoot)) return list;

        foreach (string dir in Directory.GetDirectories(_saveRoot, "seed_*"))
        {
            string name = Path.GetFileName(dir);
            if (!long.TryParse(name.Substring("seed_".Length),
                    NumberStyles.Integer, CultureInfo.InvariantCulture, out long seed))
                continue;
            if (File.Exists(Path.Combine(dir, ApMarkerFile)))
                continue;   // an Archipelago slot's folder — not a standalone world

            var info = LoadMeta(seed) ?? new D2SeedInfo
            {
                Seed = seed,
                Created = Directory.GetCreationTime(dir),
            };
            info.Seed = seed;
            try
            {
                info.Characters = Directory.GetFiles(dir, "*.d2s")
                    .Select(Path.GetFileNameWithoutExtension)
                    .Where(n => !string.IsNullOrEmpty(n))
                    .Select(n => n!)
                    .OrderBy(n => n, StringComparer.OrdinalIgnoreCase)
                    .ToList();
            }
            catch { info.Characters = new List<string>(); }
            list.Add(info);
        }
        return list.OrderByDescending(s => s.Created).ToList();
    }

    public D2SeedInfo? LoadMeta(long seed)
    {
        try
        {
            string p = Path.Combine(SeedFolder(seed), MetaFile);
            if (!File.Exists(p)) return null;
            var info = JsonSerializer.Deserialize<D2SeedInfo>(File.ReadAllText(p));
            if (info != null) info.Seed = seed;
            return info;
        }
        catch { return null; }
    }

    // Persist a seed's settings + creation time.
    // Preserves the original Created timestamp when the seed already exists.
    public void SaveMeta(D2SeedInfo info)
    {
        try
        {
            EnsureFolder(info.Seed);
            var existing = LoadMeta(info.Seed);
            if (existing != null) info.Created = existing.Created;
            File.WriteAllText(
                Path.Combine(SeedFolder(info.Seed), MetaFile),
                JsonSerializer.Serialize(info, JsonOpts));
        }
        catch { /* non-fatal — the folder still isolates the characters */ }
    }
}
