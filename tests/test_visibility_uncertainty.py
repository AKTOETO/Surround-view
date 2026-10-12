"""Known-answer and enclosure controls independent of the captured scene."""
import itertools
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/research'))
from visibility_uncertainty import certificates, pixel_certificates
from source_visibility import blocked_by_faces


class UncertaintyTests(unittest.TestCase):
    def test_known_answer_zero_and_boundary(self):
        points = np.array([[3, 0, 0], [0, 3, 0], [3, 1.05, 0], [0, 0, 0]])
        b, c = certificates([0, 0, 0], points, [1, -1, -1], [2, 1, 1], 0, 0)
        np.testing.assert_array_equal(b, [True, False, True, False])
        np.testing.assert_array_equal(c, ~b)
        b, c = certificates([0, 0, 0], [[3, 3, 0]], [1, -1, -1], [2, 1, 1], 0, .1)
        self.assertFalse(b[0]); self.assertFalse(c[0])  # tangent nominal, ambiguous when uncertain

    def test_endpoint_bias_origin_inside_and_empty_erosion(self):
        b, c = certificates([0, 0, 0], [[1, 0, 0], [1.01, 0, 0]], [1, -1, -1], [2, 1, 1], .02, 0)
        np.testing.assert_array_equal(b, [False, False]); self.assertTrue(c.all())
        b, c = certificates([1.5, 0, 0], [[1.51, 0, 0], [3, 0, 0]], [1, -1, -1], [2, 1, 1], .02, 0)
        np.testing.assert_array_equal(b, [False, True])
        b, c = certificates([0, 0, 0], [[3, 0, 0]], [1, -1, -1], [2, 1, 1], 0, .6)
        self.assertFalse(b[0]); self.assertFalse(c[0])

    def test_nested_certificates_enclose_translations_and_face_errors(self):
        rng = np.random.default_rng(20261013)
        points = rng.uniform(-4, 4, (200, 3))
        origin = [-3, -.5, .2]; lo = np.array([-1., -1., -1.]); hi = -lo; u = .2
        for bias in [0, .02, 10]:
            b, c = certificates(origin, points, lo, hi, bias, u)
            candidates = [(lo + d, hi + d) for d in map(np.array, itertools.product([-u, 0, u], repeat=3))]
            candidates += [(lo + rng.uniform(-u, u, 3), hi + rng.uniform(-u, u, 3)) for _ in range(100)]
            for lower, upper in candidates:
                actual = blocked_by_faces(origin, points, lower, upper, bias)
                self.assertFalse(np.any(b & ~actual)); self.assertFalse(np.any(c & actual))

    def test_larger_uncertainty_only_weakens_certificates(self):
        points = np.random.default_rng(17).uniform(-3, 3, (400, 3))
        last_b = last_c = np.ones(len(points), bool)
        for u in [0, .001, .1, .5, 1, 2]:
            b, c = certificates([-3, 0, 0], points, [-1, -1, -1], [1, 1, 1], .02, u)
            self.assertFalse(np.any(b & ~last_b)); self.assertFalse(np.any(c & ~last_c))
            last_b, last_c = b, c

    def test_collapsed_erosion_remains_a_closed_box(self):
        b, c = certificates([-2, 0, 0], [[2, 0, 0], [2, 1, 0]], [-1, -1, -1], [1, 1, 1], 0, 1)
        np.testing.assert_array_equal(b, [True, False])
        np.testing.assert_array_equal(c, [False, False])

    def test_pixel_partition_handles_unavailable_and_mixed_cameras(self):
        e = np.array([[0, 0], [1, 0], [1, 1], [1, 1]], bool)
        b = np.array([[0, 0], [1, 0], [1, 0], [1, 0]], bool)
        c = np.array([[0, 0], [0, 0], [0, 1], [0, 0]], bool)
        g, i, a = pixel_certificates(e, b, c)
        np.testing.assert_array_equal(g, [0, 0, 1, 0]); np.testing.assert_array_equal(i, [0, 1, 0, 0])
        np.testing.assert_array_equal(a, [0, 0, 0, 1])
        with self.assertRaises(ValueError): pixel_certificates(e, e, e)

    def test_invalid_inputs_and_empty_point_set(self):
        for u, bias in [(-1, 0), (float('nan'), 0), (0, -1), (0, float('inf'))]:
            with self.assertRaises(ValueError): certificates([0, 0, 0], [[3, 0, 0]], [1, -1, -1], [2, 1, 1], bias, u)
        with self.assertRaises(ValueError): certificates([0, 0, 0], [[np.nan, 0, 0]], [1, -1, -1], [2, 1, 1], 0, 0)
        with self.assertRaises(ValueError): certificates([0, 0], [[3, 0, 0]], [1, -1, -1], [2, 1, 1], 0, 0)
        with self.assertRaises(ValueError): certificates([0, 0, 0], [[3, 0, 0]], [2, -1, -1], [1, 1, 1], 0, 0)
        b, c = certificates([0, 0, 0], np.empty((0, 3)), [1, -1, -1], [2, 1, 1], 0, 0)
        self.assertEqual(len(b), 0); self.assertEqual(len(c), 0)


if __name__ == '__main__':
    unittest.main()
