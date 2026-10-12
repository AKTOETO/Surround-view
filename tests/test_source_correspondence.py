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
from planar_precision import plane_from_mvp,reproject,view_projection
from displaced_correspondence import bilinear_rgb,chroma_mask
from hybrid_correspondence import reconstruct_rgba
from source_footprint_audit import footprint,selected_pixels
from cube_footprint_audit import cube_sample,cube_ray,fisheye_ray


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

    def test_mvp_plane_roundtrip_known_identity(self):
        pixels = np.array([[0, 0], [1, 3], [2, 2]])
        hits = plane_from_mvp(np.eye(4), pixels, 4, 3)
        np.testing.assert_allclose(hits[:, 2], 0)
        np.testing.assert_allclose(reproject(np.eye(4), hits, 4, 3), pixels[:, ::-1], atol=1e-15)
        np.testing.assert_allclose(hits[0], [-.75, 2 / 3, 0])

    def test_view_projection_places_target_at_output_center(self):
        config = {'virtual_camera': {'azimuth_rad': 0, 'elevation_rad': np.pi / 4,
                  'distance_m': 2, 'fov_y_rad': np.pi / 2, 'clip_m': [.1, 100]},
                  'output': {'width': 4, 'height': 4}}
        matrix = view_projection(config)
        np.testing.assert_allclose(reproject(matrix, np.array([[0., 0., 0.]]), 4, 4), [[1.5, 1.5]])

    def test_bilinear_samples_centers_fractional_and_clamped_border(self):
        image = np.array([[[255, 0, 0], [0, 255, 0]], [[0, 0, 255], [255, 255, 255]]], dtype=np.uint8)
        values = bilinear_rgb(image, np.array([[0, 0], [.5, .5], [1, 1], [1.8, 0]]))
        np.testing.assert_allclose(values, [[1, 0, 0], [.5, .5, .5], [1, 1, 1], [0, 1, 0]])
        np.testing.assert_array_equal(chroma_mask(np.array([[[255, 0, 255], [255, 255, 255]]])), [[True, False]])
        with self.assertRaises(ValueError):
            bilinear_rgb(image, np.array([[float('nan'), 0]]))

    def test_footprint_weights_zero_support_and_clamped_duplicates(self):
        xy,w=footprint(np.array([[.25,.75],[1.8,0],[0,0]]),2,2)
        np.testing.assert_array_equal(xy[0],[[0,0],[1,0],[0,1],[1,1]])
        np.testing.assert_allclose(w[0],[.1875,.0625,.5625,.1875])
        np.testing.assert_allclose(w.sum(axis=1),1)
        np.testing.assert_array_equal(xy[1,:,0],[1,1,1,1])
        np.testing.assert_array_equal(w[2],[1,0,0,0])
        for p in [[[np.nan,0]],[[np.inf,0]]]:
            with self.assertRaises(ValueError):footprint(p,2,2)
        with self.assertRaises(ValueError):footprint([[0,0]],0,2)

    def test_footprint_selection_keeps_all_mismatches_and_bounded_controls(self):
        kinds=np.array([[1,0],[2,2],[1,1],[1,2],[2,2]])
        status=np.array([[1,0],[1,1],[0,0],[1,1],[1,1]])
        ids,mismatch=selected_pixels(kinds,status,1)
        np.testing.assert_array_equal(ids,[0,1,2,4])
        np.testing.assert_array_equal(mismatch,[False,True,True,True])

    def test_cube_projection_faces_centers_edges_and_inverse(self):
        for face,ray in [('px',[1,0,0]),('nx',[-1,0,0]),('py',[0,1,0]),
                         ('ny',[0,-1,0]),('pz',[0,0,1]),('nz',[0,0,-1])]:
            actual,uv=cube_sample(ray,512)
            self.assertEqual(actual,face)
            np.testing.assert_allclose(uv,[255.5,255.5])
            np.testing.assert_allclose(cube_ray(face,uv,512),ray)
            for pixel in [[0,0],[511,511],[135.25,398.75]]:
                actual,uv=cube_sample(cube_ray(face,pixel,512),512)
                self.assertEqual(actual,face);np.testing.assert_allclose(uv,pixel,atol=1e-12)
        for ray,face,uv in [([1,.5,.25],'px',[191.5,383.5]),([-1,.5,.25],'nx',[319.5,383.5]),
                            ([.25,1,.5],'py',[319.5,127.5]),([.25,-1,.5],'ny',[319.5,383.5])]:
            actual,pixel=cube_sample(ray,512)
            self.assertEqual(actual,face);np.testing.assert_allclose(pixel,uv)
        self.assertEqual(cube_sample([1,1,1],512)[0],'px')
        for ray in [[0,0,0],[np.nan,0,1]]:
            with self.assertRaises(ValueError):cube_sample(ray,512)

    def test_fisheye_inverse_axis_and_quarter_turn(self):
        optics=dict(cx=10.,cy=20.,fx=100.,fy=100.,k=[0]*4,alpha=0)
        np.testing.assert_allclose(fisheye_ray([10,20],optics),[0,0,1])
        np.testing.assert_allclose(fisheye_ray([10+50*np.pi,20],optics),[1,0,0],atol=1e-15)
        with self.assertRaises(ValueError):fisheye_ray([10,20],dict(optics,alpha=.1))

    def test_signed_contribution_composition_clips_then_applies_visibility_and_ego(self):
        contributions = np.zeros((1, 3, 4, 3), np.float32)
        contributions[0, 0, 0] = [-.25, .25, 1.5]
        contributions[0, 0, 1] = [.75, .25, -.5]
        validity = np.zeros((1, 3, 4), bool)
        validity[0, 0, 0] = True
        fallback = np.array([[[1, 2, 3, 255], [10, 20, 30, 255], [40, 50, 60, 255]]], np.uint8)
        ego = np.zeros_like(fallback);ego[0, 2] = [90, 80, 70, 255]
        actual = reconstruct_rgba(contributions, validity, fallback, ego)
        np.testing.assert_array_equal(actual, [[[188, 188, 255, 255], [10, 20, 30, 255], [90, 80, 70, 255]]])

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
