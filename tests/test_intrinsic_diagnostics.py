"""Diagnostic-only CLI: exact recovery, strict inputs, no deployable model export."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from calibration.raster_board import BOARD, K, SIZE, corners, poses
from calibration.detector_errors import aligned_truth, signed_localization

BINARY = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / 'build/sv-calibrate')
sys.argv = sys.argv[:1]


def dataset():
    views = [{'id': f'view-{i}', 'uv_px': corners(r, t).tolist()} for i, (r, t) in enumerate(poses(7101))]
    return {'schema_version': 1, 'purpose': 'intrinsic_solver_diagnostic',
            'observation_origin': 'analytic_truth', 'board': BOARD,
            'resolution': {'width': SIZE[0], 'height': SIZE[1]}, 'theta_max_rad': 1.,
            'train': views[:12], 'validation': views[12:]}


class IntrinsicDiagnosticTests(unittest.TestCase):
    def test_signed_radial_tangential_known_answer_and_reversal(self):
        truth = np.column_stack([K[0, 2] + np.linspace(10., 100., 54), np.full(54, K[1, 2])])
        detected = truth + [.2, -.3]
        for reverse in (False, True):
            observed = detected[::-1] if reverse else detected
            result = signed_localization(observed, truth)
            self.assertEqual(result['reversed_indexing'], reverse)
            np.testing.assert_allclose(result['mean_uv_px'], [.2, -.3], atol=1e-12)
            self.assertAlmostEqual(result['radial_px']['mean'], .2)
            self.assertAlmostEqual(result['tangential_px']['mean'], -.3)
            self.assertAlmostEqual(result['rmse_px'], np.hypot(.2, .3))
            np.testing.assert_allclose(aligned_truth(observed, truth), truth[::-1] if reverse else truth)

    def test_optical_axis_and_invalid_localization(self):
        truth = np.tile(K[:2, 2], (54, 1))
        result = signed_localization(truth, truth)
        self.assertEqual(result['radial_tangential_count'], 0)
        self.assertIsNone(result['radial_px'])
        self.assertEqual(result['rmse_px'], 0.)
        for invalid in (np.full((54, 2), np.nan), np.zeros((53, 2))):
            with self.assertRaises(ValueError):
                aligned_truth(invalid, truth)

    def invoke(self, folder, data, extra=()):
        path = folder / 'data.json'
        path.write_text(json.dumps(data))
        process = subprocess.run([BINARY, 'diagnose-intrinsics', '--dataset', str(path),
                                  '--output', str(folder / 'output'), *extra],
                                 capture_output=True, text=True, timeout=30)
        return process

    def test_exact_recovery_and_explicit_nonacceptance(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            result = self.invoke(folder, dataset())
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads((folder / 'output/diagnostics.json').read_text())
            self.assertEqual(report['status'], 'diagnostic_only')
            self.assertEqual(report['observation_origin'], 'analytic_truth')
            self.assertEqual(report['dataset_sha256'], hashlib.sha256((folder / 'data.json').read_bytes()).hexdigest())
            self.assertLess(report['training_rmse_px'], 1e-5)
            self.assertLess(report['validation_error_px']['p95'], 1e-4)
            for key, truth in [('fx', K[0, 0]), ('fy', K[1, 1]), ('cx', K[0, 2]), ('cy', K[1, 2])]:
                self.assertAlmostEqual(report['estimate'][key], truth, delta=.001)
            self.assertFalse((folder / 'output/intrinsics.json').exists())
            self.assertNotIn('threshold_p95_px', report)
            previous = (folder / 'output/diagnostics.json').read_bytes()
            again = self.invoke(folder, dataset())
            self.assertNotEqual(again.returncode, 0)
            self.assertEqual((folder / 'output/diagnostics.json').read_bytes(), previous)

    def test_two_coefficient_profile_and_float32_origin(self):
        data = dataset()
        data['observation_origin'] = 'analytic_truth_float32'
        for view in data['train']:
            view['uv_px'] = np.asarray(view['uv_px'], dtype=np.float32).astype(float).tolist()
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            result = self.invoke(folder, data, ('--distortion-order', '2'))
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads((folder / 'output/diagnostics.json').read_text())
            self.assertEqual(report['estimate']['k'][2:], [0., 0.])
            self.assertEqual(report['distortion_order'], 2)
            self.assertLess(report['validation_error_px']['p95'], .001)

    def test_invalid_observations_rejected_before_output(self):
        mutations = [
            lambda d: d.update(purpose='production'),
            lambda d: d.update(observation_origin='unknown'),
            lambda d: d['train'][0].update(id=''),
            lambda d: d['validation'][0].update(id=d['train'][0]['id']),
            lambda d: d.update(train=d['train'][:5]),
            lambda d: d.update(validation=d['validation'][:2]),
            lambda d: d['board'].update(columns=2147483648),
            lambda d: d['board'].update(square_size_m=-1.),
            lambda d: d['resolution'].update(width=0),
            lambda d: d.update(theta_max_rad=2.),
            lambda d: d['train'][0]['uv_px'].pop(),
            lambda d: d['train'][0]['uv_px'].__setitem__(0, [640., 0.]),
            lambda d: d['train'][0]['uv_px'].__setitem__(0, [-1., 0.]),
            lambda d: d['train'][0]['uv_px'].__setitem__(0, [float('nan'), 0.]),
            lambda d: d['train'][0]['uv_px'].__setitem__(0, [1., 2., 3.]),
        ]
        for index, mutation in enumerate(mutations):
            with self.subTest(index=index), tempfile.TemporaryDirectory() as name:
                data = copy.deepcopy(dataset())
                mutation(data)
                folder = Path(name)
                result = self.invoke(folder, data)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((folder / 'output').exists())

    def test_diagnostic_rejects_gate_override_and_invalid_arguments(self):
        for extra in [('--max-error-px', '100'), ('--distortion-order', '2bad'),
                      ('--distortion-order', '2', '--distortion-order', '4')]:
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as name:
                folder = Path(name)
                result = self.invoke(folder, dataset(), extra)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((folder / 'output').exists())


if __name__ == '__main__':
    unittest.main()
