"""Character profile management.

Each character has a profile JSON file stored in the profiles/ directory.
Profiles track: name, class, seed, AP settings, skill state, and timestamps.
"""

import json
import os
import shutil
import glob as glob_mod
from dataclasses import dataclass, asdict, field
from datetime import datetime


PROFILES_DIR_NAME = "profiles"


@dataclass
class CharacterProfile:
    name: str = ""
    class_name: str = "Amazon"       # Display name (Amazon, Sorceress, etc.)
    class_code: str = "ama"           # Internal code (ama, sor, etc.)
    seed: int | None = None
    windowed: bool = True

    # Archipelago (future)
    ap_server: str = ""
    ap_slot: str = ""
    ap_password: str = ""
    mode: str = "standalone"

    # Timestamps
    created_at: str = ""
    last_played: str = ""

    # Tracking
    skills_randomized: bool = False


class CharacterProfileManager:
    """Manages character profiles stored as JSON files."""

    def __init__(self, base_dir: str):
        self.base_dir = base_dir
        self.profiles_dir = os.path.join(base_dir, PROFILES_DIR_NAME)
        os.makedirs(self.profiles_dir, exist_ok=True)

    def _profile_path(self, name: str) -> str:
        safe = "".join(c for c in name if c.isalnum() or c in " _-").strip()
        return os.path.join(self.profiles_dir, f"{safe}.json")

    def _state_path(self, name: str) -> str:
        """Skill state file for this character."""
        safe = "".join(c for c in name if c.isalnum() or c in " _-").strip()
        return os.path.join(self.profiles_dir, f"{safe}_skills.json")

    def exists(self, name: str) -> bool:
        return os.path.exists(self._profile_path(name))

    def save(self, profile: CharacterProfile) -> str:
        path = self._profile_path(profile.name)
        with open(path, "w") as f:
            json.dump(asdict(profile), f, indent=2)
        return path

    def load(self, name: str) -> CharacterProfile | None:
        path = self._profile_path(name)
        if not os.path.exists(path):
            return None
        with open(path, "r") as f:
            data = json.load(f)
        return CharacterProfile(**{
            k: v for k, v in data.items()
            if k in CharacterProfile.__dataclass_fields__
        })

    def list_profiles(self) -> list[CharacterProfile]:
        """List all saved character profiles, sorted by last_played desc."""
        profiles = []
        for fpath in glob_mod.glob(os.path.join(self.profiles_dir, "*.json")):
            if fpath.endswith("_skills.json"):
                continue
            try:
                with open(fpath, "r") as f:
                    data = json.load(f)
                p = CharacterProfile(**{
                    k: v for k, v in data.items()
                    if k in CharacterProfile.__dataclass_fields__
                })
                if p.name:
                    profiles.append(p)
            except Exception:
                continue
        profiles.sort(key=lambda p: p.last_played or "", reverse=True)
        return profiles

    def delete(self, name: str) -> bool:
        path = self._profile_path(name)
        state_path = self._state_path(name)
        deleted = False
        if os.path.exists(path):
            os.remove(path)
            deleted = True
        if os.path.exists(state_path):
            os.remove(state_path)
        return deleted

    def get_state_path(self, name: str) -> str:
        """Get the skill_state.json path for a specific character."""
        return self._state_path(name)


class SaveGameIsolator:
    """Manages D2 save game isolation.

    When launching a character, moves all OTHER save files to a backup folder
    so the game only sees the active character's saves.
    """

    BACKUP_DIR_NAME = "d2_save_backup"

    # D2 save file extensions
    SAVE_EXTENSIONS = {".d2s", ".d2x", ".key", ".ma0", ".ma1", ".ma2",
                       ".ma3", ".map", ".d2i"}

    def __init__(self, base_dir: str, save_dir: str):
        self.backup_dir = os.path.join(base_dir, self.BACKUP_DIR_NAME)
        self.save_dir = save_dir

    def isolate_character(self, char_name: str) -> int:
        """Move all save files EXCEPT char_name's to backup.

        Returns the number of files moved.
        """
        if not self.save_dir or not os.path.isdir(self.save_dir):
            return 0

        os.makedirs(self.backup_dir, exist_ok=True)
        moved = 0

        for fname in os.listdir(self.save_dir):
            fpath = os.path.join(self.save_dir, fname)
            if not os.path.isfile(fpath):
                continue

            base, ext = os.path.splitext(fname)
            if ext.lower() not in self.SAVE_EXTENSIONS:
                continue

            # Get the character name from filename (e.g., "Hero.d2s" -> "Hero")
            # Some files like SharedStash.d2i don't have a character name
            file_char = base.split(".")[0] if "." in base else base

            if file_char.lower() == char_name.lower():
                continue  # Keep this character's files

            # Move to backup
            dest = os.path.join(self.backup_dir, fname)
            if os.path.exists(dest):
                os.remove(dest)
            shutil.move(fpath, dest)
            moved += 1

        return moved

    def restore_all(self) -> int:
        """Restore all backed-up save files to the save directory.

        Returns the number of files restored.
        """
        if not os.path.isdir(self.backup_dir):
            return 0
        if not self.save_dir:
            return 0

        os.makedirs(self.save_dir, exist_ok=True)
        restored = 0

        for fname in os.listdir(self.backup_dir):
            src = os.path.join(self.backup_dir, fname)
            if not os.path.isfile(src):
                continue
            dest = os.path.join(self.save_dir, fname)
            if os.path.exists(dest):
                os.remove(dest)
            shutil.move(src, dest)
            restored += 1

        return restored
