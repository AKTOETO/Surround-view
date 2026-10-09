"""Known geometry, source occlusion and paired-experiment integrity controls."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tools/blender'), str(ROOT/'tools/research')]
from visibility import projects_inside, visible_camera_bits
from temporal_seam_stability import load_sequence, visibility_mask
from stitch_resolution import validate_pair

FIXTURE = ROOT/'tests/data/stitch_resolution_v1'


def camera(index):
    transform = np.eye(4)
    transform[0, 3] = -index
    return {'id': index, 'T_camera_from_vehicle': transform.tolist(),
            'resolution': {'width': 101, 'height': 101},
            'projection': {'fx': 20., 'fy': 20., 'cx': 50., 'cy': 50.,
                           'alpha': 0., 'k': [0.]*4, 'z_epsilon_m': 1e-6,
                           'theta_max_rad': 1.4}}


class SceneVisibilityTests(unittest.TestCase):
    def setUp(self):
        self.cameras = [camera(i) for i in range(4)]
        self.point = np.array([0., 0., 5.])

    def test_projection_limits(self):
        self.assertTrue(projects_inside(self.cameras[0], self.point))
        self.assertFalse(projects_inside(self.cameras[0], [0, 0, -5]))
        self.assertFalse(projects_inside(self.cameras[0], [50, 0, .1]))
        self.assertFalse(projects_inside(self.cameras[0], [np.nan, 0, 1]))
        clipped = camera(0)
        clipped['projection']['cx'] = 150
        self.assertFalse(projects_inside(clipped, self.point))

    def test_front_occluder_blocks_only_its_camera(self):
        def cast(origin, direction, distance):
            return origin+direction if origin[0] == 0 else self.point
        self.assertEqual(visible_camera_bits(self.point, np.eye(4), self.cameras, cast), 14)

    def test_no_hit_is_not_visible(self):
        self.assertEqual(visible_camera_bits(self.point, np.eye(4), self.cameras, lambda *args: None), 0)

    def test_rigid_motion_preserves_visibility_and_moves_origins(self):
        pose = np.eye(4)
        angle = .7
        pose[:2, :2] = [[np.cos(angle), -np.sin(angle)], [np.sin(angle), np.cos(angle)]]
        pose[:3, 3] = [12, -4, 0]
        world = (pose @ np.append(self.point, 1))[:3]
        origins = []
        def cast(origin, direction, distance):
            origins.append(origin)
            np.testing.assert_allclose(origin+direction*(distance-.02), world, atol=1e-12)
            return world
        self.assertEqual(visible_camera_bits(world, pose, self.cameras, cast), 15)
        np.testing.assert_allclose(origins[0], [12, -4, 0])

    def test_hit_tolerance_and_camera_ids(self):
        cast = lambda *args: self.point+np.array([0, 0, .005])
        self.assertEqual(visible_camera_bits(self.point, np.eye(4), self.cameras, cast), 15)
        self.assertEqual(visible_camera_bits(self.point, np.eye(4), self.cameras, cast, .001), 0)
        with self.assertRaises(ValueError):
            visible_camera_bits(self.point, np.eye(4), self.cameras[:3], cast)
        with self.assertRaises(ValueError):
            visible_camera_bits(self.point, np.eye(4), self.cameras, cast, 0)

    def test_visibility_policy_and_legacy_failure(self):
        bits = np.array([0, 1, 3, 15], np.uint8)
        np.testing.assert_array_equal(visibility_mask(bits, 'any'), [False, True, True, True])
        np.testing.assert_array_equal(visibility_mask(bits, 'all'), [False, False, False, True])
        with self.assertRaises(ValueError):
            visibility_mask(np.array([16], np.uint8), 'any')
        with self.assertRaisesRegex(ValueError, 'requires independent'):
            load_sequence(ROOT/'tests/data/paired_street_v1', ROOT/'tests/data/paired_street_v1', 'any')

    def test_captured_pair_has_identical_truth_and_mask_removes_hidden_points(self):
        self.assertTrue(validate_pair(FIXTURE/'64', FIXTURE/'256')['direct_truth_bit_identical'])
        _, _, _, masks, _, _ = load_sequence(FIXTURE/'256', FIXTURE/'256', 'ignore')
        _, _, _, matched, _, _ = load_sequence(FIXTURE/'256', FIXTURE/'256', 'any')
        self.assertTrue(all(np.all(b <= a) for a, b in zip(masks, matched)))
        self.assertGreater(sum(m.sum() for m in masks), sum(m.sum() for m in matched))
        self.assertTrue(all(m.any() for m in matched))

    def test_changed_pair_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)/'high'
            shutil.copytree(FIXTURE/'256', root)
            target = root/'capture.json'
            payload = json.loads(target.read_text())
            payload['face_size'] = 128
            target.write_text(json.dumps(payload))
            with self.assertRaisesRegex(ValueError, '64 and 256'):
                validate_pair(FIXTURE/'64', root)


if __name__ == '__main__':
    unittest.main()
