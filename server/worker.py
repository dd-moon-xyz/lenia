import asyncio
import struct
from contextlib import asynccontextmanager, suppress
from collections.abc import AsyncIterator

from server.catalog import ANIMALS, ROOT
from server.frames import encode_frame
from server.models import Organism, Start
from server.arena import CircularArena

__all__ = ["SimulationWorker", "simulation_worker"]


class SimulationWorker:
    def __init__(self, process: asyncio.subprocess.Process, config: Start):
        self.process = process
        self.config = config
        self.masses = ()
        self.lock = asyncio.Lock()

    def organism_line(self, item: Organism) -> str:
        size = organism_config(self.config, item).size
        return f"{size} {item.type} {item.scale} {item.x} {item.y} {item.velocity}"

    async def ready(self) -> None:
        await self.process.stdin.drain()
        async with asyncio.timeout(30):
            ready = await self.process.stdout.readline()

        if ready != b"READY\n":
            raise RuntimeError("Simulation failed to start; check server logs and CUDA/OpenGL setup")

    async def start(self) -> None:
        if self.config.boundary == "circle":
            header = f"{self.config.size} {self.config.fps} {self.config.dt} {len(self.config.organisms)}\n"
            organisms = "".join(f"{self.organism_line(item)}\n" for item in self.config.organisms)
        else:
            first = self.config.organisms[0]
            header = f"{self.config.size} {first.type} {first.scale} {self.config.dt} {len(self.config.organisms)}\n"
            organisms = "".join(f"{item.x} {item.y}\n" for item in self.config.organisms)

        self.process.stdin.write((header + organisms).encode())
        await self.ready()

    async def frame(self) -> bytes:
        started = asyncio.get_running_loop().time()
        async with self.lock:
            self.process.stdin.write(b"n")
            await self.process.stdin.drain()
            async with asyncio.timeout(30):
                if self.config.boundary == "circle":
                    count = len(self.config.organisms)
                    metadata = await self.process.stdout.readexactly(4 * count)
                    self.masses = struct.unpack(f"={count}f", metadata)

                raw = await self.process.stdout.readexactly(self.config.size * self.config.size * 3)

        if self.config.boundary == "circle":
            return raw

        frame = await asyncio.to_thread(encode_frame, raw, self.config.size, self.config.pixel_size)
        await asyncio.sleep(max(0, 1 / self.config.fps - (asyncio.get_running_loop().time() - started)))
        return frame

    async def replace_organism(self, index: int, organism: Organism, angle: float) -> None:
        async with self.lock:
            command = f"r {index} {self.organism_line(organism)} {angle}\n"
            self.process.stdin.write(command.encode())
            await self.ready()

    async def redirect(self, outward: bool) -> None:
        async with self.lock:
            self.process.stdin.write(b"o" if outward else b"i")
            await self.ready()

    async def close(self) -> None:
        if self.process.returncode is None:
            with suppress(ProcessLookupError):
                self.process.kill()

        await self.process.wait()


@asynccontextmanager
async def simulation_worker(config: Start) -> AsyncIterator[SimulationWorker | CircularArena]:
    process = await asyncio.create_subprocess_exec(
        str(ROOT / "build/lenia"), "--stream-arena" if config.boundary == "circle" else "--stream",
        cwd=ROOT / "build",
        stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE,
    )

    worker = SimulationWorker(process, config)
    arena = CircularArena(config, worker) if config.boundary == "circle" else None
    try:
        await worker.start()
        yield arena if arena is not None else worker
    finally:
        if arena is not None:
            await arena.close()

        await worker.close()


def organism_config(config: Start, item: Organism) -> Start:
    animal = ANIMALS[item.type]
    extent = max(2 * int(animal[5]) * item.scale + 1, 2 * int(animal[12]) * item.scale, 2 * int(animal[13]) * item.scale)
    size = min(config.size, max(256, 1 << (extent - 1).bit_length()))
    return config.model_copy(update={
        "size": size,
        "dt": min(config.dt, 1 / float(animal[6])),
        "organisms": [item.model_copy(update={"x": size // 2, "y": size // 2})],
    })
