import random
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image


@unittest.skipUnless(shutil.which("g++"), "Requires a C++ compiler")
class BoxResizeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.directory.name) / "resize"
        root = Path(__file__).resolve().parents[1]
        subprocess.run([
            "g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", str(root / "source/lenia"), str(root / "tests/stream_resize.cpp"),
            "-o", str(cls.binary),
        ], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_shared_cuda_filter_matches_pillow_box_and_orientation(self):
        generator = random.Random(123)
        for size, output in ((7, 2), (8, 5), (256, 1), (256, 120), (1024, 480), (17, 17)):
            with self.subTest(size=size, output=output):
                raw = generator.randbytes(size * size * 3)
                wire = subprocess.run(
                    [str(self.binary)], input=f"{size} {output}\n".encode() + raw,
                    capture_output=True, check=True,
                ).stdout
                self.assertEqual(len(wire), output * output * 3)
                actual = Image.frombytes("RGB", (output, output), wire).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
                expected = Image.frombytes("RGB", (size, size), raw).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
                expected = expected.resize((output, output), Image.Resampling.BOX)
                # Pillow quantizes filter coefficients; at most one RGB level
                # may differ from the integer CUDA average.
                self.assertLessEqual(max(abs(a-b) for a,b in zip(actual.tobytes(), expected.tobytes())), 1)
