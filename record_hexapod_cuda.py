import argparse
import subprocess
from math import ceil
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent


def record(path: Path, duration: float, fps: int) -> None:
    if not 0 < duration <= 30 or not 5 <= fps <= 60:
        raise ValueError("Duration must be within 0–30 seconds and FPS within 5–60")

    size = 1024
    with subprocess.Popen(
        [str(ROOT / "build/lenia"), "--stream-hexapod-preview"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
    ) as process:
        process.stdin.write(f"{size} {fps} {1 / fps} 2 1\n{size} 528 1 {size / 2} {size / 2} 25\n".encode())
        process.stdin.flush()
        if process.stdout.readline() != b"READY\n":
            raise RuntimeError("CUDA preview worker failed to start")

        def frame() -> Image.Image:
            process.stdin.write(b"n")
            process.stdin.flush()
            metadata = process.stdout.read(4)
            pixels = process.stdout.read(size * size * 3)
            if len(metadata) != 4 or len(pixels) != size * size * 3:
                raise RuntimeError("CUDA preview worker returned an incomplete frame")

            return Image.frombytes("RGB", (size, size), pixels).transpose(Image.Transpose.FLIP_TOP_BOTTOM)

        animation = (frame() for _ in range(ceil(duration * fps)))
        path.parent.mkdir(parents=True, exist_ok=True)
        next(animation).save(
            path, save_all=True, append_images=animation, duration=round(1000 / fps),
            loop=0, disposal=2,
        )

    if process.returncode:
        raise RuntimeError(f"CUDA preview worker exited with code {process.returncode}")

    print(f"Saved {path}: {ceil(duration * fps)} CUDA-rendered frames")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Record the CUDA six-arm organism preview")
    parser.add_argument("--output", type=Path, default=ROOT / "resources/hexapod/propulsion.gif")
    parser.add_argument("--duration", type=float, default=8)
    parser.add_argument("--fps", type=int, default=25)
    args = parser.parse_args()
    record(args.output, args.duration, args.fps)
