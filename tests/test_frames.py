import io
import unittest

from PIL import Image

from lenia.server.frames import encode_frame, encode_image


class FrameEncodingTests(unittest.TestCase):
    def test_bottom_up_decode_preserves_jpeg_and_pixelation(self):
        for size in (7, 256):
            raw = bytes(
                channel
                for y in range(size)
                for x in range(size)
                for channel in (x % 256, y % 256, (x + y) % 256)
            )
            expected = Image.frombytes("RGB", (size, size), raw).transpose(
                Image.Transpose.FLIP_TOP_BOTTOM
            )
            for pixel_size in (1, 2, 3, 8):
                for output_size in (None, 1, 5, 480):
                    with self.subTest(size=size, pixel=pixel_size, output=output_size):
                        self.assertEqual(
                            encode_frame(raw, size, pixel_size, output_size),
                            encode_image(expected, pixel_size, output_size),
                        )

    def test_top_row_comes_from_bottom_of_gpu_frame(self):
        size = 64
        raw = bytes((255, 0, 0)) * (size * size // 2)
        raw += bytes((0, 0, 255)) * (size * size // 2)
        image = Image.open(io.BytesIO(encode_frame(raw, size)))
        self.assertGreater(image.getpixel((32, 8))[2], 250)
        self.assertGreater(image.getpixel((32, 56))[0], 250)


if __name__ == "__main__":
    unittest.main()
