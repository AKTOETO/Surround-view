"""Read-only entry-distance attribution and fixed supplied-box central filters."""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image
from source_correspondence import sha, verified, load_product, describe
from source_visibility import blocked_by_faces
from source_footprint_audit import footprint
from uncertainty_facing import compose, face_masks
from visibility_uncertainty import certificates
from displaced_correspondence import linearize, chroma_mask
from native_facing import collect as validate_native
from native_uncertainty import checked_plan


def entry_gap(origin, points, minimum, maximum):
    """Distance from first finite-face hit to endpoint; NaN for no segment hit.

    Independent six-plane intersection, not native slab code. Closed AABB:
    an origin inside/on its boundary has entry0, matching reference semantics.
    """
    o, p, lo, hi = [np.asarray(v, float) for v in (origin, points, minimum, maximum)]
    if o.shape != (3,) or p.ndim != 2 or p.shape[1] != 3 or lo.shape != (3,) or hi.shape != (3,):
        raise ValueError('expected XYZ origin/bounds and Nx3 endpoints')
    if not all(np.isfinite(v).all() for v in (o, p, lo, hi)) or np.any(lo >= hi):
        raise ValueError('finite nonempty bounds required')
    delta = p - o
    length = np.linalg.norm(delta, axis=1)
    if np.all((o >= lo) & (o <= hi)):
        return length
    first = np.full(len(p), np.inf)
    for axis in range(3):
        for plane in (lo[axis], hi[axis]):
            t = np.divide(plane - o[axis], delta[:, axis], out=np.full(len(p), np.inf),
                          where=np.abs(delta[:, axis]) > 1e-12)
            finite_t = np.where(np.isfinite(t), t, 0)
            hit = (t >= 0) & (t <= 1)
            for other in range(3):
                if axis != other:
                    coordinate = o[other] + finite_t * delta[:, other]
                    hit &= (coordinate >= lo[other] - 1e-10) & (coordinate <= hi[other] + 1e-10)
            first = np.minimum(first, np.where(hit, t, np.inf))
    return np.where(np.isfinite(first), (1 - np.where(np.isfinite(first), first, 1)) * length, np.nan)


def collect(root, path):
    plan = json.loads(path.read_text())
    for name, digest in plan['frozen_files'].items():
        verified(root / name, digest)
    primary = json.loads((root / plan['primary_report']).read_text())
    if validate_native(root, root / plan['native_plan']) != primary:
        raise ValueError('native parent audit differs')
    native = checked_plan(root, root / plan['native_plan'])
    parent = checked_plan(root, root / native['parent_native_plan'])
    baseline_report = json.loads((root / native['primary_report']).read_text())
    sensitivity = json.loads((root / parent['sensitivity_plan']).read_text())
    truth_path = root / parent['truth_path']
    truth = json.loads(truth_path.read_text())
    target_id = next(v for k, v in truth['objects'].items() if 'coded object target' in k)
    road_id = next(v for k, v in truth['objects'].items() if k.startswith('SV road.'))
    rows = []
    for original in primary['rows']:
        if not original['facing_enabled']:
            continue
        directory = root / original['directory']
        meta = json.loads(verified(directory / 'report.json', original['report_sha256']).read_text())
        if meta['config']['surface']['type'] != 'rectangular_bowl_v1' or meta['config'].get('fusion', {}).get('mode', 'edge_feather') != 'edge_feather':
            raise ValueError('only frozen rectangular edge-feather fallback supported')
        products = {p['file']: load_product(directory, p) for p in meta['products']}
        frame = original['frame']; t = truth['frames'][frame]
        ids = np.load(verified(truth_path.parent / t['objects'], truth['sha256'][t['objects']]), allow_pickle=False)
        vis = np.load(verified(truth_path.parent / t['visibility'], truth['sha256'][t['visibility']]), allow_pickle=False).ravel()
        direct = np.asarray(Image.open(verified(truth_path.parent / t['rgb'], truth['sha256'][t['rgb']])).convert('RGB')).reshape(-1, 3)
        target, road = (ids == target_id).ravel(), (ids == road_id).ravel()
        visible = target & (vis != 0)
        count = ids.size; width = ids.shape[1]
        points = products['carrier-positions.f32'].reshape(count, 3)
        rgb = products['output.rgba'][..., :3].reshape(count, 3)
        colors = np.zeros((count, 4, 3)); weights = np.zeros((count, 4))
        eligible = np.zeros((count, 4), bool); references = eligible.copy()
        removals = {c['name']: eligible.copy() for c in plan['candidates']}
        box = meta['config']['surface']['source_occlusion_box']
        reference = sensitivity['nominal_boxes_by_frame'][str(frame)]
        pairs = []
        for camera, cfg in enumerate(meta['config']['cameras']):
            valid = products[f'camera{camera}-status.u8'][..., 0].ravel() == 1
            eligible[:, camera] = valid
            indices = np.flatnonzero(valid)
            q = points[valid]; o = np.linalg.inv(np.asarray(cfg['T_camera_from_vehicle']))[:3, 3]
            gap = entry_gap(o, q, box['min_m'], box['max_m'])
            ref_gap = entry_gap(o, q, reference['min_m'], reference['max_m'])
            ref = blocked_by_faces(o, q, reference['min_m'], reference['max_m'], sensitivity['nominal_reference_bias_m'])
            references[valid, camera] = ref
            nominal_blocked = blocked_by_faces(o, q, box['min_m'], box['max_m'], box['tolerance_m'])
            uncertainty = box.get('uncertainty', {'radius_m': 0})
            definite, clear = certificates(o, q, box['min_m'], box['max_m'], box['tolerance_m'], uncertainty['radius_m'])
            near, facing = face_masks(o, q, box['min_m'], box['max_m'], box.get('support_tolerance_m', 1e-5))
            uv = products[f'camera{camera}-pixels.f32'].reshape(count, 2)[valid]
            source = meta['inputs'][camera]
            image = np.asarray(Image.open(verified(root / source['path'], source['sha256'])).convert('RGB'))
            xy, coefficients = footprint(uv, image.shape[1], image.shape[0])
            colors[valid, camera] = (image[xy[..., 1], xy[..., 0]] / 255 * coefficients[..., None]).sum(axis=1)
            edge = np.minimum.reduce([uv[:, 0], uv[:, 1], image.shape[1]-1-uv[:, 0], image.shape[0]-1-uv[:, 1]])
            weights[valid, camera] = np.clip(edge / meta['config'].get('fusion', {}).get('edge_width_px', 24), 0, 1)
            for c in plan['candidates']:
                removals[c['name']][valid, camera] = blocked_by_faces(o, q, box['min_m'], box['max_m'], c['tolerance_m'])
            for j in np.flatnonzero(ref):
                pixel = indices[j]; y, x = divmod(int(pixel), width)
                pairs.append(dict(y=y, x=x, camera=camera, virtual_roi='target' if target[pixel] else 'road' if road[pixel] else 'other',
                    target_visible_to_camera=bool(target[pixel] and vis[pixel] & (1 << camera)),
                    carrier_xyz_m=q[j].tolist(), camera_origin_m=o.tolist(),
                    direction_cosines=((q[j]-o)/np.linalg.norm(q[j]-o)).tolist(),
                    reference_gap_m=float(ref_gap[j]), supplied_gap_m=float(gap[j]) if np.isfinite(gap[j]) else None,
                    supplied_central_blocked=bool(nominal_blocked[j]), uncertainty_class='blocked' if definite[j] else 'clear' if clear[j] else 'ambiguous',
                    near_face_bits=int((near[j] * (1 << np.arange(6))).sum()), facing_face_bits=int((facing[j] * (1 << np.arange(6))).sum()),
                    base_weight=float(weights[pixel, camera]), rejected_by={name: bool(mask[pixel, camera]) for name, mask in removals.items()}))
        if int(references.sum()) != original['reference_blocked_pairs']:
            raise ValueError('reference count differs')
        predicted, supported, _ = compose(colors, weights)
        error = np.abs(predicted[supported] - rgb[supported])
        if error.size and error.max() > plan['reconstruction_error_limit_lsb']:
            raise ValueError('unexplained native RGB; counterfactual invalid')
        baseline = next(r for r in baseline_report['rows'] if r['frame'] == frame and r['kind'] == 'center' and r['shift'] == 'nom' and r['policy'] == 'nominal')
        candidates = []
        for c in plan['candidates']:
            removed = removals[c['name']]; retained = eligible & ~removed
            proposed, supported_after, _ = compose(colors, np.where(removed, 0, weights))
            changed = (removed & (weights > 0)).any(axis=1)
            after = rgb.copy()
            after[changed & supported_after] = np.rint(np.clip(proposed[changed & supported_after], 0, 255)).astype(np.uint8)
            after[changed & ~supported_after] = np.rint(np.array([.23, .24, .24]) * 255).astype(np.uint8)
            proxy = chroma_mask(after)
            metrics = dict(visible_proxy_hits=int((visible & proxy).sum()), visible_without_source=int((visible & ~retained.any(axis=1)).sum()),
                visible_losing_all_positive_weights=int((visible & supported & ~supported_after).sum()),
                removed_pairs=int(removed.sum()), removed_reference_pairs=int((references & removed).sum()),
                outside_proxy=int((~target & proxy).sum()), reference_blocked_pairs=int((references & ~removed).sum()),
                road_linear_rgb_mae=float(np.abs(linearize(after[road] / 255) - linearize(direct[road] / 255)).mean()))
            gates=dict(all_visible_hits=metrics['visible_proxy_hits']==int(visible.sum()), no_visible_without_source=metrics['visible_without_source']==0,
                outside_below_nominal_center=metrics['outside_proxy']<baseline['outside_expected_pixels'],
                road_not_worse_than_nominal_center=metrics['road_linear_rgb_mae']<=baseline['road_linear_rgb_mae'],
                no_reference_blocking_increase=metrics['reference_blocked_pairs']<=baseline['nominal_blocked_retained_camera_pairs'])
            candidates.append(dict(**c, hypothetical_metrics=metrics, hypothetical_gates=gates, hypothetical_all_gates=all(gates.values())))
        rows.append(dict(frame=frame, variant=original['variant'], shift=original['shift'], kind=original['kind'], policy=original['policy'],
            report_sha256=original['report_sha256'], output_sha256=original['output_sha256'],
            uncertainty_radius_m=box.get('uncertainty', {}).get('radius_m', 0), reference_pairs=pairs,
            reconstruction_error_lsb=describe(error.ravel()), candidates=candidates))
    if len(rows) != 36:
        raise ValueError('incomplete selection')
    return dict(schema_version=1, scope=plan['scope'], phase=plan['phase'], plan_sha256=sha(path), collector_sha256=sha(__file__), dependency_sha256={name: sha(root / 'tools/research' / name) for name in ['source_correspondence.py', 'source_visibility.py', 'source_footprint_audit.py', 'uncertainty_facing.py', 'visibility_uncertainty.py', 'displaced_correspondence.py', 'native_facing.py', 'native_uncertainty.py']}, rows=rows, limitations=plan['limitations'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, default=Path('configs/research/facing-entry-gap-plan.json'))
    parser.add_argument('--output-report', type=Path, required=True)
    args = parser.parse_args()
    report = collect(Path(__file__).resolve().parents[2], args.plan.resolve())
    args.output_report.parent.mkdir(parents=True, exist_ok=True)
    args.output_report.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
