"""Skill pool state management.

Tracks which skills are equipped (in the skill tree) vs unlocked (available
for swapping). Persisted to skill_state.json.
"""

import json
import os
from dataclasses import dataclass, asdict


@dataclass
class EquippedSkill:
    slot_index: int
    skill_name: str
    skill_id: int
    original_class: str
    skilldesc_ref: str


@dataclass
class SkillPoolState:
    target_class: str = ""
    seed: int | None = None
    equipped: list[dict] = None  # List of EquippedSkill as dicts
    unlocked: list[str] = None   # Skill names available for swapping

    def __post_init__(self):
        if self.equipped is None:
            self.equipped = []
        if self.unlocked is None:
            self.unlocked = []


class SkillPoolManager:
    def __init__(self, state_path: str):
        self.state_path = state_path
        self.state = SkillPoolState()

    def initialize_from_randomization(self, equipped_skills, all_skills, target_class, seed):
        """Set up initial state after randomization.

        equipped_skills: list of SkillEntry (the 30 selected)
        all_skills: list of SkillEntry (all 210)
        """
        equipped_names = {s.skill_name for s in equipped_skills}

        self.state = SkillPoolState(
            target_class=target_class,
            seed=seed,
            equipped=[
                {
                    "slot_index": i,
                    "skill_name": s.skill_name,
                    "skill_id": s.skill_id,
                    "original_class": s.original_class,
                    "skilldesc_ref": s.skilldesc_ref,
                }
                for i, s in enumerate(equipped_skills)
            ],
            unlocked=[
                s.skill_name for s in all_skills
                if s.skill_name not in equipped_names
            ],
        )

    def initialize_from_randomization_with_slots(
        self, equipped_with_slots, all_skills, target_class, seed
    ):
        """Set up initial state with explicit slot indices.

        equipped_with_slots: list of (slot_index, SkillEntry)
        all_skills: list of SkillEntry (all 210)
        """
        equipped_names = {s.skill_name for _, s in equipped_with_slots}

        self.state = SkillPoolState(
            target_class=target_class,
            seed=seed,
            equipped=[
                {
                    "slot_index": slot_idx,
                    "skill_name": s.skill_name,
                    "skill_id": s.skill_id,
                    "original_class": s.original_class,
                    "skilldesc_ref": s.skilldesc_ref,
                }
                for slot_idx, s in equipped_with_slots
            ],
            unlocked=[
                s.skill_name for s in all_skills
                if s.skill_name not in equipped_names
            ],
        )

    def get_equipped(self) -> list[EquippedSkill]:
        return [EquippedSkill(**d) for d in self.state.equipped]

    def get_unlocked(self) -> list[str]:
        return list(self.state.unlocked)

    def swap(self, slot_index: int, new_skill_data: dict) -> bool:
        """Swap a skill in an equipped slot with an unlocked skill.

        new_skill_data must contain: skill_name, skill_id, original_class, skilldesc_ref
        Returns True if swap was successful.
        """
        new_name = new_skill_data.get("skill_name", "")
        if new_name not in self.state.unlocked:
            return False
        if slot_index < 0 or slot_index >= len(self.state.equipped):
            return False

        old = self.state.equipped[slot_index]
        old_name = old["skill_name"]

        # Move old skill to unlocked, remove new from unlocked
        self.state.unlocked.remove(new_name)
        self.state.unlocked.append(old_name)
        self.state.unlocked.sort()

        # Update ALL fields in the equipped slot
        old["skill_name"] = new_skill_data["skill_name"]
        old["skill_id"] = new_skill_data["skill_id"]
        old["original_class"] = new_skill_data["original_class"]
        old["skilldesc_ref"] = new_skill_data["skilldesc_ref"]

        return True

    def reset_state(self):
        """Clear the state (for re-randomization)."""
        self.state = SkillPoolState()

    def save(self):
        with open(self.state_path, "w") as f:
            json.dump(asdict(self.state), f, indent=2)

    def load(self) -> bool:
        if os.path.exists(self.state_path):
            with open(self.state_path, "r") as f:
                data = json.load(f)
            self.state = SkillPoolState(**data)
            return True
        return False
