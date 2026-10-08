"""Optics/coordinate/file contract tests. No Blender installation is required."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools' / 'blender'))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from rig import configuration, cube_coordinates, vehicle_pose
from scenario import perturb, validate
from convert import bilinear, convert, convert_camera, linear_rgb, srgb8
from depth_truth import _sample_depth, convert_camera_depth
from validate_depth_plane import evaluate as evaluate_depth_plane
from validate_depth_primitives import (
    evaluate_primitive as evaluate_depth_primitive,
    ray_sphere_intersect,
    ray_box_intersect,
)
from semantics import (
    SEMANTIC_CLASSES,
    colorize_semantics,
    convert_camera_semantics,
    evaluate_semantic_oracle,
)
from markers import markers_ground_truth, GROUND_MARKERS, RAISED_OBSTACLES
from scenario import (
    SCENARIO_PRESETS,
    scenario_preset,
    get_split,
    TRAIN_SEEDS,
    HOLDOUT_SEEDS,
)
from compare_visibility import evaluate as evaluate_visibility, project as project_visibility


class BlenderFixtureTests(unittest.TestCase):
    def test_mounts_and_frame_conventions(self):
        centers = []
        for cam in configuration()['cameras']:
            transform = np.array(cam['T_camera_from_vehicle'])
            rotation = transform[:3,:3]
            np.testing.assert_allclose(rotation @ rotation.T,np.eye(3),atol=1e-14)
            self.assertAlmostEqual(np.linalg.det(rotation),1.)
            center = -rotation.T @ transform[:3,3]
            centers.append(center)
            np.testing.assert_allclose(transform @ [*center,1],[0,0,0,1],atol=1e-14)
            self.assertLess(rotation[2,2],0)  # Optical axis points slightly down.
        self.assertGreater(centers[0][0],2.3)
        self.assertLess(centers[2][0],-2.3)
        # Mirror is centered at |y|=.981 and has half-width .08 m.
        self.assertGreater(abs(centers[1][1]),1.061+.025)
        self.assertGreater(abs(centers[3][1]),1.061+.025)
        np.testing.assert_allclose(vehicle_pose(30)[:3,3],[2,0,0])

    def test_mount_randomization_and_override(self):
        nominal = configuration()
        recipe = dict(schema_version=1, seed=42, mounts=dict(yaw_deg=5, pitch_deg=4, along_body_m=.15))
        actual, offsets = perturb(nominal, recipe)
        self.assertEqual(actual, perturb(nominal, recipe)[0])
        self.assertEqual(nominal, configuration())
        for camera, offset, base in zip(actual['cameras'], offsets, nominal['cameras']):
            T, N = np.array(camera['T_camera_from_vehicle']), np.array(base['T_camera_from_vehicle'])
            np.testing.assert_allclose(T[:3,:3] @ T[:3,:3].T, np.eye(3), atol=1e-14)
            self.assertAlmostEqual(np.linalg.det(T[:3,:3]), 1)
            delta = -T[:3,:3].T @ T[:3,3] + N[:3,:3].T @ N[:3,3]
            axis = 1 if camera['id'] in (0,2) else 0
            self.assertAlmostEqual(delta[axis], offset['along_body_m'])
            self.assertLessEqual(abs(offset['yaw_deg']), 5)
            self.assertLessEqual(abs(offset['pitch_deg']), 4)
        zero, _ = perturb(nominal, dict(schema_version=1, seed=42))
        for a, b in zip(zero['cameras'], nominal['cameras']):
            np.testing.assert_allclose(a['T_camera_from_vehicle'], b['T_camera_from_vehicle'], atol=1e-14)
        recipe['mounts']['overrides'] = {'0': dict(yaw_deg=1, pitch_deg=-2, along_body_m=.1)}
        _, offsets = perturb(nominal, recipe)
        self.assertEqual(offsets[0]['yaw_deg'], 1)
        self.assertEqual(offsets[0]['pitch_deg'], -2)
        self.assertEqual(offsets[0]['along_body_m'], .1)

    def test_invalid_scenario_rules(self):
        for recipe in (dict(schema_version=1, seed=-1), dict(schema_version=1, seed=True),
                       dict(schema_version=1, mounts=dict(yaw_deg=21)),
                       dict(schema_version=1, mounts=dict(overrides={'4': {}})),
                       dict(schema_version=1, world=dict(building_height_m=[10,4])),
                       dict(schema_version=1, unknown=True)):
            with self.assertRaises(ValueError):
                validate(recipe)

    def test_face_centers_and_image_axes(self):
        rays = np.array([[1,0,0],[-1,0,0],[0,1,0],[0,-1,0],[0,0,1],[0,0,-1]])
        mappings = cube_coordinates(rays,64)
        for index,name in enumerate(('px','nx','py','ny','pz','nz')):
            mask,uv = mappings[name]
            self.assertEqual(np.flatnonzero(mask).tolist(),[index])
            np.testing.assert_allclose(uv[index],[31.5,31.5])
        # Optical +X must point right, +Y down on forward face, including corners.
        mask, uv = cube_coordinates(np.array([[.5,.25,1],[-.5,-.25,1]]),64)['pz']
        self.assertTrue(mask.all())
        np.testing.assert_allclose(uv,[[47.5,39.5],[15.5,23.5]])

    def test_direction_field_roundtrip(self):
        size = 128
        yy,xx = np.mgrid[:size,:size]
        a,b = (xx+.5)*2/size-1,(yy+.5)*2/size-1
        one = np.ones_like(a)
        # Independent face equations, not the producer's face_basis implementation.
        directions = {'px':(one,b,-a), 'nx':(-one,b,a), 'py':(a,one,-b),
                      'ny':(a,-one,b), 'pz':(a,b,one), 'nz':(-a,b,-one)}
        faces = {}
        for name, components in directions.items():
            d = np.stack(components,axis=-1)
            d /= np.linalg.norm(d,axis=-1,keepdims=True)
            faces[name] = srgb8((d+1)/2)
        cam = configuration()['cameras'][0]
        result = linear_rgb(convert_camera(cam,faces,size))
        # Closed-form equidistant inverse, independent of camera_rays Newton solver.
        yy,xx = np.mgrid[:400,:400]
        a,b = (xx-199.5)/128,(yy-199.5)/128
        theta = np.hypot(a,b)
        ratio = np.sinc(theta/np.pi)
        expected = (np.stack([a*ratio,b*ratio,np.cos(theta)],axis=-1)+1)/2
        valid = theta <= 1.48
        self.assertLess(np.max(np.abs(result[valid]-expected[valid])),.008)
        self.assertTrue((result[~valid] == 0).all())
        # Missing negative-Z face is fine for <180 degree cameras; missing +Z isn't.
        faces.pop('nz')
        convert_camera(cam,faces,size)
        faces.pop('pz')
        with self.assertRaises(ValueError):
            convert_camera(cam,faces,size)

    def test_bilinear_and_linear_light(self):
        pixels = np.array([[[0,0,0],[255,255,255]]],np.uint8)
        value = srgb8(bilinear(linear_rgb(pixels),np.array([[.5,0]])))
        np.testing.assert_array_equal(value,[[188,188,188]])

    def test_depth_truth_returns_radial_range_and_nan_for_no_hit(self):
        camera = configuration()['cameras'][0]
        face_size = 32
        faces = {name:np.full((face_size,face_size),5.,np.float32)
                 for name in ('px','nx','py','ny','pz')}
        ranges = convert_camera_depth(camera,faces,face_size)
        center = (int(camera['projection']['cy']),int(camera['projection']['cx']))
        self.assertAlmostEqual(float(ranges[center]),5.,delta=.001)
        off_axis = (center[0],center[1]+40)
        expected = 5./np.cos(np.arctan2(40,camera['projection']['fx']))
        self.assertAlmostEqual(float(ranges[off_axis]),float(expected),delta=.01)
        missing = {name:np.full((face_size,face_size),1e10,np.float32) for name in faces}
        self.assertTrue(np.isnan(convert_camera_depth(camera,missing,face_size)).all())

    def test_depth_sampling_never_selects_invalid_zero_weight_texel(self):
        # At uv=(0, 0), the first sample has all interpolation weight, but
        # it is invalid. The only valid sample has a zero coefficient.
        image = np.array([[1e10, 7.0], [1e10, 1e10]], dtype=np.float32)
        sampled = _sample_depth(image, np.array([[0.0, 0.0]]), 1e9)
        self.assertEqual(float(sampled[0]), 7.0)

    def test_depth_truth_matches_independent_analytic_plane(self):
        coarse = evaluate_depth_plane(32, [0, 0, 1], 2.0)
        fine = evaluate_depth_plane(256, [0, 0, 1], 2.0)
        self.assertEqual(fine['valid_fraction_of_comparable'], 1.0)
        self.assertLess(fine['absolute_error_m']['p95_m'],
                        coarse['absolute_error_m']['p95_m'] * .2)

        tilted = evaluate_depth_plane(128, [.16, -.11, .98], 6.0)
        self.assertEqual(tilted['valid_fraction_of_comparable'], 1.0)
        self.assertLess(tilted['nearby_core_roi_m']['p95_m'], .0001)

    def test_visibility_projection_and_no_return_weight_accounting(self):
        config = configuration()
        config['output'].update(width=96, height=54)
        camera = config['cameras'][0]
        transform = np.asarray(camera['T_camera_from_vehicle'])
        center = -transform[:3, :3].T @ transform[:3, 3]
        point = center + transform[:3, :3].T @ [0., 0., 5.]
        uv, distance, valid = project_visibility(camera, point.reshape(1, 1, 3))
        np.testing.assert_allclose(uv[0, 0], [199.5, 199.5], atol=1e-10)
        self.assertAlmostEqual(float(distance[0, 0]), 5.)
        self.assertTrue(bool(valid[0, 0]))

        images = [np.zeros((400, 400, 3), dtype=float) for _ in range(4)]
        no_returns = [np.full((400, 400), np.nan, dtype=np.float32) for _ in range(4)]
        metrics, diagnostic = evaluate_visibility(config, images, no_returns)
        self.assertGreater(metrics['projected_pixels'], 0)
        self.assertEqual(metrics['exact_surface_coverage_fraction'], 0.)
        self.assertAlmostEqual(
            metrics['weighted_contribution_fraction']['depth_has_no_return'], 1.)
        self.assertEqual(diagnostic.shape, (54, 96, 3))

    def test_depth_truth_matches_analytic_sphere_and_cube_primitives(self):
        sphere_scene = [{"kind": "sphere", "center": [0.0, 0.0, 3.0], "radius": 0.8}]
        coarse_sphere = evaluate_depth_primitive(32, sphere_scene, "sphere_3m")
        fine_sphere = evaluate_depth_primitive(256, sphere_scene, "sphere_3m")
        self.assertEqual(fine_sphere["expected_hit_rays"], coarse_sphere["expected_hit_rays"])
        self.assertLess(fine_sphere["interior_surface_error_m"]["p95_m"],
                        coarse_sphere["interior_surface_error_m"]["p95_m"] * 0.15)
        self.assertLess(fine_sphere["overall_error_m"]["p50_m"], 0.001)

        cube_scene = [{
            "kind": "box",
            "center": [-0.5, 0.3, 6.0],
            "half_extents": [0.6, 0.6, 1.0],
            "rotation": [
                [np.cos(np.pi / 6), 0, np.sin(np.pi / 6)],
                [0, 1, 0],
                [-np.sin(np.pi / 6), 0, np.cos(np.pi / 6)],
            ],
        }]
        fine_cube = evaluate_depth_primitive(256, cube_scene, "tilted_cube")
        self.assertLess(fine_cube["overall_error_m"]["p95_m"], 0.01)
        self.assertEqual(fine_cube["step_discontinuity_false_negative_rays"], 0)

        discontinuous_scene = [
            {"kind": "sphere", "center": [-0.3, 0.2, 2.5], "radius": 0.5},
            {"kind": "plane", "normal": [0.0, 0.0, 1.0], "offset_m": 10.0},
        ]
        disc = evaluate_depth_primitive(128, discontinuous_scene, "disc")
        self.assertEqual(disc["step_discontinuity_false_negative_rays"], 0)
        self.assertEqual(disc["step_discontinuity_false_positive_rays"], 0)

    def test_discrete_semantic_label_conversion_and_oracle(self):
        objects = [
            {"kind": "box", "center": [0.0, 0.0, 4.0], "half_extents": [0.75, 0.75, 0.75], "semantic_id": 6},
            {"kind": "sphere", "center": [1.0, 0.0, 3.0], "radius": 0.5, "semantic_id": 5},
            {"kind": "plane", "normal": [0.0, 0.0, 1.0], "offset_m": 10.0, "semantic_id": 1},
        ]
        fine = evaluate_semantic_oracle(128, objects)
        self.assertGreater(fine["overall_pixel_accuracy"], 0.999)
        self.assertGreater(fine["per_class"]["ground_drivable"]["iou"], 0.99)
        self.assertGreater(fine["per_class"]["vertical_obstacle"]["iou"], 0.98)

        # Colorization test
        labels = np.array([[0, 1], [4, 6]], dtype=np.int32)
        rgb = colorize_semantics(labels)
        self.assertEqual(rgb.shape, (2, 2, 3))
        np.testing.assert_array_equal(rgb[0, 0], SEMANTIC_CLASSES[0]["color"])
        np.testing.assert_array_equal(rgb[1, 0], SEMANTIC_CLASSES[4]["color"])

    def test_ground_and_raised_markers_geometry(self):
        gt = markers_ground_truth()
        self.assertGreaterEqual(len(gt["ground_markers"]), 8)
        self.assertGreaterEqual(len(gt["raised_obstacles"]), 5)
        for m in gt["ground_markers"]:
            self.assertEqual(m["world_xyz_m"][2], 0.0)
            self.assertEqual(m["vehicle_xyz_m"][2], 0.0)

        # Transformed pose check
        pose = np.eye(4)
        pose[0, 3] = 10.0  # Vehicle moved forward 10m
        gt_shifted = markers_ground_truth(pose)
        first_marker = gt_shifted["ground_markers"][0]
        self.assertAlmostEqual(first_marker["vehicle_xyz_m"][0],
                               first_marker["world_xyz_m"][0] - 10.0)

    def test_scenario_presets_and_train_holdout_split(self):
        self.assertEqual(len(TRAIN_SEEDS), 8)
        self.assertEqual(len(HOLDOUT_SEEDS), 4)

        for preset_id in ("S0", "S1", "S2", "S3", "S4", "S5"):
            recipe_train = scenario_preset(preset_id, seed=3)
            self.assertEqual(recipe_train["split"], "train")
            self.assertEqual(get_split(3), "train")
            self.assertEqual(recipe_train["preset_id"], SCENARIO_PRESETS[preset_id]["id"])

            recipe_holdout = scenario_preset(preset_id, seed=9)
            self.assertEqual(recipe_holdout["split"], "holdout")
            self.assertEqual(get_split(9), "holdout")

    def test_manifest_hashes_and_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / 'capture'
            source.mkdir()
            image = np.full((32,32,3),170,np.uint8)
            Image.fromarray(image).save(source/'face.png')
            digest = hashlib.sha256((source/'face.png').read_bytes()).hexdigest()
            cfg = configuration()
            captures = [{'id':c['id'],'faces':dict.fromkeys(('px','nx','py','ny','pz'),'face.png'),
                         'T_world_from_camera':np.linalg.inv(c['T_camera_from_vehicle']).tolist()}
                        for c in cfg['cameras']]
            metadata = {'schema_version':1,'origin':'test','blender_version':'test','engine':'test',
                        'view_transform':'Standard','fps':30,'script_sha256':{},'limitations':[],
                        'config':cfg,'face_size':32,'sha256':{'face.png':digest},
                        'frames':[{'scenario_timestamp_ns':'0','T_world_from_vehicle':np.eye(4).tolist(),
                                   'cameras':captures}]}
            (source/'capture.json').write_text(json.dumps(metadata))
            output = Path(temporary)/'replay'
            manifest = json.loads(convert(source,output).read_text())
            self.assertEqual(len(manifest['sha256']),4)
            self.assertEqual(manifest['calibration_ids'],[c['calibration_id'] for c in cfg['cameras']])
            for path,expected in manifest['sha256'].items():
                self.assertEqual(hashlib.sha256((output/path).read_bytes()).hexdigest(),expected)
            with self.assertRaises(FileExistsError):
                convert(source,output)
            (source/'face.png').write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError,'checksum'):
                convert(source,Path(temporary)/'bad')
            self.assertFalse((Path(temporary)/'bad').exists())

    def test_unsafe_capture_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root/'capture.json').write_text(json.dumps({
                'schema_version':1,'frames':[{}],'sha256':{'../escape':'unused'}}))
            with self.assertRaisesRegex(ValueError,'unsafe'):
                convert(root,root/'output')


if __name__ == '__main__':
    unittest.main()
