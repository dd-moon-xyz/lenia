import asyncio
import json
import unittest
from contextlib import asynccontextmanager
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from lenia.server.catalog import ANIMALS
from lenia.server.routes import stream


class PopulationTests(unittest.TestCase):
    def test_population_is_opt_in_and_uses_live_mass(self):
        for enabled in (False, True):
            config = {
                "action": "start", "simulation": "hexapod", "boundary": "circle",
                "population_updates": enabled,
                "organisms": [{"type": len(ANIMALS)}] * 4,
            }
            websocket = SimpleNamespace(
                client="test", accept=AsyncMock(), close=AsyncMock(),
                receive_text=AsyncMock(side_effect=(json.dumps(config), "next", "stop")),
                send_json=AsyncMock(), send_bytes=AsyncMock(),
            )
            worker = SimpleNamespace(
                frame=AsyncMock(return_value=b"frame"),
                worker=SimpleNamespace(masses=(1.0, .001, 0.0, .002)),
            )

            @asynccontextmanager
            async def simulation_worker(_):
                yield worker

            with patch("lenia.server.routes.simulation_worker", simulation_worker):
                asyncio.run(stream(websocket))

            reports = tuple(call.args[0] for call in websocket.send_json.call_args_list
                            if call.args[0].get("status") == "population")
            expected = ({"status": "population", "alive": 2, "total": 4},) if enabled else ()
            self.assertEqual(reports, expected)
            websocket.send_bytes.assert_awaited_once_with(b"frame")
