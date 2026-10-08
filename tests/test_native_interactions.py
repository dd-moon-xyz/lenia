import asyncio
import os
import unittest

from server.models import Start
from server.worker import simulation_worker


@unittest.skipUnless(os.environ.get("LENIA_GPU_TESTS") == "1", "Requires CUDA/OpenGL desktop session")
class NativeInteractionTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        asyncio.get_running_loop().set_debug(False)

    async def test_contact_damage_and_reseed(self):
        samples = {}
        for label, positions in (
            ("overlap", ((512, 512), (512, 512))),
            ("separate", ((300, 300), (700, 700))),
        ):
            config = Start(
                action="start", size=1024, fps=60, boundary="circle",
                organisms=[{"type": 0, "scale": 4, "x": x, "y": y} for x, y in positions],
            )

            async with simulation_worker(config) as arena:
                steps = 7200 if label == "overlap" else 120
                for step in range(steps):
                    await arena.worker.frame()
                    if step in (0, 119, 599, steps - 1):
                        samples[label, step] = arena.worker.masses

                if label == "overlap":
                    await arena.worker.replace_organism(0, config.organisms[0], 0)
                    await arena.worker.frame()
                    self.assertGreater(arena.worker.masses[0], 1000)
                    self.assertLessEqual(arena.worker.masses[1], 0.001)

        self.assertLess(sum(samples["overlap", 599]), sum(samples["overlap", 119]))
        self.assertTrue(all(mass > 0.001 for mass in samples["overlap", 119]))
        self.assertTrue(all(mass <= 0.001 for mass in samples["overlap", 7199]))
        for initial, final in zip(samples["separate", 0], samples["separate", 119]):
            self.assertAlmostEqual(final / initial, 1, delta=0.02)

        self.assertLess(sum(samples["overlap", 599]), sum(samples["separate", 119]))
