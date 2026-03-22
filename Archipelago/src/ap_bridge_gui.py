"""
Diablo II Archipelago Bridge — GUI
Tkinter window that wraps the AP bridge, showing connection status,
message log, and reconnect controls.

Usage: python ap_bridge_gui.py --server HOST:PORT --slot NAME --password PASS --char CHARNAME --gamedir PATH
"""
import tkinter as tk
from tkinter import scrolledtext
import threading
import asyncio
import queue
import argparse
import time
import sys
import os

# Add our directory to path so we can import ap_bridge
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ap_bridge import D2ArchipelagoBridge


class BridgeGUI:
    # Status colors
    COLORS = {
        "connecting": "#FFB800",   # yellow
        "connected": "#FFB800",    # yellow (not yet authenticated)
        "authenticated": "#00CC44",  # green
        "disconnected": "#CC3333",  # red
        "error": "#CC3333",        # red
        "refused": "#CC3333",      # red
    }

    def __init__(self, server, slot, password, char_name, game_dir):
        self.server = server
        self.slot = slot
        self.char_name = char_name
        self.msg_queue = queue.Queue()
        self.status_queue = queue.Queue()
        self.bridge = None
        self.loop = None
        self._bridge_thread = None

        # Build the GUI
        self.root = tk.Tk()
        self.root.title("Diablo II Archipelago - AP Bridge")
        self.root.geometry("560x420")
        self.root.minsize(480, 350)
        self.root.configure(bg="#1a1a2e")
        self.root.protocol("WM_DELETE_CLOSE", self._on_close)

        self._build_ui()

        # Create bridge with callbacks
        self.bridge = D2ArchipelagoBridge(
            server, slot, password, char_name, game_dir,
            log_callback=self._on_log,
            status_callback=self._on_status,
        )

        # Start bridge thread
        self._start_bridge_thread()

        # Start polling the queues
        self.root.after(100, self._poll_queues)

    def _build_ui(self):
        bg = "#1a1a2e"
        fg = "#e0e0e0"
        accent = "#16213e"

        # --- Header ---
        header = tk.Frame(self.root, bg="#0f3460", height=50)
        header.pack(fill=tk.X)
        header.pack_propagate(False)

        tk.Label(header, text="AP Bridge", font=("Segoe UI", 14, "bold"),
                 bg="#0f3460", fg="#e0e0e0").pack(side=tk.LEFT, padx=12, pady=8)

        # Status dot + text in header
        self.status_frame = tk.Frame(header, bg="#0f3460")
        self.status_frame.pack(side=tk.RIGHT, padx=12)

        self.status_dot = tk.Canvas(self.status_frame, width=14, height=14,
                                     bg="#0f3460", highlightthickness=0)
        self.status_dot.pack(side=tk.LEFT, padx=(0, 6))
        self._dot_id = self.status_dot.create_oval(2, 2, 12, 12, fill="#CC3333", outline="")

        self.status_label = tk.Label(self.status_frame, text="Disconnected",
                                      font=("Segoe UI", 10), bg="#0f3460", fg="#CC3333")
        self.status_label.pack(side=tk.LEFT)

        # --- Info bar ---
        info_bar = tk.Frame(self.root, bg=accent, height=36)
        info_bar.pack(fill=tk.X)
        info_bar.pack_propagate(False)

        info_text = f"Server: {self.server}  |  Slot: {self.slot}  |  Char: {self.char_name}"
        tk.Label(info_bar, text=info_text, font=("Segoe UI", 9),
                 bg=accent, fg="#a0a0c0").pack(side=tk.LEFT, padx=12, pady=6)

        self.stats_label = tk.Label(info_bar, text="Items: 0  |  Checks: 0",
                                     font=("Segoe UI", 9), bg=accent, fg="#a0a0c0")
        self.stats_label.pack(side=tk.RIGHT, padx=12, pady=6)

        # --- Log area ---
        log_frame = tk.Frame(self.root, bg=bg)
        log_frame.pack(fill=tk.BOTH, expand=True, padx=8, pady=(8, 4))

        self.log_text = scrolledtext.ScrolledText(
            log_frame, wrap=tk.WORD, font=("Consolas", 9),
            bg="#0d1117", fg="#c9d1d9", insertbackground="#c9d1d9",
            selectbackground="#264f78", relief=tk.FLAT, borderwidth=0,
            state=tk.DISABLED, height=12
        )
        self.log_text.pack(fill=tk.BOTH, expand=True)

        # Tag colors for log messages
        self.log_text.tag_configure("timestamp", foreground="#6e7681")
        self.log_text.tag_configure("unlock", foreground="#3fb950")
        self.log_text.tag_configure("error", foreground="#f85149")
        self.log_text.tag_configure("ap_msg", foreground="#58a6ff")
        self.log_text.tag_configure("info", foreground="#c9d1d9")

        # --- Button bar ---
        btn_bar = tk.Frame(self.root, bg=bg)
        btn_bar.pack(fill=tk.X, padx=8, pady=(4, 8))

        btn_style = {"font": ("Segoe UI", 9), "relief": tk.FLAT, "cursor": "hand2",
                     "padx": 16, "pady": 4}

        self.reconnect_btn = tk.Button(
            btn_bar, text="Reconnect", bg="#238636", fg="white",
            activebackground="#2ea043", activeforeground="white",
            command=self._on_reconnect, **btn_style)
        self.reconnect_btn.pack(side=tk.LEFT, padx=(0, 8))

        self.clear_btn = tk.Button(
            btn_bar, text="Clear Log", bg="#30363d", fg="#c9d1d9",
            activebackground="#484f58", activeforeground="white",
            command=self._on_clear_log, **btn_style)
        self.clear_btn.pack(side=tk.RIGHT)

    def _on_log(self, msg):
        """Called from bridge (background thread) — queue it for GUI."""
        self.msg_queue.put(msg)

    def _on_status(self, status, items_received, checks_sent):
        """Called from bridge (background thread) — queue it for GUI."""
        self.status_queue.put((status, items_received, checks_sent))

    def _poll_queues(self):
        """Poll message and status queues from the main thread."""
        # Process log messages
        try:
            while True:
                msg = self.msg_queue.get_nowait()
                self._append_log(msg)
        except queue.Empty:
            pass

        # Process status updates
        try:
            while True:
                status, items, checks = self.status_queue.get_nowait()
                self._update_status(status, items, checks)
        except queue.Empty:
            pass

        self.root.after(100, self._poll_queues)

    def _append_log(self, msg):
        """Append a timestamped message to the log widget."""
        self.log_text.configure(state=tk.NORMAL)

        timestamp = time.strftime("[%H:%M:%S] ")
        self.log_text.insert(tk.END, timestamp, "timestamp")

        # Color based on content
        if "UNLOCKED:" in msg:
            tag = "unlock"
        elif "error" in msg.lower() or "failed" in msg.lower() or "refused" in msg.lower():
            tag = "error"
        elif msg.startswith("AP: "):
            tag = "ap_msg"
        else:
            tag = "info"

        self.log_text.insert(tk.END, msg + "\n", tag)
        self.log_text.see(tk.END)
        self.log_text.configure(state=tk.DISABLED)

    def _update_status(self, status, items_received, checks_sent):
        """Update the status indicator and stats."""
        # Determine display text and color
        base_status = status.split(":")[0].strip() if ":" in status else status
        color = self.COLORS.get(base_status, "#CC3333")

        display_names = {
            "connecting": "Connecting...",
            "connected": "Connected",
            "authenticated": "Connected",
            "disconnected": "Disconnected",
            "error": "Error",
            "refused": "Refused",
        }
        display = display_names.get(base_status, status)

        self.status_dot.itemconfig(self._dot_id, fill=color)
        self.status_label.configure(text=display, fg=color)
        self.stats_label.configure(text=f"Items: {items_received}  |  Checks: {checks_sent}")

    def _on_reconnect(self):
        """Reconnect button clicked."""
        self._append_log("Manual reconnect requested...")
        if self.bridge:
            self.bridge.request_reconnect()

    def _on_clear_log(self):
        """Clear the log area."""
        self.log_text.configure(state=tk.NORMAL)
        self.log_text.delete("1.0", tk.END)
        self.log_text.configure(state=tk.DISABLED)

    def _on_close(self):
        """Window close."""
        self.root.destroy()

    def _start_bridge_thread(self):
        """Start the bridge asyncio loop in a background thread."""
        def run_bridge():
            self.loop = asyncio.new_event_loop()
            asyncio.set_event_loop(self.loop)
            self.loop.run_until_complete(self._bridge_loop())

        self._bridge_thread = threading.Thread(target=run_bridge, daemon=True)
        self._bridge_thread.start()

    async def _bridge_loop(self):
        """Main bridge loop — connect, reconnect on failure."""
        while True:
            try:
                self.bridge._request_reconnect = False
                await self.bridge.connect()
            except Exception as e:
                self.bridge.log(f"Bridge error: {e}")

            if self.bridge._request_reconnect:
                self.bridge.log("Reconnecting now...")
                continue

            self.bridge.log("Reconnecting in 5 seconds...")
            await asyncio.sleep(5)

    def run(self):
        """Start the tkinter main loop."""
        self.root.mainloop()


def main():
    parser = argparse.ArgumentParser(description="Diablo II Archipelago Bridge GUI")
    parser.add_argument("--server", required=True, help="AP server host:port")
    parser.add_argument("--slot", required=True, help="Slot/player name")
    parser.add_argument("--password", default="", help="Server password")
    parser.add_argument("--char", required=True, help="Character name")
    parser.add_argument("--gamedir", required=True, help="Game root directory")
    args = parser.parse_args()

    gui = BridgeGUI(args.server, args.slot, args.password, args.char, args.gamedir)
    gui.run()


if __name__ == "__main__":
    main()
