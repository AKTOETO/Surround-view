"""Production OpenCV calibration under paired board distance x local tilt factors."""
import argparse
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from calibration.capture_factors import PROFILES, diagnostics, factor_poses
from calibration.coverage import coverage
from calibration.coverage_study import fit
from calibration.optical_models import FAMILIES, metric_validation
from calibration.raster_board import BOARD, K, corners, detection_error, image, poses
from calibration.raster_study import digest, invoke

ROOT = Path(__file__).resolve().parents[2]


def study(output, binary=Path('build/sv-calibrate'), seeds=(7101, 7102)):
    output, binary = Path(output).resolve(), Path(binary).resolve()
    if not binary.is_file() or not seeds or len(set(seeds)) != len(seeds):
        raise ValueError('existing calibrator and unique seeds required')
    sources = ['tools/calibration/' + n for n in ('capture_study.py', 'capture_factors.py', 'coverage.py',
               'coverage_study.py', 'optical_models.py', 'raster_board.py', 'raster_study.py')]
    sources += ['src/vision/calibration.cpp', 'src/apps/calibrate.cpp', 'docs/research/CALIBRATION_CAPTURE_PROTOCOL.md']
    hashes = {n: digest(ROOT / n) for n in sources}
    binary_hash = digest(binary)
    output.mkdir(parents=True, exist_ok=False)
    results = []
    for family in FAMILIES:
        for seed in seeds:
            folder = output / f'{family.name}-{seed}'
            folder.mkdir()
            central = poses(seed)
            groups = {'central_train': central[:4], 'central_validation': central[12:],
                      'outer_validation': factor_poses(seed + 2000, 2.6, .175)}
            groups.update({name: factor_poses(seed + 1000, **parameters) for name, parameters in PROFILES.items()})
            files, views, image_hashes = {}, [], {}
            for group, geometry in groups.items():
                files[group] = []
                for i, (r, t) in enumerate(geometry):
                    name = f'{group}-{i:02}.png'
                    files[group].append(name)
                    Image.fromarray(image(r, t, optics=family)).save(folder / name)
                    image_hashes[name] = digest(folder / name)
                    destination = folder / f'detect-{group}-{i}'
                    process = invoke([binary, 'detect', '--image', folder / name, '--output', destination])
                    row = {'group': group, 'view': i, 'file': name, 'process': process}
                    if process['returncode'] == 0:
                        points = json.loads((destination / 'detections.json').read_text())['points']
                        row['localization'] = detection_error([p['uv_px'] for p in points], corners(r, t, family))
                    views.append(row)
            entry = {'family': family.name, 'seed': seed, 'views': views, 'image_sha256': image_hashes,
                     'geometry': {g: [{'rotation': r.tolist(), 'translation': t.tolist()} for r, t in geometry]
                                  for g, geometry in groups.items()}, 'profiles': []}
            for profile, parameters in PROFILES.items():
                geometry = groups['central_train'] + groups[profile]
                row = {'profile': profile, 'parameters': parameters, 'train_coverage': coverage(geometry, family),
                       'peripheral_diagnostics': diagnostics(groups[profile], family), 'dataset_sha256': {}, 'fits': []}
                for gate in ('central', 'full'):
                    data = {'schema_version': 1, 'camera_id': 0, 'board': BOARD, 'theta_max_rad': 1.,
                            'train': files['central_train'] + files[profile],
                            'validation': files['central_validation'] + (files['outer_validation'] if gate == 'full' else [])}
                    name = f'{profile}-{gate}.json'
                    (folder / name).write_text(json.dumps(data, indent=2) + '\n')
                    row['dataset_sha256'][gate] = digest(folder / name)
                for order in (2, 4):
                    a = fit(binary, folder, f'{profile}-central.json', folder / f'{profile}-central-{order}', order)
                    b = fit(binary, folder, f'{profile}-full.json', folder / f'{profile}-full-{order}', order)
                    fitted = {'order': order, 'central_gate': a, 'full_gate': b}
                    if 'estimate' in a:
                        fitted['metric_validation'] = metric_validation(a['estimate'], K, family,
                            groups['central_validation'] + groups['outer_validation'])
                    if 'estimate' in a and 'estimate' in b:
                        av, bv = ([m['fx'], m['fy'], m['cx'], m['cy'], *m['k']] for m in (a['estimate'], b['estimate']))
                        fitted['repeat_fit_max_parameter_delta'] = float(np.max(np.abs(np.asarray(av) - bv)))
                    row['fits'].append(fitted)
                entry['profiles'].append(row)
            entry['validation_coverage'] = {g: coverage(groups[g], family) for g in ('central_validation', 'outer_validation')}
            results.append(entry)
            accepted = sum('estimate' in f['central_gate'] for p in entry['profiles'] for f in p['fits'])
            print(f'{family.name}/{seed}: detection {sum("localization" in v for v in views)}/48; central accepted {accepted}/8', flush=True)
    if hashes != {n: digest(ROOT / n) for n in sources} or binary_hash != digest(binary):
        raise RuntimeError('experiment source or binary changed during capture; refusing a mixed-provenance summary')
    report = {'schema_version': 1, 'experiment': 'E-CAL-capture-01', 'seeds': list(seeds), 'true_K': K.tolist(),
              'production_binary_sha256': binary_hash, 'code_sha256': hashes,
              'limitations': ['synthetic clean radial optics; no physical camera or nonradial/extrinsic errors',
                              'center directions equal across factors; individual corner angular occupancy differs',
                              'distance changes scale and angular extent; tilt changes foreshortening',
                              'central-gate exported estimates only; failed estimates have no oracle metrics',
                              'exploratory protocol fixed before main outcomes, after geometric clipping checks'],
              'results': results}
    (output / 'summary.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=Path('build/sv-calibrate'))
    parser.add_argument('--seeds', type=int, nargs='+', default=[7101, 7102])
    args = parser.parse_args()
    study(args.output, args.binary, args.seeds)
