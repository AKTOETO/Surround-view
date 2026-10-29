"""Scientific fixture invariants; no live Blender, server, or historical artifacts required."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'docs/diploma'))
from plot_thesis_e3 import evaluation_mask, input_guard, checked
from plot_thesis_e2 import verify_sample
from plot_thesis_performance import distribution, read_telemetry


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


class E2NativeAuditTests(unittest.TestCase):
    def test_independent_metrics_and_guard_failures(self):
        import hashlib
        with tempfile.TemporaryDirectory() as directory:
            branch = Path(directory)
            rgba = np.full((180, 320, 4), 20, np.uint8)
            rgba[..., 3] = 255
            raw = branch / 'frame.rgba'
            raw.write_bytes(rgba.tobytes())
            inputs = [dict(camera_id=i) for i in range(4)]
            surface = dict(type='rectangular_bowl_v1', corner_height_m=0)
            variant = dict(mode='hard_best_angle', surface=surface)
            sample = dict(warmup=False, rgba_file='frame.rgba',
                rgba_sha256=hashlib.sha256(rgba.tobytes()).hexdigest(),
                metadata=dict(inputs=[dict(x, used=True) for x in inputs], view={}, health='READY',
                    paused=True, source_type='replay', width=320, height=180, pixel_format='RGBA8',
                    row_origin='top_left', mesh_triangles=3196, fusion_mode='hard_best_angle',
                    surface=surface, mesh_resources=dict(active=dict(buffer_bytes=58512, vertices=1680))),
                quality=dict(selected_pixels=57600, exact_match=False, mae_rgb8=10., rmse_rgb8=10.,
                    max_absolute_rgb8=10, psnr_db=float(10*np.log10(255.**2/100))))
            truth, mask = np.full((180, 320, 3), 10, np.uint8), np.ones((180, 320), bool)
            rgb, difference = verify_sample(branch, sample, inputs, {}, variant, truth, mask)
            np.testing.assert_array_equal(rgb, rgba[..., :3])
            self.assertEqual(difference, 0.)
            for fault in ('inputs', 'budget', 'vertices', 'surface', 'metric', 'hash'):
                invalid = copy.deepcopy(sample)
                if fault == 'inputs':
                    invalid['metadata']['inputs'][0]['camera_id'] = 9
                elif fault == 'budget':
                    invalid['metadata']['mesh_resources']['active']['buffer_bytes'] = 60501
                elif fault == 'vertices':
                    invalid['metadata']['mesh_resources']['active']['vertices'] = 1811
                elif fault == 'surface':
                    invalid['metadata']['surface']['corner_height_m'] = 1
                elif fault == 'metric':
                    invalid['quality']['mae_rgb8'] = 9.
                else:
                    invalid['rgba_sha256'] = '0' * 64
                with self.subTest(fault=fault), self.assertRaises(ValueError):
                    verify_sample(branch, invalid, inputs, {}, variant, truth, mask)


class ThesisPerformanceAuditTests(unittest.TestCase):
    def test_nearest_rank_tail_and_invalid_samples(self):
        self.assertEqual(distribution([*range(1, 31), None]),
                         dict(count=30, p50=15., p95=29., p99=30., maximum=30.))
        for values in ([], [None], [float('nan')], [float('inf')], [-1.]):
            with self.subTest(values=values), self.assertRaises(ValueError):
                distribution(values)

    def test_telemetry_snapshot_bound_and_loss_rejection(self):
        import json
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'catalog.json').write_text(json.dumps(dict(accepted=True,
                telemetry_catalog=dict(server_instance_id='instance', epoch='0'))))
            def sample(cursor, stamp, frame):
                return dict(cursor=str(cursor), timestamp_ns=str(stamp), frame=dict(frame_id=frame),
                    paused=True, process=dict(cpu_percent=10. * cursor, rss_bytes=str(100 * 2**20)),
                    collection_wall_ms=.01)
            for page, after, samples in ((1, 0, [sample(1, 50, 'old'), sample(2, 100, 'a')]),
                                         (2, 2, [sample(3, 200, 'b'), sample(4, 250, 'late'),
                                                  sample(5, 300, 'late')])):
                (root / f'telemetry-{page}.json').write_text(json.dumps(dict(accepted=True,
                    telemetry=dict(server_instance_id='instance', epoch='0', enabled=True,
                        after_cursor=str(after), next_cursor=str(after + len(samples)),
                        latest_cursor='4' if page == 1 else '5', lost_samples='0', samples=samples))))
            report = dict(samples=[dict(warmup=False, metadata=dict(frame_id=frame,
                render_complete_timestamp_ns=str(stamp))) for frame, stamp in (('a', 100), ('b', 200))])
            result = read_telemetry(root, report)
            self.assertEqual(result['received_samples'], 4)
            self.assertEqual(result['selected_samples'], 2)
            self.assertEqual(result['cpu_one_core_percent']['maximum'], 30.)
            path = root / 'telemetry-2.json'
            page = json.loads(path.read_text())
            page['telemetry']['lost_samples'] = '1'
            path.write_text(json.dumps(page))
            with self.assertRaises(ValueError):
                read_telemetry(root, report)


if __name__ == '__main__':
    unittest.main()
