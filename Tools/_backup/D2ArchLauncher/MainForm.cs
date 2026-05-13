using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net.Http;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Win32;

namespace D2ArchLauncher;

public class MainForm : Form
{
	private enum LState
	{
		NotInstalled,
		UpToDate,
		GameUpdate,
		Downloading
	}

	// 1.5.2 — Iron Frame redesign. Window enlarged from 950×580 → 1040×660
	// to give the new artwork room to breathe. Sidebar widened, action bar
	// taller, masthead unchanged height-wise but offset slightly to seat
	// the logo PNG correctly.
	private const int WIN_W = 1040;

	private const int WIN_H = 660;

	private const int BANNER_H = 110;

	private const int SIDEBAR_W = 230;

	private const int BOTTOM_H = 124;

	// Outer padding around the whole shell (matches the design's 16px
	// shell margin so panel edges sit cleanly on the stone background).
	private const int OUTER_PAD = 16;

	// 1.5.2 — Iron Frame design tokens. Sampled directly from the
	// hand-painted asset PNGs so widget chrome blends with the artwork.
	// Match tokens.css from the design pack 1:1 (--bg-void, --gold,
	// --gold-hi, --ink, --ink-soft, --ink-mute, --ember, etc.).
	private static readonly Color CL_VOID      = Color.FromArgb(10,  8,  7);   // bg-void  #0a0807
	private static readonly Color CL_STONE     = Color.FromArgb(26, 22, 18);   // bg-stone #1a1612
	private static readonly Color CL_STONE_DEEP= Color.FromArgb(16, 12, 10);   // bg-stone-deep
	private static readonly Color CL_GOLD      = Color.FromArgb(212, 160, 74); // gold     #d4a04a
	private static readonly Color CL_GOLD_HI   = Color.FromArgb(244, 201, 122);// gold-hi  #f4c97a
	private static readonly Color CL_GOLD_DIM  = Color.FromArgb(138, 106, 58); // gold-dim #8a6a3a
	private static readonly Color CL_INK       = Color.FromArgb(232, 217, 184);// ink      #e8d9b8
	private static readonly Color CL_INK_SOFT  = Color.FromArgb(184, 168, 136);// ink-soft #b8a888
	private static readonly Color CL_INK_MUTE  = Color.FromArgb(122, 111,  92);// ink-mute #7a6f5c
	private static readonly Color CL_IRON      = Color.FromArgb(106,  90,  72);// iron-edge
	private static readonly Color CL_IRON_DARK = Color.FromArgb( 31,  24,  18);// iron-shadow
	private static readonly Color CL_EMBER     = Color.FromArgb(200,  84,  26);// ember
	private static readonly Color CL_EMBER_HI  = Color.FromArgb(255, 122,  42);// ember-hi

	private const string GITHUB_OWNER = "solida1987";

	private const string GITHUB_REPO = "Diablo-II-Archipelago";

	private const string GAME_VERSION = "Beta 1.8.6";

	private const string LAUNCHER_VERSION = "1.5.3";

	/// &lt;summary&gt;
	/// Compare two version strings semantically.
	/// Returns &lt;0 if a &lt; b, 0 if equal, &gt;0 if a &gt; b.
	/// Handles "Beta-1.7.1", "Beta 1.7.1", "1.4.0", "v1.4.0" prefixes/formats.
	/// Missing numeric components are treated as 0 (e.g. "1.4" == "1.4.0").
	/// Non-numeric tails (pre-release tags) are compared lexicographically
	/// AFTER the numeric prefix — "1.4.0" &gt; "1.4.0-rc1".
	/// </summary>
	private static int CompareVersions(string? a, string? b)
	{
		if (string.IsNullOrWhiteSpace(a) && string.IsNullOrWhiteSpace(b)) return 0;
		if (string.IsNullOrWhiteSpace(a)) return -1;
		if (string.IsNullOrWhiteSpace(b)) return 1;

		static (int[] nums, string tail) Parse(string s)
		{
			// Strip common prefixes: "Beta-", "Beta ", "v", "V", "Launcher "
			string t = s.Trim();
			string[] prefixes = { "Beta-", "Beta ", "beta-", "beta ", "Launcher ", "launcher ", "v", "V" };
			bool stripped;
			do
			{
				stripped = false;
				foreach (string p in prefixes)
				{
					if (t.StartsWith(p, StringComparison.Ordinal))
					{
						t = t.Substring(p.Length);
						stripped = true;
						break;
					}
				}
			} while (stripped);
			// Split at first non-digit/non-dot/non-underscore character
			int i = 0;
			while (i < t.Length && (char.IsDigit(t[i]) || t[i] == '.' || t[i] == '_'))
				i++;
			string numPart = t.Substring(0, i).Replace('_', '.');
			string tail = t.Substring(i);
			List<int> nums = new List<int>();
			if (numPart.Length > 0)
			{
				foreach (string c in numPart.Split('.', StringSplitOptions.RemoveEmptyEntries))
				{
					nums.Add(int.TryParse(c, out int n) ? n : 0);
				}
			}
			return (nums.ToArray(), tail);
		}

		var (na, ta) = Parse(a);
		var (nb, tb) = Parse(b);
		int len = Math.Max(na.Length, nb.Length);
		for (int i = 0; i < len; i++)
		{
			int va = (i < na.Length) ? na[i] : 0;
			int vb = (i < nb.Length) ? nb[i] : 0;
			if (va != vb) return va.CompareTo(vb);
		}
		// Numeric parts equal — tail without tag beats tail with tag
		if (string.IsNullOrEmpty(ta) && !string.IsNullOrEmpty(tb)) return 1;
		if (!string.IsNullOrEmpty(ta) && string.IsNullOrEmpty(tb)) return -1;
		return string.Compare(ta, tb, StringComparison.OrdinalIgnoreCase);
	}

	// Shared HttpClient to avoid socket exhaustion and repeated DNS hits.
	// Timeout is generous (10 min) because SelfUpdateAsync/InstallFromRelease
	// can transfer sizeable archives over slow links.
	private static readonly HttpClient _http = CreateSharedHttp();

	private static HttpClient CreateSharedHttp()
	{
		HttpClient h = new HttpClient
		{
			Timeout = TimeSpan.FromMinutes(10.0)
		};
		h.DefaultRequestHeaders.UserAgent.ParseAdd("D2Arch-Launcher/2.0");
		return h;
	}

	private LState _state;

	private string _launcherDir;

	private string _gameDir = "";

	private string _statusText = "Starting...";

	private string _versionText = "Checking...";

	private string _newsText = "";

	// 1.5.1 — Settings & Logic Guide. Loaded from
	// https://raw.githubusercontent.com/.../Settings_Guide.md at startup,
	// with a local fallback to launcher dir if the fetch fails or the
	// user is offline.
	private string _settingsGuide = "";

	// 1.5.1 — Pre-launch verification: counts consecutive Play attempts
	// where the file-size check found something missing/corrupt. After 3
	// failed verify+repair cycles in the same session we surface an
	// "antivirus or firewall" warning and stop trying. Reset to 0 on a
	// successful launch.
	private int _consecutiveLaunchVerifyFailures;

	private string _patchNotes = "Loading...";

	private string _remoteVersion = "";

	private int _activePage;

	// Saved window position loaded from launcher_config.ini. int.MinValue is the
	// sentinel for "no saved position" so we fall back to CenterScreen on first
	// run. OnFormClosing rewrites these before SaveLauncherConfig is called.
	private int _savedWindowX = int.MinValue;

	private int _savedWindowY = int.MinValue;

	private string _apWorldDir = "";

	private string _d2OriginalDir = "";

	private TextBox _txtD2OriginalPath;

	private Button _btnBrowseD2Original;

	// Files that must be copied from the user's original D2 installation
	// 1.9.7 fix: added d2char/d2data/d2sfx (caught leaking 2026-05-01) and Game.exe
	// (caught leaking 2026-05-12). Without these in the copy list the install
	// completes "successfully" but the game can't launch — D2.DetoursLauncher
	// reports "The path Game.exe does not point to a file" and the install is
	// missing the 3 large MPQs from the Blizzard data set.
	// MUST stay in sync with GameDownloader.cs ORIGINAL_D2_FILES.
	private static readonly string[] ORIGINAL_D2_FILES = new[]
	{
		"D2.LNG", "SmackW32.dll", "binkw32.dll", "d2exp.mpq",
		"d2music.mpq", "d2speech.mpq", "d2video.mpq", "d2xmusic.mpq",
		"d2xtalk.mpq", "d2xvideo.mpq", "ijl11.dll",
		"d2char.mpq", "d2data.mpq", "d2sfx.mpq",
		"Game.exe"
	};

	private Image? _sprLogo;

	private Image? _sprSidebar;

	private Image? _sprContentPanel;

	// 1.5.2 — redesigned-launcher textures + arrow indicator. Loaded
	// from launcher/ui/ at startup. The textures are wallpaper-style
	// PNGs from the Iron Frame design pack:
	//   stone_wall  — the dark stone-courtyard background behind the
	//                 whole shell (replaces the flat #0F0C0A fill).
	//   parchment   — soft warm overlay drawn inside the content area,
	//                 evokes a worn page on stone.
	//   arrow       — small iron arrowhead drawn next to the active
	//                 sidebar nav item (replaces the old gold-border
	//                 highlight strip).
	//   bottomBar   — full-bleed iron-framed action bar at the bottom.
	private Image? _sprStoneWall;

	private Image? _sprParchment;

	private Image? _sprArrow;

	private Image? _sprBottomBar;

	private Image? _sprContentArea;

	// 1.5.2b — small_panel.png is the painted iron-and-stone bar that
	// sits behind secondary buttons (Bug Form / Report Bug) so they
	// blend with the action bar instead of looking like flat overlays.
	private Image? _sprSmallPanel;

	// 1.5.3 — generated emerald-flame strip. Built once at startup by
	// BuildFlameStrip() into an in-memory Bitmap and drawn behind the
	// masthead's bottom seam at every paint. The masthead band fades
	// to fully transparent over its bottom ~50px so the flames glow
	// through the seam clearly. Sidebar / content / bottom-bar render
	// on top, hiding the strip everywhere except the seam itself.
	private Image? _sprFlameStrip;

	private RichTextBox _txtContent;

	private RichTextBox _txtGameLog;

	private RichTextBox _txtBridgeLog;

	private Panel _settingsPanel;

	private CancellationTokenSource? _logWatchCts;

	private FileSystemWatcher? _logWatcher;

	private TextBox _txtGamePath;

	private Button _btnBrowsePath;

	private TextBox _txtApWorldPath;

	private Button _btnBrowseApWorld;

	private CheckBox _chkFullscreen;

	private CheckBox _chkVsync;

	private CheckBox _chkSkipIntro;

	private CheckBox _chkHdCursor;

	private CheckBox _chkHdText;

	private CheckBox _chkMinimap;

	private CheckBox _chkMotionPred;

	private CheckBox _chkShowFps;

	private CheckBox _chkNoPickup;

	private CheckBox _chkSharpen;

	private CheckBox _chkFxaa;

	private CheckBox _chkShowMonsterRes;

	private NumericUpDown _nudWidth;

	private NumericUpDown _nudHeight;

	private Button _btnSaveSettings;

	private Button _btnPlay;

	private Button _btnBugForm;

	private Button _btnBugReport;

	private ComboBox _cmbVersion;

	private Label _lblStatus;

	private Label _lblVersion;

	private List<string> _availableVersions = new List<string>();

	private readonly string[] _menuLabels = new string[8] { "News", "Patch Notes", "Guide", "Settings", "Game Log", "AP Bridge", "Discord", "Exit" };

	private Rectangle[] _menuRects;

	private int _menuHover = -1;

	private bool _dragging;

	private Point _dragStart;

	private bool _closeHover;

	// 1.5.2 — close button repositioned to top-right of new wider window.
	// Coordinates anchored to WIN_W so any future width tweak follows.
	private readonly Rectangle _closeRect = new Rectangle(WIN_W - 46, 10, 36, 32);

	private readonly Font _fMenu = new Font("Segoe UI", 11.5f);

	private readonly Font _fMenuBold = new Font("Segoe UI", 12f, FontStyle.Bold);

	private readonly Font _fVer = new Font("Segoe UI", 8f);

	private GameDownloader? _downloader;

	// 1.7.1: guard against null/empty _gameDir (fresh install hasn't picked
	// a game folder yet). Callers already check File.Exists(D2glIniPath)
	// before reading, so an empty sentinel is safe — it just means "no
	// d2gl.ini available yet".
	private string D2glIniPath => string.IsNullOrEmpty(_gameDir)
		? ""
		: Path.Combine(_gameDir, "d2gl.ini");

	private string LauncherConfigPath => Path.Combine(_launcherDir, "launcher_config.ini");

	public MainForm()
	{
		_launcherDir = Path.GetDirectoryName(Application.ExecutablePath) ?? "";
		// 1.7.1: do NOT pre-populate _gameDir or _d2OriginalDir with guessed
		// defaults. A fresh install (no launcher_config.ini) must show EMPTY
		// fields so the user explicitly chooses where to install and where
		// their original Diablo II lives. Auto-detection happens only inside
		// StartInstall() as a suggestion when the user actually clicks
		// "INSTALL GAME", never silently on startup.
		LoadLauncherConfig();
		Text = "Diablo II Archipelago";
		base.ClientSize = new Size(WIN_W, WIN_H);
		base.FormBorderStyle = FormBorderStyle.None;
		// Window position: if a saved location is visible on any currently
		// attached monitor, use it; otherwise center on the primary screen.
		if (_savedWindowX != int.MinValue && _savedWindowY != int.MinValue
			&& IsPointOnAnyScreen(new Point(_savedWindowX + 50, _savedWindowY + 50)))
		{
			base.StartPosition = FormStartPosition.Manual;
			base.Location = new Point(_savedWindowX, _savedWindowY);
		}
		else
		{
			base.StartPosition = FormStartPosition.CenterScreen;
		}
		BackColor = Color.FromArgb(15, 12, 10);
		DoubleBuffered = true;
		SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, value: true);
		LoadSprites();
		CreateMenuRects();
		CreateContentArea();
		CreateSettingsPanel();
		CreateBottomBar();
		if (Directory.Exists(_gameDir) && File.Exists(Path.Combine(_gameDir, "Archipelago", "version.dat")))
		{
			_state = LState.UpToDate;
		}
		UpdatePlayButton();
		base.MouseMove += OnMouseMove;
		base.MouseDown += OnMouseDown;
		base.MouseUp += delegate
		{
			_dragging = false;
		};
		// Restore the page the user had open when the launcher was last closed,
		// falling back to News (0) when no saved value is present.
		ShowPage(_activePage);
		CheckUpdatesAsync();
	}

	private void LoadSprites()
	{
		string dir = Path.Combine(_launcherDir, "ui");
		_sprLogo = SafeLoad(dir, "logo.png");
		_sprSidebar = SafeLoad(dir, "sidebar.png");
		_sprContentPanel = SafeLoad(dir, "content_panel.png");
		// 1.5.2 — Iron Frame redesign assets. Missing files fall back to
		// solid colors via the LoadSprites callers, so the launcher still
		// renders if a user's update missed one of these.
		_sprStoneWall   = SafeLoad(dir, "stone_wall.png");
		_sprParchment   = SafeLoad(dir, "parchment.png");
		_sprArrow       = SafeLoad(dir, "arrow.png");
		_sprBottomBar   = SafeLoad(dir, "bottom_bar.png");
		_sprContentArea = SafeLoad(dir, "content_area.png");
		_sprSmallPanel  = SafeLoad(dir, "small_panel.png");

		// 1.5.3 — emerald flame strip is drawn behind the masthead seam.
		// Generated procedurally instead of shipping a PNG so the strip
		// matches WIN_W exactly (no scaling artifacts) and its alpha
		// composites cleanly with whatever's underneath.
		_sprFlameStrip = BuildFlameStrip(WIN_W, 70);
	}

	// 1.5.3 — Build a horizontal "rising flame" strip into a 32-bit
	// alpha bitmap. The strip is `width × height` px with flames
	// growing UP from its bottom edge. When DrawImage'd onto the form,
	// position it so the bitmap's bottom edge sits at y = BANNER_H so
	// flame tips reach up into the masthead area and the base lines
	// up with the seam between masthead and content.
	private static Bitmap BuildFlameStrip(int width, int height)
	{
		Bitmap bmp = new Bitmap(width, height);
		using Graphics g = Graphics.FromImage(bmp);
		g.SmoothingMode = SmoothingMode.HighQuality;
		g.CompositingMode = CompositingMode.SourceOver;
		g.Clear(Color.Transparent);

		// --------------------------------------------------------------
		// Layer 1: a soft emerald glow band along the bottom of the
		// strip, mimics the "smoldering coals" base of the reference
		// flame image. Vertical gradient: brightest at the very bottom,
		// fades to transparent ~60% up.
		// --------------------------------------------------------------
		int glowTop = (int)(height * 0.45f);
		using (var glow = new LinearGradientBrush(
			new Rectangle(0, glowTop, width, height - glowTop),
			Color.FromArgb(0, 30, 200, 50),
			Color.FromArgb(180, 100, 255, 80),
			LinearGradientMode.Vertical))
		{
			g.FillRectangle(glow, 0, glowTop, width, height - glowTop);
		}

		// --------------------------------------------------------------
		// Layer 2: individual flame tongues. Deterministic random so the
		// strip is identical across launches of the same exe build —
		// avoids flicker if the form is repainted twice in quick
		// succession.
		// --------------------------------------------------------------
		Random rng = new Random(0x71AE);
		int tongueCount = (int)(width / 14);          // ~75 tongues at 1040 px
		int xStep = Math.Max(1, width / tongueCount);
		for (int i = 0; i < tongueCount; i++)
		{
			int cx = i * xStep + rng.Next(-xStep / 2, xStep / 2);
			int tipHeight = (int)(height * (0.55f + (float)rng.NextDouble() * 0.4f));
			int tongueWidth = rng.Next(14, 32);
			int alpha = rng.Next(140, 220);
			DrawFlameTongue(g, cx, height, tongueWidth, tipHeight, rng, alpha);
		}

		// --------------------------------------------------------------
		// Layer 3: small bright sparks scattered along the base, give
		// the strip the speckled "ember" look of the reference image.
		// --------------------------------------------------------------
		for (int i = 0; i < 60; i++)
		{
			int sx = rng.Next(0, width);
			int sy = rng.Next((int)(height * 0.7f), height);
			int sr = rng.Next(2, 5);
			int sa = rng.Next(160, 230);
			using SolidBrush spark = new SolidBrush(
				Color.FromArgb(sa, 200, 255, 180));
			g.FillEllipse(spark, sx - sr, sy - sr, sr * 2, sr * 2);
		}

		return bmp;
	}

	// Draw a single flame tongue: a vertical Bezier-shaped path with a
	// radial green→bright-green gradient. Center of the gradient sits
	// near the base so the tongue reads as "burning bright at bottom,
	// dimming toward tip", just like the reference image.
	private static void DrawFlameTongue(Graphics g, int cx, int baseY,
		int width, int height, Random rng, int alpha)
	{
		int top = baseY - height;
		int wave = rng.Next(-8, 8);          // tiny lateral wobble at tip
		float w2 = width / 2f;
		float w3 = width / 3f;
		float w4 = width / 4f;
		float h2 = height * 0.5f;
		float h25 = height * 0.25f;

		using GraphicsPath path = new GraphicsPath();
		path.AddBezier(
			new PointF(cx - w2, baseY),
			new PointF(cx - w3, baseY - h2),
			new PointF(cx - w4 + wave, top + h25),
			new PointF(cx + wave, top));
		path.AddBezier(
			new PointF(cx + wave, top),
			new PointF(cx + w4 - wave, top + h25),
			new PointF(cx + w3, baseY - h2),
			new PointF(cx + w2, baseY));
		path.CloseFigure();

		using PathGradientBrush brush = new PathGradientBrush(path);
		brush.CenterPoint = new PointF(cx, baseY - height * 0.25f);
		brush.CenterColor = Color.FromArgb(alpha, 200, 255, 160);
		brush.SurroundColors = new[] { Color.FromArgb(0, 0, 80, 30) };
		g.FillPath(brush, path);
	}

	private static Image? SafeLoad(string dir, string file)
	{
		string text = Path.Combine(dir, file);
		try
		{
			return File.Exists(text) ? Image.FromFile(text) : null;
		}
		catch
		{
			return null;
		}
	}

	private void CreateMenuRects()
	{
		// 1.5.2 — match the design HTML's sidebar nav layout:
		//   padding: 60px 28px 60px 42px → items sit at x=42 inside the
		//   painted iron frame, well clear of the 30-px-thick left edge.
		//   32px row height with 8px gap so 8 items + flex-spacer all fit.
		//
		// 1.5.2b — EXIT no longer flush-bottom-anchored. The flex-spacer
		// gap looked weird (a 100+ px void between Discord and Exit when
		// rendered on Windows). Stack all items sequentially instead;
		// the painted sidebar frame is plenty tall enough for 8 evenly
		// spaced rows.
		_menuRects = new Rectangle[_menuLabels.Length];
		int navItemHeight = 32;
		int navGap = 8;
		int navStride = navItemHeight + navGap;
		int navX = 42;                              // inset past iron edge
		int navW = SIDEBAR_W - navX - 28;          // right padding 28
		int navTopInset = BANNER_H + 56;            // clears top frame curl
		for (int i = 0; i < _menuLabels.Length; i++)
		{
			_menuRects[i] = new Rectangle(navX,
				navTopInset + i * navStride, navW, navItemHeight);
		}
	}

	private void CreateContentArea()
	{
		// 1.5.2 — Iron Frame layout. Content viewport sits inside the
		// content_area.png frame painted in OnPaint, with 32px inset on
		// each side so text doesn't collide with the iron border.
		int x = SIDEBAR_W + 32;
		int y = BANNER_H + 30;
		int width = WIN_W - SIDEBAR_W - 56;
		int height = WIN_H - BANNER_H - BOTTOM_H - 50;

		_txtContent = new RichTextBox
		{
			Location = new Point(x, y),
			Size = new Size(width, height),
			BackColor = CL_STONE,            // sits on top of the painted frame
			ForeColor = CL_INK,
			Font = new Font("Segoe UI", 10.5f),
			BorderStyle = BorderStyle.None,
			ReadOnly = true,
			ScrollBars = RichTextBoxScrollBars.Vertical,
			Text = "Loading news..."
		};
		base.Controls.Add(_txtContent);
		_txtGameLog = new RichTextBox
		{
			Location = new Point(x, y),
			Size = new Size(width, height),
			BackColor = Color.FromArgb(6, 4, 3),     // log terminal background
			ForeColor = Color.FromArgb(110, 200, 110),
			Font = new Font("Consolas", 9.5f),
			BorderStyle = BorderStyle.None,
			ReadOnly = true,
			ScrollBars = RichTextBoxScrollBars.Vertical,
			Text = "Game log will appear here when the game is running.\n",
			Visible = false
		};
		base.Controls.Add(_txtGameLog);
		_txtBridgeLog = new RichTextBox
		{
			Location = new Point(x, y),
			Size = new Size(width, height),
			BackColor = Color.FromArgb(6, 4, 3),
			ForeColor = Color.FromArgb(120, 180, 245),
			Font = new Font("Consolas", 9.5f),
			BorderStyle = BorderStyle.None,
			ReadOnly = true,
			ScrollBars = RichTextBoxScrollBars.Vertical,
			Text = "AP Bridge log will appear here when connected.\n",
			Visible = false
		};
		base.Controls.Add(_txtBridgeLog);
	}

	private void CreateSettingsPanel()
	{
		// 1.5.2 — match the content viewport coordinates so the settings
		// panel sits inside the painted content_area frame.
		int x = SIDEBAR_W + 32;
		int y = BANNER_H + 30;
		int width = WIN_W - SIDEBAR_W - 56;
		int height = WIN_H - BANNER_H - BOTTOM_H - 50;
		_settingsPanel = new Panel
		{
			Location = new Point(x, y),
			Size = new Size(width, height),
			BackColor = CL_STONE,
			AutoScroll = true,
			Visible = false
		};
		base.Controls.Add(_settingsPanel);
		Font font = new Font("Segoe UI", 13f, FontStyle.Bold);
		Font font2 = new Font("Segoe UI", 10f, FontStyle.Bold);
		Font font3 = new Font("Segoe UI", 10f);
		Color clr = Color.FromArgb(220, 190, 100);
		Color clr2 = Color.FromArgb(180, 155, 100);
		Color clr3 = Color.FromArgb(175, 160, 130);
		int num = 10;
		AddLabel(_settingsPanel, "Settings", 15, num, font, clr);
		num += 35;
		AddLabel(_settingsPanel, "Game Installation Path", 15, num, font2, clr2);
		num += 26;
		_txtGamePath = new TextBox
		{
			Location = new Point(20, num),
			Size = new Size(520, 28),
			Font = new Font("Segoe UI", 10f),
			BackColor = Color.FromArgb(35, 28, 18),
			ForeColor = Color.FromArgb(200, 185, 150),
			Text = _gameDir
		};
		_settingsPanel.Controls.Add(_txtGamePath);
		_btnBrowsePath = new Button
		{
			Text = "Browse...",
			Location = new Point(550, num - 1),
			Size = new Size(90, 30),
			FlatStyle = FlatStyle.Flat,
			Font = new Font("Segoe UI", 9.5f),
			ForeColor = Color.FromArgb(180, 160, 120),
			BackColor = Color.FromArgb(40, 33, 22),
			Cursor = Cursors.Hand
		};
		_btnBrowsePath.FlatAppearance.BorderColor = Color.FromArgb(80, 65, 42);
		_btnBrowsePath.FlatAppearance.MouseOverBackColor = Color.FromArgb(55, 45, 28);
		_btnBrowsePath.Click += delegate
		{
			FolderBrowserDialog folderBrowserDialog = new FolderBrowserDialog
			{
				Description = "Select game installation folder",
				SelectedPath = _gameDir,
				UseDescriptionForTitle = true
			};
			try
			{
				if (folderBrowserDialog.ShowDialog() == DialogResult.OK)
				{
					_gameDir = folderBrowserDialog.SelectedPath;
					_txtGamePath.Text = _gameDir;
					SaveLauncherConfig();
					bool flag = Directory.Exists(_gameDir) && File.Exists(Path.Combine(_gameDir, "Archipelago", "version.dat"));
					_state = (flag ? LState.UpToDate : LState.NotInstalled);
					UpdatePlayButton();
					SetStatus(flag ? ("Game found at: " + _gameDir) : "No game found at selected path");
					LoadSettings();
				}
			}
			finally
			{
				((IDisposable)(object)folderBrowserDialog)?.Dispose();
			}
		};
		_settingsPanel.Controls.Add(_btnBrowsePath);
		num += 40;
		AddLabel(_settingsPanel, "Original Diablo II Installation", 15, num, font2, clr2);
		num += 26;
		_txtD2OriginalPath = new TextBox
		{
			Location = new Point(20, num),
			Size = new Size(520, 28),
			Font = new Font("Segoe UI", 10f),
			BackColor = Color.FromArgb(35, 28, 18),
			ForeColor = Color.FromArgb(200, 185, 150),
			Text = _d2OriginalDir
		};
		_settingsPanel.Controls.Add(_txtD2OriginalPath);
		_btnBrowseD2Original = new Button
		{
			Text = "Browse...",
			Location = new Point(550, num - 1),
			Size = new Size(90, 30),
			FlatStyle = FlatStyle.Flat,
			Font = new Font("Segoe UI", 9.5f),
			ForeColor = Color.FromArgb(180, 160, 120),
			BackColor = Color.FromArgb(40, 33, 22),
			Cursor = Cursors.Hand
		};
		_btnBrowseD2Original.FlatAppearance.BorderColor = Color.FromArgb(80, 65, 42);
		_btnBrowseD2Original.FlatAppearance.MouseOverBackColor = Color.FromArgb(55, 45, 28);
		_btnBrowseD2Original.Click += delegate
		{
			FolderBrowserDialog folderBrowserDialog = new FolderBrowserDialog
			{
				Description = "Select your original Diablo II installation folder (containing Game.exe and .mpq files)",
				SelectedPath = _d2OriginalDir,
				UseDescriptionForTitle = true
			};
			try
			{
				if (folderBrowserDialog.ShowDialog() == DialogResult.OK)
				{
					_d2OriginalDir = folderBrowserDialog.SelectedPath;
					_txtD2OriginalPath.Text = _d2OriginalDir;
					SaveLauncherConfig();
					SetStatus(ValidateD2OriginalDir(_d2OriginalDir)
						? "Original D2 found: " + _d2OriginalDir
						: "Warning: Game.exe or d2data.mpq not found in selected folder");
				}
			}
			finally
			{
				((IDisposable)(object)folderBrowserDialog)?.Dispose();
			}
		};
		_settingsPanel.Controls.Add(_btnBrowseD2Original);
		num += 40;
		AddLabel(_settingsPanel, "Archipelago Custom Worlds Folder", 15, num, font2, clr2);
		num += 26;
		string text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "Archipelago", "custom_worlds");
		_txtApWorldPath = new TextBox
		{
			Location = new Point(20, num),
			Size = new Size(520, 28),
			Font = new Font("Segoe UI", 10f),
			BackColor = Color.FromArgb(35, 28, 18),
			ForeColor = Color.FromArgb(200, 185, 150),
			Text = (_apWorldDir ?? text)
		};
		_settingsPanel.Controls.Add(_txtApWorldPath);
		_btnBrowseApWorld = new Button
		{
			Text = "Browse...",
			Location = new Point(550, num - 1),
			Size = new Size(90, 30),
			FlatStyle = FlatStyle.Flat,
			Font = new Font("Segoe UI", 9.5f),
			ForeColor = Color.FromArgb(180, 160, 120),
			BackColor = Color.FromArgb(40, 33, 22),
			Cursor = Cursors.Hand
		};
		_btnBrowseApWorld.FlatAppearance.BorderColor = Color.FromArgb(80, 65, 42);
		_btnBrowseApWorld.FlatAppearance.MouseOverBackColor = Color.FromArgb(55, 45, 28);
		_btnBrowseApWorld.Click += delegate
		{
			FolderBrowserDialog folderBrowserDialog = new FolderBrowserDialog
			{
				Description = "Select Archipelago custom_worlds folder",
				SelectedPath = _txtApWorldPath.Text,
				UseDescriptionForTitle = true
			};
			try
			{
				if (folderBrowserDialog.ShowDialog() == DialogResult.OK)
				{
					_apWorldDir = folderBrowserDialog.SelectedPath;
					_txtApWorldPath.Text = _apWorldDir;
					SaveLauncherConfig();
					SetStatus("AP World folder set: " + _apWorldDir);
				}
			}
			finally
			{
				((IDisposable)(object)folderBrowserDialog)?.Dispose();
			}
		};
		_settingsPanel.Controls.Add(_btnBrowseApWorld);
		num += 40;
		AddLabel(_settingsPanel, "Screen", 15, num, font2, clr2);
		num += 28;
		_chkFullscreen = AddCheckbox(_settingsPanel, "Fullscreen", 20, num, font3, clr3);
		_chkVsync = AddCheckbox(_settingsPanel, "VSync", 220, num, font3, clr3);
		num += 32;
		AddLabel(_settingsPanel, "Window Width:", 20, num + 3, font3, clr3);
		_nudWidth = AddNumeric(_settingsPanel, 150, num, 800, 3840, 1600);
		AddLabel(_settingsPanel, "Height:", 280, num + 3, font3, clr3);
		_nudHeight = AddNumeric(_settingsPanel, 340, num, 600, 2160, 1200);
		num += 38;
		AddLabel(_settingsPanel, "Features", 15, num, font2, clr2);
		num += 28;
		_chkSkipIntro = AddCheckbox(_settingsPanel, "Skip Intro Videos", 20, num, font3, clr3);
		_chkHdCursor = AddCheckbox(_settingsPanel, "HD Cursor", 220, num, font3, clr3);
		num += 32;
		_chkHdText = AddCheckbox(_settingsPanel, "HD Text", 20, num, font3, clr3);
		_chkMinimap = AddCheckbox(_settingsPanel, "Minimap Widget", 220, num, font3, clr3);
		num += 32;
		_chkMotionPred = AddCheckbox(_settingsPanel, "Motion Prediction", 20, num, font3, clr3);
		_chkNoPickup = AddCheckbox(_settingsPanel, "No Pickup (/nopickup)", 220, num, font3, clr3);
		num += 32;
		_chkShowFps = AddCheckbox(_settingsPanel, "Show FPS Counter", 20, num, font3, clr3);
		_chkShowMonsterRes = AddCheckbox(_settingsPanel, "Show Monster Resistances", 220, num, font3, clr3);
		num += 38;
		AddLabel(_settingsPanel, "Graphics", 15, num, font2, clr2);
		num += 28;
		_chkSharpen = AddCheckbox(_settingsPanel, "Luma Sharpen", 20, num, font3, clr3);
		_chkFxaa = AddCheckbox(_settingsPanel, "Anti-Aliasing (FXAA)", 220, num, font3, clr3);
		num += 40;
		_btnSaveSettings = new Button
		{
			Text = "SAVE SETTINGS",
			Location = new Point(20, num),
			Size = new Size(180, 40),
			FlatStyle = FlatStyle.Flat,
			Font = new Font("Segoe UI", 11f, FontStyle.Bold),
			ForeColor = Color.FromArgb(225, 200, 110),
			BackColor = Color.FromArgb(50, 40, 25),
			Cursor = Cursors.Hand
		};
		_btnSaveSettings.FlatAppearance.BorderColor = Color.FromArgb(130, 108, 58);
		_btnSaveSettings.FlatAppearance.MouseOverBackColor = Color.FromArgb(65, 52, 28);
		_btnSaveSettings.Click += delegate
		{
			SaveSettings();
		};
		_settingsPanel.Controls.Add(_btnSaveSettings);
		LoadSettings();
	}

	private Label AddLabel(Control parent, string text, int x, int y, Font font, Color clr)
	{
		Label label = new Label
		{
			Text = text,
			Location = new Point(x, y),
			AutoSize = true,
			Font = font,
			ForeColor = clr,
			BackColor = Color.Transparent
		};
		parent.Controls.Add(label);
		return label;
	}

	private CheckBox AddCheckbox(Control parent, string text, int x, int y, Font font, Color clr)
	{
		CheckBox checkBox = new CheckBox
		{
			Text = text,
			Location = new Point(x, y),
			AutoSize = true,
			Font = font,
			ForeColor = clr,
			BackColor = Color.Transparent
		};
		parent.Controls.Add(checkBox);
		return checkBox;
	}

	private NumericUpDown AddNumeric(Control parent, int x, int y, int min, int max, int val)
	{
		NumericUpDown numericUpDown = new NumericUpDown
		{
			Location = new Point(x, y),
			Size = new Size(100, 28),
			Minimum = min,
			Maximum = max,
			Value = val,
			Font = new Font("Segoe UI", 10f),
			BackColor = Color.FromArgb(35, 28, 18),
			ForeColor = Color.FromArgb(200, 185, 150)
		};
		parent.Controls.Add(numericUpDown);
		return numericUpDown;
	}

	private void LoadSettings()
	{
		if (!File.Exists(D2glIniPath))
		{
			return;
		}
		try
		{
			string[] array = File.ReadAllLines(D2glIniPath);
			Dictionary<string, string> dictionary = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
			string[] array2 = array;
			for (int i = 0; i < array2.Length; i++)
			{
				string text = array2[i].Trim();
				if (!text.StartsWith(";") && !text.StartsWith("[") && text.Contains('='))
				{
					string[] array3 = text.Split('=', 2);
					if (array3.Length == 2)
					{
						dictionary[array3[0].Trim()] = array3[1].Trim();
					}
				}
			}
			_chkFullscreen.Checked = GetBool(dictionary, "fullscreen");
			_chkVsync.Checked = GetBool(dictionary, "vsync");
			_chkSkipIntro.Checked = GetBool(dictionary, "skip_intro");
			_chkHdCursor.Checked = GetBool(dictionary, "hd_cursor", def: true);
			_chkHdText.Checked = GetBool(dictionary, "hd_text", def: true);
			_chkMinimap.Checked = GetBool(dictionary, "mini_map");
			_chkMotionPred.Checked = GetBool(dictionary, "motion_prediction");
			_chkNoPickup.Checked = GetBool(dictionary, "no_pickup");
			_chkShowFps.Checked = GetBool(dictionary, "show_fps");
			_chkShowMonsterRes.Checked = GetBool(dictionary, "show_monster_res", def: true);
			_chkSharpen.Checked = GetBool(dictionary, "sharpen", def: true);
			_chkFxaa.Checked = GetBool(dictionary, "fxaa");
			if (dictionary.TryGetValue("window_width", out var value) && int.TryParse(value, out var result))
			{
				_nudWidth.Value = Math.Clamp(result, (int)_nudWidth.Minimum, (int)_nudWidth.Maximum);
			}
			if (dictionary.TryGetValue("window_height", out var value2) && int.TryParse(value2, out var result2))
			{
				_nudHeight.Value = Math.Clamp(result2, (int)_nudHeight.Minimum, (int)_nudHeight.Maximum);
			}
		}
		catch
		{
		}
	}

	private static bool GetBool(Dictionary<string, string> ini, string key, bool def = false)
	{
		if (ini.TryGetValue(key, out string value))
		{
			return value.Equals("true", StringComparison.OrdinalIgnoreCase);
		}
		return def;
	}

	private void SaveSettings()
	{
		if (_txtGamePath != null && _txtGamePath.Text != _gameDir)
		{
			_gameDir = _txtGamePath.Text;
			bool flag = Directory.Exists(_gameDir) && File.Exists(Path.Combine(_gameDir, "Archipelago", "version.dat"));
			_state = (flag ? LState.UpToDate : LState.NotInstalled);
			UpdatePlayButton();
		}
		// Collect every d2gl setting in a dictionary so we can either apply it
		// live (when the game is installed) or defer it until after install
		// completes (catch-22 fix: user shouldn't have to install first, then
		// open settings separately).
		Dictionary<string, string> pending = BuildPendingD2GLMap();

		if (!File.Exists(D2glIniPath))
		{
			// Game not installed yet — stash settings in launcher_config.ini
			// under [pending_d2gl]; ApplyPendingD2GLSettings() will flush them
			// into d2gl.ini after InstallFromReleaseAsync succeeds.
			try
			{
				SavePendingD2GLSettings(pending);
				if (_txtApWorldPath != null)
				{
					_apWorldDir = _txtApWorldPath.Text;
				}
				SaveLauncherConfig();
				SetStatus("Settings saved. They will be applied after game install completes.");
			}
			catch (Exception ex)
			{
				MessageBox.Show("Failed to save pending settings:\n" + ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Hand);
			}
			return;
		}
		try
		{
			List<string> list = File.ReadAllLines(D2glIniPath).ToList();
			foreach (var kv in pending)
			{
				SetIniValueRaw(list, kv.Key, kv.Value);
			}
			File.WriteAllLines(D2glIniPath, list);
			if (_txtApWorldPath != null)
			{
				_apWorldDir = _txtApWorldPath.Text;
			}
			SaveLauncherConfig();
			CopyApWorldFile();
			// If any pending settings were previously stashed, clear them now —
			// they've just been flushed to the live d2gl.ini.
			ClearPendingD2GLSettings();
			SetStatus("Settings saved!");
		}
		catch (Exception ex)
		{
			MessageBox.Show("Failed to save settings:\n" + ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Hand);
		}
	}

	private Dictionary<string, string> BuildPendingD2GLMap()
	{
		return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
		{
			{ "fullscreen", _chkFullscreen.Checked.ToString().ToLower() },
			{ "vsync", _chkVsync.Checked.ToString().ToLower() },
			{ "window_width", ((int)_nudWidth.Value).ToString() },
			{ "window_height", ((int)_nudHeight.Value).ToString() },
			{ "skip_intro", _chkSkipIntro.Checked.ToString().ToLower() },
			{ "hd_cursor", _chkHdCursor.Checked.ToString().ToLower() },
			{ "hd_text", _chkHdText.Checked.ToString().ToLower() },
			{ "mini_map", _chkMinimap.Checked.ToString().ToLower() },
			{ "motion_prediction", _chkMotionPred.Checked.ToString().ToLower() },
			{ "no_pickup", _chkNoPickup.Checked.ToString().ToLower() },
			{ "show_fps", _chkShowFps.Checked.ToString().ToLower() },
			{ "show_monster_res", _chkShowMonsterRes.Checked.ToString().ToLower() },
			{ "sharpen", _chkSharpen.Checked.ToString().ToLower() },
			{ "fxaa", _chkFxaa.Checked.ToString().ToLower() },
		};
	}

	private static void SetIniValueRaw(List<string> lines, string key, string val)
	{
		string newLine = key + "=" + val;
		for (int i = 0; i < lines.Count; i++)
		{
			if (lines[i].TrimStart().StartsWith(key + "=", StringComparison.OrdinalIgnoreCase))
			{
				lines[i] = newLine;
				return;
			}
		}
		lines.Add(newLine);
	}

	// Persist pending d2gl values into launcher_config.ini under a dedicated
	// [pending_d2gl] section so ApplyPendingD2GLSettings() can find them after
	// the game is installed.
	private void SavePendingD2GLSettings(Dictionary<string, string> pending)
	{
		List<string> lines = File.Exists(LauncherConfigPath)
			? File.ReadAllLines(LauncherConfigPath).ToList()
			: new List<string>();

		// Strip any existing [pending_d2gl] section so we don't accumulate stale
		// values across repeated saves.
		List<string> filtered = new List<string>();
		bool inPending = false;
		foreach (string line in lines)
		{
			string trimmed = line.Trim();
			if (trimmed.StartsWith("[pending_d2gl]", StringComparison.OrdinalIgnoreCase))
			{
				inPending = true;
				continue;
			}
			if (inPending && trimmed.StartsWith("[") && trimmed.EndsWith("]"))
			{
				inPending = false;
			}
			if (!inPending)
			{
				filtered.Add(line);
			}
		}

		// Append fresh section.
		if (filtered.Count > 0 && !string.IsNullOrWhiteSpace(filtered[filtered.Count - 1]))
		{
			filtered.Add("");
		}
		filtered.Add("[pending_d2gl]");
		foreach (var kv in pending)
		{
			filtered.Add(kv.Key + "=" + kv.Value);
		}
		File.WriteAllLines(LauncherConfigPath, filtered);
	}

	private void ClearPendingD2GLSettings()
	{
		if (!File.Exists(LauncherConfigPath))
		{
			return;
		}
		try
		{
			List<string> lines = File.ReadAllLines(LauncherConfigPath).ToList();
			List<string> filtered = new List<string>();
			bool inPending = false;
			foreach (string line in lines)
			{
				string trimmed = line.Trim();
				if (trimmed.StartsWith("[pending_d2gl]", StringComparison.OrdinalIgnoreCase))
				{
					inPending = true;
					continue;
				}
				if (inPending && trimmed.StartsWith("[") && trimmed.EndsWith("]"))
				{
					inPending = false;
				}
				if (!inPending)
				{
					filtered.Add(line);
				}
			}
			File.WriteAllLines(LauncherConfigPath, filtered);
		}
		catch { }
	}

	// Reads any [pending_d2gl] section from launcher_config.ini and writes the
	// values into the now-existing d2gl.ini, then clears the section. Called by
	// the post-install completion handler.
	private void ApplyPendingD2GLSettings()
	{
		if (!File.Exists(LauncherConfigPath) || !File.Exists(D2glIniPath))
		{
			return;
		}
		try
		{
			Dictionary<string, string> pending = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
			bool inPending = false;
			foreach (string line in File.ReadAllLines(LauncherConfigPath))
			{
				string trimmed = line.Trim();
				if (trimmed.StartsWith("[pending_d2gl]", StringComparison.OrdinalIgnoreCase))
				{
					inPending = true;
					continue;
				}
				if (inPending && trimmed.StartsWith("[") && trimmed.EndsWith("]"))
				{
					inPending = false;
					continue;
				}
				if (inPending && trimmed.Contains('=') && !trimmed.StartsWith(";"))
				{
					string[] parts = trimmed.Split('=', 2);
					if (parts.Length == 2)
					{
						pending[parts[0].Trim()] = parts[1].Trim();
					}
				}
			}
			if (pending.Count == 0)
			{
				return;
			}
			List<string> iniLines = File.ReadAllLines(D2glIniPath).ToList();
			foreach (var kv in pending)
			{
				SetIniValueRaw(iniLines, kv.Key, kv.Value);
			}
			File.WriteAllLines(D2glIniPath, iniLines);
			ClearPendingD2GLSettings();
			SetStatus($"Applied {pending.Count} pending setting(s) to d2gl.ini.");
		}
		catch { }
	}

	private static void SetIniValue(List<string> lines, string key, bool val)
	{
		string newLine = key + "=" + val.ToString().ToLower();
		for (int i = 0; i < lines.Count; i++)
		{
			if (lines[i].TrimStart().StartsWith(key + "=", StringComparison.OrdinalIgnoreCase))
			{
				lines[i] = newLine;
				return;
			}
		}
		// Key does not exist in the file yet — append it so settings save
		// correctly on the first write (previous behaviour silently dropped
		// the value when the key was missing).
		lines.Add(newLine);
	}

	private static void SetIniValue(List<string> lines, string key, int val)
	{
		string newLine = $"{key}={val}";
		for (int i = 0; i < lines.Count; i++)
		{
			if (lines[i].TrimStart().StartsWith(key + "=", StringComparison.OrdinalIgnoreCase))
			{
				lines[i] = newLine;
				return;
			}
		}
		// Append rather than silently drop (see comment in bool overload).
		lines.Add(newLine);
	}

	private void ShowPage(int page)
	{
		_activePage = page;
		// 1.5.1 — Page index map (renumbered to insert Guide at 2):
		//   0 = News         (RichTextBox _txtContent)
		//   1 = Patch Notes  (RichTextBox _txtContent)
		//   2 = Guide        (RichTextBox _txtContent)  — NEW
		//   3 = Settings     (Panel       _settingsPanel)
		//   4 = Game Log     (RichTextBox _txtGameLog)
		//   5 = AP Bridge    (RichTextBox _txtBridgeLog)
		_txtContent.Visible  = page == 0 || page == 1 || page == 2;
		_settingsPanel.Visible = page == 3;
		_txtGameLog.Visible  = page == 4;
		_txtBridgeLog.Visible = page == 5;
		switch (page)
		{
		case 0:
			_txtContent.Text = (string.IsNullOrEmpty(_newsText) ? "Welcome to Diablo II Archipelago!\n\nNo news available yet.\n\nNews will be loaded from GitHub when available.\nCheck back after the first game update!" : _newsText);
			break;
		case 1:
			_txtContent.Text = _patchNotes;
			break;
		case 2:
			_txtContent.Text = (string.IsNullOrEmpty(_settingsGuide)
				? "Settings Guide is loading...\n\nIf this message persists, the launcher may be offline. The guide ships with the launcher_package.zip and is also fetched fresh from GitHub on each launch.\n\nThe guide explains every YAML setting, all four Goal modes, sphere/fill logic, and recommended setups for solo + multiworld."
				: _settingsGuide);
			_txtContent.SelectionStart = 0;
			_txtContent.ScrollToCaret();
			break;
		case 3:
			LoadSettings();
			break;
		case 4:
			_txtGameLog.SelectionStart = _txtGameLog.TextLength;
			_txtGameLog.ScrollToCaret();
			break;
		case 5:
			_txtBridgeLog.SelectionStart = _txtBridgeLog.TextLength;
			_txtBridgeLog.ScrollToCaret();
			break;
		}
		Invalidate();
	}

	private void CreateBottomBar()
	{
		// 1.5.2 — Iron Frame action bar (124px tall) sits at the bottom
		// of a 1040×660 window. Coordinates anchor to WIN_W/WIN_H so the
		// layout follows any future window resize.
		//
		// 1.5.2b — repositioned text + buttons to sit inside the visible
		// interior of bottom_bar.png. The PNG paints iron edges on its
		// top + bottom ~30 px each, so the readable "stone" band is
		// roughly the central 60-65 px. statusY/sideY/playY were too
		// high, leaving status text on the seam line above the bar.
		int barTop = WIN_H - BOTTOM_H;       // y of the action bar (= 536)
		int playW = 224;
		int playH = 56;
		int playX = WIN_W - playW - 28;      // right-aligned inside bar
		int playY = barTop + 36;             // pushed down into bar interior
		int dropY = playY + playH + 4;
		int sideY = barTop + 46;             // matches the play button vertical center
		// 1.5.2c — pushed status text further right (32 → 64) so it sits
		// past the painted iron-corner ornament on the left edge of
		// bottom_bar.png. At 32 the text was clipping against the iron.
		int statusX = 64;
		int statusY = barTop + 42;           // inside the readable band

		_btnPlay = new Button
		{
			Size = new Size(playW, playH),
			Location = new Point(playX, playY),
			Text = "INSTALL GAME",
			FlatStyle = FlatStyle.Flat,
			Font = new Font("Segoe UI", 13f, FontStyle.Bold),
			ForeColor = CL_GOLD_HI,
			BackColor = Color.FromArgb(45, 35, 20),
			Cursor = Cursors.Hand
		};
		_btnPlay.FlatAppearance.BorderColor = Color.FromArgb(140, 115, 60);
		_btnPlay.FlatAppearance.BorderSize = 2;
		_btnPlay.FlatAppearance.MouseOverBackColor = Color.FromArgb(65, 52, 28);
		_btnPlay.FlatAppearance.MouseDownBackColor = Color.FromArgb(80, 65, 35);
		_btnPlay.Click += delegate
		{
			OnMainButton();
		};
		base.Controls.Add(_btnPlay);
		_cmbVersion = new ComboBox
		{
			Size = new Size(playW, 24),
			Location = new Point(playX, dropY),
			DropDownStyle = ComboBoxStyle.DropDownList,
			MaxDropDownItems = 10,
			Font = new Font("Segoe UI", 9f),
			BackColor = Color.FromArgb(35, 28, 18),
			ForeColor = CL_INK_SOFT,
			FlatStyle = FlatStyle.Flat
		};
		_cmbVersion.Items.Add("Latest");
		_cmbVersion.SelectedIndex = 0;
		_cmbVersion.SelectedIndexChanged += delegate
		{
			RecomputeStateForSelection();
		};
		base.Controls.Add(_cmbVersion);
		// Bug Form / Report Bug — placed left of the play button, vertical center.
		int bugFormX = playX - 230;
		int reportX  = playX - 116;
		_btnBugForm = MakeSmallButton("Bug Form", bugFormX, sideY);
		_btnBugForm.Click += delegate
		{
			OpenUrl("https://docs.google.com/spreadsheets/d/14L8DUuIc8Z6Yfw0dM2nj3dnoM1PRRMn-2Qh_eneP5Ls/edit?gid=864403189#gid=864403189");
		};
		base.Controls.Add(_btnBugForm);
		_btnBugReport = MakeSmallButton("Report Bug", reportX, sideY, 110);
		_btnBugReport.Click += delegate
		{
			OpenUrl("https://github.com/solida1987/Diablo-II-Archipelago/issues/new");
		};
		base.Controls.Add(_btnBugReport);
		_lblStatus = new Label
		{
			Location = new Point(statusX, statusY),
			Size = new Size(playX - statusX - 250, 26),
			Font = new Font("Segoe UI", 12f, FontStyle.Bold),
			ForeColor = CL_INK,
			BackColor = Color.Transparent,
			Text = _statusText
		};
		base.Controls.Add(_lblStatus);
		// 1.5.2c — version sub-line was using CL_INK_MUTE (#7a6f5c) which
		// was nearly invisible against the dark stone interior of
		// bottom_bar.png. Bumped to CL_INK_SOFT (#b8a888) — same softer
		// secondary tone the design HTML uses for `.selected small`.
		_lblVersion = new Label
		{
			Location = new Point(statusX, statusY + 30),
			Size = new Size(playX - statusX - 250, 20),
			Font = new Font("Segoe UI", 9.5f, FontStyle.Italic),
			ForeColor = CL_INK_SOFT,
			BackColor = Color.Transparent,
			Text = _versionText
		};
		base.Controls.Add(_lblVersion);
	}

	private Button MakeSmallButton(string text, int x, int y, int w = 100)
	{
		// 1.5.2b — secondary buttons (Bug Form / Report Bug) now wear the
		// painted small_panel.png plate so they blend with the iron-bound
		// action bar. Falls back to a flat dark button if the asset is
		// missing.
		Button button = new Button();
		button.Size = new Size(w, 36);
		button.Location = new Point(x, y);
		button.Text = text;
		button.FlatStyle = FlatStyle.Flat;
		button.Font = new Font("Segoe UI", 9.5f, FontStyle.Bold);
		button.ForeColor = CL_GOLD;
		button.Cursor = Cursors.Hand;
		button.FlatAppearance.BorderSize = 0;
		button.FlatAppearance.MouseOverBackColor = Color.Transparent;
		button.FlatAppearance.MouseDownBackColor = Color.Transparent;

		if (_sprSmallPanel != null)
		{
			// Use the painted iron-stone plate as the button surface.
			button.BackgroundImage = _sprSmallPanel;
			button.BackgroundImageLayout = ImageLayout.Stretch;
			button.BackColor = Color.Transparent;
			// Hover/focus colors are handled by ForeColor swap in event
			// handlers below — the plate stays visually static so the
			// painted iron texture doesn't get washed out.
			button.MouseEnter += (s, e) => { button.ForeColor = CL_GOLD_HI; };
			button.MouseLeave += (s, e) => { button.ForeColor = CL_GOLD; };
		}
		else
		{
			// Fallback: flat dark button with iron-edge border.
			button.BackColor = Color.FromArgb(30, 25, 16);
			button.FlatAppearance.BorderSize = 1;
			button.FlatAppearance.BorderColor = CL_IRON;
			button.FlatAppearance.MouseOverBackColor = Color.FromArgb(45, 38, 24);
		}
		return button;
	}

	protected override void OnPaint(PaintEventArgs e)
	{
		Graphics graphics = e.Graphics;
		graphics.SmoothingMode = SmoothingMode.HighQuality;
		graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
		graphics.TextRenderingHint = TextRenderingHint.ClearTypeGridFit;

		// 1.5.2 — Iron Frame redesign render pipeline.
		// 1. Stone-wall texture as the whole-window background (subtle
		//    courtyard feel; falls back to the dark void color).
		// 2. Masthead band on top — the logo PNG sits centered, version
		//    label tucked under the close button.
		// 3. Sidebar painted from sidebar.png on the left rail.
		// 4. Content area: content_area.png frame + parchment.png as a
		//    soft overlay (clipped to the viewport).
		// 5. Bottom bar: bottom_bar.png stretched full width.
		// All controls (RichTextBox / Buttons) are positioned later in
		// CreateContentArea / CreateSettingsPanel / CreateBottomBar.

		graphics.Clear(CL_VOID);

		// Stone-wall background — drawn first so everything else layers
		// on top. We center+stretch to fill the window so seams don't
		// show on resize.
		if (_sprStoneWall != null)
		{
			graphics.DrawImage(_sprStoneWall, 0, 0, WIN_W, WIN_H);
			// Darken slightly so artwork blends instead of dominating.
			using SolidBrush voidVeil = new SolidBrush(Color.FromArgb(170, CL_VOID));
			graphics.FillRectangle(voidVeil, 0, 0, WIN_W, WIN_H);
		}

		// 1.5.3 — emerald flame strip drawn BEFORE the masthead band so
		// it sits behind the entire chrome. We position the flames so
		// their base aligns with the seam at y = BANNER_H, tips reaching
		// up into the bottom of the masthead. The masthead darkening
		// below uses a vertical fade-out over its bottom 50px so the
		// flames glow through cleanly at the seam.
		if (_sprFlameStrip != null)
		{
			int flameY = BANNER_H - _sprFlameStrip.Height;
			graphics.DrawImage(_sprFlameStrip, 0, flameY,
				_sprFlameStrip.Width, _sprFlameStrip.Height);
		}

		// Masthead band: darker stone fill across the top of the window,
		// but with a smooth fade to fully transparent over its bottom
		// ~50 px so the flame strip behind shows through at the seam.
		int mastFadeStart = BANNER_H - 50;
		using (SolidBrush mastheadBrush = new SolidBrush(Color.FromArgb(220, CL_STONE_DEEP)))
		{
			graphics.FillRectangle(mastheadBrush, 0, 0, WIN_W, mastFadeStart);
		}
		using (LinearGradientBrush mastheadFade = new LinearGradientBrush(
			new Rectangle(0, mastFadeStart, WIN_W, BANNER_H - mastFadeStart),
			Color.FromArgb(220, CL_STONE_DEEP),
			Color.FromArgb(0, CL_STONE_DEEP),
			LinearGradientMode.Vertical))
		{
			graphics.FillRectangle(mastheadFade,
				0, mastFadeStart, WIN_W, BANNER_H - mastFadeStart);
		}

		if (_sprLogo != null)
		{
			int logoW = Math.Min(_sprLogo.Width, 460);
			int logoH = _sprLogo.Height * logoW / _sprLogo.Width;
			if (logoH > BANNER_H - 8) // keep within the band
			{
				logoH = BANNER_H - 8;
				logoW = _sprLogo.Width * logoH / _sprLogo.Height;
			}
			graphics.DrawImage(_sprLogo, (WIN_W - logoW) / 2, 4, logoW, logoH);
		}
		else
		{
			TextRenderer.DrawText(graphics, "DIABLO II ARCHIPELAGO",
				new Font("Segoe UI", 26f, FontStyle.Bold),
				new Rectangle(0, 10, WIN_W, 50), CL_GOLD,
				TextFormatFlags.HorizontalCenter);
		}

		// Version label tucked into the masthead's bottom-right corner.
		TextRenderer.DrawText(graphics, $"v{LAUNCHER_VERSION}", _fVer,
			new Point(WIN_W - 78, BANNER_H - 18), CL_INK_MUTE);

		// Custom close button — gold X with hover lift.
		Color closeColor = _closeHover ? CL_EMBER_HI : CL_INK_SOFT;
		TextRenderer.DrawText(graphics, "X",
			new Font("Segoe UI", 13f, FontStyle.Bold),
			_closeRect, closeColor,
			TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);

		// Iron-edge dividers between regions.
		using (Pen ironPen = new Pen(Color.FromArgb(120, CL_IRON), 2f))
		{
			graphics.DrawLine(ironPen, 0, BANNER_H, WIN_W, BANNER_H);
			graphics.DrawLine(ironPen, 0, WIN_H - BOTTOM_H, WIN_W, WIN_H - BOTTOM_H);
		}

		// Sidebar background — sidebar.png stretched to fit the rail.
		Rectangle sidebarRect = new Rectangle(0, BANNER_H + 1, SIDEBAR_W, WIN_H - BANNER_H - BOTTOM_H - 1);
		if (_sprSidebar != null)
		{
			graphics.DrawImage(_sprSidebar, sidebarRect);
		}
		else
		{
			using SolidBrush sidebarBrush = new SolidBrush(CL_STONE);
			graphics.FillRectangle(sidebarBrush, sidebarRect);
		}

		// Vertical iron seam separating sidebar from content area.
		using (Pen seamPen = new Pen(CL_IRON_DARK))
		{
			graphics.DrawLine(seamPen, SIDEBAR_W, BANNER_H, SIDEBAR_W, WIN_H - BOTTOM_H);
		}

		// Sidebar nav items: arrow.png + gold text on active, ink on
		// hover, ink-soft on idle. Matches the design HTML's nav style:
		//   default: #b8a888 (ink-soft)
		//   hover:   #e8d9b8 (ink)
		//   active:  #d4a04a (gold) + arrow
		for (int i = 0; i < _menuLabels.Length; i++)
		{
			Rectangle rect = _menuRects[i];
			bool isActive = i < 6 && i == _activePage;
			bool isHover  = i == _menuHover;
			Font font = isActive ? _fMenuBold : _fMenu;
			Color textColor = isActive ? CL_GOLD
				: isHover  ? CL_INK
				: CL_INK_SOFT;

			// Active gets the iron arrowhead drawn 6px into the rect,
			// vertically centered. The PNG is 63×54; render at 14×12.
			int textX = rect.X + 28;
			if (isActive && _sprArrow != null)
			{
				int arrowH = 12;
				int arrowW = _sprArrow.Width * arrowH / _sprArrow.Height;
				graphics.DrawImage(_sprArrow,
					rect.X + 6, rect.Y + (rect.Height - arrowH) / 2,
					arrowW, arrowH);
			}
			else if (isActive)
			{
				// Fallback chevron if arrow.png is missing.
				TextRenderer.DrawText(graphics, ">",
					new Font("Segoe UI", 12f, FontStyle.Bold),
					new Point(rect.X + 8, rect.Y + 8), CL_GOLD);
			}

			TextRenderer.DrawText(graphics, _menuLabels[i].ToUpperInvariant(),
				font, new Point(textX, rect.Y + 8), textColor);
		}

		// Bottom action bar — bottom_bar.png stretched full-bleed.
		Rectangle bottomRect = new Rectangle(0, WIN_H - BOTTOM_H, WIN_W, BOTTOM_H);
		if (_sprBottomBar != null)
		{
			graphics.DrawImage(_sprBottomBar, bottomRect);
		}
		else
		{
			using SolidBrush bottomBrush = new SolidBrush(CL_STONE_DEEP);
			graphics.FillRectangle(bottomBrush, bottomRect);
		}

		// Content area frame (sits behind RichTextBox / settings panel).
		// content_area.png is 9-slice-style; we just stretch it across
		// the inner viewport. Parchment overlay is applied to the
		// RichTextBox itself via Image, not painted here.
		Rectangle contentRect = new Rectangle(
			SIDEBAR_W + 1,
			BANNER_H + 1,
			WIN_W - SIDEBAR_W - 1,
			WIN_H - BANNER_H - BOTTOM_H - 1);
		if (_sprContentArea != null)
		{
			// Inset slightly so the frame sits inside the iron seam.
			Rectangle frame = new Rectangle(
				contentRect.X + 6,
				contentRect.Y + 6,
				contentRect.Width - 12,
				contentRect.Height - 12);
			graphics.DrawImage(_sprContentArea, frame);
		}
		else if (_sprContentPanel != null)
		{
			graphics.DrawImage(_sprContentPanel, contentRect);
		}
	}

	private void OnMouseMove(object? sender, MouseEventArgs e)
	{
		bool flag = false;
		int num = -1;
		for (int i = 0; i < _menuRects.Length; i++)
		{
			if (_menuRects[i].Contains(e.Location))
			{
				num = i;
				break;
			}
		}
		if (num != _menuHover)
		{
			_menuHover = num;
			flag = true;
		}
		bool flag2 = _closeRect.Contains(e.Location);
		if (flag2 != _closeHover)
		{
			_closeHover = flag2;
			flag = true;
		}
		Cursor = ((_menuHover >= 0 || _closeHover) ? Cursors.Hand : Cursors.Default);
		if (_dragging)
		{
			base.Location = new Point(base.Location.X + e.X - _dragStart.X, base.Location.Y + e.Y - _dragStart.Y);
		}
		if (flag)
		{
			Invalidate();
		}
	}

	private void OnMouseDown(object? sender, MouseEventArgs e)
	{
		if (e.Button != MouseButtons.Left)
		{
			return;
		}
		if (_closeRect.Contains(e.Location))
		{
			Close();
			return;
		}
		for (int i = 0; i < _menuRects.Length; i++)
		{
			if (_menuRects[i].Contains(e.Location))
			{
				// 1.5.1 — menu order: News, Patch Notes, Guide, Settings,
				// Game Log, AP Bridge, Discord, Exit (8 items).
				switch (i)
				{
				case 0:  // News
					ShowPage(0);
					break;
				case 1:  // Patch Notes
					ShowPage(1);
					break;
				case 2:  // Guide  (NEW)
					ShowPage(2);
					break;
				case 3:  // Settings (was 2)
					ShowPage(3);
					break;
				case 4:  // Game Log (was 3)
					ShowPage(4);
					break;
				case 5:  // AP Bridge (was 4)
					ShowPage(5);
					break;
				case 6:  // Discord (was 5)
					OpenUrl("https://discord.com/channels/731205301247803413/1141778504380330065");
					break;
				case 7:  // Exit (was 6)
					Close();
					break;
				}
				return;
			}
		}
		if (e.Y < 115)
		{
			_dragging = true;
			_dragStart = e.Location;
		}
	}

	private void OnMainButton()
	{
		switch (_state)
		{
		case LState.NotInstalled:
		{
			string? value = ReadReg("D2Key");
			string value2 = ReadReg("D2XKey");
			if (!string.IsNullOrEmpty(value) && !string.IsNullOrEmpty(value2))
			{
				StartInstall();
			}
			else
			{
				ShowCDKeyDialog();
			}
			break;
		}
		case LState.UpToDate:
			LaunchGame();
			break;
		case LState.GameUpdate:
			StartUpdate();
			break;
		case LState.Downloading:
			if (_downloader != null)
			{
				_downloader.IsCancelled = true;
			}
			break;
		}
	}

	private void StartInstall()
	{
		// 1.7.1: on a fresh install _gameDir is empty. Prompt the user to
		// pick where the game should be installed. Default suggestion:
		// &lt;launcher_dir&gt;/game — user can accept or choose a different folder.
		if (string.IsNullOrEmpty(_gameDir))
		{
			if (!PromptForGameInstallDir()) return;
		}

		// Require original D2 path before installing.
		if (string.IsNullOrEmpty(_d2OriginalDir) || !ValidateD2OriginalDir(_d2OriginalDir))
		{
			if (!PromptForOriginalD2Dir()) return;
		}

		_state = LState.Downloading;
		UpdatePlayButton();
		_btnPlay.Text = "CANCEL";
		_btnPlay.Enabled = true;
		string text = _cmbVersion.SelectedItem?.ToString();
		string releaseTag = null;
		if (text != null && !text.StartsWith("Latest"))
		{
			releaseTag = text;
		}
		_downloader = new GameDownloader(Path.Combine(_gameDir));
		_downloader.OnProgress += delegate(int pct, string status)
		{
			Invoke(delegate
			{
				SetStatus(status);
			});
		};
		_downloader.OnComplete += delegate(bool success, string msg)
		{
			Invoke(delegate
			{
				SetStatus(msg);
				bool flag = Directory.Exists(_gameDir) && File.Exists(Path.Combine(_gameDir, "Archipelago", "version.dat"));
				// Presume NotInstalled until we confirm all original files copied.
				_state = (flag ? LState.UpToDate : LState.NotInstalled);
				UpdatePlayButton();

				bool allOriginalsCopied = false;
				List<string> originalMissing = new List<string>();
				if (success)
				{
					// Copy original D2 files from user's installation
					SetStatus("Copying original Diablo II files...");
					var copyResult = CopyOriginalD2Files();
					originalMissing = copyResult.missing;
					allOriginalsCopied = (originalMissing.Count == 0 && copyResult.copied == ORIGINAL_D2_FILES.Length);
					if (allOriginalsCopied)
					{
						SetStatus($"Original D2 files copied successfully ({copyResult.copied}/{ORIGINAL_D2_FILES.Length}).");
					}
					else
					{
						// Revert state: without the Blizzard files the game won't run.
						_state = LState.NotInstalled;
						UpdatePlayButton();
						SetStatus($"FAILED: {originalMissing.Count} original D2 file(s) could not be copied.");
					}
				}
				// 1.7.1: always run CopyApWorldFile — empty _apWorldDir now
				// falls back to &lt;launcher_dir&gt;/custom_worlds instead of skipping.
				if (success && allOriginalsCopied)
				{
					CopyApWorldFile();
				}
				if (success && allOriginalsCopied)
				{
					// Apply any settings the user saved before the game was installed.
					ApplyPendingD2GLSettings();
					MessageBox.Show("Game installed successfully!\n\nClick PLAY to start.", "Install Complete", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
				}
				else if (success && !allOriginalsCopied)
				{
					MessageBox.Show(
						"FAILED: the following files could not be copied from your Diablo II installation:\n\n" +
						string.Join("\n", originalMissing) + "\n\n" +
						"The game cannot run without these original Blizzard files. Please point the launcher at a valid Classic Diablo II + LoD install and retry.",
						"Install Incomplete", MessageBoxButtons.OK, MessageBoxIcon.Hand);
				}
			});
		};
		_downloader.InstallFromReleaseAsync(releaseTag);
	}

	private void StartUpdate()
	{
		_state = LState.Downloading;
		UpdatePlayButton();
		_btnPlay.Text = "CANCEL";
		_btnPlay.Enabled = true;
		_downloader = new GameDownloader(Path.Combine(_gameDir));
		_downloader.OnProgress += delegate(int pct, string status)
		{
			Invoke(delegate
			{
				SetStatus(status);
			});
		};
		_downloader.OnComplete += delegate(bool success, string msg)
		{
			Invoke(delegate
			{
				SetStatus(msg);
				_state = LState.UpToDate;
				UpdatePlayButton();
				if (success)
				{
					// Ensure original D2 files are present after update.
					// An update to an existing install should already have them,
					// but we re-run the copy to heal any missing file.
					var copyResult = CopyOriginalD2Files();
					if (copyResult.missing.Count > 0)
					{
						SetStatus($"Update OK, but {copyResult.missing.Count} original D2 file(s) still missing. Reinstall may be needed.");
					}
				}
				// 1.7.1: always run CopyApWorldFile — empty _apWorldDir now
				// falls back to &lt;launcher_dir&gt;/custom_worlds instead of skipping.
				if (success)
				{
					CopyApWorldFile();
				}
			});
		};
		_downloader.UpdateFromManifestAsync();
	}

	private void UpdatePlayButton()
	{
		if (_btnPlay != null)
		{
			Button btnPlay = _btnPlay;
			btnPlay.Text = _state switch
			{
				LState.NotInstalled => "INSTALL GAME",
				LState.UpToDate => "PLAY",
				LState.GameUpdate => "UPDATE GAME",
				LState.Downloading => "Downloading...",
				_ => "PLAY",
			};
			_btnPlay.Enabled = _state != LState.Downloading;
		}
	}

	/// <summary>
	/// 1.8.5 fix (R11) — Compute LState from the version dropdown
	/// selection rather than always comparing remote-latest vs
	/// installed. Picking a previously-installed older build now
	/// shows PLAY instead of UPDATE GAME.
	///
	/// State table:
	///   no version.dat                        -> NotInstalled (INSTALL GAME)
	///   selected matches installed            -> UpToDate     (PLAY)
	///   selected differs from installed       -> GameUpdate   (UPDATE GAME)
	///   "Latest" with no remote yet           -> UpToDate     (PLAY)
	///
	/// Called from CheckUpdate after the dropdown is repopulated and
	/// from _cmbVersion.SelectedIndexChanged whenever the user picks a
	/// different version.
	/// </summary>
	private void RecomputeStateForSelection()
	{
		// Don't clobber an active download: if the user is mid-install
		// the button should keep saying "Downloading..." even if the
		// dropdown change handler fires.
		if (_state == LState.Downloading) return;

		bool installed = !string.IsNullOrEmpty(_gameDir)
			&& Directory.Exists(_gameDir)
			&& File.Exists(Path.Combine(_gameDir, "Archipelago", "version.dat"));

		string installedVer = "";
		if (installed)
		{
			try
			{
				installedVer = File.ReadAllText(Path.Combine(_gameDir, "Archipelago", "version.dat")).Trim();
			}
			catch
			{
				installedVer = GAME_VERSION;
			}
		}

		// Resolve the dropdown selection to a concrete version string.
		// Items beginning with "Latest" map to _remoteVersion (the most
		// recent GitHub release we found during CheckUpdate).
		string sel = _cmbVersion?.SelectedItem?.ToString() ?? "Latest";
		string targetVer = sel.StartsWith("Latest", StringComparison.Ordinal)
			? _remoteVersion
			: sel;

		// Refresh the version label so the user sees what's installed
		// and what they're about to download (or play).
		string installedDisplay = installed
			? installedVer.Replace("-", " ").Replace("_", ".")
			: "Not installed";
		string targetDisplay = (targetVer ?? "").Replace("-", " ").Replace("_", ".");
		_versionText = "Installed: " + installedDisplay + "  |  Selected: " + targetDisplay;
		if (_lblVersion != null) _lblVersion.Text = _versionText;

		if (!installed)
		{
			_state = LState.NotInstalled;
		}
		// Empty target — no remote info AND no specific pick — keep the
		// user playable if they have an install on disk.
		else if (string.IsNullOrEmpty(targetVer))
		{
			_state = LState.UpToDate;
		}
		// Selected exactly matches what's on disk → PLAY.
		else if (CompareVersions(targetVer, installedVer) == 0)
		{
			_state = LState.UpToDate;
		}
		// Anything else (newer OR older) means "this dropdown choice is
		// not currently installed; click to download it". Reuse the
		// GameUpdate state — the click handler routes that to
		// StartUpdate() which downloads the selected release tag.
		else
		{
			_state = LState.GameUpdate;
		}

		UpdatePlayButton();
		SetStatus(_state switch
		{
			LState.NotInstalled => "Click INSTALL GAME to begin",
			LState.GameUpdate => "Click UPDATE GAME to switch to " + targetDisplay
				+ " (currently installed: " + installedDisplay + ")",
			_ => "Ready to play " + installedDisplay,
		});
	}

	// 1.5.1 — Pre-launch verification wrapper. Before actually starting
	// Game.exe we read game_manifest.json and confirm every non-Blizzard
	// file exists with the size the manifest claims. If anything is off
	// we redownload the bad files and retry. After 3 consecutive failures
	// we surface a warning to the user (antivirus / SmartScreen / firewall
	// is the usual culprit when downloads keep getting silently truncated
	// or the files vanish moments after writing).
	private async void LaunchGame()
	{
		const int MAX_VERIFY_ATTEMPTS = 3;

		// Quick game-folder sanity check — a missing exe means we can't
		// even attempt a launch, so we surface that error directly without
		// running the manifest verifier (which would just report 100% of
		// files missing and then loop trying to repair).
		string launcherExe = Path.Combine(_gameDir, "D2Arch_Launcher.exe");
		string fallbackExe = Path.Combine(_gameDir, "Game.exe");
		if (!File.Exists(launcherExe) && !File.Exists(fallbackExe))
		{
			MessageBox.Show("Game not found at:\n" + _gameDir, "Error", MessageBoxButtons.OK, MessageBoxIcon.Hand);
			return;
		}

		_btnPlay.Enabled = false;
		string originalText = _btnPlay.Text;
		_btnPlay.Text = "VERIFYING...";

		GameDownloader verifier = new GameDownloader(_gameDir);
		verifier.OnProgress += delegate(int pct, string status)
		{
			try { Invoke((Action)(() => SetStatus(status))); } catch { }
		};

		try
		{
			for (int attempt = 1; attempt <= MAX_VERIFY_ATTEMPTS; attempt++)
			{
				SetStatus($"Verifying installation (attempt {attempt}/{MAX_VERIFY_ATTEMPTS})...");
				GameDownloader.VerificationResult vr = await verifier.VerifyInstallationAsync();

				if (vr.IsValid)
				{
					if (attempt > 1)
					{
						SetStatus($"Repair successful after {attempt - 1} attempt(s). Launching...");
					}
					else
					{
						SetStatus("Verification OK. Launching...");
					}
					_consecutiveLaunchVerifyFailures = 0;
					_btnPlay.Text = originalText;
					_btnPlay.Enabled = true;
					LaunchGameInternal();
					return;
				}

				if (vr.ManifestUnavailable)
				{
					// Cannot verify — let the launch proceed instead of
					// blocking offline users. (VerifyInstallationAsync already
					// flips IsValid=true in this case, so we shouldn't reach
					// this branch, but the guard documents intent.)
					SetStatus("Manifest unavailable; skipping verification.");
					_btnPlay.Text = originalText;
					_btnPlay.Enabled = true;
					LaunchGameInternal();
					return;
				}

				_consecutiveLaunchVerifyFailures++;
				int badCount = vr.MissingOrCorrupt.Count;
				SetStatus($"Found {badCount} bad/missing files. Repairing (attempt {attempt}/{MAX_VERIFY_ATTEMPTS})...");
				_btnPlay.Text = "REPAIRING...";

				bool repairOk = await verifier.RepairFilesAsync(vr.MissingOrCorrupt);
				if (!repairOk)
				{
					SetStatus($"Repair attempt {attempt} could not download all files; will re-verify...");
				}
				// Loop continues — next iteration re-verifies; if all files
				// now match we exit via the IsValid branch above.
			}

			// Fell through MAX_VERIFY_ATTEMPTS times — surface the warning.
			ShowAntivirusWarning();
		}
		catch (Exception ex)
		{
			SetStatus("Verification error: " + ex.Message);
			MessageBox.Show(
				"The pre-launch installation check failed unexpectedly:\n\n" +
				ex.Message +
				"\n\nYou can try clicking Play again, or use UPDATE GAME to redownload from scratch.",
				"Verification error",
				MessageBoxButtons.OK,
				MessageBoxIcon.Warning);
		}
		finally
		{
			_btnPlay.Text = originalText;
			_btnPlay.Enabled = true;
		}
	}

	// 1.5.1 — Friendly warning when 3 consecutive verify-repair cycles
	// failed in the same session. AV/SmartScreen blocking the .exe drops,
	// firewall denying raw.githubusercontent, and quota'd Wi-Fi all surface
	// the same way: files keep arriving truncated or never. Telling the
	// user "try disabling AV" up front saves a lot of Discord questions.
	private void ShowAntivirusWarning()
	{
		_btnPlay.Text = "PLAY";
		_btnPlay.Enabled = true;
		SetStatus("Verification failed 3 times. See popup for help.");
		MessageBox.Show(
			"The launcher tried 3 times to download missing or corrupt files but they keep failing.\n\n" +
			"This is almost always caused by one of:\n" +
			"  • Antivirus software (Windows Defender, etc.) deleting files after download\n" +
			"  • SmartScreen / Real-time Protection quarantining the .exe payloads\n" +
			"  • A firewall or proxy blocking raw.githubusercontent.com\n\n" +
			"What to try:\n" +
			"  1. Temporarily disable your antivirus and click PLAY again.\n" +
			"  2. Add the launcher's game folder to your antivirus exclusion list.\n" +
			"  3. Confirm GitHub is reachable in your browser.\n" +
			"  4. Use UPDATE GAME to redownload the full package.\n\n" +
			"If the problem still persists, please post a Game Log excerpt on the\n" +
			"Discord (link in the sidebar) and a maintainer will help.",
			"Files keep failing to download",
			MessageBoxButtons.OK,
			MessageBoxIcon.Warning);
	}

	private void LaunchGameInternal()
	{
		string text = Path.Combine(_gameDir, "D2Arch_Launcher.exe");
		string text2 = Path.Combine(_gameDir, "Game.exe");
		string text3 = Path.Combine(_gameDir, "d2arch_log.txt");
		string text4 = File.Exists(text) ? text : text2;
		if (!File.Exists(text4))
		{
			MessageBox.Show("Game not found at:\n" + _gameDir, "Error", MessageBoxButtons.OK, MessageBoxIcon.Hand);
			return;
		}
		try
		{
			File.Delete(text3);
		}
		catch
		{
		}
		_txtGameLog.Text = "Starting game...\n";
		_txtBridgeLog.Text = "Waiting for AP Bridge...\n";
		// WindowStyle = Hidden intentionally omitted — it has no effect on GUI
		// applications (only console ones), and setting it requires UseShellExecute.
		ProcessStartInfo startInfo = new ProcessStartInfo
		{
			FileName = text4,
			Arguments = "-3dfx -direct -txt",
			WorkingDirectory = _gameDir,
			UseShellExecute = false,
			CreateNoWindow = true,
			RedirectStandardOutput = true,
			RedirectStandardError = true
		};
		// 1.8.0 — tell D2.Detours where to find our patch/ folder so DC6 overrides
		// (custom splash, main menu background, etc.) actually load. START.bat set
		// this previously; launcher path was missing it.
		startInfo.EnvironmentVariables["DIABLO2_PATCH"] = System.IO.Path.Combine(_gameDir, "patch");
		try
		{
			Process gameProcess = Process.Start(startInfo);
			if (gameProcess != null)
			{
				gameProcess.OutputDataReceived += (s, e) =>
				{
					if (!string.IsNullOrEmpty(e.Data))
					{
						try { Invoke((Action)(() => AppendLog(_txtGameLog, e.Data + "\n"))); } catch { }
					}
				};
				gameProcess.ErrorDataReceived += (s, e) =>
				{
					if (!string.IsNullOrEmpty(e.Data))
					{
						try { Invoke((Action)(() => AppendLog(_txtGameLog, "[ERR] " + e.Data + "\n"))); } catch { }
					}
				};
				gameProcess.BeginOutputReadLine();
				gameProcess.BeginErrorReadLine();
			}
			SetStatus("Game launched! Check Game Log tab for output.");
			StartLogWatcher(text3);
			ShowPage(3);
		}
		catch (Exception ex)
		{
			MessageBox.Show("Failed to launch game:\n" + ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Hand);
		}
	}

	private void StartLogWatcher(string logPath)
	{
		_logWatchCts?.Cancel();
		_logWatchCts = new CancellationTokenSource();
		CancellationToken ct = _logWatchCts.Token;
		Task.Run(async delegate
		{
			for (int i = 0; i < 30; i++)
			{
				if (File.Exists(logPath))
				{
					break;
				}
				if (ct.IsCancellationRequested)
				{
					break;
				}
				await Task.Delay(1000, ct);
			}
			if (File.Exists(logPath) && !ct.IsCancellationRequested)
			{
				long lastPos = 0L;
				while (!ct.IsCancellationRequested)
				{
					try
					{
						FileInfo fi = new FileInfo(logPath);
						if (fi.Length > lastPos)
						{
							using FileStream fs = new FileStream(logPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
							fs.Seek(lastPos, SeekOrigin.Begin);
							using StreamReader sr = new StreamReader(fs);
							string newText = await sr.ReadToEndAsync(ct);
							lastPos = fi.Length;
							if (!string.IsNullOrEmpty(newText))
							{
								Invoke(delegate
								{
									AppendLog(_txtGameLog, newText);
								});
							}
						}
					}
					catch
					{
					}
					try
					{
						string text = Path.Combine(_gameDir, "Archipelago", "ap_bridge_log.txt");
						if (File.Exists(text))
						{
							new FileInfo(text);
							string bridgeText = File.ReadAllText(text);
							if (bridgeText.Length > 0)
							{
								Invoke(delegate
								{
									if (_txtBridgeLog.Text != bridgeText)
									{
										_txtBridgeLog.Text = bridgeText;
									}
								});
							}
						}
					}
					catch
					{
					}
					await Task.Delay(500, ct);
				}
			}
		}, ct);
	}

	private void AppendLog(RichTextBox rtb, string text)
	{
		rtb.AppendText(text);
		rtb.SelectionStart = rtb.TextLength;
		rtb.ScrollToCaret();
		if (rtb.TextLength > 60000)
		{
			rtb.Text = rtb.Text.Substring(rtb.TextLength - 50000);
			rtb.SelectionStart = rtb.TextLength;
			rtb.ScrollToCaret();
		}
	}

	private static bool ValidateCDKey(string rawKey, out string error)
	{
		error = "";
		string text = rawKey.Replace("-", "").Replace(" ", "").Trim()
			.ToUpper();
		if (text.Length == 16)
		{
			string text2 = text;
			foreach (char c in text2)
			{
				if (c < '0' || c > '9')
				{
					error = "16-digit keys must contain only numbers (0-9).";
					return false;
				}
			}
			int num = 3;
			for (int j = 0; j < 15; j++)
			{
				int num2 = text[j] - 48;
				num += num2 ^ (num * 2);
			}
			if (num % 10 != text[15] - 48)
			{
				error = "Invalid CD key (checksum failed). Please check for typos.";
				return false;
			}
			return true;
		}
		if (text.Length == 26)
		{
			string text2 = text;
			foreach (char value in text2)
			{
				if (!"246789BCDEFGHJKMNPRTVWXYZ".Contains(value))
				{
					error = $"Invalid character '{value}' in 26-digit key.\nValid characters: {"246789BCDEFGHJKMNPRTVWXYZ"}";
					return false;
				}
			}
			return true;
		}
		error = "CD key must be 16 digits (retail CD) or 26 characters (digital).";
		return false;
	}

	private void ShowCDKeyDialog()
	{
		Form dlg = new Form
		{
			Text = "Enter CD Keys",
			ClientSize = new Size(450, 290),
			FormBorderStyle = FormBorderStyle.FixedDialog,
			StartPosition = FormStartPosition.CenterParent,
			BackColor = Color.FromArgb(30, 25, 18),
			MaximizeBox = false,
			MinimizeBox = false
		};
		try
		{
			Label label = new Label
			{
				Text = "Enter your CD keys to verify ownership",
				Location = new Point(20, 15),
				AutoSize = true,
				ForeColor = Color.FromArgb(220, 190, 100),
				Font = new Font("Segoe UI", 12f, FontStyle.Bold)
			};
			Label label2 = new Label
			{
				Text = "Diablo II CD Key:",
				Location = new Point(20, 60),
				AutoSize = true,
				ForeColor = Color.FromArgb(165, 148, 118),
				Font = new Font("Segoe UI", 10f)
			};
			TextBox txt1 = new TextBox
			{
				Location = new Point(20, 85),
				Size = new Size(410, 30),
				Font = new Font("Consolas", 13f),
				CharacterCasing = CharacterCasing.Upper,
				MaxLength = 30
			};
			Label label3 = new Label
			{
				Text = "Lord of Destruction CD Key:",
				Location = new Point(20, 128),
				AutoSize = true,
				ForeColor = Color.FromArgb(165, 148, 118),
				Font = new Font("Segoe UI", 10f)
			};
			TextBox txt2 = new TextBox
			{
				Location = new Point(20, 153),
				Size = new Size(410, 30),
				Font = new Font("Consolas", 13f),
				CharacterCasing = CharacterCasing.Upper,
				MaxLength = 30
			};
			Label label4 = new Label
			{
				Text = "Accepts 16-digit (CD) and 26-digit (digital) keys",
				Location = new Point(20, 195),
				AutoSize = true,
				ForeColor = Color.FromArgb(100, 88, 68),
				Font = new Font("Segoe UI", 9f)
			};
			Button button = new Button
			{
				Text = "Install",
				Location = new Point(240, 235),
				Size = new Size(90, 40),
				FlatStyle = FlatStyle.Flat,
				BackColor = Color.FromArgb(55, 45, 30),
				ForeColor = Color.FromArgb(225, 200, 110),
				Font = new Font("Segoe UI", 11f, FontStyle.Bold)
			};
			button.FlatAppearance.BorderColor = Color.FromArgb(130, 108, 58);
			Button button2 = new Button
			{
				Text = "Cancel",
				Location = new Point(340, 235),
				Size = new Size(90, 40),
				FlatStyle = FlatStyle.Flat,
				BackColor = Color.FromArgb(40, 33, 22),
				ForeColor = Color.FromArgb(150, 130, 100),
				Font = new Font("Segoe UI", 10f)
			};
			button2.FlatAppearance.BorderColor = Color.FromArgb(70, 58, 38);
			button.Click += delegate
			{
				string text = txt1.Text;
				string text2 = txt2.Text;
				string error2;
				if (!ValidateCDKey(text, out string error))
				{
					MessageBox.Show("Invalid Diablo II CD Key:\n" + error, "Invalid Key", MessageBoxButtons.OK, MessageBoxIcon.Hand);
				}
				else if (!ValidateCDKey(text2, out error2))
				{
					MessageBox.Show("Invalid Lord of Destruction CD Key:\n" + error2, "Invalid Key", MessageBoxButtons.OK, MessageBoxIcon.Hand);
				}
				else
				{
					string value = text.Replace("-", "").Replace(" ", "").Trim()
						.ToUpper();
					string value2 = text2.Replace("-", "").Replace(" ", "").Trim()
						.ToUpper();
					WriteReg("D2Key", value);
					WriteReg("D2XKey", value2);
					dlg.DialogResult = DialogResult.OK;
				}
			};
			button2.Click += delegate
			{
				dlg.DialogResult = DialogResult.Cancel;
			};
			dlg.Controls.AddRange(new Control[8] { label, label2, txt1, label3, txt2, label4, button, button2 });
			dlg.AcceptButton = button;
			dlg.CancelButton = button2;
			if (dlg.ShowDialog(this) == DialogResult.OK)
			{
				SetStatus("CD keys saved. Starting installation...");
				StartInstall();
			}
		}
		finally
		{
			if (dlg != null)
			{
				((IDisposable)dlg).Dispose();
			}
		}
	}

	private async Task CheckUpdatesAsync()
	{
		SetStatus("Checking for updates...");
		try
		{
			using JsonDocument doc = JsonDocument.Parse(await _http.GetStringAsync("https://api.github.com/repos/solida1987/Diablo-II-Archipelago/releases/latest"));
			JsonElement root = doc.RootElement;
			if (root.TryGetProperty("tag_name", out var value))
			{
				_remoteVersion = value.GetString() ?? "";
			}
			if (root.TryGetProperty("body", out var value2))
			{
				_patchNotes = value2.GetString() ?? "No patch notes.";
			}
			try
			{
				_newsText = await _http.GetStringAsync("https://raw.githubusercontent.com/solida1987/Diablo-II-Archipelago/main/news.txt");
			}
			catch
			{
				_newsText = "";
			}
			// 1.5.1 — fetch the Settings & Logic Guide from raw.githubusercontent.
			// Local fallback: launcher dir / Settings_Guide.md (shipped in the
			// launcher_package.zip so first-launch users have something to read
			// even if they're offline). Final fallback: a stub message that
			// ShowPage(2) substitutes when _settingsGuide is empty.
			try
			{
				_settingsGuide = await _http.GetStringAsync("https://raw.githubusercontent.com/solida1987/Diablo-II-Archipelago/main/Settings_Guide.md");
			}
			catch
			{
				try
				{
					string localGuide = Path.Combine(_launcherDir, "Settings_Guide.md");
					if (File.Exists(localGuide))
					{
						_settingsGuide = File.ReadAllText(localGuide);
					}
					else
					{
						_settingsGuide = "";
					}
				}
				catch
				{
					_settingsGuide = "";
				}
			}
			// If the guide page is currently visible, refresh it now that
			// the fetch finished so the user doesn't keep staring at the
			// "loading" placeholder.
			if (_activePage == 2)
			{
				try
				{
					BeginInvoke(new Action(() => ShowPage(2)));
				}
				catch
				{
					// Form may not be fully initialized yet.
				}
			}
			string launcherZipUrl = null;
			if (root.TryGetProperty("assets", out var value3))
			{
				foreach (JsonElement item in value3.EnumerateArray())
				{
					if ((item.GetProperty("name").GetString() ?? "") == "launcher_package.zip")
					{
						launcherZipUrl = item.GetProperty("browser_download_url").GetString();
						break;
					}
				}
			}
			// Determine the currently-installed launcher version. If a previous
			// self-update wrote launcher_version.dat we trust that file (so a user
			// on 1.4.0 who just installed 1.5.0 doesn't get prompted again until
			// 1.6.0 ships). Fall back to the compiled-in constant for fresh installs.
			string installedLauncherVer = LAUNCHER_VERSION;
			string launcherVersionDatPath = Path.Combine(_launcherDir, "launcher_version.dat");
			try
			{
				if (File.Exists(launcherVersionDatPath))
				{
					string fromDat = File.ReadAllText(launcherVersionDatPath).Trim();
					if (!string.IsNullOrEmpty(fromDat))
					{
						installedLauncherVer = fromDat;
					}
				}
			}
			catch
			{
			}

			// launcher_version.txt on the default branch now supports two lines:
			//   line 1: version (e.g. "1.4.0")
			//   line 2: SHA-256 of launcher_package.zip (hex, 64 chars)
			// Legacy files with only the version string are accepted for backwards
			// compatibility (1.3.0 -> 1.4.0 migration) but we log a warning and
			// skip the SHA integrity check in that case.
			string? remoteLauncherVer = null;
			string? remoteLauncherSha = null;
			try
			{
				string raw = (await _http.GetStringAsync("https://raw.githubusercontent.com/solida1987/Diablo-II-Archipelago/main/launcher_version.txt")).Replace("\r", "");
				string[] parts = raw.Split('\n', StringSplitOptions.RemoveEmptyEntries);
				if (parts.Length >= 1)
				{
					remoteLauncherVer = parts[0].Trim();
				}
				if (parts.Length >= 2)
				{
					remoteLauncherSha = parts[1].Trim().ToLowerInvariant();
				}
			}
			catch
			{
			}
			// 1.7.1 fix: only prompt for update when remote is STRICTLY NEWER
			// than what's installed. A local dev build that's newer than the
			// public release should NOT ask to downgrade, and an identical
			// version should also not prompt.
			bool updateAvailable = !string.IsNullOrEmpty(remoteLauncherVer)
				&& CompareVersions(remoteLauncherVer, installedLauncherVer) > 0;
			if (updateAvailable && launcherZipUrl != null)
			{
				string capturedZipUrl = launcherZipUrl;
				string? capturedRemoteVer = remoteLauncherVer;
				string? capturedSha = remoteLauncherSha;
				string capturedInstalled = installedLauncherVer;
				Invoke(delegate
				{
					if (MessageBox.Show($"A new launcher version is available!\n\nCurrent: v{capturedInstalled}\nAvailable: v{capturedRemoteVer}\n\n" + "Update now? The launcher will restart.", "Launcher Update", MessageBoxButtons.YesNo, MessageBoxIcon.Asterisk) == DialogResult.Yes)
					{
						_ = SelfUpdateAsync(capturedZipUrl, capturedRemoteVer, capturedSha);
					}
				});
			}
			try
			{
				using JsonDocument jsonDocument = JsonDocument.Parse(await _http.GetStringAsync("https://api.github.com/repos/solida1987/Diablo-II-Archipelago/releases"));
				_availableVersions.Clear();
				foreach (JsonElement item2 in jsonDocument.RootElement.EnumerateArray())
				{
					if (item2.TryGetProperty("tag_name", out var value4))
					{
						string text = value4.GetString();
						if (!string.IsNullOrEmpty(text))
						{
							_availableVersions.Add(text);
						}
					}
				}
			}
			catch
			{
			}
			Invoke(delegate
			{
				// Repopulate the dropdown FIRST so RecomputeStateForSelection
				// has the up-to-date list to consult. SelectedIndex=0 picks
				// "Latest" — the default state — and that index assignment
				// fires SelectedIndexChanged, which calls
				// RecomputeStateForSelection() and refreshes the button.
				_cmbVersion.Items.Clear();
				_cmbVersion.Items.Add("Latest" + ((_remoteVersion != "") ? (" (" + _remoteVersion + ")") : ""));
				foreach (string availableVersion in _availableVersions)
				{
					if (availableVersion != _remoteVersion)
					{
						_cmbVersion.Items.Add(availableVersion);
					}
				}
				_cmbVersion.SelectedIndex = 0;
				ShowPage(_activePage);
				// 1.8.5 fix (R11): RecomputeStateForSelection reads the
				// dropdown selection (now "Latest") plus the installed
				// version.dat, then sets _state and refreshes the button +
				// status text. Replaces the older logic that hard-coded
				// the "remote vs installed" comparison and ignored the
				// dropdown.
				RecomputeStateForSelection();
			});
		}
		catch
		{
			Invoke(delegate
			{
				SetStatus("Could not check updates");
			});
		}
	}

	private void LoadLauncherConfig()
	{
		if (!File.Exists(LauncherConfigPath))
		{
			return;
		}
		try
		{
			string[] array = File.ReadAllLines(LauncherConfigPath);
			bool inPendingBlock = false;
			for (int i = 0; i < array.Length; i++)
			{
				string text = array[i].Trim();
				// Skip everything inside [pending_d2gl] — those values are not
				// top-level launcher settings and must not leak into this switch.
				if (text.StartsWith("[", StringComparison.Ordinal) && text.EndsWith("]", StringComparison.Ordinal))
				{
					inPendingBlock = text.Equals("[pending_d2gl]", StringComparison.OrdinalIgnoreCase);
					continue;
				}
				if (inPendingBlock)
				{
					continue;
				}
				if (text.StartsWith(";") || !text.Contains('='))
				{
					continue;
				}
				string[] array2 = text.Split('=', 2);
				if (array2.Length != 2)
				{
					continue;
				}
				string text2 = array2[0].Trim();
				string text3 = array2[1].Trim();
				switch (text2)
				{
				case "game_dir":
					if (Directory.Exists(text3) || text3.Length > 2)
					{
						_gameDir = text3;
					}
					break;
				case "apworld_dir":
					if (text3.Length > 2)
					{
						_apWorldDir = text3;
					}
					break;
				case "d2_original_dir":
					if (text3.Length > 2)
					{
						_d2OriginalDir = text3;
					}
					break;
				case "last_page":
				{
					// 1.5.1 — page range expanded from 0..4 to 0..5 with the
					// new Guide page slotted at index 2. Older configs with
					// last_page=2 (Settings) will now land on Guide; that's a
					// one-time adjustment, accepted as a UX improvement (Guide
					// is more useful as a default than the config panel).
					if (int.TryParse(text3, out var result) && result >= 0 && result <= 5)
					{
						_activePage = result;
					}
					break;
				}
				case "window_x":
					if (int.TryParse(text3, out int wx))
					{
						_savedWindowX = wx;
					}
					break;
				case "window_y":
					if (int.TryParse(text3, out int wy))
					{
						_savedWindowY = wy;
					}
					break;
				}
			}
		}
		catch
		{
		}
	}

	private void SaveLauncherConfig()
	{
		try
		{
			if (_txtApWorldPath != null && _txtApWorldPath.Text.Length > 2)
			{
				_apWorldDir = _txtApWorldPath.Text;
			}
			if (_txtD2OriginalPath != null && _txtD2OriginalPath.Text.Length > 2)
			{
				_d2OriginalDir = _txtD2OriginalPath.Text;
			}

			// Preserve any [pending_d2gl] section across saves — it stores the
			// user's settings when the game isn't installed yet and must not be
			// clobbered when other config values are written.
			List<string> pendingBlock = ReadPendingD2GLBlock();

			// Preserve window position (fix #18). Location is updated by
			// OnFormClosing before SaveLauncherConfig is called, so base.Location
			// reflects the final window position on shutdown. For mid-session
			// saves it captures the current position which is also fine.
			string windowX = base.Location.X.ToString();
			string windowY = base.Location.Y.ToString();

			List<string> contents = new List<string>
			{
				"; Diablo II Archipelago Launcher Config",
				"; This file is auto-generated. Edit at your own risk.",
				"",
				"game_dir=" + _gameDir,
				"apworld_dir=" + _apWorldDir,
				"d2_original_dir=" + _d2OriginalDir,
				$"last_page={_activePage}",
				$"window_x={windowX}",
				$"window_y={windowY}"
			};
			if (pendingBlock.Count > 0)
			{
				contents.Add("");
				contents.AddRange(pendingBlock);
			}
			File.WriteAllLines(LauncherConfigPath, contents);
		}
		catch
		{
		}
	}

	// Returns the verbatim text of the [pending_d2gl] section (including header)
	// from launcher_config.ini, or an empty list when no section exists.
	private List<string> ReadPendingD2GLBlock()
	{
		List<string> block = new List<string>();
		if (!File.Exists(LauncherConfigPath))
		{
			return block;
		}
		try
		{
			bool inPending = false;
			foreach (string line in File.ReadAllLines(LauncherConfigPath))
			{
				string trimmed = line.Trim();
				if (trimmed.StartsWith("[pending_d2gl]", StringComparison.OrdinalIgnoreCase))
				{
					inPending = true;
					block.Add("[pending_d2gl]");
					continue;
				}
				if (inPending && trimmed.StartsWith("[") && trimmed.EndsWith("]"))
				{
					break;
				}
				if (inPending)
				{
					block.Add(line);
				}
			}
		}
		catch { }
		return block;
	}

	/// <summary>
	/// 1.7.1: on a first-time install, prompt the user to pick the game
	/// installation directory. Default suggestion: &lt;launcher_dir&gt;/game.
	/// Returns false if the user cancels.
	/// </summary>
	private bool PromptForGameInstallDir()
	{
		string defaultGame = Path.Combine(_launcherDir, "game");
		var dlg = new FolderBrowserDialog
		{
			Description = "Where do you want to install Diablo II Archipelago?\n" +
			              "The default is a 'game' folder next to this launcher.",
			UseDescriptionForTitle = true,
			SelectedPath = _launcherDir,
			ShowNewFolderButton = true,
		};
		try
		{
			DialogResult r = dlg.ShowDialog();
			if (r != DialogResult.OK)
			{
				// On cancel, fall back to default next-to-launcher so the user
				// gets a working install without being interrogated every time.
				_gameDir = defaultGame;
			}
			else
			{
				string picked = dlg.SelectedPath;
				// If the user picked the launcher's own folder, put the game
				// in a 'game' subfolder so the launcher binaries aren't mixed
				// with game files.
				string normalized = Path.GetFullPath(picked).TrimEnd('\\', '/');
				string launcherNormalized = Path.GetFullPath(_launcherDir).TrimEnd('\\', '/');
				if (string.Equals(normalized, launcherNormalized, StringComparison.OrdinalIgnoreCase))
				{
					_gameDir = defaultGame;
				}
				else
				{
					_gameDir = picked;
				}
			}
			if (_txtGamePath != null) _txtGamePath.Text = _gameDir;
			SaveLauncherConfig();
			return true;
		}
		finally
		{
			dlg.Dispose();
		}
	}

	/// <summary>
	/// 1.7.1: prompt the user for the original Diablo II install location.
	/// Auto-detects via registry / common paths and offers the result as a
	/// one-click suggestion; user can accept, browse to a different folder,
	/// or cancel the install entirely.
	/// </summary>
	private bool PromptForOriginalD2Dir()
	{
		string suggestion = AutoDetectD2Install();
		bool hasSuggestion = !string.IsNullOrEmpty(suggestion) && ValidateD2OriginalDir(suggestion);

		if (hasSuggestion)
		{
			DialogResult choice = MessageBox.Show(
				$"We found a Classic Diablo II install at:\n\n{suggestion}\n\n" +
				"Use this installation?\n\n" +
				"[Yes]   use the detected path\n" +
				"[No]    browse to a different folder\n" +
				"[Cancel] abort install",
				"Diablo II Installation", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
			if (choice == DialogResult.Cancel) return false;
			if (choice == DialogResult.Yes)
			{
				_d2OriginalDir = suggestion;
				if (_txtD2OriginalPath != null) _txtD2OriginalPath.Text = _d2OriginalDir;
				SaveLauncherConfig();
				return true;
			}
			// Fall through to manual browse.
		}

		var dlg = new FolderBrowserDialog
		{
			Description = "Select your original Diablo II installation folder\n" +
			              "(the folder that contains Game.exe and all .mpq files)",
			UseDescriptionForTitle = true,
		};
		try
		{
			DialogResult r = dlg.ShowDialog();
			if (r != DialogResult.OK)
			{
				MessageBox.Show(
					"Install cancelled.\n\n" +
					"Please point to your Classic Diablo II + LoD install folder " +
					"(the one containing Game.exe and d2data.mpq) to continue.",
					"Install Cancelled", MessageBoxButtons.OK, MessageBoxIcon.Information);
				return false;
			}
			var detail = ValidateD2OriginalDirDetailed(dlg.SelectedPath);
			if (!detail.ok)
			{
				MessageBox.Show(
					"A valid Classic Diablo II installation is required.\n\n" +
					detail.reason + "\n\n" +
					"The folder must contain all 11 original Blizzard files:\n" +
					string.Join(", ", ORIGINAL_D2_FILES) + "\n\n" +
					"Please install Classic Diablo II + LoD, then try again.",
					"Original Game Required", MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return false;
			}
			_d2OriginalDir = dlg.SelectedPath;
			if (_txtD2OriginalPath != null) _txtD2OriginalPath.Text = _d2OriginalDir;
			SaveLauncherConfig();
			return true;
		}
		finally
		{
			dlg.Dispose();
		}
	}

	/// <summary>
	/// 1.7.1: resolve the AP custom-worlds destination. If the user has
	/// set one in Settings, use it; otherwise default to
	/// &lt;launcher_dir&gt;/custom_worlds next to the launcher so the apworld
	/// file gets placed somewhere predictable without extra configuration.
	/// </summary>
	private string ResolveApWorldDestination()
	{
		if (!string.IsNullOrEmpty(_apWorldDir)) return _apWorldDir;
		return Path.Combine(_launcherDir, "custom_worlds");
	}

	private void CopyApWorldFile()
	{
		string apWorldDest = ResolveApWorldDestination();
		if (string.IsNullOrEmpty(apWorldDest))
		{
			return;
		}
		string path = Path.Combine(_gameDir, "apworld");
		if (!Directory.Exists(path))
		{
			return;
		}
		string[] files = Directory.GetFiles(path, "*.apworld");
		if (files.Length == 0)
		{
			return;
		}
		try
		{
			Directory.CreateDirectory(apWorldDest);
			string[] array = files;
			foreach (string text in array)
			{
				string destFileName = Path.Combine(apWorldDest, Path.GetFileName(text));
				File.Copy(text, destFileName, overwrite: true);
			}
			SetStatus($"AP World updated: {files.Length} file(s) copied to {apWorldDest}");
		}
		catch (Exception ex)
		{
			SetStatus("Failed to copy AP World: " + ex.Message);
		}
	}

	private static string AutoDetectD2Install()
	{
		// Candidate paths are probed in order; the first one that passes
		// ValidateD2OriginalDir (has ALL 11 Blizzard files, no D2R/ClassicMode
		// markers) wins. Registry lookups run BEFORE the hardcoded path list
		// because a user who installed D2 into a non-default folder will have
		// the correct path in the registry.
		List<string> candidates = new List<string>();

		// Per-user install (HKCU) — legacy Blizzard installer writes here.
		TryAddRegPath(candidates, Registry.CurrentUser, @"Software\Blizzard Entertainment\Diablo II", "InstallPath");

		// System-wide Blizzard installs (HKLM, 32-bit and 64-bit views).
		TryAddRegPath(candidates, Registry.LocalMachine, @"SOFTWARE\Blizzard Entertainment\Diablo II", "InstallPath");
		TryAddRegPath(candidates, Registry.LocalMachine, @"SOFTWARE\WOW6432Node\Blizzard Entertainment\Diablo II", "InstallPath");

		// Windows Add/Remove Programs uninstall entries.
		TryAddRegPath(candidates, Registry.LocalMachine, @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Diablo II", "InstallLocation");
		TryAddRegPath(candidates, Registry.LocalMachine, @"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Diablo II", "InstallLocation");

		// GOG — Diablo II: LoD, product id 1435828550.
		TryAddRegPath(candidates, Registry.LocalMachine, @"SOFTWARE\GOG.com\Games\1435828550", "path");
		TryAddRegPath(candidates, Registry.LocalMachine, @"SOFTWARE\WOW6432Node\GOG.com\Games\1435828550", "path");

		// Hardcoded fallback paths — default Blizzard + GOG + common custom
		// locations on both C: and D: drives.
		candidates.Add(@"C:\Program Files (x86)\Diablo II");
		candidates.Add(@"C:\Program Files\Diablo II");
		candidates.Add(@"C:\GOG Games\Diablo II");
		candidates.Add(@"C:\Games\Diablo II");
		candidates.Add(@"D:\Diablo II");
		candidates.Add(@"D:\Games\Diablo II");
		candidates.Add(@"D:\GOG Games\Diablo II");
		try
		{
			string userProfile = Environment.GetEnvironmentVariable("USERPROFILE");
			if (!string.IsNullOrEmpty(userProfile))
			{
				candidates.Add(Path.Combine(userProfile, "Games", "Diablo II"));
			}
		}
		catch { }

		foreach (string candidate in candidates)
		{
			if (ValidateD2OriginalDir(candidate))
			{
				return candidate;
			}
		}
		return "";
	}

	// True when `p` falls inside the working area of at least one currently
	// attached monitor. Used on startup so we don't restore the window onto a
	// monitor the user has unplugged.
	private static bool IsPointOnAnyScreen(Point p)
	{
		foreach (Screen s in Screen.AllScreens)
		{
			if (s.WorkingArea.Contains(p))
			{
				return true;
			}
		}
		return false;
	}

	private static void TryAddRegPath(List<string> candidates, RegistryKey root, string subKey, string valueName)
	{
		try
		{
			using RegistryKey? key = root.OpenSubKey(subKey);
			if (key != null)
			{
				string? regPath = key.GetValue(valueName) as string;
				if (!string.IsNullOrWhiteSpace(regPath))
				{
					candidates.Add(regPath.TrimEnd('\\', '/'));
				}
			}
		}
		catch { }
	}

	// Returns true when `path` is a valid Classic D2 + LoD install:
	//   - exists on disk
	//   - is NOT a Diablo II Resurrected install (D2R.exe / ClassicMode\ means reject)
	//   - contains all 11 original Blizzard files required by the shipped game package
	private static bool ValidateD2OriginalDir(string path)
	{
		return ValidateD2OriginalDirDetailed(path).ok;
	}

	// Detailed variant used by install code to report WHY validation failed.
	private static (bool ok, string reason, List<string> missing) ValidateD2OriginalDirDetailed(string path)
	{
		if (string.IsNullOrEmpty(path) || !Directory.Exists(path))
		{
			return (false, "Folder does not exist.", new List<string>());
		}

		// D2R rejection — the Resurrected client is binary-incompatible with
		// the Archipelago bridge. Detect by executable or the ClassicMode
		// compatibility subfolder which is unique to Resurrected installs.
		if (File.Exists(Path.Combine(path, "D2R.exe"))
			|| Directory.Exists(Path.Combine(path, "ClassicMode")))
		{
			return (false,
				"This is Diablo II Resurrected. Archipelago requires the Classic Diablo II + LoD installation.",
				new List<string>());
		}

		// 1.7.1: reject folders that ARE the Archipelago game install itself.
		// If the registry or a hardcoded path accidentally points at our own
		// mod's game folder (which may contain the 11 Blizzard files copied
		// from a previous install), auto-detect would loop back onto itself.
		// D2Archipelago.dll is unique to our mod and never present in a real
		// Classic Diablo II installation.
		if (File.Exists(Path.Combine(path, "D2Archipelago.dll")))
		{
			return (false,
				"This folder is an existing Archipelago install, not an original Diablo II installation. " +
				"Please point to your Classic Diablo II + LoD folder (where you originally installed Diablo II).",
				new List<string>());
		}

		List<string> missing = new List<string>();
		foreach (string file in ORIGINAL_D2_FILES)
		{
			if (!File.Exists(Path.Combine(path, file)))
			{
				missing.Add(file);
			}
		}
		if (missing.Count > 0)
		{
			return (false, "Missing required files: " + string.Join(", ", missing), missing);
		}
		return (true, "", missing);
	}

	// Copies all 11 original Blizzard files from the user's D2 install into the
	// game folder. Returns the total number copied plus a list of files that
	// could not be copied (missing at source or I/O failure). Callers MUST only
	// treat the install as successful when `missing` is empty.
	private (int copied, List<string> missing) CopyOriginalD2Files()
	{
		List<string> missing = new List<string>();
		if (string.IsNullOrEmpty(_d2OriginalDir) || !ValidateD2OriginalDir(_d2OriginalDir))
		{
			// Whole source dir is invalid — report everything as missing.
			foreach (string file in ORIGINAL_D2_FILES)
			{
				missing.Add(file);
			}
			return (0, missing);
		}

		int copied = 0;
		foreach (string file in ORIGINAL_D2_FILES)
		{
			string src = Path.Combine(_d2OriginalDir, file);
			string dst = Path.Combine(_gameDir, file);
			if (!File.Exists(src))
			{
				missing.Add(file);
				continue;
			}
			try
			{
				File.Copy(src, dst, overwrite: true);
				copied++;
			}
			catch
			{
				missing.Add(file);
			}
		}
		return (copied, missing);
	}

	private async Task SelfUpdateAsync(string zipUrl, string? remoteVersion, string? expectedSha256)
	{
		SetStatus("Downloading launcher update...");
		string tempZip = Path.Combine(Path.GetTempPath(), "d2arch_launcher_update.zip");
		string tempDir = Path.Combine(Path.GetTempPath(), "d2arch_launcher_extract");
		try
		{
			SetStatus("Downloading new launcher...");
			byte[] zipBytes = await _http.GetByteArrayAsync(zipUrl);
			await File.WriteAllBytesAsync(tempZip, zipBytes);

			// Integrity check. If the remote manifest supplied a SHA-256 we enforce
			// it strictly; a mismatch aborts the update before any files are touched.
			// If no SHA was supplied (legacy 1.3.0 -> 1.4.0 transition) we log a
			// warning and proceed; from 1.4.0 onward launcher_version.txt always
			// carries the hash.
			if (!string.IsNullOrEmpty(expectedSha256))
			{
				string actualSha;
				using (System.Security.Cryptography.SHA256 sha = System.Security.Cryptography.SHA256.Create())
				{
					actualSha = BitConverter.ToString(sha.ComputeHash(zipBytes)).Replace("-", "").ToLowerInvariant();
				}
				if (!string.Equals(actualSha, expectedSha256, StringComparison.OrdinalIgnoreCase))
				{
					try { File.Delete(tempZip); } catch { }
					SetStatus("Launcher update aborted: SHA-256 mismatch.");
					MessageBox.Show(
						"Launcher update aborted.\n\n" +
						"The downloaded launcher archive failed its SHA-256 integrity check.\n" +
						$"Expected: {expectedSha256}\nActual:   {actualSha}\n\n" +
						"The update has NOT been applied. Please try again later.",
						"Update Integrity Error", MessageBoxButtons.OK, MessageBoxIcon.Hand);
					return;
				}
			}
			else
			{
				SetStatus("Warning: launcher_version.txt has no SHA-256; skipping integrity check.");
			}

			SetStatus("Extracting update...");
			if (Directory.Exists(tempDir))
			{
				Directory.Delete(tempDir, recursive: true);
			}
			ZipFile.ExtractToDirectory(tempZip, tempDir);

			// Persist the new installed version so subsequent launches don't
			// re-prompt for the same update. Written BEFORE we hand off to the
			// updater .bat because the running process owns this launcher dir.
			try
			{
				if (!string.IsNullOrEmpty(remoteVersion))
				{
					File.WriteAllText(Path.Combine(_launcherDir, "launcher_version.dat"), remoteVersion.Trim());
				}
			}
			catch
			{
			}

			string batPath = Path.Combine(Path.GetTempPath(), "d2arch_update.bat");
			string value = Environment.ProcessId.ToString();
			string launcherDir = _launcherDir;
			string value2 = Path.Combine(_launcherDir, "Diablo II Archipelago.exe");
			string contents = $"@echo off\necho Waiting for launcher to close...\n:wait\ntasklist /FI \"PID eq {value}\" 2>nul | find \"{value}\" >nul\nif not errorlevel 1 (\n    timeout /t 1 /nobreak >nul\n    goto wait\n)\necho Copying new launcher files...\nxcopy /Y /E \"{tempDir}\\*\" \"{launcherDir}\\\" >nul 2>&1\necho Cleaning up...\nrmdir /S /Q \"{tempDir}\" >nul 2>&1\ndel \"{tempZip}\" >nul 2>&1\necho Starting updated launcher...\nstart \"\" \"{value2}\"\ndel \"%~f0\" >nul 2>&1\nexit\n";
			await File.WriteAllTextAsync(batPath, contents);
			SetStatus("Restarting launcher...");
			Process.Start(new ProcessStartInfo
			{
				FileName = "cmd.exe",
				Arguments = "/c \"" + batPath + "\"",
				WindowStyle = ProcessWindowStyle.Hidden,
				CreateNoWindow = true
			});
			Application.Exit();
		}
		catch (Exception ex)
		{
			try { if (File.Exists(tempZip)) File.Delete(tempZip); } catch { }
			SetStatus("Launcher update failed: " + ex.Message);
			MessageBox.Show("Failed to update launcher:\n" + ex.Message, "Update Error", MessageBoxButtons.OK, MessageBoxIcon.Hand);
		}
	}

	private void SetStatus(string text)
	{
		_statusText = text;
		if (_lblStatus != null)
		{
			_lblStatus.Text = text;
		}
	}

	private static void OpenUrl(string url)
	{
		Process.Start(new ProcessStartInfo(url)
		{
			UseShellExecute = true
		});
	}

	private static string? ReadReg(string name)
	{
		try
		{
			using RegistryKey registryKey = Registry.CurrentUser.OpenSubKey("Software\\Blizzard Entertainment\\Diablo II");
			return registryKey?.GetValue(name) as string;
		}
		catch
		{
			return null;
		}
	}

	private static void WriteReg(string name, string value)
	{
		try
		{
			using RegistryKey registryKey = Registry.CurrentUser.CreateSubKey("Software\\Blizzard Entertainment\\Diablo II");
			registryKey.SetValue(name, value, RegistryValueKind.String);
		}
		catch
		{
		}
	}

	protected override void OnFormClosing(FormClosingEventArgs e)
	{
		SaveLauncherConfig();
		_logWatchCts?.Cancel();
		base.OnFormClosing(e);
	}
}
