import asyncio
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock

from pydantic import ValidationError

from lenia.server.arena import CircularArena
from lenia.server.models import Movement, Start
from lenia.server.worker import SimulationWorker


class MovementValidationTests(unittest.TestCase):
    def test_vector_requires_nonzero_finite_components_and_explicit_escape(self):
        base = {'action': 'movement', 'direction': 'vector', 'speed_multiplier': 1.5,
                'vector': {'x': -.6, 'y': .8}, 'escape': True}
        command = Movement.model_validate(base)
        self.assertEqual(command.vector.x, -.6)
        for vector in ({'x': 0, 'y': 0}, {'x': float('nan'), 'y': 1}, {'x': 1.1, 'y': 0},
                       {'x': True, 'y': 0}, {'x': 1, 'y': '0'}, {'x': 1}, {'x': 1, 'y': 0, 'z': 0}):
            with self.subTest(vector=vector), self.assertRaises(ValidationError):
                Movement.model_validate({**base, 'vector': vector})
        for missing in ('vector', 'escape'):
            with self.assertRaises(ValidationError):
                Movement.model_validate({key: value for key, value in base.items() if key != missing})
        with self.assertRaises(ValidationError):
            Movement.model_validate({**base, 'escape': 'true'})
        for direction in ('inward', 'outward'):
            with self.assertRaises(ValidationError):
                Movement.model_validate({**base, 'direction': direction})

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
    async def test_vector_protocol_preserves_signs_and_escape_independently(self):
        process = SimpleNamespace(stdin=Mock(drain=AsyncMock()), stdout=asyncio.StreamReader())
        worker = SimulationWorker(process, self.config())
        for escape in (True, False):
            process.stdout.feed_data(b'READY\n')
            await worker.steer_vector(-.6, .8, escape, 1.5)
            process.stdin.write.assert_called_with(f'v -0.6 0.8 {int(escape)} 1.5\n'.encode())

    async def test_vector_failure_requires_rebuild_and_preserves_birth_gate(self):
        process = SimpleNamespace(stdin=Mock(drain=AsyncMock()), stdout=asyncio.StreamReader())
        process.stdout.feed_eof()
        worker = SimulationWorker(process, self.config())
        arena = CircularArena(self.config(), worker)
        with self.assertRaisesRegex(RuntimeError, 'rebuild the native'):
            await arena.steer_vector(1, 0, True, 1)
        self.assertFalse(arena.outward)
        await arena.close()

    async def test_vector_escape_suppresses_births_and_radial_commands_resume_them(self):
        worker = Mock(steer_vector=AsyncMock(), movement=AsyncMock())
        arena = CircularArena(self.config(), worker)
        arena._replace = AsyncMock()
        await arena.steer_vector(1, 0, True, 2)
        await arena.replace(0)
        arena._replace.assert_not_awaited()
        await arena.steer_vector(0, -1, True, 1)
        worker.steer_vector.assert_awaited_with(0, -1, True, 1)
        self.assertTrue(arena.outward)
        await arena.movement(False, 1)
        await arena.replace(0)
        arena._replace.assert_awaited_once_with(0)
        await arena.steer_vector(1, 0, False, 1)
        self.assertFalse(arena.outward)
        await arena.close()

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
