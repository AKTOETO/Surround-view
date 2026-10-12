"""Read-only paired local supplied-height control; no RGB-derived geometry."""
import json
import numpy as np
from PIL import Image
from source_correspondence import sha, verified, load_product, describe, project
from source_correspondence_controls import transform_points
from displaced_correspondence import chroma_mask, linearize
from reference_height import mask_scores


def vehicle_center(world_center, pose):
    """Explicit world-to-vehicle conversion of supplied geometry (includes rotation)."""
    return transform_points(np.linalg.inv(np.asarray(pose)), np.asarray([world_center]))[0]


def collect(root, plan_path):
    plan = json.loads(plan_path.read_text())
    for name, digest in plan['frozen_files'].items():
        verified(root / name, digest)
    output = root / plan['output_root']
    inv_path = output / 'invocations.json'
    inv = json.loads(inv_path.read_text())
    if inv['plan_sha256'] != sha(plan_path) or inv['binary_sha256'] != plan['binary_sha256']:
        raise ValueError('capture binding differs')
    verified(output / 'sv-correspondence-probe', plan['binary_sha256'])
    expected = [(f, v) for f in plan['frames'] for v in plan['variants']]
    if [(c['frame'], c['variant']) for c in inv['cases']] != expected or any(c['returncode'] for c in inv['cases']):
        raise ValueError('capture cases differ')
    truth = json.loads((root / plan['truth_path']).read_text())
    capture = (root / plan['truth_path']).parent
    target_id = next(v for k, v in truth['objects'].items() if 'coded object target' in k)
    road_id = next(v for k, v in truth['objects'].items() if k.startswith('SV road.'))
    rows = []
    baselines = {}
    fingerprints = set()
    for call in inv['cases']:
        frame, variant, name = call['frame'], call['variant'], call['name']
        d = output / name
        meta = json.loads((d / 'report.json').read_text())
        cfg = root / plan['config_root'] / (name + '.json')
        if (meta['config_sha256'] != sha(cfg) or meta['frame_index'] != frame
                or not meta['normal_output_unchanged'] or meta['source_revision'] != plan['source_revision']
                or meta['manifest_sha256'] != plan['manifest_sha256']):
            raise ValueError('native capture differs')
        fingerprints.add(meta['source_fingerprint'])
        for source in meta['inputs']:
            verified(root / source['path'], source['sha256'])
        products = {p['file']: load_product(d, p) for p in meta['products']}
        rgb = products['output.rgba'][..., :3]
        t = truth['frames'][frame]
        ids = np.load(verified(capture / t['objects'], truth['sha256'][t['objects']]), allow_pickle=False)
        direct = np.asarray(Image.open(verified(capture / t['rgb'], truth['sha256'][t['rgb']])).convert('RGB'))
        vis = np.load(verified(capture / t['visibility'], truth['sha256'][t['visibility']]), allow_pickle=False)
        target, road = ids == target_id, ids == road_id
        visible = target & (vis != 0)
        proxy = chroma_mask(rgb, plan['chroma_threshold'])
        metrics = mask_scores(proxy, target)
        metrics.update(visible_target_proxy_hits=int((proxy & visible).sum()), visible_target_pixels=int(visible.sum()))
        regions = products['carrier-regions.u8'][..., 0]
        covered = (regions != 0) & (regions != 5)
        positions = products['carrier-positions.f32']
        points = np.load(root / plan['reference_root'] / f'frame{frame}/direct-points.npy', allow_pickle=False)
        ego = np.asarray(t['T_world_from_vehicle'])
        world = transform_points(ego, positions[target & covered])
        metrics['carrier_to_virtual_target_distance_m'] = describe(np.linalg.norm(world - points[target & covered], axis=1))
        metrics['target_carrier_coverage_pixels'] = int((target & covered).sum())
        vehicle = transform_points(np.linalg.inv(ego), points[target])
        y, x = np.argwhere(target).T
        errors = []
        for i, camera in enumerate(meta['config']['cameras']):
            uv, valid = project(camera, vehicle)
            actual = products[f'camera{i}-pixels.f32'][y, x]
            status = products[f'camera{i}-status.u8'][y, x, 0]
            selected = valid & (status == 1) & ((vis[target] & (1 << i)) != 0)
            errors.extend(np.linalg.norm(actual[selected] - uv[selected], axis=1))
        metrics['visible_point_projection_error_px'] = describe(errors)
        metrics['visible_point_projection_pairs'] = len(errors)
        metrics['road_linear_rgb_mae'] = float(np.abs(linearize(rgb[road] / 255) - linearize(direct[road] / 255)).mean())
        metrics['road_pixels'] = int(road.sum())
        metrics['road_carrier_coverage_pixels'] = int((road & covered).sum())
        if variant == 'global_080':
            verified(d / 'output.rgba', plan['global_output_sha256'][str(frame)])
        if variant == 'plane':
            old = root / plan['reference_root'] / f'frame{frame}/native/plane/output.rgba'
            if sha(d / 'output.rgba') != sha(old):
                raise ValueError('baseline RGBA differs')
            baselines[frame] = rgb.copy()
        metrics['road_changed_pixels_vs_plane'] = int((np.any(rgb != baselines[frame], axis=2) & road).sum())
        rows.append(dict(frame=frame, variant=variant, surface=meta['config']['surface'],
                         directory=str(d.relative_to(root)), source_revision=meta['source_revision'],
                         source_fingerprint=meta['source_fingerprint'], device=meta['device'],
                         report_sha256=sha(d / 'report.json'), output_sha256=sha(d / 'output.rgba'), **metrics))
    if len(fingerprints) != 1:
        raise ValueError('mixed native fingerprints')
    return dict(schema_version=1, scope=plan['scope'], plan_sha256=sha(plan_path),
                collector_sha256=sha(__file__), binary_sha256=plan['binary_sha256'],
                invocations_sha256=sha(inv_path), rows=rows, limitations=plan['limitations'])
