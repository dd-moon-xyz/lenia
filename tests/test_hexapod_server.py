import unittest

from pydantic import ValidationError

from server.catalog import ANIMALS
from server.models import Start
from server.presets import default_config


class HexapodServerTests(unittest.IsolatedAsyncioTestCase):
    async def test_default_fleet_selects_cuda_worker(self):
        config = Start.model_validate(default_config())
        self.assertEqual(config.simulation, "hexapod")
        self.assertEqual(len(config.organisms), 50)
        self.assertTrue(all(item.type == len(ANIMALS) for item in config.organisms))

    async def test_hexapod_type_cannot_enter_native_worker(self):
        with self.assertRaises(ValidationError):
            Start.model_validate({"action": "start", "organisms": [{"type": len(ANIMALS)}]})

        with self.assertRaises(ValidationError):
            Start.model_validate({"action": "start", "simulation": "hexapod", "boundary": "circle", "organisms": [{"type": 0}]})
