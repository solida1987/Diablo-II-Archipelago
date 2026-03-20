"""Diablo II 1.14b memory reading for player state.

Reads the player's current area, experience, and kill tracking from
the running Game.exe process. Uses ctypes for Windows process memory access.

Memory offsets derived from D2 struct definitions (d2structs.h).
In 1.14b/1.14d all DLLs are merged into Game.exe.
"""

import ctypes
import ctypes.wintypes
import struct

# Windows API
kernel32 = ctypes.windll.kernel32
advapi32 = ctypes.windll.advapi32
PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400

# D2 1.14b memory offsets (relative to Game.exe base)
# The player unit global pointer (was in D2Client.dll, now merged into Game.exe)
PLAYER_UNIT_OFFSET = 0x003A6A70

# Struct offsets from d2structs.h (1.14 version):
#   unit_any.path    = 0x2C  (union at field index after act_no/act/seed/_2)
#   path.room1       = 0x1C
#   room1.room2      = 0x10
#   room2.level      = 0x58
#   level.level_no   = 0x1D0
#   unit_any.act_no  = 0x18
UNIT_PATH_OFFSET = 0x2C
UNIT_ACT_NO_OFFSET = 0x18
PATH_ROOM1_OFFSET = 0x1C
ROOM1_ROOM2_OFFSET = 0x10
ROOM2_LEVEL_OFFSET = 0x58
LEVEL_NO_OFFSET = 0x1D0

# Stat list offsets (for reading experience etc.)
STAT_LIST_OFFSET = 0x5C    # unit_any.stats (actually pStatListEx)
STAT_ARRAY_OFFSET = 0x48   # StatListEx -> full stats pointer (includes XP, level, etc.)
STAT_COUNT_OFFSET = 0x4C   # StatListEx -> full stat count (WORD)

# D2 stat IDs
STAT_EXPERIENCE = 13

# Default Game.exe base for 32-bit non-ASLR executables
DEFAULT_BASE = 0x00400000


def _enable_debug_privilege():
    """Enable SeDebugPrivilege for the current process.

    This allows reading memory of processes we didn't create,
    or processes with different security contexts.
    """
    try:
        TOKEN_ADJUST_PRIVILEGES = 0x0020
        TOKEN_QUERY = 0x0008
        SE_PRIVILEGE_ENABLED = 0x00000002

        class LUID(ctypes.Structure):
            _fields_ = [("LowPart", ctypes.wintypes.DWORD),
                        ("HighPart", ctypes.wintypes.LONG)]

        class TOKEN_PRIVILEGES(ctypes.Structure):
            _fields_ = [("PrivilegeCount", ctypes.wintypes.DWORD),
                        ("Luid", LUID),
                        ("Attributes", ctypes.wintypes.DWORD)]

        h_token = ctypes.wintypes.HANDLE()
        if not advapi32.OpenProcessToken(
            kernel32.GetCurrentProcess(),
            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
            ctypes.byref(h_token),
        ):
            return False

        luid = LUID()
        if not advapi32.LookupPrivilegeValueW(
            None, "SeDebugPrivilege", ctypes.byref(luid)
        ):
            kernel32.CloseHandle(h_token)
            return False

        tp = TOKEN_PRIVILEGES()
        tp.PrivilegeCount = 1
        tp.Luid = luid
        tp.Attributes = SE_PRIVILEGE_ENABLED

        result = advapi32.AdjustTokenPrivileges(
            h_token, False, ctypes.byref(tp),
            ctypes.sizeof(tp), None, None,
        )
        kernel32.CloseHandle(h_token)
        return bool(result)
    except Exception:
        return False


# Try to enable debug privilege on module load
_enable_debug_privilege()


class D2MemoryReader:
    """Read game state from a running Diablo II process."""

    def __init__(self):
        self._process_handle = None
        self._pid = None
        self._base_address = None
        self._debug_log: list[str] = []
        self._attach_error: str = ""
        self._owns_handle: bool = True
        self._player_unit_offset: int = PLAYER_UNIT_OFFSET

    def attach(self, pid: int, existing_handle: int | None = None) -> bool:
        """Attach to a running Game.exe process.

        Args:
            pid: Process ID of Game.exe.
            existing_handle: If provided, use this process handle instead of
                calling OpenProcess. Useful when we launched the process ourselves
                (subprocess.Popen._handle has PROCESS_ALL_ACCESS).
        """
        self._attach_error = ""
        try:
            if existing_handle is not None:
                # Use the handle directly from Popen (has PROCESS_ALL_ACCESS)
                # Don't close this handle on detach — Popen owns it
                self._owns_handle = False
                handle = int(existing_handle)
            else:
                self._owns_handle = True
                handle = kernel32.OpenProcess(
                    PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
                if not handle:
                    err = ctypes.GetLastError()
                    self._attach_error = f"OpenProcess failed (error {err})"
                    return False

            self._process_handle = handle
            self._pid = pid

            # Try to get module base via psapi
            self._base_address = self._get_module_base(pid)
            if self._base_address is None:
                # Fallback: Game.exe is 32-bit, non-ASLR, base is 0x400000
                self._base_address = DEFAULT_BASE
                self._attach_error = f"Using default base 0x{DEFAULT_BASE:X}"

            # Verify we can actually read from the process
            test = self._read_int(self._base_address)
            if test is None:
                self._attach_error = f"Cannot read memory at base 0x{self._base_address:X}"
                if self._owns_handle:
                    kernel32.CloseHandle(handle)
                self._process_handle = None
                self._base_address = None
                return False

            # Try to find the player unit offset for this D2 version
            found_offset = self.scan_for_player_unit()
            if found_offset is not None:
                self._player_unit_offset = found_offset
                self._attach_error = f"Found player unit at offset 0x{found_offset:X}"
            else:
                self._player_unit_offset = PLAYER_UNIT_OFFSET  # Fallback
                self._attach_error = "Using default offset (scan found nothing — character may not be in-game yet)"

            return True
        except Exception as e:
            self._attach_error = str(e)
            return False

    def detach(self):
        """Close the process handle."""
        if self._process_handle:
            if self._owns_handle:
                kernel32.CloseHandle(self._process_handle)
            self._process_handle = None
            self._pid = None
            self._base_address = None

    def is_attached(self) -> bool:
        return self._process_handle is not None

    def _get_module_base(self, pid: int) -> int | None:
        """Get the base address of Game.exe module."""
        try:
            psapi = ctypes.windll.psapi
            h_process = self._process_handle

            # For 64-bit Python reading 32-bit process, use EnumProcessModulesEx
            # with LIST_MODULES_32BIT flag
            LIST_MODULES_32BIT = 0x01
            h_modules = (ctypes.wintypes.HMODULE * 1024)()
            cb_needed = ctypes.wintypes.DWORD()

            # Try EnumProcessModulesEx first (handles 32/64 bit cross-reading)
            try:
                if psapi.EnumProcessModulesEx(
                    h_process,
                    ctypes.byref(h_modules),
                    ctypes.sizeof(h_modules),
                    ctypes.byref(cb_needed),
                    LIST_MODULES_32BIT,
                ):
                    if h_modules[0]:
                        return h_modules[0] & 0xFFFFFFFF  # Mask to 32-bit
            except AttributeError:
                pass  # EnumProcessModulesEx not available

            # Fallback to EnumProcessModules
            h_module = ctypes.wintypes.HMODULE()
            cb_needed = ctypes.wintypes.DWORD()

            if not psapi.EnumProcessModules(
                h_process,
                ctypes.byref(h_module),
                ctypes.sizeof(h_module),
                ctypes.byref(cb_needed),
            ):
                return None

            class MODULEINFO(ctypes.Structure):
                _fields_ = [
                    ("lpBaseOfDll", ctypes.c_void_p),
                    ("SizeOfImage", ctypes.wintypes.DWORD),
                    ("EntryPoint", ctypes.c_void_p),
                ]

            mod_info = MODULEINFO()
            if not psapi.GetModuleInformation(
                h_process,
                h_module,
                ctypes.byref(mod_info),
                ctypes.sizeof(mod_info),
            ):
                return None

            return mod_info.lpBaseOfDll

        except Exception:
            return None

    def _read_int(self, address: int) -> int | None:
        """Read a 4-byte unsigned integer from process memory."""
        if not self._process_handle:
            return None
        try:
            buf = ctypes.c_uint32()
            bytes_read = ctypes.c_size_t()
            if kernel32.ReadProcessMemory(
                self._process_handle,
                ctypes.c_void_p(address),
                ctypes.byref(buf),
                4,
                ctypes.byref(bytes_read),
            ):
                return buf.value
            return None
        except Exception:
            return None

    def _read_short(self, address: int) -> int | None:
        """Read a 2-byte unsigned short from process memory."""
        if not self._process_handle:
            return None
        try:
            buf = ctypes.c_uint16()
            bytes_read = ctypes.c_size_t()
            if kernel32.ReadProcessMemory(
                self._process_handle,
                ctypes.c_void_p(address),
                ctypes.byref(buf),
                2,
                ctypes.byref(bytes_read),
            ):
                return buf.value
            return None
        except Exception:
            return None

    def _read_bytes(self, address: int, size: int) -> bytes | None:
        """Read raw bytes from process memory."""
        if not self._process_handle:
            return None
        try:
            buf = (ctypes.c_byte * size)()
            bytes_read = ctypes.c_size_t()
            if kernel32.ReadProcessMemory(
                self._process_handle,
                ctypes.c_void_p(address),
                ctypes.byref(buf),
                size,
                ctypes.byref(bytes_read),
            ):
                return bytes(buf)
            return None
        except Exception:
            return None

    def _read_ptr(self, address: int) -> int | None:
        """Read a pointer (4 bytes on 32-bit D2)."""
        return self._read_int(address)

    def get_current_area(self, debug: bool = False) -> int | None:
        """Get the player's current area ID.

        Follows the full pointer chain from d2structs.h:
          [base + 0x3A6A70]  -> unit
          [unit + 0x2C]      -> path
          [path + 0x1C]      -> room1
          [room1 + 0x10]     -> room2
          [room2 + 0x58]     -> level
          [level + 0x1D0]    -> level_no (area ID)
        """
        if not self._base_address:
            return None

        # Step 1: Read player unit pointer
        unit_ptr = self._read_ptr(self._base_address + self._player_unit_offset)
        if debug:
            self._debug_log.append(
                f"unit_ptr @ base+0x{self._player_unit_offset:X} = "
                f"{hex(unit_ptr) if unit_ptr else 'None'}")
        if not unit_ptr:
            return None

        # Step 2: Read path pointer from unit struct
        path_ptr = self._read_ptr(unit_ptr + UNIT_PATH_OFFSET)
        if debug:
            self._debug_log.append(
                f"path_ptr @ unit+0x{UNIT_PATH_OFFSET:X} = "
                f"{hex(path_ptr) if path_ptr else 'None'}")
        if not path_ptr:
            return None

        # Step 3: Read room1 from path
        room1_ptr = self._read_ptr(path_ptr + PATH_ROOM1_OFFSET)
        if debug:
            self._debug_log.append(
                f"room1 @ path+0x{PATH_ROOM1_OFFSET:X} = "
                f"{hex(room1_ptr) if room1_ptr else 'None'}")
        if not room1_ptr:
            return None

        # Step 4: Read room2 from room1
        room2_ptr = self._read_ptr(room1_ptr + ROOM1_ROOM2_OFFSET)
        if debug:
            self._debug_log.append(
                f"room2 @ room1+0x{ROOM1_ROOM2_OFFSET:X} = "
                f"{hex(room2_ptr) if room2_ptr else 'None'}")
        if not room2_ptr:
            return None

        # Step 5: Read level from room2
        level_ptr = self._read_ptr(room2_ptr + ROOM2_LEVEL_OFFSET)
        if debug:
            self._debug_log.append(
                f"level @ room2+0x{ROOM2_LEVEL_OFFSET:X} = "
                f"{hex(level_ptr) if level_ptr else 'None'}")
        if not level_ptr:
            return None

        # Step 6: Read level_no from level
        level_no = self._read_int(level_ptr + LEVEL_NO_OFFSET)
        if debug:
            self._debug_log.append(
                f"level_no @ level+0x{LEVEL_NO_OFFSET:X} = {level_no}")
        if level_no is not None and 0 < level_no < 200:
            return level_no

        return None

    def get_stat(self, stat_id: int) -> int | None:
        """Read a player stat by ID from the stat list.

        Follows: unit -> pStatListEx -> base stats array, then
        iterates looking for the matching stat ID.
        Each stat entry is 8 bytes: DWORD (hiword=layer, loword=stat_id), DWORD (value).
        """
        if not self._base_address:
            return None

        unit_ptr = self._read_ptr(self._base_address + self._player_unit_offset)
        if not unit_ptr:
            return None

        stat_list_ptr = self._read_ptr(unit_ptr + STAT_LIST_OFFSET)
        if not stat_list_ptr:
            return None

        # Read pointer to stat array and count
        stats_ptr = self._read_ptr(stat_list_ptr + STAT_ARRAY_OFFSET)
        stat_count = self._read_short(stat_list_ptr + STAT_COUNT_OFFSET)

        if not stats_ptr or not stat_count or stat_count > 512:
            return None

        # Read all stat entries at once (8 bytes each)
        data = self._read_bytes(stats_ptr, stat_count * 8)
        if not data:
            return None

        # Search for our stat ID
        # D2 stat entry: lower 16 bits = layer/param, upper 16 bits = stat ID
        for i in range(stat_count):
            offset = i * 8
            id_layer = struct.unpack_from("<I", data, offset)[0]
            value = struct.unpack_from("<i", data, offset + 4)[0]
            entry_stat_id = (id_layer >> 16) & 0xFFFF
            if entry_stat_id == stat_id:
                return value

        return None

    def get_experience(self) -> int | None:
        """Get the player's current experience points."""
        return self.get_stat(STAT_EXPERIENCE)

    def debug_stats(self) -> list[str]:
        """Return debug info about the stat reading chain."""
        info = []
        if not self._base_address:
            return ["No base address"]

        unit_ptr = self._read_ptr(self._base_address + self._player_unit_offset)
        if not unit_ptr:
            return ["unit_ptr is None"]
        info.append(f"unit=0x{unit_ptr:X}")

        stat_list_ptr = self._read_ptr(unit_ptr + STAT_LIST_OFFSET)
        if not stat_list_ptr:
            return info + [f"stat_list @ unit+0x{STAT_LIST_OFFSET:X} = None"]
        info.append(f"stat_list=0x{stat_list_ptr:X}")

        # Try multiple offset pairs for the stat array
        for arr_off, cnt_off in [(0x24, 0x28), (0x48, 0x4C), (0x1C, 0x20)]:
            stats_ptr = self._read_ptr(stat_list_ptr + arr_off)
            stat_count = self._read_short(stat_list_ptr + cnt_off)
            if stats_ptr and stat_count and 0 < stat_count < 512:
                info.append(f"stats @ +0x{arr_off:X}: ptr=0x{stats_ptr:X} count={stat_count}")
                # Read first few stats
                data = self._read_bytes(stats_ptr, min(stat_count, 10) * 8)
                if data:
                    for i in range(min(stat_count, 10)):
                        offset = i * 8
                        id_layer = struct.unpack_from("<I", data, offset)[0]
                        value = struct.unpack_from("<i", data, offset + 4)[0]
                        sid = id_layer & 0xFFFF
                        layer = (id_layer >> 16) & 0xFFFF
                        info.append(f"  stat[{i}]: id={sid} layer={layer} val={value}")
            else:
                info.append(f"stats @ +0x{arr_off:X}: ptr={stats_ptr} count={stat_count}")

        return info


    def scan_for_player_unit(self) -> int | None:
        """Scan Game.exe memory for the player unit global pointer.

        The player unit pointer is a global variable in Game.exe's data section.
        When a character is in-game, it points to a unit_any struct where:
          - type (DWORD at +0x00) == 0 (player unit)
          - txtfile_no (DWORD at +0x04) is 0-6 (class ID)
          - path pointer (at +0x2C) is non-null
          - act pointer (at +0x1C) is non-null

        We scan the .data section of Game.exe looking for pointers that
        match this pattern.
        """
        if not self._process_handle or not self._base_address:
            return None

        base = self._base_address

        # Known offsets for various 1.14x versions to try first
        known_offsets = [
            0x003A6A70,  # 1.14d
            0x003A5E74,  # 1.14b (common)
            0x003A5E40,  # 1.14b (alt)
            0x0039CEFC,  # 1.14a
            0x0039CF2C,  # 1.14a (alt)
        ]

        for offset in known_offsets:
            result = self._test_player_unit_offset(base + offset)
            if result is not None:
                return offset

        # Brute-force scan the likely data section range (0x390000 - 0x3B0000)
        scan_start = 0x00390000
        scan_end = 0x003B0000
        chunk_size = 4096

        for chunk_offset in range(scan_start, scan_end, chunk_size):
            data = self._read_bytes(base + chunk_offset, chunk_size)
            if not data:
                continue

            for i in range(0, len(data) - 4, 4):
                ptr = struct.unpack_from("<I", data, i)[0]
                if ptr < 0x10000 or ptr > 0x7FFFFFFF:
                    continue

                # Test if this looks like a player unit pointer
                result = self._test_player_unit_offset(base + chunk_offset + i, ptr_value=ptr)
                if result is not None:
                    return chunk_offset + i

        return None

    def _test_player_unit_offset(self, address: int, ptr_value: int | None = None) -> int | None:
        """Test if address contains a valid player unit pointer."""
        if ptr_value is None:
            ptr_value = self._read_ptr(address)

        if not ptr_value or ptr_value < 0x10000 or ptr_value > 0x7FFFFFFF:
            return None

        # Read first 0x30 bytes of the potential unit struct
        unit_data = self._read_bytes(ptr_value, 0x30)
        if not unit_data:
            return None

        unit_type = struct.unpack_from("<I", unit_data, 0x00)[0]
        class_id = struct.unpack_from("<I", unit_data, 0x04)[0]
        act_ptr = struct.unpack_from("<I", unit_data, 0x1C)[0]
        path_ptr = struct.unpack_from("<I", unit_data, 0x2C)[0]

        # Player unit: type==0, class 0-6, valid act and path pointers
        if (unit_type == 0
                and class_id <= 6
                and 0x10000 < act_ptr < 0x7FFFFFFF
                and 0x10000 < path_ptr < 0x7FFFFFFF):
            return ptr_value

        return None


def find_d2_pid() -> int | None:
    """Find the PID of a running Game.exe process."""
    try:
        import subprocess
        result = subprocess.run(
            ["tasklist", "/FI", "IMAGENAME eq Game.exe", "/FO", "CSV", "/NH"],
            capture_output=True, text=True, timeout=5,
        )
        for line in result.stdout.strip().split("\n"):
            parts = line.strip('"').split('","')
            if len(parts) >= 2 and parts[0] == "Game.exe":
                return int(parts[1])
    except Exception:
        pass
    return None
