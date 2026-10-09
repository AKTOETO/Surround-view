"""Matched-model directional sensitivity with equal-RMS signed UV perturbations."""
import argparse
import copy
import json
from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from calibration.diagnostic_study import parameters
from calibration.optical_models import FAMILIES, metric_validation
from calibration.raster_board import K
from calibration.raster_study import digest, invoke
from calibration.sensitivity import AMPLITUDES, DIRECTIONS, direction_field, pair_response

ROOT = Path(__file__).resolve().parents[2]


def diagnostic(binary, dataset, output, order, family, geometry):
    process = invoke([binary, 'diagnose-intrinsics', '--dataset', dataset, '--output', output,
                      '--distortion-order', order])
    row = {'process': process}
    if (output / 'diagnostics.json').exists():
        report = json.loads((output / 'diagnostics.json').read_text())
        row['report'] = report
        row['metric_validation'] = metric_validation(report['estimate'], K, family, geometry)
    return row


def study(source, capture_path, output, binary=Path('build/sv-calibrate')):
    source, capture_path, output, binary = (Path(p).resolve() for p in (source, capture_path, output, binary))
    parent_path = source / 'summary.json'
    parent = json.loads(parent_path.read_text())
    if parent['experiment'] != 'E-CAL-diagnostic-01' or digest(capture_path) != parent['parent_summary_sha256']:
        raise ValueError('matching diagnostic parent and capture summary required')
    capture = json.loads(capture_path.read_text())
    names = ['tools/calibration/' + n for n in ('sensitivity_study.py', 'sensitivity.py', 'diagnostic_study.py',
             'optical_models.py', 'raster_board.py', 'raster_study.py')]
    names += ['src/vision/calibration.cpp', 'src/apps/diagnose_intrinsics.cpp', 'src/apps/calibrate.cpp',
              'docs/research/CALIBRATION_SENSITIVITY_PROTOCOL.md']
    hashes = {name: digest(ROOT / name) for name in names}
    for name in ('src/vision/calibration.cpp', 'tools/calibration/optical_models.py', 'tools/calibration/raster_board.py'):
        if hashes[name] != parent['code_sha256'][name]:
            raise ValueError('numerical core changed: ' + name)
    input_hashes = {parent_path: digest(parent_path), capture_path: digest(capture_path)}
    binary_hash = digest(binary)
    output.mkdir(parents=True, exist_ok=False)
    results = []
    for entry in parent['results']:
        if entry['family'] not in ('equidistant', 'kb_nonzero'):
            continue
        family = next(f for f in FAMILIES if f.name == entry['family'])
        original = next(e for e in capture['results'] if (e['family'], e['seed']) == (family.name, entry['seed']))
        geometry = [(np.asarray(p['rotation']), np.asarray(p['translation']))
                    for group in ('central_validation', 'outer_validation') for p in original['geometry'][group]]
        for profile in entry['profiles']:
            if profile['profile'] not in ('small_front', 'large_front'):
                continue
            source_dataset = source / f"{family.name}-{entry['seed']}" / f"{profile['profile']}-analytic_truth.json"
            expected = profile['dataset_sha256']['analytic_truth']
            if digest(source_dataset) != expected:
                raise ValueError('exact parent observations changed')
            input_hashes[source_dataset] = expected
            base_data = json.loads(source_dataset.read_text())
            truth = np.asarray([v['uv_px'] for v in base_data['train']])
            for order in ((2, 4) if family.name == 'equidistant' else (4,)):
                key = f"{family.name}-{entry['seed']}-{profile['profile']}-k{order}"
                folder = output / key
                folder.mkdir()
                baseline = diagnostic(binary, source_dataset, folder / 'baseline', order, family, geometry)
                if 'report' in baseline:
                    old = next(f for f in profile['fits'] if f['origin'] == 'analytic_truth' and f['order'] == order)['report']['estimate']
                    delta = float(np.abs(parameters(old) - parameters(baseline['report']['estimate'])).max())
                    baseline['parent_replay_max_parameter_delta'] = delta
                    baseline['parent_replay_confirmed'] = delta <= 1e-7
                result = {'family': family.name, 'seed': entry['seed'], 'profile': profile['profile'],
                          'order': order, 'baseline': baseline, 'pairs': []}
                for direction, noise_seed in DIRECTIONS:
                    field = direction_field(truth, direction, noise_seed)
                    for h in AMPLITUDES:
                        pair = {'direction': direction, 'noise_seed': noise_seed, 'amplitude_px': h, 'fits': {}}
                        for sign, multiplier in (('positive', 1), ('negative', -1)):
                            data = copy.deepcopy(base_data)
                            data['observation_origin'] = 'controlled_perturbation'
                            uv = truth + multiplier * h * field
                            for view, pixels in zip(data['train'], uv):
                                view['uv_px'] = pixels.tolist()
                            name = f'{direction}-{noise_seed}-h{h}-{sign}'
                            dataset = folder / (name + '.json')
                            dataset.write_text(json.dumps(data, indent=2) + '\n')
                            fit = diagnostic(binary, dataset, folder / name, order, family, geometry)
                            fit['dataset_sha256'] = digest(dataset)
                            fit['actual_input_rms_px'] = float(np.sqrt(np.mean(np.sum((uv-truth)**2, axis=-1))))
                            pair['fits'][sign] = fit
                        a, b = pair['fits']['positive'], pair['fits']['negative']
                        if all('report' in fit for fit in (a, b, baseline)):
                            pair['response'] = pair_response(a['report']['estimate'], b['report']['estimate'],
                                                             baseline['report']['estimate'], h, family)
                        result['pairs'].append(pair)
                results.append(result)
                print(key + ': ' + str(sum('report' in f for p in result['pairs'] for f in p['fits'].values())) + '/36 perturbed fits exported', flush=True)
    if hashes != {name: digest(ROOT / name) for name in names} or digest(binary) != binary_hash:
        raise RuntimeError('source/binary changed during sensitivity study')
    if any(digest(path) != h for path, h in input_hashes.items()):
        raise RuntimeError('parent inputs changed during sensitivity study')
    report = {'schema_version': 1, 'experiment': 'E-CAL-sensitivity-01',
              'code_sha256': hashes, 'production_binary_sha256': binary_hash,
              'parent_summary_sha256': input_hashes[parent_path], 'capture_summary_sha256': input_hashes[capture_path],
              'exact_input_sha256': {str(p.relative_to(source)): h for p, h in input_hashes.items() if p not in (parent_path, capture_path)},
              'amplitudes_px': list(AMPLITUDES), 'random_seeds': [1021, 1022, 1023],
              'limitations': ['matched radial models, two reviewed pose seeds, two front-facing capture profiles',
                              'coordinate perturbations, not detector/raster blur/noise or physical validation',
                              'directional finite response, not full Jacobian condition number or uncertainty',
                              'diagnostic_only; no acceptance gate or server config changes'], 'results': results}
    (output / 'summary.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=Path('artifacts/calibration-diagnostic-v2'))
    parser.add_argument('--capture-summary', type=Path, default=Path('docs/validation/baselines/calibration_capture_v1.json'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=Path('build/sv-calibrate'))
    args = parser.parse_args()
    study(args.input, args.capture_summary, args.output, args.binary)
