"""Replay capture profiles using exact, float32-exact and detected train corners."""
import argparse
import json
from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from calibration.detector_errors import aligned_truth, signed_localization
from calibration.optical_models import FAMILIES, metric_validation
from calibration.raster_board import BOARD, K, SIZE, corners
from calibration.raster_study import digest, invoke

ROOT = Path(__file__).resolve().parents[2]
ORIGINS = ('analytic_truth', 'analytic_truth_float32', 'detected_raster')


def parameters(model):
    return np.array([model['fx'], model['fy'], model['cx'], model['cy'], *model['k']])


def study(source, output, binary=Path('build/sv-calibrate')):
    source, output, binary = (Path(p).resolve() for p in (source, output, binary))
    parent_path = source / 'summary.json'
    parent_hash = digest(parent_path)
    parent = json.loads(parent_path.read_text())
    if parent['experiment'] != 'E-CAL-capture-01':
        raise ValueError('E-CAL-capture-01 source required')
    code = ['tools/calibration/' + name for name in ('diagnostic_study.py', 'detector_errors.py',
            'optical_models.py', 'raster_board.py', 'raster_study.py')]
    code += ['src/vision/calibration.cpp', 'src/vision/camera.cpp', 'src/apps/diagnose_intrinsics.cpp',
             'src/apps/diagnose_intrinsics.hpp', 'src/apps/calibrate.cpp',
             'docs/research/INTRINSIC_DIAGNOSTIC_PROTOCOL.md']
    hashes = {name: digest(ROOT / name) for name in code}
    for name in ('src/vision/calibration.cpp', 'tools/calibration/optical_models.py', 'tools/calibration/raster_board.py'):
        if hashes[name] != parent['code_sha256'][name]:
            raise ValueError('parent numerical core changed: ' + name)
    binary_hash = digest(binary)
    output.mkdir(parents=True, exist_ok=False)
    results, input_hashes = [], {}
    for entry in parent['results']:
        case = f"{entry['family']}-{entry['seed']}"
        folder = source / case
        destination = output / case
        destination.mkdir()
        family = next(f for f in FAMILIES if f.name == entry['family'])
        lookup = {}
        for view in entry['views']:
            name, group, i = view['file'], view['group'], view['view']
            image_path = folder / name
            if digest(image_path) != entry['image_sha256'][name]:
                raise ValueError('parent image content changed: ' + str(image_path))
            input_hashes[str(image_path.relative_to(source))] = digest(image_path)
            detection_path = folder / f'detect-{group}-{i}' / 'detections.json'
            input_hashes[str(detection_path.relative_to(source))] = digest(detection_path)
            detected = json.loads(detection_path.read_text())
            if detected['image_sha256'] != entry['image_sha256'][name]:
                raise ValueError('detection image provenance mismatch')
            uv = np.asarray([p['uv_px'] for p in detected['points']])
            pose = entry['geometry'][group][i]
            r, t = np.asarray(pose['rotation']), np.asarray(pose['translation'])
            truth = corners(r, t, family)
            lookup[group, i] = {'id': name, 'truth': aligned_truth(uv, truth), 'detected': uv,
                               'geometry': (r, t), 'localization': signed_localization(uv, truth)}
        result = {'family': family.name, 'seed': entry['seed'], 'profiles': [],
                  'detector_localization': [{'group': g, 'view': i, 'file': v['id'], **v['localization']}
                                            for (g, i), v in lookup.items()]}
        validation = [lookup[g, i] for g, count in (('central_validation', 4), ('outer_validation', 8)) for i in range(count)]
        for profile in entry['profiles']:
            name = profile['profile']
            training = [lookup[g, i] for g, count in (('central_train', 4), (name, 8)) for i in range(count)]
            row = {'profile': name, 'fits': [], 'dataset_sha256': {}}
            for origin in ORIGINS:
                def pixels(v):
                    return (v['detected'] if origin == 'detected_raster' else
                            v['truth'].astype(np.float32).astype(float) if origin == 'analytic_truth_float32' else v['truth'])
                data = {'schema_version': 1, 'purpose': 'intrinsic_solver_diagnostic',
                        'observation_origin': origin, 'board': BOARD, 'theta_max_rad': 1.,
                        'resolution': {'width': SIZE[0], 'height': SIZE[1]},
                        'train': [{'id': v['id'], 'uv_px': pixels(v).tolist()} for v in training],
                        'validation': [{'id': v['id'], 'uv_px': v['truth'].tolist()} for v in validation]}
                path = destination / f'{name}-{origin}.json'
                path.write_text(json.dumps(data, indent=2) + '\n')
                row['dataset_sha256'][origin] = digest(path)
                for order in (2, 4):
                    target = destination / f'{name}-{origin}-{order}'
                    process = invoke([binary, 'diagnose-intrinsics', '--dataset', path, '--output', target,
                                      '--distortion-order', order])
                    fitted = {'origin': origin, 'order': order, 'process': process}
                    if (target / 'diagnostics.json').exists():
                        report = json.loads((target / 'diagnostics.json').read_text())
                        fitted['report'] = report
                        fitted['metric_validation'] = metric_validation(report['estimate'], K, family,
                                                                        [v['geometry'] for v in validation])
                        if origin == 'detected_raster':
                            old = next(f for f in profile['fits'] if f['order'] == order)['central_gate']['estimate']
                            delta = float(np.abs(parameters(old) - parameters(report['estimate'])).max())
                            fitted['production_replay_max_parameter_delta'] = delta
                            fitted['production_replay_confirmed'] = delta <= 1e-7
                    row['fits'].append(fitted)
            result['profiles'].append(row)
        results.append(result)
        print(case + ': ' + str(sum('report' in f for p in result['profiles'] for f in p['fits'])) + '/24 diagnostic fits exported', flush=True)
    if hashes != {name: digest(ROOT / name) for name in code} or digest(binary) != binary_hash:
        raise RuntimeError('diagnostic source/binary changed during run')
    if digest(parent_path) != parent_hash or any(digest(source / name) != h for name, h in input_hashes.items()):
        raise RuntimeError('parent input changed during diagnostic run')
    report = {'schema_version': 1, 'experiment': 'E-CAL-diagnostic-01', 'parent_summary_sha256': parent_hash,
              'parent_binary_sha256': parent['production_binary_sha256'], 'production_binary_sha256': binary_hash,
              'code_sha256': hashes, 'input_sha256': input_hashes, 'results': results,
              'limitations': ['exploratory replay of already reviewed capture profiles, not new scenes',
                              'validation UV exact for all variants; no acceptance gate in diagnostic mode',
                              'same numerical core; not a condition-number or uncertainty estimate',
                              'only finite radial models; model mismatch may remain with exact points']}
    (output / 'summary.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=Path('artifacts/calibration-capture-v1'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=Path('build/sv-calibrate'))
    args = parser.parse_args()
    study(args.input, args.output, args.binary)
