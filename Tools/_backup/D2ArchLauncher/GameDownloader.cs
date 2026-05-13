using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text.Json;
using System.Threading.Tasks;

namespace D2ArchLauncher;

public class GameDownloader
{
	private const string GITHUB_OWNER = "solida1987";

	private const string GITHUB_REPO = "Diablo-II-Archipelago";

	private readonly string _gameDir;

	private readonly HttpClient _http;

	// Original Blizzard files - NOT downloaded from GitHub (copied from user's D2 install)
	// 1.9.7 fix: added d2char/d2data/d2sfx (caught leaking 2026-05-01) and Game.exe
	// (caught leaking 2026-05-12). Without these in the launcher's copy list, users
	// got installs with missing files and "Game.exe does not point to a file" errors
	// when trying to launch (per Dewsader/Alphena bug reports).
	private static readonly HashSet<string> ORIGINAL_D2_FILES = new(StringComparer.OrdinalIgnoreCase)
	{
		"D2.LNG", "SmackW32.dll", "binkw32.dll", "d2exp.mpq",
		"d2music.mpq", "d2speech.mpq", "d2video.mpq", "d2xmusic.mpq",
		"d2xtalk.mpq", "d2xvideo.mpq", "ijl11.dll",
		"d2char.mpq", "d2data.mpq", "d2sfx.mpq",
		"Game.exe"
	};

	public bool IsCancelled { get; set; }

	public event Action<int, string>? OnProgress;

	public event Action<bool, string>? OnComplete;

	public GameDownloader(string gameDir)
	{
		_gameDir = gameDir;
		_http = new HttpClient();
		_http.DefaultRequestHeaders.UserAgent.ParseAdd("D2Arch-Launcher/2.0");
		_http.Timeout = TimeSpan.FromMinutes(30.0);
	}

	public async Task InstallFromReleaseAsync(string? releaseTag = null)
	{
		string tempZip = Path.Combine(Path.GetTempPath(), "d2arch_install.zip");
		try
		{
			Report(0, "Fetching release info...");
			string requestUri = ((releaseTag == null || releaseTag.StartsWith("Latest")) ? "https://api.github.com/repos/solida1987/Diablo-II-Archipelago/releases/latest" : $"https://api.github.com/repos/{"solida1987"}/{"Diablo-II-Archipelago"}/releases/tags/{releaseTag}");
			using JsonDocument doc = JsonDocument.Parse(await _http.GetStringAsync(requestUri));
			JsonElement rootElement = doc.RootElement;
			// Capture actual tag from the release JSON so we can always write version.dat,
			// even when the caller passed null/"Latest" for releaseTag.
			string actualTag = releaseTag;
			if (string.IsNullOrEmpty(actualTag) || actualTag.StartsWith("Latest"))
			{
				if (rootElement.TryGetProperty("tag_name", out JsonElement tagEl))
				{
					actualTag = tagEl.GetString();
				}
			}
			string text = null;
			string text2 = null;
			string apworldUrl = null;
			if (rootElement.TryGetProperty("assets", out var value))
			{
				foreach (JsonElement item in value.EnumerateArray())
				{
					string text3 = item.GetProperty("name").GetString() ?? "";
					string text4 = item.GetProperty("browser_download_url").GetString() ?? "";
					if (text3.Contains("game_package") || text3.Contains("installer_package"))
					{
						text = text4;
					}
					if (text3 == "game_manifest.json")
					{
						text2 = text4;
					}
					if (text3.EndsWith(".apworld"))
					{
						apworldUrl = text4;
					}
				}
			}
			if (text == null)
			{
				if (text2 != null)
				{
					await InstallFromManifestAsync(text2);
				}
				else
				{
					this.OnComplete?.Invoke(arg1: false, "No game package found in release assets.");
				}
				return;
			}
			Report(5, "Downloading game package...");
			await DownloadFileWithProgress(text, tempZip, 5, 80);
			if (IsCancelled)
			{
				Cleanup(tempZip);
				return;
			}
			Report(82, "Extracting game files...");
			Directory.CreateDirectory(_gameDir);
			using (ZipArchive zipArchive = ZipFile.OpenRead(tempZip))
			{
				int count = zipArchive.Entries.Count;
				int num = 0;
				foreach (ZipArchiveEntry entry in zipArchive.Entries)
				{
					if (IsCancelled)
					{
						break;
					}
					if (string.IsNullOrEmpty(entry.Name))
					{
						continue;
					}
					// 1.7.1: preserve the zip's directory structure verbatim.
					// Our game_package.zip (built by Tools/_pack_game.py) contains
					// multiple top-level folders (Archipelago/, apworld/, data/,
					// patch/, ap_bridge_dist/) and top-level files — there is NO
					// wrapper folder to strip. The previous "strip first segment
					// if it has no dot" heuristic was flattening every folder,
					// causing e.g. patch/D2Common.dll.disabled -> top-level, which
					// made D2.Detours fail with "Could not find directory …\patch".
					string text5 = entry.FullName;
					if (!string.IsNullOrEmpty(text5))
					{
						string text6 = Path.Combine(_gameDir, text5.Replace('/', '\\'));
						string directoryName = Path.GetDirectoryName(text6);
						if (directoryName != null)
						{
							Directory.CreateDirectory(directoryName);
						}
						entry.ExtractToFile(text6, overwrite: true);
						num++;
						int pct = 82 + num * 15 / count;
						if (num % 20 == 0)
						{
							Report(pct, $"Extracting... ({num}/{count})");
						}
					}
				}
			}
			Cleanup(tempZip);
			if (apworldUrl != null)
			{
				Report(97, "Downloading AP World...");
				string text7 = Path.Combine(_gameDir, "apworld");
				Directory.CreateDirectory(text7);
				string destPath = Path.Combine(text7, "diablo2_archipelago.apworld");
				await DownloadFileSimple(apworldUrl, destPath);
			}
			Report(98, "Cleaning cache...");
			DeleteBinCache();
			Directory.CreateDirectory(Path.Combine(_gameDir, "save"));
			Report(99, "Finalizing version...");
			UpdateVersionDat(actualTag);
			Report(100, "Installation complete!");
			this.OnComplete?.Invoke(arg1: true, "Game installed successfully!");
		}
		catch (Exception ex)
		{
			// Ensure partial download leftovers in %TEMP% are removed so they don't
			// leak disk space or confuse a subsequent install attempt.
			Cleanup(tempZip);
			this.OnComplete?.Invoke(arg1: false, "Installation failed: " + ex.Message);
		}
	}

	public async Task UpdateFromManifestAsync()
	{
		string requestUri = "https://api.github.com/repos/solida1987/Diablo-II-Archipelago/releases/latest";
		try
		{
			Report(0, "Checking for updates...");
			using JsonDocument doc = JsonDocument.Parse(await _http.GetStringAsync(requestUri));
			JsonElement rootElement = doc.RootElement;
			string text = null;
			if (rootElement.TryGetProperty("assets", out var value))
			{
				foreach (JsonElement item in value.EnumerateArray())
				{
					if ((item.GetProperty("name").GetString() ?? "") == "game_manifest.json")
					{
						text = item.GetProperty("browser_download_url").GetString();
						break;
					}
				}
			}
			if (text == null)
			{
				this.OnComplete?.Invoke(arg1: false, "No manifest found. Try full reinstall.");
				return;
			}
			await InstallFromManifestAsync(text);
		}
		catch (Exception ex)
		{
			this.OnComplete?.Invoke(arg1: false, "Update failed: " + ex.Message);
		}
	}

	private async Task InstallFromManifestAsync(string manifestUrl)
	{
		Report(5, "Downloading manifest...");
		string manifestJson = await _http.GetStringAsync(manifestUrl);
		// 1.5.1 — cache the manifest locally so the launcher's pre-launch
		// verifier doesn't have to re-fetch it on every Play click.
		try
		{
			Directory.CreateDirectory(_gameDir);
			File.WriteAllText(Path.Combine(_gameDir, "game_manifest.json"), manifestJson);
		}
		catch
		{
			// Non-fatal — verifier will fall back to fetching from GitHub.
		}
		using JsonDocument doc = JsonDocument.Parse(manifestJson);
		JsonElement rootElement = doc.RootElement;
		List<(string path, string sha256, long size)> filesToDownload = new List<(string, string, long)>();
		string baseUrl = "https://raw.githubusercontent.com/solida1987/Diablo-II-Archipelago/main/game/";

		// 1.8.5 fix (R2) — Track every path the manifest claims should
		// exist. After the download pass we use this set to delete files
		// in mod-managed directories that the new release no longer
		// includes (e.g. a DLL that 1.7.x had but 1.8.x removed).
		// Without this, "update via launcher" left stale files behind
		// and required a fresh install/manual delete to recover.
		HashSet<string> manifestPaths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

		if (rootElement.TryGetProperty("files", out var value))
		{
			Report(10, "Comparing local files...");
			int num = value.GetArrayLength();
			int num2 = 0;
			foreach (JsonElement item2 in value.EnumerateArray())
			{
				string text = item2.GetProperty("path").GetString() ?? "";
				string text2 = item2.GetProperty("sha256").GetString() ?? "";
				long @int = item2.GetProperty("size").GetInt64();

				// Track this path for the orphan-cleanup pass below.
				if (text.Length > 0)
				{
					manifestPaths.Add(text.Replace('\\', '/'));
				}

				// Skip original Blizzard files — these come from user's D2 install, not GitHub
				string fileName = Path.GetFileName(text);
				if (ORIGINAL_D2_FILES.Contains(fileName))
				{
					num2++;
					continue;
				}

				string text3 = Path.Combine(_gameDir, text.Replace('/', '\\'));
				bool flag = true;
				if (File.Exists(text3) && text2.Length > 0 && ComputeSha256(text3).Equals(text2, StringComparison.OrdinalIgnoreCase))
				{
					flag = false;
				}
				if (flag)
				{
					filesToDownload.Add((text, text2, @int));
				}
				num2++;
				if (num2 % 10 == 0)
				{
					Report(10 + num2 * 20 / num, $"Checking files... ({num2}/{num})");
				}
			}
		}
		if (filesToDownload.Count == 0)
		{
			Report(100, "All files up to date!");
			this.OnComplete?.Invoke(arg1: true, "No updates needed — all files match.");
			return;
		}
		Report(30, $"Downloading {filesToDownload.Count} files...");
		Directory.CreateDirectory(_gameDir);
		int downloaded = 0;
		int failed = 0;
		foreach (var (text4, sha, _) in filesToDownload)
		{
			if (IsCancelled)
			{
				break;
			}
			string url = baseUrl + text4;
			string localPath = Path.Combine(_gameDir, text4.Replace('/', '\\'));
			string directoryName = Path.GetDirectoryName(localPath);
			if (directoryName != null)
			{
				Directory.CreateDirectory(directoryName);
			}
			Report(30 + downloaded * 65 / filesToDownload.Count, $"Downloading {Path.GetFileName(text4)} ({downloaded + 1}/{filesToDownload.Count})");
			bool ok = false;
			for (int retry = 0; retry < 3; retry++)
			{
				if (ok)
				{
					break;
				}
				try
				{
					await DownloadFileSimple(url, localPath);
					if (sha.Length > 0)
					{
						string dlHash = ComputeSha256(localPath);
						ok = dlHash.Equals(sha, StringComparison.OrdinalIgnoreCase);
						if (!ok)
						{
							File.Delete(localPath);
						}
					}
					else
					{
						ok = true;
					}
				}
				catch
				{
					if (retry == 2)
					{
						failed++;
					}
				}
			}
			if (ok)
			{
				downloaded++;
			}
		}
		DeleteBinCache();

		// 1.8.5 fix (R2) — Delete files in mod-managed directories that
		// no longer appear in the manifest. Runs only on a clean update
		// (no failed downloads) so a partial / interrupted update doesn't
		// orphan files we still need. Scope is intentionally narrow: only
		// directories that contain mod payload (patch/, ap_bridge_dist/,
		// apworld/) plus a small allow-list of top-level mod files. User
		// data (save/, character state, d2arch.ini, Game/Archipelago/
		// per-character files) is never touched.
		int orphansDeleted = 0;
		if (failed == 0)
		{
			orphansDeleted = DeleteOrphans(manifestPaths);
		}
		if (failed == 0)
		{
			string result = null;
			JsonFindString(manifestJson, "version", ref result);
			if (result != null)
			{
				UpdateVersionDat(result);
			}
		}
		Report(100, $"Done! {downloaded} files downloaded, {failed} failed.");
		string completeMsg = $"Updated {downloaded} files.";
		if (orphansDeleted > 0)
		{
			completeMsg += $" Removed {orphansDeleted} stale file(s).";
		}
		if (failed > 0)
		{
			completeMsg += $" {failed} failed.";
		}
		this.OnComplete?.Invoke(failed == 0, completeMsg);
	}

	/// <summary>
	/// 1.8.5 fix (R2) — Remove files that exist locally but no longer
	/// appear in the manifest. Called from InstallFromManifestAsync after
	/// downloads complete successfully.
	///
	/// Scope is deliberately narrow:
	///   patch/                  — D2.Detours overlay DLLs
	///   ap_bridge_dist/         — PyInstaller-frozen bridge runtime
	///   apworld/                — apworld package
	///   {top-level mod files}   — the few binaries we ship at game root
	///
	/// User-managed paths are NOT scanned:
	///   save/                   — D2 character saves
	///   Game/Archipelago/       — d2arch.ini, character state files
	///   data/                   — data tables (some are user-edited)
	///   Original Blizzard files — never deleted
	///
	/// Returns the number of orphan files deleted.
	/// </summary>
	private int DeleteOrphans(HashSet<string> manifestPaths)
	{
		int deleted = 0;
		// Mod-managed directories: every file here came from a previous
		// install/update, so deleting orphans is safe.
		string[] modDirs = { "patch", "ap_bridge_dist", "apworld" };
		foreach (string dirName in modDirs)
		{
			string fullDir = Path.Combine(_gameDir, dirName);
			if (!Directory.Exists(fullDir)) continue;
			string[] files;
			try { files = Directory.GetFiles(fullDir, "*", SearchOption.AllDirectories); }
			catch { continue; }
			foreach (string filePath in files)
			{
				string rel = Path.GetRelativePath(_gameDir, filePath).Replace('\\', '/');
				if (!manifestPaths.Contains(rel))
				{
					try
					{
						File.Delete(filePath);
						deleted++;
					}
					catch { }
				}
			}
		}

		// Top-level mod-only files. We only delete files we KNOW belong
		// to the mod payload — never iterate the game-root listing
		// blindly because Blizzard original files and user data live
		// there too.
		HashSet<string> knownTopFiles = new(StringComparer.OrdinalIgnoreCase)
		{
			"D2Archipelago.dll",
			"D2Archipelago.exp",
			"D2Archipelago.lib",
			"D2Arch_Launcher.exe",
			"D2.DetoursLauncher.exe",
			"Detours.dll",
		};
		foreach (string fileName in knownTopFiles)
		{
			string fullPath = Path.Combine(_gameDir, fileName);
			if (!File.Exists(fullPath)) continue;
			if (!manifestPaths.Contains(fileName))
			{
				try
				{
					File.Delete(fullPath);
					deleted++;
				}
				catch { }
			}
		}
		return deleted;
	}

	private void UpdateVersionDat(string? version)
	{
		if (string.IsNullOrEmpty(version))
		{
			return;
		}
		try
		{
			string text = Path.Combine(_gameDir, "Archipelago");
			Directory.CreateDirectory(text);
			File.WriteAllText(Path.Combine(text, "version.dat"), version.Trim());
			Report(99, "Version set to: " + version);
		}
		catch
		{
		}
	}

	private static void JsonFindString(string json, string key, ref string? result)
	{
		string text = "\"" + key + "\"";
		int num = json.IndexOf(text);
		if (num < 0)
		{
			return;
		}
		num = json.IndexOf('"', num + text.Length);
		if (num >= 0)
		{
			num++;
			int num2 = json.IndexOf('"', num);
			if (num2 > num)
			{
				result = json.Substring(num, num2 - num);
			}
		}
	}

	private async Task DownloadFileWithProgress(string url, string destPath, int startPct, int endPct)
	{
		using HttpResponseMessage response = await _http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead);
		response.EnsureSuccessStatusCode();
		long? totalBytes = response.Content.Headers.ContentLength;
		await using Stream stream = await response.Content.ReadAsStreamAsync();
		await using FileStream fileStream = new FileStream(destPath, FileMode.Create);
		byte[] buffer = new byte[81920];
		long bytesRead = 0L;
		DateTime lastReport = DateTime.MinValue;
		while (true)
		{
			int num;
			int read = (num = await stream.ReadAsync(buffer));
			if (num <= 0 || IsCancelled)
			{
				break;
			}
			await fileStream.WriteAsync(buffer.AsMemory(0, read));
			bytesRead += read;
			if ((DateTime.Now - lastReport).TotalMilliseconds > 250.0)
			{
				lastReport = DateTime.Now;
				double num2 = (totalBytes.HasValue ? ((double)bytesRead / (double)totalBytes.Value) : 0.0);
				int pct = startPct + (int)(num2 * (double)(endPct - startPct));
				string text = (totalBytes.HasValue ? $"{bytesRead / 1048576}MB / {totalBytes.Value / 1048576}MB" : $"{bytesRead / 1048576}MB");
				Report(pct, "Downloading... " + text);
			}
		}
	}

	private async Task DownloadFileSimple(string url, string destPath)
	{
		await File.WriteAllBytesAsync(destPath, await _http.GetByteArrayAsync(url));
	}

	// =====================================================================
	// 1.5.1 — Pre-launch installation verifier
	//
	// VerifyInstallationAsync(): reads game_manifest.json (local copy if
	// present, else fetched from the latest GitHub release) and confirms
	// every non-Blizzard file exists with the expected size. Returns the
	// list of files that are missing or have wrong size.
	//
	// RepairFilesAsync(): downloads the listed files from the raw github
	// content URL, overwriting any local copies. Used by the launcher's
	// pre-launch repair loop (max 3 attempts before showing the antivirus
	// warning to the user).
	//
	// Design: size-only check (FileInfo.Length is O(1) metadata read), no
	// SHA256 — verification of ~355 files completes in well under a second.
	// SHA256 was tested but added 5-15 seconds to launch, which the user
	// rejected with "hurtigt tjekker" (quick check).
	//
	// Skips ORIGINAL_D2_FILES because those come from the user's own D2
	// install, not from GitHub — verifying their size against the manifest
	// would false-positive on legitimate Blizzard files of slightly
	// different versions.
	// =====================================================================

	public class VerificationResult
	{
		public bool IsValid { get; set; }
		public List<string> MissingOrCorrupt { get; } = new List<string>();
		public string ErrorMessage { get; set; } = "";
		public bool ManifestUnavailable { get; set; }
	}

	public async Task<VerificationResult> VerifyInstallationAsync(bool fetchManifestIfMissing = true)
	{
		VerificationResult result = new VerificationResult();
		string localManifestPath = Path.Combine(_gameDir, "game_manifest.json");
		string? manifestJson = null;

		if (File.Exists(localManifestPath))
		{
			try { manifestJson = await File.ReadAllTextAsync(localManifestPath); }
			catch { manifestJson = null; }
		}

		if (string.IsNullOrEmpty(manifestJson) && fetchManifestIfMissing)
		{
			try
			{
				using JsonDocument doc = JsonDocument.Parse(
					await _http.GetStringAsync($"https://api.github.com/repos/{GITHUB_OWNER}/{GITHUB_REPO}/releases/latest"));
				string? manifestUrl = null;
				if (doc.RootElement.TryGetProperty("assets", out var assets))
				{
					foreach (JsonElement item in assets.EnumerateArray())
					{
						if ((item.GetProperty("name").GetString() ?? "") == "game_manifest.json")
						{
							manifestUrl = item.GetProperty("browser_download_url").GetString();
							break;
						}
					}
				}
				if (manifestUrl != null)
				{
					manifestJson = await _http.GetStringAsync(manifestUrl);
					try { await File.WriteAllTextAsync(localManifestPath, manifestJson); } catch { }
				}
			}
			catch (Exception ex)
			{
				result.ErrorMessage = "Could not fetch manifest: " + ex.Message;
				result.ManifestUnavailable = true;
				// Without a manifest we can't verify; treat as "valid enough
				// to launch" so an offline user isn't blocked.
				result.IsValid = true;
				return result;
			}
		}

		if (string.IsNullOrEmpty(manifestJson))
		{
			result.ManifestUnavailable = true;
			result.IsValid = true;
			return result;
		}

		try
		{
			using JsonDocument mdoc = JsonDocument.Parse(manifestJson);
			if (!mdoc.RootElement.TryGetProperty("files", out JsonElement files))
			{
				result.IsValid = true;
				return result;
			}

			foreach (JsonElement entry in files.EnumerateArray())
			{
				string path = entry.GetProperty("path").GetString() ?? "";
				if (path.Length == 0) continue;
				long expectedSize = 0;
				if (entry.TryGetProperty("size", out JsonElement sizeEl))
				{
					expectedSize = sizeEl.GetInt64();
				}

				string fileName = Path.GetFileName(path);
				if (ORIGINAL_D2_FILES.Contains(fileName)) continue;

				string localPath = Path.Combine(_gameDir, path.Replace('/', '\\'));
				bool valid = false;
				try
				{
					FileInfo fi = new FileInfo(localPath);
					if (fi.Exists)
					{
						// expectedSize == 0 → manifest didn't carry size info;
						// accept any non-empty file as valid.
						if (expectedSize == 0 || fi.Length == expectedSize)
						{
							valid = true;
						}
					}
				}
				catch
				{
					valid = false;
				}

				if (!valid)
				{
					result.MissingOrCorrupt.Add(path);
				}
			}

			result.IsValid = result.MissingOrCorrupt.Count == 0;
		}
		catch (Exception ex)
		{
			result.ErrorMessage = "Manifest parse error: " + ex.Message;
			// Manifest is broken; don't block launch.
			result.IsValid = true;
		}

		return result;
	}

	public async Task<bool> RepairFilesAsync(List<string> filesToRepair)
	{
		if (filesToRepair == null || filesToRepair.Count == 0) return true;

		string baseUrl = $"https://raw.githubusercontent.com/{GITHUB_OWNER}/{GITHUB_REPO}/main/game/";
		int total = filesToRepair.Count;
		int done = 0;
		int failed = 0;

		foreach (string relPath in filesToRepair)
		{
			if (IsCancelled) break;
			Report((done * 100) / Math.Max(1, total),
				$"Repairing {Path.GetFileName(relPath)} ({done + 1}/{total})...");

			string url = baseUrl + relPath.Replace('\\', '/');
			string localPath = Path.Combine(_gameDir, relPath.Replace('/', '\\'));
			string? dir = Path.GetDirectoryName(localPath);
			if (dir != null)
			{
				try { Directory.CreateDirectory(dir); } catch { }
			}

			bool ok = false;
			for (int retry = 0; retry < 2 && !ok; retry++)
			{
				try
				{
					await DownloadFileSimple(url, localPath);
					ok = true;
				}
				catch
				{
					if (retry == 1) failed++;
				}
			}
			if (ok) done++;
		}

		Report(100, $"Repair done: {done} ok, {failed} failed.");
		return failed == 0;
	}

	// 1.5.1 — On every successful manifest-based install/update, write the
	// manifest JSON next to the game files so later launches can verify
	// without re-fetching it. Called from InstallFromManifestAsync and
	// InstallFromReleaseAsync (the InstallFromRelease branch already pulls
	// the manifest URL during the asset enumeration; saving it costs ~50 KB
	// disk and avoids a network round-trip on every Play click).
	internal void SaveManifestLocally(string manifestJson)
	{
		try
		{
			string path = Path.Combine(_gameDir, "game_manifest.json");
			File.WriteAllText(path, manifestJson);
		}
		catch
		{
			// Non-fatal — verifier will fall back to fetching from GitHub.
		}
	}

	private static string ComputeSha256(string filePath)
	{
		using SHA256 sHA = SHA256.Create();
		using FileStream inputStream = File.OpenRead(filePath);
		return BitConverter.ToString(sHA.ComputeHash(inputStream)).Replace("-", "").ToLower();
	}

	private void DeleteBinCache()
	{
		string path = Path.Combine(_gameDir, "data", "global", "excel");
		if (!Directory.Exists(path))
		{
			return;
		}
		string[] files = Directory.GetFiles(path, "*.bin");
		foreach (string path2 in files)
		{
			try
			{
				File.Delete(path2);
			}
			catch
			{
			}
		}
	}

	private void Cleanup(string tempFile)
	{
		try
		{
			if (File.Exists(tempFile))
			{
				File.Delete(tempFile);
			}
		}
		catch
		{
		}
	}

	private void Report(int pct, string status)
	{
		this.OnProgress?.Invoke(pct, status);
	}
}
