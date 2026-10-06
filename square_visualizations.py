import argparse
import asyncio
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw
from joblib import Parallel, delayed
from tqdm import tqdm

from server.catalog import ANIMALS, ROOT


def square_bounds(image: Image.Image, edge_coverage: float) -> tuple[int, int, int, int] | None:
    channels = image.split()
    mask = ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]), channels[2]).point(
        lambda value: 255 if value > 0 else 0
    )

    bounds = mask.getbbox()
    if bounds is None:
        return None

    left, top, right, bottom = bounds
    width, height = right - left, bottom - top
    if min(width, height) < 32 or abs(width - height) > max(1, max(width, height) * 0.02):
        return None

    band = max(1, round(min(width, height) * 0.03))
    edges = (
        (left, top, right, top + band), (left, bottom - band, right, bottom),
        (left, top, left + band, bottom), (right - band, top, right, bottom),
    )

    density = mask.crop(bounds).histogram()[255] / (width * height)
    if all(mask.crop(edge).histogram()[255] / ((width if index < 2 else height) * band) >= edge_coverage * density
           for index, edge in enumerate(edges)):
        return bounds

    return None


async def scan_type(type_id: int, scale: int, args: argparse.Namespace) -> dict | None:
    animal = ANIMALS[type_id]
    extent = max(2 * int(animal[5]) * scale + 1, 2 * int(animal[12]) * scale, 2 * int(animal[13]) * scale)
    field_size = min(args.size, max(256, 1 << (extent - 1).bit_length()))
    process = await asyncio.create_subprocess_exec(
        str(ROOT / "build/lenia"), "--stream-arena", cwd=ROOT / "build",
        stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
    )

    try:
        dt = min(0.1, 1 / float(animal[6]))
        process.stdin.write(f"{args.size} 60 {dt} 1\n{field_size} {type_id} {scale} {args.size // 2} {args.size // 2} 25\n".encode())
        await process.stdin.drain()
        async with asyncio.timeout(30):
            ready = await process.stdout.readline()

        if ready != b"READY\n":
            raise RuntimeError(f"Native worker failed for type {type_id}; check CUDA/OpenGL and build/lenia")

        for frame in range(1, args.frames + 1):
            process.stdin.write(b"n")
            await process.stdin.drain()
            async with asyncio.timeout(30):
                await process.stdout.readexactly(4)
                raw = await process.stdout.readexactly(args.size * args.size * 3)

            if frame % args.sample_every:
                continue

            image = Image.frombytes("RGB", (args.size, args.size), raw).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            circle = Image.new("L", image.size)
            radius = (args.size / 2 - 8) * 0.9 - 3
            center = args.size / 2
            ImageDraw.Draw(circle).ellipse((center - radius, center - radius, center + radius, center + radius), fill=255)
            image = Image.composite(image, Image.new("RGB", image.size), circle)
            bounds = square_bounds(image, args.edge_coverage)
            if bounds is not None:
                filename = f"{type_id:03}-scale-{scale}-frame-{frame:04}.png"
                image.crop(bounds).save(args.output / filename)
                return {"type": type_id, "name": animal[4], "scale": scale, "field_size": field_size, "frame": frame, "bounds": bounds, "image": filename}

        return None
    finally:
        if process.returncode is None:
            process.kill()

        await process.wait()


def scan_job(type_id: int, args: argparse.Namespace) -> dict:
    animal = ANIMALS[type_id]
    if animal[10:12] != ["1", "1"]:
        return {"type": type_id, "status": "skipped", "reason": "unsupported kernel/growth rule"}

    preset_scale = max(1, min(3, 64 // max(int(animal[12]), int(animal[13]))))
    scales = tuple(
        scale for scale in (args.scale or preset_scale,)
        if 2 * int(animal[5]) * scale + 1 <= args.size
        and max(int(animal[12]), int(animal[13])) * scale <= args.size / 3
    )

    if not scales:
        return {"type": type_id, "status": "skipped", "reason": "seed or kernel does not fit"}

    matches = tuple(filter(None, (asyncio.run(scan_type(type_id, scale, args)) for scale in scales)))
    return {"type": type_id, "status": "match" if matches else "no_match", "matches": matches, "scales": scales}


def main(args: argparse.Namespace) -> None:
    args.output.mkdir(parents=True, exist_ok=True)
    with Parallel(n_jobs=args.jobs, backend="loky", return_as="generator_unordered", batch_size=1) as parallel:
        completed = parallel(delayed(scan_job)(type_id, args) for type_id in range(args.first, args.last + 1))
        results = sorted(
            tqdm(completed, total=args.last - args.first + 1, desc="Organisms", unit="type", ascii=True, colour="#808080"),
            key=lambda result: result["type"],
        )

    matches = [match for result in results for match in result.get("matches", ())]
    report = {
        "settings": {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
        "matches": matches, "results": results,
    }

    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    if matches:
        sheet = Image.new("RGB", (4 * 192, ((len(matches) + 3) // 4) * 220), "#202020")
        draw = ImageDraw.Draw(sheet)
        for index, match in enumerate(matches):
            x, y = index % 4 * 192, index // 4 * 220
            with Image.open(args.output / match["image"]) as preview:
                preview.thumbnail((184, 184))
                sheet.paste(preview, (x + 4, y + 4))

            draw.text((x + 4, y + 192), f"Type {match['type']} / scale {match['scale']} / {match['frame']}", fill="white")

        sheet.save(args.output / "squares.png")

    print(f"Saved {len(matches)} matches to {args.output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Simulate catalog types and export solid or tiled square-shaped states.")
    parser.add_argument("--first", type=int, default=0)
    parser.add_argument("--last", type=int, default=20)
    parser.add_argument("--jobs", type=int, default=2, help="Number of concurrent native GPU workers")
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--sample-every", type=int, default=30)
    parser.add_argument("--size", type=int, choices=(256, 512, 1024), default=1024)
    parser.add_argument("--scale", type=int, choices=range(1, 11), help="Override the organism's preset scale")
    parser.add_argument("--edge-coverage", type=float, default=0.6, help="Minimum edge-band density relative to whole-body density")
    parser.add_argument("--output", type=Path, default=ROOT / "square-visualizations")
    arguments = parser.parse_args()
    if not 0 <= arguments.first <= arguments.last < len(ANIMALS):
        parser.error("type range must be within 0–527")

    if arguments.frames < 1 or not 1 <= arguments.sample_every <= arguments.frames:
        parser.error("frames must be positive and sample-every must be between 1 and frames")

    if not 0 < arguments.edge_coverage <= 1:
        parser.error("edge-coverage must be greater than zero and at most one")

    if arguments.jobs < 1:
        parser.error("jobs must be positive")

    main(arguments)
