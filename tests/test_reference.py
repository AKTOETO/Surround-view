#!/usr/bin/env python3
"""Analytic answers for independent dense carrier reference."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from copy import deepcopy

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import reference as ref
import simulator


class ReferenceTests(unittest.TestCase):
    def test_server_pyramid_parameter_and_legacy_alias(self):
        cfg = simulator.config()
        cfg['output'] = dict(width=31, height=23)
        rng = np.random.default_rng(42)
        images = [rng.integers(0, 256, (c['resolution']['height'], c['resolution']['width'], 3))
                  for c in cfg['cameras']]
        cfg['fusion'] = dict(mode='multi_band', pyramid_levels=1)
        single = ref.render(cfg, images)['rgb']
        cfg['fusion']['pyramid_levels'] = 4
        multiple = ref.render(cfg, images)['rgb']
        self.assertFalse(np.array_equal(single, multiple))
        legacy = deepcopy(cfg)
        legacy['fusion']['num_pyramid_levels'] = legacy['fusion'].pop('pyramid_levels')
        np.testing.assert_array_equal(multiple, ref.render(legacy, images)['rgb'])
        legacy['fusion']['pyramid_levels'] = 1
        with self.assertRaisesRegex(ValueError, 'conflicting'):
            ref.render(legacy, images)
    def test_stable_roots_and_linear_limit(self):
        roots = ref.roots(np.array([1., 0., 1., 1.]), [1e8, 2, 0, 0], [1, -6, 0, 1])
        np.testing.assert_allclose(np.sort(roots[:, 0]), [-1e8, -1e-8])
        self.assertEqual(roots[0, 1], 3)
        np.testing.assert_array_equal(roots[:, 2], [0, 0])
        self.assertTrue(np.isinf(roots[:, 3]).all())

    def test_dome_floor_caps_and_clipping(self):
        s = dict(type='dome_floor_v1', dome_radius_m=5)
        directions = np.array([[0., 0, -1], [0, 0, 1], [1, 0, 0]])
        p, hit, t = ref.intersect(s, [0, 0, 1], directions)
        np.testing.assert_array_equal(hit, True)
        np.testing.assert_allclose(t, [1, 4, np.sqrt(24)])
        np.testing.assert_allclose(p[0], [0, 0, 0])
        # Clip against optical-axis depth, not the Euclidean ray distance.
        _, hit, _ = ref.intersect(s, [0, 0, 1], directions[2:3], far=2.5, depth_factor=.5)
        self.assertTrue(hit[0])
        _, hit, _ = ref.intersect(s, [0, 0, 1], directions[:1], near=2)
        self.assertFalse(hit[0])

    def test_enclosures_have_no_holes_in_all_directions(self):
        rng = np.random.default_rng(71)
        directions = rng.normal(size=(4096, 3))
        directions /= np.linalg.norm(directions, axis=-1, keepdims=True)
        for surface in (dict(type='dome_floor_v1', dome_radius_m=12),
                        dict(type='cylinder_floor_v1', radius_m=12, height_m=12),
                        dict(type='cube_floor_v1', half_extent_m=12, height_m=12)):
            _, hit, distance = ref.intersect(surface, [3, -2, 2], directions)
            self.assertTrue(hit.all(), surface['type'])
            self.assertTrue((distance > 0).all())

    def test_cylinder_and_cube(self):
        for kind in ('cylinder_floor_v1', 'cube_floor_v1'):
            s = dict(type=kind, height_m=4, radius_m=5, half_extent_m=5)
            p, hit, t = ref.intersect(s, [0, 0, 1], np.array([[0., 0, -1], [0, 0, 1], [1, 0, 0]]))
            np.testing.assert_array_equal(hit, True)
            np.testing.assert_allclose(t, [1, 3, 5])
        # Tangent of cylinder, plus a genuine miss above its cap.
        s = dict(type='cylinder_floor_v1', radius_m=5, height_m=4)
        _, hit, t = ref.intersect(s, [5, -2, 2], np.array([[0., 1, 0]]))
        self.assertTrue(hit[0])
        self.assertAlmostEqual(t[0], 2)
        _, hit, _ = ref.intersect(s, [6, 0, 5], np.array([[-1., 0, 0]]))
        self.assertFalse(hit[0])

    def test_bowl_exact_patches_and_plane(self):
        s = simulator.config()['surface']
        xs = np.array([0., 2.6, 4.3, -6, 6.1])
        origins = [0, 0, 5]
        # Rays reach each chosen x at t=1. Analytic z remains an independent answer.
        expected_z = s['corner_height_m']/2*(np.maximum(np.abs(xs)-2.6, 0)/3.4)**2
        directions = np.stack([xs, np.zeros_like(xs), expected_z-5], axis=-1)
        p, hit, t = ref.intersect(s, origins, directions)
        np.testing.assert_array_equal(hit, [True, True, True, True, False])
        np.testing.assert_allclose(t[:4], 1, atol=1e-12)
        np.testing.assert_allclose(p[:4, 2], expected_z[:4], atol=1e-12)
        s['corner_height_m'] = 0
        _, hit, t = ref.intersect(s, [0, 0, 2], np.array([[0., 0, -1], [0, 0, 1], [1, 0, 0]]))
        np.testing.assert_array_equal(hit, [True, False, False])
        self.assertEqual(t[0], 2)

    def test_dense_bowl_residual(self):
        rng = np.random.default_rng(17)
        d = rng.normal(size=(10000, 3))
        d[:, 2] = -np.abs(d[:, 2])
        s = simulator.config()['surface']
        p, hit, _ = ref.intersect(s, [0, 0, 5], d)
        self.assertGreater(hit.sum(), 4000)
        expected = .75*((np.maximum(np.abs(p[:, 0])-2.6, 0)/3.4)**2
                          +(np.maximum(np.abs(p[:, 1])-1.2, 0)/3.3)**2)
        np.testing.assert_allclose(p[hit, 2], expected[hit], atol=1e-10)

    def test_view_top_left_and_center(self):
        c = simulator.config()
        c['output'] = dict(width=3, height=3)
        eye, d, depth = ref.rays(c)
        np.testing.assert_allclose(d[1, 1], -eye/np.linalg.norm(eye), atol=1e-14)
        self.assertGreater(d[0, 1, 2], d[2, 1, 2])
        self.assertAlmostEqual(depth[1, 1], 1)

    def camera(self):
        return dict(id=0, calibration_id='test', resolution=dict(width=2, height=2),
                    T_camera_from_vehicle=np.eye(4).tolist(),
                    projection=dict(fx=1, fy=1, cx=.5, cy=.5, k=[0]*4,
                                    theta_max_rad=1.4, z_epsilon_m=1e-6))

    def test_bilinear_axis_fov_and_behind(self):
        image = np.array([[[0, 0, 0], [255, 0, 0]], [[0, 255, 0], [0, 0, 255]]], float)
        rgb, valid, edge, _ = ref.sample(self.camera(), np.array([[0., 0, 1], [0, 0, -1], [10, 0, 1]]), image)
        np.testing.assert_allclose(rgb[0], [.25, .25, .25])
        np.testing.assert_array_equal(valid, [True, False, False])
        self.assertEqual(edge[0], .5)

    def fusion_fixture(self):
        c = simulator.config()
        c['surface']['corner_height_m'] = 0
        c['output'] = dict(width=1, height=1)
        c['virtual_camera'].update(azimuth_rad=0, elevation_rad=np.pi/2, distance_m=5)
        c['cameras'] = [dict(self.camera(), id=i) for i in range(4)]
        for camera in c['cameras']:
            camera['T_camera_from_vehicle'][2][3] = 1
        images = [np.full((2, 2, 3), v, dtype=float) for v in (0, 255, 0, 255)]
        return c, images

    def test_linear_rgb_and_deterministic_tie(self):
        c, images = self.fusion_fixture()
        r = ref.render(c, images)
        self.assertEqual(r['coverage'][0, 0], 4)
        np.testing.assert_allclose(r['weights'], .25)
        np.testing.assert_array_equal(r['rgb'][0, 0], [188]*3)
        c['fusion'] = dict(mode='hard_best_angle')
        r = ref.render(c, images)
        np.testing.assert_array_equal(r['weights'][0, 0], [1, 0, 0, 0])
        np.testing.assert_array_equal(r['rgb'][0, 0], [0]*3)
        # Raster-border validity is distinct from zero feather weight.
        for cam in c['cameras']:
            cam['projection']['cx'] = 0
        c['fusion']['mode'] = 'angular_feather'
        r = ref.render(c, images)
        self.assertEqual(r['coverage'][0, 0], 4)
        self.assertEqual(r['weights'].sum(), 0)

    def test_report_provenance_checksums_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            c, images = self.fusion_fixture()
            (root/'config.json').write_text(json.dumps(c))
            paths, hashes = [], {}
            for i, image in enumerate(images):
                name = f'{i}.png'
                Image.fromarray(np.uint8(image)).save(root/name)
                paths.append(name)
                hashes[name] = hashlib.sha256((root/name).read_bytes()).hexdigest()
            m = dict(schema_version=1, calibration_ids=['test']*4, sha256=hashes,
                     frames=[dict(paths=paths, scenario_timestamp_ns='0')])
            (root/'manifest.json').write_text(json.dumps(m))
            report = ref.run(root/'config.json', root/'manifest.json', root/'out')
            self.assertEqual(report['carrier_miss_pixels'], 0)
            self.assertTrue((root/'out'/'REPORT.md').exists())
            with self.assertRaises(FileExistsError):
                ref.run(root/'config.json', root/'manifest.json', root/'out')
            m['sha256'][paths[0]] = 'bad'
            (root/'manifest.json').write_text(json.dumps(m))
            with self.assertRaisesRegex(ValueError, 'checksum'):
                ref.run(root/'config.json', root/'manifest.json', root/'bad')
            self.assertFalse((root/'bad').exists())


if __name__ == '__main__':
    unittest.main()
