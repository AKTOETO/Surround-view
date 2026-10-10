"""Known-answer controls for photometric perturbations and scene independence."""
from pathlib import Path
import sys
import tempfile
import copy
import unittest

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tools/blender'), str(ROOT/'tools/research')]
from scenario import near_obstacle_positions, validate
from stitch_robustness import expose, exposure_stops, validate_capture
from rig import vehicle_pose
from stitch_resolution import validate_pair
from stitch_mask_followup import compare_rgb, validate_inputs


class StitchRobustnessTests(unittest.TestCase):
    def test_zero_exposure_is_exact_identity_for_all_rgb8_values(self):
        values = np.arange(256, dtype=np.uint8).reshape(16,16)
        rgb = np.repeat(values[..., None], 3, axis=-1)
        transformed, clipped = expose(rgb, 0)
        np.testing.assert_array_equal(transformed, rgb)
        self.assertEqual(clipped, 0)
        self.assertFalse(np.shares_memory(transformed, rgb))

    def test_gain_uses_linear_light(self):
        rgb, clipped = expose(np.full((2,2,3), 255, np.uint8), -1)
        np.testing.assert_array_equal(rgb, 188)
        self.assertEqual(clipped, 0)
        rgb, clipped = expose(np.full((2,2,3), 255, np.uint8), 1)
        np.testing.assert_array_equal(rgb, 255)
        self.assertEqual(clipped, 1)

    def test_ev_changes_only_front_in_temporal_condition(self):
        self.assertEqual(exposure_stops('front_jump', 0), [.5,0,0,0])
        self.assertEqual(exposure_stops('front_jump', 1), [-.5,0,0,0])
        self.assertEqual(exposure_stops('nominal', 1), [0]*4)
        self.assertEqual(exposure_stops('static_bias', 2), [.5,-.5,.25,-.25])

    def test_invalid_exposure_rejected(self):
        with self.assertRaises(ValueError):
            expose(np.zeros((2,2,3)), 0)
        for ev in (float('nan'), 4):
            with self.assertRaises(ValueError):
                expose(np.zeros((2,2,3), np.uint8), ev)
        with self.assertRaises(ValueError):
            exposure_stops('unknown', 0)

    def test_seed_changes_near_obstacles_within_declared_bounds(self):
        base = np.array(near_obstacle_positions())
        a = {'schema_version':1,'seed':12,'world':{'near_obstacle_jitter_m':.8}}
        b = {**a, 'seed':13}
        positions = near_obstacle_positions(a)
        np.testing.assert_array_equal(positions, near_obstacle_positions(a))
        self.assertFalse(np.array_equal(positions, near_obstacle_positions(b)))
        self.assertTrue(np.all(np.abs(np.asarray(positions)-base) <= .8))
        # A building-only change must not consume the obstacle random stream.
        changed = {**a, 'world':{**a['world'], 'building_spacing_m':12}}
        np.testing.assert_array_equal(positions, near_obstacle_positions(changed))
        np.testing.assert_array_equal(base, near_obstacle_positions({'schema_version':1,'seed':12}))

    def test_invalid_obstacle_jitter_rejected(self):
        with self.assertRaises(ValueError):
            validate({'schema_version':1,'world':{'near_obstacle_jitter_m':1}})

    def test_convergence_pair_preserves_direct_truth(self):
        root = ROOT/'tests/data/stitch_resolution_v1'
        self.assertTrue(validate_pair(root/'256', root/'512', (256,512))['direct_truth_bit_identical'])
        with self.assertRaises(ValueError):
            validate_pair(root/'256', root/'512', (512,256))

    def test_locked_capture_rejects_static_layout_or_wrong_time(self):
        recipe = {'schema_version':1,'seed':12,'world':{'near_obstacle_jitter_m':.8}}
        expected = {'face_size':256,'frames':2,'frame_step':6,'width':320,'height':180}
        meta = {'scenario_recipe':validate(recipe), 'near_obstacles':near_obstacle_positions(recipe),
                'face_size':256,'frames':[{'scenario_timestamp_ns':str(round(i*6*1e9/30)),
                                          'T_world_from_vehicle':vehicle_pose(i*6).tolist()} for i in range(2)]}
        cfg = {'output':{'width':320,'height':180}}
        validate_capture(meta, recipe, expected, cfg)
        meta['near_obstacles'] = near_obstacle_positions()
        with self.assertRaisesRegex(ValueError, 'near obstacles'):
            validate_capture(meta, recipe, expected, cfg)
        meta['near_obstacles'] = near_obstacle_positions(recipe)
        meta['frames'][1]['scenario_timestamp_ns'] = '1'
        with self.assertRaisesRegex(ValueError, 'trajectory/timestamps'):
            validate_capture(meta, recipe, expected, cfg)



class MaskFollowupTests(unittest.TestCase):
    def test_rgb8_subtraction_does_not_wrap_and_counts_changed_pixels(self):
        with tempfile.TemporaryDirectory() as directory:
            old, new = Path(directory)/'old', Path(directory)/'new'
            old.mkdir()
            new.mkdir()
            before = np.array([[[200, 10, 20], [0, 0, 0]]], dtype=np.uint8)
            after = before.copy()
            after[0, 0, 0] = 190
            name = 'dome_floor_multi_band_0000.png'
            Image.fromarray(before).save(old/name)
            Image.fromarray(after).save(new/name)
            result = compare_rgb(old, new, 1)['multi_band']
            self.assertEqual(result['max_rgb8'], 10)
            self.assertEqual(result['mean_abs_rgb8'], 10/6)
            self.assertEqual(result['changed_pixel_percent'], 50)

    def test_missing_case_cannot_publish_successful_comparison(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                compare_rgb(root, root, 1)

    def test_unexpected_change_in_unaffected_mode_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            old, new = Path(directory)/'old', Path(directory)/'new'
            old.mkdir()
            new.mkdir()
            name = 'plane_edge_feather_0000.png'
            Image.fromarray(np.zeros((2, 2, 3), dtype=np.uint8)).save(old/name)
            Image.fromarray(np.ones((2, 2, 3), dtype=np.uint8)).save(new/name)
            with self.assertRaisesRegex(ValueError, 'nonpyramid'):
                compare_rgb(old, new, 1)

    def test_legacy_hash_layout_is_accepted_but_changed_inputs_are_not(self):
        before = {'input_sha256': {'config.json': 'a', 'capture.json': 'b'},
                  'classifier_control': [], 'results': []}
        after = {'fusion_implementation': 'validity_zero_extension_v2',
                 'input_sha256': {'dataset': {'config.json': 'a'},
                                  'capture': {'capture.json': 'b'}},
                 'classifier_control': [], 'results': []}
        validate_inputs('static_object', before, after)
        changed = copy.deepcopy(after)
        changed['input_sha256']['dataset']['config.json'] = 'tampered'
        with self.assertRaisesRegex(ValueError, 'inputs changed'):
            validate_inputs('static_object', before, changed)


if __name__ == '__main__':
    unittest.main()
