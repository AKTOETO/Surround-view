"""Joint information matrix includes shared intrinsics and view-specific nuisance poses."""
from pathlib import Path
import sys
import unittest

import numpy as np
from scipy.spatial.transform import Rotation

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from calibration.joint_information import analyze_joint_fit, solve_joint
from calibration.optical_models import FAMILIES
from calibration.raster_board import BOARD, K, corners, poses


class JointInformationTests(unittest.TestCase):
    def setUp(self):
        geometry = poses(7311)[:12]
        self.poses = [(Rotation.from_matrix(r).as_rotvec(), t) for r, t in geometry]
        x, y = np.meshgrid(np.arange(9)*.08-.32, np.arange(6)*.08-.2)
        self.objects = np.stack([x.ravel(), y.ravel(), np.zeros(54)], axis=-1)
        self.observed = [corners(r, t) for r, t in geometry]
        self.estimate = {'fx': K[0, 0], 'fy': K[1, 1], 'cx': K[0, 2], 'cy': K[1, 2],
                         'k': [0., 0., 0., 0.]}

    def test_exact_joint_fit_builds_full_camera_and_pose_jacobian(self):
        fit = solve_joint(self.objects, self.observed, self.poses, self.estimate, 4)
        self.assertTrue(fit.success, fit.message)
        self.assertLess(np.sqrt(np.mean(fit.residuals**2)), 1e-5)
        self.assertEqual(fit.jacobian.shape, (12*54*2, 8+12*6))
        self.assertEqual(len(fit.parameter_names), fit.jacobian.shape[1])
        self.assertTrue(np.isfinite(fit.jacobian).all())
        report = analyze_joint_fit(fit, .1, 'synthetic_test')
        self.assertEqual(report['rows'], 1296)
        self.assertEqual(report['columns'], 80)
        self.assertIn(report['numerical_rank'], (79, 80))
        self.assertEqual(report['observation_source'], 'synthetic_test')
        self.assertGreater(len(report['singular_values']), 0)
        if report['uncertainty_status'] == 'local_linear_iid_assumption':
            covariance = np.asarray(report['intrinsics_covariance_by_sigma_source']['detector_truth_rms_iid']['covariance'])
            self.assertEqual(covariance.shape, (8, 8))
            self.assertTrue(np.isfinite(covariance).all())

    def test_exact_board_projection_is_recovered_when_pose_is_a_nuisance(self):
        fit = solve_joint(self.objects, self.observed, self.poses, self.estimate, 2)
        for key in ('fx', 'fy', 'cx', 'cy'):
            self.assertAlmostEqual(fit.estimate[key], self.estimate[key], delta=1e-3)
        self.assertEqual(fit.estimate['k'][2:], [0., 0.])
        self.assertLess(fit.estimate['k'][0], 1e-5)


if __name__ == '__main__':
    unittest.main()
