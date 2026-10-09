"""Peripheral raster capture: geometry, disjoint seeds and native detector."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from calibration.coverage import board_points, coverage, peripheral_poses
from calibration.optical_models import FAMILIES, project
from calibration.raster_board import K, SIZE, corners, detection_error, image, poses

BINARY = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / 'build/sv-calibrate')
sys.argv = sys.argv[:1]


class CoverageTests(unittest.TestCase):
    def test_seed_reproducibility_and_disjoint_validation(self):
        for seed in (4101, 4102):
            train = peripheral_poses(seed + 1000)
            repeat = peripheral_poses(seed + 1000)
            validation = peripheral_poses(seed + 2000)
            self.assertEqual(len(train), 8)
            for (r, t), (rr, tt) in zip(train, repeat):
                np.testing.assert_array_equal(r, rr)
                np.testing.assert_array_equal(t, tt)
            a = {tuple(np.r_[r.ravel(), t]) for r, t in train}
            b = {tuple(np.r_[r.ravel(), t]) for r, t in validation}
            self.assertEqual(len(a | b), 16)

    def test_rigid_front_facing_geometry(self):
        for r, t in peripheral_poses(5101):
            np.testing.assert_allclose(r.T @ r, np.eye(3), atol=1e-14)
            self.assertAlmostEqual(np.linalg.det(r), 1.)
            np.testing.assert_allclose(r[:, 2], t / np.linalg.norm(t), atol=1e-14)
            self.assertTrue(np.all(board_points(r, t, outer=True)[:, 2] > 0))

    def test_complete_board_border_visible_for_declared_cases(self):
        # Check the outer border, not just the detected inner corners.
        for seed in (5101, 5102, 6101, 6102):
            for r, t in peripheral_poses(seed):
                for family in FAMILIES:
                    uv = project(board_points(r, t, outer=True), K, family)
                    margin = np.minimum(uv, np.asarray(SIZE) - 1 - uv)
                    self.assertGreater(float(margin.min()), 10.)

    def test_equal_budget_reaches_periphery_without_visibility_filter(self):
        central = poses(4101)[:12]
        wide = central[:4] + peripheral_poses(5101)
        self.assertEqual(len(central), len(wide))
        for family in FAMILIES:
            a, b = coverage(central, family), coverage(wide, family)
            self.assertLess(a['theta_max_rad'], .7)
            self.assertGreater(b['theta_max_rad'], 1.)
            self.assertEqual(sum(b['counts']), 12 * 54)
            self.assertEqual(b['visible_corners'], b['total_corners'])
            self.assertGreater(b['counts'][-2], 0)

    def test_known_board_corner_coordinates(self):
        points = board_points(np.eye(3), np.array([0., 0., 2.]))
        self.assertEqual(points.shape, (54, 3))
        np.testing.assert_allclose(points[[0, -1]], [[-.32, -.20, 2.], [.32, .20, 2.]])
        border = board_points(np.eye(3), np.array([0., 0., 2.]), outer=True)
        np.testing.assert_allclose(border[[0, -1]], [[-.40, -.28, 2.], [.40, .28, 2.]])

    def test_native_detector_on_peripheral_nonzero_optics(self):
        family = FAMILIES[1]
        r, t = peripheral_poses(5101)[4]
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            Image.fromarray(image(r, t, optics=family)).save(folder / 'board.png')
            process = subprocess.run([BINARY, 'detect', '--image', str(folder / 'board.png'),
                                      '--output', str(folder / 'detection')],
                                     capture_output=True, text=True, timeout=30)
            self.assertEqual(process.returncode, 0, process.stderr)
            points = json.loads((folder / 'detection/detections.json').read_text())['points']
            error = detection_error([p['uv_px'] for p in points], corners(r, t, family))
            self.assertLess(error['rmse_px'], .5)


if __name__ == '__main__':
    unittest.main()
