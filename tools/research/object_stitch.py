"""Controlled RGB object-identity screen over carriers/fusion with source-ID provenance."""
import argparse
import copy
import hashlib
import json
import os
import platform
from pathlib import Path
import sys
import time

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from fusion import FUSION_MODES
from object_metrics import measure_target, projected_object_ids, target_mask
from reference import render
from run_e_stitch_01 import CARRIERS
from temporal_seam_stability import _verified, load_sequence


def interleaved_orders(keys, repeats, seed=20261010):
    """Return deterministic complete randomized blocks over workload keys."""
    if repeats < 1 or len(set(keys)) != len(keys):
        raise ValueError('positive repeats and unique workload keys required')
    rng = np.random.default_rng(seed)
    return [[keys[i] for i in rng.permutation(len(keys))] for _ in range(repeats)]


def _host_metadata():
    return {
        'platform': platform.platform(),
        'python': platform.python_version(),
        'numpy': np.__version__,
        'machine': platform.machine(),
        'processor': platform.processor(),
        'logical_cpu_count': os.cpu_count(),
        'thread_environment': {name: os.environ.get(name) for name in (
            'OMP_NUM_THREADS', 'OPENBLAS_NUM_THREADS', 'MKL_NUM_THREADS', 'NUMEXPR_NUM_THREADS')},
    }


def load_objects(root):
    root = Path(root).resolve()
    cfg, images, truths, masks, stamps, poses = load_sequence(root, root, 'any')
    metadata = json.loads((root/'paired_truth.json').read_text())
    target = metadata.get('diagnostic_target')
    if not target or target['object_name'] not in metadata['objects']:
        raise ValueError('coded diagnostic target identity required')
    object_id = metadata['objects'][target['object_name']]
    source, expected = [], []
    for row in metadata['frames']:
        names = row.get('source_objects', [])
        if len(names) != 4:
            raise ValueError('all four source-camera ID maps required')
        arrays = []
        for camera, name in zip(cfg['cameras'], names):
            if not Path(name).name.startswith(f"source_objects_camera{camera['id']}_"):
                raise ValueError('source object camera order mismatch')
            value = np.load(_verified(root, name, metadata['sha256']), allow_pickle=False)
            shape = (camera['resolution']['height'], camera['resolution']['width'])
            if value.shape != shape or value.dtype != np.uint16:
                raise ValueError('uint16 source ID map at camera resolution required')
            arrays.append(value)
        source.append(arrays)
        ids = np.load(_verified(root, row['objects'], metadata['sha256']), allow_pickle=False)
        expected.append(ids == object_id)
    return cfg, images, truths, source, expected, target, object_id


def run_study(fixture, output, warmup=2, repeats=7, order_seed=20261010):
    if warmup < 0 or repeats < 3:
        raise ValueError('warmup >= 0 and at least three CPU render repeats required')
    cfg, images, truths, source, expected, target, object_id = load_objects(fixture)
    threshold, minimum = target['chroma_threshold'], target['min_component_pixels']
    controls = [measure_target(target_mask(rgb, threshold), mask, minimum)
                for rgb, mask in zip(truths, expected)]
    if any(c['iou'] is None or c['iou'] < .75 for c in controls):
        raise ValueError('direct RGB classifier fails declared 0.75 target-ID IoU control')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    cases = []
    for carrier, surface in CARRIERS.items():
        for mode in FUSION_MODES:
            config = copy.deepcopy(cfg)
            config['surface'], config['fusion'] = surface, {'mode':mode}
            for index, frame in enumerate(images):
                cases.append({'key':f'{carrier}/{mode}/{index:04d}', 'carrier':carrier,
                              'mode':mode, 'frame_index':index, 'config':config, 'frame_data':frame})
    keys = [case['key'] for case in cases]
    case_by_key = {case['key']:case for case in cases}
    for case in cases:
        for _ in range(warmup):
            render(case['config'], case['frame_data'])
    schedules = interleaved_orders(keys, repeats, order_seed)
    timings_by_key = {key:[] for key in keys}
    rendered_by_key = {}
    for schedule in schedules:
        for key in schedule:
            case = case_by_key[key]
            started = time.perf_counter_ns()
            rendered = render(case['config'], case['frame_data'])
            timings_by_key[key].append((time.perf_counter_ns()-started)/1e6)
            rendered_by_key[key] = rendered

    rows = []
    for case in cases:
        carrier, mode, index = case['carrier'], case['mode'], case['frame_index']
        config, frame = case['config'], case['frame_data']
        result = rendered_by_key[case['key']]
        observed = target_mask(result['rgb'], threshold)
        metric = measure_target(observed, expected[index], minimum)
        ids = np.stack([projected_object_ids(c, result['world'], label)
                        for c,label in zip(cfg['cameras'], source[index])], axis=-1)
        support = np.sum(result['weights']*(ids == object_id), axis=-1)
        metric['observed_pixels_with_source_id_support'] = int((observed & (support > .05)).sum())
        metric['source_target_pixels'] = [int((label == object_id).sum()) for label in source[index]]
        name = f'{carrier}_{mode}_{index:04d}'
        Image.fromarray(result['rgb']).save(output/f'{name}.png')
        Image.fromarray((observed*255).astype(np.uint8)).save(output/f'{name}_mask.png')
        timings = timings_by_key[case['key']]
        rows.append({'carrier':carrier,'mode':mode,'frame':index,'target':metric,
                     'cpu_render_ms':{'samples':timings,'p50':float(np.median(timings)),
                                      'p95':float(np.percentile(timings,95)), 'max':max(timings)}})
    fixture = Path(fixture)
    hashes = {name:hashlib.sha256((fixture/name).read_bytes()).hexdigest()
              for name in ('config.json','capture.json','paired_truth.json','manifest.json','ground_truth.json')}
    protocol = Path(__file__).resolve().parents[2]/'docs/research/STITCH_TIMING_PROTOCOL.md'
    code = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (
        Path(__file__), Path(__file__).resolve().parents[1]/'object_metrics.py',
        Path(__file__).resolve().parents[1]/'reference.py', Path(__file__).resolve().parents[1]/'fusion.py',
        Path(__file__).resolve().parents[1]/'run_e_stitch_01.py')}
    report = {'schema_version':1,'experiment':'E-STITCH-coded-object-timing-02', 'target_id':object_id,
              'protocol_sha256':hashlib.sha256(protocol.read_bytes()).hexdigest(),
              'timing': {'warmup_per_case':warmup, 'repeats':repeats, 'order':'randomized complete blocks',
                         'order_seed':order_seed, 'case_keys':keys, 'block_orders':schedules,
                         'host':_host_metadata(),
                         'scope':'CPU reference.render only; excludes truth/metrics/file I/O; not GPU or server latency'},
              'classifier_control':controls, 'input_sha256':hashes,'code_sha256':code,'results':rows,
              'limitations':['one coded emission cuboid, two frames, one rig/view',
                             'color classifier is not a natural-object recognizer',
                             'connected/overlapping copies may merge; counts do not capture every ghost',
                             'source IDs use ideal pixel-center opaque rays; RGB is filtered',
                             'carrier geometry budgets unequal; offline analytic screen only',
                             'projected ID mass is provenance, not final RGB segmentation']}
    (output/'summary.json').write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, default=Path('tests/data/object_stitch_v1'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--warmup', type=int, default=2)
    parser.add_argument('--repeats', type=int, default=7)
    parser.add_argument('--order-seed', type=int, default=20261010)
    args = parser.parse_args()
    run_study(args.fixture, args.output, args.warmup, args.repeats, args.order_seed)
