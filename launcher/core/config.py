"""Application configuration, persisted to config.json."""

import json
import os
from dataclasses import dataclass, asdict, field


@dataclass
class AppConfig:
    game_dir: str = ""
    ap_server: str = ""
    ap_slot: str = ""
    ap_password: str = ""
    mode: str = "standalone"  # "standalone" or "archipelago"
    windowed: bool = True
    last_class: str = "Amazon"
    last_seed: str = ""  # Empty = random
    last_char_name: str = "Hero"

    @classmethod
    def load(cls, path: str) -> "AppConfig":
        if os.path.exists(path):
            with open(path, "r") as f:
                data = json.load(f)
            return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})
        return cls()

    def save(self, path: str) -> None:
        with open(path, "w") as f:
            json.dump(asdict(self), f, indent=2)
