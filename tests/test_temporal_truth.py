"""Negative controls for temporal residual and independent scene-edge metrics."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from temporal_seam_stability import (
    boundary_motion, evaluate_temporal_stability, independent_edge_error,
    load_sequence, seam_boundary,
)

FIXTURE = Path(__file__).resolve().parent/'data'/'paired_street_v1'


class TemporalTruthTests(unittest.TestCase):
    def setUp(self):
        self.rgb = np.zeros((3, 20, 24, 3), float)
        self.weights = np.zeros((3, 20, 24, 2))
        self.weights[:, :, :12, 0] = 1
        self.weights[:, :, 12:, 1] = 1
        self.valid = np.ones_like(self.weights, bool)
        self.mask = np.ones((3, 20, 24), bool)
        self.poses = np.tile(np.eye(4), (3, 1, 1))
        self.poses[:, 0, 3] = [0, .4, .8]

    def evaluate(self, frames, truth, **kwargs):
        args = dict(weights=self.weights, validity=self.valid, masks=self.mask,
                    timestamps_ns=[0, 200000000, 400000000], poses=self.poses)
        args.update(kwargs)
        return evaluate_temporal_stability(frames, truth, **args)

    def test_true_scene_changes_are_not_called_flicker(self):
        moving = self.rgb.copy()
        moving[1] = .2
        moving[2] = .4
        result = self.evaluate(moving, moving)
        for transition in result['transitions']:
            self.assertEqual(transition['residual_change_mae'], 0)
            self.assertAlmostEqual(transition['raw_change_mae'], .2)
            self.assertAlmostEqual(transition['translation_m'], .4)

    def test_injected_temporal_bias_is_detected(self):
        frames = self.rgb.copy()
        frames[1] = .1
        result = self.evaluate(frames, self.rgb)
        for transition in result['transitions']:
            self.assertAlmostEqual(transition['residual_change_mae'], .1)

    def test_hard_seam_exists_and_known_shift_is_measured(self):
        previous = seam_boundary(self.weights[0], self.valid[0])
        current = np.zeros_like(previous)
        current[:, 14:16] = True
        self.assertEqual(previous.sum(), 40)
        self.assertAlmostEqual(boundary_motion(previous, current)['mean_distance_px'], 2.5)
        self.assertEqual(boundary_motion(previous, previous)['mean_distance_px'], 0)

    def test_absent_seam_is_undefined(self):
        absent = np.zeros((20, 24), bool)
        self.assertIsNone(boundary_motion(absent, absent)['mean_distance_px'])
        self.assertIsNone(boundary_motion(absent, absent)['iou'])

    def test_empty_roi_is_undefined(self):
        result = self.evaluate(self.rgb, self.rgb, masks=np.zeros_like(self.mask))
        self.assertIsNone(result['transitions'][0]['residual_change_mae'])

    def test_timestamp_order_and_rgb_range_rejected(self):
        with self.assertRaises(ValueError):
            self.evaluate(self.rgb, self.rgb, timestamps_ns=[0, 0, 1])
        with self.assertRaises(ValueError):
            self.evaluate(self.rgb+255, self.rgb)

    def test_extra_contour_control(self):
        truth = np.zeros((20, 24, 3))
        truth[:, 5:10] = 1
        roi = np.ones(truth.shape[:2], bool)
        identity = independent_edge_error(truth, truth, roi)
        duplicated = truth.copy()
        duplicated[:, 16:20] = 1
        injected = independent_edge_error(duplicated, truth, roi)
        self.assertEqual(identity['extra_edge_pixels'], 0)
        self.assertGreater(injected['extra_edge_pixels'], 0)

    def test_checked_in_sequence_has_real_pose_changes(self):
        cfg, inputs, truth, masks, stamps, poses = load_sequence(FIXTURE, FIXTURE)
        self.assertEqual(len(inputs), 3)
        self.assertAlmostEqual(poses[2][0][3]-poses[0][0][3], .8)
        self.assertEqual(int(stamps[2])-int(stamps[0]), 400000000)
        self.assertGreater(np.abs(truth[1]-truth[0]).mean(), .001)
        self.assertTrue(all(mask.any() for mask in masks))

    def test_corruption_and_pose_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)/'dataset'
            shutil.copytree(FIXTURE, root)
            target = root/'paired_truth.json'
            payload = json.loads(target.read_text())
            payload['frames'][1]['T_world_from_vehicle'][0][3] += .1
            target.write_text(json.dumps(payload))
            with self.assertRaisesRegex(ValueError, 'poses disagree'):
                load_sequence(root, root)
            shutil.copyfile(FIXTURE/'paired_truth.json', target)
            (root/'virtual_0000.png').write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError, 'checksum'):
                load_sequence(root, root)


if __name__ == '__main__':
    unittest.main()
