"""Seeded scene-variant and explicit per-camera exposure stress study (offline CPU)."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'blender'))
from temporal_seam_stability import evaluate_sequence, load_sequence
from scenario import validate, near_obstacle_positions
from rig import vehicle_pose


def validate_capture(meta, recipe, expected, config):
    if meta['scenario_recipe'] != validate(recipe):
        raise ValueError('captured recipe differs from locked study plan')
    if meta['near_obstacles'] != near_obstacle_positions(recipe):
        raise ValueError('captured near obstacles differ from seeded layout')
    if meta['face_size'] != expected['face_size'] or len(meta['frames']) != expected['frames']:
        raise ValueError('captured resolution/frame count differs from plan')
    if config['output'] != {'width': expected['width'], 'height': expected['height']}:
        raise ValueError('output dimensions differ from plan')
    for index, row in enumerate(meta['frames']):
        frame = index*expected['frame_step']
        if (int(row['scenario_timestamp_ns']) != round(frame*1e9/30)
                or row['T_world_from_vehicle'] != vehicle_pose(frame).tolist()):
            raise ValueError('captured trajectory/timestamps differ from plan')


def exposure_stops(condition, frame):
    if condition == 'nominal':
        return [0.]*4
    if condition == 'static_bias':
        return [.5, -.5, .25, -.25]
    if condition == 'front_jump':
        return [.5 if frame % 2 == 0 else -.5, 0., 0., 0.]
    raise ValueError('unknown photometric condition')


def expose(image, stops):
    """Apply 2**EV to linearized sRGB, clip, encode/quantize back to RGB8.

    This is a digital camera-gain model, not a physical sensor/noise model.
    """
    image = np.asarray(image)
    if image.dtype != np.uint8 or image.ndim != 3 or image.shape[-1] != 3:
        raise ValueError('RGB8 input required')
    if not np.isfinite(stops) or abs(stops) > 3:
        raise ValueError('finite exposure within +/-3 stops required')
    value = image.astype(float)/255
    linear = np.where(value <= .04045, value/12.92, ((value+.055)/1.055)**2.4)
    gain = linear*2.**stops
    clipped_fraction = float(np.mean(gain > 1))
    gain = np.clip(gain, 0, 1)
    srgb = np.where(gain <= .0031308, 12.92*gain, 1.055*gain**(1/2.4)-.055)
    return np.rint(np.clip(srgb, 0, 1)*255).astype(np.uint8), clipped_fraction


def run_study(plan_path, inputs, output):
    plan_path, inputs, output = Path(plan_path), Path(inputs), Path(output)
    plan = json.loads(plan_path.read_text())
    if len({r['seed'] for r in plan['scenarios']}) != len(plan['scenarios']):
        raise ValueError('unique scene seeds required')
    # Validate all files and capture recipes before publishing any output.
    sequences, provenance = {}, {}
    for recipe in plan['scenarios']:
        seed = recipe['seed']
        dataset, capture = inputs/f'seed{seed}-inputs', inputs/f'seed{seed}-capture'
        sequences[seed] = load_sequence(dataset, capture, 'any')
        meta = json.loads((capture/'capture.json').read_text())
        validate_capture(meta, recipe, plan['capture'], sequences[seed][0])
        provenance[str(seed)] = {
            str(path): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (dataset/'config.json', dataset/'manifest.json', dataset/'ground_truth.json',
                         capture/'capture.json', capture/'paired_truth.json')}
    output.mkdir(parents=True, exist_ok=False)
    results = {}
    for seed, (cfg, images, truths, masks, stamps, poses) in sequences.items():
        results[str(seed)] = {}
        for condition in ('nominal', 'static_bias', 'front_jump'):
            transformed, exposure, saturation, digests = [], [], [], []
            for index, frame in enumerate(images):
                ev = exposure_stops(condition, index)
                transformed_frame, clipped, frame_hash = [], [], []
                for image, stops in zip(frame, ev):
                    rgb, fraction = expose(image, stops)
                    transformed_frame.append(rgb)
                    clipped.append(fraction)
                    frame_hash.append(hashlib.sha256(rgb.tobytes()).hexdigest())
                transformed.append(transformed_frame)
                exposure.append(ev)
                saturation.append(clipped)
                digests.append(frame_hash)
            result = evaluate_sequence(cfg, transformed, truths, masks, stamps, poses,
                                       output/f'seed{seed}-{condition}')
            results[str(seed)][condition] = {'results': result, 'camera_ev_by_frame': exposure,
                                           'clipped_channel_fraction': saturation,
                                           'transformed_rgb_sha256': digests}
    names = ('temporal_seam_stability.py', 'reference.py', 'fusion.py', 'stitch_metrics.py')
    code = {name: hashlib.sha256((Path(__file__).resolve().parents[1]/name).read_bytes()).hexdigest()
            for name in names}
    code['stitch_robustness.py'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    report = {'schema_version': 1, 'experiment': 'E-STITCH-01-scene-exposure',
              'plan_sha256': hashlib.sha256(plan_path.read_bytes()).hexdigest(),
              'inputs_sha256': provenance, 'code_sha256': code, 'results': results,
              'limitations': ['three nearby layout variants of one street, not three street types',
                              'three-frame straight drive; no dynamic objects or sensor noise',
                              'nominal true mounts, no calibration error; no exposure compensation',
                              'digital linear-light gain; oracle remains nominal exposure',
                              'no object-correspondence ghost metric or target-device timing']}
    (output/'summary.json').write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, default=Path('assets/scenarios/stitch-validation-v1.json'))
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run_study(args.plan, args.inputs, args.output)
