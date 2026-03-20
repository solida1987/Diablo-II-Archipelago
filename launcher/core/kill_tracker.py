"""Kill tracking and area check management.

Tracks kills per area for the Archipelago check system.
Each area has a kill target — reaching it earns a "check".
State is persisted per character.
"""

import json
import os
from dataclasses import dataclass, field, asdict


@dataclass
class AreaProgress:
    area_id: int
    kills: int = 0
    completed: bool = False


@dataclass
class KillTrackerState:
    areas: dict[int, dict] = field(default_factory=dict)  # area_id -> AreaProgress as dict

    def get_area(self, area_id: int) -> AreaProgress:
        if area_id in self.areas:
            d = self.areas[area_id]
            return AreaProgress(**d) if isinstance(d, dict) else d
        return AreaProgress(area_id=area_id)

    def set_area(self, progress: AreaProgress):
        self.areas[progress.area_id] = asdict(progress)

    def add_kill(self, area_id: int, count: int = 1) -> AreaProgress:
        prog = self.get_area(area_id)
        prog.kills += count
        self.set_area(prog)
        return prog

    def mark_completed(self, area_id: int):
        prog = self.get_area(area_id)
        prog.completed = True
        self.set_area(prog)

    def get_completed_count(self) -> int:
        return sum(
            1 for d in self.areas.values()
            if (d.get("completed", False) if isinstance(d, dict) else d.completed)
        )


class KillTracker:
    """Manages kill tracking state, persisted to disk."""

    def __init__(self, state_path: str):
        self.state_path = state_path
        self.state = KillTrackerState()

    def save(self):
        with open(self.state_path, "w") as f:
            json.dump(asdict(self.state), f, indent=2)

    def load(self) -> bool:
        if os.path.exists(self.state_path):
            with open(self.state_path, "r") as f:
                data = json.load(f)
            self.state = KillTrackerState(
                areas={int(k): v for k, v in data.get("areas", {}).items()}
            )
            return True
        return False

    def record_kill(self, area_id: int, kill_target: int) -> tuple[AreaProgress, bool]:
        """Record a kill in the given area.

        Returns (progress, newly_completed) where newly_completed is True
        if this kill crossed the target threshold.
        """
        prog = self.state.add_kill(area_id)
        newly_completed = False

        if not prog.completed and kill_target > 0 and prog.kills >= kill_target:
            prog.completed = True
            self.state.set_area(prog)
            newly_completed = True

        return prog, newly_completed

    def get_progress(self, area_id: int) -> AreaProgress:
        return self.state.get_area(area_id)

    def reset(self):
        self.state = KillTrackerState()
