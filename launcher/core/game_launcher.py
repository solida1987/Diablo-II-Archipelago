"""Game launcher - prepares data files and launches Diablo II."""

import os
import shutil
import subprocess
from .d2_data import D2TxtFile

VANILLA_TXT_DIR = os.path.join(os.path.dirname(__file__), "..", "data", "vanilla_txt")


class GameLauncher:
    def __init__(self, game_dir: str):
        self.game_dir = game_dir
        self.excel_dir = os.path.join(game_dir, "data", "global", "excel")

    def prepare_data_folder(self, modified_files: dict[str, D2TxtFile]) -> None:
        """Set up data/global/excel/ with ONLY the modified .txt files.

        We provide only our 3 modified files (Skills.txt, SkillDesc.txt,
        CharStats.txt). The game with -direct -txt will compile these to
        .bin and use MPQ for all other data tables.

        We delete ALL .bin files to force a clean recompile. The game
        regenerates .bin from MPQ for any table without a .txt file.
        """
        os.makedirs(self.excel_dir, exist_ok=True)

        # Remove ALL old files (clean slate)
        for fname in os.listdir(self.excel_dir):
            fpath = os.path.join(self.excel_dir, fname)
            if fname.endswith((".txt", ".bin")) and os.path.isfile(fpath):
                os.remove(fpath)

        # Write ONLY modified files
        for fname, txt_file in modified_files.items():
            txt_file.save(os.path.join(self.excel_dir, fname))

    def get_game_exe(self) -> str:
        """Find Game.exe in the game directory."""
        exe = os.path.join(self.game_dir, "Game.exe")
        if os.path.exists(exe):
            return exe
        raise FileNotFoundError(f"Game.exe not found in {self.game_dir}")

    def launch(self, windowed: bool = True) -> subprocess.Popen:
        """Launch Game.exe with -direct -txt flags."""
        exe = self.get_game_exe()
        args = [exe, "-direct", "-txt"]
        if windowed:
            args.append("-w")

        return subprocess.Popen(
            args,
            cwd=self.game_dir,
            creationflags=subprocess.DETACHED_PROCESS,
        )

    def is_running(self) -> bool:
        """Check if Game.exe is currently running (Windows only)."""
        try:
            result = subprocess.run(
                ["tasklist", "/FI", "IMAGENAME eq Game.exe"],
                capture_output=True, text=True, timeout=5,
            )
            return "Game.exe" in result.stdout
        except Exception:
            return False

    def clean_all_bins(self) -> None:
        """Remove ALL .bin files (forces full recompile on next launch)."""
        if os.path.isdir(self.excel_dir):
            for fname in os.listdir(self.excel_dir):
                if fname.endswith(".bin"):
                    os.remove(os.path.join(self.excel_dir, fname))

    def clean_data_folder(self) -> None:
        """Remove the data/global/excel/ folder (cleanup)."""
        if os.path.isdir(self.excel_dir):
            shutil.rmtree(self.excel_dir)
