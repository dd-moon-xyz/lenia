import asyncio
import math
import unittest
from unittest.mock import patch

from server.arena import CircularArena
from server.catalog import ANIMALS
from server.births import replacement_organism
from server.models import Start


class FakeWorker:
    def __init__(self, config, mass=0.0):
        self.config = config
        self.masses = (mass,)
        self.replacements = ()

    async def frame(self):
        return bytes(self.config.size ** 2 * 3)

    async def replace_organism(self, index, organism, angle):
        await asyncio.sleep(0)
        self.replacements = (*self.replacements, (index, organism, angle))
        self.masses = (100.0,)


class BirthTests(unittest.IsolatedAsyncioTestCase):
    def config(self):
        return Start.model_validate({"action": "start", "boundary": "circle", "fps": 60, "organisms": [{"type": 0}]})

    async def test_dead_organism_sends_random_boundary_birth_to_native_worker(self):
        config = self.config()
        worker = FakeWorker(config)
        arena = CircularArena(config, worker)
        try:
            for _ in range(6):
                await arena.frame()

            self.assertEqual(len(worker.replacements), 1)
            index, organism, angle = worker.replacements[0]
            self.assertEqual(index, 0)
            self.assertIn(organism.type, range(len(ANIMALS)))
            self.assertGreaterEqual(angle, 0)
            self.assertLessEqual(angle, 2 * math.pi)
        finally:
            await arena.close()

    async def test_hidden_but_living_organism_is_not_replaced(self):
        config = self.config()
        worker = FakeWorker(config, mass=100.0)
        arena = CircularArena(config, worker)
        try:
            for _ in range(6):
                await arena.frame()

            self.assertFalse(worker.replacements)
        finally:
            await arena.close()

    async def test_pending_replacement_is_cancelled_on_close(self):
        config = self.config()
        worker = FakeWorker(config)
        entered = asyncio.Event()

        async def replace(index, organism, angle):
            entered.set()
            await asyncio.Event().wait()

        worker.replace_organism = replace
        arena = CircularArena(config, worker)
        for _ in range(5):
            await arena.frame()

        await asyncio.wait_for(entered.wait(), 1)
        await arena.close()
        self.assertTrue(all(task.cancelled() for task in arena.pending.values()))

    def test_replacement_fits_and_uses_a_different_type(self):
        replacement = replacement_organism(512, {0, 1, 2})
        self.assertNotIn(replacement.type, {0, 1, 2})
        Start.model_validate({"action": "start", "boundary": "circle", "organisms": [replacement]})

    def test_no_birth_reuses_a_living_type_when_pool_is_exhausted(self):
        with self.assertRaises(ValueError):
            replacement_organism(512, set(range(len(ANIMALS))))

    def test_birth_pool_includes_all_types_in_the_intervals(self):
        with patch("server.births.random.choice", side_effect=lambda candidates: candidates[0]) as choose:
            replacement_organism(1024, {0, 1, 2})

        candidates = choose.call_args_list[0].args[0]
        self.assertEqual(len(candidates), len(set(candidates)))
        self.assertFalse(set(candidates) & {0, 1, 2})
        self.assertIn(6, candidates)
        self.assertIn(16, candidates)
        self.assertEqual(set(candidates), set(range(3, 18)))
