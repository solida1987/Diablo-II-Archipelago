"""Entry point for the Diablo II Archipelago Launcher."""

import os
import sys
import tkinter as tk

# Ensure the launcher package is importable
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from launcher.gui.launcher_window import LauncherWindow


def main():
    # Base directory is the D2 installation folder (parent of launcher/)
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    root = tk.Tk()
    app = LauncherWindow(root, base_dir)
    root.mainloop()


if __name__ == "__main__":
    main()
