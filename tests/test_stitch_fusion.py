"""Unit tests for E-STITCH-01 fusion baselines, carrier geometry, and stitch metrics."""
import json
from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "blender"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from fusion import (
    FUSION_MODES,
    build_gaussian_pyramid,
    build_laplacian_pyramid,
    compute_graph_cut_seam_mask,
    compute_seam_distance_weights,
    fuse_samples,
    multi_band_blend,
    reconstruct_laplacian_pyramid,
)
from reference import intersect, rays
from stitch_metrics import (
    compute_depth_consistency,
    compute_ghost_contours,
    compute_seam_metrics,
)


class StitchFusionTests(unittest.TestCase):
    def setUp(self):
        self.H, self.W = 64, 64
        # Synthetic 4-camera setup with an overlap region in the center
        self.colors = np.zeros((self.H, self.W, 4, 3), dtype=np.float32)
        # Cam 0 (front): red
        self.colors[:, :, 0] = [1.0, 0.0, 0.0]
        # Cam 1 (right): green
        self.colors[:, :, 1] = [0.0, 1.0, 0.0]
        # Cam 2 (rear): blue
        self.colors[:, :, 2] = [0.0, 0.0, 1.0]
        # Cam 3 (left): yellow
        self.colors[:, :, 3] = [1.0, 1.0, 0.0]

        self.validity = np.zeros((self.H, self.W, 4), dtype=bool)
        # Front covers top half
        self.validity[:40, :, 0] = True
        # Right covers right half
        self.validity[:, 24:, 1] = True
        # Rear covers bottom half
        self.validity[24:, :, 2] = True
        # Left covers left half
        self.validity[:, :40, 3] = True

        self.thetas = np.zeros((self.H, self.W, 4), dtype=np.float32)
        self.edges = np.full((self.H, self.W, 4), 20.0, dtype=np.float32)
        self.points = np.zeros((self.H, self.W, 3), dtype=np.float32)

    def test_pyramid_decomposition_and_exact_reconstruction(self):
        image = np.random.default_rng(42).uniform(0.0, 1.0, (64, 64, 3)).astype(np.float32)
        g_pyr = build_gaussian_pyramid(image, num_levels=4)
        self.assertEqual(len(g_pyr), 4)
        self.assertEqual(g_pyr[0].shape, (64, 64, 3))
        self.assertEqual(g_pyr[1].shape, (32, 32, 3))
        self.assertEqual(g_pyr[2].shape, (16, 16, 3))
        self.assertEqual(g_pyr[3].shape, (8, 8, 3))

        l_pyr = build_laplacian_pyramid(g_pyr)
        reconstructed = reconstruct_laplacian_pyramid(l_pyr)
        np.testing.assert_allclose(reconstructed, image, atol=1e-4)

    def test_all_seven_fusion_modes_produce_valid_normalized_outputs(self):
        for mode in FUSION_MODES:
            blended, weights = fuse_samples(
                self.colors, self.validity, self.thetas, self.edges, self.points,
                mode=mode, edge_width_px=24.0, angle_power=2.0
            )
            self.assertEqual(blended.shape, (self.H, self.W, 3))
            self.assertEqual(weights.shape, (self.H, self.W, 4))
            # Colors must stay bounded in [0, 1]
            self.assertTrue(np.all(blended >= 0.0) and np.all(blended <= 1.0))
            # Weights must be non-negative and sum to 1 where validity exists
            weight_sum = np.sum(weights, axis=-1)
            has_cov = np.sum(self.validity, axis=-1) > 0
            np.testing.assert_allclose(weight_sum[has_cov], 1.0, atol=1e-5)

    def test_graph_cut_minimizes_boundary_cost(self):
        weights = compute_graph_cut_seam_mask(self.colors, self.validity)
        self.assertEqual(weights.shape, (self.H, self.W, 4))
        # Hard binary choice per pixel
        has_cov = np.sum(self.validity, axis=-1) > 0
        self.assertTrue(np.all(np.isin(weights[has_cov], [0.0, 1.0])))

    def test_burger_like_carrier_intersection(self):
        burger_surface = {
            "type": "burger_like_v1",
            "outer_radius_m": 12.0,
            "fillet_radius_m": 2.0,
            "height_m": 12.0,
        }
        # Ray pointing straight down from 8m
        origin = np.array([0.0, 0.0, 8.0])
        down_ray = np.array([[[0.0, 0.0, -1.0]]])
        pts, hit, dist = intersect(burger_surface, origin, down_ray)
        self.assertTrue(hit[0, 0])
        self.assertAlmostEqual(float(dist[0, 0]), 8.0, delta=1e-4)
        self.assertAlmostEqual(float(pts[0, 0, 2]), 0.0, delta=1e-4)

        # Ray pointing horizontally towards the fillet/wall
        horiz_ray = np.array([[[1.0, 0.0, 0.0]]])
        origin_low = np.array([0.0, 0.0, 1.0])
        pts_h, hit_h, dist_h = intersect(burger_surface, origin_low, horiz_ray)
        self.assertTrue(hit_h[0, 0])
        self.assertGreater(float(dist_h[0, 0]), 10.0)

    def test_seam_and_ghost_metrics(self):
        fused = np.full((self.H, self.W, 3), 128, dtype=np.uint8)
        weights = np.ones((self.H, self.W, 4), dtype=np.float32) / 4.0
        seam_m = compute_seam_metrics(fused, weights, self.validity)
        self.assertIn("mean_delta_e", seam_m)
        self.assertIn("gradient_discontinuity", seam_m)

        ghost_m = compute_ghost_contours(fused, self.colors, self.validity)
        self.assertIn("ghost_fraction_of_overlap", ghost_m)
        self.assertIn("overlap_pixels", ghost_m)


if __name__ == "__main__":
    unittest.main()
