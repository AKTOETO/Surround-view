"""Known-answer controls for the independent scene-correspondence audit."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/research'))
from source_correspondence import classify, load_product, project
from source_correspondence_controls import transform_points
from planar_correspondence import intersect_plane


class SourceCorrespondenceTests(unittest.TestCase):
    def test_projection_known_axis_angle_and_behind(self):
        camera = {'T_camera_from_vehicle': np.eye(4).tolist(),
                  'resolution': {'width': 100, 'height': 100},
                  'projection': {'k': [0, 0, 0, 0], 'alpha': 0, 'fx': 20, 'fy': 30,
                                 'cx': 49.5, 'cy': 49.5, 'theta_max_rad': 1.4,
                                 'z_epsilon_m': 1e-6}}
        pixels, valid = project(camera, np.array([[0, 0, 2], [1, 0, 1], [0, 1, 1], [0, 0, -1]]))
        np.testing.assert_allclose(pixels[:3], [[49.5, 49.5], [49.5+5*np.pi, 49.5],
                                              [49.5, 49.5+7.5*np.pi]], atol=1e-12)
        np.testing.assert_array_equal(valid, [True, True, True, False])
        camera['projection']['k'][0] = .01
        with self.assertRaisesRegex(ValueError, 'equidistant'):
            project(camera, np.array([[0, 0, 1]]))

    def test_classification_separates_visibility_identity_and_point(self):
        points = np.zeros((5, 3))
        hits = np.zeros((5, 1, 3))
        hits[0:2] = np.nan
        hits[3, 0, 0] = .1
        categories = classify(np.array([[3], [1], [1], [1], [1]]),
                              np.array([[0], [0], [2], [1], [1]]), hits, points, .02)
        for index, name in enumerate(['invalid_projection', 'no_scene_hit', 'wrong_object',
                                      'same_object_wrong_point', 'matched_surface']):
            expected = np.zeros((5, 1), bool)
            expected[index, 0] = True
            np.testing.assert_array_equal(categories[name], expected)

    def test_world_vehicle_transform_includes_rotation_and_translation(self):
        pose = np.array([[0, -1, 0, 4], [1, 0, 0, -3], [0, 0, 1, 1], [0, 0, 0, 1]])
        vehicle = np.array([[1, 2, 3], [-2, 1, 0]])
        expected = np.array([[2, -2, 4], [3, -5, 1]])
        np.testing.assert_allclose(transform_points(pose, vehicle), expected)
        np.testing.assert_allclose(transform_points(np.linalg.inv(pose), expected), vehicle)
        self.assertGreater(np.linalg.norm(expected - vehicle), 1)
        with self.assertRaises(ValueError):
            transform_points(np.eye(3), vehicle)

    def test_planar_oracle_known_hits_and_rejected_rays(self):
        origin = np.array([1., 2., 3.])
        directions = np.array([[0., 0., -1.], [1., -2., -3.]])
        np.testing.assert_allclose(intersect_plane(origin, directions), [[1, 2, 0], [2, 0, 0]])
        np.testing.assert_allclose(intersect_plane(origin, directions * 7), [[1, 2, 0], [2, 0, 0]])
        for ray, message in [([1, 0, 0], 'parallel'), ([0, 0, 1], 'behind'),
                             ([0, float('nan'), -1], 'finite')]:
            with self.assertRaisesRegex(ValueError, message):
                intersect_plane(origin, ray)

    def test_layout_checksum_and_payload_size_are_enforced(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'positions.f32'
            values = np.array([[[1.25, -2.5], [np.nan, 3]]], dtype='<f4')
            path.write_bytes(values.tobytes())
            descriptor = {'file': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                          'width': 2, 'height': 1, 'channels': 2, 'dtype': 'float32',
                          'byte_order': 'little_endian', 'origin': 'top_left'}
            np.testing.assert_array_equal(load_product(Path(directory), descriptor), values)
            path.write_bytes(b'bad')
            with self.assertRaisesRegex(ValueError, 'checksum'):
                load_product(Path(directory), descriptor)
            descriptor['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
            with self.assertRaises(ValueError):
                load_product(Path(directory), descriptor)


if __name__ == '__main__':
    unittest.main()
