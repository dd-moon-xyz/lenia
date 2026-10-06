import argparse
import asyncio
import io
import json
from contextlib import suppress
from pathlib import Path

import pygame
from websockets.asyncio.client import connect
from websockets.exceptions import WebSocketException

from server.presets import default_config

DEFAULT_CONFIG = default_config()


async def display_stream(url: str, config: dict, commands: asyncio.Queue) -> None:
    async with connect(url, max_size=4 * 1024 * 1024) as websocket:
        await websocket.send(json.dumps(config))
        async for message in websocket:
            if isinstance(message, str):
                status = json.loads(message)
                if "error" in status:
                    raise ValueError(status["error"])

                if status.get("status") == "started":
                    pygame.display.set_mode((status["size"], status["size"]))
                    pygame.display.set_caption("Lenia stream — Hold Space to move outward, Esc to quit")
                    await websocket.send("next")

                continue

            while not commands.empty():
                await websocket.send(commands.get_nowait())

            await websocket.send("next")
            frame = pygame.image.load(io.BytesIO(message), "frame.jpg").convert()
            pygame.display.get_surface().blit(frame, (0, 0))
            pygame.display.flip()


async def run(url: str, config: dict) -> None:
    pygame.display.init()
    pygame.display.set_mode((512, 512))
    pygame.display.set_caption("Lenia stream — connecting…")
    commands = asyncio.Queue()
    stream = asyncio.create_task(display_stream(url, config, commands))
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
                    if event.type in {pygame.KEYDOWN, pygame.KEYUP} and event.key == pygame.K_SPACE:
                        commands.put_nowait("outward" if event.type == pygame.KEYDOWN else "inward")

                    if event.type == pygame.WINDOWFOCUSLOST:
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
    args = parser.parse_args()
    try:
        config = DEFAULT_CONFIG
        if args.config is not None:
            config = json.loads(args.config.read_text())

        asyncio.run(run(args.url, config))
    except (OSError, ValueError, WebSocketException, pygame.error) as error:
        parser.exit(1, f"Error: {error}\n")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
