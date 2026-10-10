"""Audit same-input mask-fix reruns; combine raw reports without ranking timings."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

PAIRS = {
    'resolution': ('stitch-resolution-v1', 'stitch-resolution-mask-v2', 84),
    'convergence': ('stitch-convergence-v1', 'stitch-convergence-mask-v2', 84),
    'exposure': ('stitch-robustness-v2', 'stitch-robustness-mask-v2', 189),
    'static_object': ('object-study-interleaved-v2', 'object-study-mask-v2', 84),
    'moving_object': ('object-stitch-motion-results-final-v2', 'object-motion-mask-v2', 378),
}
MODES = ('graph_cut_multi_band', 'multi_band', 'hard_best_angle',
         'angular_feather', 'seam_distance_feather', 'graph_cut_seam', 'edge_feather')


def require(condition, detail):
    if not condition:
        raise ValueError(detail)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_inputs(key, previous, current):
    require(current['fusion_implementation'] == 'validity_zero_extension_v2', 'unexpected fusion version')
    if key in ('resolution', 'convergence'):
        require(previous['pairing'] == current['pairing'], 'truth pairing changed')
        require(previous.get('face_sizes', [64, 256]) == current['face_sizes'], 'face sizes changed')
        for size in current['face_sizes']:
            for policy in ('ignore', 'any'):
                a = previous['results'][str(size)][policy]
                b = current['results'][str(size)][policy]
                require(a['input_sha256'] == b['input_sha256'], 'resolution inputs changed')
                require(a['visibility_policy'] == b['visibility_policy'], 'visibility policy changed')
    elif key == 'exposure':
        require(previous['inputs_sha256'] == current['inputs_sha256'], 'exposure inputs changed')
        require(previous['plan_sha256'] == current['plan_sha256'], 'exposure plan changed')
        for seed, conditions in current['results'].items():
            for condition, result in conditions.items():
                before = previous['results'][seed][condition]
                require(before['transformed_rgb_sha256'] == result['transformed_rgb_sha256'],
                        'transformed exposure pixels changed')
    else:
        before, after = previous['input_sha256'], current['input_sha256']
        if key == 'static_object' and 'dataset' not in before:
            # Legacy static screen used a flat map with disjoint file names.
            after = {**after['dataset'], **after['capture']}
        require(before == after, 'object inputs changed')
        require(previous['classifier_control'] == current['classifier_control'], 'classifier controls changed')
        keys = lambda report: [(r['carrier'], r['mode'], r['frame']) for r in report['results']]
        require(keys(previous) == keys(current), 'object cases changed')


def compare_rgb(old, new, expected_cases):
    def paths(root):
        return {p.relative_to(root) for p in root.rglob('*.png')
                if not p.stem.endswith(('_mask', '_source_support'))}
    files = paths(new)
    require(len(files) == expected_cases, 'incomplete RGB case set')
    require(files == paths(old), 'old/new RGB file sets differ')
    totals = {}
    for relative in sorted(files):
        with Image.open(old / relative) as image:
            before = np.asarray(image).astype(np.int16)
        with Image.open(new / relative) as image:
            after = np.asarray(image).astype(np.int16)
        require(before.shape == after.shape and before.ndim == 3 and before.shape[-1] == 3,
                'RGB shapes differ or are not three-channel images')
        mode = next((m for m in MODES if m in relative.stem), None)
        require(mode is not None, 'unknown fusion output')
        stat = totals.setdefault(mode, {'cases': 0, 'pixels': 0, 'channels': 0,
                                      'absolute_sum': 0, 'max_rgb8': 0, 'changed_pixels': 0,
                                      'output_sha256': {}})
        delta = np.abs(after - before)
        stat['cases'] += 1
        stat['pixels'] += delta.shape[0] * delta.shape[1]
        stat['channels'] += delta.size
        stat['absolute_sum'] += int(delta.sum())
        stat['max_rgb8'] = max(stat['max_rgb8'], int(delta.max()))
        stat['changed_pixels'] += int(np.any(delta != 0, axis=-1).sum())
        stat['output_sha256'][str(relative)] = sha256(new / relative)
    for mode, stat in totals.items():
        stat['mean_abs_rgb8'] = stat['absolute_sum'] / stat['channels']
        stat['changed_pixel_percent'] = 100 * stat['changed_pixels'] / stat['pixels']
        if mode not in ('multi_band', 'graph_cut_multi_band'):
            require(stat['max_rgb8'] == 0, 'unexpected nonpyramid RGB change: ' + mode)
    return totals


def run(artifacts, output):
    combined = {
        'schema_version': 1, 'fusion_implementation': 'validity_zero_extension_v2',
        'purpose': 'same-input regression after invalid-RGB mask correction; not independent quality confirmation',
        'timing_comparability': 'not accepted: concurrent quality reruns, no isolated timing or thermal protocol',
        'comparison_runner_sha256': sha256(Path(__file__)),
        'pairing_checks': {}, 'historical_code_sha256': {}, 'reports': {},
        'historical_summary_sha256': {}, 'rgb8_comparisons': {},
    }
    for key, (old_name, new_name, cases) in PAIRS.items():
        old, new = artifacts / old_name, artifacts / new_name
        previous = json.loads((old / 'summary.json').read_text())
        current = json.loads((new / 'summary.json').read_text())
        validate_inputs(key, previous, current)
        combined['rgb8_comparisons'][key] = compare_rgb(old, new, cases)
        combined['pairing_checks'][key] = {'input_hashes_equal': True, 'expected_rgb_cases': cases}
        combined['historical_code_sha256'][key] = previous.get('code_sha256', {})
        combined['reports'][key] = current
        combined['historical_summary_sha256'][key] = sha256(old / 'summary.json')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(combined, indent=2, allow_nan=False) + '\n')


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifacts', type=Path, default=root / 'artifacts')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.artifacts, args.output)
