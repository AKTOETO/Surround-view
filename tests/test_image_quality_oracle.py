"""Unit tests for vehicle body mask, image quality oracle, and temporal seam stability."""
import json
from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "blender"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from body_mask import (
    compute_camera_self_occlusion,
    compute_vehicle_footprint_mask,
    ray_box_test,
    vehicle_body_boxes,
)
from image_quality_oracle import (
    compute_psnr,
    compute_ssim,
    evaluate_image_quality,
    render_scene_oracle,
)
from rig import configuration


class ImageQualityOracleTests(unittest.TestCase):
    def setUp(self):
        self.config = configuration()
        self.config["output"] = {"width": 128, "height": 72}
        self.config["surface"] = {
            "type": "dome_floor_v1",
            "dome_radius_m": 12.0,
            "dome_latitude_cells": 32,
            "dome_longitude_cells": 64,
            "floor_radial_cells": 16,
        }

    def test_vehicle_body_boxes_and_footprint_mask(self):
        boxes = vehicle_body_boxes()
        self.assertGreaterEqual(len(boxes), 4)
        
        # Test point at ground center inside vehicle
        inside_pt = np.array([[[0.0, 0.0, 0.0]]])
        mask_inside = compute_vehicle_footprint_mask(inside_pt)
        self.assertTrue(mask_inside[0, 0])
        
        # Test point far outside vehicle
        outside_pt = np.array([[[10.0, 5.0, 0.0]]])
        mask_outside = compute_vehicle_footprint_mask(outside_pt)
        self.assertFalse(mask_outside[0, 0])

    def test_ray_box_intersection(self):
        center = [0.0, 0.0, 1.0]
        half_extents = [1.0, 1.0, 0.5]
        
        # Ray hitting box directly
        origins = np.array([[[0.0, 0.0, 5.0]]])
        dirs = np.array([[[0.0, 0.0, -1.0]]])
        hit, t_enter = ray_box_test(origins, dirs, center, half_extents)
        self.assertTrue(hit[0, 0])
        self.assertAlmostEqual(float(t_enter[0, 0]), 3.5, delta=1e-4)

        # Ray missing box
        dirs_miss = np.array([[[1.0, 0.0, 0.0]]])
        hit_m, _ = ray_box_test(origins, dirs_miss, center, half_extents)
        self.assertFalse(hit_m[0, 0])

    def test_camera_self_occlusion(self):
        cam_front = self.config["cameras"][0]
        # Point in front of front camera (visible)
        front_pt = np.array([[[5.0, 0.0, 0.0]]])
        vis_front = compute_camera_self_occlusion(cam_front, front_pt)
        self.assertTrue(vis_front[0, 0])
        
        # Point behind the vehicle center (blocked by chassis)
        rear_pt = np.array([[[-5.0, 0.0, 0.0]]])
        vis_rear = compute_camera_self_occlusion(cam_front, rear_pt)
        self.assertFalse(vis_rear[0, 0])

    def test_scene_oracle_render_and_metrics(self):
        oracle = render_scene_oracle(self.config, width=128, height=72)
        self.assertEqual(oracle["rgb"].shape, (72, 128, 3))
        self.assertEqual(oracle["depth"].shape, (72, 128))
        self.assertEqual(oracle["semantics"].shape, (72, 128))
        
        # Ground points must have semantic class 1 or 2 or 7
        is_ground = oracle["semantics"] == 1
        self.assertTrue(is_ground.any())
        
        # Evaluate identical image gives PSNR 100 dB and SSIM 1.0
        psnr_id = compute_psnr(oracle["rgb"], oracle["rgb"])
        self.assertGreaterEqual(psnr_id, 99.0)
        ssim_id = compute_ssim(oracle["rgb"], oracle["rgb"])
        self.assertAlmostEqual(ssim_id, 1.0, delta=1e-3)
        
        # Evaluate corrupted image gives valid lower metrics
        noisy = np.clip(oracle["rgb"] + 0.1, 0.0, 1.0)
        eval_res = evaluate_image_quality(noisy, oracle)
        self.assertIn("overall", eval_res)
        self.assertIn("ground_plane", eval_res)
        self.assertIn("vertical_obstacles", eval_res)
        self.assertGreater(eval_res["overall"]["psnr_db"], 10.0)
        self.assertLess(eval_res["overall"]["psnr_db"], 40.0)

if __name__ == "__main__":
    unittest.main()
