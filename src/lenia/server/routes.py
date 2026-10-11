import asyncio
import logging
from contextlib import suppress
from pathlib import Path

from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse

from lenia.server.catalog import ANIMALS
from lenia.server.models import Start
from lenia.server.presets import default_config
from lenia.server.worker import simulation_worker

__all__ = ["router"]

router = APIRouter()
logger = logging.getLogger("uvicorn.error")


@router.get("/")
async def viewer():
    return FileResponse(Path(__file__).with_name("viewer.html"))


@router.get("/preset")
async def preset():
    return default_config()


@router.get("/organisms")
async def organisms():
    return [
        {"type": index, "name": row[4]} for index, row in enumerate(ANIMALS)
    ] + [{"type": len(ANIMALS), "name": "Six-arm hexapod"}]


@router.websocket("/ws")
async def stream(websocket: WebSocket):
    await websocket.accept()
    peer = websocket.client
    reason = "session finished"
    logger.info("WebSocket connection opened: %s", peer)
    try:
        async with asyncio.timeout(30):
            config = Start.model_validate_json(
                await websocket.receive_text()
            )

        async with simulation_worker(config) as worker:
            await websocket.send_json(
                {
                    "status": "started",
                    "size": config.output_size or config.size,
                    "fps": config.fps,
                }
            )
            while True:
                async with asyncio.timeout(60):
                    command = await websocket.receive_text()

                if command == "stop":
                    reason = "client requested stop"
                    await websocket.send_json({"status": "stopped"})
                    break

                if command in {"outward", "inward", "inward_fast"}:
                    if config.boundary != "circle":
                        raise ValueError(
                            "Directed trajectories require a circular arena"
                        )

                    await worker.redirect(
                        command == "outward", command != "inward"
                    )
                    continue

                if command != "next":
                    raise ValueError(
                        "Send 'next' for a frame, 'outward', 'inward', or 'inward_fast' to steer organisms, or 'stop' to end the simulation"
                    )

                await websocket.send_bytes(await worker.frame())
    except WebSocketDisconnect as error:
        reason = f"client disconnected (code {error.code})"
    except (
        ValueError,
        OSError,
        RuntimeError,
        TimeoutError,
        asyncio.IncompleteReadError,
    ) as error:
        reason = str(error) or "Simulation timed out"
        with suppress(WebSocketDisconnect, RuntimeError, OSError):
            await websocket.send_json({"error": reason})

    except asyncio.CancelledError:
        reason = "session cancelled"
        raise
    finally:
        with suppress(WebSocketDisconnect, RuntimeError, OSError):
            await websocket.close()

        logger.info("WebSocket connection closed: %s — %s", peer, reason)
