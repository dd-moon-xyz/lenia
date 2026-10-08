import argparse
import asyncio
import io
import json
from contextlib import AsyncExitStack, asynccontextmanager, suppress
from pathlib import Path

import pygame
from websockets.asyncio.client import connect
from websockets.exceptions import WebSocketException

from server.presets import default_config

DEFAULT_CONFIG = default_config()


class VideoRecorder:
    def __init__(self, writer: asyncio.StreamWriter, fps: int):
        self.writer = writer
        self.fps = fps
        self.started = None
        self.frame = None
        self.frames = 0

    async def advance(self, timestamp: float) -> None:
        if self.frame is None:
            return

        target = max(1, round((timestamp - self.started) * self.fps))
        for _ in range(max(0, target - self.frames)):
            self.writer.write(self.frame)
            await self.writer.drain()
            self.frames += 1

    async def write(self, frame: bytes, timestamp: float) -> None:
        if self.started is None:
            self.started = timestamp

        await self.advance(timestamp)
        self.frame = frame


@asynccontextmanager
async def record_video(path: Path, fps: int):
    process = await asyncio.create_subprocess_exec(
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-f", "image2pipe", "-framerate", str(fps), "-c:v", "mjpeg", "-i", "pipe:0",
        "-vf", "scale=1080:1920:force_original_aspect_ratio=decrease:flags=lanczos,pad=1080:1920:(ow-iw)/2:(oh-ih)/2:black,setsar=1",
        "-an", "-c:v", "libx264", "-preset", "veryfast", "-crf", "18",
        "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(path.resolve()),
        stdin=asyncio.subprocess.PIPE,
    )

    video = VideoRecorder(process.stdin, fps)
    try:
        yield video
    finally:
        try:
            await video.advance(asyncio.get_running_loop().time())
        finally:
            process.stdin.close()
            try:
                code = await process.wait()
            finally:
                if process.returncode is None:
                    process.kill()
                    await process.wait()

        if code != 0:
            raise ValueError(f"Video recording failed (FFmpeg exit {code})")


async def display_stream(url: str, config: dict, commands: asyncio.Queue, record: Path | None = None) -> None:
    async with connect(url, max_size=4 * 1024 * 1024, compression=None) as websocket, AsyncExitStack() as recording:
        video = None
        await websocket.send(json.dumps(config))
        async for message in websocket:
            if isinstance(message, str):
                status = json.loads(message)
                if "error" in status:
                    raise ValueError(status["error"])

                if status.get("status") == "started":
                    pygame.display.set_mode((status["size"], status["size"]))
                    pygame.display.set_caption("Lenia stream — Hold Space/C outward, V inward, Esc to quit")
                    if record is not None:
                        video = await recording.enter_async_context(record_video(record, config.get("fps", 20)))

                    await websocket.send("next")

                continue

            while not commands.empty():
                await websocket.send(commands.get_nowait())

            await websocket.send("next")
            frame = pygame.image.load(io.BytesIO(message), "frame.jpg")
            pygame.display.get_surface().blit(frame, (0, 0))
            pygame.display.flip()
            if video is not None:
                await video.write(message, asyncio.get_running_loop().time())


async def run(url: str, config: dict, record: Path | None = None) -> None:
    pygame.display.init()
    pygame.display.set_mode((512, 512))
    pygame.display.set_caption("Lenia stream — connecting…")
    commands = asyncio.Queue()
    held = {}
    steering_keys = {pygame.K_SPACE: "outward", pygame.K_c: "outward", pygame.K_v: "inward_fast"}
    stream = asyncio.create_task(display_stream(url, config, commands, record))
    try:
        while not stream.done():
            events = pygame.event.get()
            if any(
                event.type == pygame.QUIT or (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE)
                for event in events
            ):

                return

            if config.get("boundary") == "circle":
                for event in events:
                    if event.type in {pygame.KEYDOWN, pygame.KEYUP} and event.key in steering_keys:
                        if event.type == pygame.KEYDOWN and event.key not in held:
                            held[event.key] = steering_keys[event.key]
                            commands.put_nowait(held[event.key])

                        if event.type == pygame.KEYUP and event.key in held:
                            del held[event.key]
                            commands.put_nowait(next(reversed(held.values()), "inward"))

                    if event.type == pygame.WINDOWFOCUSLOST:
                        held.clear()
                        commands.put_nowait("inward")

            await asyncio.sleep(0.01)

        await stream
    finally:
        stream.cancel()
        with suppress(asyncio.CancelledError):
            await stream

        pygame.display.quit()


def main() -> None:
    parser = argparse.ArgumentParser(description="Display a Lenia WebSocket animation in pygame")
    parser.add_argument("--url", default="ws://127.0.0.1:8000/ws", help="Server WebSocket URL")
    parser.add_argument("--config", type=Path, help="JSON file containing the server start configuration")
    parser.add_argument("--record", type=Path, help="Record received frames to an MP4 video (requires FFmpeg)")
    args = parser.parse_args()
    try:
        config = DEFAULT_CONFIG
        if args.config is not None:
            config = json.loads(args.config.read_text())

        asyncio.run(run(args.url, config, args.record))
    except (OSError, ValueError, WebSocketException, pygame.error) as error:
        parser.exit(1, f"Error: {error}\n")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
