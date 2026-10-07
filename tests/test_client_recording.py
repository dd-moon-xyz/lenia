import importlib.util
import unittest

if importlib.util.find_spec("pygame") is not None:
    from client import VideoRecorder


class VideoWriter:
    def __init__(self):
        self.frames = []

    def write(self, frame):
        self.frames.append(frame)

    async def drain(self):
        pass


@unittest.skipUnless(importlib.util.find_spec("pygame"), "Requires the optional pygame client")
class RecordingTests(unittest.IsolatedAsyncioTestCase):
    async def test_recording_preserves_display_intervals_and_final_hold(self):
        writer = VideoWriter()
        video = VideoRecorder(writer, 30)
        await video.write(b"first", 0.0)
        await video.write(b"second", 0.1)
        await video.write(b"third", 0.4)
        await video.advance(0.5)
        self.assertEqual(writer.frames, [b"first"] * 3 + [b"second"] * 9 + [b"third"] * 3)

    async def test_fast_arrivals_do_not_accelerate_video_time(self):
        writer = VideoWriter()
        video = VideoRecorder(writer, 30)
        for index in range(100):
            await video.write(bytes([index]), index / 300)

        await video.advance(1 / 3)
        self.assertEqual(len(writer.frames), 10)

    async def test_no_frames_before_first_display(self):
        writer = VideoWriter()
        video = VideoRecorder(writer, 30)
        await video.advance(20.0)
        self.assertFalse(writer.frames)
