using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text.Json;

namespace LauncherV2.Plugins.DiabloII;

///
/// Builds the tracker pack's artwork on this machine, out of this player's
/// own copy of the game.
///
/// The published pack carries no game pictures. It carries sprites.json — a
/// list saying "this frame of this archive file becomes this PNG" — and we
/// rebuild the images here. The player gets real in-game graphics; nobody
/// distributes Blizzard's art. Same shape as cover art and emulators: the
/// launcher supplies the machinery, the player supplies the content.
///
/// Never throws. A tracker is an extra; a game install must not fail because
/// a picture could not be drawn.
///
public static class D2TrackerPack
{
    public const string PackageUid = "solida_diablo2_ap";

    /// PopTracker's own search path, shared with a copy the player may have
    /// installed themselves.
    public static string PacksDir => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
        "PopTracker", "packs");

    public static string PackDir => Path.Combine(PacksDir, PackageUid);

    /// Is the pack there at all (installed by the launcher)?
    public static bool IsPackPresent =>
        File.Exists(Path.Combine(PackDir, "sprites.json"));

    /// Has the artwork already been built here?
    public static bool IsArtworkBuilt(string? packDir = null)
    {
        try
        {
            var plan = ReadPlan(packDir ?? PackDir);
            if (plan == null || plan.Sprites.Count == 0) return false;
            string root = packDir ?? PackDir;
            foreach (var s in plan.Sprites)
                if (!File.Exists(Path.Combine(root, s.Out.Replace('/', Path.DirectorySeparatorChar))))
                    return false;
            return true;
        }
        catch { return false; }
    }

    ///
    /// Fill in every missing picture. Returns how many were written.
    ///
    /// Archives are opened ONE AT A TIME on purpose: d2data and d2exp are a
    /// quarter of a gigabyte each and the reader holds the whole file, so
    /// three at once would cost the player half a gig of memory for nothing.
    /// Each archive fulfils what it can and is let go.
    ///
    public static int BuildArtwork(string gameDir, string? packDir = null,
                                   Action<string>? log = null)
    {
        string root = packDir ?? PackDir;
        int written = 0, missing = 0;
        try
        {
            var plan = ReadPlan(root);
            if (plan == null)
            {
                log?.Invoke("[Tracker] no sprites.json in the pack - nothing to build");
                return 0;
            }

            // Only what is not already here, so a second run costs nothing.
            var todo = new List<SpriteRecipe>();
            foreach (var s in plan.Sprites)
                if (!File.Exists(Path.Combine(root, s.Out.Replace('/', Path.DirectorySeparatorChar))))
                    todo.Add(s);
            if (todo.Count == 0) return 0;

            log?.Invoke($"[Tracker] building {todo.Count} pictures from your copy of the game");

            // The palette lives in ONE archive, and it is not the same archive
            // as most of the sprites: pal.dat is in d2data while the expansion
            // art is in d2exp. Finding it must therefore be its own pass —
            // gating each archive on "does this one hold the palette" silently
            // skipped every expansion sprite.
            byte[]? palette = FindPalette(gameDir, plan, log);
            if (palette == null)
            {
                log?.Invoke("[Tracker] the game's palette could not be read - "
                          + "no artwork can be built");
                return 0;
            }

            foreach (string arcName in plan.Archives)
            {
                if (todo.Count == 0) break;
                string arcPath = Path.Combine(gameDir, arcName);
                if (!File.Exists(arcPath)) continue;

                D2MpqTables.MpqReader mpq;
                try { mpq = new D2MpqTables.MpqReader(arcPath); }
                catch (Exception ex)
                {
                    log?.Invoke($"[Tracker] {arcName}: not readable ({ex.Message})");
                    continue;
                }

                // Several sprites come from one file (a class's skill sheet
                // holds sixty), so the file is read once and the frames are
                // cut out of it.
                var cache = new Dictionary<string, byte[]?>(StringComparer.OrdinalIgnoreCase);

                for (int i = todo.Count - 1; i >= 0; i--)
                {
                    var recipe = todo[i];
                    if (!cache.TryGetValue(recipe.Mpq, out byte[]? raw))
                    {
                        raw = TryRead(mpq, recipe.Mpq);
                        cache[recipe.Mpq] = raw;
                    }
                    if (raw == null) continue;

                    byte[]? png;
                    try { png = Dc6ToPng(raw, palette, recipe.Frame); }
                    catch { png = null; }
                    if (png == null) continue;

                    if (WriteFile(root, recipe.Out, png)) { written++; todo.RemoveAt(i); }
                }
            }

            // Anything the archives could not give us gets our own stand-in,
            // so the pack always renders rather than showing holes.
            if (todo.Count > 0)
            {
                byte[]? stand = null;
                if (!string.IsNullOrEmpty(plan.Fallback))
                {
                    string p = Path.Combine(root, plan.Fallback!.Replace('/', Path.DirectorySeparatorChar));
                    if (File.Exists(p)) { try { stand = File.ReadAllBytes(p); } catch { } }
                }
                if (stand != null)
                    foreach (var recipe in todo)
                        if (WriteFile(root, recipe.Out, stand)) missing++;
            }

            log?.Invoke($"[Tracker] {written} pictures built"
                      + (missing > 0 ? $", {missing} stood in for" : ""));
        }
        catch (Exception ex)
        {
            log?.Invoke("[Tracker] artwork build failed: " + ex.Message);
        }
        return written;
    }

    // ---------------------------------------------------------------- plan

    private sealed class SpriteRecipe
    {
        public string Out = "";
        public string Mpq = "";
        public int Frame;
    }

    private sealed class SpritePlan
    {
        public string Palette = @"data\global\palette\ACT1\pal.dat";
        public List<string> Archives = new();
        public string? Fallback;
        public List<SpriteRecipe> Sprites = new();
    }

    private static SpritePlan? ReadPlan(string packDir)
    {
        string path = Path.Combine(packDir, "sprites.json");
        if (!File.Exists(path)) return null;
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            var root = doc.RootElement;
            var plan = new SpritePlan();
            if (root.TryGetProperty("palette", out var pal) && pal.GetString() is { } ps)
                plan.Palette = ps;
            if (root.TryGetProperty("fallback", out var fb))
                plan.Fallback = fb.GetString();
            if (root.TryGetProperty("archives", out var arcs)
                && arcs.ValueKind == JsonValueKind.Array)
                foreach (var a in arcs.EnumerateArray())
                    if (a.GetString() is { Length: > 0 } an) plan.Archives.Add(an);
            if (plan.Archives.Count == 0)
                plan.Archives.AddRange(new[] { "Patch_D2.mpq", "d2exp.mpq", "d2data.mpq" });
            if (root.TryGetProperty("sprites", out var sp)
                && sp.ValueKind == JsonValueKind.Array)
                foreach (var s in sp.EnumerateArray())
                {
                    string o = s.TryGetProperty("out", out var ov) ? ov.GetString() ?? "" : "";
                    string m = s.TryGetProperty("mpq", out var mv) ? mv.GetString() ?? "" : "";
                    int f = s.TryGetProperty("frame", out var fv) && fv.TryGetInt32(out int fi) ? fi : 0;
                    // A path that climbs out of the pack is not a sprite.
                    if (o.Length == 0 || m.Length == 0 || o.Contains("..")) continue;
                    plan.Sprites.Add(new SpriteRecipe { Out = o, Mpq = m, Frame = f });
                }
            return plan;
        }
        catch { return null; }
    }

    ///
    /// The game's colour table, wherever it happens to live.
    ///
    /// Searched newest-archive-last: a patch archive holds overrides while the
    /// base archive holds the bulk of the static art, so going backwards
    /// usually finds it in the first archive opened. The loop still covers
    /// every archive, so nothing depends on that being true.
    ///
    private static byte[]? FindPalette(string gameDir, SpritePlan plan, Action<string>? log)
    {
        for (int i = plan.Archives.Count - 1; i >= 0; i--)
        {
            string arcPath = Path.Combine(gameDir, plan.Archives[i]);
            if (!File.Exists(arcPath)) continue;
            try
            {
                var mpq = new D2MpqTables.MpqReader(arcPath);
                byte[]? pal = TryRead(mpq, plan.Palette);
                if (pal is { Length: >= 768 }) return pal;
            }
            catch (Exception ex)
            {
                log?.Invoke($"[Tracker] {plan.Archives[i]}: not readable ({ex.Message})");
            }
        }
        return null;
    }

    private static byte[]? TryRead(D2MpqTables.MpqReader mpq, string name)
    {
        try { return mpq.ReadFile(name); } catch { return null; }
    }

    private static bool WriteFile(string root, string rel, byte[] data)
    {
        try
        {
            string dest = Path.Combine(root, rel.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            File.WriteAllBytes(dest, data);
            return true;
        }
        catch { return false; }
    }

    // ------------------------------------------------------------- DC6 -> PNG

    ///
    /// One frame of a DC6 sprite as an RGBA PNG.
    ///
    /// DC6 is palettised run-length: scanlines are stored BOTTOM-UP, and a
    /// command byte either ends the line (0x80), skips n transparent pixels
    /// (high bit set) or copies the next n palette indices.
    ///
    internal static byte[] Dc6ToPng(byte[] dc6, byte[] pal, int frame)
    {
        int dirs = BitConverter.ToInt32(dc6, 16);
        int perDir = BitConverter.ToInt32(dc6, 20);
        long total = (long)dirs * perDir;
        if (frame < 0 || frame >= total || total <= 0 || total > 100000)
            throw new IndexOutOfRangeException($"frame {frame} of {total}");

        int off = BitConverter.ToInt32(dc6, 24 + frame * 4);
        if (off < 0 || off + 32 > dc6.Length) throw new InvalidDataException("bad frame offset");

        int w = BitConverter.ToInt32(dc6, off + 4);
        int h = BitConverter.ToInt32(dc6, off + 8);
        int length = BitConverter.ToInt32(dc6, off + 28);
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) throw new InvalidDataException("bad frame size");
        int bodyStart = off + 32;
        if (length < 0 || bodyStart + length > dc6.Length) length = dc6.Length - bodyStart;

        var idx = new byte[w * h];
        var alpha = new byte[w * h];
        int x = 0, y = h - 1, i = bodyStart, end = bodyStart + length;
        while (i < end)
        {
            byte cmd = dc6[i++];
            if (cmd == 0x80) { x = 0; y--; }
            else if ((cmd & 0x80) != 0) { x += cmd & 0x7F; }
            else
            {
                for (int n = 0; n < cmd && i < end; n++, x++, i++)
                    if (y >= 0 && y < h && x >= 0 && x < w)
                    {
                        int p = y * w + x;
                        idx[p] = dc6[i];
                        alpha[p] = 255;
                    }
            }
        }

        // pal.dat is 256 entries of B,G,R.
        var raw = new byte[h * (1 + w * 4)];
        int o = 0;
        for (int row = 0; row < h; row++)
        {
            raw[o++] = 0;                       // filter: none
            for (int col = 0; col < w; col++)
            {
                int p = row * w + col;
                int e = idx[p] * 3;
                raw[o++] = pal[e + 2];          // R
                raw[o++] = pal[e + 1];          // G
                raw[o++] = pal[e];              // B
                raw[o++] = alpha[p];
            }
        }
        return Png(w, h, raw);
    }

    private static byte[] Png(int w, int h, byte[] raw)
    {
        using var ms = new MemoryStream();
        ms.Write(new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A }, 0, 8);

        var ihdr = new byte[13];
        WriteBE(ihdr, 0, w); WriteBE(ihdr, 4, h);
        ihdr[8] = 8;        // bit depth
        ihdr[9] = 6;        // colour type: RGBA
        Chunk(ms, "IHDR", ihdr);

        byte[] deflated;
        using (var comp = new MemoryStream())
        {
            using (var z = new ZLibStream(comp, CompressionLevel.SmallestSize, true))
                z.Write(raw, 0, raw.Length);
            deflated = comp.ToArray();
        }
        Chunk(ms, "IDAT", deflated);
        Chunk(ms, "IEND", Array.Empty<byte>());
        return ms.ToArray();
    }

    private static void WriteBE(byte[] b, int at, int v)
    {
        b[at] = (byte)(v >> 24); b[at + 1] = (byte)(v >> 16);
        b[at + 2] = (byte)(v >> 8); b[at + 3] = (byte)v;
    }

    private static void Chunk(Stream s, string tag, byte[] payload)
    {
        var len = new byte[4]; WriteBE(len, 0, payload.Length);
        s.Write(len, 0, 4);
        var body = new byte[4 + payload.Length];
        for (int i = 0; i < 4; i++) body[i] = (byte)tag[i];
        Buffer.BlockCopy(payload, 0, body, 4, payload.Length);
        s.Write(body, 0, body.Length);
        var crc = new byte[4]; WriteBE(crc, 0, unchecked((int)Crc32(body)));
        s.Write(crc, 0, 4);
    }

    private static readonly uint[] CrcTable = BuildCrcTable();

    private static uint[] BuildCrcTable()
    {
        var t = new uint[256];
        for (uint n = 0; n < 256; n++)
        {
            uint c = n;
            for (int k = 0; k < 8; k++)
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }

    private static uint Crc32(byte[] data)
    {
        uint c = 0xFFFFFFFFu;
        foreach (byte b in data) c = CrcTable[(c ^ b) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    }
}
