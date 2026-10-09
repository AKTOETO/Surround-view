"""Joint camera/board-pose information analysis for the frozen E-CAL protocol."""
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
from calibration.joint_information import analyze_joint_fit, solve_joint
from calibration.optical_models import FAMILIES, metric_validation
from calibration.raster_study import digest

ROOT = Path(__file__).resolve().parents[2]
CAPTURE = ROOT / 'docs/validation/baselines/calibration_capture_v1.json'
DIAGNOSTIC = ROOT / 'artifacts/calibration-diagnostic-v2/summary.json'
PROTOCOL = ROOT / 'docs/research/CALIBRATION_JOINT_INFORMATION_PROTOCOL.md'


def _objects(dataset):
    board = dataset['board']
    columns, rows, square = board['columns'], board['rows'], board['square_size_m']
    x, y = np.meshgrid(np.arange(columns) * square - (columns - 1) * square / 2,
                       np.arange(rows) * square - (rows - 1) * square / 2)
    # The OpenCV detector normalizes the chessboard's symmetric 180-degree
    # ambiguity; the stored UV arrays already follow that reversed ordering.
    return np.column_stack([x.ravel(), y.ravel(), np.zeros(x.size)])[::-1].copy()


def _summary_entry(summary, family, seed):
    return next(row for row in summary['results']
                if row['family'] == family and row['seed'] == seed)


def study(output, capture_path=CAPTURE, diagnostic_path=DIAGNOSTIC):
    output, capture_path, diagnostic_path = map(Path, (output, capture_path, diagnostic_path))
    capture_hash, diagnostic_hash = digest(capture_path), digest(diagnostic_path)
    capture = json.loads(capture_path.read_text())
    diagnostic = json.loads(diagnostic_path.read_text())
    if capture['experiment'] != 'E-CAL-capture-01' or diagnostic['experiment'] != 'E-CAL-diagnostic-01':
        raise ValueError('frozen E-CAL-capture-01 and E-CAL-diagnostic-01 summaries required')
    if diagnostic['parent_summary_sha256'] != capture_hash:
        raise ValueError('diagnostic parent summary does not match capture summary')
    code = ['tools/calibration/information_study.py', 'tools/calibration/joint_information.py',
            'tools/calibration/optical_models.py', 'tools/calibration/raster_study.py',
            'src/vision/calibration.cpp', 'docs/research/CALIBRATION_JOINT_INFORMATION_PROTOCOL.md']
    code_hashes = {name: digest(ROOT / name) for name in code}
    for name in ('src/vision/calibration.cpp', 'tools/calibration/optical_models.py'):
        if code_hashes[name] != diagnostic['code_sha256'][name]:
            raise ValueError('parent numerical core changed: ' + name)
    output.mkdir(parents=True, exist_ok=False)
    results, data_hashes = [], {}
    for capture_case in capture['results']:
        family_name, seed = capture_case['family'], capture_case['seed']
        if family_name not in ('equidistant', 'kb_nonzero'):
            continue
        parent_case = _summary_entry(diagnostic, family_name, seed)
        family = next(item for item in FAMILIES if item.name == family_name)
        case_name = f'{family_name}-{seed}'
        case_dir = output / case_name
        case_dir.mkdir()
        by_profile = {row['profile']: row for row in capture_case['profiles']}
        diagnostic_profiles = {row['profile']: row for row in parent_case['profiles']}
        for profile_name in ('small_front', 'large_front'):
            capture_profile = by_profile[profile_name]
            parent_profile = diagnostic_profiles[profile_name]
            group_views = capture_case['geometry']
            train_pose_data = [group_views['central_train'][i] for i in range(4)] + [
                group_views[profile_name][i] for i in range(8)]
            poses = [(Rotation.from_matrix(np.asarray(row['rotation'], float)).as_rotvec(),
                      np.asarray(row['translation'], float))
                     for row in train_pose_data]
            validation_pose_data = [group_views['central_validation'][i] for i in range(4)] + [
                group_views['outer_validation'][i] for i in range(8)]
            validation_poses = [(np.asarray(row['rotation'], float), np.asarray(row['translation'], float))
                                for row in validation_pose_data]
            data_dir = ROOT / 'artifacts/calibration-diagnostic-v2' / case_name
            exact = json.loads((data_dir / f'{profile_name}-analytic_truth.json').read_text())
            detected = json.loads((data_dir / f'{profile_name}-detected_raster.json').read_text())
            objects = _objects(exact)
            truth_uv = np.asarray([view['uv_px'] for view in exact['train']], float)
            detected_uv = np.asarray([view['uv_px'] for view in detected['train']], float)
            if len(truth_uv) != 12 or len(detected_uv) != 12 or not np.allclose(
                    np.asarray([view['uv_px'] for view in exact['validation']]),
                    np.asarray([view['uv_px'] for view in detected['validation']])):
                raise ValueError(f'unmatched or incomplete observations: {case_name}/{profile_name}')
            detector_sigma = float(np.sqrt(np.mean(np.sum((detected_uv - truth_uv) ** 2, axis=2)) / 2))
            orders = (2, 4) if family_name == 'equidistant' else (4,)
            for order in orders:
                baseline_fit = next(row for row in capture_profile['fits'] if row['order'] == order)
                estimate = baseline_fit['central_gate']['estimate']
                for origin, observations in (('analytic_truth', truth_uv), ('detected_raster', detected_uv)):
                    fit = solve_joint(objects, observations, poses, estimate, order)
                    analysis = analyze_joint_fit(fit, detector_sigma, origin)
                    analysis['family'] = family_name
                    analysis['seed'] = seed
                    analysis['profile'] = profile_name
                    analysis['distortion_order'] = order
                    analysis['dataset_sha256'] = {
                        'exact': digest(data_dir / f'{profile_name}-analytic_truth.json'),
                        'detected': digest(data_dir / f'{profile_name}-detected_raster.json')}
                    data_hashes[f'{case_name}/{profile_name}-analytic_truth.json'] = analysis['dataset_sha256']['exact']
                    data_hashes[f'{case_name}/{profile_name}-detected_raster.json'] = analysis['dataset_sha256']['detected']
                    truth = np.array([300., 295., 319.5, 239.5])
                    actual = np.array([fit.estimate['fx'], fit.estimate['fy'],
                                       fit.estimate['cx'], fit.estimate['cy']])
                    analysis['intrinsic_delta_from_truth'] = (actual - truth).tolist()
                    parent = np.array([estimate['fx'], estimate['fy'], estimate['cx'], estimate['cy']])
                    analysis['intrinsic_delta_from_parent_estimate'] = (actual - parent).tolist()
                    analysis['validation_at_known_poses'] = metric_validation(
                        fit.estimate, np.array([[300., 0., 319.5], [0., 295., 239.5], [0., 0., 1.]]),
                        family, validation_poses)
                    analysis['jacobian_sha256_le_f64'] = hashlib.sha256(
                        np.asarray(fit.jacobian, dtype='<f8').tobytes(order='C')).hexdigest()
                    analysis['residual_norm_px'] = float(np.linalg.norm(fit.residuals))
                    analysis['residual_p95_px'] = float(np.percentile(np.abs(fit.residuals), 95))
                    results.append(analysis)
                    print(f'{case_name}/{profile_name}/order{order}/{origin}: '
                          f"{analysis['practical_rank_1e-10']}/{analysis['columns']} rank, "
                          f"success={fit.success}, nfev={fit.evaluations}", flush=True)
    if code_hashes != {name: digest(ROOT / name) for name in code}:
        raise RuntimeError('analysis source changed during study')
    if digest(capture_path) != capture_hash or digest(diagnostic_path) != diagnostic_hash:
        raise RuntimeError('parent summary changed during study')
    report = {'schema_version': 1, 'experiment': 'E-CAL-information-01',
              'protocol': str(PROTOCOL.relative_to(ROOT)), 'capture_summary_sha256': capture_hash,
              'diagnostic_summary_sha256': diagnostic_hash,
              'parent_binary_sha256': diagnostic['parent_binary_sha256'],
              'production_binary_sha256': diagnostic['production_binary_sha256'],
              'runtime': {'python': platform.python_version(), 'numpy': np.__version__,
                          'scipy': scipy.__version__, 'platform': platform.platform()},
              'code_sha256': code_hashes, 'input_dataset_sha256': data_hashes,
              'matched_cases': len(results) // 2, 'fits': len(results),
              'converged_fits': sum(bool(row['fit']['success']) for row in results),
              'results': results,
              'limitations': ['exploratory reanalysis of existing synthetic captures, not independent scenes',
                              'local linear covariance with explicitly stated iid or view-cluster assumptions',
                              'only 12 board-view clusters; sandwich errors are exploratory',
                              'no physical camera, board metrology, nonradial or target-platform validation']}
    (output / 'summary.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--capture', type=Path, default=CAPTURE)
    parser.add_argument('--diagnostic', type=Path, default=DIAGNOSTIC)
    args = parser.parse_args()
    study(args.output, args.capture, args.diagnostic)
