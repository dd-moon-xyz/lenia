import asyncio
import struct
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock

from lenia.server.models import Start
from lenia.server.presets import default_config
from lenia.server.worker import SimulationWorker


class WorkerResizeTests(unittest.IsolatedAsyncioTestCase):
    def worker(self, **updates):
        config = default_config()
        config["organisms"] = config["organisms"][:1]
        config.update(updates)
        process = SimpleNamespace(
            stdin=Mock(drain=AsyncMock()), stdout=asyncio.StreamReader(),
        )
        return SimulationWorker(process, Start.model_validate(config))

    async def test_gpu_resize_negotiates_and_reads_consecutive_smaller_frames(self):
        worker = self.worker(output_size=480)
        worker.process.stdout.feed_data(b"READY\nREADY\n")
        await worker.start()
        self.assertTrue(worker.process.stdin.write.call_args_list[0].args[0].startswith(b"1024 60 "))
        self.assertEqual(worker.process.stdin.write.call_args_list[1].args[0], b"s 480\n")
        self.assertEqual(worker.frame_size, 480)
        for value in (10, 20):
            raw = bytes([value]) * (480 * 480 * 3)
            worker.process.stdout.feed_data(struct.pack("=f", value) + raw)
            self.assertEqual(await asyncio.wait_for(worker.frame(), 1), raw)
            self.assertEqual(worker.masses, (float(value),))

    async def test_cpu_fallback_for_pixelation_and_unscaled_or_enlarged_output(self):
        for options in ({}, {"output_size":1024}, {"output_size":480,"pixel_size":2}, {"size":512,"output_size":800,"organisms":[{"type":528,"x":128,"y":128}]}):
            with self.subTest(options=options):
                worker = self.worker(**options)
                worker.process.stdout.feed_data(b"READY\n")
                await worker.start()
                self.assertEqual(worker.process.stdin.write.call_count, 1)
                self.assertEqual(worker.frame_size, worker.config.size)

    async def test_old_native_worker_fails_with_rebuild_instruction(self):
        worker = self.worker(output_size=480)
        worker.process.stdout.feed_data(b"READY\n")
        worker.process.stdout.feed_eof()
        with self.assertRaisesRegex(RuntimeError, "rebuild the native"):
            await worker.start()
