from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, model_validator

from server.catalog import ANIMALS, AVAILABLE_TYPES
from server.velocity import normalize_velocities

__all__ = ["Organism", "Start"]


class Organism(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)
    type: int = Field(ge=0, lt=len(ANIMALS))
    scale: int = Field(default=1, ge=1, le=10)
    x: int = Field(default=128, ge=0)
    y: int = Field(default=128, ge=0)
    velocity: float = Field(default=25.0, gt=0, le=25, allow_inf_nan=False)


class Start(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)
    action: Literal["start"]
    size: int = Field(default=512, ge=256, le=1024)
    fps: int = Field(default=20, ge=1, le=60)
    pixel_size: int = Field(default=1, ge=1, le=16)
    dt: float = Field(default=0.1, gt=0, le=0.2, allow_inf_nan=False)
    boundary: Literal["wrap", "circle"] = "wrap"
    organisms: list[Organism] = Field(min_length=1, max_length=100)

    excluded_types: list[int] = Field(default_factory=list)

    @model_validator(mode="after")
    def validate_world(self):
        if any(item.type not in AVAILABLE_TYPES for item in self.organisms):
            raise ValueError("Organism type is outside the configured ranges")

        first = self.organisms[0]
        if self.boundary == "wrap" and any((item.type, item.scale) != (first.type, first.scale) for item in self.organisms):
            raise ValueError("All organisms must share the same type and scale in this version")

        if any(2 * int(ANIMALS[item.type][5]) * item.scale + 1 > self.size for item in self.organisms):
            raise ValueError("Organism kernel is larger than the world")

        if any(item.x >= self.size or item.y >= self.size for item in self.organisms):
            raise ValueError("Organism positions must be inside the world")

        if self.boundary == "circle":
            if any(max(int(ANIMALS[item.type][12]), int(ANIMALS[item.type][13])) * item.scale > self.size / 3 for item in self.organisms):
                raise ValueError("Organism seed is too large for the circular arena")

            normalize_velocities(self.organisms)

        return self
