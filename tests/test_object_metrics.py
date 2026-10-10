"""Clean/doubled/missing/displaced RGB controls and source-ID contract tests."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tools/research'), str(ROOT/'tools/blender')]
from object_metrics import measure_target, measure_target_support, projected_object_ids, target_mask
from object_stitch import interleaved_orders, load_objects
from diagnostic_motion import frame_positions, position_for_capture, validate_captured_positions


class ObjectMetricTests(unittest.TestCase):
    def setUp(self):
        self.truth = np.zeros((32,48), bool)
        self.truth[8:24,8:12] = True

    def test_interleaved_timing_schedule_is_complete_reproducible_and_varied(self):
        keys = [f'case-{index}' for index in range(24)]
        first = interleaved_orders(keys, 7, seed=20261010)
        second = interleaved_orders(keys, 7, seed=20261010)
        self.assertEqual(first, second)
        self.assertEqual(len(first), 7)
        self.assertTrue(all(len(order) == len(keys) and set(order) == set(keys) for order in first))
        self.assertGreater(len({tuple(order) for order in first}), 1)
        with self.assertRaises(ValueError):
            interleaved_orders(keys + [keys[0]], 3)

    def test_source_support_known_answers_and_merged_copy(self):
        support = self.truth.astype(float)
        exact = measure_target_support(support, self.truth)
        self.assertEqual(exact['mean_support_inside_truth'], 1)
        self.assertEqual(exact['outside_support_fraction'], 0)
        self.assertEqual(exact['support_mass_over_truth_area'], 1)
        self.assertEqual(exact['thresholds']['0.5']['iou'], 1)

        # A touching extra copy can form a single component while source IDs
        # still reveal that half of the target support lies outside direct truth.
        doubled = support.copy()
        doubled[8:24, 12:16] = 1
        result = measure_target_support(doubled, self.truth)
        self.assertEqual(result['outside_support_fraction'], .5)
        self.assertEqual(result['support_mass_over_truth_area'], 2)
        self.assertEqual(result['thresholds']['0.5']['components'], 1)
        self.assertEqual(result['thresholds']['0.5']['extra_components'], 0)
        self.assertEqual(result['thresholds']['0.5']['iou'], .5)

    def test_empty_support_and_invalid_support(self):
        result = measure_target_support(np.zeros_like(self.truth, dtype=float), self.truth)
        self.assertIsNone(result['outside_support_fraction'])
        self.assertEqual(result['support_mass_over_truth_area'], 0)
        self.assertEqual(result['thresholds']['0.5']['recall'], 0)
        with self.assertRaises(ValueError):
            measure_target_support(np.full_like(self.truth, 1.1, dtype=float), self.truth)
        with self.assertRaises(ValueError):
            measure_target_support(np.zeros_like(self.truth, dtype=float), np.zeros_like(self.truth))

    def test_frozen_motion_plan_maps_positions_to_capture_frames(self):
        target = json.loads((ROOT/'assets/scenarios/object-stitch-motion-v1.json').read_text())['target']
        capture = json.loads((ROOT/'assets/scenarios/object-stitch-motion-v1.json').read_text())['capture']
        keyed = frame_positions(target, capture)
        self.assertEqual([frame for frame, _ in keyed], [1, 4, 7, 10, 13, 16, 19, 22, 25])
        self.assertEqual(keyed[0][1], (4.0, -1.2, .9))
        self.assertEqual(keyed[-1][1], (4.0, 1.2, .9))
        self.assertEqual(position_for_capture(target, capture, 4), (4., 0., .9))
        validate_captured_positions(target, [
            {'diagnostic_target_position_m':list(position)} for _, position in keyed])
        with self.assertRaisesRegex(ValueError, 'differs'):
            validate_captured_positions(target, [
                {'diagnostic_target_position_m':list(position)} for _, position in keyed[:-1]]
                + [{'diagnostic_target_position_m':[99, 99, 99]}])
        with self.assertRaises(ValueError):
            frame_positions(target, {'frames':8,'frame_step':3})

    def test_clean_rgb(self):
        rgb = np.full((32,48,3), .3)
        rgb[self.truth] = [1,0,1]
        result = measure_target(target_mask(rgb), self.truth)
        self.assertEqual(result['iou'], 1)
        self.assertEqual(result['extra_components'], 0)
        self.assertEqual(result['extra_run_rows'], 0)
        self.assertEqual(result['recall'], 1)

    def test_doubled_target_is_detected_in_rgb(self):
        rgb = np.zeros((32,48,3), np.uint8)
        rgb[self.truth] = [255,0,255]
        rgb[8:24,30:34] = [255,0,255]
        result = measure_target(target_mask(rgb), self.truth)
        self.assertEqual(result['extra_components'], 1)
        self.assertEqual(result['extra_run_row_fraction'], 1)
        self.assertEqual(result['iou'], .5)

    def test_missing_target(self):
        result = measure_target(np.zeros_like(self.truth), self.truth)
        self.assertEqual(result['recall'], 0)
        self.assertEqual(result['components'], 0)
        self.assertEqual(result['missing_rows'], 16)

    def test_displaced_single_object_is_not_called_double(self):
        moved = np.roll(self.truth, 20, axis=1)
        result = measure_target(moved, self.truth)
        self.assertEqual(result['iou'], 0)
        self.assertEqual(result['extra_components'], 0)
        self.assertEqual(result['extra_run_rows'], 0)

    def test_joined_copies_expose_count_limitation(self):
        joined = self.truth.copy()
        joined[8:24,8:34] = True
        result = measure_target(joined, self.truth)
        self.assertEqual(result['components'], 1)
        self.assertEqual(result['extra_run_rows'], 0)
        self.assertLess(result['iou'], .2)

    def test_empty_truth_is_undefined_and_small_noise_removed(self):
        empty = np.zeros_like(self.truth)
        self.assertIsNone(measure_target(empty, empty)['iou'])
        noise = empty.copy()
        noise[0,:7] = True
        self.assertEqual(measure_target(noise, self.truth)['components'], 0)
        self.assertFalse(target_mask(np.ones((5,5,3))).any())

    def test_discrete_projection_and_invalid_points(self):
        camera = {'T_camera_from_vehicle':np.eye(4).tolist(),
                  'projection':{'fx':2.,'fy':2.,'cx':3.,'cy':3.,'k':[0.]*4,
                                'theta_max_rad':1.4,'z_epsilon_m':1e-6}}
        labels = np.zeros((7,7),np.uint16)
        labels[3,3] = 42
        points = np.array([[[0.,0.,5.],[0.,0.,-1.],[np.nan,0,0]]])
        np.testing.assert_array_equal(projected_object_ids(camera, points, labels), [[42,0,0]])
        with self.assertRaises(ValueError):
            projected_object_ids(camera, points, labels.astype(float))

    def test_checked_in_source_ids_and_checksum_rejection(self):
        root = ROOT/'tests/data/object_stitch_v1'
        cfg, images, truths, source, masks, target, object_id = load_objects(root)
        self.assertEqual(len(source), 2)
        self.assertGreaterEqual(sum((ids == object_id).any() for ids in source[0]), 2)
        for rgb, mask in zip(truths, masks):
            self.assertGreaterEqual(measure_target(target_mask(rgb),mask)['iou'], .75)
        with tempfile.TemporaryDirectory() as folder:
            separated = Path(folder)
            dataset, capture = separated/'dataset', separated/'capture'
            dataset.mkdir()
            capture.mkdir()
            for path in root.iterdir():
                if path.name.startswith(('camera',)) or path.name in ('config.json','ground_truth.json','manifest.json','nominal-config.json'):
                    shutil.copy2(path, dataset/path.name)
                elif path.name.startswith(('objects_', 'virtual_', 'visibility_', 'source_objects_')) or path.name in ('capture.json','paired_truth.json'):
                    shutil.copy2(path, capture/path.name)
            split = load_objects(dataset, capture)
            self.assertEqual(split[-1], object_id)
            self.assertEqual(len(split[1]), len(images))
            np.testing.assert_array_equal(split[4][0], masks[0])

        with tempfile.TemporaryDirectory() as folder:
            copy = Path(folder)/'fixture'
            shutil.copytree(root,copy)
            meta = json.loads((copy/'paired_truth.json').read_text())
            (copy/meta['frames'][0]['source_objects'][0]).write_bytes(b'corrupted')
            with self.assertRaisesRegex(ValueError, 'checksum'):
                load_objects(copy)


if __name__ == '__main__':
    unittest.main()
