import math
import unittest

from server.catalog import ANIMALS, AVAILABLE_TYPES
from server.models import Start
from server.presets import default_config
from server.births import replacement_organism


class ArenaTests(unittest.TestCase):
    def test_circle_accepts_seven_distinct_types_at_sixty_fps(self):
        config = Start.model_validate({
            "action": "start", "boundary": "circle", "fps": 60,
            "organisms": [{"type": index, "velocity": 20.0} for index in range(7)],
        })

        self.assertEqual(config.fps, 60)
        self.assertEqual(len({item.type for item in config.organisms}), 7)

    def test_frame_rate_limit(self):
        with self.assertRaises(ValueError):
            Start.model_validate({"action": "start", "fps": 61, "organisms": [{"type": 0}]})

    def test_doubling_size_halves_velocity(self):
        config = Start.model_validate({
            "action": "start", "boundary": "circle",
            "organisms": [{"type": 0, "scale": 2}, {"type": 0, "scale": 4}],
        })

        self.assertEqual(config.organisms[0].velocity, 25)
        self.assertEqual(config.organisms[1].velocity, 12.5)

    def test_type_dimensions_affect_velocity(self):
        config = Start.model_validate({
            "action": "start", "boundary": "circle",
            "organisms": [{"type": 0, "scale": 4}, {"type": 1, "scale": 4}],
        })

        self.assertAlmostEqual(config.organisms[0].velocity * 80, config.organisms[1].velocity * 84)

    def test_default_scene_uses_fifty_hexapods(self):
        config = Start.model_validate(default_config())
        self.assertEqual(len(config.organisms), 50)
        self.assertEqual({item.type for item in config.organisms}, {len(ANIMALS)})
        self.assertFalse({item.type for item in config.organisms} & set(config.excluded_types))
        self.assertTrue(all(item.scale == 1 for item in config.organisms))
        self.assertEqual(config.simulation, "hexapod")
        self.assertTrue(all(math.hypot(item.x - 512, item.y - 512) <= 401 for item in config.organisms))

    def test_all_types_in_the_intervals_are_allowed(self):
        for organism_type in AVAILABLE_TYPES:
            config = Start(action="start", size=1024, organisms=[{"type": organism_type}])
            self.assertEqual(config.organisms[0].type, organism_type)

    def test_types_outside_the_intervals_are_rejected(self):
        with self.assertRaises(ValueError):
            Start(action="start", size=1024, organisms=[{"type": 18}])

    def test_previous_exclusions_do_not_filter_types(self):
        config = Start(action="start", excluded_types=[0], organisms=[{"type": 0}])
        self.assertEqual(config.organisms[0].type, 0)

    def test_population_limit(self):
        with self.assertRaises(ValueError):
            Start(action="start", boundary="circle", organisms=[{"type": 0}] * 101)

    def test_birth_remains_available_with_all_types_alive(self):
        config = Start.model_validate(default_config())
        current = set(AVAILABLE_TYPES)
        replacement = replacement_organism(config.size)
        self.assertIn(replacement.type, current)
        self.assertEqual(replacement.scale, 1)
