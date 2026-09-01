using System;
using System.Collections.Generic;
using System.IO;

namespace LauncherV2.Plugins.DiabloII;

// Pull the vanilla excel tables the randomizer needs out of the player's OWN
// MPQ archives.
//
// Why this exists: the per-seed patcher (D2DataFiles) zeroes requirement
// columns in weapons.txt, armor.txt and friends — but those tables are
// Blizzard's data, so the game package must not ship them (EULA; see
// Tools/_pack_game.py, "vanilla table"). The ENGINE happily reads absent
// tables straight from the archives, which is why the game always ran — but
// the patcher had no source files, so "item requirements off", shop shuffle
// on gear, and Levels.txt force-full-generation silently did nothing on any
// install that never had the .txt on disk. Measured 2026-09-01 on a live
// install: only the mod's own ten tables present, every vanilla one missing.
//
// This is a minimal, read-only MPQ v1 reader: header scan, Blizzard hash,
// table decrypt, sector walk, and the two compressions the D2 archives
// actually use — PKWARE DCL implode (a direct port of zlib/contrib/blast.c,
// Mark Adler, zlib licence) and raw zlib. Proven against a live install:
// all six tables extract from Patch_D2.mpq / d2exp.mpq / d2data.mpq with
// correct headers ("name\ttype\t...", "index\tset\titem\t...\tlvl req\t...").
//
// Everything stays on the player's machine: their archives, their disk, a
// local transform. Nothing is distributed.
public static class D2MpqTables
{
    // The tables D2DataFiles manages that the package cannot ship.
    // The mod's own shipped tables (skills.txt, SuperUniques.txt, misc.txt,
    // UniqueItems.txt, ...) are deliberately NOT here: extracting vanilla
    // over a missing MOD table would strip the mod's changes — the repair
    // flow owns those.
    private static readonly string[] VanillaTables =
    {
        "weapons.txt", "armor.txt", "Levels.txt",
        "MagicPrefix.txt", "MagicSuffix.txt", "SetItems.txt",
    };

    // Patch archive first: in the engine's own priority order the patch wins,
    // so its copy is the one the game actually plays with.
    private static readonly string[] Archives =
    {
        "Patch_D2.mpq", "d2exp.mpq", "d2data.mpq",
    };

    ///
    /// Extract every missing vanilla table into data\global\excel.
    /// Returns the number written. Never throws — a failure leaves that
    /// table absent, which is exactly the state the install was already in.
    ///
    public static int ExtractMissingTables(string gameDir, Action<string>? log = null)
    {
        int written = 0;
        try
        {
            string excelDir = Path.Combine(gameDir, "data", "global", "excel");
            var missing = new List<string>();
            foreach (string t in VanillaTables)
                if (!File.Exists(Path.Combine(excelDir, t)))
                    missing.Add(t);
            if (missing.Count == 0) return 0;

            Directory.CreateDirectory(excelDir);
            foreach (string arcName in Archives)
            {
                if (missing.Count == 0) break;
                string arcPath = Path.Combine(gameDir, arcName);
                if (!File.Exists(arcPath)) continue;

                MpqReader mpq;
                try { mpq = new MpqReader(arcPath); }
                catch (Exception ex)
                {
                    log?.Invoke($"[Tables] {arcName}: not readable ({ex.Message})");
                    continue;
                }

                for (int i = missing.Count - 1; i >= 0; i--)
                {
                    string name = missing[i];
                    byte[]? data;
                    try { data = mpq.ReadFile("data\\global\\excel\\" + name); }
                    catch { data = null; }
                    if (data == null || data.Length < 64) continue;

                    // Behaviour check: an excel table is tab-separated text.
                    bool hasTab = false;
                    for (int b = 0; b < data.Length && b < 4096; b++)
                        if (data[b] == (byte)'\t') { hasTab = true; break; }
                    if (!hasTab) continue;

                    File.WriteAllBytes(Path.Combine(excelDir, name), data);
                    log?.Invoke($"[Tables] {name} extracted from {arcName} "
                              + $"({data.Length:N0} bytes)");
                    missing.RemoveAt(i);
                    written++;
                }
            }
            foreach (string still in missing)
                log?.Invoke($"[Tables] {still} not found in any archive — "
                          + "its options stay engine-default");
        }
        catch (Exception ex)
        {
            log?.Invoke("[Tables] extraction failed: " + ex.Message);
        }
        return written;
    }

    // ─── MPQ v1 reader ──────────────────────────────────────────────────

    private sealed class MpqReader
    {
        private readonly byte[] _d;
        private readonly int _base;
        private readonly int _sectorSize;
        private readonly byte[] _hashTable;   // decrypted
        private readonly byte[] _blockTable;  // decrypted
        private readonly int _hashEntries;

        public MpqReader(string path)
        {
            _d = File.ReadAllBytes(path);
            _base = -1;
            for (int off = 0; off + 4 <= _d.Length && off < 0x100000; off += 0x200)
                if (_d[off] == (byte)'M' && _d[off + 1] == (byte)'P'
                 && _d[off + 2] == (byte)'Q' && _d[off + 3] == 0x1A)
                { _base = off; break; }
            if (_base < 0) throw new InvalidDataException("no MPQ header");

            ushort blockShift = BitConverter.ToUInt16(_d, _base + 14);
            uint htOfs = BitConverter.ToUInt32(_d, _base + 16);
            uint btOfs = BitConverter.ToUInt32(_d, _base + 20);
            int htLen = (int)BitConverter.ToUInt32(_d, _base + 24);
            int btLen = (int)BitConverter.ToUInt32(_d, _base + 28);
            _sectorSize = 512 << blockShift;
            _hashEntries = htLen;

            _hashTable = Decrypt(_d, _base + (int)htOfs, htLen * 16,
                                 Hash("(hash table)", 3));
            _blockTable = Decrypt(_d, _base + (int)btOfs, btLen * 16,
                                  Hash("(block table)", 3));
        }

        public byte[]? ReadFile(string name)
        {
            const uint EXISTS = 0x80000000, ENCRYPTED = 0x00010000,
                       FIXKEY = 0x00020000, IMPLODE = 0x00000100,
                       COMPRESS = 0x00000200, SINGLE = 0x01000000;

            int idx = (int)(Hash(name, 0) & (uint)(_hashEntries - 1));
            uint hA = Hash(name, 1), hB = Hash(name, 2);
            int block = -1;
            for (int probe = 0; probe < _hashEntries; probe++)
            {
                int i = (idx + probe) % _hashEntries;
                uint n1 = BitConverter.ToUInt32(_hashTable, i * 16);
                uint n2 = BitConverter.ToUInt32(_hashTable, i * 16 + 4);
                uint blk = BitConverter.ToUInt32(_hashTable, i * 16 + 12);
                if (blk == 0xFFFFFFFF) return null;          // empty stops the chain
                if (blk != 0xFFFFFFFE && n1 == hA && n2 == hB)
                { block = (int)(blk & 0x0FFFFFFF); break; }
            }
            if (block < 0) return null;

            uint fofs  = BitConverter.ToUInt32(_blockTable, block * 16);
            int  csize = (int)BitConverter.ToUInt32(_blockTable, block * 16 + 4);
            int  fsize = (int)BitConverter.ToUInt32(_blockTable, block * 16 + 8);
            uint flags = BitConverter.ToUInt32(_blockTable, block * 16 + 12);
            if ((flags & EXISTS) == 0) return null;
            if (fsize <= 0 || fsize > (64 << 20)) return null;

            int start = _base + (int)fofs;
            uint key = 0;
            if ((flags & ENCRYPTED) != 0)
            {
                string baseName = name.Replace('/', '\\');
                int sl = baseName.LastIndexOf('\\');
                if (sl >= 0) baseName = baseName[(sl + 1)..];
                key = Hash(baseName, 3);
                if ((flags & FIXKEY) != 0)
                    key = (key + fofs) ^ (uint)fsize;
            }

            if ((flags & (IMPLODE | COMPRESS)) == 0)
            {
                byte[] raw = (flags & ENCRYPTED) != 0
                    ? Decrypt(_d, start, csize, key)
                    : Slice(_d, start, csize);
                return raw.Length >= fsize ? Slice(raw, 0, fsize) : null;
            }

            if ((flags & SINGLE) != 0)
            {
                byte[] body = (flags & ENCRYPTED) != 0
                    ? Decrypt(_d, start, csize, key)
                    : Slice(_d, start, csize);
                return Desector(body, fsize, flags);
            }

            int nsec = (fsize + _sectorSize - 1) / _sectorSize;
            byte[] tblRaw = (flags & ENCRYPTED) != 0
                ? Decrypt(_d, start, 4 * (nsec + 1), key - 1)
                : Slice(_d, start, 4 * (nsec + 1));
            var outBuf = new MemoryStream(fsize);
            for (int s = 0; s < nsec; s++)
            {
                int so = (int)BitConverter.ToUInt32(tblRaw, s * 4);
                int eo = (int)BitConverter.ToUInt32(tblRaw, (s + 1) * 4);
                if (eo < so || start + eo > _d.Length) return null;
                byte[] sraw = (flags & ENCRYPTED) != 0
                    ? Decrypt(_d, start + so, eo - so, key + (uint)s)
                    : Slice(_d, start + so, eo - so);
                int expect = Math.Min(_sectorSize, fsize - (int)outBuf.Length);
                byte[] plain = Desector(sraw, expect, flags);
                outBuf.Write(plain, 0, Math.Min(plain.Length, expect));
            }
            return outBuf.Length == fsize ? outBuf.ToArray() : null;
        }

        private static byte[] Desector(byte[] sraw, int expect, uint flags)
        {
            if (sraw.Length == expect) return sraw;          // stored
            if ((flags & 0x00000200) != 0)                   // COMPRESS: method byte
            {
                byte m = sraw[0];
                byte[] body = Slice(sraw, 1, sraw.Length - 1);
                return m switch
                {
                    0x02 => Inflate(body),
                    0x08 => PkExplode.Explode(body),
                    _ => throw new InvalidDataException($"compression 0x{m:X2}"),
                };
            }
            if ((flags & 0x00000100) != 0)                   // IMPLODE: raw PKWARE
                return PkExplode.Explode(sraw);
            return sraw;
        }

        private static byte[] Inflate(byte[] body)
        {
            using var src = new MemoryStream(body);
            using var z = new System.IO.Compression.ZLibStream(
                src, System.IO.Compression.CompressionMode.Decompress);
            using var dst = new MemoryStream();
            z.CopyTo(dst);
            return dst.ToArray();
        }

        private static byte[] Slice(byte[] d, int ofs, int len)
        {
            len = Math.Max(0, Math.Min(len, d.Length - ofs));
            var r = new byte[len];
            Buffer.BlockCopy(d, ofs, r, 0, len);
            return r;
        }

        // ── Blizzard crypt table + hash + decrypt ──
        private static readonly uint[] Crypt = BuildCrypt();

        private static uint[] BuildCrypt()
        {
            var t = new uint[0x500];
            uint seed = 0x00100001;
            for (int i = 0; i < 0x100; i++)
                for (int j = 0; j < 5; j++)
                {
                    seed = (seed * 125 + 3) % 0x2AAAAB;
                    uint a = (seed & 0xFFFF) << 16;
                    seed = (seed * 125 + 3) % 0x2AAAAB;
                    t[i + j * 0x100] = a | (seed & 0xFFFF);
                }
            return t;
        }

        private static uint Hash(string s, int type)
        {
            uint s1 = 0x7FED7FED, s2 = 0xEEEEEEEE;
            foreach (char raw in s.ToUpperInvariant())
            {
                char ch = raw == '/' ? '\\' : raw;
                s1 = Crypt[(type << 8) + ch] ^ (s1 + s2);
                s2 = ch + s1 + s2 + (s2 << 5) + 3;
            }
            return s1;
        }

        private static byte[] Decrypt(byte[] d, int ofs, int len, uint key)
        {
            len = Math.Max(0, Math.Min(len, d.Length - ofs)) & ~3;
            var outB = new byte[len];
            uint s2 = 0xEEEEEEEE;
            for (int i = 0; i < len; i += 4)
            {
                s2 += Crypt[0x400 + (int)(key & 0xFF)];
                uint v = BitConverter.ToUInt32(d, ofs + i) ^ (key + s2);
                BitConverter.TryWriteBytes(outB.AsSpan(i), v);
                key = ((~key << 0x15) + 0x11111111) | (key >> 0x0B);
                s2 = v + s2 + (s2 << 5) + 3;
            }
            return outB;
        }
    }

    // ─── PKWARE DCL explode (port of zlib/contrib/blast.c) ──────────────

    private static class PkExplode
    {
        private const int MaxBits = 13;

        private static readonly byte[] LitLen =
        {
            11,124,8,7,28,7,188,13,76,4,10,8,12,10,12,10,8,23,8,
            9,7,6,7,8,7,6,55,8,23,24,12,11,7,9,11,12,6,7,22,5,
            7,24,6,11,9,6,7,22,7,11,38,7,9,8,25,11,8,11,9,12,
            8,12,5,38,5,38,5,11,7,5,6,21,6,10,53,8,7,24,10,27,
            44,253,253,253,252,252,252,13,12,45,12,45,12,61,12,45,
            44,173,
        };
        private static readonly byte[] LenLen  = { 2, 35, 36, 53, 38, 23 };
        private static readonly byte[] DistLen = { 2, 20, 53, 230, 247, 151, 248 };
        private static readonly short[] Base =
            { 3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264 };
        private static readonly byte[] Extra =
            { 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8 };

        private static readonly (short[] Count, short[] Symbol) LitCode  = Construct(LitLen);
        private static readonly (short[] Count, short[] Symbol) LenCode  = Construct(LenLen);
        private static readonly (short[] Count, short[] Symbol) DistCode = Construct(DistLen);

        private static (short[], short[]) Construct(byte[] rep)
        {
            var length = new List<short>();
            foreach (byte b in rep)
            {
                int n = (b >> 4) + 1;
                for (int i = 0; i < n; i++) length.Add((short)(b & 15));
            }
            var count = new short[MaxBits + 1];
            foreach (short l in length) count[l]++;
            var offs = new short[MaxBits + 1];
            for (int l = 1; l < MaxBits; l++)
                offs[l + 1] = (short)(offs[l] + count[l]);
            var symbol = new short[length.Count];
            for (short sym = 0; sym < length.Count; sym++)
                if (length[sym] != 0)
                    symbol[offs[length[sym]]++] = sym;
            return (count, symbol);
        }

        public static byte[] Explode(byte[] data)
        {
            int pos = 0, bitbuf = 0, bitcnt = 0;

            int Bits(int need)
            {
                int val = bitbuf;
                while (bitcnt < need)
                {
                    val |= data[pos++] << bitcnt;
                    bitcnt += 8;
                }
                bitbuf = val >> need;
                bitcnt -= need;
                return val & ((1 << need) - 1);
            }

            int Decode((short[] Count, short[] Symbol) huff)
            {
                int code = 0, first = 0, index = 0, len = 1;
                int bb = bitbuf, left = bitcnt;
                while (true)
                {
                    while (left > 0)
                    {
                        left--;
                        code |= (bb & 1) ^ 1;
                        bb >>= 1;
                        int cnt = huff.Count[len];
                        if (code < first + cnt)
                        {
                            bitbuf = bb;
                            bitcnt = (bitcnt - len) & 7;
                            return huff.Symbol[index + (code - first)];
                        }
                        index += cnt;
                        first = (first + cnt) << 1;
                        code <<= 1;
                        len++;
                    }
                    left = (MaxBits + 1) - len;
                    if (left == 0) throw new InvalidDataException("bad code");
                    bb = data[pos++];
                    if (left > 8) left = 8;
                }
            }

            int lit = Bits(8);
            if (lit > 1) throw new InvalidDataException("bad DCL literal flag");
            int dict = Bits(8);
            if (dict < 4 || dict > 6) throw new InvalidDataException("bad DCL dict");

            var outB = new List<byte>(1 << 16);
            while (true)
            {
                if (Bits(1) != 0)
                {
                    int symbol = Decode(LenCode);
                    int length = Base[symbol] + Bits(Extra[symbol]);
                    if (length == 519) break;               // end code
                    symbol = length == 2 ? 2 : dict;
                    int dist = (Decode(DistCode) << symbol) + Bits(symbol) + 1;
                    if (dist > outB.Count)
                        throw new InvalidDataException("distance too far back");
                    for (int i = 0; i < length; i++)
                        outB.Add(outB[outB.Count - dist]);
                }
                else
                {
                    outB.Add((byte)(lit != 0 ? Decode(LitCode) : Bits(8)));
                }
            }
            return outB.ToArray();
        }
    }
}
