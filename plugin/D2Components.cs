using System;
using System.Collections.Generic;

namespace LauncherV2.Plugins.DiabloII;

// The third-party components Diablo II needs on a modern machine, and where
// they come from.
//
// ⛔ NOTHING HERE IS SHIPPED. The mod package contains no third-party binary at
// all -- that was deliberate (see the copyleft removal). Each component is
// fetched from its OWN project's GitHub release, at download time, and only
// after the player has read who wrote it and said yes to that one component.
// The rulebook is explicit: "Skal noget hentes, trykker brugeren selv ja."
//
// Every field below was read off the actual repository and the actual release
// asset on 2026-08-17 -- licences from the LICENSE file, archive layouts from
// the downloaded zips. A guess here becomes a wrong file in someone's game
// folder, which looks exactly like a bug in the mod.
public static class D2Components
{
    /// Where a wanted file sits inside the downloaded archive.
    ///
    /// DSOAL ships a zip INSIDE its release zip, and inside that a Win32 and a
    /// Win64 folder. Diablo II 1.10f is a 32-bit process, so Win64's dsound.dll
    /// would load into nothing and the game would simply have no sound --
    /// silent, and indistinguishable from "the download failed".
    public sealed record ArchiveFile(
        string PathInArchive,   // exact entry name, or the name inside NestedArchive
        string SaveAs,          // file name written into the game folder
        bool Required = true);  // false: nice to have, absent is not a failure

    public sealed record Component(
        string Key,
        string Name,
        string Author,
        string Licence,
        string RepoUrl,
        string ReleasesUrl,
        bool RequiredToPlay,
        string WhatItIs,        // shown to the player: what this does for them
        string WhyNeeded,       // shown to the player: why the game wants it
        string Owner,           // GitHub owner/repo for the release lookup
        string Repo,
        string? PinnedTag,      // null = latest release; DSOAL has no "latest"
        string AssetPattern,    // matched case-insensitively against asset names
        string? NestedArchive,  // an archive inside the asset, or null
        IReadOnlyList<ArchiveFile> Files,
        string? ManualSteps);   // shown when the player picks "Do it myself"

    /// The install order is deliberate: the renderer first, because the game
    /// cannot open a window without one and because SGD2FreeRes is loaded BY
    /// D2GL -- installing the resolution mod first would leave it inert and
    /// look broken.
    public static readonly IReadOnlyList<Component> All = new[]
    {
        new Component(
            Key: "d2gl",
            Name: "D2GL",
            Author: "bayaraa",
            Licence: "GPL-3.0",
            RepoUrl: "https://github.com/bayaraa/d2gl",
            ReleasesUrl: "https://github.com/bayaraa/d2gl/releases",
            RequiredToPlay: true,
            WhatItIs: "A modern graphics renderer for Diablo II — it replaces the "
                    + "1.10f engine's DirectDraw and Glide output with OpenGL, and "
                    + "adds widescreen support, filtering and an uncapped frame rate.",
            WhyNeeded: "Diablo II 1.10f draws through DirectDraw, which modern "
                     + "Windows no longer provides properly. Without a wrapper the "
                     + "game either refuses to open a window or shows a black "
                     + "screen. This is the one component the game cannot start "
                     + "without.",
            Owner: "bayaraa", Repo: "d2gl",
            PinnedTag: null,
            AssetPattern: "D2GL*.zip",
            NestedArchive: null,
            Files: new[]
            {
                new ArchiveFile("ddraw.dll",    "ddraw.dll"),
                new ArchiveFile("glide3x.dll",  "glide3x.dll"),
                new ArchiveFile("d2gl.mpq",     "d2gl.mpq"),
                // The licence travels with the binary. Not a legal requirement
                // for a local download, but it is the honest thing to leave
                // beside someone else's work.
                new ArchiveFile("LICENSE.md",   "D2GL-LICENSE.md", Required: false),
            },
            ManualSteps:
                "1. Open the releases page and download the D2GL zip.\n" +
                "2. Open the zip and copy ddraw.dll, glide3x.dll and d2gl.mpq\n" +
                "   into the mod's game folder (the button below opens it).\n" +
                "3. Come back here and press Check again."),

        new Component(
            Key: "sgd2freeres",
            Name: "SGD2FreeRes",
            Author: "Mir Drualga",
            Licence: "AGPL-3.0",
            RepoUrl: "https://github.com/mir-diablo-ii-tools/SlashGaming-Diablo-II-Free-Resolution",
            ReleasesUrl: "https://github.com/mir-diablo-ii-tools/SlashGaming-Diablo-II-Free-Resolution/releases",
            RequiredToPlay: false,
            WhatItIs: "Unlocks Diablo II's resolution beyond the original 800x600, "
                    + "so the game can fill a modern screen instead of a small box "
                    + "in the middle of it.",
            WhyNeeded: "Optional. The game plays perfectly without it — this only "
                     + "changes how much of the world you can see. It is loaded BY "
                     + "D2GL, so it does nothing on its own.",
            Owner: "mir-diablo-ii-tools", Repo: "SlashGaming-Diablo-II-Free-Resolution",
            PinnedTag: null,
            // Two assets ship per release: "Vanilla" for playing, "Modders" for
            // building. Picking the wrong one installs something that does not
            // run the game.
            AssetPattern: "*Vanilla*.zip",
            NestedArchive: null,
            Files: new[]
            {
                new ArchiveFile("SGD2FreeRes.dll", "SGD2FreeRes.dll"),
                new ArchiveFile("SGD2FreeRes.mpq", "SGD2FreeRes.mpq"),
                new ArchiveFile("SGD2FreeRes - LICENSE.md", "SGD2FreeRes-LICENSE.md",
                                Required: false),
            },
            ManualSteps:
                "1. Open the releases page and download the \"Vanilla\" zip\n" +
                "   (NOT the \"Modders\" one — that is for building it yourself).\n" +
                "2. Copy SGD2FreeRes.dll and SGD2FreeRes.mpq into the game folder.\n" +
                "3. Come back here and press Check again."),

        new Component(
            Key: "dsoal",
            Name: "DSOAL",
            Author: "Chris Robinson (kcat)",
            Licence: "LGPL-2.1",
            RepoUrl: "https://github.com/kcat/dsoal",
            ReleasesUrl: "https://github.com/kcat/dsoal/releases",
            RequiredToPlay: false,
            WhatItIs: "Restores Diablo II's positional and environmental audio by "
                    + "re-implementing DirectSound3D on top of OpenAL Soft.",
            WhyNeeded: "Optional. Windows removed hardware DirectSound3D long ago, "
                     + "so without this the game still has sound — just flat, with "
                     + "no direction or reverb.",
            Owner: "kcat", Repo: "dsoal",
            // DSOAL publishes a ROLLING prerelease. GitHub's "latest release"
            // endpoint ignores prereleases and returns 404 for this repo, so the
            // tag is pinned rather than discovered.
            PinnedTag: "latest-master",
            AssetPattern: "DSOAL.zip",
            // The release zip contains ONE entry: another zip.
            NestedArchive: "DSOAL_*.zip",
            Files: new[]
            {
                // Win32, not Win64: Diablo II 1.10f is a 32-bit process.
                new ArchiveFile("DSOAL/Win32/dsound.dll",      "dsound.dll"),
                new ArchiveFile("DSOAL/Win32/dsoal-aldrv.dll", "dsoal-aldrv.dll"),
                new ArchiveFile("DSOAL/Win32/alsoft.ini",      "alsoft.ini"),
                new ArchiveFile("DSOAL/Documentation/DSOAL-License.txt",
                                "DSOAL-LICENSE.txt", Required: false),
            },
            ManualSteps:
                "1. Open the releases page and download DSOAL.zip.\n" +
                "2. It contains another zip — open that one too.\n" +
                "3. From the Win32 folder (NOT Win64 — the game is 32-bit) copy\n" +
                "   dsound.dll, dsoal-aldrv.dll and alsoft.ini into the game folder.\n" +
                "4. Come back here and press Check again."),
    };

    public static Component? ByKey(string key)
    {
        foreach (var c in All)
            if (string.Equals(c.Key, key, StringComparison.OrdinalIgnoreCase)) return c;
        return null;
    }
}
