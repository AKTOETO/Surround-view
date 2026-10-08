"""Unit tests for realistic photographic calibration and quality-gate thresholds."""
import json
from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "blender"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from real_data_calibration_evaluation import (
    evaluate_detector_and_quality_gate,
    generate_synthetic_photographic_chessboard,
)
from rig import configuration


class RealDataCalibrationTests(unittest.TestCase):
    def setUp(self):
        self.config = configuration()
        self.cam = self.config["cameras"][0]

    def test_photographic_chessboard_generation(self):
        img_u8, exact_uv, pts_veh = generate_synthetic_photographic_chessboard(
            board_size=(9, 6),
            square_size_m=0.08,
            image_size=(200, 200),
            camera_config=self.cam,
            noise_sigma=0.01,
            blur_sigma=0.5,
        )
        self.assertEqual(img_u8.shape, (200, 200))
        self.assertEqual(exact_uv.shape, (54, 2))
        self.assertEqual(pts_veh.shape, (54, 3))
        # Image must have non-trivial contrast
        self.assertGreater(float(np.max(img_u8) - np.min(img_u8)), 100.0)

    def test_detector_quality_gate_evaluation(self):
        res = evaluate_detector_and_quality_gate(self.cam, num_trials=5)
        self.assertIn("detection_rate_pct", res)
        self.assertGreater(res["detection_rate_pct"], 80.0)
        self.assertIn("quality_gate_recommendation", res)
        self.assertIn("tier_1_optimal", res["quality_gate_recommendation"])
        self.assertIn("tier_3_rejection_recalibrate", res["quality_gate_recommendation"])


if __name__ == "__main__":
    unittest.main()
