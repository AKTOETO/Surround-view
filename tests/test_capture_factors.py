"""Independent controls for paired distance/tilt geometry and raster localization."""
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
from calibration.capture_factors import PROFILES, diagnostics, factor_poses
from calibration.coverage import board_points
from calibration.optical_models import FAMILIES, project
from calibration.raster_board import K, SIZE, corners, detection_error, image

BINARY = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / 'build/sv-calibrate')
sys.argv = sys.argv[:1]


class CaptureFactorTests(unittest.TestCase):
    def test_factors_preserve_center_directions_and_roll_basis(self):
        base = factor_poses(8101, 3.2, 0.)
        for parameters in PROFILES.values():
            for (r, t), (br, bt) in zip(factor_poses(8101, **parameters), base):
                np.testing.assert_allclose(t / np.linalg.norm(t), bt / np.linalg.norm(bt), atol=1e-14)
                self.assertAlmostEqual(np.linalg.norm(t), parameters['distance_m'])
                np.testing.assert_allclose(r.T @ r, np.eye(3), atol=1e-14)
                self.assertAlmostEqual(np.linalg.det(r), 1.)
                # For local Rx(a) Ry(b), normal.dot(original normal)=cos(a)cos(b).
                self.assertAlmostEqual(r[:, 2] @ br[:, 2], np.cos(parameters['tilt_rad'])**2)

    def test_distance_alone_does_not_change_rotation(self):
        for tilt in (0., .35):
            for (a, _), (b, _) in zip(factor_poses(8101, 2., tilt), factor_poses(8101, 3.2, tilt)):
                np.testing.assert_array_equal(a, b)

    def test_all_declared_borders_visible_and_validation_disjoint(self):
        for seed in (7101, 7102):
            validation = factor_poses(seed + 2000, 2.6, .175)
            val = {tuple(np.r_[r.ravel(), t]) for r, t in validation}
            for parameters in PROFILES.values():
                train = factor_poses(seed + 1000, **parameters)
                self.assertFalse(val & {tuple(np.r_[r.ravel(), t]) for r, t in train})
                for r, t in train + validation:
                    self.assertTrue(np.all(board_points(r, t, outer=True)[:, 2] > 0))
                    for family in FAMILIES:
                        uv = project(board_points(r, t, outer=True), K, family)
                        self.assertGreater(float(np.minimum(uv, np.array(SIZE)-1-uv).min()), 10.)

    def test_projected_size_and_analytic_normal_tilt(self):
        for family in FAMILIES:
            small = diagnostics(factor_poses(8101, 3.2, 0.), family)
            large = diagnostics(factor_poses(8101, 2., 0.), family)
            tilted = diagnostics(factor_poses(8101, 2., .35), family)
            self.assertGreater(large['cell_edge_px']['median'], small['cell_edge_px']['median'] * 1.5)
            self.assertLess(small['normal_to_center_ray_rad']['max'], 3e-8)
            self.assertAlmostEqual(tilted['normal_to_center_ray_rad']['min'], np.arccos(np.cos(.35)**2))

    def test_invalid_factors(self):
        for distance, tilt in ((0., 0.), (-1., 0.), (np.nan, 0.), (2., np.nan), (2., .6)):
            with self.assertRaises(ValueError):
                factor_poses(8101, distance, tilt)

    def test_native_detector_on_large_tilted_peripheral_board(self):
        r, t = factor_poses(8101, 2., .35)[4]
        family = FAMILIES[1]
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            Image.fromarray(image(r, t, optics=family)).save(folder / 'board.png')
            process = subprocess.run([BINARY, 'detect', '--image', str(folder / 'board.png'),
                                      '--output', str(folder / 'detected')],
                                     capture_output=True, text=True, timeout=30)
            self.assertEqual(process.returncode, 0, process.stderr)
            points = json.loads((folder / 'detected/detections.json').read_text())['points']
            self.assertLess(detection_error([p['uv_px'] for p in points], corners(r, t, family))['rmse_px'], .5)


if __name__ == '__main__':
    unittest.main()
