"""Visual skill tree editor with drag & drop.

Shows 3 skill tree tabs side by side, each with 10 tier-labeled slots.
An available skills pool below allows dragging skills into matching
tier slots. Save button applies changes to game files.

Layout per tab (10 slots):
  Row 0:  [T1] --- [T1]       (2 beginner slots)
  Row 1:  [T2] [T2] [T2]      (3 intermediate slots)
  Row 2:  [T3] [T3] [T3]      (3 advanced slots)
  Row 3:  [T4] --- [T4]       (2 endgame slots)
"""

import os
import tkinter as tk
from tkinter import ttk
from PIL import Image, ImageTk
from ..core.skill_tiers import (
    TAB_LAYOUT, TIER_LABELS, TIER_NAMES, TIER_COLORS, TIER_COLORS_DIM,
    build_tier_map, get_slot_tier,
)
from ..core.icon_extractor import build_skill_icon_map, create_empty_slot_icon
from ..core.d2_data import D2TxtFile
from ..core.skill_pool import SkillPoolManager
from ..core.skill_randomizer import CLASS_CODES, CLASS_NAMES, get_all_class_skills

# -- Theme colors (match launcher) --
BG_DARK = "#0d0d1a"
BG = "#1a1a2e"
BG_LIGHT = "#16213e"
BG_PANEL = "#111128"
FG = "#e0e0e0"
FG_DIM = "#666680"
FG_MUTED = "#888899"
GOLD = "#d4a017"
GOLD_DIM = "#8a6b10"
GREEN = "#4ecca3"
RED = "#e94560"
BORDER = "#2a2a4a"
ACCENT = "#e94560"

CLASS_COLORS = {
    "ama": "#f0c040", "sor": "#6090f0", "nec": "#60d060",
    "pal": "#f0f090", "bar": "#f08040", "dru": "#80c060",
    "ass": "#d060d0",
}

SLOT_SIZE = 52       # Pixel size of each slot
ICON_SIZE = 40       # Icon image size
SLOT_PAD = 6         # Padding between slots
TAB_NAMES = ["Tab 1", "Tab 2", "Tab 3"]


class SkillTreeView(tk.Frame):
    """Visual skill tree editor — embeds in the game container area."""

    def __init__(
        self,
        parent,
        pool_manager: SkillPoolManager,
        vanilla_skills_path: str,
        vanilla_sdesc_path: str,
        game_dir: str,
        on_save_callback=None,
    ):
        super().__init__(parent, bg=BG_DARK)
        self.configure(bg=BG_DARK)

        self.pool_manager = pool_manager
        self.game_dir = game_dir
        self.on_save_callback = on_save_callback
        self.vanilla_skills_path = vanilla_skills_path
        self.vanilla_sdesc_path = vanilla_sdesc_path

        # Load vanilla data for tier/icon mapping
        self.vanilla_skills = D2TxtFile.from_file(vanilla_skills_path)
        self.vanilla_sdesc = D2TxtFile.from_file(vanilla_sdesc_path)
        self.tier_map = build_tier_map(self.vanilla_skills)

        # Build complete skill lookup: name -> {class, skilldesc, skill_id, row_index}
        self._build_skill_lookup()

        # Load skill icons
        self.skill_icons: dict[str, Image.Image] = {}
        self.tk_icons: dict[str, ImageTk.PhotoImage] = {}
        self._load_icons()

        # State: 3 tabs × 10 slots = 30 slots, each can hold a skill name or None
        self.tree_slots: list[list[str | None]] = [
            [None] * 10 for _ in range(3)
        ]
        self._load_current_state()

        # Available skills (not in tree)
        self.available_skills: list[str] = []
        self._refresh_available()

        # Drag state
        self._drag_skill: str | None = None
        self._drag_source: tuple[int, int] | None = None
        self._drag_label: tk.Label | None = None

        # Tooltip state
        self._tooltip: tk.Toplevel | None = None

        # Filter state
        self._tier_filter = 0  # 0 = all, 1-4 = specific tier

        # Build UI
        self._build_ui()
        self._refresh_tree_display()
        self._refresh_pool_display()

    def cleanup(self):
        """Release resources before destruction."""
        # Remove any lingering global mousewheel binding
        if hasattr(self, '_pool_wheel_id') and self._pool_wheel_id:
            try:
                self.pool_canvas.unbind_all("<MouseWheel>")
            except Exception:
                pass
            self._pool_wheel_id = None

        # Destroy tooltip if open
        if hasattr(self, '_tooltip'):
            self._hide_tooltip()

        # Cancel any drag
        if hasattr(self, '_drag_label') and self._drag_label:
            try:
                self._drag_label.destroy()
            except Exception:
                pass
            self._drag_label = None
            self._drag_skill = None
            self._drag_source = None

        # Clear icon references to free memory
        if hasattr(self, 'tk_icons'):
            self.tk_icons.clear()
        if hasattr(self, '_empty_icons'):
            self._empty_icons = {}
        if hasattr(self, 'skill_icons'):
            self.skill_icons.clear()

    def _build_skill_lookup(self):
        """Build lookup maps from vanilla data for all 210 class skills."""
        self.skill_info: dict[str, dict] = {}  # name -> info dict
        self.skilldesc_to_name: dict[str, str] = {}  # skilldesc -> skill name

        for ri in range(len(self.vanilla_skills.rows)):
            cc = self.vanilla_skills.get_value(ri, "charclass").strip()
            if cc not in CLASS_CODES:
                continue
            name = self.vanilla_skills.get_value(ri, "skill").strip()
            sid = self.vanilla_skills.get_value(ri, "Id").strip()
            sd = self.vanilla_skills.get_value(ri, "skilldesc").strip()
            if name:
                self.skill_info[name] = {
                    "original_class": cc,
                    "skill_id": int(sid) if sid else 0,
                    "skilldesc_ref": sd,
                    "row_index": ri,
                }
                if sd:
                    self.skilldesc_to_name[sd] = name

    def _load_icons(self):
        """Load all skill icons into memory."""
        self.skill_icons = build_skill_icon_map(
            self.vanilla_skills, self.vanilla_sdesc, ICON_SIZE
        )
        for name, pil_img in self.skill_icons.items():
            self.tk_icons[name] = ImageTk.PhotoImage(pil_img)

        # Create empty slot icons for each tier
        self._empty_icons = {}
        for tier in range(1, 5):
            pil = create_empty_slot_icon(ICON_SIZE, tier)
            self._empty_icons[tier] = ImageTk.PhotoImage(pil)

    def _load_current_state(self):
        """Load tree state from output game files (ground truth).

        Falls back to pool_manager if no output files exist.
        """
        # Try reading from output files first (these ARE what's in the game)
        excel_dir = os.path.join(self.game_dir, "data", "global", "excel")
        skills_path = os.path.join(excel_dir, "Skills.txt")
        sdesc_path = os.path.join(excel_dir, "SkillDesc.txt")

        loaded_from_files = False
        if os.path.exists(skills_path) and os.path.exists(sdesc_path):
            try:
                self._sync_from_output_files(skills_path, sdesc_path)
                loaded_from_files = True
                count = sum(1 for t in range(3) for s in range(10)
                            if self.tree_slots[t][s])
                print(f"Tree editor: loaded {count} skills from output files")
            except Exception as e:
                print(f"Could not read output files: {e}")
                import traceback
                traceback.print_exc()

        # Fall back to pool_manager if no output files
        if not loaded_from_files:
            equipped = self.pool_manager.get_equipped()
            for eq in equipped:
                tab = eq.slot_index // 10
                slot = eq.slot_index % 10
                if 0 <= tab < 3 and 0 <= slot < 10:
                    self.tree_slots[tab][slot] = eq.skill_name
            print("Tree editor: loaded from pool_manager (no output files)")

    def _sync_from_output_files(self, skills_path: str, sdesc_path: str):
        """Read output game files and sync tree_slots to match what's in-game."""
        out_sdesc = D2TxtFile.from_file(sdesc_path)

        # D2 position → our grid position reverse mapping
        # Our save mapping: row 0→D2row1, row 1→D2row3, row 2→D2row5, row 3→D2row6
        d2row_to_our = {1: 0, 2: 0, 3: 1, 4: 1, 5: 2, 6: 3}

        # Find ALL visible skills in output SkillDesc
        visible_skills = []
        for ri in range(len(out_sdesc.rows)):
            pg = out_sdesc.get_value(ri, "SkillPage").strip()
            if not pg or pg == "0":
                continue
            sd_name = out_sdesc.get_value(ri, "skilldesc").strip()
            d2_row = out_sdesc.get_value(ri, "SkillRow").strip()
            d2_col = out_sdesc.get_value(ri, "SkillColumn").strip()
            if sd_name and d2_row and d2_col:
                visible_skills.append((sd_name, int(pg), int(d2_row), int(d2_col)))

        if not visible_skills:
            return

        # Clear tree and rebuild from output files
        self.tree_slots = [[None] * 10 for _ in range(3)]

        placed = 0
        for sd_name, page, d2_row, d2_col in visible_skills:
            skill_name = self.skilldesc_to_name.get(sd_name)
            if not skill_name:
                print(f"  Warning: unknown skilldesc '{sd_name}'")
                continue

            tab = page - 1  # page 1-3 → tab 0-2
            if not (0 <= tab < 3):
                continue

            our_row = d2row_to_our.get(d2_row, -1)
            our_col = d2_col - 1  # 1-based → 0-based

            if our_row < 0:
                # Try nearest row mapping for non-standard positions
                our_row = max(0, min(3, (d2_row - 1) // 2))

            # Find matching slot in TAB_LAYOUT
            matched = False
            for slot_idx, (sr, sc, tier) in enumerate(TAB_LAYOUT):
                if sr == our_row and sc == our_col:
                    self.tree_slots[tab][slot_idx] = skill_name
                    placed += 1
                    matched = True
                    break

            if not matched:
                # Find first empty slot in this tab with matching row
                for slot_idx, (sr, sc, tier) in enumerate(TAB_LAYOUT):
                    if sr == our_row and self.tree_slots[tab][slot_idx] is None:
                        self.tree_slots[tab][slot_idx] = skill_name
                        placed += 1
                        matched = True
                        break

            if not matched:
                # Last resort: find ANY empty slot in this tab
                for slot_idx in range(10):
                    if self.tree_slots[tab][slot_idx] is None:
                        self.tree_slots[tab][slot_idx] = skill_name
                        placed += 1
                        break

        print(f"Tree editor: synced {placed}/{len(visible_skills)} visible skills from game files")

    def _refresh_available(self):
        """Rebuild available skills list: all skills NOT in tree."""
        in_tree = set()
        for tab in range(3):
            for slot in range(10):
                name = self.tree_slots[tab][slot]
                if name:
                    in_tree.add(name)

        # All 210 skills minus what's in the tree
        all_names = sorted(self.skill_info.keys())
        self.available_skills = [n for n in all_names if n not in in_tree]

    def _build_ui(self):
        """Build the complete UI."""
        # Title
        title = tk.Label(
            self, text="SKILL TREE EDITOR", font=("Segoe UI", 14, "bold"),
            fg=GOLD, bg=BG_DARK,
        )
        title.pack(pady=(8, 2))

        # Skill count
        count = sum(
            1 for t in range(3) for s in range(10) if self.tree_slots[t][s]
        )
        self.tree_count_label = tk.Label(
            self, text=f"{count} / 30 skills placed",
            font=("Segoe UI", 9), fg=FG_DIM, bg=BG_DARK,
        )
        self.tree_count_label.pack(pady=(0, 6))

        # Main container
        main = tk.Frame(self, bg=BG_DARK)
        main.pack(fill="both", expand=True, padx=8, pady=4)

        # Top section: 3 skill tree tabs side by side
        tree_frame = tk.Frame(main, bg=BG_DARK)
        tree_frame.pack(fill="x", pady=(0, 6))

        self.slot_widgets: list[list[tk.Frame]] = []

        for tab_idx in range(3):
            tab_container = tk.Frame(tree_frame, bg=BG_PANEL, bd=1, relief="solid")
            tab_container.pack(side="left", fill="both", expand=True, padx=3)

            # Tab header
            hdr = tk.Label(
                tab_container, text=TAB_NAMES[tab_idx],
                font=("Segoe UI", 10, "bold"), fg=GOLD, bg=BG_PANEL,
            )
            hdr.pack(pady=(4, 2))

            # Grid of slots
            grid = tk.Frame(tab_container, bg=BG_PANEL)
            grid.pack(padx=8, pady=(0, 8))

            tab_slots = []
            for slot_idx, (row, col, tier) in enumerate(TAB_LAYOUT):
                slot_frame = self._create_slot(grid, tab_idx, slot_idx, tier)
                slot_frame.grid(
                    row=row, column=col,
                    padx=SLOT_PAD // 2, pady=SLOT_PAD // 2,
                )
                tab_slots.append(slot_frame)

            self.slot_widgets.append(tab_slots)

        # Separator
        sep = tk.Frame(main, bg=BORDER, height=2)
        sep.pack(fill="x", pady=4)

        # Pool section header
        pool_header = tk.Frame(main, bg=BG_DARK)
        pool_header.pack(fill="x")

        tk.Label(
            pool_header, text="AVAILABLE SKILLS",
            font=("Segoe UI", 10, "bold"), fg=GOLD, bg=BG_DARK,
        ).pack(side="left")

        self.pool_count_label = tk.Label(
            pool_header, text="(0)", font=("Segoe UI", 9), fg=FG_DIM, bg=BG_DARK,
        )
        self.pool_count_label.pack(side="left", padx=4)

        # Tier filter buttons
        filter_frame = tk.Frame(pool_header, bg=BG_DARK)
        filter_frame.pack(side="right")

        self._filter_buttons = {}
        btn_all = tk.Label(
            filter_frame, text="ALL", font=("Segoe UI", 8, "bold"),
            fg=FG, bg=BG_LIGHT, padx=6, pady=1, cursor="hand2",
        )
        btn_all.pack(side="left", padx=1)
        btn_all.bind("<Button-1>", lambda e: self._set_filter(0))
        self._filter_buttons[0] = btn_all

        for tier in range(1, 5):
            color = TIER_COLORS[tier]
            btn = tk.Label(
                filter_frame, text=TIER_LABELS[tier],
                font=("Segoe UI", 8, "bold"),
                fg=color, bg=BG_LIGHT, padx=6, pady=1, cursor="hand2",
            )
            btn.pack(side="left", padx=1)
            btn.bind("<Button-1>", lambda e, t=tier: self._set_filter(t))
            self._filter_buttons[tier] = btn

        # Pool canvas with scrollbar
        pool_outer = tk.Frame(main, bg=BG_DARK)
        pool_outer.pack(fill="both", expand=True, pady=(4, 0))

        self.pool_canvas = tk.Canvas(
            pool_outer, bg=BG_DARK, highlightthickness=0, height=200,
        )
        self.pool_scroll = tk.Scrollbar(
            pool_outer, orient="vertical", command=self.pool_canvas.yview,
        )
        self.pool_canvas.configure(yscrollcommand=self.pool_scroll.set)

        self.pool_scroll.pack(side="right", fill="y")
        self.pool_canvas.pack(side="left", fill="both", expand=True)

        self.pool_inner = tk.Frame(self.pool_canvas, bg=BG_DARK)
        self.pool_canvas_window = self.pool_canvas.create_window(
            (0, 0), window=self.pool_inner, anchor="nw",
        )
        self.pool_inner.bind("<Configure>", self._on_pool_configure)
        self.pool_canvas.bind("<Configure>", self._on_pool_canvas_configure)

        # Mousewheel: only active when hovering over pool area
        self._pool_wheel_id = None
        def _pool_enter(e):
            self._pool_wheel_id = self.pool_canvas.bind_all("<MouseWheel>", self._on_pool_mousewheel)
        def _pool_leave(e):
            if self._pool_wheel_id:
                self.pool_canvas.unbind_all("<MouseWheel>")
                self._pool_wheel_id = None
        self.pool_canvas.bind("<Enter>", _pool_enter)
        self.pool_canvas.bind("<Leave>", _pool_leave)
        self._pool_leave_fn = _pool_leave  # Store for cleanup

        # Bottom bar: Save button
        bottom = tk.Frame(self, bg=BG_DARK)
        bottom.pack(fill="x", padx=8, pady=8)

        self.save_btn = tk.Button(
            bottom, text="SAVE & APPLY", font=("Segoe UI", 11, "bold"),
            fg="white", bg=ACCENT, activebackground="#ff6b81",
            padx=20, pady=6, cursor="hand2", command=self._on_save,
        )
        self.save_btn.pack(side="right")

        self.status_label = tk.Label(
            bottom, text="", font=("Segoe UI", 9), fg=GREEN, bg=BG_DARK,
        )
        self.status_label.pack(side="left", fill="x", expand=True)

    def _create_slot(self, parent, tab_idx: int, slot_idx: int, tier: int) -> tk.Frame:
        """Create a single skill slot widget."""
        color = TIER_COLORS[tier]
        dim_color = TIER_COLORS_DIM[tier]

        frame = tk.Frame(
            parent, bg=dim_color, bd=1, relief="solid",
            width=SLOT_SIZE + 14, height=SLOT_SIZE + 24,
            highlightbackground=color, highlightthickness=1,
        )
        frame.pack_propagate(False)

        # Tier label
        tier_lbl = tk.Label(
            frame, text=TIER_LABELS[tier],
            font=("Segoe UI", 7, "bold"), fg=color, bg=dim_color,
        )
        tier_lbl.pack(pady=(1, 0))

        # Icon area
        icon_lbl = tk.Label(
            frame, bg=dim_color, width=ICON_SIZE, height=ICON_SIZE,
            image=self._empty_icons.get(tier),
        )
        icon_lbl.pack()

        # Skill name (truncated)
        name_lbl = tk.Label(
            frame, text="Empty", font=("Segoe UI", 6),
            fg=FG_DIM, bg=dim_color, wraplength=SLOT_SIZE + 10,
        )
        name_lbl.pack()

        # Store references
        frame._tab_idx = tab_idx
        frame._slot_idx = slot_idx
        frame._tier = tier
        frame._icon_lbl = icon_lbl
        frame._name_lbl = name_lbl
        frame._tier_lbl = tier_lbl

        # Drag from slot + right-click to remove + hover tooltip
        for widget in (frame, icon_lbl, name_lbl, tier_lbl):
            widget.bind("<ButtonPress-1>", lambda e, t=tab_idx, s=slot_idx: self._start_drag_from_slot(e, t, s))
            widget.bind("<B1-Motion>", self._on_drag_motion)
            widget.bind("<ButtonRelease-1>", self._on_drag_release)
            widget.bind("<Button-3>", lambda e, t=tab_idx, s=slot_idx: self._on_slot_right_click(t, s))
            widget.bind("<Enter>", lambda e, t=tab_idx, s=slot_idx: self._show_slot_tooltip(e, t, s))
            widget.bind("<Leave>", lambda e: self._hide_tooltip())
            widget._tab_idx = tab_idx
            widget._slot_idx = slot_idx
            widget._tier = tier

        return frame

    def _refresh_tree_display(self):
        """Update all slot visuals from tree_slots state."""
        count = 0
        for tab_idx in range(3):
            for slot_idx in range(10):
                skill_name = self.tree_slots[tab_idx][slot_idx]
                slot_frame = self.slot_widgets[tab_idx][slot_idx]
                tier = slot_frame._tier

                if skill_name and skill_name in self.tk_icons:
                    count += 1
                    slot_frame._icon_lbl.configure(image=self.tk_icons[skill_name])
                    short = skill_name[:10] + ".." if len(skill_name) > 10 else skill_name
                    slot_frame._name_lbl.configure(text=short, fg=FG)

                    orig_class = self._get_skill_class(skill_name)
                    if orig_class:
                        cc_color = CLASS_COLORS.get(orig_class, FG)
                        slot_frame.configure(highlightbackground=cc_color)
                elif skill_name:
                    # Skill exists but no icon
                    count += 1
                    slot_frame._icon_lbl.configure(image=self._empty_icons.get(tier))
                    short = skill_name[:10] + ".." if len(skill_name) > 10 else skill_name
                    slot_frame._name_lbl.configure(text=short, fg=FG)
                    slot_frame.configure(highlightbackground=TIER_COLORS[tier])
                else:
                    slot_frame._icon_lbl.configure(image=self._empty_icons.get(tier))
                    slot_frame._name_lbl.configure(text="Empty", fg=FG_DIM)
                    slot_frame.configure(highlightbackground=TIER_COLORS[tier])

        if hasattr(self, 'tree_count_label'):
            self.tree_count_label.configure(text=f"{count} / 30 skills placed")

    def _refresh_pool_display(self):
        """Rebuild the available skills pool display."""
        for w in self.pool_inner.winfo_children():
            w.destroy()

        # Filter by tier
        skills_to_show = []
        for name in self.available_skills:
            tier = self.tier_map.get(name, 1)
            if self._tier_filter == 0 or tier == self._tier_filter:
                skills_to_show.append((name, tier))

        self.pool_count_label.configure(
            text=f"({len(skills_to_show)} / {len(self.available_skills)})"
        )

        # Update filter button highlights
        for t, btn in self._filter_buttons.items():
            if t == self._tier_filter:
                btn.configure(bg=BG, relief="sunken")
            else:
                btn.configure(bg=BG_LIGHT, relief="flat")

        # Create skill cards in a wrapping grid
        cols = 8
        for i, (name, tier) in enumerate(skills_to_show):
            row = i // cols
            col = i % cols
            card = self._create_pool_card(self.pool_inner, name, tier)
            card.grid(row=row, column=col, padx=2, pady=2)

    def _create_pool_card(self, parent, skill_name: str, tier: int) -> tk.Frame:
        """Create a draggable skill card for the pool."""
        color = TIER_COLORS[tier]
        dim = TIER_COLORS_DIM[tier]
        orig_class = self._get_skill_class(skill_name)
        cc_color = CLASS_COLORS.get(orig_class, color) if orig_class else color

        card = tk.Frame(
            parent, bg=dim, bd=1, relief="raised",
            highlightbackground=cc_color, highlightthickness=1,
            cursor="hand2",
        )

        # Icon
        icon_img = self.tk_icons.get(skill_name)
        icon_lbl = tk.Label(card, bg=dim, image=icon_img)
        icon_lbl.pack(padx=2, pady=(2, 0))

        # Name
        short = skill_name[:8] + ".." if len(skill_name) > 8 else skill_name
        name_lbl = tk.Label(
            card, text=short, font=("Segoe UI", 6),
            fg=cc_color, bg=dim, wraplength=50,
        )
        name_lbl.pack(padx=2, pady=(0, 1))

        # Tier + class badge
        info = self.skill_info.get(skill_name, {})
        class_code = info.get("original_class", "")
        class_short = CLASS_NAMES.get(class_code, "")[:3]
        badge_text = f"{TIER_LABELS[tier]} {class_short}"
        tier_lbl = tk.Label(
            card, text=badge_text, font=("Segoe UI", 6, "bold"),
            fg=color, bg=dim,
        )
        tier_lbl.pack(pady=(0, 2))

        # Drag start + hover tooltip bindings
        for widget in (card, icon_lbl, name_lbl, tier_lbl):
            widget.bind("<ButtonPress-1>", lambda e, n=skill_name: self._start_drag_from_pool(e, n))
            widget.bind("<B1-Motion>", self._on_drag_motion)
            widget.bind("<ButtonRelease-1>", self._on_drag_release)
            widget.bind("<Enter>", lambda e, n=skill_name: self._show_pool_tooltip(e, n))
            widget.bind("<Leave>", lambda e: self._hide_tooltip())

        return card

    # -- Tooltips --

    def _show_slot_tooltip(self, event, tab_idx, slot_idx):
        """Show tooltip for a tree slot."""
        skill_name = self.tree_slots[tab_idx][slot_idx]
        if not skill_name:
            return
        self._show_tooltip(event, skill_name)

    def _show_pool_tooltip(self, event, skill_name):
        """Show tooltip for a pool card."""
        self._show_tooltip(event, skill_name)

    def _show_tooltip(self, event, skill_name):
        """Show tooltip with full skill info."""
        self._hide_tooltip()
        info = self.skill_info.get(skill_name, {})
        cc = info.get("original_class", "?")
        class_name = CLASS_NAMES.get(cc, cc)
        tier = self.tier_map.get(skill_name, 1)

        text = f"{skill_name}\n{class_name} - {TIER_NAMES[tier]}"

        self._tooltip = tk.Toplevel(self)
        self._tooltip.wm_overrideredirect(True)
        self._tooltip.configure(bg="#333344")

        lbl = tk.Label(
            self._tooltip, text=text, font=("Segoe UI", 9),
            fg=FG, bg="#333344", padx=8, pady=4, justify="left",
        )
        lbl.pack()

        x = event.x_root + 15
        y = event.y_root + 10
        self._tooltip.geometry(f"+{x}+{y}")

    def _hide_tooltip(self):
        if self._tooltip:
            self._tooltip.destroy()
            self._tooltip = None

    # -- Drag & Drop --

    def _start_drag_from_pool(self, event, skill_name: str):
        """Begin dragging a skill from the pool."""
        self._drag_skill = skill_name
        self._drag_source = None
        self._hide_tooltip()

        icon_img = self.tk_icons.get(skill_name)
        self._drag_label = tk.Label(
            self, image=icon_img, bg=BG_DARK, bd=2, relief="solid",
            highlightbackground=GOLD, highlightthickness=2,
        )
        x = event.x_root - self.winfo_rootx()
        y = event.y_root - self.winfo_rooty()
        self._drag_label.place(x=x - ICON_SIZE // 2, y=y - ICON_SIZE // 2)
        self._drag_label.lift()

    def _start_drag_from_slot(self, event, tab_idx: int, slot_idx: int):
        """Begin dragging a skill from a tree slot."""
        skill_name = self.tree_slots[tab_idx][slot_idx]
        if not skill_name:
            return

        self._drag_skill = skill_name
        self._drag_source = (tab_idx, slot_idx)
        self._hide_tooltip()

        icon_img = self.tk_icons.get(skill_name)
        self._drag_label = tk.Label(
            self, image=icon_img, bg=BG_DARK, bd=2, relief="solid",
            highlightbackground=GOLD, highlightthickness=2,
        )
        x = event.x_root - self.winfo_rootx()
        y = event.y_root - self.winfo_rooty()
        self._drag_label.place(x=x - ICON_SIZE // 2, y=y - ICON_SIZE // 2)
        self._drag_label.lift()

    def _on_drag_motion(self, event):
        """Move the floating drag label."""
        if self._drag_label:
            x = event.x_root - self.winfo_rootx()
            y = event.y_root - self.winfo_rooty()
            self._drag_label.place(x=x - ICON_SIZE // 2, y=y - ICON_SIZE // 2)
            self._highlight_drop_target(event.x_root, event.y_root)

    def _on_drag_release(self, event):
        """Handle drop."""
        if not self._drag_skill or not self._drag_label:
            self._cancel_drag()
            return

        target = self._find_slot_at(event.x_root, event.y_root)

        if target:
            tab_idx, slot_idx, tier = target
            skill_tier = self.tier_map.get(self._drag_skill, 1)

            if skill_tier == tier:
                existing = self.tree_slots[tab_idx][slot_idx]

                # Remove from source slot if dragging from tree
                if self._drag_source:
                    src_tab, src_slot = self._drag_source
                    self.tree_slots[src_tab][src_slot] = None

                # Place skill in target slot
                self.tree_slots[tab_idx][slot_idx] = self._drag_skill

                self._refresh_all()
                self.status_label.configure(
                    text=f"Placed: {self._drag_skill}", fg=GREEN,
                )
            else:
                self.status_label.configure(
                    text=f"Tier mismatch! {self._drag_skill} is "
                         f"{TIER_LABELS[skill_tier]}, slot requires {TIER_LABELS[tier]}",
                    fg=RED,
                )
        else:
            # Dropped outside a slot
            if self._drag_source:
                src_tab, src_slot = self._drag_source
                self.tree_slots[src_tab][src_slot] = None
                self._refresh_all()
                self.status_label.configure(
                    text=f"Removed: {self._drag_skill}", fg=FG_DIM,
                )

        self._cancel_drag()

    def _cancel_drag(self):
        if self._drag_label:
            self._drag_label.destroy()
        self._drag_label = None
        self._drag_skill = None
        self._drag_source = None
        self._clear_highlights()

    def _highlight_drop_target(self, root_x, root_y):
        self._clear_highlights()
        target = self._find_slot_at(root_x, root_y)
        if target and self._drag_skill:
            tab_idx, slot_idx, tier = target
            skill_tier = self.tier_map.get(self._drag_skill, 1)
            frame = self.slot_widgets[tab_idx][slot_idx]
            if skill_tier == tier:
                frame.configure(highlightbackground=GREEN, highlightthickness=2)
            else:
                frame.configure(highlightbackground=RED, highlightthickness=2)

    def _clear_highlights(self):
        for tab_idx in range(3):
            for slot_idx in range(10):
                frame = self.slot_widgets[tab_idx][slot_idx]
                skill_name = self.tree_slots[tab_idx][slot_idx]
                tier = frame._tier
                if skill_name:
                    orig_class = self._get_skill_class(skill_name)
                    color = CLASS_COLORS.get(orig_class, TIER_COLORS[tier]) if orig_class else TIER_COLORS[tier]
                else:
                    color = TIER_COLORS[tier]
                frame.configure(highlightbackground=color, highlightthickness=1)

    def _find_slot_at(self, root_x, root_y) -> tuple[int, int, int] | None:
        for tab_idx in range(3):
            for slot_idx in range(10):
                frame = self.slot_widgets[tab_idx][slot_idx]
                try:
                    fx = frame.winfo_rootx()
                    fy = frame.winfo_rooty()
                    fw = frame.winfo_width()
                    fh = frame.winfo_height()
                    if fx <= root_x <= fx + fw and fy <= root_y <= fy + fh:
                        return (tab_idx, slot_idx, frame._tier)
                except Exception:
                    pass
        return None

    # -- Slot interactions --

    def _on_slot_right_click(self, tab_idx: int, slot_idx: int):
        skill_name = self.tree_slots[tab_idx][slot_idx]
        if skill_name:
            self.tree_slots[tab_idx][slot_idx] = None
            self._refresh_all()
            self.status_label.configure(
                text=f"Removed: {skill_name}", fg=FG_DIM,
            )

    # -- Helpers --

    def _get_skill_class(self, skill_name: str) -> str | None:
        info = self.skill_info.get(skill_name)
        if info:
            return info["original_class"]
        return None

    def _refresh_all(self):
        self._refresh_available()
        self._refresh_tree_display()
        self._refresh_pool_display()

    def _set_filter(self, tier: int):
        self._tier_filter = tier
        self._refresh_pool_display()

    # -- Scroll handlers --

    def _on_pool_configure(self, event):
        self.pool_canvas.configure(scrollregion=self.pool_canvas.bbox("all"))

    def _on_pool_canvas_configure(self, event):
        self.pool_canvas.itemconfig(self.pool_canvas_window, width=event.width)

    def _on_pool_mousewheel(self, event):
        self.pool_canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")

    # -- Save --

    def _on_save(self):
        """Save the current tree configuration to game files and pool_manager."""
        # Build the skill list from tree_slots (30 entries, some None)
        tree_skills = []
        for tab_idx in range(3):
            for slot_idx in range(10):
                name = self.tree_slots[tab_idx][slot_idx]
                tree_skills.append(name)

        # Count placed skills
        placed = sum(1 for n in tree_skills if n is not None)
        if placed == 0:
            self.status_label.configure(text="No skills placed!", fg=RED)
            return

        # Update pool_manager state
        equipped_list = []
        for idx, name in enumerate(tree_skills):
            if name is None:
                continue
            info = self.skill_info.get(name, {})
            equipped_list.append({
                "slot_index": idx,
                "skill_name": name,
                "skill_id": info.get("skill_id", 0),
                "original_class": info.get("original_class", ""),
                "skilldesc_ref": info.get("skilldesc_ref", ""),
            })

        in_tree_names = {n for n in tree_skills if n is not None}
        unlocked = sorted(n for n in self.skill_info if n not in in_tree_names)

        from ..core.skill_pool import SkillPoolState
        self.pool_manager.state.equipped = equipped_list
        self.pool_manager.state.unlocked = unlocked
        self.pool_manager.save()

        # Apply to game files
        if self.on_save_callback:
            self.on_save_callback(tree_skills)
            # Close the editor window after successful save
            self.destroy()
        else:
            self.status_label.configure(
                text=f"Pool saved ({placed} skills). No game callback.", fg=GOLD,
            )
