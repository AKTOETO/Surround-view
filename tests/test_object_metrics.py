"""Clean/doubled/missing/displaced RGB controls and source-ID contract tests."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tools/research')]
from object_metrics import measure_target, projected_object_ids, target_mask
from object_stitch import load_objects


class ObjectMetricTests(unittest.TestCase):
    def setUp(self):
        self.truth = np.zeros((32,48), bool)
        self.truth[8:24,8:12] = True

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
            copy = Path(folder)/'fixture'
            shutil.copytree(root,copy)
            meta = json.loads((copy/'paired_truth.json').read_text())
            (copy/meta['frames'][0]['source_objects'][0]).write_bytes(b'corrupted')
            with self.assertRaisesRegex(ValueError, 'checksum'):
                load_objects(copy)


if __name__ == '__main__':
    unittest.main()
