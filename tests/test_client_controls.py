import importlib.util
import unittest
from unittest.mock import patch

if importlib.util.find_spec("pygame") is not None:
    import client
    import pygame


@unittest.skipUnless(importlib.util.find_spec("pygame"), "Requires the optional pygame client")
class ControlTests(unittest.IsolatedAsyncioTestCase):
    async def test_direction_priority_release_and_focus_loss(self):
        sent = []

        async def stream(url, config, commands, record):
            while True:
                sent.append(await commands.get())

        events = iter(([
            pygame.event.Event(pygame.KEYDOWN, key=pygame.K_c),
            pygame.event.Event(pygame.KEYDOWN, key=pygame.K_c),
            pygame.event.Event(pygame.KEYDOWN, key=pygame.K_v),
            pygame.event.Event(pygame.KEYUP, key=pygame.K_v),
            pygame.event.Event(pygame.KEYUP, key=pygame.K_c),
            pygame.event.Event(pygame.KEYDOWN, key=pygame.K_SPACE),
            pygame.event.Event(pygame.KEYDOWN, key=pygame.K_v),
            pygame.event.Event(pygame.WINDOWFOCUSLOST),
            pygame.event.Event(pygame.KEYUP, key=pygame.K_v),
        ], [pygame.event.Event(pygame.QUIT)]))

        with (
            patch.object(client, "display_stream", stream),
            patch.object(pygame.event, "get", side_effect=lambda: next(events)),
            patch.object(pygame.display, "init"),
            patch.object(pygame.display, "set_mode"),
            patch.object(pygame.display, "set_caption"),
            patch.object(pygame.display, "quit"),
        ):

            await client.run("unused", {"boundary": "circle"})

        self.assertEqual(sent, ["outward", "inward_fast", "outward", "inward", "outward", "inward_fast", "inward"])
