"""Evaluate captured frame sequences against matched independent Blender virtual views."""
import argparse
import copy
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage as ndi

from fusion import FUSION_MODES
from reference import render
from stitch_metrics import rgb_to_gray, simple_edge_detection


def seam_boundary(weights, validity):
    """Argmax-weight label boundary; a hard seam is a boundary too."""
    weights, validity = np.asarray(weights), np.asarray(validity, bool)
    if weights.ndim != 3 or weights.shape != validity.shape:
        raise ValueError('weights/validity must have matching HxWxC shapes')
    labels = np.argmax(weights, axis=-1)
    active = (validity.sum(axis=-1) > 1) & (weights.sum(axis=-1) > 1e-6)
    boundary = np.zeros(active.shape, bool)
    for axis in (0, 1):
        a, b = [slice(None)]*2, [slice(None)]*2
        a[axis], b[axis] = slice(None, -1), slice(1, None)
        a, b = tuple(a), tuple(b)
        changed = (labels[a] != labels[b]) & active[a] & active[b]
        boundary[a] |= changed
        boundary[b] |= changed
    return boundary


def boundary_motion(previous, current):
    """Symmetric nearest-boundary distance; missing boundaries are undefined."""
    p, c = int(previous.sum()), int(current.sum())
    result = {'previous_pixels': p, 'current_pixels': c, 'iou': None,
              'mean_distance_px': None, 'p95_distance_px': None}
    if p and c:
        distance = np.concatenate([ndi.distance_transform_edt(~current)[previous],
                                   ndi.distance_transform_edt(~previous)[current]])
        result.update(iou=float((previous & current).sum()/(previous | current).sum()),
                      mean_distance_px=float(distance.mean()),
                      p95_distance_px=float(np.percentile(distance, 95)))
    return result


def evaluate_temporal_stability(frames, truths, weights, validity, masks, timestamps_ns, poses):
    """Residual changes in a vehicle-fixed view, not motion-compensated scene flicker.

    Subtract the matching direct scene view before temporal differencing. Real object
    motion remains in both inputs; parallax/occlusion error changes remain in residuals.
    This metric cannot alone distinguish ghost trails, seam jumps and geometry error.
    RGB must be finite floats in [0,1]; masks define common non-ego object interiors.
    """
    frames, truths = np.asarray(frames), np.asarray(truths)
    masks = np.asarray(masks, bool)
    n = len(frames)
    if (n < 2 or frames.shape != truths.shape or frames.ndim != 4
            or frames.shape[-1] != 3 or masks.shape != frames.shape[:3]
            or len(weights) != n or len(validity) != n or len(poses) != n
            or len(timestamps_ns) != n):
        raise ValueError('at least two shape-matched frames, truths, masks and metadata required')
    if (not np.isfinite(frames).all() or not np.isfinite(truths).all()
            or min(frames.min(), truths.min()) < 0 or max(frames.max(), truths.max()) > 1):
        raise ValueError('RGB must contain finite float samples in [0,1]')
    stamps = [int(t) for t in timestamps_ns]
    if any(b <= a for a, b in zip(stamps, stamps[1:])):
        raise ValueError('timestamps must strictly increase')
    poses = np.asarray(poses, float)
    if poses.shape != (n, 4, 4) or not np.isfinite(poses).all():
        raise ValueError('finite 4x4 vehicle poses required')
    boundaries = [seam_boundary(w, v) for w, v in zip(weights, validity)]
    if any(b.shape != masks.shape[1:] for b in boundaries):
        raise ValueError('weight maps must match frame dimensions')
    residual = frames - truths
    transitions = []
    for t in range(1, n):
        roi = masks[t-1] & masks[t]
        count = int(roi.sum())
        residual_delta = np.abs(residual[t]-residual[t-1])[roi]
        raw_delta = np.abs(frames[t]-frames[t-1])[roi]
        transitions.append({
            'from': t-1, 'to': t, 'dt_ns': stamps[t]-stamps[t-1],
            'translation_m': float(np.linalg.norm(poses[t, :3, 3]-poses[t-1, :3, 3])),
            'roi_pixels': count,
            'residual_change_mae': float(residual_delta.mean()) if count else None,
            'raw_change_mae': float(raw_delta.mean()) if count else None,
            'seam_motion': boundary_motion(boundaries[t-1], boundaries[t]),
        })
    return {'num_frames': n, 'transitions': transitions,
            'interpretation': 'vehicle-view residual change; no optical-flow compensation; not pure flicker'}


def independent_edge_error(rgb, truth, roi, tolerance_px=2):
    """Extra fused edges relative to direct RGB edges; not object-level ghost detection."""
    actual = simple_edge_detection(rgb_to_gray(rgb), .08) & roi
    expected = simple_edge_detection(rgb_to_gray(truth), .08)
    if expected.any():
        unexplained = actual & (ndi.distance_transform_edt(~expected) > tolerance_px)
    else:
        unexplained = actual.copy()
    count = int(actual.sum())
    return {'output_edge_pixels': count, 'extra_edge_pixels': int(unexplained.sum()),
            'extra_edge_fraction': float(unexplained.sum()/count) if count else None}


def _verified(root, name, hashes):
    path = (root/name).resolve()
    if (not path.is_relative_to(root) or name not in hashes
            or hashlib.sha256(path.read_bytes()).hexdigest() != hashes[name]):
        raise ValueError(f'unsafe path or checksum mismatch: {name}')
    return path


def load_sequence(dataset, capture):
    dataset, capture = Path(dataset).resolve(), Path(capture).resolve()
    cfg = json.loads((dataset/'config.json').read_text())
    manifest = json.loads((dataset/'manifest.json').read_text())
    truth = json.loads((capture/'paired_truth.json').read_text())
    if truth['config'] != cfg:
        raise ValueError('paired truth configuration does not match camera dataset')
    capture_digest = hashlib.sha256((capture/'capture.json').read_bytes()).hexdigest()
    ground = json.loads((dataset/'ground_truth.json').read_text())
    if truth['capture_sha256'] != capture_digest or ground['capture_sha256'] != capture_digest:
        raise ValueError('camera inputs and truth must originate in the same capture')
    if len(manifest['frames']) != len(truth['frames']) or len(truth['frames']) < 2:
        raise ValueError('matching multi-frame input/truth sequences required')
    ids = [c['id'] for c in cfg['cameras']]
    if ids != list(range(4)) or manifest['calibration_ids'] != [c['calibration_id'] for c in cfg['cameras']]:
        raise ValueError('ordered camera IDs 0..3 and matching calibrations required')
    images, reference, masks, poses, stamps = [], [], [], [], []
    for index, (row, target) in enumerate(zip(manifest['frames'], truth['frames'])):
        if (int(row['scenario_timestamp_ns']) != int(target['scenario_timestamp_ns'])
                or row.get('offset_ns') != [0]*4
                or ground['frames'][index]['T_world_from_vehicle'] != target['T_world_from_vehicle']):
            raise ValueError('input/truth timestamps, synchronization or poses disagree')
        if len(row['paths']) != 4:
            raise ValueError('four camera images required')
        frame = []
        for camera, name in zip(cfg['cameras'], row['paths']):
            if not Path(name).name.startswith(f"camera{camera['id']}_"):
                raise ValueError('camera image order does not match config')
            with Image.open(_verified(dataset, name, manifest['sha256'])) as image:
                value = np.asarray(image.convert('RGB'))
            if value.shape != (camera['resolution']['height'], camera['resolution']['width'], 3):
                raise ValueError('camera image resolution mismatch')
            frame.append(value)
        images.append(frame)
        with Image.open(_verified(capture, target['rgb'], truth['sha256'])) as image:
            reference.append(np.asarray(image.convert('RGB'), float)/255.)
        labels = np.load(_verified(capture, target['objects'], truth['sha256']), allow_pickle=False)
        expected_shape = (cfg['output']['height'], cfg['output']['width'])
        if labels.shape != expected_shape or reference[-1].shape != (*expected_shape, 3):
            raise ValueError('truth dimensions mismatch')
        if not np.issubdtype(labels.dtype, np.integer):
            raise ValueError('object truth must contain integer labels')
        # Remove ego, background and two pixels on both sides of every object boundary.
        boundary = (ndi.maximum_filter(labels, size=3) != ndi.minimum_filter(labels, size=3))
        roi = (labels != 0) & ~np.isin(labels, truth['ego_object_ids'])
        masks.append(roi & ~ndi.binary_dilation(boundary, iterations=2))
        stamps.append(target['scenario_timestamp_ns'])
        poses.append(target['T_world_from_vehicle'])
    return cfg, images, reference, masks, stamps, poses


def run_temporal_analysis(dataset, capture, output):
    cfg, images, truths, masks, stamps, poses = load_sequence(dataset, capture)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    results = {}
    for mode in FUSION_MODES:
        config = copy.deepcopy(cfg)
        config['fusion'] = {'mode': mode}
        rendered = [render(config, frame) for frame in images]
        frames = [r['rgb'].astype(float)/255 for r in rendered]
        # Boundary extraction needs the actual per-camera validity, not just total coverage.
        from reference import sample
        valid = [np.stack([sample(c, r['world'], im)[1] & r['hit']
                           for c, im in zip(config['cameras'], frame)], axis=-1)
                 for r, frame in zip(rendered, images)]
        rois = [mask & r['evaluation'] & (r['coverage'] > 0) for mask, r in zip(masks, rendered)]
        result = evaluate_temporal_stability(frames, truths, [r['weights'] for r in rendered],
                                              valid, rois, stamps, poses)
        result['frames'] = []
        for index, (rgb, target, roi) in enumerate(zip(frames, truths, rois)):
            result['frames'].append({'roi_pixels': int(roi.sum()),
                'mae': float(np.abs(rgb-target)[roi].mean()) if roi.any() else None,
                'edges': independent_edge_error(rgb, target, roi)})
            Image.fromarray(rendered[index]['rgb']).save(output/f'{mode}_{index:04d}.png')
        results[mode] = result
    inputs = {'dataset': str(Path(dataset).resolve()), 'capture': str(Path(capture).resolve())}
    hashes = {str(Path(root)/name): hashlib.sha256((Path(root)/name).read_bytes()).hexdigest()
              for root, names in ((dataset, ('config.json', 'manifest.json', 'ground_truth.json')),
                                  (capture, ('capture.json', 'paired_truth.json')))
              for name in names}
    code_hashes = {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                   for name in ('temporal_seam_stability.py', 'reference.py', 'fusion.py', 'stitch_metrics.py')}
    report = {'schema_version': 2, 'inputs': inputs, 'input_sha256': hashes,
              'code_sha256': code_hashes, 'results': results}
    (output/'temporal_stability.json').write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dataset', required=True)
    parser.add_argument('--capture', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    run_temporal_analysis(args.dataset, args.capture, args.output)
