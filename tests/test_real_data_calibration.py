"""Legacy synthetic-jitter contract; no detector/calibration validation implied."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "blender"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from real_data_calibration_evaluation import (
    evaluate_detector_and_quality_gate,
    generate_synthetic_photographic_chessboard,
    run_evaluation,
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

    def test_legacy_jitter_is_labelled_and_seeded(self):
        res = evaluate_detector_and_quality_gate(self.cam, seed=42)
        self.assertFalse(res["detector_executed"])
        self.assertFalse(res["solver_executed"])
        self.assertEqual(res["threshold_status"], "illustrative_unvalidated")
        self.assertEqual(res, evaluate_detector_and_quality_gate(self.cam, seed=42))
        self.assertIn("detection_rate_pct", res)
        self.assertGreater(res["detection_rate_pct"], 80.0)
        self.assertIn("quality_gate_recommendation", res)
        self.assertIn("tier_1_optimal", res["quality_gate_recommendation"])
        self.assertIn("tier_3_rejection_recalibrate", res["quality_gate_recommendation"])
        with self.assertRaises(TypeError):
            evaluate_detector_and_quality_gate(self.cam, num_trials=5)

    def test_generated_report_discloses_model_scope(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = root / "config.json"
            config.write_text(json.dumps(self.config))
            run_evaluation(config, root / "result")
            report = (root / "result" / "REPORT.md").read_text()
            self.assertIn("Детектор и solver не выполняются", report)
            self.assertIn("illustrative_unvalidated", report)
            record = json.loads((root / "result" / "real_data_calibration_evaluation.json").read_text())
            self.assertFalse(record["detector_executed"])
            self.assertFalse(record["solver_executed"])


if __name__ == "__main__":
    unittest.main()
