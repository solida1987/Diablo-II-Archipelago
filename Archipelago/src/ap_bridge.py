"""
Diablo II Archipelago Bridge
Connects to AP server via WebSocket, receives items (skill unlocks),
and writes them to the per-character state file.
Monitors a checks file for completed checks and sends them to AP.

Usage: python ap_bridge.py --server HOST:PORT --slot NAME --password PASS --char CHARNAME --gamedir PATH
"""
import asyncio
import json
import sys
import os
import time
import argparse
import ssl
import websockets

# AP protocol constants
AP_VERSION = {"major": 0, "minor": 5, "build": 0, "class": "Version"}
GAME_NAME = "Diablo II Archipelago"
AP_ITEM_BASE = 45000  # AP item ID = 45000 + D2 skill ID
CHECK_POLL_INTERVAL = 2  # seconds between check file polls


class D2ArchipelagoBridge:
    def __init__(self, server, slot, password, char_name, game_dir, log_callback=None, status_callback=None):
        self.server = server
        self.slot = slot
        self.password = password
        self.char_name = char_name
        self.game_dir = game_dir
        self.ws = None
        self.authenticated = False
        self.item_index = 0  # track last processed item index for reconnects
        self.checked_locations = set()
        self.slot_data = {}
        self._stop_polling = False
        self._request_reconnect = False

        # Callbacks for GUI integration
        self._log_callback = log_callback      # fn(message_str)
        self._status_callback = status_callback  # fn(status_str, items_received, checks_sent)

        # Paths
        self.arch_dir = os.path.join(game_dir, "Archipelago")
        self.state_file = os.path.join(self.arch_dir, f"d2arch_state_{char_name}.dat")
        self.checks_file = os.path.join(self.arch_dir, f"d2arch_checks_{char_name}.dat")
        self.status_file = os.path.join(self.arch_dir, "ap_status.dat")

        # Load skill database from state file
        self.skills = {}  # id -> {name, cls, unlocked}
        self.load_skills()

        # Count already-unlocked skills so we don't re-log them
        self.item_index = sum(1 for s in self.skills.values() if s['unlocked'])

    def log(self, msg):
        """Log a message — to GUI callback if set, otherwise print."""
        print(msg)
        if self._log_callback:
            self._log_callback(msg)

    def _notify_status(self, status):
        """Notify GUI of status change."""
        if self._status_callback:
            unlocked = sum(1 for s in self.skills.values() if s['unlocked'])
            self._status_callback(status, unlocked, len(self.checked_locations))

    def request_reconnect(self):
        """Called from GUI to trigger a reconnect."""
        self._request_reconnect = True
        if self.ws:
            asyncio.ensure_future(self.ws.close())

    def load_skills(self):
        """Load skill database from state file."""
        if not os.path.exists(self.state_file):
            self.log(f"State file not found: {self.state_file}")
            return

        past_assignments = False
        with open(self.state_file, 'r') as f:
            for line in f:
                line = line.strip()
                if line == "assignments=":
                    past_assignments = True
                    continue
                if not past_assignments or not line:
                    continue
                parts = line.split(',')
                if len(parts) >= 4:
                    name, cls, unlocked, skill_id = parts[0], parts[1], int(parts[2]), int(parts[3])
                    self.skills[skill_id] = {
                        'name': name,
                        'cls': cls,
                        'unlocked': unlocked,
                        'ap_id': AP_ITEM_BASE + skill_id
                    }
        self.log(f"Loaded {len(self.skills)} skills from state file")

    def write_status(self, status):
        """Write connection status for launcher/GUI to read."""
        try:
            unlocked = sum(1 for s in self.skills.values() if s['unlocked'])
            with open(self.status_file, 'w') as f:
                f.write(f"status={status}\n")
                f.write(f"items_received={unlocked}\n")
                f.write(f"checks_sent={len(self.checked_locations)}\n")
        except Exception:
            pass
        self._notify_status(status)

    def unlock_skill(self, ap_item_id):
        """Unlock a skill by AP item ID -> update state file."""
        skill_id = ap_item_id - AP_ITEM_BASE
        if skill_id not in self.skills:
            self.log(f"Unknown skill ID: {skill_id} (AP ID: {ap_item_id})")
            return

        skill = self.skills[skill_id]
        if skill['unlocked']:
            return  # Already unlocked

        skill['unlocked'] = 1
        self.log(f"UNLOCKED: {skill['name']} (ID {skill_id})")

        # Rewrite state file with updated unlock status
        self.save_state()

    def save_state(self):
        """Rewrite the state file with current unlock status."""
        if not os.path.exists(self.state_file):
            return

        # Read header lines (everything before assignments=)
        header_lines = []
        with open(self.state_file, 'r') as f:
            for line in f:
                header_lines.append(line)
                if line.strip() == "assignments=":
                    break

        # Write header + updated skills
        with open(self.state_file, 'w') as f:
            for line in header_lines:
                f.write(line)
            for skill_id in sorted(self.skills.keys()):
                sk = self.skills[skill_id]
                f.write(f"{sk['name']},{sk['cls']},{sk['unlocked']},{skill_id}\n")

    def check_for_new_checks(self):
        """Read checks file for new completed checks."""
        if not os.path.exists(self.checks_file):
            return []

        new_checks = []
        try:
            with open(self.checks_file, 'r') as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("check="):
                        check_id = int(line.split('=')[1])
                        if check_id not in self.checked_locations:
                            new_checks.append(check_id)
                            self.checked_locations.add(check_id)
        except Exception:
            pass
        return new_checks

    async def _poll_checks(self):
        """Periodically poll the checks file and send new checks to AP."""
        while not self._stop_polling:
            await asyncio.sleep(CHECK_POLL_INTERVAL)
            if not self.authenticated or self.ws is None:
                continue
            try:
                new_checks = self.check_for_new_checks()
                if new_checks:
                    check_msg = [{"cmd": "LocationChecks", "locations": new_checks}]
                    await self.ws.send(json.dumps(check_msg))
                    self.log(f"Sent {len(new_checks)} location checks")
                    self.write_status("authenticated")
            except Exception as e:
                self.log(f"Check poll error: {e}")
                break

    async def _open_and_run(self, url, extra_kwargs):
        """Open a websocket connection and run the message loop."""
        async with websockets.connect(
            url, ping_interval=30, ping_timeout=10, **extra_kwargs
        ) as ws:
            self.ws = ws
            self.log(f"WebSocket connected to {url}")
            self.write_status("connected")

            # Wait for RoomInfo
            msg = await ws.recv()
            data = json.loads(msg)
            if isinstance(data, list) and data:
                self.log(f"Received: {data[0].get('cmd', 'unknown')}")

            # Send Connect — password field MUST always be present
            connect_msg = [{
                "cmd": "Connect",
                "password": self.password,
                "game": GAME_NAME,
                "name": self.slot,
                "uuid": f"d2arch_{self.char_name}_{int(time.time())}",
                "version": AP_VERSION,
                "items_handling": 0b111,
                "slot_data": True,
                "tags": []
            }]

            await ws.send(json.dumps(connect_msg))
            self.log(f"Sent Connect as '{self.slot}'")

            # Start check polling as a concurrent task
            self._stop_polling = False
            poll_task = asyncio.create_task(self._poll_checks())

            try:
                async for message in ws:
                    await self.handle_message(message)
            finally:
                self._stop_polling = True
                poll_task.cancel()
                try:
                    await poll_task
                except asyncio.CancelledError:
                    pass

    async def connect(self):
        """Connect to AP server. Try wss:// first, fall back to ws://."""
        self.authenticated = False
        self.ws = None

        # Try wss first
        wss_url = f"wss://{self.server}"
        self.log(f"Connecting to {wss_url}...")
        self.write_status("connecting")

        try:
            ssl_ctx = ssl.create_default_context()
            await self._open_and_run(wss_url, {"open_timeout": 10, "ssl": ssl_ctx})
        except websockets.exceptions.ConnectionClosed as e:
            self.log(f"Connection closed: {e}")
            self.write_status("disconnected")
        except Exception as e:
            self.log(f"WSS failed ({e}), trying WS...")
            ws_url = f"ws://{self.server}"
            try:
                await self._open_and_run(ws_url, {"open_timeout": 10})
            except websockets.exceptions.ConnectionClosed as e2:
                print(f"Connection closed: {e2}")
                self.write_status("disconnected")
            except Exception as e2:
                self.log(f"WS also failed: {e2}")
                self.write_status(f"error: {e2}")

    async def handle_message(self, raw):
        """Handle incoming AP messages."""
        try:
            data = json.loads(raw)
        except Exception:
            return

        if not isinstance(data, list):
            return

        for packet in data:
            cmd = packet.get("cmd", "")

            if cmd == "Connected":
                self.authenticated = True
                self.slot_data = packet.get("slot_data", {})

                # Load already-checked locations from server
                checked = packet.get("checked_locations", [])
                self.checked_locations.update(checked)

                self.log(f"Authenticated! Slot data: {json.dumps(self.slot_data)[:200]}")
                self.log(f"Server reports {len(checked)} already-checked locations")
                self.write_status("authenticated")

            elif cmd == "ConnectionRefused":
                errors = packet.get("errors", [])
                self.log(f"Connection refused: {errors}")
                self.write_status(f"refused: {errors}")

            elif cmd == "ReceivedItems":
                items = packet.get("items", [])
                index = packet.get("index", 0)
                self.log(f"Received {len(items)} items (index {index})")

                for item in items:
                    item_id = item.get("item", 0)
                    self.unlock_skill(item_id)

                self.write_status("authenticated")

            elif cmd == "PrintJSON":
                # Chat/event messages — just log
                msg_data = packet.get("data", [])
                text = "".join(d.get("text", "") for d in msg_data if isinstance(d, dict))
                if text:
                    self.log(f"AP: {text}")

            elif cmd == "RoomUpdate":
                # May contain updated checked_locations
                checked = packet.get("checked_locations", [])
                if checked:
                    self.checked_locations.update(checked)

            elif cmd == "Bounced":
                pass  # DeathLink or other bounced packets

            elif cmd == "DataPackage":
                pass  # Game data lookup tables

            elif cmd == "InvalidPacket":
                self.log(f"Server rejected packet: {packet.get('text', 'unknown')}")


if __name__ == "__main__":
    import traceback
    log_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ap_bridge_log.txt")
    try:
        from ap_bridge_gui import main as gui_main
        gui_main()
    except Exception as e:
        with open(log_path, "w") as f:
            f.write(f"Bridge GUI crashed:\n{traceback.format_exc()}\n")
        raise
