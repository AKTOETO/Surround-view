"""Known-answer finite-face entry distances, independently derived fixtures."""
import sys
from pathlib import Path
import unittest
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/research'))
from facing_entry_gap import entry_gap
from source_visibility import blocked_by_faces


class EntryGapTests(unittest.TestCase):
    def test_axial_endpoint_before_at_and_beyond_face(self):
        got = entry_gap([-2, 0, 0], [[-1.1, 0, 0], [-1, 0, 0], [0, 0, 0], [2, 0, 0]], [-1]*3, [1]*3)
        self.assertTrue(np.isnan(got[0]))
        np.testing.assert_allclose(got[1:], [0, 1, 3])

    def test_oblique_corner_and_miss(self):
        got = entry_gap([-2, -2, 0], [[0, 0, 0], [-2, 2, 0]], [-1]*3, [1]*3)
        self.assertAlmostEqual(got[0], np.sqrt(2))
        self.assertTrue(np.isnan(got[1]))

    def test_inside_and_boundary_origin_match_closed_box(self):
        np.testing.assert_allclose(entry_gap([0, 0, 0], [[0, 0, 0], [2, 0, 0]], [-1]*3, [1]*3), [0, 2])
        np.testing.assert_allclose(entry_gap([-1, 0, 0], [[-2, 0, 0]], [-1]*3, [1]*3), [1])

    def test_strict_bias_threshold(self):
        o, p = [-2, 0, 0], [[0, 0, 0]]
        self.assertEqual(entry_gap(o, p, [-1]*3, [1]*3)[0], 1)
        self.assertTrue(blocked_by_faces(o, p, [-1]*3, [1]*3, .99)[0])
        self.assertFalse(blocked_by_faces(o, p, [-1]*3, [1]*3, 1)[0])

    def test_translation_axis_permutation_and_scale(self):
        o = np.array([-2, -3, 0]); p = np.array([[0, 0, 0], [-2, 0, 0]])
        expected = entry_gap(o, p, [-1]*3, [1]*3)
        shift = np.array([7, -4, 11]); perm = [2, 0, 1]
        actual = entry_gap((o*3+shift)[perm], (p*3+shift)[:,perm], (np.array([-1]*3)*3+shift)[perm], (np.array([1]*3)*3+shift)[perm])
        np.testing.assert_allclose(actual, expected*3, equal_nan=True)

    def test_invalid_and_empty(self):
        self.assertEqual(len(entry_gap([0]*3, np.empty((0,3)), [-1]*3, [1]*3)), 0)
        for o,p,lo,hi in [([0]*2, [[0]*3], [-1]*3, [1]*3), ([0]*3, [[np.nan,0,0]], [-1]*3, [1]*3), ([0]*3, [[0]*3], [1]*3, [1]*3)]:
            with self.assertRaises(ValueError):
                entry_gap(o,p,lo,hi)


if __name__ == '__main__':
    unittest.main()
