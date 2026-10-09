"""Input provenance and shape checks for the E-STITCH-01 runner."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "blender"))
sys.path.insert(0, str(ROOT / "tools"))

from run_e_stitch_01 import load_experiment_inputs


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class EStitchInputTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.dataset = self.root / "dataset"
        self.dataset.mkdir()
        self.config_path = self.root / "config.json"
        self.config_path.write_text(json.dumps({
            "cameras": [
                {
                    "id": camera_id,
                    "calibration_id": f"calibration-{camera_id}",
                    "resolution": {"width": 8, "height": 6},
                }
                for camera_id in range(2)
            ]
        }))

        rgb_paths = []
        rgb_hashes = {}
        depth_paths = []
        depth_hashes = {}
        for camera_id in range(2):
            rgb_name = f"camera{camera_id}_0000.ppm"
            rgb_path = self.dataset / rgb_name
            Image.new("RGB", (8, 6), (camera_id * 80, 10, 20)).save(rgb_path)
            rgb_paths.append(rgb_name)
            rgb_hashes[rgb_name] = sha256(rgb_path)

            depth_name = f"depth_camera{camera_id}_0000.npy"
            depth_path = self.dataset / depth_name
            with depth_path.open("wb") as output:
                np.save(output, np.full((6, 8), 4.0, dtype=np.float32))
            depth_paths.append(depth_name)
            depth_hashes[depth_name] = sha256(depth_path)

        (self.dataset / "manifest.json").write_text(json.dumps({
            "schema_version": 1,
            "calibration_ids": ["calibration-0", "calibration-1"],
            "frames": [{"paths": rgb_paths}],
            "sha256": rgb_hashes,
        }))
        (self.dataset / "ground_truth.json").write_text(json.dumps({
            "depth_truth": {
                "frames": [{"files": depth_paths}],
                "sha256": depth_hashes,
            }
        }))

    def tearDown(self):
        self.temp.cleanup()

    def test_loads_synchronous_camera_inputs_and_records_provenance(self):
        config, images, depths, provenance = load_experiment_inputs(
            self.config_path, self.dataset
        )

        self.assertEqual(len(config["cameras"]), 2)
        self.assertEqual([image.shape for image in images], [(6, 8, 3)] * 2)
        self.assertEqual([depth.shape for depth in depths], [(6, 8)] * 2)
        self.assertEqual(len(provenance["implementation_sha256"]), 4)

    def test_rejects_rgb_changed_after_manifest_was_written(self):
        with (self.dataset / "camera0_0000.ppm").open("ab") as output:
            output.write(b"corruption")

        with self.assertRaisesRegex(ValueError, "RGB checksum"):
            load_experiment_inputs(self.config_path, self.dataset)

    def test_rejects_camera_order_that_does_not_match_config(self):
        manifest_path = self.dataset / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["frames"][0]["paths"].reverse()
        manifest_path.write_text(json.dumps(manifest))

        with self.assertRaisesRegex(ValueError, "RGB camera order"):
            load_experiment_inputs(self.config_path, self.dataset)

    def test_rejects_unsynchronized_input_frame(self):
        manifest_path = self.dataset / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["frames"][0]["offset_ns"] = [0, 1]
        manifest_path.write_text(json.dumps(manifest))

        with self.assertRaisesRegex(ValueError, "synchronized"):
            load_experiment_inputs(self.config_path, self.dataset)

    def test_rejects_manifest_calibration_mismatch(self):
        manifest_path = self.dataset / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["calibration_ids"][1] = "wrong-calibration"
        manifest_path.write_text(json.dumps(manifest))

        with self.assertRaisesRegex(ValueError, "calibration IDs"):
            load_experiment_inputs(self.config_path, self.dataset)


if __name__ == "__main__":
    unittest.main()
