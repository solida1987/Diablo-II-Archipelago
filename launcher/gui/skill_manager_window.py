"""Skill Manager window for swapping equipped skills with unlocked ones."""

import os
import tkinter as tk
from tkinter import ttk, messagebox

from ..core.d2_data import D2TxtFile
from ..core.skill_randomizer import (
    get_all_class_skills, swap_skill, SkillEntry,
    CLASS_NAMES, CLASS_CODES,
)
from ..core.skill_pool import SkillPoolManager
from ..core.game_launcher import GameLauncher

VANILLA_TXT_DIR = os.path.join(os.path.dirname(__file__), "..", "data", "vanilla_txt")


class SkillManagerWindow:
    def __init__(self, parent: tk.Tk, pool_manager: SkillPoolManager, base_dir: str):
        self.parent = parent
        self.pool_manager = pool_manager
        self.base_dir = base_dir

        # Load skill data for lookups
        vanilla_dir = os.path.normpath(VANILLA_TXT_DIR)
        skills_txt = D2TxtFile.from_file(os.path.join(vanilla_dir, "Skills.txt"))
        self.all_skills = get_all_class_skills(skills_txt)
        self.skill_by_name: dict[str, SkillEntry] = {s.skill_name: s for s in self.all_skills}

        self.selected_slot: int | None = None

        self.window = tk.Toplevel(parent)
        self.window.title("Skill Manager")
        self.window.geometry("750x600")
        self.window.transient(parent)
        self._build_ui()

    def _build_ui(self):
        main = ttk.Frame(self.window, padding=10)
        main.pack(fill=tk.BOTH, expand=True)

        # Top: two panels side by side
        panels = ttk.Frame(main)
        panels.pack(fill=tk.BOTH, expand=True)

        # Left panel: equipped skills
        left = ttk.LabelFrame(panels, text="Equipped Skills (30)", padding=5)
        left.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0, 5))

        self.equipped_tree = ttk.Treeview(
            left, columns=("slot", "skill", "class"), show="headings", height=20,
        )
        self.equipped_tree.heading("slot", text="#")
        self.equipped_tree.heading("skill", text="Skill")
        self.equipped_tree.heading("class", text="Original Class")
        self.equipped_tree.column("slot", width=30, anchor=tk.CENTER)
        self.equipped_tree.column("skill", width=180)
        self.equipped_tree.column("class", width=100)
        self.equipped_tree.pack(fill=tk.BOTH, expand=True)
        self.equipped_tree.bind("<<TreeviewSelect>>", self._on_equipped_select)

        scrollbar_eq = ttk.Scrollbar(left, orient=tk.VERTICAL, command=self.equipped_tree.yview)
        self.equipped_tree.configure(yscrollcommand=scrollbar_eq.set)

        # Right panel: available/unlocked skills
        right = ttk.LabelFrame(panels, text="Available Skills", padding=5)
        right.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(5, 0))

        # Filter
        filter_frame = ttk.Frame(right)
        filter_frame.pack(fill=tk.X, pady=(0, 5))
        ttk.Label(filter_frame, text="Filter:").pack(side=tk.LEFT)
        self.filter_var = tk.StringVar(value="All")
        filter_combo = ttk.Combobox(
            filter_frame, textvariable=self.filter_var,
            values=["All"] + [CLASS_NAMES[c] for c in CLASS_CODES],
            state="readonly", width=15,
        )
        filter_combo.pack(side=tk.LEFT, padx=5)
        filter_combo.bind("<<ComboboxSelected>>", lambda e: self._refresh_available())

        self.available_tree = ttk.Treeview(
            right, columns=("skill", "class"), show="headings", height=18,
        )
        self.available_tree.heading("skill", text="Skill")
        self.available_tree.heading("class", text="Original Class")
        self.available_tree.column("skill", width=180)
        self.available_tree.column("class", width=100)
        self.available_tree.pack(fill=tk.BOTH, expand=True)

        scrollbar_av = ttk.Scrollbar(right, orient=tk.VERTICAL, command=self.available_tree.yview)
        self.available_tree.configure(yscrollcommand=scrollbar_av.set)

        # Bottom: swap controls
        bottom = ttk.Frame(main)
        bottom.pack(fill=tk.X, pady=(10, 0))

        self.swap_label = tk.StringVar(value="Select an equipped skill, then an available skill")
        ttk.Label(bottom, textvariable=self.swap_label).pack(anchor=tk.W, pady=(0, 5))

        btn_row = ttk.Frame(bottom)
        btn_row.pack(fill=tk.X)

        ttk.Button(btn_row, text="Swap Selected", command=self._on_swap).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_row, text="Reset All Points", command=self._on_reset).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_row, text="Apply & Close", command=self._on_apply).pack(side=tk.RIGHT, padx=5)

        self._refresh_all()

    def _refresh_all(self):
        self._refresh_equipped()
        self._refresh_available()

    def _refresh_equipped(self):
        self.equipped_tree.delete(*self.equipped_tree.get_children())
        equipped = self.pool_manager.get_equipped()
        for i, sk in enumerate(equipped):
            tab_label = f"T{(i // 10) + 1}"
            class_name = CLASS_NAMES.get(sk.original_class, sk.original_class)
            self.equipped_tree.insert("", tk.END, iid=str(i), values=(
                f"{i+1}", sk.skill_name, class_name,
            ))

    def _refresh_available(self):
        self.available_tree.delete(*self.available_tree.get_children())
        unlocked = self.pool_manager.get_unlocked()
        filter_class = self.filter_var.get()

        for name in sorted(unlocked):
            skill = self.skill_by_name.get(name)
            if not skill:
                continue
            class_name = CLASS_NAMES.get(skill.original_class, skill.original_class)
            if filter_class != "All" and class_name != filter_class:
                continue
            self.available_tree.insert("", tk.END, values=(name, class_name))

    def _on_equipped_select(self, event):
        sel = self.equipped_tree.selection()
        if sel:
            self.selected_slot = int(sel[0])
            equipped = self.pool_manager.get_equipped()
            sk = equipped[self.selected_slot]
            self.swap_label.set(f"Selected slot {self.selected_slot + 1}: {sk.skill_name}")

    def _on_swap(self):
        if self.selected_slot is None:
            messagebox.showinfo("Swap", "Select an equipped skill first (left panel)")
            return

        av_sel = self.available_tree.selection()
        if not av_sel:
            messagebox.showinfo("Swap", "Select an available skill to swap in (right panel)")
            return

        new_name = self.available_tree.item(av_sel[0])["values"][0]
        new_skill = self.skill_by_name.get(new_name)
        if not new_skill:
            return

        old_equipped = self.pool_manager.get_equipped()
        old_name = old_equipped[self.selected_slot].skill_name

        # Use the fixed swap() with full skill data
        success = self.pool_manager.swap(self.selected_slot, {
            "skill_name": new_skill.skill_name,
            "skill_id": new_skill.skill_id,
            "original_class": new_skill.original_class,
            "skilldesc_ref": new_skill.skilldesc_ref,
        })

        if success:
            self.pool_manager.save()
            self._refresh_all()
            self.swap_label.set(f"Swapped: {old_name} -> {new_name} (click Apply & Close to update game files)")
        else:
            messagebox.showerror("Error", "Swap failed - skill not available")

    def _on_reset(self):
        messagebox.showinfo(
            "Reset Points",
            "Skill points will be reset on next game launch.\n"
            "Note: This requires save file editing (coming in future update).",
        )

    def _on_apply(self):
        """Write modified skill assignments to data files and close."""
        try:
            self._apply_to_files()
            self.pool_manager.save()
            messagebox.showinfo("Applied", "Skills updated. Restart the game to see changes.")
            self.window.destroy()
        except Exception as e:
            messagebox.showerror("Error", f"Failed to apply:\n{e}")

    def _apply_to_files(self):
        """Rewrite Skills.txt and SkillDesc.txt based on current equipped state."""
        vanilla_dir = os.path.normpath(VANILLA_TXT_DIR)
        skills_txt = D2TxtFile.from_file(os.path.join(vanilla_dir, "Skills.txt"))
        sdesc_txt = D2TxtFile.from_file(os.path.join(vanilla_dir, "SkillDesc.txt"))
        cstats_txt = D2TxtFile.from_file(os.path.join(vanilla_dir, "CharStats.txt"))

        target_class = self.pool_manager.state.target_class
        equipped = self.pool_manager.get_equipped()
        equipped_names = {sk.skill_name for sk in equipped}

        # Build lookup for all skills in Skills.txt
        all_skills = get_all_class_skills(skills_txt)
        skill_by_name = {s.skill_name: s for s in all_skills}

        # Get skills by class for rebalancing
        by_class = {c: [] for c in CLASS_CODES}
        for s in all_skills:
            by_class[s.original_class].append(s)

        # Assign equipped skills to target class
        equipped_row_indices = set()
        for sk in equipped:
            entry = skill_by_name.get(sk.skill_name)
            if entry:
                skills_txt.set_value(entry.row_index, "charclass", target_class)
                skills_txt.set_value(entry.row_index, "reqlevel", "1")
                skills_txt.set_value(entry.row_index, "reqskill1", "")
                skills_txt.set_value(entry.row_index, "reqskill2", "")
                skills_txt.set_value(entry.row_index, "reqskill3", "")
                equipped_row_indices.add(entry.row_index)

        # Rebalance: target class's original unselected skills fill gaps
        spare = [s for s in by_class[target_class] if s.row_index not in equipped_row_indices]
        spare_idx = 0
        for cc in CLASS_CODES:
            if cc == target_class:
                continue
            taken = [s for s in by_class[cc] if s.row_index in equipped_row_indices]
            for _ in taken:
                if spare_idx < len(spare):
                    skills_txt.set_value(spare[spare_idx].row_index, "charclass", cc)
                    spare_idx += 1

        # Apply skill tree layout
        sd_name_to_row = {}
        for ri in range(len(sdesc_txt.rows)):
            name = sdesc_txt.get_value(ri, "skilldesc")
            if name:
                sd_name_to_row[name] = ri

        # Hide all class skill descs first
        for ri in range(len(sdesc_txt.rows)):
            page = sdesc_txt.get_value(ri, "SkillPage")
            if page and page != "0":
                sdesc_txt.set_value(ri, "SkillPage", "0")

        # Place equipped skills in grid
        for slot_idx, sk in enumerate(equipped):
            entry = skill_by_name.get(sk.skill_name)
            if not entry or entry.skilldesc_ref not in sd_name_to_row:
                continue
            sd_row = sd_name_to_row[entry.skilldesc_ref]

            tab = (slot_idx // 10) + 1
            pos = slot_idx % 10
            if pos < 9:
                row = (pos // 3) + 1
                col = (pos % 3) + 1
            else:
                row = 5
                col = 1

            sdesc_txt.set_value(sd_row, "SkillPage", str(tab))
            sdesc_txt.set_value(sd_row, "SkillRow", str(row))
            sdesc_txt.set_value(sd_row, "SkillColumn", str(col))
            sdesc_txt.set_value(sd_row, "ListRow", str(tab))

        # Update CharStats
        from ..core.skill_randomizer import _update_charstats, CLASS_NAMES
        _update_charstats(cstats_txt, target_class, equipped[0].skill_name)

        # Write to game data folder
        from ..core.config import AppConfig
        config = AppConfig.load(os.path.join(self.base_dir, "config.json"))
        launcher = GameLauncher(config.game_dir)
        launcher.prepare_data_folder({
            "Skills.txt": skills_txt,
            "SkillDesc.txt": sdesc_txt,
            "CharStats.txt": cstats_txt,
        })
