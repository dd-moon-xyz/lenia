import asyncio
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock

from pydantic import ValidationError

from lenia.server.arena import CircularArena
from lenia.server.models import Movement, Start
from lenia.server.worker import SimulationWorker


class MovementValidationTests(unittest.TestCase):
    def test_accepts_independent_direction_and_numeric_multiplier(self):
        for direction in ('inward', 'outward'):
            for speed in (.5, 1, 2.5, 10):
                command = Movement.model_validate({'action': 'movement', 'direction': direction, 'speed_multiplier': speed})
                self.assertEqual(command.speed_multiplier, speed)

    def test_rejects_invalid_or_ambiguous_commands(self):
        base = {'action': 'movement', 'direction': 'outward', 'speed_multiplier': 1.0}
        invalid = [{'speed_multiplier': value} for value in (0, -1, 10.1, float('nan'), float('inf'), '2', True)]
        invalid += [{'direction': 'sideways'}, {'action': 'start'}, {'velocity': 2}]
        for updates in invalid:
            with self.subTest(updates=updates), self.assertRaises(ValidationError):
                Movement.model_validate({**base, **updates})
        for field in base:
            with self.assertRaises(ValidationError):
                Movement.model_validate({key: value for key, value in base.items() if key != field})


class MovementProtocolTests(unittest.IsolatedAsyncioTestCase):
    def config(self):
        return Start.model_validate({'action': 'start', 'boundary': 'circle', 'organisms': [{'type': 0}]})

    async def test_worker_sends_direction_and_arbitrary_speed_independently(self):
        process = SimpleNamespace(stdin=Mock(drain=AsyncMock()), stdout=asyncio.StreamReader())
        worker = SimulationWorker(process, self.config())
        for outward, speed, expected in ((False, .5, b'm 0 0.5\n'), (True, 1.0, b'm 1 1.0\n'), (True, 2.5, b'm 1 2.5\n')):
            process.stdout.feed_data(b'READY\n')
            await worker.movement(outward, speed)
            process.stdin.write.assert_called_with(expected)

    async def test_old_native_worker_reports_rebuild_requirement(self):
        process = SimpleNamespace(stdin=Mock(drain=AsyncMock()), stdout=asyncio.StreamReader())
        process.stdout.feed_eof()
        worker = SimulationWorker(process, self.config())
        with self.assertRaisesRegex(RuntimeError, 'rebuild the native'):
            await worker.movement(True, 1.0)

    async def test_birth_gate_tracks_direction_independently_of_speed(self):
        worker = Mock(movement=AsyncMock())
        arena = CircularArena(self.config(), worker)
        for outward, speed in ((True, .5), (True, 2.5), (False, 2.5), (False, 1.0)):
            await arena.movement(outward, speed)
            self.assertEqual(arena.outward, outward)
            worker.movement.assert_awaited_with(outward, speed)
        await arena.close()

    async def test_failed_command_preserves_birth_gate(self):
        worker = Mock(movement=AsyncMock(side_effect=OSError('worker failed')))
        arena = CircularArena(self.config(), worker)
        with self.assertRaises(OSError):
            await arena.movement(True, 1.0)
        self.assertFalse(arena.outward)
        await arena.close()
