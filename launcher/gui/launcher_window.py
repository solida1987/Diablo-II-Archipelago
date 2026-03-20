"""Main launcher window for Diablo II Archipelago.

Features an embedded game view with skill panels, tracker sidebar,
and Archipelago integration. The game window is captured and reparented
inside the launcher for a seamless experience. Fully resizable.

Now with character profile management: Create / Load views.
"""

import os
import random
import time
import ctypes
import ctypes.wintypes
import logging
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from ..core.config import AppConfig
from ..core.d2_data import D2TxtFile
from ..core.d2s_creator import create_character, get_save_dir, CLASS_IDS
from ..core.skill_randomizer import (
    randomize_skills, get_all_class_skills, CLASS_NAMES, CLASS_CODE_FROM_NAME,
)
from ..core.skill_pool import SkillPoolManager
from ..core.game_launcher import GameLauncher
from ..core.icon_merger import merge_and_deploy
from ..core.character_profile import (
    CharacterProfile, CharacterProfileManager, SaveGameIsolator,
)
from ..core.area_data import get_area_map, get_area_monsters_display
from ..core.d2_memory import D2MemoryReader, find_d2_pid
from ..core.kill_tracker import KillTracker

VANILLA_TXT_DIR = os.path.join(os.path.dirname(__file__), "..", "data", "vanilla_txt")

# ── Color Palette ──
BG_DARK = "#0d0d1a"
BG = "#1a1a2e"
BG_LIGHT = "#16213e"
BG_FIELD = "#0f3460"
BG_PANEL = "#111128"
FG = "#e0e0e0"
FG_DIM = "#666680"
FG_MUTED = "#888899"
ACCENT = "#e94560"
ACCENT_HOVER = "#ff6b81"
GOLD = "#d4a017"
GOLD_DIM = "#8a6b10"
GREEN = "#4ecca3"
RED = "#e94560"
BLUE = "#4ea8de"
PURPLE = "#9b59b6"
BORDER = "#2a2a4a"

# Class colors
CLASS_COLORS = {
    "ama": "#f0c040", "sor": "#6090f0", "nec": "#60d060",
    "pal": "#f0f090", "bar": "#f08040", "dru": "#80c060",
    "ass": "#d060d0",
}

# ── Win32 Constants ──
GWL_STYLE = -16
GWL_EXSTYLE = -20
WS_CAPTION = 0x00C00000
WS_THICKFRAME = 0x00040000
WS_CHILD = 0x40000000
WS_POPUP = 0x80000000
WS_MINIMIZEBOX = 0x00020000
WS_MAXIMIZEBOX = 0x00010000
WS_SYSMENU = 0x00080000
WS_EX_APPWINDOW = 0x00040000
WS_EX_TOOLWINDOW = 0x00000080
SWP_FRAMECHANGED = 0x0020
SWP_NOZORDER = 0x0004
SWP_NOMOVE = 0x0002
SWP_NOSIZE = 0x0001
HWND_TOP = 0
GA_ROOT = 2
GWLP_HWNDPARENT = -8
SW_HIDE = 0
SW_SHOWNOACTIVATE = 4

user32 = ctypes.windll.user32

# SetWindowLongPtrW for 64-bit compatibility
try:
    _SetWindowLongPtrW = user32.SetWindowLongPtrW
except AttributeError:
    _SetWindowLongPtrW = user32.SetWindowLongW


def _apply_dark_theme(root: tk.Tk):
    """Configure dark theme for ttk widgets."""
    style = ttk.Style()
    style.theme_use("clam")

    style.configure(".", background=BG, foreground=FG, fieldbackground=BG_FIELD,
                     borderwidth=0, font=("Segoe UI", 10))
    style.configure("TFrame", background=BG)
    style.configure("TLabel", background=BG, foreground=FG, font=("Segoe UI", 10))
    style.configure("TLabelframe", background=BG, foreground=GOLD,
                     font=("Segoe UI", 10, "bold"))
    style.configure("TLabelframe.Label", background=BG, foreground=GOLD,
                     font=("Segoe UI", 10, "bold"))
    style.configure("TEntry", fieldbackground=BG_FIELD, foreground=FG,
                     insertcolor=FG, font=("Segoe UI", 10))
    style.configure("TCombobox", fieldbackground=BG_FIELD, foreground=FG,
                     selectbackground=ACCENT, font=("Segoe UI", 10))
    style.configure("TCheckbutton", background=BG, foreground=FG, font=("Segoe UI", 10))
    style.configure("TRadiobutton", background=BG, foreground=FG, font=("Segoe UI", 10))

    # Buttons
    style.configure("Launch.TButton", background=ACCENT, foreground="white",
                     font=("Segoe UI", 11, "bold"), padding=(16, 8))
    style.map("Launch.TButton",
              background=[("active", ACCENT_HOVER), ("disabled", "#555")])
    style.configure("Secondary.TButton", background=BG_LIGHT, foreground=FG,
                     font=("Segoe UI", 10), padding=(10, 6))
    style.map("Secondary.TButton", background=[("active", BG_FIELD)])
    style.configure("Small.TButton", background=BG_LIGHT, foreground=FG,
                     font=("Segoe UI", 9), padding=(6, 3))
    style.map("Small.TButton", background=[("active", BG_FIELD)])
    style.configure("Gold.TButton", background=GOLD_DIM, foreground="white",
                     font=("Segoe UI", 10, "bold"), padding=(10, 6))
    style.map("Gold.TButton", background=[("active", GOLD)])
    style.configure("Danger.TButton", background="#5a1a1a", foreground=RED,
                     font=("Segoe UI", 9), padding=(8, 4))
    style.map("Danger.TButton", background=[("active", "#7a2a2a")])

    # Dim label
    style.configure("Dim.TLabel", foreground=FG_DIM, font=("Segoe UI", 9))

    # Progressbar
    style.configure("green.Horizontal.TProgressbar",
                     troughcolor=BG_DARK, background=GREEN, thickness=6)

    root.configure(bg=BG_DARK)
    root.option_add("*TCombobox*Listbox.background", BG_FIELD)
    root.option_add("*TCombobox*Listbox.foreground", FG)


class LauncherWindow:
    """Main launcher with embedded game view and sidebars."""

    LEFT_W = 220
    RIGHT_W = 220
    TOP_H = 44
    BOTTOM_H = 100
    MIN_GAME_W = 640
    MIN_GAME_H = 480

    def __init__(self, root: tk.Tk, base_dir: str):
        self.root = root
        self.base_dir = base_dir
        self.config_path = os.path.join(base_dir, "config.json")

        self.config = AppConfig.load(self.config_path)
        if not self.config.game_dir:
            self.config.game_dir = base_dir

        # Character profile manager
        self.profile_mgr = CharacterProfileManager(base_dir)
        self.active_profile: CharacterProfile | None = None

        # Pool manager (set per-character on load/create)
        self.pool_manager: SkillPoolManager | None = None

        # Skill tree editor (embedded in game container)
        self.skill_tree_view = None

        self.game_hwnd = None
        self.game_process = None
        self.game_embedded = False
        self._monitor_id = None  # Track after() ID for cancellation
        self._closing = False
        self._memory_reader = D2MemoryReader()
        self._kill_tracker: KillTracker | None = None
        self._current_area_id: int | None = None
        self._last_xp: int | None = None  # For kill detection via XP changes
        self._kill_save_counter = 0       # Auto-save kills every N polls

        # File logger
        self.log_path = os.path.join(base_dir, "launcher.log")
        self.file_logger = logging.getLogger("d2launcher")
        self.file_logger.setLevel(logging.DEBUG)
        # Clear old handlers
        self.file_logger.handlers.clear()
        fh = logging.FileHandler(self.log_path, mode="w", encoding="utf-8")
        fh.setFormatter(logging.Formatter("%(asctime)s  %(message)s", "%H:%M:%S"))
        self.file_logger.addHandler(fh)
        self.file_logger.info("=== Launcher started ===")
        self.file_logger.info(f"Base dir: {base_dir}")

        _apply_dark_theme(root)
        self._build_ui()
        self._start_game_monitor()

        root.protocol("WM_DELETE_WINDOW", self._on_close)

    # ════════════════════════════════════════════════════════════════
    #  UI CONSTRUCTION
    # ════════════════════════════════════════════════════════════════

    def _build_ui(self):
        r = self.root
        r.title("Diablo II \u2014 Archipelago Randomizer")
        r.geometry("1280x800")
        r.minsize(self.LEFT_W + self.MIN_GAME_W + self.RIGHT_W,
                  self.TOP_H + self.MIN_GAME_H + self.BOTTOM_H)
        r.configure(bg=BG_DARK)

        r.rowconfigure(1, weight=1)
        r.columnconfigure(0, weight=0)
        r.columnconfigure(1, weight=1)
        r.columnconfigure(2, weight=0)

        self._build_top_bar()
        self._build_left_panel()
        self._build_game_frame()
        self._build_right_panel()
        self._build_bottom_bar()

        r.bind("<Configure>", self._on_resize)
        r.bind("<Unmap>", self._on_launcher_minimize)
        r.bind("<Map>", self._on_launcher_restore)

    # ─── TOP BAR ───────────────────────────────────────────────────
    def _build_top_bar(self):
        top = tk.Frame(self.root, bg=BG_DARK, height=self.TOP_H)
        top.grid(row=0, column=0, columnspan=3, sticky="ew")
        top.grid_propagate(False)

        tk.Label(top, text="DIABLO II", font=("Segoe UI", 14, "bold"),
                 fg=GOLD, bg=BG_DARK).pack(side=tk.LEFT, padx=(12, 0))
        tk.Label(top, text="ARCHIPELAGO", font=("Segoe UI", 9),
                 fg=ACCENT, bg=BG_DARK).pack(side=tk.LEFT, padx=(6, 0), pady=(4, 0))

        self.conn_label = tk.Label(top, text="\u25cf OFFLINE",
                                   font=("Consolas", 9), fg=FG_DIM, bg=BG_DARK)
        self.conn_label.pack(side=tk.RIGHT, padx=12)

        self.info_label = tk.Label(top, text="", font=("Consolas", 9),
                                   fg=FG_MUTED, bg=BG_DARK)
        self.info_label.pack(side=tk.RIGHT, padx=20)
        self._update_info_label()

        sep = tk.Frame(self.root, bg=BORDER, height=1)
        sep.grid(row=0, column=0, columnspan=3, sticky="sew")

    # ─── LEFT PANEL ────────────────────────────────────────────────
    def _build_left_panel(self):
        left = tk.Frame(self.root, bg=BG_PANEL, width=self.LEFT_W)
        left.grid(row=1, column=0, sticky="ns")
        left.grid_propagate(False)
        self._left_panel = left

        # ── Tab bar ──
        tab_bar = tk.Frame(left, bg=BG_DARK)
        tab_bar.pack(fill=tk.X)
        self._left_tabs = {}
        self._left_active_tab = "PROGRESS"

        for tab_name in ["PROGRESS", "SKILLS"]:
            btn = tk.Label(tab_bar, text=tab_name, font=("Segoe UI", 8, "bold"),
                           fg=GOLD if tab_name == "PROGRESS" else FG_DIM,
                           bg=BG_PANEL if tab_name == "PROGRESS" else BG_DARK,
                           padx=12, pady=6, cursor="hand2")
            btn.pack(side=tk.LEFT)
            btn.bind("<Button-1>", lambda e, n=tab_name: self._switch_left_tab(n))
            self._left_tabs[tab_name] = btn

        # ── Scrollable content area (shared canvas) ──
        self.left_canvas = tk.Canvas(left, bg=BG_PANEL, highlightthickness=0, bd=0)
        self.left_canvas.pack(fill=tk.BOTH, expand=True, padx=2, pady=4)

        self.left_content = tk.Frame(self.left_canvas, bg=BG_PANEL)
        self._left_window = self.left_canvas.create_window(
            (0, 0), window=self.left_content, anchor=tk.NW)

        self.left_content.bind("<Configure>",
            lambda e: self.left_canvas.configure(scrollregion=self.left_canvas.bbox("all")))
        self.left_canvas.bind("<Configure>",
            lambda e: self.left_canvas.itemconfig(self._left_window, width=e.width))

        self._left_wheel_id = None
        def _left_wheel(e):
            self.left_canvas.yview_scroll(int(-1 * (e.delta / 120)), "units")
        def _left_enter(e):
            self._left_wheel_id = self.left_canvas.bind_all("<MouseWheel>", _left_wheel)
        def _left_leave(e):
            if self._left_wheel_id:
                self.left_canvas.unbind_all("<MouseWheel>")
                self._left_wheel_id = None
        self.left_canvas.bind("<Enter>", _left_enter)
        self.left_canvas.bind("<Leave>", _left_leave)
        self.left_canvas.bind("<Destroy>", lambda e: _left_leave(e))

        # Also keep references for the old skills populate
        self.skills_frame = self.left_content
        self.skill_canvas = self.left_canvas
        self._skills_window = self._left_window
        self.skill_count_label = tk.Label(left)  # Hidden placeholder

        self._populate_left_panel()

    def _switch_left_tab(self, tab_name: str):
        if tab_name == self._left_active_tab:
            return
        self._left_active_tab = tab_name
        for name, btn in self._left_tabs.items():
            if name == tab_name:
                btn.config(fg=GOLD, bg=BG_PANEL)
            else:
                btn.config(fg=FG_DIM, bg=BG_DARK)
        self._populate_left_panel()

    def _populate_left_panel(self):
        if self._left_active_tab == "PROGRESS":
            self._populate_progress()
        else:
            self._populate_skills()

    def _populate_progress(self):
        """Show area clearing progress per act."""
        for w in self.left_content.winfo_children():
            w.destroy()

        if not self._kill_tracker:
            tk.Label(self.left_content,
                     text="\n  No character loaded",
                     font=("Segoe UI", 9), fg=FG_DIM, bg=BG_PANEL,
                     justify=tk.LEFT).pack(anchor=tk.W, padx=8)
            return

        area_map = get_area_map()
        # Group areas by act
        acts: dict[int, list] = {1: [], 2: [], 3: [], 4: [], 5: []}
        for area in sorted(area_map.values(), key=lambda a: a.area_id):
            if area.is_town or not area.monsters or area.act not in acts:
                continue
            acts[area.act].append(area)

        for act_num in range(1, 6):
            areas = acts[act_num]
            if not areas:
                continue

            # Count cleared
            cleared = sum(
                1 for a in areas
                if self._kill_tracker.get_progress(a.area_id).completed
            )

            # Act header
            hdr = tk.Frame(self.left_content, bg=BG_DARK)
            hdr.pack(fill=tk.X, padx=6, pady=(8, 3))

            act_color = GREEN if cleared == len(areas) else GOLD_DIM
            tk.Label(hdr, text=f"  ACT {act_num}", font=("Segoe UI", 7, "bold"),
                     fg=act_color, bg=BG_DARK).pack(side=tk.LEFT)
            tk.Label(hdr, text=f"{cleared}/{len(areas)}",
                     font=("Consolas", 7), fg=FG_DIM, bg=BG_DARK).pack(side=tk.RIGHT, padx=4)

            # Highlight current act
            is_current_act = (self._current_area_id is not None and
                              area_map.get(self._current_area_id, None) is not None and
                              area_map[self._current_area_id].act == act_num)

            # Area rows
            for area in areas:
                prog = self._kill_tracker.get_progress(area.area_id)
                mons = get_area_monsters_display(area.area_id)
                num_types = max(len(mons), 1)
                total_target = area.kills_per_type * num_types

                row = tk.Frame(self.left_content, bg=BG_PANEL)
                row.pack(fill=tk.X, padx=6, pady=0)

                # Checkmark or bullet
                is_active = (self._current_area_id == area.area_id)
                if prog.completed:
                    marker = "\u2713"
                    marker_color = GREEN
                elif is_active:
                    marker = "\u25b6"
                    marker_color = GOLD
                else:
                    marker = "\u2022"
                    marker_color = FG_DIM

                tk.Label(row, text=marker, font=("Segoe UI", 7),
                         fg=marker_color, bg=BG_PANEL, width=2).pack(side=tk.LEFT)

                # Area name
                name = area.display_name
                if len(name) > 20:
                    name = name[:18] + ".."
                name_color = FG if is_active else (GREEN if prog.completed else FG_DIM)
                tk.Label(row, text=name, font=("Segoe UI", 7),
                         fg=name_color, bg=BG_PANEL, anchor=tk.W).pack(
                         side=tk.LEFT, fill=tk.X, expand=True)

                # Kill count
                if prog.kills > 0 or is_active:
                    count_color = GREEN if prog.completed else FG_DIM
                    tk.Label(row, text=f"{prog.kills}/{total_target}",
                             font=("Consolas", 6), fg=count_color,
                             bg=BG_PANEL).pack(side=tk.RIGHT, padx=2)

    def _populate_skills(self):
        for w in self.left_content.winfo_children():
            w.destroy()

        if not self.pool_manager:
            tk.Label(self.left_content,
                     text="\n  No character loaded",
                     font=("Segoe UI", 9), fg=FG_DIM, bg=BG_PANEL,
                     justify=tk.LEFT).pack(anchor=tk.W, padx=8)
            return

        equipped = self.pool_manager.get_equipped()
        if not equipped:
            tk.Label(self.left_content,
                     text="\n  No skills loaded\n\n  Click RANDOMIZE\n  & LAUNCH to start",
                     font=("Segoe UI", 9), fg=FG_DIM, bg=BG_PANEL,
                     justify=tk.LEFT).pack(anchor=tk.W, padx=8)
            return

        tabs = ["TAB 1", "TAB 2", "TAB 3"]

        for t in range(3):
            th = tk.Frame(self.left_content, bg=BG_DARK)
            th.pack(fill=tk.X, padx=6, pady=(8 if t == 0 else 10, 3))
            tk.Label(th, text=f"  {tabs[t]}", font=("Segoe UI", 7, "bold"),
                     fg=GOLD_DIM, bg=BG_DARK).pack(side=tk.LEFT, fill=tk.X, expand=True)

            for i in range(t * 10, min(t * 10 + 10, len(equipped))):
                self._add_skill_row(equipped[i])

    def _add_skill_row(self, skill):
        row = tk.Frame(self.left_content, bg=BG_PANEL, cursor="hand2")
        row.pack(fill=tk.X, padx=6, pady=1)

        color = CLASS_COLORS.get(skill.original_class, FG_DIM)
        tk.Frame(row, bg=color, width=3).pack(side=tk.LEFT, fill=tk.Y, padx=(0, 6))

        name = skill.skill_name if len(skill.skill_name) <= 22 else skill.skill_name[:20] + ".."
        tk.Label(row, text=name, font=("Segoe UI", 8), fg=FG,
                 bg=BG_PANEL, anchor=tk.W).pack(side=tk.LEFT, fill=tk.X, expand=True)

        tk.Label(row, text=skill.original_class.upper(),
                 font=("Consolas", 7), fg=FG_DIM, bg=BG_PANEL).pack(side=tk.RIGHT, padx=(4, 2))

        def enter(_):
            row.config(bg=BG_LIGHT)
            for c in row.winfo_children():
                if isinstance(c, tk.Label):
                    c.config(bg=BG_LIGHT)
        def leave(_):
            row.config(bg=BG_PANEL)
            for c in row.winfo_children():
                if isinstance(c, tk.Label):
                    c.config(bg=BG_PANEL)
        row.bind("<Enter>", enter)
        row.bind("<Leave>", leave)

    # ─── GAME FRAME ────────────────────────────────────────────────
    def _build_game_frame(self):
        border = tk.Frame(self.root, bg=BORDER)
        border.grid(row=1, column=1, sticky="nsew", padx=1, pady=1)

        self.game_container = tk.Frame(border, bg="#000000")
        self.game_container.pack(fill=tk.BOTH, expand=True, padx=1, pady=1)

        self.placeholder = tk.Frame(self.game_container, bg=BG_DARK)
        self.placeholder.place(relx=0, rely=0, relwidth=1, relheight=1)
        self._build_placeholder()

    def _build_placeholder(self):
        """Build the center placeholder with Create/Load character views."""
        p = self.placeholder

        # Clear existing content
        for w in p.winfo_children():
            w.destroy()

        center = tk.Frame(p, bg=BG_DARK)
        center.place(relx=0.5, rely=0.5, anchor=tk.CENTER)

        tk.Label(center, text="DIABLO II", font=("Segoe UI", 30, "bold"),
                 fg=GOLD, bg=BG_DARK).pack(pady=(0, 2))
        tk.Label(center, text="ARCHIPELAGO RANDOMIZER", font=("Segoe UI", 12),
                 fg=ACCENT, bg=BG_DARK).pack(pady=(0, 20))

        # ── Tab-like buttons ──
        tab_frame = tk.Frame(center, bg=BG_DARK)
        tab_frame.pack(fill=tk.X, pady=(0, 0))

        self._tab_buttons = {}
        self._view_frames = {}

        for tab_name in ["CHARACTERS", "NEW CHARACTER"]:
            btn = tk.Label(tab_frame, text=f"  {tab_name}  ",
                           font=("Segoe UI", 10, "bold"), fg=FG_DIM,
                           bg=BG_DARK, cursor="hand2", padx=12, pady=4)
            btn.pack(side=tk.LEFT, padx=(0, 2))
            btn.bind("<Button-1>", lambda e, t=tab_name: self._switch_tab(t))
            self._tab_buttons[tab_name] = btn

        # Separator under tabs
        tk.Frame(center, bg=BORDER, height=1).pack(fill=tk.X, pady=(0, 0))

        # ── Content frames ──
        content = tk.Frame(center, bg=BG_DARK, width=520)
        content.pack(fill=tk.BOTH, expand=True)

        self._build_characters_view(content)
        self._build_create_view(content)

        # Show characters tab if profiles exist, else create tab
        profiles = self.profile_mgr.list_profiles()
        if profiles:
            self._switch_tab("CHARACTERS")
        else:
            self._switch_tab("NEW CHARACTER")

    def _switch_tab(self, tab_name: str):
        """Switch between Characters and New Character views."""
        for name, btn in self._tab_buttons.items():
            if name == tab_name:
                btn.config(fg=GOLD, bg=BG_LIGHT)
            else:
                btn.config(fg=FG_DIM, bg=BG_DARK)

        for name, frame in self._view_frames.items():
            if name == tab_name:
                frame.pack(fill=tk.BOTH, expand=True)
            else:
                frame.pack_forget()

        if tab_name == "CHARACTERS":
            self._refresh_character_list()

    # ─── CHARACTERS VIEW (Load) ──────────────────────────────────
    def _build_characters_view(self, parent):
        frame = tk.Frame(parent, bg=BG_DARK)
        self._view_frames["CHARACTERS"] = frame

        # Scrollable character list
        self.char_list_canvas = tk.Canvas(frame, bg=BG_DARK, highlightthickness=0, bd=0)
        self.char_list_canvas.pack(fill=tk.BOTH, expand=True, padx=4, pady=8)

        self.char_list_frame = tk.Frame(self.char_list_canvas, bg=BG_DARK)
        self._char_list_window = self.char_list_canvas.create_window(
            (0, 0), window=self.char_list_frame, anchor=tk.NW)

        self.char_list_frame.bind("<Configure>",
            lambda _: self.char_list_canvas.configure(
                scrollregion=self.char_list_canvas.bbox("all")))
        self.char_list_canvas.bind("<Configure>",
            lambda e: self.char_list_canvas.itemconfig(
                self._char_list_window, width=e.width))

        self._charlist_wheel_id = None
        def _charlist_wheel(e):
            self.char_list_canvas.yview_scroll(int(-1 * (e.delta / 120)), "units")
        def _charlist_enter(e):
            self._charlist_wheel_id = self.char_list_canvas.bind_all("<MouseWheel>", _charlist_wheel)
        def _charlist_leave(e):
            if self._charlist_wheel_id:
                self.char_list_canvas.unbind_all("<MouseWheel>")
                self._charlist_wheel_id = None
        self.char_list_canvas.bind("<Enter>", _charlist_enter)
        self.char_list_canvas.bind("<Leave>", _charlist_leave)
        self.char_list_canvas.bind("<Destroy>", lambda e: _charlist_leave(e))

    def _refresh_character_list(self):
        for w in self.char_list_frame.winfo_children():
            w.destroy()

        profiles = self.profile_mgr.list_profiles()
        if not profiles:
            tk.Label(self.char_list_frame,
                     text="No characters yet.\nCreate one in the NEW CHARACTER tab.",
                     font=("Segoe UI", 11), fg=FG_DIM, bg=BG_DARK,
                     justify=tk.CENTER).pack(pady=40)
            return

        for prof in profiles:
            self._add_character_card(prof)

    def _add_character_card(self, profile: CharacterProfile):
        """Add a clickable character card to the list."""
        card = tk.Frame(self.char_list_frame, bg=BG_LIGHT, cursor="hand2",
                        padx=12, pady=8)
        card.pack(fill=tk.X, padx=8, pady=3)

        # Left: class color bar
        color = CLASS_COLORS.get(profile.class_code, FG_DIM)
        tk.Frame(card, bg=color, width=4).pack(side=tk.LEFT, fill=tk.Y, padx=(0, 10))

        # Info block
        info = tk.Frame(card, bg=BG_LIGHT)
        info.pack(side=tk.LEFT, fill=tk.X, expand=True)

        # Name + class
        name_row = tk.Frame(info, bg=BG_LIGHT)
        name_row.pack(fill=tk.X)
        tk.Label(name_row, text=profile.name, font=("Segoe UI", 12, "bold"),
                 fg=FG, bg=BG_LIGHT).pack(side=tk.LEFT)
        tk.Label(name_row, text=f"  {profile.class_name}",
                 font=("Segoe UI", 10), fg=FG_MUTED, bg=BG_LIGHT).pack(side=tk.LEFT)

        # Details
        details = []
        if profile.seed is not None:
            details.append(f"Seed: {profile.seed}")
        if profile.mode != "standalone" and profile.ap_server:
            details.append(f"AP: {profile.ap_server}")
        if profile.last_played:
            details.append(f"Last: {profile.last_played[:16]}")
        elif profile.created_at:
            details.append(f"Created: {profile.created_at[:16]}")

        if details:
            tk.Label(info, text="  \u2502  ".join(details),
                     font=("Consolas", 8), fg=FG_DIM, bg=BG_LIGHT).pack(anchor=tk.W)

        # Right: Play + Delete buttons
        btn_frame = tk.Frame(card, bg=BG_LIGHT)
        btn_frame.pack(side=tk.RIGHT, padx=(10, 0))

        play_btn = tk.Label(btn_frame, text="\u25b6 PLAY", font=("Segoe UI", 10, "bold"),
                            fg="#000", bg=GREEN, padx=12, pady=4, cursor="hand2")
        play_btn.pack(pady=(0, 4))
        play_btn.bind("<Button-1>", lambda e, p=profile: self._load_and_launch(p))

        reset_btn = tk.Label(btn_frame, text="\u21bb Maps", font=("Segoe UI", 8),
                             fg=BLUE, bg=BG_LIGHT, cursor="hand2", padx=4)
        reset_btn.pack(pady=(0, 2))
        reset_btn.bind("<Button-1>", lambda e, p=profile: self._reset_maps(p))

        del_btn = tk.Label(btn_frame, text="\u2715", font=("Segoe UI", 9),
                           fg=RED, bg=BG_LIGHT, cursor="hand2", padx=4)
        del_btn.pack()
        del_btn.bind("<Button-1>", lambda e, p=profile: self._delete_character(p))

        # Hover effect on whole card
        def enter(_):
            card.config(bg=BG_FIELD)
            info.config(bg=BG_FIELD)
            name_row.config(bg=BG_FIELD)
            for c in info.winfo_children():
                c.config(bg=BG_FIELD)
            for c in name_row.winfo_children():
                c.config(bg=BG_FIELD)
            btn_frame.config(bg=BG_FIELD)
            del_btn.config(bg=BG_FIELD)
            reset_btn.config(bg=BG_FIELD)
        def leave(_):
            card.config(bg=BG_LIGHT)
            info.config(bg=BG_LIGHT)
            name_row.config(bg=BG_LIGHT)
            for c in info.winfo_children():
                c.config(bg=BG_LIGHT)
            for c in name_row.winfo_children():
                c.config(bg=BG_LIGHT)
            btn_frame.config(bg=BG_LIGHT)
            del_btn.config(bg=BG_LIGHT)
            reset_btn.config(bg=BG_LIGHT)
        card.bind("<Enter>", enter)
        card.bind("<Leave>", leave)

    def _delete_character(self, profile: CharacterProfile):
        if not messagebox.askyesno(
            "Delete Character",
            f"Delete '{profile.name}'?\n\nThis removes the profile and skill state.\n"
            f"The D2 save file will NOT be deleted.",
        ):
            return
        self.profile_mgr.delete(profile.name)
        self._log(f"Character '{profile.name}' deleted.", "gold")
        self._refresh_character_list()

    def _reset_maps(self, profile: CharacterProfile):
        """Delete map files for a character (like joining multiplayer)."""
        if not messagebox.askyesno(
            "Reset Maps",
            f"Reset maps for '{profile.name}'?\n\n"
            f"This will delete all map data, giving you\n"
            f"fresh randomly generated maps.\n\n"
            f"Your progress, waypoints, quests, and\n"
            f"kill tracker will NOT be affected.",
        ):
            return

        save_dir = get_save_dir()
        if not save_dir:
            messagebox.showerror("Error", "Could not find D2 save directory.")
            return

        map_extensions = {".ma0", ".ma1", ".ma2", ".ma3", ".map"}
        deleted = 0
        for ext in map_extensions:
            path = os.path.join(save_dir, profile.name + ext)
            if os.path.exists(path):
                try:
                    os.remove(path)
                    deleted += 1
                except OSError as e:
                    self._log(f"Could not delete {path}: {e}", "red")

        if deleted > 0:
            self._log(f"Reset maps for '{profile.name}': {deleted} files deleted.", "gold")
        else:
            self._log(f"No map files found for '{profile.name}'.", "dim")

    def _load_and_launch(self, profile: CharacterProfile):
        """Load a character profile and launch the game."""
        self.active_profile = profile
        game_dir = self.config.game_dir

        if not os.path.exists(os.path.join(game_dir, "Game.exe")):
            messagebox.showerror("Error", f"Game.exe not found in:\n{game_dir}")
            return

        # Set up pool manager for this character
        state_path = self.profile_mgr.get_state_path(profile.name)
        self.pool_manager = SkillPoolManager(state_path)
        self.pool_manager.load()

        # Set up kill tracker for this character
        kills_path = os.path.join(
            self.profile_mgr.profiles_dir, f"{profile.name}_kills.json")
        self._kill_tracker = KillTracker(kills_path)
        self._kill_tracker.load()
        self._current_area_id = None
        self._last_xp = None

        # Save game isolation
        save_dir = get_save_dir()
        if save_dir:
            isolator = SaveGameIsolator(self.base_dir, save_dir)
            # First restore any previously backed up saves
            restored = isolator.restore_all()
            if restored:
                self._log(f"Restored {restored} backed-up save files.", "dim")
            # Then isolate for this character
            moved = isolator.isolate_character(profile.name)
            if moved:
                self._log(f"Isolated saves: moved {moved} other files to backup.", "dim")

        # Check if skills already randomized
        if profile.skills_randomized and self.pool_manager.state.equipped:
            # Already set up — just prepare files and launch
            self._log(f"Loading character '{profile.name}'...", "gold")
            self._prepare_and_launch_existing(profile)
        else:
            # First time — randomize
            self._log(f"Creating new game for '{profile.name}'...", "gold")
            self._randomize_and_launch(profile)

        self._populate_skills()
        self._update_info_label()
        self._update_right_panel()

    def _prepare_and_launch_existing(self, profile: CharacterProfile):
        """Prepare game files from existing skill state and launch."""
        try:
            game_dir = self.config.game_dir
            target_class = profile.class_code

            vanilla_dir = os.path.normpath(VANILLA_TXT_DIR)
            skills_txt = D2TxtFile.from_file(os.path.join(vanilla_dir, "Skills.txt"))
            sdesc_txt = D2TxtFile.from_file(os.path.join(vanilla_dir, "SkillDesc.txt"))
            cstats_txt = D2TxtFile.from_file(os.path.join(vanilla_dir, "CharStats.txt"))

            # Merge icons
            vanilla_skills_for_icons = D2TxtFile.from_file(
                os.path.join(vanilla_dir, "Skills.txt"))
            merge_and_deploy(sdesc_txt, vanilla_skills_for_icons, game_dir)

            # Get all skills for lookup
            all_skills = get_all_class_skills(skills_txt)
            skill_by_name = {s.skill_name: s for s in all_skills}

            # Apply equipped skills from pool state
            equipped = self.pool_manager.get_equipped()
            equipped_entries = []
            for eq in equipped:
                entry = skill_by_name.get(eq.skill_name)
                if entry:
                    equipped_entries.append(entry)

            equipped_set = {s.row_index for s in equipped_entries}

            for skill in equipped_entries:
                skills_txt.set_value(skill.row_index, "charclass", target_class)
                skills_txt.set_value(skill.row_index, "reqlevel", "1")
                skills_txt.set_value(skill.row_index, "reqskill1", "")
                skills_txt.set_value(skill.row_index, "reqskill2", "")
                skills_txt.set_value(skill.row_index, "reqskill3", "")

            # Rebalance
            from ..core.skill_randomizer import CLASS_CODES, get_skills_by_class
            by_class = get_skills_by_class(all_skills)
            spare = [s for s in by_class[target_class] if s.row_index not in equipped_set]
            spare_idx = 0
            for cc in CLASS_CODES:
                if cc == target_class:
                    continue
                taken = [s for s in by_class[cc] if s.row_index in equipped_set]
                for _ in taken:
                    if spare_idx < len(spare):
                        skills_txt.set_value(spare[spare_idx].row_index, "charclass", cc)
                        spare_idx += 1

            # Layout from pool state positions
            from ..core.skill_tiers import TAB_LAYOUT
            sd_name_to_row = {}
            for ri in range(len(sdesc_txt.rows)):
                name = sdesc_txt.get_value(ri, "skilldesc")
                if name:
                    sd_name_to_row[name] = ri

            # Hide all
            for ri in range(len(sdesc_txt.rows)):
                pg = sdesc_txt.get_value(ri, "SkillPage")
                if pg and pg != "0":
                    sdesc_txt.set_value(ri, "SkillPage", "0")

            # Place equipped skills at their slot positions
            for eq in equipped:
                entry = skill_by_name.get(eq.skill_name)
                if not entry:
                    continue
                sd_row = sd_name_to_row.get(entry.skilldesc_ref)
                if sd_row is None:
                    continue

                slot_idx = eq.slot_index
                tab = slot_idx // 10
                slot = slot_idx % 10
                if slot < len(TAB_LAYOUT):
                    row, col, tier = TAB_LAYOUT[slot]
                    page = tab + 1
                    d2_row = {0: 1, 1: 3, 2: 5, 3: 6}[row]
                    d2_col = col + 1
                    sdesc_txt.set_value(sd_row, "SkillPage", str(page))
                    sdesc_txt.set_value(sd_row, "SkillRow", str(d2_row))
                    sdesc_txt.set_value(sd_row, "SkillColumn", str(d2_col))
                    sdesc_txt.set_value(sd_row, "ListRow", str(page))

            # Update CharStats
            if equipped_entries:
                from ..core.skill_randomizer import _update_charstats
                _update_charstats(cstats_txt, target_class, equipped_entries[0].skill_name)

            # Write files
            launcher = GameLauncher(game_dir)
            launcher.prepare_data_folder({
                "Skills.txt": skills_txt,
                "SkillDesc.txt": sdesc_txt,
                "CharStats.txt": cstats_txt,
            })

            # Update last played
            profile.last_played = time.strftime("%Y-%m-%d %H:%M:%S")
            self.profile_mgr.save(profile)

            self._log("Game files prepared.", "green")
            self._launch_game()

        except Exception as e:
            messagebox.showerror("Error", f"Failed to prepare game:\n{e}")
            self._log(f"ERROR: {e}", "red")
            import traceback
            traceback.print_exc()

    def _randomize_and_launch(self, profile: CharacterProfile):
        """First-time randomization for a character."""
        try:
            game_dir = self.config.game_dir
            target_class = profile.class_code

            vanilla_dir = os.path.normpath(VANILLA_TXT_DIR)
            skills_txt = D2TxtFile.from_file(os.path.join(vanilla_dir, "Skills.txt"))
            sdesc_txt = D2TxtFile.from_file(os.path.join(vanilla_dir, "SkillDesc.txt"))
            cstats_txt = D2TxtFile.from_file(os.path.join(vanilla_dir, "CharStats.txt"))

            # Merge icons
            self._log("Merging skill icons...", "gold")
            try:
                vanilla_skills_for_icons = D2TxtFile.from_file(
                    os.path.join(vanilla_dir, "Skills.txt"))
                icon_ok = merge_and_deploy(sdesc_txt, vanilla_skills_for_icons, game_dir)
                if icon_ok:
                    self._log("Skill icons merged and deployed!", "green")
                else:
                    self._log("Icon merge skipped (missing files)", "gold")
            except Exception as ie:
                self._log(f"Icon merge error: {ie}", "red")

            seed = profile.seed
            self._log("Randomizing skills...", "gold")

            vanilla_skills_copy = D2TxtFile.from_file(
                os.path.join(vanilla_dir, "Skills.txt"))
            vanilla_sdesc_copy = D2TxtFile.from_file(
                os.path.join(vanilla_dir, "SkillDesc.txt"))

            selected = randomize_skills(
                skills_txt, sdesc_txt, cstats_txt, target_class, seed,
                vanilla_skills_txt=vanilla_skills_copy,
                vanilla_sdesc_txt=vanilla_sdesc_copy,
            )
            self._log(f"Selected {len(selected)} skills (seed: {seed})", "green")

            # Initialize pool with tree slot positions
            all_skills_after = get_all_class_skills(skills_txt)
            skills_per_tab = len(selected) // 3
            equipped_with_slots = []
            for i, s in enumerate(selected):
                tab = i // skills_per_tab
                slot_in_tab = i % skills_per_tab
                global_slot = tab * 10 + slot_in_tab
                equipped_with_slots.append((global_slot, s))
            self.pool_manager.initialize_from_randomization_with_slots(
                equipped_with_slots, all_skills_after, target_class, seed)
            self.pool_manager.save()

            # Write game files
            self._log("Preparing game files...")
            launcher = GameLauncher(game_dir)
            launcher.prepare_data_folder({
                "Skills.txt": skills_txt,
                "SkillDesc.txt": sdesc_txt,
                "CharStats.txt": cstats_txt,
            })

            # Create D2 save file
            save_dir = get_save_dir()
            if not save_dir:
                save_dir = os.path.join(game_dir, "Save")
            create_character(profile.name, profile.class_name, save_dir)
            self._log(f"Character '{profile.name}' created ({profile.class_name})", "green")

            # Mark profile as randomized
            profile.skills_randomized = True
            profile.last_played = time.strftime("%Y-%m-%d %H:%M:%S")
            self.profile_mgr.save(profile)

            self._launch_game()

        except Exception as e:
            messagebox.showerror("Error", f"Failed to launch:\n{e}")
            self._log(f"ERROR: {e}", "red")
            import traceback
            traceback.print_exc()

    def _launch_game(self):
        """Start Game.exe."""
        try:
            game_dir = self.config.game_dir
            launcher = GameLauncher(game_dir)
            self._log("Launching Diablo II...", "gold")
            self.game_process = launcher.launch(
                windowed=self.active_profile.windowed if self.active_profile else True)
            self.game_status.config(text="\u25cf Launching...", fg=GOLD)
        except Exception as e:
            self._log(f"Launch error: {e}", "red")

    # ─── NEW CHARACTER VIEW (Create) ─────────────────────────────
    def _build_create_view(self, parent):
        frame = tk.Frame(parent, bg=BG_DARK)
        self._view_frames["NEW CHARACTER"] = frame

        inner = tk.Frame(frame, bg=BG_DARK)
        inner.pack(fill=tk.BOTH, expand=True, padx=20, pady=16)

        # ── Character ──
        box1 = tk.Frame(inner, bg=BG_LIGHT, padx=20, pady=14)
        box1.pack(fill=tk.X, pady=(0, 8))

        tk.Label(box1, text="CHARACTER", font=("Segoe UI", 9, "bold"),
                 fg=GOLD, bg=BG_LIGHT).pack(anchor=tk.W, pady=(0, 6))

        r1 = tk.Frame(box1, bg=BG_LIGHT)
        r1.pack(fill=tk.X, pady=2)
        tk.Label(r1, text="Name:", fg=FG_MUTED, bg=BG_LIGHT,
                 font=("Segoe UI", 10)).pack(side=tk.LEFT, padx=(0, 8))
        self.name_var = tk.StringVar(value="")
        tk.Entry(r1, textvariable=self.name_var, width=16, bg=BG_FIELD,
                 fg=FG, insertbackground=FG, font=("Segoe UI", 10),
                 relief=tk.FLAT).pack(side=tk.LEFT, padx=(0, 20), ipady=2)

        tk.Label(r1, text="Class:", fg=FG_MUTED, bg=BG_LIGHT,
                 font=("Segoe UI", 10)).pack(side=tk.LEFT, padx=(0, 8))
        self.class_var = tk.StringVar(value=self.config.last_class)
        ttk.Combobox(r1, textvariable=self.class_var,
                     values=list(CLASS_NAMES.values()),
                     state="readonly", width=14).pack(side=tk.LEFT)

        tk.Label(box1, text="Class is cosmetic only \u2014 all skills are randomized",
                 font=("Segoe UI", 8), fg=FG_DIM, bg=BG_LIGHT).pack(anchor=tk.W, pady=(2, 0))

        # ── Settings ──
        box2 = tk.Frame(inner, bg=BG_LIGHT, padx=20, pady=14)
        box2.pack(fill=tk.X, pady=(0, 8))

        tk.Label(box2, text="SETTINGS", font=("Segoe UI", 9, "bold"),
                 fg=GOLD, bg=BG_LIGHT).pack(anchor=tk.W, pady=(0, 6))

        r2 = tk.Frame(box2, bg=BG_LIGHT)
        r2.pack(fill=tk.X, pady=2)
        tk.Label(r2, text="Game Path:", fg=FG_MUTED, bg=BG_LIGHT,
                 font=("Segoe UI", 10), width=10, anchor=tk.W).pack(side=tk.LEFT)
        self.path_var = tk.StringVar(value=self.config.game_dir)
        tk.Entry(r2, textvariable=self.path_var, width=35, bg=BG_FIELD,
                 fg=FG, insertbackground=FG, font=("Segoe UI", 9),
                 relief=tk.FLAT).pack(side=tk.LEFT, padx=(0, 5), ipady=2)
        ttk.Button(r2, text="...", command=self._browse_path,
                   style="Small.TButton", width=3).pack(side=tk.LEFT)

        r3 = tk.Frame(box2, bg=BG_LIGHT)
        r3.pack(fill=tk.X, pady=2)
        self.windowed_var = tk.BooleanVar(value=self.config.windowed)
        tk.Checkbutton(r3, text="Windowed", variable=self.windowed_var,
                       font=("Segoe UI", 9), fg=FG, bg=BG_LIGHT,
                       selectcolor=BG_FIELD, activebackground=BG_LIGHT,
                       activeforeground=FG).pack(side=tk.LEFT, padx=(0, 20))
        tk.Label(r3, text="Seed:", fg=FG_MUTED, bg=BG_LIGHT,
                 font=("Segoe UI", 10)).pack(side=tk.LEFT, padx=(0, 5))
        self.seed_var = tk.StringVar(value="")
        tk.Entry(r3, textvariable=self.seed_var, width=14, bg=BG_FIELD,
                 fg=FG, insertbackground=FG, font=("Segoe UI", 10),
                 relief=tk.FLAT).pack(side=tk.LEFT, ipady=2)
        tk.Label(r3, text="(blank = random)", font=("Segoe UI", 8),
                 fg=FG_DIM, bg=BG_LIGHT).pack(side=tk.LEFT, padx=6)

        # ── Archipelago (greyed) ──
        box3 = tk.Frame(inner, bg=BG_LIGHT, padx=20, pady=10)
        box3.pack(fill=tk.X, pady=(0, 8))

        ah = tk.Frame(box3, bg=BG_LIGHT)
        ah.pack(fill=tk.X)
        tk.Label(ah, text="ARCHIPELAGO", font=("Segoe UI", 9, "bold"),
                 fg=GOLD_DIM, bg=BG_LIGHT).pack(side=tk.LEFT)
        tk.Label(ah, text="coming soon", font=("Segoe UI", 8),
                 fg=FG_DIM, bg=BG_LIGHT).pack(side=tk.LEFT, padx=8)

        self.ap_entries = {}
        for lbl, attr in [("Server:", "ap_server"), ("Slot:", "ap_slot"),
                           ("Password:", "ap_password")]:
            rr = tk.Frame(box3, bg=BG_LIGHT)
            rr.pack(fill=tk.X, pady=1)
            tk.Label(rr, text=lbl, font=("Segoe UI", 9), fg=FG_DIM,
                     bg=BG_LIGHT, width=10, anchor=tk.W).pack(side=tk.LEFT)
            ent = tk.Entry(rr, width=30, bg=BG_FIELD, fg=FG_DIM,
                           font=("Segoe UI", 9), relief=tk.FLAT, state=tk.DISABLED)
            ent.pack(side=tk.LEFT, padx=5, ipady=1)
            self.ap_entries[attr] = ent

        # ── Create Button ──
        bf = tk.Frame(inner, bg=BG_DARK)
        bf.pack(fill=tk.X, pady=(8, 0))

        self.launch_btn = tk.Button(bf, text="\u25b6  CREATE & LAUNCH",
                                     command=self._on_create_character,
                                     font=("Segoe UI", 12, "bold"),
                                     fg="white", bg=ACCENT,
                                     activebackground=ACCENT_HOVER,
                                     activeforeground="white",
                                     relief=tk.FLAT, cursor="hand2",
                                     pady=10)
        self.launch_btn.pack(fill=tk.X, ipady=4)

    # ─── RIGHT PANEL ───────────────────────────────────────────────
    def _build_right_panel(self):
        right = tk.Frame(self.root, bg=BG_PANEL, width=self.RIGHT_W)
        right.grid(row=1, column=2, sticky="ns")
        right.grid_propagate(False)

        def section(parent, title, pady_top=12):
            tk.Label(parent, text=title, font=("Segoe UI", 9, "bold"),
                     fg=GOLD, bg=BG_PANEL).pack(pady=(pady_top, 4), padx=10, anchor=tk.W)
            tk.Frame(parent, bg=BORDER, height=1).pack(fill=tk.X, padx=10, pady=(0, 6))
            f = tk.Frame(parent, bg=BG_PANEL)
            f.pack(fill=tk.X, padx=10)
            return f

        def stat_row(parent, label, color):
            r = tk.Frame(parent, bg=BG_PANEL)
            r.pack(fill=tk.X, pady=2)
            tk.Label(r, text=label, font=("Segoe UI", 8), fg=FG_DIM,
                     bg=BG_PANEL).pack(side=tk.LEFT)
            lbl = tk.Label(r, text="\u2014", font=("Consolas", 9, "bold"), fg=color,
                     bg=BG_PANEL)
            lbl.pack(side=tk.RIGHT)
            return lbl

        # ── Character Info ──
        cf = section(right, "CHARACTER", 10)
        self.right_char_name = stat_row(cf, "Name", FG)
        self.right_char_class = stat_row(cf, "Class", FG)
        self.right_char_seed = stat_row(cf, "Seed", FG_MUTED)

        # ── Current Area ──
        af = section(right, "CURRENT AREA")
        self.right_area_name = tk.Label(af, text="Not in game",
                 font=("Segoe UI", 10, "bold"), fg=FG_DIM, bg=BG_PANEL)
        self.right_area_name.pack(anchor=tk.W, pady=(0, 4))
        self.right_area_act = stat_row(af, "Act", FG_MUTED)
        self.right_area_kills = stat_row(af, "Kills", GREEN)

        # ── Monsters in Area ──
        mf = section(right, "MONSTERS HERE")
        self.right_monsters_frame = mf

        self.right_monster_labels = []
        for _ in range(7):
            lbl = tk.Label(mf, text="", font=("Segoe UI", 8), fg=FG_DIM,
                           bg=BG_PANEL, anchor=tk.W)
            lbl.pack(fill=tk.X)
            self.right_monster_labels.append(lbl)

        # ── Checks ──
        pf = section(right, "PROGRESS")
        self.right_checks_done = stat_row(pf, "Areas Cleared", GREEN)
        self.right_checks_total = stat_row(pf, "Total Areas", FG_DIM)
        self.right_skills_equipped = stat_row(pf, "Skills Equipped", BLUE)

        spacer = tk.Frame(right, bg=BG_PANEL)
        spacer.pack(fill=tk.BOTH, expand=True)

        self._update_right_panel()

    # ─── BOTTOM BAR ────────────────────────────────────────────────
    def _build_bottom_bar(self):
        bottom = tk.Frame(self.root, bg=BG_DARK, height=self.BOTTOM_H)
        bottom.grid(row=2, column=0, columnspan=3, sticky="ew")
        bottom.grid_propagate(False)

        tk.Frame(bottom, bg=BORDER, height=1).pack(fill=tk.X)

        inner = tk.Frame(bottom, bg=BG_DARK)
        inner.pack(fill=tk.BOTH, expand=True, padx=10, pady=4)

        br = tk.Frame(inner, bg=BG_DARK)
        br.pack(fill=tk.X, pady=(0, 4))

        ttk.Button(br, text="Skill Tree", command=self._on_skill_tree,
                   style="Secondary.TButton").pack(side=tk.LEFT, padx=(0, 4))

        self.game_status = tk.Label(br, text="\u25cf Game not running",
                                     font=("Consolas", 9), fg=FG_DIM, bg=BG_DARK)
        self.game_status.pack(side=tk.RIGHT, padx=8)

        self.log_text = tk.Text(inner, height=3, bg="#080812", fg=FG_MUTED,
                                font=("Consolas", 8), relief=tk.FLAT,
                                state=tk.DISABLED, wrap=tk.WORD,
                                insertbackground=FG_DIM, padx=6, pady=4)
        self.log_text.pack(fill=tk.BOTH, expand=True)

        self.log_text.tag_config("green", foreground=GREEN)
        self.log_text.tag_config("gold", foreground=GOLD)
        self.log_text.tag_config("red", foreground=RED)
        self.log_text.tag_config("dim", foreground=FG_DIM)

        self._log("Ready. Create a character or load an existing one.")

    # ════════════════════════════════════════════════════════════════
    #  HELPERS
    # ════════════════════════════════════════════════════════════════

    def _update_right_panel(self, area_id: int | None = None):
        """Update the right panel with current character/area info."""
        # Character info
        if self.active_profile:
            p = self.active_profile
            self.right_char_name.config(text=p.name)
            self.right_char_class.config(text=p.class_name)
            self.right_char_seed.config(text=str(p.seed) if p.seed is not None else "\u2014")
        else:
            self.right_char_name.config(text="\u2014")
            self.right_char_class.config(text="\u2014")
            self.right_char_seed.config(text="\u2014")

        # Area info
        if area_id is not None:
            area = get_area_map().get(area_id)
            if area and not area.is_town:
                self.right_area_name.config(text=area.display_name, fg=FG)
                self.right_area_act.config(text=str(area.act))

                # Get kill progress and monster info
                total_kills = 0
                if self._kill_tracker:
                    prog = self._kill_tracker.get_progress(area_id)
                    total_kills = prog.kills
                mons = get_area_monsters_display(area_id)
                num_types = max(len(mons), 1)
                total_target = area.kills_per_type * num_types

                # Show kill progress as fraction and color
                done = total_kills >= total_target
                kill_color = GREEN if done else FG
                self.right_area_kills.config(
                    text=f"{total_kills} / {total_target}", fg=kill_color)

                # Show monster names
                for i, lbl in enumerate(self.right_monster_labels):
                    if i < len(mons):
                        _, display = mons[i]
                        lbl.config(text=f"\u2022 {display}", fg=FG_MUTED)
                    else:
                        lbl.config(text="")
            elif area:
                self.right_area_name.config(text=area.display_name, fg=GOLD)
                self.right_area_act.config(text=str(area.act))
                self.right_area_kills.config(text="\u2014")
                for lbl in self.right_monster_labels:
                    lbl.config(text="")
        else:
            self.right_area_name.config(text="Not in game", fg=FG_DIM)
            self.right_area_act.config(text="\u2014")
            self.right_area_kills.config(text="\u2014")
            for lbl in self.right_monster_labels:
                lbl.config(text="")

        # Progress
        if self.pool_manager and self.pool_manager.state.equipped:
            equipped = self.pool_manager.get_equipped()
            self.right_skills_equipped.config(text=str(len(equipped)))
        else:
            self.right_skills_equipped.config(text="\u2014")

        checks_done = 0
        if self._kill_tracker:
            checks_done = self._kill_tracker.state.get_completed_count()
        self.right_checks_done.config(text=str(checks_done))
        self.right_checks_total.config(text="126")

    def _log(self, msg: str, tag: str = "dim"):
        self.log_text.config(state=tk.NORMAL)
        ts = time.strftime("%H:%M:%S")
        self.log_text.insert(tk.END, f"[{ts}] ", "dim")
        self.log_text.insert(tk.END, f"{msg}\n", tag)
        self.log_text.see(tk.END)
        self.log_text.config(state=tk.DISABLED)
        self.file_logger.info(msg)

    def _update_info_label(self):
        if self.active_profile:
            p = self.active_profile
            seed = str(p.seed) if p.seed is not None else "\u2014"
            self.info_label.config(
                text=f"{p.name}  \u2502  {p.class_name}  \u2502  Seed: {seed}")
        else:
            self.info_label.config(text="No character loaded")

    def _on_close(self):
        self._closing = True

        # Cancel game monitor loop
        if self._monitor_id:
            self.root.after_cancel(self._monitor_id)
            self._monitor_id = None

        # Clean up skill tree editor
        if self.skill_tree_view:
            self.skill_tree_view.cleanup()
            self.skill_tree_view.destroy()
            self.skill_tree_view = None

        # Clean up memory reader and save kill tracker
        self._memory_reader.detach()
        if self._kill_tracker:
            self._kill_tracker.save()

        # Restore backed-up saves before closing
        save_dir = get_save_dir()
        if save_dir:
            isolator = SaveGameIsolator(self.base_dir, save_dir)
            isolator.restore_all()

        if self.game_process:
            try:
                self.game_process.terminate()
                self.game_process.wait(timeout=3)
            except Exception:
                try:
                    self.game_process.kill()
                except Exception:
                    pass
        if self.game_hwnd and user32.IsWindow(self.game_hwnd):
            WM_CLOSE = 0x0010
            user32.PostMessageW(self.game_hwnd, WM_CLOSE, 0, 0)
        self.root.destroy()

    def _browse_path(self):
        path = filedialog.askdirectory(initialdir=self.config.game_dir)
        if path:
            self.path_var.set(path)
            self.config.game_dir = path
            self.config.save(self.config_path)

    def _save_config(self):
        self.config.game_dir = self.path_var.get()
        self.config.mode = "standalone"
        self.config.last_class = self.class_var.get()
        self.config.last_seed = self.seed_var.get()
        self.config.windowed = self.windowed_var.get()
        self.config.save(self.config_path)

    # ════════════════════════════════════════════════════════════════
    #  RESIZE + GAME EMBEDDING
    # ════════════════════════════════════════════════════════════════

    def _on_resize(self, event):
        if event.widget != self.root:
            return
        if self.game_embedded and self.game_hwnd:
            self._reposition_game()

    def _on_launcher_minimize(self, event):
        if event.widget == self.root and self.game_embedded and self.game_hwnd:
            if user32.IsWindow(self.game_hwnd):
                user32.ShowWindow(self.game_hwnd, SW_HIDE)

    def _on_launcher_restore(self, event):
        if event.widget == self.root and self.game_embedded and self.game_hwnd:
            if user32.IsWindow(self.game_hwnd):
                user32.ShowWindow(self.game_hwnd, SW_SHOWNOACTIVATE)
                self._reposition_game()
                user32.SetWindowPos(self.game_hwnd, HWND_TOP, 0, 0, 0, 0,
                                    SWP_NOMOVE | SWP_NOSIZE)

    def _reposition_game(self):
        if not self.game_hwnd or not user32.IsWindow(self.game_hwnd):
            return
        self.root.update_idletasks()
        x = self.game_container.winfo_rootx()
        y = self.game_container.winfo_rooty()
        w = self.game_container.winfo_width()
        h = self.game_container.winfo_height()
        if w > 10 and h > 10:
            user32.MoveWindow(self.game_hwnd, x, y, w, h, True)

    def _focus_game(self, event=None):
        if self.game_hwnd and user32.IsWindow(self.game_hwnd):
            user32.SetForegroundWindow(self.game_hwnd)

    def _on_launcher_focus(self, event):
        if event.widget != self.root:
            return
        if self.game_embedded and self.game_hwnd and user32.IsWindow(self.game_hwnd):
            user32.SetWindowPos(self.game_hwnd, HWND_TOP, 0, 0, 0, 0,
                                SWP_NOMOVE | SWP_NOSIZE)

    def _start_game_monitor(self):
        if self._closing:
            return
        self._check_game()
        self._monitor_id = self.root.after(1000, self._start_game_monitor)

    def _check_game(self):
        if self.game_embedded and self.game_hwnd:
            if not user32.IsWindow(self.game_hwnd):
                self.game_embedded = False
                self.game_hwnd = None
                self.game_process = None
                self._memory_reader.detach()
                self._current_area_id = None
                self._last_xp = None
                if self._kill_tracker:
                    self._kill_tracker.save()
                self.placeholder.place(relx=0, rely=0, relwidth=1, relheight=1)
                self.game_status.config(text="\u25cf Game closed", fg=RED)
                self._log("Game window closed.", "red")
                self._update_right_panel()
                return
            self._reposition_game()
            fg = user32.GetForegroundWindow()
            if fg == self.launcher_hwnd or fg == self.game_hwnd:
                user32.SetWindowPos(self.game_hwnd, HWND_TOP, 0, 0, 0, 0,
                                    SWP_NOMOVE | SWP_NOSIZE)

            # Read player's current area from memory
            self._poll_player_area()

        if not self.game_embedded and self.game_process:
            hwnd = self._find_d2_window()
            if hwnd:
                self._embed_game(hwnd)

    def _poll_player_area(self):
        """Read current area and XP from D2 memory, detect kills, update UI."""
        if not self._memory_reader.is_attached():
            # Try to attach using the game process PID
            if self.game_process and self.game_process.pid:
                pid = find_d2_pid()
                if not pid:
                    self._log("Could not find Game.exe PID.", "red")
                    return
                self._log(f"Found Game.exe PID: {pid}", "dim")
                # Pass the Popen handle to avoid ACCESS_DENIED from OpenProcess
                popen_handle = getattr(self.game_process, '_handle', None)
                if self._memory_reader.attach(pid, existing_handle=popen_handle):
                    self._log(f"Memory attached. Base: 0x{self._memory_reader._base_address:X}", "green")
                    if self._memory_reader._attach_error:
                        self._log(f"[MEM] {self._memory_reader._attach_error}", "dim")
                    # Initialize XP baseline
                    self._last_xp = self._memory_reader.get_experience()
                    self._log(f"Initial XP: {self._last_xp}", "dim")
                else:
                    self._log(f"Memory attach failed: {self._memory_reader._attach_error}", "red")
                    return
            else:
                return

        # Read current area
        area_id = self._memory_reader.get_current_area()

        # If area read fails, retry scan every ~5 seconds
        # (player might not have been in-game during initial attach)
        if area_id is None and self._memory_reader.is_attached():
            self._kill_save_counter += 1
            if self._kill_save_counter % 5 == 0:  # Every ~5 polls
                found = self._memory_reader.scan_for_player_unit()
                if found is not None and found != self._memory_reader._player_unit_offset:
                    self._memory_reader._player_unit_offset = found
                    self._log(f"[MEM] Found player unit at 0x{found:X}", "green")
                    area_id = self._memory_reader.get_current_area()
                    if area_id is not None:
                        self._last_xp = self._memory_reader.get_experience()

        # Detect kills via XP changes
        current_xp = self._memory_reader.get_experience()
        if (current_xp is not None and self._last_xp is not None
                and current_xp > self._last_xp
                and self._current_area_id is not None
                and self._kill_tracker):
            # XP increased — register a kill in the current area
            area_map = get_area_map()
            area = area_map.get(self._current_area_id)
            mons = get_area_monsters_display(self._current_area_id)
            num_types = max(len(mons), 1)
            total_target = (area.kills_per_type * num_types) if area else 0

            if total_target > 0:
                prog, newly_completed = self._kill_tracker.record_kill(
                    self._current_area_id, total_target)
                if newly_completed:
                    area_name = area.display_name if area else f"Area {self._current_area_id}"
                    self._log(
                        f"CHECK: {area_name} complete! ({prog.kills}/{total_target})",
                        "green")
                    self._kill_tracker.save()
                    if self._left_active_tab == "PROGRESS":
                        self._populate_progress()

        if current_xp is not None:
            self._last_xp = current_xp

        # Auto-save kill tracker every ~30 seconds (30 polls at 1s)
        if self._kill_tracker:
            self._kill_save_counter += 1
            if self._kill_save_counter >= 30:
                self._kill_save_counter = 0
                self._kill_tracker.save()

        # Update area display
        if area_id is not None and area_id != self._current_area_id:
            # Log stat debug info on first successful area read
            if self._current_area_id is None:
                for line in self._memory_reader.debug_stats():
                    self._log(f"[STAT] {line}", "dim")
            self._current_area_id = area_id
            area_map = get_area_map()
            area = area_map.get(area_id)
            if area:
                self._log(f"Entered: {area.display_name} (Act {area.act})", "gold")
            self._update_right_panel(area_id)
            if self._left_active_tab == "PROGRESS":
                self._populate_progress()
        elif area_id is not None:
            # Same area — refresh kills display
            self._update_right_panel(area_id)

    def _find_d2_window(self):
        for title in ["Diablo II", "Diablo II - Lord of Destruction"]:
            hwnd = user32.FindWindowW(None, title)
            if hwnd:
                return hwnd
        return None

    def _embed_game(self, hwnd):
        try:
            self.game_hwnd = hwnd

            tk_hwnd = self.root.winfo_id()
            self.launcher_hwnd = user32.GetAncestor(tk_hwnd, GA_ROOT)

            style = user32.GetWindowLongW(hwnd, GWL_STYLE)
            style &= ~(WS_CAPTION | WS_THICKFRAME)
            user32.SetWindowLongW(hwnd, GWL_STYLE, style)

            user32.SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                                SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE)

            self._reposition_game()

            self.game_container.bind("<Button-1>", self._focus_game)
            self.root.bind("<FocusIn>", self._on_launcher_focus)

            # Hide placeholder and skill tree editor if open
            self.placeholder.place_forget()
            if self.skill_tree_view:
                self.skill_tree_view.cleanup()
                self.skill_tree_view.place_forget()
                self.skill_tree_view.destroy()
                self.skill_tree_view = None
            self.game_embedded = True
            self.game_status.config(text="\u25cf Game running", fg=GREEN)
            self._log("Game embedded in launcher!", "green")

            user32.SetForegroundWindow(hwnd)

        except Exception as e:
            self._log(f"Embed failed: {e}", "red")

    # ════════════════════════════════════════════════════════════════
    #  CHARACTER ACTIONS
    # ════════════════════════════════════════════════════════════════

    def _on_create_character(self):
        """Create a new character profile and launch the game."""
        self._save_config()
        char_name = self.name_var.get().strip()

        if not char_name or len(char_name) < 2:
            messagebox.showerror("Error", "Character name must be at least 2 characters.")
            return

        # D2 character name rules: 2-15 chars, ASCII only
        if len(char_name) > 15:
            messagebox.showerror("Error", "Character name must be 15 characters or less.")
            return

        if not all(c.isalpha() or c in "_-" for c in char_name):
            messagebox.showerror("Error", "Name can only contain letters, hyphens, and underscores.")
            return

        # Uniqueness check
        if self.profile_mgr.exists(char_name):
            messagebox.showerror("Error",
                f"Character '{char_name}' already exists.\n"
                f"Choose a different name or load the existing character.")
            return

        game_dir = self.path_var.get()
        if not os.path.exists(os.path.join(game_dir, "Game.exe")):
            messagebox.showerror("Error", f"Game.exe not found in:\n{game_dir}")
            return

        # Generate seed
        seed_str = self.seed_var.get().strip()
        if seed_str:
            try:
                seed = int(seed_str)
            except ValueError:
                seed = hash(seed_str) & 0x7FFFFFFF
        else:
            seed = random.randint(0, 2**31)

        class_name = self.class_var.get()
        class_code = CLASS_CODE_FROM_NAME.get(class_name, "ama")

        # Create profile
        from datetime import datetime
        profile = CharacterProfile(
            name=char_name,
            class_name=class_name,
            class_code=class_code,
            seed=seed,
            windowed=self.windowed_var.get(),
            mode="standalone",
            created_at=time.strftime("%Y-%m-%d %H:%M:%S"),
        )
        self.profile_mgr.save(profile)
        self._log(f"Profile created: {char_name} ({class_name}, seed: {seed})", "green")

        # Set up pool manager for this character
        state_path = self.profile_mgr.get_state_path(char_name)
        self.pool_manager = SkillPoolManager(state_path)

        # Launch
        self._load_and_launch(profile)

    # ════════════════════════════════════════════════════════════════
    #  SKILL TREE (with game exit flow)
    # ════════════════════════════════════════════════════════════════

    def _on_skill_tree(self):
        """Open skill tree editor. If game is running, ask to save & exit first."""
        if not self.active_profile:
            messagebox.showinfo("No Character", "Load or create a character first.")
            return

        if not self.pool_manager:
            messagebox.showinfo("No Skills", "No skill data loaded. Launch the game first.")
            return

        if self.game_embedded and self.game_hwnd and user32.IsWindow(self.game_hwnd):
            # Game is running — need to exit first
            result = messagebox.askyesnocancel(
                "Game Running",
                "The game needs to save and exit before editing skills.\n\n"
                "Save your game first, then click Yes to close it.\n\n"
                "Yes = Close game & open skill editor\n"
                "No = Cancel",
            )
            if result is None or result is False:
                return

            # Close the game
            self._log("Closing game for skill editing...", "gold")
            WM_CLOSE = 0x0010
            user32.PostMessageW(self.game_hwnd, WM_CLOSE, 0, 0)

            # Wait for game to close, then open editor
            self._wait_for_game_close_then_edit()
            return

        # Game not running — open editor directly
        self._open_skill_tree_editor()

    def _wait_for_game_close_then_edit(self):
        """Poll until game window closes, then open skill editor."""
        if self.game_hwnd and user32.IsWindow(self.game_hwnd):
            self.root.after(500, self._wait_for_game_close_then_edit)
            return

        # Game closed
        self.game_embedded = False
        self.game_hwnd = None
        self.game_process = None
        self._memory_reader.detach()
        self._current_area_id = None
        self._last_xp = None
        if self._kill_tracker:
            self._kill_tracker.save()
        self.placeholder.place(relx=0, rely=0, relwidth=1, relheight=1)
        self.game_status.config(text="\u25cf Game closed", fg=RED)
        self._log("Game closed. Opening skill editor...", "gold")

        self._open_skill_tree_editor()

    def _open_skill_tree_editor(self):
        """Show skill tree editor embedded in the game container area."""
        from .skill_tree_view import SkillTreeView
        vanilla_dir = os.path.normpath(VANILLA_TXT_DIR)
        game_dir = self.config.game_dir

        # Remove any existing skill tree view
        if self.skill_tree_view:
            self.skill_tree_view.cleanup()
            self.skill_tree_view.destroy()
            self.skill_tree_view = None

        def on_save(tree_skills):
            self._apply_tree_changes(tree_skills)
            # Remove skill tree view after save
            if self.skill_tree_view:
                self.skill_tree_view.cleanup()
                self.skill_tree_view.place_forget()
                self.skill_tree_view.destroy()
                self.skill_tree_view = None
            # Show placeholder again
            self.placeholder.place(relx=0, rely=0, relwidth=1, relheight=1)

        self.skill_tree_view = SkillTreeView(
            self.game_container,
            self.pool_manager,
            os.path.join(vanilla_dir, "Skills.txt"),
            os.path.join(vanilla_dir, "SkillDesc.txt"),
            game_dir,
            on_save_callback=on_save,
        )
        # Hide placeholder, show skill tree in the game area
        self.placeholder.place_forget()
        self.skill_tree_view.place(relx=0, rely=0, relwidth=1, relheight=1)

    def _apply_tree_changes(self, tree_skills: list):
        """Apply skill tree changes from the visual editor to game files."""
        try:
            vanilla_dir = os.path.normpath(VANILLA_TXT_DIR)
            game_dir = self.config.game_dir

            if not self.active_profile:
                return
            target_class = self.active_profile.class_code

            # Load fresh vanilla data
            skills_txt = D2TxtFile.from_file(os.path.join(vanilla_dir, "Skills.txt"))
            sdesc_txt = D2TxtFile.from_file(os.path.join(vanilla_dir, "SkillDesc.txt"))
            cstats_txt = D2TxtFile.from_file(os.path.join(vanilla_dir, "CharStats.txt"))

            # Re-merge icons
            vanilla_skills_for_icons = D2TxtFile.from_file(
                os.path.join(vanilla_dir, "Skills.txt"))
            merge_and_deploy(sdesc_txt, vanilla_skills_for_icons, game_dir)

            all_skills = get_all_class_skills(skills_txt)
            skill_by_name = {s.skill_name: s for s in all_skills}

            equipped_names = [n for n in tree_skills if n is not None]
            equipped_entries = [skill_by_name[n] for n in equipped_names if n in skill_by_name]

            equipped_set = {s.row_index for s in equipped_entries}
            for skill in equipped_entries:
                skills_txt.set_value(skill.row_index, "charclass", target_class)
                skills_txt.set_value(skill.row_index, "reqlevel", "1")
                skills_txt.set_value(skill.row_index, "reqskill1", "")
                skills_txt.set_value(skill.row_index, "reqskill2", "")
                skills_txt.set_value(skill.row_index, "reqskill3", "")

            from ..core.skill_randomizer import CLASS_CODES, get_skills_by_class
            by_class = get_skills_by_class(all_skills)
            spare = [s for s in by_class[target_class] if s.row_index not in equipped_set]
            spare_idx = 0
            for cc in CLASS_CODES:
                if cc == target_class:
                    continue
                taken = [s for s in by_class[cc] if s.row_index in equipped_set]
                for _ in taken:
                    if spare_idx < len(spare):
                        skills_txt.set_value(spare[spare_idx].row_index, "charclass", cc)
                        spare_idx += 1

            from ..core.skill_tiers import TAB_LAYOUT
            sd_name_to_row = {}
            for ri in range(len(sdesc_txt.rows)):
                name = sdesc_txt.get_value(ri, "skilldesc")
                if name:
                    sd_name_to_row[name] = ri

            for ri in range(len(sdesc_txt.rows)):
                pg = sdesc_txt.get_value(ri, "SkillPage")
                if pg and pg != "0":
                    sdesc_txt.set_value(ri, "SkillPage", "0")

            for idx, skill_name in enumerate(tree_skills):
                if skill_name is None:
                    continue
                skill = skill_by_name.get(skill_name)
                if not skill:
                    continue
                sd_row = sd_name_to_row.get(skill.skilldesc_ref)
                if sd_row is None:
                    continue

                tab = idx // 10
                slot = idx % 10
                if slot < len(TAB_LAYOUT):
                    row, col, tier = TAB_LAYOUT[slot]
                    page = tab + 1
                    d2_row = {0: 1, 1: 3, 2: 5, 3: 6}[row]
                    d2_col = col + 1
                    sdesc_txt.set_value(sd_row, "SkillPage", str(page))
                    sdesc_txt.set_value(sd_row, "SkillRow", str(d2_row))
                    sdesc_txt.set_value(sd_row, "SkillColumn", str(d2_col))
                    sdesc_txt.set_value(sd_row, "ListRow", str(page))

            if equipped_names:
                from ..core.skill_randomizer import _update_charstats
                _update_charstats(cstats_txt, target_class, equipped_names[0])

            from ..core.game_launcher import GameLauncher
            launcher = GameLauncher(game_dir)
            launcher.prepare_data_folder({
                "Skills.txt": skills_txt,
                "SkillDesc.txt": sdesc_txt,
                "CharStats.txt": cstats_txt,
            })

            self._log("Skill tree changes applied!", "green")
            self._populate_skills()
            self._update_right_panel()

            # Ask to relaunch
            if messagebox.askyesno("Changes Applied",
                    "Skill tree changes saved!\n\nLaunch the game now?"):
                self._launch_game()

        except Exception as e:
            self._log(f"Error applying changes: {e}", "red")
            import traceback
            traceback.print_exc()
