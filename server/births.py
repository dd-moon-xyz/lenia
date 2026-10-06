import random

from server.catalog import ANIMALS, AVAILABLE_TYPES
from server.models import Organism

__all__ = ["replacement_organism"]


def replacement_organism(size: int, excluded: set[int] | frozenset[int] = frozenset()) -> Organism:
    candidates = tuple(
        index for index in AVAILABLE_TYPES
        for animal in (ANIMALS[index],)
        if index not in excluded
        and max(int(animal[12]), int(animal[13])) <= size / 3
        and 2 * int(animal[5]) + 1 <= size
    )

    if not candidates:
        raise ValueError("No compatible organism type differs from the living types")

    return Organism(type=random.choice(candidates), scale=1)
