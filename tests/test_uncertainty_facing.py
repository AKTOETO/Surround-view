"""Independent known-answer controls for finite-face orientation diagnostics."""
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/research'))
from uncertainty_facing import face_masks, compose


class FacingTests(unittest.TestCase):
    def test_six_face_order_and_strict_camera_half_space(self):
        points = np.array([[-1, 0, 0], [1, 0, 0], [0, -1, 0], [0, 1, 0], [0, 0, -1], [0, 0, 1]])
        near, facing = face_masks([2, 2, 2], points, [-1]*3, [1]*3, 0)
        np.testing.assert_array_equal(near, np.eye(6, dtype=bool))
        np.testing.assert_array_equal(facing.any(axis=1), [0, 1, 0, 1, 0, 1])
        _, tangent = face_masks([1, 1, 1], points, [-1]*3, [1]*3, 0)
        self.assertFalse(tangent.any())

    def test_finite_band_excludes_extensions_and_deep_interior(self):
        points = np.array([[1.01, 0, 0], [1, 1.1, 0], [0, 0, 0]])
        near, facing = face_masks([2, 0, 0], points, [-1]*3, [1]*3, .02)
        np.testing.assert_array_equal(near.any(axis=1), [1, 0, 0])
        np.testing.assert_array_equal(facing.any(axis=1), [1, 0, 0])

    def test_corner_keeps_all_faces_without_axis_tie_choice(self):
        near, facing = face_masks([0, 2, 0], [[1, 1, 0]], [-1]*3, [1]*3, 0)
        np.testing.assert_array_equal(near[0], [0, 1, 0, 1, 0, 0])
        np.testing.assert_array_equal(facing[0], [0, 0, 0, 1, 0, 0])
        self.assertTrue(facing.any())  # A nearest-X tie choice would incorrectly reject.

    def test_axis_permutation_preserves_any_face_decision(self):
        points = np.array([[1, 1, 0], [-1, 0, 0], [0, 0, 1.01]])
        origin = np.array([0, 2, -2]); lo = np.array([-1., -1., -1.]); hi = -lo
        near, facing = face_masks(origin, points, lo, hi, .02)
        for axes in [[2, 0, 1], [1, 0, 2]]:
            n, f = face_masks(origin[axes], points[:, axes], lo[axes], hi[axes], .02)
            np.testing.assert_array_equal(n.any(axis=1), near.any(axis=1))
            np.testing.assert_array_equal(f.any(axis=1), facing.any(axis=1))

    def test_compose_linear_blend_and_removal_of_last_source(self):
        colors = np.array([[[1, 1, 1], [0, 0, 0]], [[1, 0, 0], [0, 0, 1]]], float)
        rgb, supported, normalized = compose(colors, [[1, 1], [0, 0]])
        np.testing.assert_allclose(rgb[0], [187.51603068]*3, atol=1e-7)
        np.testing.assert_array_equal(supported, [1, 0])
        np.testing.assert_allclose(normalized, [[.5, .5], [0, 0]])
        rgb, supported, _ = compose(colors[:1], [[1, 0]])
        np.testing.assert_allclose(rgb, [[255]*3]); self.assertTrue(supported[0])

    def test_compose_camera_permutation_and_zero_weight_independence(self):
        colors = np.array([[[.2, .5, .8], [.8, .2, .1], [1, 0, 1]]])
        weights = np.array([[.3, .7, 0]])
        a = compose(colors, weights)[0]
        np.testing.assert_allclose(compose(colors[:, [2, 0, 1]], weights[:, [2, 0, 1]])[0], a)
        colors[0, 2] = [0, 1, 0]
        np.testing.assert_allclose(compose(colors, weights)[0], a)

    def test_invalid_inputs_and_empty_samples(self):
        for band in [-1, np.nan, np.inf]:
            with self.assertRaises(ValueError): face_masks([0]*3, [[0]*3], [-1]*3, [1]*3, band)
        with self.assertRaises(ValueError): face_masks([0]*2, [[0]*3], [-1]*3, [1]*3, 0)
        with self.assertRaises(ValueError): face_masks([0]*3, [[np.nan]*3], [-1]*3, [1]*3, 0)
        with self.assertRaises(ValueError): face_masks([0]*3, [[0]*3], [1]*3, [-1]*3, 0)
        with self.assertRaises(ValueError): compose(np.zeros((1, 2, 3)), [[1, -1]])
        with self.assertRaises(ValueError): compose(np.zeros((1, 2, 3)), [[1]])
        n, f = face_masks([0]*3, np.empty((0, 3)), [-1]*3, [1]*3, 0)
        self.assertEqual(n.shape, (0, 6)); self.assertEqual(f.shape, (0, 6))


if __name__ == '__main__':
    unittest.main()
