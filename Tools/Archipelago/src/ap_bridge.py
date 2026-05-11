"""
Diablo II Archipelago Bridge (Console Edition)
Runs as background process, started by launcher.
Waits for ap_command.dat from the game DLL, then connects to AP server.
All output goes to stdout (visible in launcher console window).

Usage: ap_bridge.exe --gamedir PATH

NOTE: the shipped ap_bridge.exe in Game/ap_bridge_dist/ is a PyInstaller-frozen
copy of THIS source file. Edits here do not take effect in the binary until
someone re-runs the PyInstaller build. See ap_bridge.spec for the build recipe.
"""
import asyncio
import json
import sys
import os
import time
import argparse
import ssl
import signal
import logging
import tempfile

try:
    import websockets
    from websockets.exceptions import ConnectionClosed
    # InvalidStatusCode was renamed to InvalidStatus in websockets 17.x.
    # Accept either to stay forward-compatible.
    try:
        from websockets.exceptions import InvalidStatusCode
    except ImportError:
        from websockets.exceptions import InvalidStatus as InvalidStatusCode  # type: ignore
except ImportError:
    print("ERROR: 'websockets' package not installed. Run: pip install websockets")
    sys.exit(1)

# AP protocol constants
AP_VERSION = {"major": 0, "minor": 5, "build": 0, "class": "Version"}
GAME_NAME = "Diablo II Archipelago"
AP_ITEM_BASE = 45000
CHECK_POLL_INTERVAL = 2
COMMAND_POLL_INTERVAL = 1

# Filler + zone-key ID ranges (per items.py)
FILLER_ID_MIN = 45500
FILLER_ID_MAX = 45599
ZONE_KEY_ID_MIN = 46001
ZONE_KEY_ID_MAX = 46038
SKILL_ID_MIN = 45006  # AP_ITEM_BASE + lowest skill D2-id
SKILL_ID_MAX = 45280  # AP_ITEM_BASE + highest skill D2-id
# 1.8.5 — gate-key ID range used by the DLL's zone-locking/F4 tracker.
# These are the per-difficulty gate-boss keys (18 gates × 3 diff = 54 IDs).
# Kept in sync with d2arch_zones.c GATEKEY_AP_BASE_* constants.
# Layout: each difficulty occupies 20 IDs starting at a base, but only the
# first 18 are valid gate keys (the last 2 in each block are unused gaps).
#   Normal:    46101-46118 valid, 46119-46120 GAP
#   Nightmare: 46121-46138 valid, 46139-46140 GAP
#   Hell:      46141-46158 valid
GATE_KEY_ID_MIN = 46101
GATE_KEY_ID_MAX = 46158
GATEKEY_PER_DIFF = 18
GATEKEY_AP_BASE_NORMAL = 46101
GATEKEY_AP_BASE_NM     = 46121
GATEKEY_AP_BASE_HELL   = 46141


def decode_gate_key(item_id):
    """Return (diff_label, slot, gate_name) or None if not a valid gate key.
    Mirrors GateKey_FromAPId in d2arch_zones.c — accounts for the 2-id gap
    between difficulty ranges so bogus IDs (46119, 46120, 46139, 46140) are
    rejected instead of being mislabeled.

    Slot layout within each difficulty:
        0-3   = Act 1 Gates 1-4
        4-7   = Act 2 Gates 1-4
        8-11  = Act 3 Gates 1-4
        12-13 = Act 4 Gates 1-2 (Act 4 has only 2 gates)
        14-17 = Act 5 Gates 1-4
    """
    if GATEKEY_AP_BASE_NORMAL <= item_id < GATEKEY_AP_BASE_NORMAL + GATEKEY_PER_DIFF:
        diff_label, slot = "Normal", item_id - GATEKEY_AP_BASE_NORMAL
    elif GATEKEY_AP_BASE_NM <= item_id < GATEKEY_AP_BASE_NM + GATEKEY_PER_DIFF:
        diff_label, slot = "Nightmare", item_id - GATEKEY_AP_BASE_NM
    elif GATEKEY_AP_BASE_HELL <= item_id < GATEKEY_AP_BASE_HELL + GATEKEY_PER_DIFF:
        diff_label, slot = "Hell", item_id - GATEKEY_AP_BASE_HELL
    else:
        return None
    if   0 <= slot < 4:   gate_name = f"Act 1 Gate {slot + 1}"
    elif 4 <= slot < 8:   gate_name = f"Act 2 Gate {slot - 3}"
    elif 8 <= slot < 12:  gate_name = f"Act 3 Gate {slot - 7}"
    elif 12 <= slot < 14: gate_name = f"Act 4 Gate {slot - 11}"
    elif 14 <= slot < 18: gate_name = f"Act 5 Gate {slot - 13}"
    else:                 gate_name = f"Gate Slot {slot}"
    return (diff_label, slot, gate_name)

# Retry/backoff tuning
SEND_BACKOFF_SEQ = [1, 2, 4, 8]      # seconds, ceiling applied below
SEND_BACKOFF_MAX = 8
CONN_BACKOFF_SEQ = [5, 10, 20, 40, 60]
CONN_BACKOFF_MAX = 60
SEND_FAIL_STATUS_THRESHOLD = 10      # consecutive failures before UI warning

# 1.9.5 — heartbeat in ap_status.dat so the DLL can detect a dead bridge
# process (Gap 1 from AP_LIFECYCLE_AUDIT_2026-05-11.md). Bridge writes a
# `heartbeat=<unix_timestamp>` line every HEARTBEAT_INTERVAL seconds. DLL
# treats heartbeat older than ~90s OR file mtime older than ~120s as
# "ghost-dead" and respawns the bridge.
HEARTBEAT_INTERVAL = 30              # seconds — write heartbeat this often


# ---------------------------------------------------------------------------
# Zone-key ID -> name mapping (hardcoded fallback if items.py isn't importable)
# Kept in sync with Game/apworld/diablo2_archipelago/items.py ZONE_KEY_ITEMS.
# ---------------------------------------------------------------------------
_ZONE_KEY_NAMES = {
    46001: "Cold Plains Key",
    46002: "Burial Grounds Key",
    46003: "Stony Field Key",
    46004: "Dark Wood Key",
    46005: "Black Marsh Key",
    46006: "Tristram Key",
    46007: "Monastery Key",
    46008: "Jail & Cathedral Key",
    46009: "Catacombs Key",
    46010: "Andariel's Lair Key",
    46011: "Rocky Waste Key",
    46012: "Dry Hills Key",
    46013: "Far Oasis Key",
    46014: "Lost City Key",
    46015: "Palace Key",
    46016: "Arcane Sanctuary Key",
    46017: "Canyon of the Magi Key",
    46018: "Duriel's Lair Key",
    46019: "Spider Forest Key",
    46020: "Jungle Key",
    46021: "Kurast Key",
    46022: "Upper Kurast Key",
    46023: "Travincal Key",
    46024: "Durance of Hate Key",
    46025: "Outer Steppes Key",
    46026: "City of the Damned Key",
    46027: "River of Flame Key",
    46028: "Chaos Sanctuary Key",
    46029: "Bloody Foothills Key",
    46030: "Highlands Key",
    46031: "Caverns Key",
    46032: "Summit Key",
    46033: "Nihlathak Key",
    46034: "Worldstone Keep Key",
    46035: "Throne of Destruction Key",
    46036: "Zone Key (reserved 36)",
    46037: "Zone Key (reserved 37)",
    46038: "Zone Key (reserved 38)",
}


def zone_key_name(ap_item_id: int) -> str:
    """Map a zone-key AP item ID to its display name."""
    return _ZONE_KEY_NAMES.get(ap_item_id, f"Zone Key #{ap_item_id}")


def _atomic_write_text(path: str, content: str) -> None:
    """Write text to `path` atomically via temp file + os.replace.

    Race window with DLL's DeleteFileA is minimized: os.replace is atomic on
    Windows once the temp file exists and is closed. The DLL either sees the
    old file (reads + deletes), or the new file (reads both sets of unlocks).
    """
    dir_name = os.path.dirname(path) or "."
    fd, tmp_path = tempfile.mkstemp(prefix=".ap_tmp_", dir=dir_name, text=False)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as f:
            f.write(content)
            f.flush()
            try:
                os.fsync(f.fileno())
            except OSError:
                pass  # fsync can fail on some filesystems; replace still atomic
        os.replace(tmp_path, path)
    except Exception:
        # Clean up stray temp file on error
        try:
            os.remove(tmp_path)
        except OSError:
            pass
        raise


class D2ArchipelagoBridge:
    def __init__(self, server, slot, password, char_name, arch_dir, deathlink=False):
        self.server = server
        self.slot = slot
        self.password = password
        self.char_name = char_name
        self.arch_dir = arch_dir
        self.deathlink = deathlink
        self.ws = None
        self.authenticated = False
        self.checked_locations = set()
        self.slot_data = {}
        self._stop = False
        self.players = {}  # slot_id -> player_name
        self.location_owners_file = os.path.join(arch_dir, "ap_location_owners.dat")
        # 1.8.5 — second file used by the DLL F4 tracker to display where each
        # zone-key item is located across the multiworld. Format per line:
        #   item_id=location_id|finder_slot|finder_player|recipient_slot
        # `finder_slot` is the player whose location must be checked to release
        # the item. `recipient_slot` is the player who receives it. Both can be
        # the same in single-player or self-placement multiworlds.
        self.item_locations_file = os.path.join(arch_dir, "ap_item_locations.dat")
        self._item_locations = {}      # ap_item_id -> "loc_id|finder_slot|finder_name|recipient_slot"

        # 1.9.0: per-character files moved to Game/Save/ alongside .d2s.
        # arch_dir is .../Game/Archipelago/, so the sibling Save/ is one
        # level up + "Save". Falls back to arch_dir if Save/ doesn't
        # resolve (degenerate fresh-install case).
        save_dir = os.path.normpath(os.path.join(arch_dir, "..", "Save"))
        if not os.path.isdir(save_dir):
            save_dir = arch_dir
        self.save_dir = save_dir + os.sep if not save_dir.endswith(os.sep) else save_dir

        # File paths (state + checks now per-character in Save/)
        self.state_file = os.path.join(self.save_dir, f"d2arch_state_{char_name}.dat")
        self.checks_file = os.path.join(self.save_dir, f"d2arch_checks_{char_name}.dat")
        # Legacy fallback paths so a freshly-upgraded install can still
        # find files the user hasn't yet migrated. The DLL handles the
        # one-time move at OnCharacterLoad; the bridge just probes both.
        self._state_file_legacy = os.path.join(arch_dir, f"d2arch_state_{char_name}.dat")
        self._checks_file_legacy = os.path.join(arch_dir, f"d2arch_checks_{char_name}.dat")
        if not os.path.exists(self.state_file) and os.path.exists(self._state_file_legacy):
            self.state_file = self._state_file_legacy
        if not os.path.exists(self.checks_file) and os.path.exists(self._checks_file_legacy):
            self.checks_file = self._checks_file_legacy
        self.status_file = os.path.join(arch_dir, "ap_status.dat")
        self.unlocks_file = os.path.join(arch_dir, "ap_unlocks.dat")
        # 1.8.4: Dedup is now PER-CHARACTER. Previously a single global
        # `d2arch_bridge_locations.dat` tracked which locations had been
        # delivered to ap_unlocks.dat — but items are applied to the
        # per-character .d2s, so when the player switched characters within
        # the same slot the new character would have ALL items skipped
        # ("DEDUP: skipping already-processed location ...") and end up with
        # zero starter skills, zero gate keys, zero everything.
        # Per-character file: d2arch_bridge_locations_<char>.dat
        # Legacy global file is ignored (left in place but not read/written)
        # so existing installs won't be retroactively confused.
        self.legacy_processed_file = os.path.join(
            arch_dir, "d2arch_bridge_locations.dat")
        self.processed_file = self._processed_file_for_char(char_name)

        # Send-retry state for check uploads
        self.pending_checks = []       # list of AP loc IDs not yet acknowledged
        self.send_attempt = 0           # current backoff step (0-based)
        self.failed_send_count = 0      # consecutive send failures

        # 1.7.1: Dedup by AP LOCATION id (persisted). Each AP check delivers
        # exactly one item from one unique location, so location-based dedup
        # correctly handles stackable fillers (gold, stat pts, skill pts, etc.)
        # while preventing re-application of already-processed items on reconnect
        # FOR THE SAME CHARACTER.
        self.processed_locations: set[int] = set()
        self._load_processed_set()

        # Skill database
        self.skills = {}
        self.load_skills()
        self.items_received = sum(1 for s in self.skills.values() if s['unlocked'])

        # 1.9.5 — Gap 1: persistent _last_status so the periodic heartbeat
        # task can re-write the current status without changing it.
        self._last_status = "disconnected"

        # 1.9.5 — Gap 3: per-character AP spoiler + checklist files. The
        # spoiler lists what items are at MY locations (so I know what
        # rewards to look forward to and who they'll go to in a
        # multiworld). The checklist is a running log of received items.
        # Both live in Game/Save/ alongside .d2s.
        self.spoiler_file   = os.path.join(self.save_dir,
                                           f"d2arch_ap_spoiler_{char_name}.txt")
        self.checklist_file = os.path.join(self.save_dir,
                                           f"d2arch_ap_checklist_{char_name}.txt")
        # location_id -> dict(item_id, item_name, recipient_slot, recipient_name)
        # populated from LocationInfo packet (response to LocationScouts)
        self._spoiler_data: dict[int, dict] = {}
        # location_ids that have been delivered ([RECEIVED] in spoiler)
        self._spoiler_received_locs: set[int] = set()
        # slot_info dict from Connected packet: slot -> {"name":, "game":}
        self.slot_info: dict[int, dict] = {}
        # 1.9.5 Gap 6 fix — our own slot id, captured from the Connected
        # packet's top-level "slot" field. Previously we read
        # `slot_data.get("slot")` which is the wrong place: slot_data is the
        # per-slot YAML options dict, which has no "slot" key, so it always
        # returned 0 and the spoiler reported all our own items as
        # "for Slot0". The real id lives in `packet.slot`.
        self.my_slot_id: int = 0

        # 1.9.5 Gap 2 fix — event used to break out of the exponential-backoff
        # sleep inside connect_and_run. main_loop sets it when the player
        # clicks "Force Reconnect" while the bridge is currently sleeping
        # between retries. Created here as None so __init__ doesn't require
        # a running event loop; we lazy-create in the first sleep call.
        self._force_reconnect_event = None

        # 1.9.5 — Gap 6 surface: snapshot of pending check count + last
        # error so the DLL can render them in F1 Editor Page 2. Updated
        # whenever write_status is called.
        self._last_pending_checks = 0
        self._last_conn_attempt   = 0

    # ------------------------------------------------------------------
    # Logging
    # ------------------------------------------------------------------
    def log(self, msg, level=logging.INFO):
        """Log via stdlib logging; still prints timestamped line to stdout."""
        ts = time.strftime("%H:%M:%S")
        line = f"[{ts}] {msg}"
        print(line, flush=True)
        try:
            logging.log(level, msg)
        except Exception:
            pass

    # ------------------------------------------------------------------
    # File helpers
    # ------------------------------------------------------------------
    def _find_file(self, prefix, suffix=".dat"):
        """Find a file by prefix, scanning Save/ first then Archipelago/ for legacy.
        1.9.0: per-char files now live in Game/Save/. Legacy installs may
        still have them in Game/Archipelago/, so we probe both."""
        import glob
        candidates = []
        for d in (getattr(self, "save_dir", None), self.arch_dir):
            if not d:
                continue
            candidates.extend(glob.glob(os.path.join(d, f"{prefix}*{suffix}")))
        if candidates:
            return max(candidates, key=os.path.getmtime)
        return None

    def _processed_file_for_char(self, char_name):
        """Per-character dedup file path. 1.9.0: lives in Save/ alongside .d2s."""
        d = getattr(self, "save_dir", None) or self.arch_dir
        return os.path.join(d, f"d2arch_bridge_locations_{char_name}.dat")

    def _char_name_from_checks_file(self, checks_path):
        """Extract '<char>' from '.../d2arch_checks_<char>.dat'."""
        base = os.path.basename(checks_path)
        if base.startswith("d2arch_checks_") and base.endswith(".dat"):
            return base[len("d2arch_checks_"):-len(".dat")]
        return None

    def _switch_processed_file_for_char(self, new_char):
        """Switch the dedup set to a different character's file.

        On character change the new character must receive ALL items the slot
        owns (starter inventory + items earned by previously-completed checks),
        because the DLL applies items to per-character `.d2s` state and the new
        character has none of them yet. We accomplish this by giving the new
        character its own dedup file: if it doesn't exist, the set starts
        empty, ReceivedItems on the next reconnect/replay floods every item
        through to ap_unlocks.dat, and the DLL applies them fresh."""
        new_path = self._processed_file_for_char(new_char)
        if new_path == self.processed_file:
            return
        self.log(f"Switching dedup to per-char file: "
                 f"{os.path.basename(new_path)}")
        self.processed_file = new_path
        self.processed_locations = set()
        if os.path.exists(new_path):
            self._load_processed_set()
        # If the file doesn't exist yet, processed_locations stays empty —
        # this is correct: the next ReceivedItems replay from the AP server
        # will flood items through to this character.

        # 1.9.5 Gap 3 fix — also repoint the spoiler + checklist sidecar
        # files to the new character. Without this, mid-session character
        # swaps would keep appending to the previous character's checklist
        # and overwriting the wrong spoiler. The in-memory _spoiler_data
        # and _spoiler_received_locs caches are also reset so the rewrite
        # of the new char's spoiler doesn't carry over stale entries.
        self.spoiler_file = os.path.join(
            self.save_dir, f"d2arch_ap_spoiler_{new_char}.txt")
        self.checklist_file = os.path.join(
            self.save_dir, f"d2arch_ap_checklist_{new_char}.txt")
        self._spoiler_data = {}
        self._spoiler_received_locs = set()
        self.log(f"Spoiler+checklist rebound: "
                 f"{os.path.basename(self.spoiler_file)} / "
                 f"{os.path.basename(self.checklist_file)}")

    def load_skills(self):
        # Try configured state file, fallback to scanning
        state = self.state_file
        if not os.path.exists(state):
            found = self._find_file("d2arch_state_")
            if found:
                self.log(f"Found state file: {os.path.basename(found)}")
                self.state_file = found
                state = found
            else:
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
                        'name': name, 'cls': cls, 'unlocked': unlocked,
                        'ap_id': AP_ITEM_BASE + skill_id
                    }
        self.log(f"Loaded {len(self.skills)} skills from state file")

    # ------------------------------------------------------------------
    # Processed-location persistence (1.7.1: location-based dedup)
    # ------------------------------------------------------------------
    def _load_processed_set(self):
        """Load already-processed (sender_slot, AP location) pairs from disk.

        1.8.5 FIX: dedup key is now (sender_slot_id, location_id), NOT just
        location_id. AP location IDs are derived from quest_id and are GLOBAL
        across all players in a multiworld — every slot has location 42010
        for "Clear Blood Moor". The pre-1.8.5 single-int dedup collapsed
        multiple players' items at the same location into one set entry, so
        when player A found his own item at 42010 first and player B sent a
        skill to A at 42010 later, the second item was silently dropped.

        File format:
            New (1.8.5+):    `<sender_slot>:<location_id>` per line
            Legacy (pre-1.8.5): bare `<location_id>` — IGNORED on load
                because we can't know who the sender was. The DLL's per-
                character `d2arch_applied_<char>.dat` is the safety net
                that prevents duplicate apply if the AP server replays the
                same items.
        """
        if not os.path.exists(self.processed_file):
            return
        try:
            new_count, legacy_skipped = 0, 0
            with open(self.processed_file, 'r', encoding='utf-8') as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    if ':' in line:
                        slot_str, loc_str = line.split(':', 1)
                        try:
                            self.processed_locations.add(
                                (int(slot_str), int(loc_str)))
                            new_count += 1
                        except ValueError:
                            continue
                    else:
                        # Legacy single-int format — can't recover sender
                        legacy_skipped += 1
            msg = f"Loaded {new_count} previously-processed (sender,location) pairs"
            if legacy_skipped:
                msg += (f"; ignored {legacy_skipped} legacy single-int entries "
                        f"(DLL applied-id dedup will catch duplicate items)")
            self.log(msg)
        except OSError as e:
            self.log(f"Failed to load processed set: {e}", level=logging.WARNING)

    def _append_processed(self, sender_slot_id: int, ap_location_id: int):
        """Persist a single (sender_slot, location) pair to the dedup file."""
        key = (sender_slot_id, ap_location_id)
        self.processed_locations.add(key)
        try:
            with open(self.processed_file, 'a', encoding='utf-8') as f:
                f.write(f"{sender_slot_id}:{ap_location_id}\n")
                f.flush()
                try:
                    os.fsync(f.fileno())
                except OSError:
                    pass
        except OSError as e:
            self.log(f"Failed to append processed (slot={sender_slot_id}, "
                     f"loc={ap_location_id}): {e}",
                     level=logging.WARNING)

    # ------------------------------------------------------------------
    # Status & unlock files
    # ------------------------------------------------------------------
    def write_status(self, status=None, send_errors=None, errormsg=None,
                     conn_attempt=None, pending_checks=None):
        """Write status snapshot to ap_status.dat.

        1.9.5 changes:
          - `status` is now optional; pass None to re-emit the current
            persistent status (used by _heartbeat_task to refresh the
            heartbeat without changing state).
          - Always emits `heartbeat=<unix_timestamp>` so the DLL can
            detect ghost-dead bridges (process alive but stuck).
          - Tracks `conn_attempt` + `pending_checks` for F1 UI surface.
        """
        if status is not None:
            self._last_status = status
        if conn_attempt is not None:
            self._last_conn_attempt = conn_attempt
        if pending_checks is not None:
            self._last_pending_checks = pending_checks
        try:
            lines = [
                f"status={self._last_status}",
                f"items_received={self.items_received}",
                f"checks_sent={len(self.checked_locations)}",
                f"heartbeat={int(time.time())}",
            ]
            if self._last_pending_checks:
                lines.append(f"pending_checks={self._last_pending_checks}")
            if self._last_conn_attempt:
                lines.append(f"conn_attempt={self._last_conn_attempt}")
            if send_errors is not None:
                lines.append(f"send_errors={send_errors}")
            if errormsg is not None:
                lines.append(f"errormsg={errormsg}")
            _atomic_write_text(self.status_file, "\n".join(lines) + "\n")
        except Exception as e:
            # Best-effort; never let status writes kill the bridge
            try:
                logging.warning(f"write_status failed: {e}")
            except Exception:
                pass

    # ------------------------------------------------------------------
    # 1.9.5 — Heartbeat task (Gap 1)
    # ------------------------------------------------------------------
    async def _heartbeat_task(self):
        """Refresh ap_status.dat heartbeat every HEARTBEAT_INTERVAL.

        The DLL watches this timestamp to detect a ghost-dead bridge
        (process alive but stuck in some bad state, OR background task
        scheduler hung). If the heartbeat hasn't advanced in ~90s the
        DLL treats the bridge as dead and respawns it.
        """
        while not self._stop:
            try:
                await asyncio.sleep(HEARTBEAT_INTERVAL)
                # Update pending_checks counter so the F1 UI stays current
                pc = len(self.pending_checks) if self.pending_checks else 0
                self.write_status(pending_checks=pc)
            except asyncio.CancelledError:
                raise
            except Exception as e:
                try:
                    self.log(f"heartbeat task error: {e}",
                             level=logging.WARNING)
                except Exception:
                    pass

    # ------------------------------------------------------------------
    # 1.9.5 Gap 2 fix — interruptible backoff sleep
    # ------------------------------------------------------------------
    async def _interruptible_sleep(self, delay):
        """Sleep up to `delay` seconds, but return early if the caller
        externally invokes kick_reconnect(). This makes Force Reconnect
        responsive even when connect_and_run is mid-backoff (otherwise
        the user could wait up to 60s for the next retry attempt)."""
        if self._force_reconnect_event is None:
            try:
                self._force_reconnect_event = asyncio.Event()
            except Exception:
                # No running loop in some edge case — fall back to plain sleep
                await asyncio.sleep(delay)
                return
        try:
            await asyncio.wait_for(
                self._force_reconnect_event.wait(),
                timeout=delay,
            )
            # Event was set — clear it for the next call and return early.
            self._force_reconnect_event.clear()
            try:
                self.log("Backoff interrupted by Force Reconnect — retrying now")
            except Exception:
                pass
        except asyncio.TimeoutError:
            # Normal timeout — the delay expired before any kick.
            pass

    def kick_reconnect(self):
        """Break the current backoff sleep (if any) and retry immediately.
        Safe to call from any task; no-op if no event has been created
        yet (which means we're not currently sleeping in backoff)."""
        ev = self._force_reconnect_event
        if ev is not None:
            try:
                ev.set()
            except Exception:
                pass

    # ------------------------------------------------------------------
    # 1.9.5 — AP spoiler + checklist files (Gap 3)
    # ------------------------------------------------------------------
    def _resolve_item_name_for_spoiler(self, item_id: int, recipient_slot: int) -> str:
        """Best-effort item name for spoiler. Uses our own item maps for
        D2A items, falls back to '(item X from <game>)' for other games."""
        # Our own item ID ranges (see items.py)
        if ZONE_KEY_ID_MIN <= item_id <= ZONE_KEY_ID_MAX:
            return zone_key_name(item_id)
        if GATE_KEY_ID_MIN <= item_id <= GATE_KEY_ID_MAX:
            # 1.9.5 Gap 5 fix — use the gap-aware decoder so NM/Hell keys
            # report the correct slot and bogus IDs in the 2-id gaps don't
            # masquerade as valid keys.
            gk = decode_gate_key(item_id)
            if gk is None:
                return f"Gate Key (invalid id {item_id})"
            diff_label, slot, gate_name = gk
            return f"Gate Key: {gate_name} ({diff_label})"
        if FILLER_ID_MIN <= item_id <= FILLER_ID_MAX:
            filler_names = {
                45500: "Gold", 45501: "Gold Bundle (M)", 45502: "Gold Bundle (L)",
                45503: "+5 Stat Points", 45504: "+1 Skill Point",
                45505: "Trap: Monsters", 45506: "Reset Point",
                45507: "Boss Loot", 45508: "Experience",
                45511: "Trap: Slow", 45512: "Trap: Weaken", 45513: "Trap: Poison",
                45514: "Andariel Loot", 45515: "Duriel Loot",
                45516: "Mephisto Loot", 45517: "Diablo Loot", 45518: "Baal Loot",
                45519: "Random Charm", 45520: "Random Set Item",
                45521: "Random Unique",
            }
            return filler_names.get(item_id, f"Filler #{item_id}")
        # Skill range — look up in our skill map
        if AP_ITEM_BASE <= item_id < AP_ITEM_BASE + 500:
            skill_id = item_id - AP_ITEM_BASE
            if skill_id in self.skills:
                return self.skills[skill_id]['name']
            return f"Skill ID {skill_id}"
        # Cross-game item — we don't have the datapackage. Best we can do:
        info = self.slot_info.get(recipient_slot, {})
        game = info.get("game", "?")
        return f"(item {item_id} from {game})"

    def _write_spoiler_file(self):
        """Write the per-character AP spoiler file. Called whenever
        _spoiler_data or _spoiler_received_locs changes."""
        try:
            # 1.9.5 Gap 6 fix — use the slot id captured from the Connected
            # packet's top-level "slot" field, not slot_data.get("slot")
            # which would always be 0.
            our_slot = int(getattr(self, "my_slot_id", 0))
            our_name = self.players.get(our_slot, self.slot)
            lines = [
                "=" * 70,
                f"D2Archipelago — AP Slot Spoiler",
                f"Character: {self.char_name}",
                f"Slot: {our_name} (#{our_slot})",
                f"Server: {self.server}",
                f"Generated: {time.strftime('%Y-%m-%d %H:%M:%S')}",
                f"Items received: {self.items_received}",
                f"Locations at MY slot: {len(self._spoiler_data)}",
                f"Of these, RECEIVED so far: {len(self._spoiler_received_locs)}",
                "=" * 70,
                "",
                "Items at locations YOU need to check (your slot's check list):",
                "Status: [RECVD] = already delivered, [PENDING] = still to find",
                "",
            ]
            for loc_id in sorted(self._spoiler_data.keys()):
                entry = self._spoiler_data[loc_id]
                status = "[RECVD]" if loc_id in self._spoiler_received_locs else "[PENDING]"
                lines.append(
                    f"  {status}  Loc {loc_id:>6} -> {entry['item_name']:<40} "
                    f"recipient: {entry['recipient_name']}"
                )
            lines.append("")
            lines.append("=" * 70)
            _atomic_write_text(self.spoiler_file, "\n".join(lines) + "\n")
            self.log(f"Wrote AP spoiler ({len(self._spoiler_data)} locs, "
                     f"{len(self._spoiler_received_locs)} received) -> "
                     f"{os.path.basename(self.spoiler_file)}")
        except Exception as e:
            self.log(f"Failed to write spoiler file: {e}",
                     level=logging.WARNING)

    def _append_to_checklist(self, item_name: str, sender_name: str,
                             ap_location_id: int):
        """Append a received item to the per-character checklist file.

        File grows append-only with one human-readable line per receive.
        Player can open this anytime to see what they've gotten.

        1.9.5 Gap 13 fix — ap_location_id=0 is rendered as "(event)" to
        accommodate non-item events like DeathLink bounces."""
        try:
            ts = time.strftime("%Y-%m-%d %H:%M:%S")
            if ap_location_id and ap_location_id > 0:
                loc_part = f"@ loc {ap_location_id}"
            else:
                loc_part = "@ (event)"
            line = (f"[{ts}] RECV  {item_name:<40} from {sender_name or '?':<20} "
                    f"{loc_part}\n")
            # Ensure file has header on first write
            if not os.path.exists(self.checklist_file):
                hdr = (
                    "=" * 70 + "\n"
                    f"D2Archipelago — AP Checklist for character '{self.char_name}'\n"
                    f"Server: {self.server} | Slot: {self.slot}\n"
                    "Each line is one received AP item — chronological log.\n"
                    "=" * 70 + "\n\n"
                )
                with open(self.checklist_file, "w", encoding="utf-8") as f:
                    f.write(hdr)
            with open(self.checklist_file, "a", encoding="utf-8") as f:
                f.write(line)
        except Exception as e:
            self.log(f"Failed to append checklist: {e}", level=logging.WARNING)

    def write_unlock(self, ap_item_id, item_name=None, sender_name=None,
                     ap_location_id=0):
        """Append an unlock to ap_unlocks.dat using atomic temp+rename.

        Preserves append semantics: reads existing content, appends the new
        unlock line, and swaps the file via os.replace. The DLL's DeleteFileA
        is tolerated: it either sees the old file (deletes it) or the new
        file (reads both unlocks at once). Either way, no unlock is lost.

        Line format evolution (DLL parser tolerates all three):
          1.8.4 added sender:    unlock=<id>|<sender>
          1.9.0 added location:  unlock=<id>|<sender>|<loc>
          legacy fallback:       unlock=<id>

        The location field lets the DLL mark the local quest complete
        when the item came from a self-released check (sender == g_apSlot
        AND loc >= 42000)."""
        path = self.unlocks_file
        existing = ""
        if os.path.exists(path):
            try:
                with open(path, 'r', encoding='utf-8') as f:
                    existing = f.read()
            except OSError:
                existing = ""
        new_content = existing
        if existing and not existing.endswith("\n"):
            new_content += "\n"
        if sender_name:
            # Sanitize: strip any '|' or newline that would break DLL parser.
            safe_sender = sender_name.replace("|", "_").replace("\n", "").replace("\r", "").strip()
            if ap_location_id and ap_location_id > 0:
                new_content += f"unlock={ap_item_id}|{safe_sender}|{ap_location_id}\n"
            else:
                new_content += f"unlock={ap_item_id}|{safe_sender}\n"
        else:
            if ap_location_id and ap_location_id > 0:
                new_content += f"unlock={ap_item_id}||{ap_location_id}\n"
            else:
                new_content += f"unlock={ap_item_id}\n"
        try:
            _atomic_write_text(path, new_content)
        except Exception as e:
            self.log(f"Failed to write unlock {ap_item_id}: {e}",
                     level=logging.ERROR)
            return False
        label = item_name if item_name else f"item {ap_item_id}"
        from_part = f" from {sender_name}" if sender_name else ""
        loc_part = f" loc={ap_location_id}" if ap_location_id else ""
        # 1.9.5 Bug 15 fix — was INFO, now DEBUG. Redundant with the
        # surrounding UNLOCKED/RECEIVED log lines that already say
        # everything important. This was firing per item on every
        # ReceivedItems replay.
        self.log(f"UNLOCK written: {label} (id={ap_item_id}){from_part}{loc_part}",
                 level=logging.DEBUG)
        return True

    # ------------------------------------------------------------------
    # Incoming item handling
    # ------------------------------------------------------------------
    def process_item(self, ap_item_id, ap_location_id=0, sender_name="",
                     sender_slot_id=0):
        """Process a received AP item (skill, filler, or zone key).

        1.7.1: Dedup is done by AP LOCATION id (not item id). Each AP
        location is a unique one-time check that delivers one item. This
        correctly handles stackable fillers (gold/stat-pts/skill-pts/traps)
        where multiple checks grant the same item type — each check has a
        distinct location id, so all instances get applied.

        1.8.5 FIX: Dedup key is now (sender_slot_id, location_id), not just
        location_id. AP location IDs are global (every player has 42010 for
        "Clear Blood Moor"), so the old single-key dedup silently dropped
        items when player A's self-find at 42010 collided with player B's
        send-to-A at 42010. Now both are tracked separately.

        Reconnect replay protection: when the server resends ReceivedItems
        on reconnect, the (sender, location) pairs are the same — any
        already-applied pair is skipped.

        Skills/zone keys are non-stackable by design (unique per ap_item_id),
        but the (sender, location) dedup handles them correctly too.
        """
        # 1.7.1: location-based dedup for real check locations (positive IDs).
        # 1.8.4 FIX: location_id=-2 (starter inventory) bypasses dedup.
        # 1.8.5 FIX: dedup key is (sender_slot_id, location_id) tuple, not
        # bare location_id — see _load_processed_set docstring.
        dedup_key = (sender_slot_id, ap_location_id)
        if ap_location_id > 0 and dedup_key in self.processed_locations:
            # 1.9.5 Bug 15 fix — was INFO, now DEBUG. AP server replays the
            # full ReceivedItems set on every reconnect, so this line fires
            # thousands of times per session. The actual delivery (UNLOCKED
            # line) is what the user cares about.
            self.log(f"DEDUP: skipping already-processed (slot={sender_slot_id}, "
                     f"loc={ap_location_id}) (item {ap_item_id})",
                     level=logging.DEBUG)
            return

        # --- Zone Key branch ---
        if ZONE_KEY_ID_MIN <= ap_item_id <= ZONE_KEY_ID_MAX:
            name = zone_key_name(ap_item_id)
            self.log(f"ZONE KEY: {name}")
            if self.write_unlock(ap_item_id, name, sender_name=sender_name,
                                  ap_location_id=ap_location_id):
                if ap_location_id > 0:
                    self._append_processed(sender_slot_id, ap_location_id)
                self.items_received += 1
                # 1.9.5 — checklist + spoiler updates
                self._append_to_checklist(name, sender_name, ap_location_id)
                if ap_location_id > 0:
                    self._spoiler_received_locs.add(ap_location_id)
                    if self._spoiler_data:  # only rewrite if we have scout data
                        self._write_spoiler_file()
            return

        # --- Gate Key branch (1.9.0) ---
        # Gate keys (46101-46158) used to fall through to the skill
        # branch and get logged as "Skill ID 1109" etc. The DLL parses
        # them correctly via GateKey_FromAPId regardless, but the
        # bridge log was misleading and the unlock line wrote a
        # nonsense item-name.
        if GATE_KEY_ID_MIN <= ap_item_id <= GATE_KEY_ID_MAX:
            # 1.9.5 Gap 5/12 fix — use the gap-aware decoder so the log line
            # and the checklist entry both show the actual gate (Act/Gate)
            # and the right difficulty. The previous offset arithmetic
            # treated NM keys as if they started at 46119 (the gap) instead
            # of 46121, so every NM key was logged as a Nightmare key from
            # the WRONG slot, and Hell keys were similarly off-by-2.
            gk = decode_gate_key(ap_item_id)
            if gk is None:
                # ID in the GATE_KEY_ID_MIN..GATE_KEY_ID_MAX envelope but
                # inside one of the gaps (46119, 46120, 46139, 46140).
                # The DLL's GateKey_FromAPId would also reject these, so
                # do not write_unlock — fall through to log + ignore.
                self.log(f"GATE KEY: invalid id {ap_item_id} (gap range) — skipping",
                         level=logging.WARNING)
                return
            diff_label, slot, gate_name = gk
            name = f"Gate Key: {gate_name} ({diff_label})"
            self.log(f"GATE KEY: {name}")
            if self.write_unlock(ap_item_id, name, sender_name=sender_name,
                                  ap_location_id=ap_location_id):
                if ap_location_id > 0:
                    self._append_processed(sender_slot_id, ap_location_id)
                self.items_received += 1
                # 1.9.5 — checklist + spoiler updates
                self._append_to_checklist(name, sender_name, ap_location_id)
                if ap_location_id > 0:
                    self._spoiler_received_locs.add(ap_location_id)
                    if self._spoiler_data:
                        self._write_spoiler_file()
            return

        # --- Filler branch ---
        if FILLER_ID_MIN <= ap_item_id <= FILLER_ID_MAX:
            # 1.9.0: names harmonized with APworld items.py FILLER_ITEMS list.
            # IDs 45501/45502/45507 retired (kept here as legacy fallbacks for
            # in-flight seeds generated before the redesign).
            names = {
                45500: "Gold",
                45501: "Gold Bundle (Medium)",       # legacy
                45502: "Gold Bundle (Large)",        # legacy
                45503: "5 Stat Points",
                45504: "Skill Point",
                45505: "Trap: Monsters",
                45506: "Reset Point",
                45507: "Boss Loot Drop",             # legacy
                45508: "Experience",
                45511: "Trap: Slow",
                45512: "Trap: Weaken",
                45513: "Trap: Poison",
                45514: "Drop: Andariel Loot",
                45515: "Drop: Duriel Loot",
                45516: "Drop: Mephisto Loot",
                45517: "Drop: Diablo Loot",
                45518: "Drop: Baal Loot",
                45519: "Drop: Random Charm",
                45520: "Drop: Random Set Item",
                45521: "Drop: Random Unique",
            }
            name = names.get(ap_item_id, f"Filler #{ap_item_id}")
            self.log(f"RECEIVED: {name}")
            if self.write_unlock(ap_item_id, name, sender_name=sender_name,
                                  ap_location_id=ap_location_id):
                if ap_location_id > 0:
                    self._append_processed(sender_slot_id, ap_location_id)
                self.items_received += 1
                # 1.9.5 — checklist + spoiler updates
                self._append_to_checklist(name, sender_name, ap_location_id)
                if ap_location_id > 0:
                    self._spoiler_received_locs.add(ap_location_id)
                    if self._spoiler_data:
                        self._write_spoiler_file()
            return

        # --- Skill branch ---
        skill_id = ap_item_id - AP_ITEM_BASE

        if skill_id in self.skills:
            skill = self.skills[skill_id]
            if not skill['unlocked']:
                skill['unlocked'] = 1
                self.log(f"UNLOCKED: {skill['name']} [{skill['cls']}] (ID {skill_id})")
            name = skill['name']
        else:
            name = f"Skill ID {skill_id}"
            # 1.9.5 Bug 15 fix — was INFO, now DEBUG. Redundant with the
            # UNLOCK written line that fires immediately after. Together
            # they were doubling per-skill log volume on every reconnect.
            self.log(f"Skill ID {skill_id} (AP item {ap_item_id}) -> written to unlocks file",
                     level=logging.DEBUG)

        if self.write_unlock(ap_item_id, name, sender_name=sender_name,
                                  ap_location_id=ap_location_id):
            if ap_location_id > 0:
                self._append_processed(sender_slot_id, ap_location_id)
            self.items_received += 1
            # 1.9.5 — checklist + spoiler updates
            self._append_to_checklist(name, sender_name, ap_location_id)
            if ap_location_id > 0:
                self._spoiler_received_locs.add(ap_location_id)
                if self._spoiler_data:
                    self._write_spoiler_file()

    # ------------------------------------------------------------------
    # Check polling
    # ------------------------------------------------------------------
    def _find_checks_file(self):
        """Find the active checks file by scanning for the most recently modified
        d2arch_checks_*.dat. ALWAYS rescans (no early-return on cached path):
        the player can switch character mid-session, the DLL writes per-character
        files, and stale empty files from deleted characters can shadow the
        active one if we trust the cached path. Bug fix: previously bridge would
        latch onto whichever d2arch_checks_*.dat existed at first poll and never
        notice when a new character started writing a different file."""
        import glob
        # 1.9.0: scan Save/ first then Archipelago/ legacy fallback.
        matches = []
        for d in (getattr(self, "save_dir", None), self.arch_dir):
            if d:
                matches.extend(glob.glob(os.path.join(d, "d2arch_checks_*.dat")))
        if not matches:
            return None
        # Pick the most recently modified — that's the active character's file.
        best = max(matches, key=os.path.getmtime)
        if best != self.checks_file:
            self.log(f"Active checks file: {os.path.basename(best)}")
            self.checks_file = best
            # Character changed. Three things must follow per-character:
            #   1. State file reload — skills mapping is per char (pool seed
            #      + starter set vary), so AP item IDs must be re-mapped.
            #   2. Dedup file switch — each character has its own
            #      d2arch_bridge_locations_<char>.dat. New char = empty set,
            #      so AP server's ReceivedItems replay floods every item
            #      through to ap_unlocks.dat for the new character to apply.
            #   3. Trigger Sync to AP server so it replays ReceivedItems
            #      (handled in poll_checks via the next read_pending_checks
            #      cycle; the explicit Sync request is sent below).
            new_char = self._char_name_from_checks_file(best)
            self._reload_state_for_active_char()
            if new_char:
                self._switch_processed_file_for_char(new_char)
                # Update char_name so any subsequent operation that uses it
                # (e.g. uuid for goal completion) reflects the active char.
                self.char_name = new_char
                # Ask AP server to replay ReceivedItems so this character
                # gets all items the slot owns. Sent best-effort; if the
                # connection isn't ready yet we just rely on natural
                # ReceivedItems flow.
                self._request_sync()
        return best

    def _request_sync(self):
        """Send a Sync request so the AP server replays ReceivedItems.

        Called on character switch so the new character receives every item
        the slot owns. Safe to call when not connected — silently no-ops."""
        if self.ws is None or not self.authenticated:
            return
        try:
            import asyncio
            import json as _json
            msg = [{"cmd": "Sync"}]
            payload = _json.dumps(msg)
            # Schedule the send on the running loop without blocking. If the
            # send fails (connection dropped), reconnect logic will handle it.
            loop = asyncio.get_event_loop()
            asyncio.ensure_future(self.ws.send(payload), loop=loop)
            self.log("Sent Sync request to AP server (replay items for new char)")
        except Exception as e:
            self.log(f"Sync request skipped: {e}", level=logging.DEBUG)

    def _reload_state_for_active_char(self):
        """Re-find and re-load the state file when the active character changes.
        Keeps self.skills in sync with whichever character is currently playing."""
        found = self._find_file("d2arch_state_")
        if found and found != self.state_file:
            self.log(f"Active state file: {os.path.basename(found)}")
            self.state_file = found
            # Reload skills dictionary
            self.skills = {}
            try:
                self.load_skills()
                self.items_received = sum(
                    1 for s in self.skills.values() if s['unlocked'])
            except Exception as e:
                self.log(f"Failed to reload skills: {e}", level=logging.WARNING)

    def read_pending_checks(self):
        """Read the checks file, return the list of AP loc IDs not yet marked sent.

        IMPORTANT: does NOT mutate self.checked_locations. Callers add IDs to
        checked_locations only after a successful send.
        """
        LOCATION_BASE = 42000  # Must match apworld locations.py
        checks_path = self._find_checks_file()
        if not checks_path:
            return []
        pending = []
        seen_local = set()  # avoid duplicates within this read
        try:
            with open(checks_path, 'r') as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("check="):
                        try:
                            quest_id = int(line.split('=')[1])
                        except (ValueError, IndexError):
                            self.log(f"Malformed check line: {line}",
                                     level=logging.WARNING)
                            continue
                        ap_loc_id = LOCATION_BASE + quest_id
                        if ap_loc_id in self.checked_locations:
                            continue
                        if ap_loc_id in seen_local:
                            continue
                        seen_local.add(ap_loc_id)
                        pending.append(ap_loc_id)
        except FileNotFoundError:
            pass
        except Exception as e:
            self.log(f"Error reading checks file: {e}", level=logging.ERROR)
        return pending

    async def poll_checks(self):
        """Periodically read checks file and send new locations to AP server.

        Only marks locations as `checked_locations` after ws.send completes
        without exception. Uses exponential backoff (1s,2s,4s,8s capped) on
        consecutive send failures; surfaces `send_errors=N` to ap_status.dat
        once failures exceed SEND_FAIL_STATUS_THRESHOLD.

        1.9.0 — earlier "ap_resync.flag" handler removed: it cleared
        dedup on every character load which caused stackable fillers
        (gold/XP/stat pts) to re-apply on every login. The per-character
        dedup file alone is the correct mechanism — it persists across
        sessions and naturally lets the AP server's reconnect-replay
        deliver only the items not yet received by this character.
        """
        while not self._stop:
            await asyncio.sleep(CHECK_POLL_INTERVAL)
            if not self.authenticated or self.ws is None:
                continue

            # Union of fresh file contents with anything left in pending.
            fresh = self.read_pending_checks()
            combined = list(self.pending_checks)
            for c in fresh:
                if c not in combined:
                    combined.append(c)

            if not combined:
                continue

            try:
                msg = [{"cmd": "LocationChecks", "locations": combined}]
                await self.ws.send(json.dumps(msg))
            except ConnectionClosed as e:
                # Keep items in pending; main loop will reconnect.
                self.pending_checks = combined
                self.failed_send_count += 1
                self.send_attempt = min(self.send_attempt + 1, len(SEND_BACKOFF_SEQ) - 1)
                self.log(f"Check send failed (ConnectionClosed): {e}. "
                         f"{len(combined)} checks pending.", level=logging.WARNING)
                if self.failed_send_count >= SEND_FAIL_STATUS_THRESHOLD:
                    self.write_status("authenticated",
                                      send_errors=self.failed_send_count)
                await asyncio.sleep(SEND_BACKOFF_SEQ[self.send_attempt])
                continue
            except Exception as e:
                # Unknown transient error: keep pending, back off.
                self.pending_checks = combined
                self.failed_send_count += 1
                self.send_attempt = min(self.send_attempt + 1, len(SEND_BACKOFF_SEQ) - 1)
                self.log(f"Check send failed: {e}. "
                         f"Retrying in {SEND_BACKOFF_SEQ[self.send_attempt]}s "
                         f"({len(combined)} pending).", level=logging.WARNING)
                if self.failed_send_count >= SEND_FAIL_STATUS_THRESHOLD:
                    self.write_status("authenticated",
                                      send_errors=self.failed_send_count)
                await asyncio.sleep(SEND_BACKOFF_SEQ[self.send_attempt])
                continue

            # Send succeeded — only now mark as checked.
            for loc_id in combined:
                self.checked_locations.add(loc_id)
            self.pending_checks = []
            self.failed_send_count = 0
            self.send_attempt = 0
            self.log(f"Sent {len(combined)} checks to AP")
            self.write_status("authenticated")

    async def poll_death(self):
        """Poll for player death file and send DeathLink bounce."""
        death_file = os.path.join(self.arch_dir, "ap_death.dat")
        while not self._stop:
            await asyncio.sleep(1)
            if not self.authenticated or not self.ws:
                continue
            if os.path.exists(death_file):
                try:
                    with open(death_file, 'r') as f:
                        data = dict(line.strip().split('=', 1) for line in f if '=' in line)
                    if data.get('death') == '1':
                        bounce = [{"cmd": "Bounce", "tags": ["DeathLink"], "data": {
                            "time": time.time(),
                            "source": self.slot,
                            "cause": data.get('cause', 'Died')
                        }}]
                        await self.ws.send(json.dumps(bounce))
                        self.log(f"DEATHLINK: Sent death from {self.slot}")
                    os.remove(death_file)
                except Exception as e:
                    self.log(f"DeathLink send error: {e}", level=logging.WARNING)

    async def poll_goal(self):
        """Poll for goal completion and send StatusUpdate to AP server."""
        goal_file = os.path.join(self.arch_dir, "ap_goal.dat")
        while not self._stop:
            await asyncio.sleep(2)
            if not self.authenticated or not self.ws:
                continue
            if os.path.exists(goal_file):
                try:
                    os.remove(goal_file)
                    status_msg = [{"cmd": "StatusUpdate", "status": 30}]  # CLIENT_GOAL
                    await self.ws.send(json.dumps(status_msg))
                    self.log("GOAL COMPLETE! Sent StatusUpdate(30) to AP server.")
                except Exception as e:
                    self.log(f"Goal send error: {e}", level=logging.WARNING)

    # ------------------------------------------------------------------
    # Connection / session loop
    # ------------------------------------------------------------------
    async def _connect_once(self, scheme: str):
        """Attempt a single connection + session. Returns "ok" on clean exit,
        "closed" if ws dropped (caller should retry), raises on fatal errors."""
        self.authenticated = False
        self.ws = None
        url = f"{scheme}://{self.server}"
        self.log(f"Connecting to {url}...")
        self.write_status("connecting")
        extra = {}
        if scheme == "wss":
            extra["ssl"] = ssl.create_default_context()

        async with websockets.connect(
            url, ping_interval=30, ping_timeout=10,
            open_timeout=10, **extra
        ) as ws:
            self.ws = ws
            self.log(f"Connected to {url}")
            self.write_status("connected")

            # Wait for RoomInfo
            raw = await ws.recv()
            data = json.loads(raw)
            if isinstance(data, list) and data:
                self.log(f"Server: {data[0].get('cmd', '?')}")

            # Send Connect (include DeathLink tag if enabled)
            tags = ["DeathLink"] if self.deathlink else []
            connect_msg = [{
                "cmd": "Connect",
                "password": self.password,
                "game": GAME_NAME,
                "name": self.slot,
                "uuid": f"d2arch_{self.char_name}_{int(time.time())}",
                "version": AP_VERSION,
                "items_handling": 0b111,
                "slot_data": True,
                "tags": tags
            }]
            await ws.send(json.dumps(connect_msg))
            self.log(f"Authenticating as '{self.slot}'...")

            # Start polling tasks. `_stop` is managed by the outer
            # connect_and_run loop and by shutdown(); do not reset here.
            # 1.9.5 Gap 8 fix — heartbeat task is now started at the
            # outer connect_and_run scope (so it covers handshake +
            # backoff). We no longer create one here.
            poll_task = asyncio.create_task(self.poll_checks())
            death_task = asyncio.create_task(self.poll_death())
            goal_task = asyncio.create_task(self.poll_goal())

            try:
                async for message in ws:
                    await self.handle_message(message)
            finally:
                self._stop = True
                for task in [poll_task, death_task, goal_task]:
                    task.cancel()
                    try:
                        await task
                    except asyncio.CancelledError:
                        pass

        return "closed"

    async def connect_and_run(self):
        """Run the session with automatic reconnect + backoff.

        On ConnectionClosed or transient network errors, retries with
        exponential backoff (5,10,20,40,60s). Writes `status=reconnecting`
        during retries. The persisted processed_ap_ids set is preserved
        across reconnects so filler/zone-key dedup survives.

        1.9.5 Gap 8 fix — the heartbeat task is started at the OUTER scope
        so it runs across the entire bridge lifetime, including the wss/ws
        handshake and the backoff sleep between attempts. Previously the
        heartbeat lived inside _connect_once and stopped firing during
        handshake/backoff, which could make the DLL ghost-detect a healthy
        bridge as dead on slow connections.
        """
        conn_attempt = 0

        # 1.9.5 Gap 8 fix — outer heartbeat task. It only writes heartbeat
        # (via write_status with no positional arg) and pending_checks; it
        # doesn't depend on self.ws or self.authenticated. Safe to run from
        # the first moment of the session through to shutdown.
        outer_hb_task = asyncio.create_task(self._heartbeat_task())

        try:
            while not self._stop:
                try:
                    # Prefer wss, fall back to ws if TLS fails on first attempt.
                    try:
                        result = await self._connect_once("wss")
                    except (ssl.SSLError, InvalidStatusCode, OSError) as tls_err:
                        self.log(f"WSS failed: {tls_err}; trying WS fallback...",
                                 level=logging.WARNING)
                        result = await self._connect_once("ws")

                    # If we successfully authenticated at any point during the
                    # session, reset the connection-retry counter so the next
                    # disconnect gets a fresh 5s backoff instead of the previous
                    # ceiling.
                    if self.authenticated:
                        conn_attempt = 0

                    # Clean exit from the session loop.
                    if result == "closed" and not self._stop:
                        self.log("Connection closed by server; will attempt reconnect.")
                        conn_attempt = min(conn_attempt + 1, len(CONN_BACKOFF_SEQ) - 1)
                        delay = CONN_BACKOFF_SEQ[conn_attempt]
                        # 1.9.5 — pass conn_attempt so F1 UI can show "attempt N/5"
                        self.write_status("reconnecting", conn_attempt=conn_attempt + 1)
                        self.log(f"Reconnecting in {delay}s (attempt {conn_attempt + 1})...")
                        # 1.9.5 Gap 2 — interruptible sleep so Force Reconnect kicks in
                        await self._interruptible_sleep(delay)
                        continue
                    # External stop requested
                    return

                except ConnectionClosed as e:
                    conn_attempt = min(conn_attempt + 1, len(CONN_BACKOFF_SEQ) - 1)
                    delay = CONN_BACKOFF_SEQ[conn_attempt]
                    self.log(f"ConnectionClosed: {e}. Reconnecting in {delay}s.",
                             level=logging.WARNING)
                    self.write_status("reconnecting", conn_attempt=conn_attempt + 1)
                    # 1.9.5 Gap 2 — interruptible sleep so Force Reconnect kicks in
                    await self._interruptible_sleep(delay)
                    continue
                except asyncio.CancelledError:
                    raise
                except Exception as e:
                    conn_attempt = min(conn_attempt + 1, len(CONN_BACKOFF_SEQ) - 1)
                    delay = CONN_BACKOFF_SEQ[conn_attempt]
                    self.log(f"Connect error: {e}. Retry in {delay}s.",
                             level=logging.ERROR)
                    self.write_status("error", errormsg=str(e)[:100],
                                      conn_attempt=conn_attempt + 1)
                    # 1.9.5 Gap 2 — interruptible sleep so Force Reconnect kicks in
                    await self._interruptible_sleep(delay)
                    continue
        finally:
            # Clean up the outer heartbeat task on exit (clean return, stop
            # requested, or exception). The inner _connect_once may also
            # spawn its own short-lived heartbeat task for legacy reasons;
            # both stop on _stop=True.
            outer_hb_task.cancel()
            try:
                await outer_hb_task
            except asyncio.CancelledError:
                pass
            except Exception:
                pass

    async def handle_message(self, raw):
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
                # 1.9.5 Gap 6 fix — our slot id is at the packet top level,
                # NOT inside slot_data. Capturing it here lets the spoiler
                # writer reliably distinguish "items for me" from "items for
                # other players" when scouting locations in a multiworld.
                try:
                    self.my_slot_id = int(packet.get("slot", 0))
                except (ValueError, TypeError):
                    self.my_slot_id = 0
                checked = packet.get("checked_locations", [])
                # Server-side ack: we can safely consider these already sent.
                self.checked_locations.update(checked)
                # 1.9.5: mark these locations as RECEIVED in the spoiler too
                self._spoiler_received_locs.update(checked)

                # Parse player info from Connected packet
                players_list = packet.get("players", [])
                for p in players_list:
                    if isinstance(p, dict):
                        self.players[p.get("slot", 0)] = p.get("alias", p.get("name", "???"))

                # 1.9.5 — capture slot_info (slot -> {name, game}) so the
                # spoiler can label cross-game items with their game name.
                slot_info_dict = packet.get("slot_info", {})
                if isinstance(slot_info_dict, dict):
                    for slot_key, info in slot_info_dict.items():
                        try:
                            self.slot_info[int(slot_key)] = info
                        except (ValueError, TypeError):
                            pass

                self.log(f"Authenticated! {len(checked)} checks done, {len(self.players)} players")
                self.write_status("authenticated")

                # Write slot_data to ap_settings.dat for the DLL to read
                try:
                    settings_file = os.path.join(self.arch_dir, "ap_settings.dat")
                    body = "".join(f"{key}={val}\n" for key, val in self.slot_data.items())
                    _atomic_write_text(settings_file, body)
                    self.log(f"Wrote {len(self.slot_data)} settings to ap_settings.dat")
                except Exception as e:
                    self.log(f"Failed to write ap_settings.dat: {e}",
                             level=logging.ERROR)

                # Scout all our locations to get item→player mapping
                try:
                    missing = packet.get("missing_locations", [])
                    if missing:
                        scout_msg = [{"cmd": "LocationScouts",
                                      "locations": list(missing),
                                      "create_as_hint": 0}]
                        await self.ws.send(json.dumps(scout_msg))
                        self.log(f"Scouting {len(missing)} locations for owner info...")
                except Exception as e:
                    self.log(f"Scout request failed: {e}", level=logging.WARNING)

            elif cmd == "ConnectionRefused":
                errors = packet.get("errors", [])
                self.log(f"REFUSED: {errors}", level=logging.ERROR)
                self.write_status("refused", errormsg=",".join(errors)[:100])

            elif cmd == "ReceivedItems":
                items = packet.get("items", [])
                self.log(f"Receiving {len(items)} items...")
                for item in items:
                    # 1.7.1: pass location_id so bridge can dedup correctly
                    # for stackable fillers (one grant per unique location).
                    # 1.8.4: pass sender slot's name so the DLL can show
                    # "<item> from <sender>" instead of "AP server" for items
                    # someone else found for us.
                    sender_slot = item.get("player", 0)
                    sender_name = self.players.get(sender_slot, "")
                    # An item the slot itself "found" still has player=own slot;
                    # leaving sender_name as the slot name is fine — the DLL
                    # compares against g_apSlot and will treat it as a self-find.
                    # 1.8.5: pass sender_slot_id so dedup distinguishes between
                    # different players' items at the same global location_id.
                    self.process_item(
                        item.get("item", 0),
                        item.get("location", 0),
                        sender_name=sender_name,
                        sender_slot_id=sender_slot,
                    )
                self.write_status("authenticated")

            elif cmd == "PrintJSON":
                parts = packet.get("data", [])
                text = "".join(d.get("text", "") for d in parts if isinstance(d, dict))
                if text:
                    self.log(f"AP: {text}")

            elif cmd == "RoomUpdate":
                checked = packet.get("checked_locations", [])
                if checked:
                    self.checked_locations.update(checked)

            elif cmd == "LocationInfo":
                # Response to LocationScouts — contains item info for each
                # location we scouted. Each `loc` dict has:
                #   "location" : location ID (in our world)
                #   "player"   : recipient slot — who gets the item
                #   "item"     : item ID being placed at this location
                #   "flags"    : item flags (progression bit etc.)
                locations_info = packet.get("locations", [])
                try:
                    body_lines = []
                    new_keys = 0
                    for loc in locations_info:
                        loc_id      = loc.get("location", 0)
                        player_slot = loc.get("player",   0)
                        item_id     = loc.get("item",     0)
                        player_name = self.players.get(player_slot, f"Player {player_slot}")
                        body_lines.append(f"{loc_id}={player_name}")

                        # 1.9.5 — capture for spoiler file
                        self._spoiler_data[loc_id] = {
                            "item_id":         item_id,
                            "item_name":       self._resolve_item_name_for_spoiler(item_id, player_slot),
                            "recipient_slot":  player_slot,
                            "recipient_name":  player_name,
                        }

                        # 1.8.5 — also record item placements for the DLL F4
                        # tracker. We store BOTH legacy zone-keys (46001-46038)
                        # AND the new gate-keys (46101-46158) so the F4 tracker
                        # can resolve any of them. `our_slot` is who finds the
                        # location; since this is a scout of OUR own missing
                        # locations, finder is always us. `player_slot` is the
                        # recipient. NOTE: this only captures keys placed in
                        # OUR own world. Cross-world placements (the typical
                        # multiworld case) require hint data, handled below.
                        is_zone_key = ZONE_KEY_ID_MIN <= item_id <= ZONE_KEY_ID_MAX
                        is_gate_key = GATE_KEY_ID_MIN <= item_id <= GATE_KEY_ID_MAX
                        if is_zone_key or is_gate_key:
                            # 1.9.5 Gap 6 fix — same slot id capture issue.
                            our_slot = int(getattr(self, "my_slot_id", 0))
                            our_name = self.players.get(our_slot, f"Slot{our_slot}")
                            value = f"{loc_id}|{our_slot}|{our_name}|{player_slot}"
                            if self._item_locations.get(item_id) != value:
                                self._item_locations[item_id] = value
                                new_keys += 1

                    _atomic_write_text(self.location_owners_file,
                                       "\n".join(body_lines) + ("\n" if body_lines else ""))
                    self.log(f"Wrote {len(locations_info)} location owners to file")

                    # Flush the item-locations cache to disk if we learned
                    # anything new. The DLL polls this file when F4 is open.
                    if new_keys > 0:
                        item_lines = [f"{k}={v}" for k, v in sorted(self._item_locations.items())]
                        _atomic_write_text(self.item_locations_file,
                                           "\n".join(item_lines) + "\n" if item_lines else "")
                        self.log(f"Updated ap_item_locations.dat (+{new_keys} keys, "
                                 f"total={len(self._item_locations)})")

                    # 1.9.5 — write the human-readable spoiler file
                    self._write_spoiler_file()
                except Exception as e:
                    self.log(f"Failed to write location owners: {e}",
                             level=logging.ERROR)

            elif cmd == "Bounced":
                if "DeathLink" in packet.get("tags", []):
                    source = packet.get("data", {}).get("source", "Unknown")
                    cause = packet.get("data", {}).get("cause", "Died")
                    self.log(f"DEATHLINK: {source} died ({cause})! Spawning trap...")
                    # 1.8.0 — Write source+cause to ap_deathlink_event.dat so
                    # the DLL can show "<player> died — trap incoming!" instead
                    # of a generic "AP TRAP!" popup.
                    try:
                        event_path = os.path.join(self.arch_dir, "ap_deathlink_event.dat")
                        with open(event_path, 'w', encoding='utf-8') as f:
                            f.write(f"source={source}\ncause={cause}\n")
                    except Exception as e:
                        self.log(f"Failed to write deathlink event file: {e}",
                                 level=logging.WARNING)
                    # Trap does NOT dedup — incoming death bounces are events,
                    # not persistent AP items. Bypass process_item entirely.
                    self.write_unlock(45505, "Death Link Trap")
                    # 1.9.5 Gap 13 fix — also append to checklist so the
                    # player can review who's been killing them. DeathLinks
                    # don't have an AP location id (they're bouncers, not
                    # locations), so ap_location_id is 0 — the checklist
                    # writer renders that as "(event)" instead of a loc id.
                    try:
                        cause_short = (cause or "Died").strip()
                        self._append_to_checklist(
                            f"Death Link Trap (from {source}: {cause_short})",
                            source, 0,
                        )
                    except Exception as e:
                        self.log(f"DeathLink checklist append failed: {e}",
                                 level=logging.WARNING)

            elif cmd == "InvalidPacket":
                text = packet.get('text', '?')
                self.log(f"Invalid packet: {text}", level=logging.ERROR)
                # Surface to DLL/UI so user knows something is wrong.
                self.write_status("error", errormsg=f"invalid_packet: {text}"[:100])

    def stop(self):
        self._stop = True
        # Flush final status
        try:
            self.write_status("disconnected")
        except Exception:
            pass

    async def shutdown(self):
        """Graceful shutdown: close ws cleanly."""
        self._stop = True
        if self.ws is not None:
            try:
                await self.ws.close()
            except Exception:
                pass
        try:
            self.write_status("disconnected")
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Command dispatch
# ---------------------------------------------------------------------------
def read_command(cmd_file):
    """Read and parse ap_command.dat, return dict or None."""
    if not os.path.exists(cmd_file):
        return None
    try:
        result = {}
        mtime = os.path.getmtime(cmd_file)
        with open(cmd_file, 'r') as f:
            for line in f:
                line = line.strip()
                if '=' in line:
                    key, val = line.split('=', 1)
                    result[key] = val
        result['_mtime'] = mtime
        return result if 'action' in result else None
    except Exception:
        return None


def clear_command(cmd_file):
    """Delete the command file after processing."""
    try:
        os.remove(cmd_file)
    except Exception:
        pass


# Module-level shutdown flag (set by signal handlers)
_SHUTDOWN_REQUESTED = False


def _install_signal_handlers():
    """Install SIGINT/SIGTERM handlers to set the shutdown flag cleanly."""
    def _handler(signum, frame):
        global _SHUTDOWN_REQUESTED
        _SHUTDOWN_REQUESTED = True
    for sig_name in ("SIGINT", "SIGTERM"):
        sig = getattr(signal, sig_name, None)
        if sig is not None:
            try:
                signal.signal(sig, _handler)
            except (ValueError, OSError):
                # signal() fails if not in main thread — ignore.
                pass


async def main_loop(game_dir):
    arch_dir = os.path.join(game_dir, "Archipelago")
    cmd_file = os.path.join(arch_dir, "ap_command.dat")

    ts = time.strftime("%H:%M:%S")
    print(f"[{ts}] AP Bridge started. Waiting for Connect command...", flush=True)

    bridge = None
    last_cmd_mtime = 0

    # Write initial status (includes heartbeat from 1.9.5 onwards)
    status_path = os.path.join(arch_dir, "ap_status.dat")
    try:
        _atomic_write_text(
            status_path,
            f"status=disconnected\nitems_received=0\nchecks_sent=0\n"
            f"heartbeat={int(time.time())}\n",
        )
    except Exception:
        pass

    # 1.9.5 — pre-bridge idle heartbeat. When no bridge instance exists,
    # we still need to keep ap_status.dat fresh so the DLL doesn't
    # ghost-detect us. Counter wraps every HEARTBEAT_INTERVAL ticks.
    pre_bridge_hb_ticks = 0

    while not _SHUTDOWN_REQUESTED:
        cmd = read_command(cmd_file)

        if cmd and cmd.get('_mtime', 0) != last_cmd_mtime:
            last_cmd_mtime = cmd.get('_mtime', 0)
            action = cmd.get('action', '')

            if action == 'connect':
                server = cmd.get('server', 'localhost:38281')
                slot = cmd.get('slot', '')
                password = cmd.get('password', '')
                char = cmd.get('char', '')
                deathlink = cmd.get('deathlink', '0') == '1'

                # If bridge is already running, disconnect first (character switch)
                if bridge is not None:
                    ts2 = time.strftime("%H:%M:%S")
                    print(f"[{ts2}] New connect command — disconnecting old bridge (char switch)...",
                          flush=True)
                    await bridge.shutdown()
                    bridge = None

                if slot:
                    bridge = D2ArchipelagoBridge(server, slot, password, char,
                                                  arch_dir, deathlink=deathlink)
                    clear_command(cmd_file)
                    # 1.9.5 Gap 2 fix — spawn bridge as a task and poll
                    # commands concurrently so a Force Reconnect (or a
                    # credential change) issued during the lifetime of
                    # this bridge can interrupt the backoff sleep or
                    # tear down the bridge entirely.
                    bridge_run_task = asyncio.create_task(
                        bridge.connect_and_run())
                    try:
                        while not _SHUTDOWN_REQUESTED and not bridge_run_task.done():
                            try:
                                await asyncio.wait_for(
                                    asyncio.shield(bridge_run_task),
                                    timeout=COMMAND_POLL_INTERVAL,
                                )
                            except asyncio.TimeoutError:
                                pass
                            if bridge_run_task.done():
                                break

                            cmd2 = read_command(cmd_file)
                            if cmd2 and cmd2.get('_mtime', 0) > last_cmd_mtime:
                                last_cmd_mtime = cmd2.get('_mtime', 0)
                                act2 = cmd2.get('action', '')
                                if act2 == 'connect':
                                    new_server = cmd2.get('server', 'localhost:38281')
                                    new_slot = cmd2.get('slot', '')
                                    new_password = cmd2.get('password', '')
                                    new_char = cmd2.get('char', '')
                                    new_dl = cmd2.get('deathlink', '0') == '1'
                                    same_creds = (
                                        new_server == bridge.server
                                        and new_slot == bridge.slot
                                        and new_password == bridge.password
                                        and new_char == bridge.char_name
                                        and new_dl == bridge.deathlink
                                    )
                                    if same_creds:
                                        ts2 = time.strftime("%H:%M:%S")
                                        print(f"[{ts2}] Force Reconnect "
                                              f"(same creds) — kicking backoff",
                                              flush=True)
                                        bridge.kick_reconnect()
                                        clear_command(cmd_file)
                                    else:
                                        ts2 = time.strftime("%H:%M:%S")
                                        print(f"[{ts2}] New connect with "
                                              f"different creds — restarting "
                                              f"bridge", flush=True)
                                        bridge._stop = True
                                        bridge.kick_reconnect()
                                        try:
                                            await bridge.shutdown()
                                        except Exception:
                                            pass
                                        try:
                                            await bridge_run_task
                                        except Exception:
                                            pass
                                        bridge = D2ArchipelagoBridge(
                                            new_server, new_slot,
                                            new_password, new_char,
                                            arch_dir, deathlink=new_dl)
                                        clear_command(cmd_file)
                                        bridge_run_task = asyncio.create_task(
                                            bridge.connect_and_run())
                                elif act2 == 'disconnect':
                                    bridge._stop = True
                                    bridge.kick_reconnect()
                                    try:
                                        await bridge.shutdown()
                                    except Exception:
                                        pass
                                    try:
                                        await bridge_run_task
                                    except Exception:
                                        pass
                                    bridge = None
                                    bridge_run_task = None
                                    clear_command(cmd_file)
                                    ts2 = time.strftime("%H:%M:%S")
                                    print(f"[{ts2}] Disconnected by user",
                                          flush=True)
                                    break
                    except asyncio.CancelledError:
                        if bridge:
                            try:
                                await bridge.shutdown()
                            except Exception:
                                pass
                        raise
                    except Exception as e:
                        ts2 = time.strftime("%H:%M:%S")
                        print(f"[{ts2}] Bridge error: {e}", flush=True)
                    finally:
                        # Ensure bridge task is fully drained before returning
                        if bridge_run_task and not bridge_run_task.done():
                            try:
                                await bridge_run_task
                            except Exception:
                                pass
                        if bridge:
                            try:
                                await bridge.shutdown()
                            except Exception:
                                pass
                        bridge = None
                        ts2 = time.strftime("%H:%M:%S")
                        print(f"[{ts2}] Disconnected. Waiting for new Connect command...",
                              flush=True)
                else:
                    ts2 = time.strftime("%H:%M:%S")
                    print(f"[{ts2}] Connect command missing slot name", flush=True)
                    clear_command(cmd_file)

            elif action == 'disconnect':
                if bridge is not None:
                    await bridge.shutdown()
                    bridge = None
                clear_command(cmd_file)
                ts2 = time.strftime("%H:%M:%S")
                print(f"[{ts2}] Disconnected by user", flush=True)

        # 1.9.5 — pre-bridge idle heartbeat. When `bridge is None` (no
        # active session), refresh ap_status.dat every HEARTBEAT_INTERVAL
        # ticks (~30s with COMMAND_POLL_INTERVAL=1s) so the DLL doesn't
        # ghost-detect the bridge as dead. Bridge-active heartbeats are
        # handled by D2ArchipelagoBridge._heartbeat_task.
        if bridge is None:
            pre_bridge_hb_ticks += 1
            if pre_bridge_hb_ticks >= HEARTBEAT_INTERVAL:
                pre_bridge_hb_ticks = 0
                try:
                    _atomic_write_text(
                        status_path,
                        f"status=disconnected\nitems_received=0\nchecks_sent=0\n"
                        f"heartbeat={int(time.time())}\n",
                    )
                except Exception:
                    pass

        await asyncio.sleep(COMMAND_POLL_INTERVAL)

    # Shutdown path
    if bridge is not None:
        await bridge.shutdown()


def _init_logging(arch_dir: str):
    """Configure stdlib logging.

    File handler writes to <arch_dir>/ap_bridge_log.txt. Level is DEBUG when
    AP_BRIDGE_DEBUG=1 in env, otherwise INFO.
    """
    try:
        os.makedirs(arch_dir, exist_ok=True)
    except OSError:
        pass
    log_path = os.path.join(arch_dir, "ap_bridge_log.txt")
    level = logging.DEBUG if os.environ.get("AP_BRIDGE_DEBUG", "0") == "1" else logging.INFO
    try:
        logging.basicConfig(
            filename=log_path,
            filemode="a",
            level=level,
            format="%(asctime)s %(levelname)s %(message)s",
        )
    except Exception:
        # If file handler fails (permissions, etc.), fall back to stderr.
        logging.basicConfig(level=level,
                            format="%(asctime)s %(levelname)s %(message)s")


def main():
    parser = argparse.ArgumentParser(description="Diablo II Archipelago Bridge")
    parser.add_argument("--gamedir", default=".", help="Game directory path")
    args = parser.parse_args()

    arch_dir = os.path.join(args.gamedir, "Archipelago")
    _init_logging(arch_dir)
    _install_signal_handlers()

    print("=" * 50)
    print("  Diablo II Archipelago - AP Bridge")
    print("  Waiting for connection command from game...")
    print("=" * 50, flush=True)

    try:
        asyncio.run(main_loop(args.gamedir))
    except KeyboardInterrupt:
        print("\nBridge stopped.", flush=True)


if __name__ == "__main__":
    main()
