import asyncio
import io
import json
import socket
import sys
import unittest
from unittest.mock import patch

import uvicorn
from PIL import Image
from websockets.asyncio.client import connect

from lenia.server.app import app
from lenia.server.presets import default_config

WORKER = """
import sys
import struct
arena = any(option in sys.argv for option in ('--stream-arena', '--stream-hexapod'))
header = sys.stdin.buffer.readline().split()
size, count = int(header[0]), int(header[-1])
for _ in range(count):
    sys.stdin.buffer.readline()
print('READY', flush=True)
frames = 0
while True:
    command = sys.stdin.buffer.read(1)
    if not command:
        break
    if command in b' \\n':
        continue
    if command in (b'o', b'i', b'b'):
        print('READY', flush=True)
        continue
    if command == b'r':
        sys.stdin.buffer.readline()
        print('READY', flush=True)
        continue
    if command != b'n':
        raise SystemExit(2)
    frames += 1
    if arena:
        mass = 0.0 if '--die-once' in sys.argv and frames <= 5 else 100.0
        sys.stdout.buffer.write(struct.pack(f'={count}f', *([mass] * count)))
    red = frames * 40 if '--count-frames' in sys.argv else 200
    sys.stdout.buffer.write(bytes([red, 30, 10]) * size ** 2)
    sys.stdout.buffer.flush()
"""


class StreamTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.url = f"ws://127.0.0.1:{self.listener.getsockname()[1]}/ws"
        self.server = uvicorn.Server(uvicorn.Config(app, log_level="error", lifespan="off"))
        self.task = asyncio.create_task(self.server.serve(sockets=[self.listener]))
        while not self.server.started:
            await asyncio.sleep(0.01)

    async def asyncTearDown(self):
        self.server.should_exit = True
        await asyncio.wait_for(self.task, 5)
        self.listener.close()

    async def test_independent_clients_and_disconnect_cleanup(self):
        launch = asyncio.create_subprocess_exec
        processes = {}

        async def worker(*args, **kwargs):
            process = await launch(sys.executable, "-u", "-c", WORKER, *args[1:], "--count-frames", **kwargs)
            processes[args[1]] = process
            return process

        with self.assertLogs("uvicorn.error", level="INFO") as logs, patch("lenia.server.worker.asyncio.create_subprocess_exec", worker):
            async with connect(self.url) as viewer, connect(self.url) as second:
                await asyncio.gather(
                    viewer.send(json.dumps({"action": "start", "size": 256, "organisms": [{"type": 0}]})),
                    second.send(json.dumps({**default_config(), "output_size": 480})),
                )

                statuses = await asyncio.gather(viewer.recv(), second.recv())
                self.assertEqual(json.loads(statuses[0]), {"status": "started", "size": 256, "fps": 20})
                self.assertEqual(json.loads(statuses[1]), {"status": "started", "size": 480, "fps": 60})
                self.assertEqual(len({process.pid for process in processes.values()}), 2)
                for red in (40, 80):
                    await viewer.send("next")
                    image = Image.open(io.BytesIO(await viewer.recv()))
                    self.assertEqual(image.size, (256, 256))
                    self.assertEqual(image.format, "JPEG")
                    self.assertAlmostEqual(image.getpixel((0, 0))[0], red, delta=3)

                await second.send("outward")
                await second.send("next")
                image = Image.open(io.BytesIO(await second.recv()))
                self.assertEqual(image.size, (480, 480))
                self.assertAlmostEqual(image.getpixel((0, 0))[0], 40, delta=3)

                await viewer.close()
                await asyncio.wait_for(processes["--stream"].wait(), 5)
                self.assertIsNone(processes["--stream-hexapod"].returncode)
                await second.send("next")
                image = Image.open(io.BytesIO(await second.recv()))
                self.assertAlmostEqual(image.getpixel((0, 0))[0], 80, delta=3)
                await second.send("stop")
                self.assertEqual(json.loads(await second.recv())["status"], "stopped")
                await asyncio.wait_for(processes["--stream-hexapod"].wait(), 5)

            await asyncio.sleep(0.01)

        self.assertTrue(any("connection opened" in line for line in logs.output))
        self.assertTrue(any("connection closed" in line and "client disconnected" in line for line in logs.output))
        self.assertTrue(any("connection closed" in line and "client requested stop" in line for line in logs.output))

    async def test_hexapod_preset_streams_fifty_walkers(self):
        launch = asyncio.create_subprocess_exec
        commands = []

        async def worker(*args, **kwargs):
            commands[:] = args
            return await launch(sys.executable, "-u", "-c", WORKER, *args[1:], **kwargs)

        with patch("lenia.server.worker.asyncio.create_subprocess_exec", worker):
            async with connect(self.url) as viewer:
                await viewer.send(json.dumps(default_config()))
                self.assertEqual(json.loads(await viewer.recv())["status"], "started")
                await viewer.send("outward")
                await viewer.send("next")
                first = await asyncio.wait_for(viewer.recv(), 10)
                await viewer.send("inward")
                await viewer.send("next")
                second = await asyncio.wait_for(viewer.recv(), 10)
                self.assertEqual(Image.open(io.BytesIO(second)).size, (1024, 1024))
                await viewer.send("stop")
                self.assertEqual(json.loads(await viewer.recv())["status"], "stopped")

            self.assertIn("--stream-hexapod", commands)

    async def test_invalid_configuration(self):
        cases = (
            {"action": "start", "organisms": [{"type": -1}]},
            {"action": "start", "organisms": [{"type": 0}, {"type": 1}]},
            {"action": "start", "organisms": [{"type": 0, "rotation": 2}]},
            {"action": "start", "size": 256, "organisms": [{"type": 0, "x": 256}]},
            {"action": "start", "output_size": 0, "organisms": [{"type": 0}]},
            {"action": "start", "output_size": 1025, "organisms": [{"type": 0}]},
            {"action": "start", "output_size": 480.0, "organisms": [{"type": 0}]},
        )

        for config in cases:
            async with connect(self.url) as viewer:
                await viewer.send(json.dumps(config))
                self.assertIn("error", json.loads(await viewer.recv()))

    async def test_display_sized_jpeg_preserves_simulation_world(self):
        launch = asyncio.create_subprocess_exec

        async def worker(*args, **kwargs):
            return await launch(sys.executable, "-u", "-c", WORKER, *args[1:], **kwargs)

        configurations = (
            {"action": "start", "size": 1024, "fps": 60, "organisms": [{"type": 0}]},
            default_config(),
        )

        with patch("lenia.server.worker.asyncio.create_subprocess_exec", worker):
            for config in configurations:
                with self.subTest(simulation=config.get("simulation", "lenia")):
                    async with connect(self.url) as viewer:
                        await viewer.send(json.dumps({**config, "output_size": 480}))
                        status = json.loads(await viewer.recv())
                        self.assertEqual(status, {"status": "started", "size": 480, "fps": 60})
                        await viewer.send("next")
                        image = Image.open(io.BytesIO(await viewer.recv()))
                        self.assertEqual(image.size, (480, 480))
                        self.assertEqual(image.format, "JPEG")
                        self.assertEqual(len(image.tobytes()), 480 * 480 * 3)
                        await viewer.send("stop")
                        self.assertEqual(json.loads(await viewer.recv())["status"], "stopped")

                    await asyncio.sleep(0.01)

    async def test_hundred_organism_native_pipeline_and_cleanup(self):
        launch = asyncio.create_subprocess_exec
        processes = {}

        async def worker(*args, **kwargs):
            process = await launch(sys.executable, "-u", "-c", WORKER, *args[1:], **kwargs)
            processes[process.pid] = process
            return process

        config = {
            "action": "start", "boundary": "circle", "size": 1024,
            "organisms": [{"type": index, "velocity": 20.0} for index in (index % 7 for index in range(100))],
        }

        with patch("lenia.server.worker.asyncio.create_subprocess_exec", worker):
            async with connect(self.url) as viewer:
                await viewer.send(json.dumps(config))
                self.assertEqual(json.loads(await viewer.recv())["status"], "started")
                await viewer.send("outward")
                await viewer.send("inward")
                for _ in range(2):
                    await viewer.send("next")
                    image = Image.open(io.BytesIO(await viewer.recv()))
                    self.assertEqual(image.size, (1024, 1024))
                    self.assertEqual(image.format, "JPEG")

            await asyncio.wait_for(asyncio.gather(*(process.wait() for process in processes.values())), 5)

        self.assertEqual(len(processes), 1)
        self.assertTrue(all(process.returncode is not None for process in processes.values()))

    async def test_worker_startup_failure(self):
        launch = asyncio.create_subprocess_exec

        async def failed_worker(*args, **kwargs):
            return await launch(sys.executable, "-c", "raise SystemExit(1)", **kwargs)

        with patch("lenia.server.worker.asyncio.create_subprocess_exec", failed_worker):
            async with connect(self.url) as viewer:
                await viewer.send(json.dumps({"action": "start", "organisms": [{"type": 0}]}))
                self.assertIn("failed to start", json.loads(await viewer.recv())["error"])

    async def test_birth_commands_share_one_process_without_corrupting_frames(self):
        launch = asyncio.create_subprocess_exec
        processes = {}

        async def worker(*args, **kwargs):
            process = await launch(sys.executable, "-u", "-c", WORKER, *args[1:], "--die-once", **kwargs)
            processes[process.pid] = process
            return process

        config = {"action": "start", "boundary": "circle", "fps": 60, "organisms": [{"type": 0}]}
        with self.assertLogs("uvicorn.error", level="INFO") as logs, patch("lenia.server.worker.asyncio.create_subprocess_exec", worker):
            async with connect(self.url) as viewer:
                await viewer.send(json.dumps(config))
                self.assertEqual(json.loads(await viewer.recv())["status"], "started")
                for _ in range(8):
                    for command in ("outward", "inward_fast", "inward"):
                        await viewer.send(command)

                    await viewer.send("next")
                    image = Image.open(io.BytesIO(await viewer.recv()))
                    self.assertEqual(image.size, (512, 512))

            await asyncio.wait_for(asyncio.gather(*(process.wait() for process in processes.values())), 5)

        self.assertEqual(len(processes), 1)
        self.assertTrue(any("Organism died" in line for line in logs.output))
        self.assertTrue(any("Organism born outside visible circle" in line for line in logs.output))
