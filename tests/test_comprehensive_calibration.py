"""Unit tests for comprehensive calibration study, distortion models, and joint optimization."""
import json
from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "blender"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from comprehensive_calibration_study import (
    BrownConradyRadTan,
    KannalaBrandtFisheye,
    ScaramuzzaOmni,
    compare_distortion_model_families,
    generate_pattern_points,
    run_joint_bundle_adjustment,
)
from rig import configuration


class ComprehensiveCalibrationTests(unittest.TestCase):
    def setUp(self):
        self.config = configuration()
        self.cam = self.config["cameras"][0]

    def test_distortion_model_projections(self):
        # 3D point along optical axis
        center_pt = np.array([[0.0, 0.0, 5.0]])
        proj_kb = KannalaBrandtFisheye.project(center_pt, 128.0, 128.0, 199.5, 199.5, [0.0, 0.0, 0.0, 0.0])
        np.testing.assert_allclose(proj_kb[0], [199.5, 199.5], atol=1e-4)

        proj_rt = BrownConradyRadTan.project(center_pt, 128.0, 128.0, 199.5, 199.5, [0.0, 0.0, 0.0, 0.0, 0.0])
        np.testing.assert_allclose(proj_rt[0], [199.5, 199.5], atol=1e-4)

        proj_omni = ScaramuzzaOmni.project(center_pt, 1.0, 1.0, 199.5, 199.5, [128.0, 0.0, 0.0, 0.0])
        np.testing.assert_allclose(proj_omni[0], [199.5, 199.5], atol=1e-4)

    def test_distortion_family_comparison(self):
        res = compare_distortion_model_families()
        self.assertIn("kannala_brandt", res)
        self.assertIn("brown_conrady_radtan", res)
        self.assertIn("scaramuzza_omni", res)
        self.assertLess(res["kannala_brandt"]["rmse_px"], 0.01)
        self.assertLess(res["scaramuzza_omni"]["rmse_px"], 0.01)

    def test_pattern_points_generation(self):
        planar = generate_pattern_points("planar_checkerboard")
        self.assertEqual(planar.shape, (64, 3))
        np.testing.assert_allclose(planar[:, 2], 0.0)

        trihedron = generate_pattern_points("noncoplanar_trihedron")
        self.assertEqual(trihedron.shape, (64, 3))
        # Must have non-zero Z points
        self.assertTrue(np.any(trihedron[:, 2] > 0.0))

    def test_joint_optimization_convergence(self):
        truth_cam = self.cam
        initial_cam = json.loads(json.dumps(truth_cam))
        # Small initial perturbation
        T_init = np.asarray(initial_cam["T_camera_from_vehicle"])
        T_init[0, 3] += 0.05
        initial_cam["T_camera_from_vehicle"] = T_init.tolist()

        pts = generate_pattern_points("noncoplanar_trihedron") + np.array([3.0, 0.0, 0.0])
        T_true = np.asarray(truth_cam["T_camera_from_vehicle"])
        p_cam = pts @ T_true[:3, :3].T + T_true[:3, 3]
        uv = KannalaBrandtFisheye.project(
            p_cam,
            truth_cam["projection"]["fx"],
            truth_cam["projection"]["fy"],
            truth_cam["projection"]["cx"],
            truth_cam["projection"]["cy"],
            truth_cam["projection"]["k"],
        )

        res = run_joint_bundle_adjustment(pts, uv, initial_cam, truth_cam)
        self.assertTrue(res["success"])
        self.assertLess(res["center_error_m"], 0.01)
        self.assertLess(res["rotation_error_deg"], 0.1)
        self.assertLess(res["rmse_px"], 0.01)


if __name__ == "__main__":
    unittest.main()
