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
from object_metrics import measure_target, measure_target_support, projected_object_ids, target_mask, spatial_rois, spatial_errors
from object_stitch import interleaved_orders, load_objects
from diagnostic_motion import frame_positions, position_for_capture, validate_captured_positions
from server_boundary import (quality as server_quality, timestamp as server_timestamp, audit_study,
    check_mesh_budget, validate_refinement_plan, refinement_difference, audit_refinement, audit_spatial)


class ObjectMetricTests(unittest.TestCase):
    def test_spatial_rejects_modified_pinned_baseline_before_reading_captures(self):
        plan = json.loads((ROOT/'configs/research/spatial-roi-plan.json').read_text())
        plan['baseline_sha256'] = '0'*64
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'plan.json'
            path.write_text(json.dumps(plan))
            with self.assertRaisesRegex(ValueError,'source baseline changed'):
                audit_spatial(path,Path(directory),Path(directory))

    def test_spatial_partition_uses_names_visibility_and_independent_boundaries(self):
        labels = np.ones((24,30),np.uint16)
        labels[:,15:] = 2
        labels[6:8,6:8] = 3
        labels[20:,0:4] = 4
        labels[0,0] = 0
        labels[0,5] = 5
        visible = np.ones(labels.shape,bool)
        visible[12,12] = False
        objects = {'SV road':1,'SV curb':2,'target':3,'ego':4,'SV road.001':5}
        groups = spatial_rois(labels,objects,[4],3,visible,['SV road'])
        counts = np.sum(list(groups.values()),axis=0)
        expected = (labels != 0) & (labels != 4) & visible
        np.testing.assert_array_equal(counts,expected.astype(int))
        self.assertTrue(groups['ground_interior'][12,8])
        self.assertTrue(groups['other_scene_interior'][12,22])
        self.assertTrue(groups['ground_boundary'][12,14])
        self.assertTrue(groups['other_scene_boundary'][12,15])
        self.assertTrue(groups['ground_boundary'][0,5])
        self.assertEqual(groups['coded_target_interior'].sum(),0)
        self.assertEqual(groups['coded_target_boundary'].sum(),4)
        wrong = labels.copy();wrong[12,12] = 99
        with self.assertRaises(ValueError):
            spatial_rois(wrong,objects,[4],3,visible,['SV road'])
        with self.assertRaises(ValueError):
            spatial_rois(labels,objects,[4],3,visible,['SV road','target'])

    def test_spatial_errors_known_weighting_empty_and_overlapping_strata(self):
        error = np.zeros((2,2,3))
        error[0,0] = 1
        a = np.array([[True,True],[False,False]])
        b = ~a
        result = spatial_errors(error,dict(a=a,b=b,empty=np.zeros_like(a)))
        self.assertEqual(result['a']['linear_mae'],.5)
        self.assertEqual(result['a']['linear_channel_p95'],1)
        self.assertEqual(result['a']['linear_channel_max'],1)
        self.assertIsNone(result['empty']['linear_mae'])
        self.assertEqual(result['empty']['pixels'],0)
        weighted = sum(g['pixels']*g['linear_mae'] for g in result.values() if g['pixels'])/4
        self.assertEqual(weighted,error.mean())
        with self.assertRaisesRegex(ValueError,'disjoint'):
            spatial_errors(error,dict(a=a,b=a))
        with self.assertRaises(ValueError):
            spatial_errors(error,dict(a=a.astype(float)))

    def test_refinement_plan_rejects_changed_shape_and_nonincreasing_axes(self):
        master = json.loads((ROOT/'configs/research/carrier-refinement-plan.json').read_text())
        validate_refinement_plan(master)
        wrong = json.loads(json.dumps(master))
        wrong['levels'][2]['plan']['carriers'][1]['surface']['corner_height_m'] = 2
        with self.assertRaisesRegex(ValueError,'physical carrier shape'):
            validate_refinement_plan(wrong)
        wrong = json.loads(json.dumps(master))
        wrong['levels'][2]['plan']['carriers'][0]['surface']['uniform_cells'][0] = 32
        with self.assertRaisesRegex(ValueError,'strictly increase'):
            validate_refinement_plan(wrong)
        wrong = json.loads(json.dumps(master))
        wrong['levels'][0]['plan']['output'] = [640,360]
        with self.assertRaisesRegex(ValueError,'inputs/scenario/output'):
            validate_refinement_plan(wrong)
        wrong = json.loads(json.dumps(master))
        wrong['levels'][2]['plan']['carriers'][0]['surface']['uniform_cells'] = [64]
        with self.assertRaisesRegex(ValueError,'strictly increase'):
            validate_refinement_plan(wrong)

    def test_refinement_rejects_changed_master_before_loading_results(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'provenance.json').write_text(json.dumps(dict(master_plan_sha256='0'*64)))
            with self.assertRaisesRegex(ValueError,'master plan changed'):
                audit_refinement(ROOT/'configs/research/carrier-refinement-plan.json',root,root)

    def test_refinement_pixel_metric_known_answer_and_mask(self):
        reference = np.zeros((2,2,4),np.uint8)
        reference[...,3] = 255
        actual = reference.copy()
        actual[0,0,:3] = 255
        actual[1,1,0] = 255
        roi = np.array([[True,True],[False,False]])
        measured = refinement_difference(actual,reference,roi)
        self.assertEqual(measured['roi_pixels'],2)
        self.assertEqual(measured['full_frame_changed_pixel_fraction'],.5)
        self.assertEqual(measured['roi_changed_pixel_fraction'],.5)
        self.assertEqual(measured['roi_max_rgb8_channel_delta'],255)
        self.assertEqual(measured['roi_linear_mae_to_fine'],.5)
        self.assertEqual(refinement_difference(reference,reference,roi)['roi_linear_mae_to_fine'],0)
        for bad in (roi.astype(float),np.zeros_like(roi)):
            with self.assertRaises(ValueError):
                refinement_difference(actual,reference,bad)
        with self.assertRaises(ValueError):
            refinement_difference(actual.astype(float),reference,roi)
        actual[0,1,3] = 0
        with self.assertRaises(ValueError):
            refinement_difference(actual,reference,roi)

    def test_budget_audit_rejects_retained_buffers_and_false_counts(self):
        plan = dict(triangles_min=32, triangles_max=32, vertices_max=25, buffer_bytes_max=684)
        active = dict(vertices=25,indices=96,triangles=32,vertex_buffer_bytes=300,
                      index_buffer_bytes=384,buffer_bytes=684)
        metadata = dict(mesh_triangles=32, mesh_resources=dict(scope='carrier_position_index_buffers',
                        active=active,resident=dict(active)))
        self.assertEqual(check_mesh_budget(metadata,plan),active)
        metadata['mesh_resources']['resident']['buffer_bytes'] += 12
        with self.assertRaisesRegex(ValueError,'inactive resident'):
            check_mesh_budget(metadata,plan)
        metadata['mesh_resources']['resident'] = dict(active)
        for field in active:
            wrong = json.loads(json.dumps(metadata))
            wrong['mesh_resources']['active'][field] += 1
            wrong['mesh_resources']['resident'] = dict(wrong['mesh_resources']['active'])
            with self.assertRaises(ValueError):
                check_mesh_budget(wrong,plan)
        with self.assertRaisesRegex(ValueError,'budget violation'):
            check_mesh_budget(metadata,dict(plan,vertices_max=24))

    def test_study_rejects_changed_freeze_before_reading_results(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            plan = root/'plan.json'
            plan.write_bytes((ROOT/'assets/scenarios/seam-generalization-v1.json').read_bytes())
            (root/'provenance.json').write_text(json.dumps(dict(plan_sha256='0'*64,
                                                              generator_sha256={})))
            with self.assertRaisesRegex(ValueError,'plan hash mismatch'):
                audit_study(plan,root)

    def setUp(self):
        self.truth = np.zeros((32,48), bool)
        self.truth[8:24,8:12] = True

    def test_server_quality_known_rgb_errors_and_joined_copy(self):
        roi = np.ones(self.truth.shape, bool)
        rgb = np.full((*self.truth.shape,3), .2)
        rgb[self.truth] = [1,0,1]
        exact = server_quality(rgb,rgb,roi,self.truth,.15,8)
        self.assertEqual(exact['linear_mae'],0)
        self.assertEqual(exact['srgb_mae'],0)
        self.assertEqual(exact['target']['iou'],1)
        self.assertEqual(exact['target']['false_positive_pixels'],0)
        copied = rgb.copy()
        copied[8:24,12:16] = [1,0,1]
        joined = server_quality(copied,rgb,roi,self.truth,.15,8)
        self.assertEqual(joined['target']['components'],1)
        self.assertEqual(joined['target']['false_positive_pixels'],64)
        self.assertEqual(joined['target']['iou'],.5)
        white, black = np.ones_like(rgb),np.zeros_like(rgb)
        error = server_quality(black,white,roi,self.truth,.15,8)
        self.assertEqual(error['linear_mae'],1)
        self.assertEqual(error['linear_channel_error_p95'],1)
        with self.assertRaises(ValueError):
            server_quality(rgb,rgb,np.zeros_like(roi),self.truth,.15,8)

    def test_server_timestamp_requires_synchronized_ordered_inputs(self):
        inputs = [dict(camera_id=c,used=True,source_timestamp_ns='200',source_clock_domain='scenario') for c in range(4)]
        self.assertEqual(server_timestamp({'inputs':inputs}),'200')
        inputs[3]['source_timestamp_ns'] = '100'
        with self.assertRaises(ValueError):
            server_timestamp({'inputs':inputs})
        inputs[3]['source_timestamp_ns'] = '200'
        with self.assertRaises(ValueError):
            server_timestamp({'inputs':list(reversed(inputs))})

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
