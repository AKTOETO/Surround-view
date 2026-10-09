"""Check real OpenCV image detection and calibration through the installed CLI interfaces."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

CALIBRATOR, FIXTURE, CAPTURE = sys.argv[1:4]
sys.argv = sys.argv[:1]


class VisionToolsTest(unittest.TestCase):
    def test_image_calibration_and_duplicate_rejection(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            subprocess.run([FIXTURE, str(root / "input")], check=True)
            command = [CAPTURE, "--config", str(Path(__file__).resolve().parents[1] / "configs" / "synthetic.json"),
                       "--output", str(root / "capture"), "--frames", "3"]
            for camera in range(4):
                command.extend([f"--source{camera}", str(root / "input" / f"source-{camera}.avi")])
            subprocess.run(command, check=True, capture_output=True)
            captured = json.loads((root / "capture" / "manifest.json").read_text())
            self.assertEqual(len(captured["frames"]), 3)
            self.assertEqual(len(captured["sha256"]), 12)
            self.assertEqual(captured["origin"], "opencv_video_capture")
            dataset = root / "input" / "dataset.json"
            detection = subprocess.run(
                [CALIBRATOR, "detect", "--image", str(root / "input" / "board-0.png"),
                 "--columns", "9", "--rows", "6", "--output", str(root / "detected")],
                check=True, capture_output=True, text=True)
            self.assertIn("detected board", detection.stdout)
            points = json.loads((root / "detected" / "detections.json").read_text())
            self.assertEqual(len(points["points"]), 54)
            result = subprocess.run(
                [CALIBRATOR, "intrinsics", "--dataset", str(dataset), "--output", str(root / "calibrated")],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            model = json.loads((root / "calibrated" / "intrinsics.json").read_text())
            self.assertLess(abs(model["fx"] / 620 - 1), .05)
            self.assertLess(abs(model["fy"] / 610 - 1), .05)
            report = json.loads((root / "calibrated" / "report.json").read_text())
            self.assertEqual(report["status"], "accepted")
            self.assertLess(report["validation_error_px"]["p95"], 1)
            reduced = subprocess.run(
                [CALIBRATOR, "intrinsics", "--dataset", str(dataset), "--output", str(root / "reduced"),
                 "--distortion-order", "2"], capture_output=True, text=True)
            self.assertEqual(reduced.returncode, 0, reduced.stderr)
            reduced_model = json.loads((root / "reduced" / "intrinsics.json").read_text())
            self.assertEqual(reduced_model["k"][2:], [0., 0.])
            self.assertEqual(json.loads((root / "reduced" / "report.json").read_text())["distortion_order"], 2)
            invalid_order = subprocess.run(
                [CALIBRATOR, "intrinsics", "--dataset", str(dataset), "--output", str(root / "bad-order"),
                 "--distortion-order", "2garbage"], capture_output=True, text=True)
            self.assertNotEqual(invalid_order.returncode, 0)
            self.assertIn("distortion order must be 2 or 4", invalid_order.stderr)
            self.assertFalse((root / "bad-order" / "intrinsics.json").exists())
            description = json.loads(dataset.read_text())
            description["validation"][0] = description["train"][0]
            dataset.write_text(json.dumps(description))
            rejected = subprocess.run(
                [CALIBRATOR, "intrinsics", "--dataset", str(dataset), "--output", str(root / "duplicate")],
                capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("duplicate image", rejected.stderr)
            self.assertFalse((root / "duplicate" / "intrinsics.json").exists())


if __name__ == "__main__":
    unittest.main()
