import os
import struct
import subprocess
import unittest
from contextlib import contextmanager

from PIL import Image

from lenia.server.catalog import ROOT


@contextmanager
def worker(mode: str, count: int, dt: float):
    with subprocess.Popen([str(ROOT / "build/lenia"), mode], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
        process.stdin.write((f"256 60 {dt} 2 {count}\n" + "256 528 1 128 128 25\n" * count).encode())
        process.stdin.flush()
        if process.stdout.readline() != b"READY\n":
            raise RuntimeError("CUDA worker failed to start")

        yield process


def frame(process, count: int, size: int = 256):
    process.stdin.write(b"n")
    process.stdin.flush()
    masses = struct.unpack(f"={count}f", process.stdout.read(count * 4))
    image = Image.frombytes("RGB", (size, size), process.stdout.read(size * size * 3))
    return masses, image


@unittest.skipUnless(os.environ.get("LENIA_GPU_TESTS") == "1", "Requires CUDA GPU")
class NativeHexapodTests(unittest.TestCase):
    def test_resized_frames_preserve_metadata_and_command_alignment(self):
        with worker("--stream-hexapod", 1, .1) as process:
            for size in (120, 1, 256):
                process.stdin.write(f"s {size}\n".encode())
                process.stdin.flush()
                self.assertEqual(process.stdout.readline(), b"READY\n")
                for _ in range(2):
                    masses, image = frame(process, 1, size)
                    self.assertEqual(image.size, (size, size))
                    self.assertGreater(masses[0], 0)
                process.stdin.write(b"i")
                process.stdin.flush()
                self.assertEqual(process.stdout.readline(), b"READY\n")

    def test_overlapping_bodies_fade_to_death(self):
        with worker("--stream-hexapod-preview", 2, .005) as process:
            initial, _ = frame(process, 2)
            for _ in range(2000):
                masses, _ = frame(process, 2)
                if max(masses) <= .001:
                    break

            self.assertTrue(all(value > 0 for value in initial))
            self.assertTrue(all(value <= .001 for value in masses))

    def test_isolated_body_survives_and_replacement_enters_from_edge(self):
        with worker("--stream-hexapod", 1, .2) as process:
            for _ in range(100):
                masses, _ = frame(process, 1)

            self.assertGreater(masses[0], .001)
            process.stdin.write(b"r 0 256 528 1 128 128 25 0\n")
            process.stdin.flush()
            self.assertEqual(process.stdout.readline(), b"READY\n")
            masses, image = frame(process, 1)
            self.assertGreater(masses[0], .001)
            self.assertIsNone(image.getbbox())
            entered = False
            for _ in range(300):
                masses, image = frame(process, 1)
                entered = entered or image.getbbox() is not None

            self.assertGreater(masses[0], .001)
            self.assertTrue(entered)
