using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net.Http;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace LauncherV2.Plugins.DiabloII;

// Fetches ONE third-party component from its own project's GitHub release and
// puts the wanted files into the game folder.
//
// Never called without the player having said yes to that specific component.
// This class does the work; D2ComponentWizard does the asking.
public static class D2ComponentInstaller
{
    /// What the caller needs in order to draw two progress bars: which step we
    /// are on, and how far into the current download we are. Bytes are -1 when
    /// the server does not say how big the file is.
    public sealed record Progress(string Stage, long BytesDone, long BytesTotal)
    {
        public double? Fraction => BytesTotal > 0
            ? Math.Clamp((double)BytesDone / BytesTotal, 0, 1)
            : null;
    }

    public sealed record Result(
        bool Success, string Message, IReadOnlyList<string> FilesWritten, string? Version);

    // GitHub refuses anonymous API calls without one, and a plain 403 reads
    // like "the download is broken" rather than "we forgot a header".
    private const string UserAgent = "Multiworld-Launcher-Diablo-Plugin";

    private static HttpClient NewClient()
    {
        var c = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
        c.DefaultRequestHeaders.UserAgent.ParseAdd(UserAgent);
        return c;
    }

    /// Turn "D2GL*.zip" into a predicate. Deliberately tiny: the patterns are
    /// ours, from D2Components, not user input.
    private static bool MatchesPattern(string name, string pattern)
    {
        var parts = pattern.Split('*');
        int pos = 0;
        for (int i = 0; i < parts.Length; i++)
        {
            if (parts[i].Length == 0) continue;
            int at = name.IndexOf(parts[i], pos, StringComparison.OrdinalIgnoreCase);
            if (at < 0) return false;
            // A leading literal must match at the very start.
            if (i == 0 && at != 0) return false;
            pos = at + parts[i].Length;
        }
        // A trailing literal must match at the very end.
        string last = parts[^1];
        if (last.Length > 0 && !name.EndsWith(last, StringComparison.OrdinalIgnoreCase))
            return false;
        return true;
    }

    /// Ask GitHub which file to download. Returns (url, name, size, tag).
    public static async Task<(string Url, string Name, long Size, string Tag)?>
        ResolveAssetAsync(D2Components.Component c, CancellationToken ct)
    {
        using var http = NewClient();

        // A pinned tag is used when the project publishes only prereleases --
        // GitHub's "latest release" endpoint skips those and answers 404.
        string api = c.PinnedTag is { } tag
            ? $"https://api.github.com/repos/{c.Owner}/{c.Repo}/releases/tags/{tag}"
            : $"https://api.github.com/repos/{c.Owner}/{c.Repo}/releases/latest";

        using var resp = await http.GetAsync(api, ct).ConfigureAwait(false);
        if (!resp.IsSuccessStatusCode) return null;

        using var doc = JsonDocument.Parse(
            await resp.Content.ReadAsStringAsync(ct).ConfigureAwait(false));
        var root = doc.RootElement;

        string tagName = root.TryGetProperty("tag_name", out var t)
            ? t.GetString() ?? "" : "";

        if (!root.TryGetProperty("assets", out var assets)
            || assets.ValueKind != JsonValueKind.Array) return null;

        foreach (var a in assets.EnumerateArray())
        {
            string name = a.TryGetProperty("name", out var n) ? n.GetString() ?? "" : "";
            if (!MatchesPattern(name, c.AssetPattern)) continue;

            string url = a.TryGetProperty("browser_download_url", out var u)
                ? u.GetString() ?? "" : "";
            long size = a.TryGetProperty("size", out var s) ? s.GetInt64() : -1;
            if (url.Length > 0) return (url, name, size, tagName);
        }
        return null;
    }

    /// Download + extract one component into `gameDir`.
    ///
    /// Downloads to memory rather than a temp file: the largest of these is
    /// D2GL at ~64 MB, and a half-written file on disk is a worse failure mode
    /// than a dropped buffer.
    public static async Task<Result> InstallAsync(
        D2Components.Component c, string gameDir,
        IProgress<Progress>? progress, CancellationToken ct)
    {
        progress?.Report(new Progress($"Asking GitHub about {c.Name}…", 0, -1));

        var asset = await ResolveAssetAsync(c, ct).ConfigureAwait(false);
        if (asset is null)
            return new Result(false,
                $"Could not find a download for {c.Name} on GitHub. The releases "
              + "page may have changed — use \"Do it myself\" and grab it by hand.",
                Array.Empty<string>(), null);

        byte[] archive;
        try
        {
            archive = await DownloadAsync(asset.Value.Url, asset.Value.Size,
                                          c.Name, progress, ct).ConfigureAwait(false);
        }
        catch (OperationCanceledException) { throw; }
        catch (Exception ex)
        {
            return new Result(false, $"Download failed: {ex.Message}",
                              Array.Empty<string>(), asset.Value.Tag);
        }

        progress?.Report(new Progress($"Unpacking {c.Name}…", 0, -1));
        try
        {
            var written = Extract(archive, c, gameDir);
            var missing = c.Files.Where(f => f.Required)
                                 .Select(f => f.SaveAs)
                                 .Where(s => !written.Contains(s, StringComparer.OrdinalIgnoreCase))
                                 .ToList();
            if (missing.Count > 0)
                return new Result(false,
                    $"{c.Name} downloaded, but these files were not inside the "
                  + $"archive: {string.Join(", ", missing)}. The project may have "
                  + "changed its layout — please report this.",
                    written, asset.Value.Tag);

            return new Result(true,
                $"{c.Name} {asset.Value.Tag} installed ({written.Count} files).",
                written, asset.Value.Tag);
        }
        catch (Exception ex)
        {
            return new Result(false, $"Could not unpack {c.Name}: {ex.Message}",
                              Array.Empty<string>(), asset.Value.Tag);
        }
    }

    private static async Task<byte[]> DownloadAsync(
        string url, long expected, string name,
        IProgress<Progress>? progress, CancellationToken ct)
    {
        using var http = NewClient();
        using var resp = await http.GetAsync(
            url, HttpCompletionOption.ResponseHeadersRead, ct).ConfigureAwait(false);
        resp.EnsureSuccessStatusCode();

        long total = resp.Content.Headers.ContentLength ?? expected;
        using var src = await resp.Content.ReadAsStreamAsync(ct).ConfigureAwait(false);
        using var dst = new MemoryStream(total > 0 ? (int)total : 4 * 1024 * 1024);

        var buffer = new byte[81920];
        long done = 0;
        int read;
        while ((read = await src.ReadAsync(buffer, ct).ConfigureAwait(false)) > 0)
        {
            dst.Write(buffer, 0, read);
            done += read;
            progress?.Report(new Progress($"Downloading {name}…", done, total));
        }
        return dst.ToArray();
    }

    /// Pull the wanted entries out, following one level of nesting when the
    /// component says its release zip contains another archive.
    private static List<string> Extract(
        byte[] archiveBytes, D2Components.Component c, string gameDir)
    {
        Directory.CreateDirectory(gameDir);
        using var outer = new ZipArchive(new MemoryStream(archiveBytes), ZipArchiveMode.Read);

        ZipArchive zip = outer;
        MemoryStream? innerStream = null;
        ZipArchive? inner = null;

        if (c.NestedArchive is { } pattern)
        {
            var entry = outer.Entries.FirstOrDefault(e => MatchesPattern(e.Name, pattern))
                ?? throw new InvalidDataException(
                    $"expected an archive matching {pattern} inside the download");
            innerStream = new MemoryStream();
            using (var s = entry.Open()) s.CopyTo(innerStream);
            innerStream.Position = 0;
            inner = new ZipArchive(innerStream, ZipArchiveMode.Read);
            zip = inner;
        }

        try
        {
            var written = new List<string>();
            foreach (var want in c.Files)
            {
                // Zip separators are '/' by spec; compare on the full entry path
                // so DSOAL/Win32/dsound.dll cannot be satisfied by Win64's.
                var entry = zip.Entries.FirstOrDefault(e =>
                    string.Equals(e.FullName.Replace('\\', '/'), want.PathInArchive,
                                  StringComparison.OrdinalIgnoreCase));
                if (entry is null)
                {
                    if (want.Required) continue;   // reported by the caller
                    continue;
                }

                string dest = Path.Combine(gameDir, want.SaveAs);
                // Guard against an archive entry escaping the game folder.
                if (!Path.GetFullPath(dest).StartsWith(
                        Path.GetFullPath(gameDir), StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException(
                        $"archive entry {want.PathInArchive} would write outside the game folder");

                entry.ExtractToFile(dest, overwrite: true);
                written.Add(want.SaveAs);
            }
            return written;
        }
        finally
        {
            inner?.Dispose();
            innerStream?.Dispose();
        }
    }
}
