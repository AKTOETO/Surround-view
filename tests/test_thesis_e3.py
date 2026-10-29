"""Scientific fixture invariants; no live Blender, server, or historical artifacts required."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'docs/diploma'))
from plot_thesis_e3 import evaluation_mask, input_guard, checked


class E3FixtureTests(unittest.TestCase):
    def test_roi_excludes_ego_invisible_background_and_two_pixel_margin(self):
        labels = np.ones((20, 20), np.uint16)
        bits = np.full((20, 20), 1, np.uint8)
        labels[10, 10] = 2
        labels[5, 5] = 0
        bits[15, 15] = 0
        roi = evaluation_mask(labels, bits, [2])
        self.assertFalse(roi[:2].any())
        self.assertFalse(roi[-2:].any())
        self.assertFalse(roi[:, :2].any())
        self.assertFalse(roi[:, -2:].any())
        for x, y in ((10, 10), (5, 5), (15, 15)):
            self.assertFalse(roi[y-2:y+3, x-2:x+3].any())
        self.assertTrue(roi[10, 6])
        # Object identity changes alone do not remove otherwise eligible pixels.
        labels[10, 6] = 3
        np.testing.assert_array_equal(roi, evaluation_mask(labels, bits, [2]))

    def test_invalid_or_empty_roi_is_rejected(self):
        labels = np.ones((20, 20), np.uint16)
        bits = np.full(labels.shape, 1, np.uint8)
        for bad in (bits.astype(float), bits[:10], np.full(labels.shape, 16, np.uint8)):
            with self.subTest(dtype=bad.dtype, shape=bad.shape), self.assertRaises(ValueError):
                evaluation_mask(labels, bad, [])
        for ids, visibility in (([1], bits), ([], bits * 0)):
            with self.assertRaises(ValueError):
                evaluation_mask(labels, visibility, ids)

    def test_guard_matches_cpp_known_answers_and_separates_metadata(self):
        # Same canonical fixture as tests/input_identity_tests.cpp.
        camera = dict(id=0, calibration_id='separate metadata guard',
            resolution=dict(width=2, height=2),
            projection=dict(fx=2., fy=2., cx=.5, cy=.5, theta_max_rad=1.45,
                            z_epsilon_m=1e-6, k=[0., 0., 0., 0.]),
            T_camera_from_vehicle=np.eye(4).tolist())
        pixels = np.arange(12, dtype=np.uint8).reshape(2, 2, 3)
        guard = input_guard(camera, pixels, '0')
        self.assertEqual(guard['image_identity']['sha256'],
            '3b472f66fd05e50a689264bd30bec89a798411442085f0a9a2cb153283aa136e')
        self.assertEqual(guard['camera_model_sha256'],
            '00dc4666fa5739db5c942b6c871f97d8b288871ead42ba37f261c4b6a32998ee')
        changed = copy.deepcopy(camera)
        changed['T_camera_from_vehicle'][0][1] = -0.
        changed['calibration_id'] = 'estimated'
        renamed = input_guard(changed, pixels, '0')
        self.assertEqual(guard['camera_model_sha256'], renamed['camera_model_sha256'])
        self.assertNotEqual(guard['calibration_id'], renamed['calibration_id'])
        changed['T_camera_from_vehicle'][0][3] = .01
        self.assertNotEqual(guard['camera_model_sha256'], input_guard(changed, pixels, '0')['camera_model_sha256'])
        changed['projection']['fx'] = float('nan')
        with self.assertRaises(ValueError):
            input_guard(changed, pixels, '0')

    def test_unhashed_changed_or_escaping_files_are_rejected(self):
        import hashlib
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'root'
            root.mkdir()
            (root / 'image').write_bytes(b'original')
            digest = hashlib.sha256(b'original').hexdigest()
            self.assertEqual(checked(root, 'image', {'image': digest}), root / 'image')
            (root / 'image').write_bytes(b'changed')
            for name, hashes in (('image', {'image': digest}), ('image', {}),
                                 ('../outside', {'../outside': digest})):
                with self.assertRaises(ValueError):
                    checked(root, name, hashes)


if __name__ == '__main__':
    unittest.main()
