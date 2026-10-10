"""Unit tests for E-STITCH-01 fusion baselines, carrier geometry, and stitch metrics."""
import json
from pathlib import Path
import sys
import unittest

import numpy as np
from scipy import ndimage as ndi

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

    def test_graph_cut_produces_binary_valid_labels(self):
        weights = compute_graph_cut_seam_mask(self.colors, self.validity)
        self.assertEqual(weights.shape, (self.H, self.W, 4))
        has_cov = np.sum(self.validity, axis=-1) > 0
        self.assertTrue(np.all(np.isin(weights[has_cov], [0.0, 1.0])))
        np.testing.assert_allclose(np.sum(weights[has_cov], axis=-1), 1.0)

    def test_graph_cut_places_seam_in_low_source_disagreement_corridor(self):
        height, width = 32, 48
        validity = np.zeros((height, width, 2), dtype=bool)
        validity[:, :, 0] = np.arange(width)[None, :] <= 31
        validity[:, :, 1] = np.arange(width)[None, :] >= 10
        colors = np.zeros((height, width, 2, 3), dtype=np.float32)
        colors[:, :, 1, :] = 0.8
        colors[:, 22:24, 1, :] = 0.01

        weights = compute_graph_cut_seam_mask(colors, validity)
        labels = np.argmax(weights, axis=-1)
        seam_x = []
        for y in range(4, height - 4):
            row = labels[y, 10:32]
            transitions = np.flatnonzero(row[1:] != row[:-1])
            seam_x.extend((transitions + 10).tolist())

        self.assertEqual(len(seam_x), height - 8)
        self.assertLessEqual(abs(float(np.mean(seam_x)) - 22.5), 2.0)

    def test_binary_graph_cut_matches_exhaustive_small_problem_minimum_energy(self):
        """The cut must minimize the documented integer Potts energy on a small overlap."""
        height, width = 4, 5
        validity = np.zeros((height, width, 2), dtype=bool)
        validity[:, :4, 0] = True
        validity[:, 1:, 1] = True
        colors = np.random.default_rng(813).random(
            (height, width, 2, 3), dtype=np.float32
        )
        smoothness = 0.1
        mask = validity[..., 0] & validity[..., 1]
        coords = np.argwhere(mask)
        node_at = np.full((height, width), -1, dtype=np.int32)
        node_at[mask] = np.arange(len(coords))

        distances = np.stack(
            [ndi.distance_transform_edt(validity[..., i]) for i in range(2)], axis=-1
        )
        local_distances = distances[mask]
        distance_sum = local_distances.sum(axis=-1) + 1e-6
        unary = np.stack([
            np.rint((1.0 - local_distances[:, i] / distance_sum) * 1000).astype(int)
            for i in range(2)
        ], axis=-1)

        disagreement = np.linalg.norm(colors[..., 0, :] - colors[..., 1, :], axis=-1)
        scale = float(np.percentile(disagreement[mask], 95))
        normalized = np.clip(disagreement / max(scale, 1e-6), 0.0, 1.0)
        edges = []
        for y, x in coords:
            for dy, dx in ((0, 1), (1, 0)):
                yy, xx = y + dy, x + dx
                if yy >= height or xx >= width or not mask[yy, xx]:
                    continue
                a, b = int(node_at[y, x]), int(node_at[yy, xx])
                difference = 0.5 * (normalized[y, x] + normalized[yy, xx])
                capacity = max(1, int(np.rint(smoothness * (0.05 + difference) * 1000)))
                edges.append((a, b, capacity))

        def energy(labels):
            return int(sum(unary[node, label] for node, label in enumerate(labels))
                       + sum(capacity for a, b, capacity in edges if labels[a] != labels[b]))

        selected = np.argmax(compute_graph_cut_seam_mask(
            colors, validity, smoothness_weight=smoothness
        ), axis=-1)[mask]
        exhaustive_minimum = min(
            energy(labels) for labels in np.ndindex(*(2,) * len(coords))
        )
        self.assertEqual(energy(selected), exhaustive_minimum)

    def test_three_and_four_camera_overlap_use_centrality_fallback(self):
        """3+/camera pixels use centrality labels; no multi-label cut is implied."""
        height = width = 11
        for camera_count in (3, 4):
            validity = np.zeros((height, width, camera_count), dtype=bool)
            validity[1:10, 1:8, 0] = True
            validity[1:10, 3:10, 1] = True
            validity[2:9, 2:9, 2] = True
            if camera_count == 4:
                validity[1:10, 1:10, 3] = True
            colors = np.random.default_rng(camera_count).random(
                (height, width, camera_count, 3), dtype=np.float32
            )

            distances = np.stack([
                ndi.distance_transform_edt(validity[..., i])
                for i in range(camera_count)
            ], axis=-1)
            centrality_winner = np.argmax(np.where(validity, distances, -1.0), axis=-1)
            fallback = validity.sum(axis=-1) >= 3
            labels = np.argmax(compute_graph_cut_seam_mask(colors, validity), axis=-1)

            self.assertTrue(fallback.any())
            np.testing.assert_array_equal(labels[fallback], centrality_winner[fallback])
            self.assertEqual(int(centrality_winner[5, 5]), camera_count - 1)

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

    def test_seam_metric_measures_output_color_step_at_label_boundary(self):
        height, width = 40, 64
        validity = np.ones((height, width, 2), dtype=bool)
        weights = np.zeros((height, width, 2), dtype=np.float32)
        weights[:, :32, 0] = 1.0
        weights[:, 32:, 1] = 1.0
        fused = np.zeros((height, width, 3), dtype=np.uint8)
        fused[:, 32:, :] = 255

        metrics = compute_seam_metrics(fused, weights, validity)

        self.assertEqual(metrics["seam_pixels"], height)
        self.assertGreater(metrics["p95_delta_e"], 90.0)

    def test_ghost_metric_requires_both_separated_edges_in_fused_output(self):
        height, width = 40, 64
        camera_a = np.zeros((height, width, 3), dtype=np.float32)
        camera_b = np.zeros_like(camera_a)
        camera_a[:, 30:, :] = 1.0
        camera_b[:, 34:, :] = 1.0
        camera_samples = np.stack([camera_a, camera_b], axis=2)
        validity = np.ones((height, width, 2), dtype=bool)

        blended = np.rint(127.5 * (camera_a + camera_b)).astype(np.uint8)
        single_source = np.rint(255.0 * camera_a).astype(np.uint8)
        blended_metrics = compute_ghost_contours(blended, camera_samples, validity)
        single_metrics = compute_ghost_contours(single_source, camera_samples, validity)

        self.assertGreater(blended_metrics["ghost_pixel_count"], 0)
        self.assertEqual(single_metrics["ghost_pixel_count"], 0)
        self.assertGreater(
            blended_metrics["ghost_fraction_of_overlap"],
            single_metrics["ghost_fraction_of_overlap"],
        )


if __name__ == "__main__":
    unittest.main()
