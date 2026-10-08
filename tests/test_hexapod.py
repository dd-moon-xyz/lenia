import unittest
from cmath import phase, rect

from hexapod import HEAD, Hexapod, LENGTHS, render


class HexapodTests(unittest.TestCase):
    def test_fluid_wake_uses_density_and_freezes_with_the_creature(self):
        creature = Hexapod()
        render(creature, 128)
        for _ in range(10):
            creature.advance(0.1)
            render(creature, 128)

        self.assertIsNotNone(creature.fluid.getbbox())
        before = creature.fluid.tobytes()
        creature.motors_running = False
        creature.advance(1)
        render(creature, 128)
        self.assertEqual(creature.fluid.tobytes(), before)

    def test_movement_points_away_from_the_head(self):
        creature = Hexapod()
        for _ in range(240):
            before = creature.position
            creature.advance(1 / 60)
            travel = creature.position - before
            head = sum(HEAD) / len(HEAD) * rect(1, creature.heading)
            self.assertLess((travel.conjugate() * head).real, 0)

    def test_planted_feet_hold_the_ground_while_body_moves(self):
        creature = Hexapod()
        steps = 0
        for _ in range(240):
            before = tuple((foot.position, foot.planted) for foot in creature.feet)
            creature.advance(1 / 60)
            for (position, planted), foot in zip(before, creature.feet):
                if planted and foot.planted:
                    self.assertEqual(foot.position, position)

                steps += planted and not foot.planted

            self.assertGreaterEqual(sum(foot.planted for foot in creature.feet), 4)

        self.assertGreater(abs(creature.position), 30)
        self.assertGreaterEqual(steps, 6)

    def test_stopping_motors_freezes_arms_and_body(self):
        creature = Hexapod()
        creature.advance(0.8)
        creature.motors_running = False
        state = creature.position, creature.heading, creature.time, creature.segments()
        creature.advance(2)
        self.assertEqual(creature.velocity(), 0j)
        self.assertEqual((creature.position, creature.heading, creature.time, creature.segments()), state)

    def test_body_cannot_move_without_planted_arms(self):
        creature = Hexapod()
        for foot in creature.feet:
            foot.step(foot.position)

        self.assertEqual(creature.velocity(), 0j)
        creature.advance(0.1)
        self.assertEqual(creature.position, 0j)

    def test_walker_steers_toward_a_target(self):
        for target in (-100 - 100j, 100 - 100j):
            creature = Hexapod()
            creature.advance(5, target)
            self.assertGreater(creature.position.real * target.real, 0)
            self.assertLess(creature.position.imag, 0)
            self.assertLess(abs(creature.position - target), abs(target))

    def test_motion_is_independent_of_render_frame_rate(self):
        slow, fast = Hexapod(), Hexapod()
        for creature, fps in ((slow, 20), (fast, 60)):
            for _ in range(fps * 3):
                creature.advance(1 / fps)

        self.assertAlmostEqual(abs(slow.position - fast.position), 0, places=7)
        self.assertAlmostEqual(slow.heading, fast.heading, places=7)

    def test_six_articulated_arms_keep_their_lengths_and_reach_their_feet(self):
        creature = Hexapod()
        for _ in range(100):
            creature.advance(0.1)
            self.assertEqual(len(creature.segments()), 18)
            for index, foot in enumerate(creature.feet):
                arm = creature.arm(index)
                self.assertEqual(len(arm), 3)
                self.assertEqual(arm[0].start, creature.root(index))
                self.assertEqual(arm[-1].end, foot.position)
                for first, second in zip(arm, arm[1:]):
                    turn = phase((second.end - second.start) / (first.end - first.start))
                    self.assertGreater(abs(turn), 0.01)

                for segment, length in zip(arm, LENGTHS):
                    self.assertAlmostEqual(abs(segment.end - segment.start), length, places=7)
