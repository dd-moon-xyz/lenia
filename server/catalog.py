import csv
from pathlib import Path

__all__ = ["ANIMALS", "ROOT", "AVAILABLE_TYPES", "ORGANISM_RANGES"]

ROOT = Path(__file__).resolve().parents[1]
with (ROOT / "resources/animals_dim.csv").open() as animal_file:
    ANIMALS = tuple(csv.reader(animal_file))

ORGANISM_RANGES = (
    (0, 17),
    # (270, 280)
)
AVAILABLE_TYPES = tuple(dict.fromkeys(
    organism_type for first, last in ORGANISM_RANGES
    for organism_type in range(first, last + 1)
))
