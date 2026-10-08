import argparse
from cmath import phase, rect
from dataclasses import dataclass, field
from itertools import accumulate, pairwise
from math import ceil, exp, floor, isfinite, pi, sin
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

LENGTHS = (97.625,) * 3
MAX_REACH = sum(LENGTHS) - 3
HEAD = tuple(rect(32, -pi / 2 + index * pi / 3) - 32j for index in range(6))
ARM_ROOTS = tuple(
    complex(side * HEAD[2].real * width, -16 + level * 16)
    for side in (-1, 1) for level, width in enumerate((1.0, 0.7, 0.7))
)

CHEVRONS = tuple((ARM_ROOTS[index], complex(0, ARM_ROOTS[index].imag + 12), ARM_ROOTS[index + 3]) for index in range(3))
FOOTHOLDS = tuple(
    root + complex(side * offset.real, offset.imag)
    for side in (-1, 1)
    for root, offset in zip(ARM_ROOTS[:3] if side < 0 else ARM_ROOTS[3:], (rect(127.5, angle) for angle in (0.8, 1.1, 1.45)))
)

# Density colors inspired by resources/lenia_showcase.gif.
DENSITY_COLORS = (
    (0.0, (0, 0, 0)), (0.1, (8, 5, 19)), (0.23, (26, 18, 67)),
    (0.36, (42, 28, 134)), (0.48, (38, 51, 217)), (0.58, (32, 141, 249)),
    (0.68, (55, 236, 225)), (0.78, (150, 255, 204)), (0.87, (255, 247, 186)),
    (0.94, (255, 196, 171)), (1.0, (255, 159, 202)),
)


def density_color(value: int) -> tuple[int, ...]:
    level = value / 255
    (lower, start), (upper, end) = next(pair for pair in pairwise(DENSITY_COLORS) if level <= pair[1][0])
    fraction = (level - lower) / (upper - lower)
    return tuple(round(a + (b - a) * fraction) for a, b in zip(start, end))


DENSITY_CHANNELS = tuple(zip(*(density_color(value) for value in range(256))))


def clamp(value: float, lower: float, upper: float) -> float:
    return max(lower, min(upper, value))


@dataclass(frozen=True)
class Segment:
    start: complex
    end: complex


@dataclass
class Foot:
    position: complex
    start: complex = 0j
    target: complex = 0j
    progress: float = 1.0

    @property
    def planted(self) -> bool:
        return self.progress == 1.0

    def step(self, target: complex) -> None:
        self.start, self.target = self.position, target
        self.progress = 0.0

    def advance(self, dt: float, root: complex) -> None:
        if self.planted:
            return

        self.progress = min(1.0, self.progress + dt / 0.24)
        blend = self.progress * self.progress * (3 - 2 * self.progress)
        travel = self.target - self.start
        position = self.start + travel * blend + travel * 0.08j * sin(pi * self.progress)
        offset = position - root
        self.position = root + offset * min(1.0, MAX_REACH / max(abs(offset), 1e-9))


@dataclass
class Hexapod:
    position: complex = 0j
    heading: float = -0.45
    motors_running: bool = True
    time: float = 0.0
    last_step: float = -1.0
    feet: tuple[Foot, ...] = field(init=False)
    fluid: Image.Image | None = field(default=None, repr=False)
    fluid_time: float = 0.0

    def __post_init__(self) -> None:
        self.feet = tuple(
            Foot(self.position + (point + complex(0, (1 - index % 3) * 8)) * rect(1, self.heading))
            for index, point in enumerate(FOOTHOLDS)
        )

    def root(self, index: int, position: complex | None = None, heading: float | None = None) -> complex:
        position = self.position if position is None else position
        heading = self.heading if heading is None else heading
        return position + ARM_ROOTS[index] * rect(1, heading)

    def destination(self) -> complex:
        return self.position + rect(180, pi / 2 - 0.45 + 0.35 * sin(self.time * 0.4))

    def velocity(self, target: complex | None = None) -> complex:
        if not self.motors_running:
            return 0j

        target = self.destination() if target is None else target
        offset = target - self.position
        # Planted arms push backward; contact resistance converts their motor motion into body motion.
        direction = 1j * rect(1, self.heading)
        alignment = max(0.0, (offset.conjugate() * direction).real / max(abs(offset), 1e-9))
        motor_velocity = -direction * min(48.0, abs(offset) * 3) * alignment
        contacts = sum(foot.planted for foot in self.feet)
        return -motor_velocity * contacts / (2 + contacts)

    def advance(self, dt: float, target: complex | None = None) -> None:
        if not isfinite(dt) or dt < 0:
            raise ValueError("dt must be finite and nonnegative")

        if not self.motors_running:
            return

        steps = max(1, ceil(dt * 240))
        interval = dt / steps
        for _ in range(steps):
            self.time += interval
            goal = self.destination() if target is None else target
            velocity = self.velocity(goal)
            offset = goal - self.position
            desired_heading = phase(offset) - pi / 2 if abs(offset) > 1 else self.heading
            turn = (desired_heading - self.heading + pi) % (2 * pi) - pi
            heading = self.heading + clamp(turn, -interval, interval) * min(1, sum(foot.planted for foot in self.feet))
            position = self.position + velocity * interval
            if all(
                abs(foot.position - self.root(index, position, heading)) <= MAX_REACH
                for index, foot in enumerate(self.feet) if foot.planted
            ):
                self.position, self.heading = position, heading

            for index, foot in enumerate(self.feet):
                foot.advance(interval, self.root(index))

            direction = 1j * rect(1, self.heading)
            reach = MAX_REACH - 8 - min(80, abs(turn) * 80)
            footholds = tuple(
                self.root(index) + lateral + direction * (reach ** 2 - abs(lateral) ** 2) ** 0.5
                for index, point in enumerate(FOOTHOLDS)
                for lateral in (rect((point - ARM_ROOTS[index]).real * 0.25, self.heading),)
            )
            candidate = max(
                (index for index, foot in enumerate(self.feet) if foot.planted),
                key=lambda index: abs(self.feet[index].position - footholds[index]), default=None,
            )

            if (
                candidate is not None and sum(not foot.planted for foot in self.feet) < 2
                and self.time - self.last_step >= 0.1
                and abs(self.feet[candidate].position - footholds[candidate]) > 240
            ):
                self.feet[candidate].step(footholds[candidate])
                self.last_step = self.time

    def arm(self, index: int) -> tuple[Segment, ...]:
        root, foot = self.root(index), self.feet[index].position
        offset = foot - root
        distance = max(abs(offset), 1e-9)
        side = -1 if index < 3 else 1
        count = len(LENGTHS)
        lower, upper = 0.0, 2 * pi / count
        for _ in range(50):
            bend = (lower + upper) / 2
            reach = LENGTHS[0] * sin(count * bend / 2) / sin(bend / 2)
            if reach > distance:
                lower = bend
            else:
                upper = bend

        bend = (lower + upper) / 2
        vertices = tuple(accumulate(
            (rect(length, phase(offset) + side * bend * (part - (count - 1) / 2))
             for part, length in enumerate(LENGTHS)),
            initial=root,
        ))

        vertices = vertices[:-1] + (foot,)
        return tuple(Segment(start, end) for start, end in pairwise(vertices))

    def segments(self) -> tuple[Segment, ...]:
        return tuple(segment for index in range(6) for segment in self.arm(index))


def noise_hash(x: float, y: float) -> float:
    x, y = (x * 233.34) % 1, (y * 851.74) % 1
    offset = x * (x + 23.45) + y * (y + 23.45)
    return ((x + offset) * (y + offset)) % 1


def value_noise(x: float, y: float) -> float:
    ix, iy = floor(x), floor(y)
    fx, fy = x - ix, y - iy
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    upper = noise_hash(ix, iy) * (1 - fx) + noise_hash(ix + 1, iy) * fx
    lower = noise_hash(ix, iy + 1) * (1 - fx) + noise_hash(ix + 1, iy + 1) * fx
    return upper * (1 - fy) + lower * fy


def fluid_wake(creature: Hexapod, density: Image.Image) -> Image.Image:
    size = density.width // 2
    source = density.resize((size, size), Image.Resampling.BILINEAR)
    if creature.fluid is None or creature.fluid.size != source.size:
        creature.fluid = Image.new("L", source.size)
        creature.fluid_time = creature.time

    elapsed = creature.time - creature.fluid_time
    steps = ceil(elapsed * 30)
    interval = elapsed / max(1, steps)
    injection = source.point(lambda value: round(max(0, value - 60) * interval * 1.8))
    for step in range(steps):
        timestamp = creature.fluid_time + (step + 1) * interval
        fluid = creature.fluid

        def traced(x: int, y: int) -> tuple[float, float]:
            px, py = x % size, y % size
            gradient_x = (fluid.getpixel(((px + 2) % size, py)) - fluid.getpixel(((px - 2) % size, py))) / 255
            gradient_y = (fluid.getpixel((px, (py + 2) % size)) - fluid.getpixel((px, (py - 2) % size))) / 255
            state_x = (source.getpixel(((px + 2) % size, py)) - source.getpixel(((px - 2) % size, py))) / 255
            state_y = (source.getpixel((px, (py + 2) % size)) - source.getpixel((px, (py - 2) % size))) / 255
            nx, ny = x * 0.075 + timestamp * 0.3, y * 0.075 - timestamp * 0.22
            curl_x = value_noise(nx, ny + 0.5) - value_noise(nx, ny - 0.5)
            curl_y = value_noise(nx - 0.5, ny) - value_noise(nx + 0.5, ny)
            vx = -state_y * 14 - gradient_y * 20 + curl_x * 9
            vy = state_x * 14 + gradient_x * 20 + curl_y * 9
            return x - vx * interval * 6, y - vy * interval * 6

        mesh = tuple(
            ((x, y, min(x + 8, size), min(y + 8, size)),
             tuple(coordinate for corner in ((x, y), (x, min(y + 8, size)),
                                            (min(x + 8, size), min(y + 8, size)), (min(x + 8, size), y))
                   for coordinate in traced(*corner)))
            for y in range(0, size, 8) for x in range(0, size, 8)
        )

        advected = fluid.transform(source.size, Image.Transform.MESH, mesh, Image.Resampling.BILINEAR)
        diffused = Image.blend(advected, advected.filter(ImageFilter.GaussianBlur(0.6)), 0.15)
        creature.fluid = ImageChops.add(diffused.point(lambda value: round(value * exp(-interval * 0.65))), injection).point(lambda value: min(110, value))

    creature.fluid_time = creature.time
    return creature.fluid.resize(density.size, Image.Resampling.BILINEAR).filter(ImageFilter.GaussianBlur(0.8))


def render(creature: Hexapod, size: int = 512, zoom: float = 1.0, centered: bool = False, channels: tuple = DENSITY_CHANNELS) -> Image.Image:
    scale = 2 * zoom
    layer = Image.new("L", (size * 2, size * 2))
    interior = Image.new("L", layer.size)
    draw = ImageDraw.Draw(layer)
    organs = ImageDraw.Draw(interior)
    center = complex(size / 2, size / 2 if centered else size * 0.55)

    def point(position: complex) -> tuple[float, float]:
        location = center * 2 + (position - creature.position) * scale
        return location.real, location.imag

    for index in range(6):
        for link, segment in enumerate(creature.arm(index)):
            samples = 4 if zoom < 0.3 else 12
            for sample in range(samples):
                start = segment.start + (segment.end - segment.start) * sample / samples
                end = segment.start + (segment.end - segment.start) * (sample + 1) / samples
                wave = sin(creature.time * 4.0 - (link + sample / samples) * 2.6 * 3 / len(LENGTHS) + index * 0.8)
                radius = (8.5 + 0.3 * wave) * scale
                draw.line((point(start), point(end)), fill=round(125 + 5 * wave), width=round(2 * radius))
                x, y = point(end)
                draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=round(125 + 5 * wave))
                radius = 3 * scale
                organs.line((point(start), point(end)), fill=round(35 + 25 * wave), width=round(2 * radius))
                organs.ellipse((x - radius, y - radius, x + radius, y + radius), fill=round(35 + 25 * wave))

    head = tuple(point(creature.position + vertex * rect(1, creature.heading)) for vertex in HEAD)
    draw.polygon(head, fill=130)
    torso = (ARM_ROOTS[0], ARM_ROOTS[3], ARM_ROOTS[5], CHEVRONS[-1][1], ARM_ROOTS[2])
    draw.polygon(tuple(point(creature.position + vertex * rect(1, creature.heading)) for vertex in torso), fill=130)
    rails = tuple((ARM_ROOTS[index], ARM_ROOTS[index + 2]) for index in (0, 3))
    for structure in CHEVRONS + rails:
        vertices = tuple(point(creature.position + vertex * rect(1, creature.heading)) for vertex in structure)
        draw.line(vertices, fill=130, width=max(1, round(11 * scale)), joint="curve")

    torso_mask = Image.new("L", layer.size)
    ImageDraw.Draw(torso_mask).polygon(
        tuple(point(creature.position + vertex * rect(1, creature.heading)) for vertex in torso), fill=255,
    )

    gradient = Image.new("L", layer.size)
    shading = ImageDraw.Draw(gradient)
    axis = 1j * rect(1, creature.heading)
    across = rect(1, creature.heading) * 60
    for row in range(-16, 30):
        centerline = creature.position + axis * row
        intensity = round(35 + 25 * sin(creature.time * 2.2 - row * 0.065))
        shading.line((point(centerline - across), point(centerline + across)), fill=intensity, width=max(1, round(3 * scale)))

    interior = ImageChops.lighter(interior, ImageChops.multiply(gradient, torso_mask))
    organs = ImageDraw.Draw(interior)
    pulse = sin(creature.time * 2.2)
    nucleus = tuple(
        point(creature.position + (rect(56 / 3, -pi / 2 + index * pi / 3) + complex(1.5 * pulse, -32)) * rect(1, creature.heading))
        for index in range(6)
    )

    organs.polygon(nucleus, fill=round(118 + 10 * pulse))
    layer = ImageChops.add(
        layer.filter(ImageFilter.GaussianBlur(2.8 * scale)),
        interior.filter(ImageFilter.GaussianBlur(2 * scale)),
    )

    halo = layer.filter(ImageFilter.GaussianBlur(4.5 * scale)).point(lambda value: round(value * 0.8))
    layer = ImageChops.lighter(layer, halo)
    if not centered:
        layer = ImageChops.offset(layer, round(creature.position.real * scale), round(creature.position.imag * scale))
    density = layer.resize((size, size), Image.Resampling.LANCZOS)
    if centered:
        offset = (round(creature.position.real * zoom), round(creature.position.imag * zoom))
        wake = fluid_wake(creature, ImageChops.offset(density, *offset))
        density = ImageChops.lighter(density, ImageChops.offset(wake, -offset[0], -offset[1]))
    else:
        density = ImageChops.lighter(density, fluid_wake(creature, density))
    return Image.merge("RGB", tuple(density.point(channel) for channel in channels))


def record(path: Path, duration: float, fps: int) -> None:
    creature = Hexapod()

    def frame() -> Image.Image:
        image = render(creature)
        creature.advance(1 / fps)
        return image

    animation = (frame() for _ in range(ceil(duration * fps)))
    path.parent.mkdir(parents=True, exist_ok=True)
    next(animation).save(
        path, save_all=True, append_images=animation, duration=round(1000 / fps),
        loop=0, disposal=2,
    )

    print(f"Saved {path}; travel: {abs(creature.position):.2f} pixels")


def display() -> None:
    import pygame

    pygame.display.init()
    screen = pygame.display.set_mode((512, 512))
    pygame.display.set_caption("Six-arm walker — Mouse: steer, A: automatic, Space: motors, R: reset, Esc: quit")
    clock = pygame.time.Clock()
    creature = Hexapod()
    target = None
    running = True
    try:
        while running:
            dt = min(clock.tick(60) / 1000, 0.05)
            for event in pygame.event.get():
                if event.type == pygame.QUIT or (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                    running = False

                if event.type == pygame.MOUSEMOTION:
                    center = complex((256 + creature.position.real) % 512, (512 * 0.55 + creature.position.imag) % 512)
                    target = creature.position + complex(*event.pos) - center

                if event.type == pygame.KEYDOWN and event.key == pygame.K_a:
                    target = None

                if event.type == pygame.KEYDOWN and event.key == pygame.K_SPACE:
                    creature.motors_running = not creature.motors_running

                if event.type == pygame.KEYDOWN and event.key == pygame.K_r:
                    creature, target = Hexapod(), None

            creature.advance(dt, target)
            image = render(creature)
            screen.blit(pygame.image.frombytes(image.tobytes(), image.size, "RGB"), (0, 0))
            pygame.display.flip()
    finally:
        pygame.quit()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="A six-arm walker with planted feet and articulated reaching steps.")
    parser.add_argument("--record", type=Path, help="Save a GIF instead of opening the live viewer")
    parser.add_argument("--duration", type=float, default=8)
    parser.add_argument("--fps", type=int, default=25)
    args = parser.parse_args()
    if not isfinite(args.duration) or not 0 < args.duration <= 30 or not 1 <= args.fps <= 60:
        parser.error("duration must be between 0 and 30 seconds and fps between 1 and 60")

    if args.record is None:
        display()
    else:
        record(args.record, args.duration, args.fps)
