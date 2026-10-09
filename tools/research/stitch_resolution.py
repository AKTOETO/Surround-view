"""Paired 64/256 cube-face study with independent source-visibility ablation."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from temporal_seam_stability import load_sequence, run_temporal_analysis


def validate_pair(low, high, sizes=(64, 256)):
    """Refuse a resolution comparison when geometry, view, poses or truth changed."""
    low, high = Path(low), Path(high)
    a, b = [json.loads((root/'paired_truth.json').read_text()) for root in (low, high)]
    ca, cb = [json.loads((root/'capture.json').read_text()) for root in (low, high)]
    if len(sizes) != 2 or not 32 <= sizes[0] < sizes[1] <= 2048:
        raise ValueError('two increasing cube-face sizes in 32..2048 required')
    if ca['face_size'] != sizes[0] or cb['face_size'] != sizes[1]:
        raise ValueError(f'expected {sizes[0]} and {sizes[1]} cube-face captures')
    if a['config'] != b['config'] or a['objects'] != b['objects'] or a['ego_object_ids'] != b['ego_object_ids']:
        raise ValueError('configuration, object IDs or ego masks changed between conditions')
    for key in ('scenario_recipe', 'mount_offsets', 'engine', 'view_transform', 'blender_version'):
        if ca.get(key) != cb.get(key):
            raise ValueError(f'capture conditions changed: {key}')
    # This validates checksums, camera order and input/truth pose alignment for both sets.
    load_sequence(low, low, 'any')
    load_sequence(high, high, 'any')
    if len(a['frames']) != len(b['frames']):
        raise ValueError('frame counts differ')
    for left, right in zip(a['frames'], b['frames']):
        for key in ('scenario_timestamp_ns', 'T_world_from_vehicle'):
            if left[key] != right[key]:
                raise ValueError(f'paired {key} differs')
        # PNG metadata (render duration etc.) may differ; decoded RGB must be exact.
        with Image.open(low/left['rgb']) as image:
            rgb_low = np.asarray(image).copy()
        with Image.open(high/right['rgb']) as image:
            rgb_high = np.asarray(image).copy()
        if not np.array_equal(rgb_low, rgb_high):
            raise ValueError('independent RGB pixels differ between conditions')
        # Integer truth arrays must also be identical, not just visually similar.
        for key in ('objects', 'visibility'):
            if a['sha256'][left[key]] != b['sha256'][right[key]]:
                raise ValueError(f'independent {key} truth differs between conditions')
    return {'direct_truth_bit_identical': True, 'frames': len(a['frames'])}


def run_study(fixture, output, sizes=(64, 256)):
    fixture, output = Path(fixture), Path(output)
    pairing = validate_pair(fixture/str(sizes[0]), fixture/str(sizes[1]), sizes)
    output.mkdir(parents=True, exist_ok=False)
    results = {}
    for size in sizes:
        root = fixture/str(size)
        results[str(size)] = {}
        for policy in ('ignore', 'any'):
            results[str(size)][policy] = run_temporal_analysis(
                root, root, output/f'{size}-{policy}', visibility_policy=policy)
    summary = {'schema_version': 1, 'experiment': 'E-STITCH-01-resolution-visibility',
               'pairing': pairing, 'face_sizes': list(sizes), 'results': results,
               'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
               'limitations': ['one scene, three frames, no independent trials',
                               'analytic carrier: triangle/memory budgets and GPU timing not measured',
                               'ray visibility excludes hide_render helpers but ignores transparency',
                               'matched visibility is not object-correspondence ghost detection']}
    (output/'summary.json').write_text(json.dumps(summary, indent=2, allow_nan=False)+'\n')
    return summary


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, default=Path('tests/data/stitch_resolution_v1'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sizes', type=int, nargs=2, default=(64, 256))
    args = parser.parse_args()
    run_study(args.fixture, args.output, args.sizes)
