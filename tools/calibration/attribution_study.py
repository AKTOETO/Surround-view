"""Compare full-Jacobian predictions with signed joint pose/intrinsic refits."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import sys

import numpy as np
import scipy
from scipy.spatial.transform import Rotation

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from calibration.information_study import CAPTURE, DIAGNOSTIC, _objects
from calibration.joint_information import solve_joint
from calibration.raster_study import digest
from calibration.sensitivity import DIRECTIONS, direction_field

ROOT = Path(__file__).resolve().parents[2]
SENSITIVITY = ROOT / 'artifacts/calibration-sensitivity-v1/summary.json'
BASELINE_JOINT = ROOT / 'artifacts/calibration-joint-information-v7/summary.json'
PROTOCOL = ROOT / 'docs/research/CALIBRATION_COMPONENT_ATTRIBUTION_PROTOCOL.md'
AMPLITUDE = .05
CAMERA_SCALE = np.array([300., 295., 640., 480., 1., 1., 1., 1.])


def _case_entry(summary, family, seed):
    return next(row for row in summary['results']
                if row['family'] == family and row['seed'] == seed)


def _camera_q(estimate, baseline, intrinsic_count):
    distortion_count = intrinsic_count - 4
    values = np.array([estimate['fx'], estimate['fy'], estimate['cx'], estimate['cy'],
                       *estimate['k'][:distortion_count]])
    base = np.array([baseline['fx'], baseline['fy'], baseline['cx'], baseline['cy'],
                     *baseline['k'][:distortion_count]])
    return (values - base) / CAMERA_SCALE[:len(values)]


def _full_q(fit, baseline_fit):
    camera = _camera_q(fit.estimate, baseline_fit.estimate, fit.intrinsics_count)
    pose = []
    for (rotation, translation), (base_rotation, base_translation) in zip(fit.poses, baseline_fit.poses):
        pose.extend(Rotation.from_matrix(rotation).as_rotvec() -
                    Rotation.from_matrix(base_rotation).as_rotvec())
        pose.extend(translation - base_translation)
    return np.r_[camera, np.asarray(pose)]


def _partition(vector, amplitude, intrinsic_count):
    intrinsic = vector[:intrinsic_count]
    pose = vector[intrinsic_count:]
    rotation = pose.reshape(-1, 6)[:, :3]
    translation = pose.reshape(-1, 6)[:, 3:]
    intrinsic_norm = float(np.linalg.norm(intrinsic))
    pose_norm = float(np.linalg.norm(pose))
    total = intrinsic_norm**2 + pose_norm**2
    return {'intrinsics_scaled_delta_per_input_px': (intrinsic / amplitude).tolist(),
            'intrinsics_scaled_l2_per_input_px': intrinsic_norm / amplitude,
            'pose_scaled_l2_per_input_px': pose_norm / amplitude,
            'intrinsics_squared_share': intrinsic_norm**2 / total if total > 0 else None,
            'pose_squared_share': pose_norm**2 / total if total > 0 else None,
            'pose_rotation_rms_deg_per_input_px': float(np.sqrt(np.mean(rotation**2)) * 180 / np.pi / amplitude),
            'pose_rotation_max_deg_per_input_px': float(np.max(np.abs(rotation)) * 180 / np.pi / amplitude),
            'pose_translation_rms_mm_per_input_px': float(np.sqrt(np.mean(translation**2)) * 1000 / amplitude),
            'pose_translation_max_mm_per_input_px': float(np.max(np.abs(translation)) * 1000 / amplitude)}


def study(output, capture_path=CAPTURE, diagnostic_path=DIAGNOSTIC,
          sensitivity_path=SENSITIVITY, baseline_joint_path=BASELINE_JOINT):
    output = Path(output)
    input_paths = [Path(capture_path), Path(diagnostic_path), Path(sensitivity_path), Path(baseline_joint_path)]
    input_hashes = {str(path): digest(path) for path in input_paths}
    capture = json.loads(input_paths[0].read_text())
    diagnostic = json.loads(input_paths[1].read_text())
    sensitivity = json.loads(input_paths[2].read_text())
    baseline_joint = json.loads(input_paths[3].read_text())
    if (capture['experiment'] != 'E-CAL-capture-01' or
            diagnostic['experiment'] != 'E-CAL-diagnostic-01' or
            sensitivity['experiment'] != 'E-CAL-sensitivity-01' or
            baseline_joint['experiment'] != 'E-CAL-information-01'):
        raise ValueError('frozen E-CAL source summaries required')
    if diagnostic['parent_summary_sha256'] != input_hashes[str(input_paths[0])]:
        raise ValueError('capture and diagnostic summaries do not match')
    if baseline_joint['capture_summary_sha256'] != input_hashes[str(input_paths[0])]:
        raise ValueError('joint baseline uses a different capture summary')
    code = ['tools/calibration/attribution_study.py', 'tools/calibration/information_study.py',
            'tools/calibration/joint_information.py', 'tools/calibration/sensitivity.py',
            'tools/calibration/optical_models.py', 'tools/calibration/raster_study.py',
            'docs/research/CALIBRATION_COMPONENT_ATTRIBUTION_PROTOCOL.md']
    code_hashes = {name: digest(ROOT / name) for name in code}
    output.mkdir(parents=True, exist_ok=False)
    results, dataset_hashes, field_hashes = [], {}, {}
    for sensitivity_case in sensitivity['results']:
        family, seed, profile, order = (sensitivity_case[k] for k in ('family', 'seed', 'profile', 'order'))
        case_id = f'{family}-{seed}-{profile}-order{order}'
        data_dir = ROOT / 'artifacts/calibration-diagnostic-v2' / f'{family}-{seed}'
        dataset_path = data_dir / f'{profile}-analytic_truth.json'
        expected_dataset_hash = _case_entry(diagnostic, family, seed)['profiles']
        expected_profile = next(row for row in expected_dataset_hash if row['profile'] == profile)
        expected = expected_profile['dataset_sha256']['analytic_truth']
        if digest(dataset_path) != expected:
            raise ValueError('exact observations changed: ' + str(dataset_path))
        dataset_hashes[str(dataset_path.relative_to(ROOT))] = expected
        dataset = json.loads(dataset_path.read_text())
        uv = np.asarray([view['uv_px'] for view in dataset['train']], float)
        objects = _objects(dataset)
        capture_case = _case_entry(capture, family, seed)
        parent_profile = next(row for row in capture_case['profiles'] if row['profile'] == profile)
        parent_fit = next(row for row in parent_profile['fits'] if row['order'] == order)
        geometry = capture_case['geometry']
        pose_data = [geometry['central_train'][i] for i in range(4)] + [
            geometry[profile][i] for i in range(8)]
        initial_poses = [(Rotation.from_matrix(np.asarray(row['rotation'], float)).as_rotvec(),
                          np.asarray(row['translation'], float)) for row in pose_data]
        baseline_fit = solve_joint(objects, uv, initial_poses, parent_fit['central_gate']['estimate'], order)
        if not baseline_fit.success:
            raise RuntimeError(f'exact baseline failed for {case_id}: {baseline_fit.message}')
        baseline_joint_row = next(row for row in baseline_joint['results']
                                  if row['family'] == family and row['seed'] == seed and
                                  row['profile'] == profile and row['distortion_order'] == order and
                                  row['observation_source'] == 'analytic_truth')
        field_rows = []
        for direction, noise_seed in DIRECTIONS:
            field = direction_field(uv, direction, noise_seed)
            field_hash = hashlib.sha256(np.asarray(field, dtype='<f8').tobytes(order='C')).hexdigest()
            field_hashes[f'{case_id}/{direction}/{noise_seed}'] = field_hash
            pair = {'direction': direction, 'noise_seed': noise_seed,
                    'amplitude_px': AMPLITUDE, 'field_sha256_le_f64': field_hash}
            actual_by_sign = {}
            for sign, multiplier in (('positive', 1.), ('negative', -1.)):
                observed = uv + multiplier * AMPLITUDE * field
                refit_poses = [(Rotation.from_matrix(rotation).as_rotvec(), translation)
                               for rotation, translation in baseline_fit.poses]
                fit = solve_joint(objects, observed, refit_poses,
                                  baseline_fit.estimate, order)
                delta_q = _full_q(fit, baseline_fit)
                actual_by_sign[sign] = delta_q
                pair[sign] = {'success': fit.success, 'status': fit.status,
                              'message': fit.message, 'cost': fit.cost,
                              'residual_rms_px': float(np.sqrt(np.mean(fit.residuals**2))),
                              'function_evaluations': fit.evaluations,
                              'joint_estimate': fit.estimate,
                              'component_response': _partition(delta_q, AMPLITUDE,
                                                               baseline_fit.intrinsics_count)}
            signed = (actual_by_sign['positive'] - actual_by_sign['negative']) / (2 * AMPLITUDE)
            even = (actual_by_sign['positive'] + actual_by_sign['negative']) / (2 * AMPLITUDE)
            linear = np.linalg.lstsq(baseline_fit.jacobian, field.ravel(), rcond=None)[0]
            denom = float(np.linalg.norm(linear))
            pair['linearized_prediction'] = _partition(linear * AMPLITUDE, AMPLITUDE,
                                                        baseline_fit.intrinsics_count)
            pair['actual_symmetric_derivative'] = _partition(signed * AMPLITUDE, AMPLITUDE,
                                                             baseline_fit.intrinsics_count)
            pair['actual_even_component'] = _partition(even * AMPLITUDE, AMPLITUDE,
                                                       baseline_fit.intrinsics_count)
            pair['linear_vs_actual_relative_l2_error'] = (
                float(np.linalg.norm(signed - linear) / denom) if denom > 0 else None)
            pair['linearized_residual_norm_px'] = float(np.linalg.norm(
                baseline_fit.jacobian @ linear - field.ravel()))
            field_rows.append(pair)
            print(f'{case_id}/{direction}/{noise_seed}: '
                  f"plus={pair['positive']['success']}, minus={pair['negative']['success']}, "
                  f"linear error={pair['linear_vs_actual_relative_l2_error']:.4g}", flush=True)
        results.append({'case_id': case_id, 'family': family, 'seed': seed,
                        'profile': profile, 'distortion_order': order,
                        'baseline_joint_estimate': baseline_fit.estimate,
                        'baseline_fit': {'success': baseline_fit.success, 'cost': baseline_fit.cost,
                                         'optimality': baseline_fit.optimality,
                                         'function_evaluations': baseline_fit.evaluations,
                                         'jacobian_sha256_le_f64': hashlib.sha256(
                                             np.asarray(baseline_fit.jacobian, dtype='<f8').tobytes()).hexdigest(),
                                         'singular_values': baseline_joint_row['singular_values'],
                                         'condition_number': baseline_joint_row['condition_number']},
                        'directions': field_rows})
    if code_hashes != {name: digest(ROOT / name) for name in code}:
        raise RuntimeError('analysis source changed during study')
    if input_hashes != {str(path): digest(path) for path in input_paths}:
        raise RuntimeError('input summary changed during study')
    report = {'schema_version': 1, 'experiment': 'E-CAL-attribution-01',
              'protocol': str(PROTOCOL.relative_to(ROOT)),
              'capture_sha256': input_hashes[str(input_paths[0])],
              'diagnostic_sha256': input_hashes[str(input_paths[1])],
              'sensitivity_sha256': input_hashes[str(input_paths[2])],
              'joint_baseline_sha256': input_hashes[str(input_paths[3])],
              'runtime': {'python': platform.python_version(), 'numpy': np.__version__,
                          'scipy': scipy.__version__, 'platform': platform.platform()},
              'code_sha256': code_hashes, 'dataset_sha256': dataset_hashes,
              'field_sha256_le_f64': field_hashes, 'cases': len(results),
              'amplitude_px': AMPLITUDE, 'direction_fields_per_case': len(DIRECTIONS),
              'joint_refits': sum(1 for case in results for _ in case['directions'] for _ in ('positive', 'negative')),
              'converged_refits': sum(int(pair[sign]['success']) for case in results
                                      for pair in case['directions'] for sign in ('positive', 'negative')),
              'results': results,
              'limitations': ['reanalysis of reviewed synthetic captures, not independent scenes',
                              'six deterministic fields at one finite amplitude, not a noise distribution',
                              'pose/intrinsic partition depends on declared parameter scaling',
                              'no raster detector rerun, physical target geometry or production acceptance gate']}
    (output / 'summary.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--capture', type=Path, default=CAPTURE)
    parser.add_argument('--diagnostic', type=Path, default=DIAGNOSTIC)
    parser.add_argument('--sensitivity', type=Path, default=SENSITIVITY)
    parser.add_argument('--joint-baseline', type=Path, default=BASELINE_JOINT)
    args = parser.parse_args()
    study(args.output, args.capture, args.diagnostic, args.sensitivity, args.joint_baseline)
