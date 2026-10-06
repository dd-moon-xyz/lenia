import asyncio
import logging
import math
import random

from server.births import replacement_organism
from server.frames import encode_frame
from server.models import Start
from server.velocity import normalize_velocities

__all__ = ["CircularArena"]


class CircularArena:
    def __init__(self, config: Start, worker):
        self.config = config
        self.worker = worker
        self.organisms = list(config.organisms)
        self.dead_frames = [0] * len(config.organisms)
        self.pending = {}
        self.deadline = None

    async def frame(self) -> bytes:
        loop = asyncio.get_running_loop()
        started = loop.time()
        for index, task in tuple(self.pending.items()):
            if task.done():
                await task
                del self.pending[index]

        raw = await self.worker.frame()
        for index, mass in enumerate(self.worker.masses):
            if index in self.pending:
                continue

            self.dead_frames[index] = self.dead_frames[index] + 1 if mass <= 0.001 else 0

            if self.dead_frames[index] >= 5:
                self.pending[index] = asyncio.create_task(self.replace(index))

        frame = await asyncio.to_thread(encode_frame, raw, self.config.size, self.config.pixel_size)
        interval = 1 / self.config.fps
        if self.deadline is None or started - self.deadline > interval:
            self.deadline = started

        self.deadline += interval
        await asyncio.sleep(max(0, self.deadline - loop.time()))
        return frame

    async def replace(self, index: int) -> None:
        logger = logging.getLogger("uvicorn.error")
        logger.info("Organism died: slot=%s type=%s", index, self.organisms[index].type)
        organism = replacement_organism(self.config.size)
        self.organisms[index] = organism
        normalize_velocities(self.organisms)
        angle = random.uniform(0, 2 * math.pi)
        await self.worker.replace_organism(index, organism, angle)
        self.dead_frames[index] = 0
        logger.info("Organism born outside visible circle: slot=%s type=%s scale=%s velocity=%.1f", index, organism.type, organism.scale, organism.velocity)

    async def redirect(self, outward: bool) -> None:
        await self.worker.redirect(outward)

    async def close(self) -> None:
        for task in self.pending.values():
            task.cancel()

        await asyncio.gather(*self.pending.values(), return_exceptions=True)
