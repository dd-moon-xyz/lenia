import io

from PIL import Image

__all__ = ["encode_frame", "encode_image"]


def encode_frame(raw: bytes, size: int, pixel_size: int = 1, output_size: int | None = None) -> bytes:
    # Decode bottom-up rows directly instead of copying the entire image again
    # to flip GPU output before resizing and JPEG encoding.
    image = Image.frombytes("RGB", (size, size), raw, "raw", "RGB", 0, -1)
    return encode_image(image, pixel_size, output_size)


def encode_image(image: Image.Image, pixel_size: int = 1, output_size: int | None = None) -> bytes:
    if pixel_size > 1:
        resolution = (max(1, image.width // pixel_size), max(1, image.height // pixel_size))
        image = image.resize(resolution, Image.Resampling.BOX).resize(image.size, Image.Resampling.NEAREST)

    if output_size is not None and image.size != (output_size, output_size):
        image = image.resize((output_size, output_size), Image.Resampling.BOX)

    output = io.BytesIO()
    image.save(output, format="JPEG", quality=80)
    return output.getvalue()
