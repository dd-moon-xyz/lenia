import argparse
import csv
import json
import struct
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent
OUTPUT = ROOT / "resources/hexapod"
SIZE = 256
HEADER = struct.Struct("=3f")
FRAME_BYTES = HEADER.size + SIZE * SIZE * 3


def run_experiment(frames: int, output: Path) -> None:
    definition = (OUTPUT / "organism.csv").read_text()
    animal = next(csv.reader(definition.splitlines()))
    catalog = (ROOT / "resources/animals_dim.csv").read_text()
    type_id = len(tuple(csv.reader(catalog.splitlines())))
    dt = 1 / float(animal[6])
    x, y = (SIZE - int(animal[12])) // 2, (SIZE - int(animal[13])) // 2
    commands = f"{SIZE} {type_id} 1 {dt} 1\n{x} {y}\n".encode() + b"n\n" * frames
    with TemporaryDirectory(prefix="lenia-hexapod-") as temporary:
        workspace = Path(temporary)
        build = workspace / "build"
        resources = workspace / "resources"
        build.mkdir()
        resources.mkdir()
        (workspace / "shaders").symlink_to(ROOT / "shaders", target_is_directory=True)
        for resource in (ROOT / "resources").iterdir():
            if resource.name != "animals_dim.csv":
                (resources / resource.name).symlink_to(resource, target_is_directory=resource.is_dir())

        (resources / "animals_dim.csv").write_text(catalog.rstrip("\n") + "\n" + definition)
        result = subprocess.run(
            [str(ROOT / "build/lenia"), "--stream-centered"], input=commands,
            cwd=build, capture_output=True, timeout=120,
        )

    if result.returncode or not result.stdout.startswith(b"READY\n"):
        raise RuntimeError(result.stderr.decode(errors="replace") or "Native worker failed to start")

    payload = memoryview(result.stdout)[6:]
    if len(payload) != frames * FRAME_BYTES:
        raise RuntimeError(f"Expected {frames} complete native frames; received {len(payload)} bytes")

    diagnostics = tuple(HEADER.unpack_from(payload, index * FRAME_BYTES) for index in range(frames))
    sample_indices = tuple(sorted({0, min(29, frames - 1), min(119, frames - 1), min(299, frames - 1), frames - 1}))

    def image_at(index: int) -> Image.Image:
        start = index * FRAME_BYTES + HEADER.size
        return Image.frombytes("RGB", (SIZE, SIZE), bytes(payload[start:start + SIZE * SIZE * 3])).transpose(
            Image.Transpose.FLIP_TOP_BOTTOM
        )

    output.mkdir(parents=True, exist_ok=True)
    samples = tuple(image_at(index) for index in sample_indices)
    sheet = Image.new("RGB", (SIZE * len(samples), SIZE + 24))
    draw = ImageDraw.Draw(sheet)
    for column, (index, picture) in enumerate(zip(sample_indices, samples)):
        sheet.paste(picture, (column * SIZE, 0))
        draw.text((column * SIZE + 8, SIZE + 5), f"Step {index + 1}, mass {diagnostics[index][2]:.1f}", fill="white")

    sheet.save(output / "evolution.png")
    animation = tuple(image_at(index) for index in range(0, frames, 10))
    animation[0].save(
        output / "evolution.gif", save_all=True, append_images=animation[1:],
        duration=100, loop=0, disposal=2,
    )

    report = {
        "name": animal[4], "status": "experimental; six-arm propulsion not established",
        "engine": "build/lenia --stream-centered", "field_size": SIZE, "scale": 1,
        "steps": frames, "dt": dt, "camera": "follows center of mass",
        "arena_translation": False, "steering": False,
        "radius": int(animal[5]), "mu": float(animal[8]), "sigma": float(animal[9]),
        "samples": tuple({"step": index + 1, "mass": diagnostics[index][2]} for index in sample_indices),
    }

    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Saved experimental evolution to {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Evolve the experimental six-arm seed with the native Lenia rules.")
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    if not 1 <= args.frames <= 1800:
        parser.error("frames must be between 1 and 1800")

    run_experiment(args.frames, args.output)
