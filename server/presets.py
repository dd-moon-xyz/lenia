import math
import random
from itertools import cycle, islice

from server.catalog import ANIMALS, AVAILABLE_TYPES

__all__ = ["default_config"]


def default_config() -> dict:
    candidates = tuple(
        index for index in AVAILABLE_TYPES
        for animal in (ANIMALS[index],)
        if max(int(animal[12]), int(animal[13])) <= 1024 / 3
        and 2 * int(animal[5]) + 1 <= 1024
    )

    types = islice(cycle(random.sample(candidates, len(candidates))), 100)
    return {
        "action": "start", "size": 1024, "fps": 60, "pixel_size": 1,
        "dt": 0.1, "boundary": "circle", "excluded_types": [],
        "organisms": [
            {
                "type": organism_type,
                "scale": 1,
                "x": round(512 + radius * math.cos(angle)),
                "y": round(512 + radius * math.sin(angle)),
            }
            for organism_type in types
            for angle in (random.uniform(0, 2 * math.pi),)
            for radius in (400 * math.sqrt(random.random()),)
        ],
    }
