"""Equal-RMS direction controls, response units, invalid counts and native shift fit."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from calibration.optical_models import FAMILIES, metric_validation
from calibration.raster_board import BOARD, K, corners, poses
from calibration.sensitivity import DIRECTIONS, direction_field, output_controls, pair_response

BINARY = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / 'build/sv-calibrate')
sys.argv = sys.argv[:1]


def model(cx_shift=0., cy_shift=0.):
    return {'fx': 300., 'fy': 295., 'cx': 319.5+cx_shift, 'cy': 239.5+cy_shift,
            'k': [0., 0., 0., 0.], 'theta_max_rad': 1.}


class SensitivityTests(unittest.TestCase):
    def setUp(self):
        self.geometry = poses(7101)
        self.truth = np.asarray([corners(r, t) for r, t in self.geometry[:12]])

    def test_equal_rms_and_signed_amplitude(self):
        for name, seed in DIRECTIONS:
            field = direction_field(self.truth, name, seed)
            self.assertAlmostEqual(np.mean(np.sum(field**2, axis=-1)), 1.)
            for h in (.025, .05, .2):
                uv = self.truth + h*field
                self.assertAlmostEqual(np.sqrt(np.mean(np.sum((uv-self.truth)**2, axis=-1))), h)
                np.testing.assert_allclose((uv+(self.truth-h*field))/2, self.truth, atol=1e-13)

    def test_radial_tangential_orthogonality_and_orientation(self):
        radial = direction_field(self.truth, 'radial')
        tangent = direction_field(self.truth, 'tangential')
        np.testing.assert_allclose(np.sum(radial*tangent, axis=-1), 0., atol=1e-14)
        np.testing.assert_allclose(tangent[..., 0], -radial[..., 1])
        np.testing.assert_allclose(tangent[..., 1], radial[..., 0])
        delta = self.truth-K[:2, 2]
        self.assertTrue(np.all(np.sum(delta*radial, axis=-1) > 0))

    def test_random_reproducibility_and_per_view_zero_mean(self):
        a = direction_field(self.truth, 'random', 1021)
        np.testing.assert_array_equal(a, direction_field(self.truth, 'random', 1021))
        self.assertFalse(np.array_equal(a, direction_field(self.truth, 'random', 1022)))
        np.testing.assert_allclose(a.mean(axis=1), 0., atol=1e-15)

    def test_shift_x_known_output_gain_and_parameter_units(self):
        for h in (.025, .05, .2):
            response = pair_response(model(h), model(-h), model(), h, FAMILIES[0])
            self.assertAlmostEqual(response['outer_gain_px_per_px']['p95'], 1., places=10)
            self.assertLess(response['outer_even_px_per_px2']['max'], 1e-8)
            np.testing.assert_allclose(response['parameter_derivative_per_px'], [0, 0, 1, 0, 0, 0, 0, 0], atol=1e-10)
            self.assertAlmostEqual(response['scaled_parameter_derivative_l2_per_px'], 1/640)
            self.assertGreater(response['floor_gain_m_per_px']['p95'], 0.)
            self.assertEqual(response['invalid_floor_points'], 0)

    def test_control_domain_matches_existing_floor_validation(self):
        estimate = model(.1)
        _, recovered, valid = output_controls(estimate, FAMILIES[0])
        result = metric_validation(estimate, K, FAMILIES[0], self.geometry[12:])
        self.assertEqual(len(recovered), result['floor_points'])
        self.assertEqual(int((~valid).sum()), result['invalid_floor_points'])
        _, recovered, valid = output_controls(model(), FAMILIES[0])
        self.assertTrue(valid.all())
        self.assertLess(np.max(np.abs(recovered[:, 1]-1.2)), 1e-12)
        self.assertTrue(np.all(np.isfinite(recovered)))
        self.assertLess(np.linalg.norm(recovered-[0., 1.2, 8.], axis=1).min(), 1e-9)

    def test_invalid_controls_are_missing_not_zero(self):
        result = pair_response(model(cy_shift=10000.), model(), model(), .1, FAMILIES[0])
        self.assertEqual(result['invalid_floor_points'], result['floor_points'])
        self.assertEqual(result['floor_gain_m_per_px']['count'], 0)
        self.assertIsNone(result['floor_gain_m_per_px']['p95'])
        for truth, name in [(self.truth[:11], 'radial'), (self.truth*np.nan, 'random'),
                            (np.tile(K[:2, 2], (12, 54, 1)), 'radial'), (self.truth, 'unknown')]:
            with self.assertRaises(ValueError):
                direction_field(truth, name)
        with self.assertRaises(ValueError):
            pair_response(model(), model(), model(), 0., FAMILIES[0])

    def test_native_controlled_origin_and_shift_cx(self):
        views = [{'id': f'view-{i}', 'uv_px': corners(r, t).tolist()} for i, (r, t) in enumerate(self.geometry)]
        for v in views[:12]:
            v['uv_px'] = (np.asarray(v['uv_px'])+[.2, 0.]).tolist()
        data = {'schema_version': 1, 'purpose': 'intrinsic_solver_diagnostic',
                'observation_origin': 'controlled_perturbation', 'resolution': {'width': 640, 'height': 480},
                'board': BOARD, 'theta_max_rad': 1., 'train': views[:12], 'validation': views[12:]}
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            (folder/'data.json').write_text(json.dumps(data))
            process = subprocess.run([BINARY, 'diagnose-intrinsics', '--dataset', str(folder/'data.json'),
                                      '--output', str(folder/'output')], capture_output=True, text=True, timeout=30)
            self.assertEqual(process.returncode, 0, process.stderr)
            report = json.loads((folder/'output/diagnostics.json').read_text())
            self.assertEqual(report['status'], 'diagnostic_only')
            self.assertEqual(report['observation_origin'], 'controlled_perturbation')
            self.assertAlmostEqual(report['estimate']['cx'], K[0, 2]+.2, delta=1e-4)
            self.assertFalse((folder/'output/intrinsics.json').exists())


if __name__ == '__main__':
    unittest.main()
