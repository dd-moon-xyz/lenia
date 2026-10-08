import math
import random

from server.catalog import ANIMALS

__all__ = ["default_config"]


def default_config() -> dict:
    return {
        "action": "start", "simulation": "hexapod", "size": 1024, "fps": 60, "pixel_size": 1,
        "dt": 0.1, "boundary": "circle", "excluded_types": [],
        "organisms": [
            {
                "type": len(ANIMALS),
                "scale": 1,
                "x": round(512 + radius * math.cos(angle)),
                "y": round(512 + radius * math.sin(angle)),
            }
            for _ in range(50)
            for angle in (random.uniform(0, 2 * math.pi),)
            for radius in (400 * math.sqrt(random.random()),)
        ],
    }
