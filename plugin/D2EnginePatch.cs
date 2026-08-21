using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;

namespace LauncherV2.Plugins.DiabloII;

// D2EnginePatch — the byte edits the mod needs in D2's DLLs, applied to
// the mod's own copies at install time, never to the player's originals.
// Each patch verifies the original bytes before writing.
internal static class D2EnginePatch
{
    internal readonly record struct Edit(int Offset, string Expected, string Replacement);

    internal sealed record EnginePatch(
        string FileName, string Clean, string Patched, Edit[] Edits);

    private static readonly EnginePatch[] Patches =
    {
        new EnginePatch("Storm.dll",
            Clean:   "4beb992a188f6e994a66f69a1301a4988d571dacc601b50504278794327e6921",
            Patched: "4b93e877c11a93a4e255b130a7d4fb5d8722788fb7673297295b8301e488f061",
            Edits: new[]
            {
                new Edit(0x015B9B, "75", "EB"),
            }),

        new EnginePatch("D2Glide.dll",
            Clean:   "6636086be7088d5361235c5fc832c1aac292ff18d91cda364ca84b0cbd147932",
            Patched: "0ba13737d4caaebbb5a82ecc64924ed7a0ce1bb5dc453ab5998985695f927e95",
            Edits: new[]
            {
                new Edit(0x002E9A, "33DB", "EB47"),
            }),

        new EnginePatch("D2Launch.dll",
            Clean:   "af95e308046aa542c684ea3f94438d0117c5b946979e12dfb9e31316b3545ca2",
            Patched: "ce90d3f8570c72867fd9de192e4687ecfd7c8b75e7975776fc6a5791673cdbc0",
            Edits: new[]
            {
                new Edit(0x00123D, "7507", "9090"),
                new Edit(0x00BF72, "75",   "EB"),
                new Edit(0x00C9C4, "741A", "9090"),
                new Edit(0x00D7DF, "0F841901", "E91A0100"),
                new Edit(0x00D7E4, "00",   "90"),
                new Edit(0x023858, "E000", "EA01"),
                new Edit(0x023888, "0A01", "0F27"),
                new Edit(0x0238B8, "2301", "0F27"),
                new Edit(0x0238E8, "4D01", "1002"),
                new Edit(0x023918, "1002", "0F27"),
                new Edit(0x023948, "1002", "0F27"),
                new Edit(0x0239A8, "44",   "EA"),
                new Edit(0x0239D8, "6E01", "0F27"),
                new Edit(0x023A08, "8701", "0F27"),
                new Edit(0x023A38, "B101", "1002"),
            }),
    };

    // The files this touches, so callers can reason about them without
    // duplicating the list.
    internal static IEnumerable<string> PatchedFileNames => Patches.Select(p => p.FileName);

    // Applies every patch in gameDir. Returns one message per
    // file that could not be brought to its patched form; an empty list means
    // every file is now patched (or already was).
    internal static List<string> Apply(string gameDir)
    {
        var problems = new List<string>();
        foreach (var patch in Patches)
        {
            string path = Path.Combine(gameDir, patch.FileName);
            if (!File.Exists(path))
            {
                problems.Add($"{patch.FileName} is missing.");
                continue;
            }

            byte[] data;
            try { data = File.ReadAllBytes(path); }
            catch (Exception ex) { problems.Add($"{patch.FileName}: {ex.Message}"); continue; }

            string have = Sha256(data);
            if (have == patch.Patched) continue;          // already done
            if (have != patch.Clean)
            {
                problems.Add(
                    $"{patch.FileName} is not the 1.10f build this mod knows how to " +
                    "patch, so it was left untouched.");
                continue;
            }

            if (!TryEdit(data, patch, out string? why))
            {
                problems.Add($"{patch.FileName}: {why}");
                continue;
            }

            if (Sha256(data) != patch.Patched)
            {
                problems.Add($"{patch.FileName} did not come out as expected and was not written.");
                continue;
            }

            // Write via a temporary file so an interrupted write cannot leave a
            // half-patched library behind.
            string tmp = path + ".patching";
            try
            {
                File.WriteAllBytes(tmp, data);
                File.Move(tmp, path, overwrite: true);
            }
            catch (Exception ex)
            {
                problems.Add($"{patch.FileName}: {ex.Message}");
                try { if (File.Exists(tmp)) File.Delete(tmp); } catch { }
            }
        }
        return problems;
    }

    // True when every file is already in its patched form.
    internal static bool IsPatched(string gameDir)
    {
        foreach (var patch in Patches)
        {
            string path = Path.Combine(gameDir, patch.FileName);
            if (!File.Exists(path)) return false;
            try { if (Sha256(File.ReadAllBytes(path)) != patch.Patched) return false; }
            catch { return false; }
        }
        return true;
    }

    private static bool TryEdit(byte[] data, EnginePatch patch, out string? problem)
    {
        foreach (var edit in patch.Edits)
        {
            byte[] expected = FromHex(edit.Expected);
            byte[] replace  = FromHex(edit.Replacement);
            if (edit.Offset < 0 || edit.Offset + expected.Length > data.Length)
            {
                problem = $"offset 0x{edit.Offset:X} lies outside the file.";
                return false;
            }
            for (int i = 0; i < expected.Length; i++)
            {
                if (data[edit.Offset + i] != expected[i])
                {
                    problem = $"the bytes at 0x{edit.Offset:X} are not what this patch expects.";
                    return false;
                }
            }
            Array.Copy(replace, 0, data, edit.Offset, replace.Length);
        }
        problem = null;
        return true;
    }

    private static byte[] FromHex(string hex)
    {
        var b = new byte[hex.Length / 2];
        for (int i = 0; i < b.Length; i++)
            b[i] = Convert.ToByte(hex.Substring(i * 2, 2), 16);
        return b;
    }

    private static string Sha256(byte[] data)
        => Convert.ToHexString(SHA256.HashData(data)).ToLowerInvariant();
}
