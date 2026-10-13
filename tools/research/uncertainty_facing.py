"""Post-hoc finite supplied-face orientation control on frozen native samples."""
import json
import numpy as np
from PIL import Image
from source_correspondence import sha, verified, load_product, describe
from source_visibility import blocked_by_faces
from source_footprint_audit import footprint
from displaced_correspondence import linearize, encode, chroma_mask
from native_uncertainty import checked_plan


def face_masks(origin, points, minimum, maximum, band):
    """Six finite faces in order -X,+X,-Y,+Y,-Z,+Z; strict outward half-space."""
    origin, points, lo, hi = [np.asarray(v, float) for v in (origin, points, minimum, maximum)]
    if origin.shape != (3,) or points.ndim != 2 or points.shape[1] != 3 or lo.shape != (3,) or hi.shape != (3,):
        raise ValueError('expected XYZ origin/bounds and Nx3 points')
    if not all(np.isfinite(v).all() for v in (origin, points, lo, hi)) or np.any(lo >= hi):
        raise ValueError('invalid geometry')
    if not np.isfinite(band) or band < 0:
        raise ValueError('finite nonnegative band required')
    finite = ((points >= lo - band) & (points <= hi + band)).all(axis=1)
    near = np.column_stack([finite & (np.abs(points[:, a] - plane) <= band)
                            for a in range(3) for plane in (lo[a], hi[a])])
    outward = np.array([origin[a] < lo[a] if side == 0 else origin[a] > hi[a]
                        for a in range(3) for side in (0, 1)])
    return near, near & outward


def compose(colors, weights):
    colors, weights = np.asarray(colors, float), np.asarray(weights, float)
    if colors.ndim != 3 or colors.shape[-1] != 3 or weights.shape != colors.shape[:2]:
        raise ValueError('expected NxCx3 colors and NxC weights')
    if not np.isfinite(colors).all() or not np.isfinite(weights).all() or np.any(weights < 0):
        raise ValueError('finite colors and nonnegative weights required')
    total = weights.sum(axis=1)
    supported = total > 1e-6
    normalized = np.divide(weights, total[:, None], out=np.zeros_like(weights), where=supported[:, None])
    return encode((linearize(colors) * normalized[..., None]).sum(axis=1)) * 255, supported, normalized


def collect(root, path):
    plan = json.loads(path.read_text())
    for name, digest in plan['frozen_files'].items():
        verified(root / name, digest)
    native_plan = checked_plan(root, root / plan['native_plan'])
    primary = json.loads((root / plan['primary_report']).read_text())
    if primary['plan_sha256'] != sha(root / plan['native_plan']):
        raise ValueError('primary binding differs')
    sensitivity = json.loads((root / native_plan['sensitivity_plan']).read_text())
    truth_path = root / native_plan['truth_path']
    truth = json.loads(truth_path.read_text())
    target_id = next(v for k, v in truth['objects'].items() if 'coded object target' in k)
    road_id = next(v for k, v in truth['objects'].items() if k.startswith('SV road.'))
    # Pin all native payloads, not merely the selected camera-pair subset.
    def read(row):
        directory = root / row['directory']
        meta = json.loads(verified(directory / 'report.json', row['report_sha256']).read_text())
        return meta, {p['file']: load_product(directory, p) for p in meta['products']}
    rows = []
    for original in primary['rows']:
        if original['policy'] == 'nominal':
            continue
        frame = original['frame']
        meta, products = read(original)
        if meta['config'].get('fusion', {}).get('mode', 'edge_feather') != 'edge_feather':
            raise ValueError('only edge_feather recomposition is defined')
        nominal = next(r for r in primary['rows'] if r['frame'] == frame and r['shift'] == original['shift']
                       and r['kind'] == original['kind'] and r['policy'] == 'nominal')
        nominal_meta, nominal_products = read(nominal)
        if meta['config']['cameras'] != nominal_meta['config']['cameras']:
            raise ValueError('matched camera geometry differs')
        for key in ['carrier-positions.f32', 'carrier-regions.u8']:
            if not np.array_equal(products[key], nominal_products[key], equal_nan=True):
                raise ValueError('matched carrier differs')
        t = truth['frames'][frame]
        ids = np.load(verified(truth_path.parent / t['objects'], truth['sha256'][t['objects']]), allow_pickle=False)
        vis = np.load(verified(truth_path.parent / t['visibility'], truth['sha256'][t['visibility']]), allow_pickle=False)
        direct = np.asarray(Image.open(verified(truth_path.parent / t['rgb'], truth['sha256'][t['rgb']])).convert('RGB'))
        height, width = ids.shape
        count = ids.size
        points = products['carrier-positions.f32'].reshape(count, 3)
        rgb = products['output.rgba'][..., :3].reshape(count, 3)
        target, road = (ids == target_id).ravel(), (ids == road_id).ravel()
        visible = target & (vis.ravel() != 0)
        weights = np.zeros((count, 4)); colors = np.zeros((count, 4, 3))
        removed = np.zeros((count, 4), bool); references = removed.copy(); added = removed.copy()
        eligible = removed.copy(); near_bits = np.zeros((count, 4), np.uint8); facing_bits = near_bits.copy()
        box = meta['config']['surface']['source_occlusion_box']
        reference = sensitivity['nominal_boxes_by_frame'][str(frame)]
        for i, camera in enumerate(meta['config']['cameras']):
            valid = products[f'camera{i}-status.u8'][..., 0].ravel() == 1
            eligible[:, i] = valid
            added[:, i] = valid & (nominal_products[f'camera{i}-status.u8'][..., 0].ravel() != 1)
            uv = products[f'camera{i}-pixels.f32'].reshape(count, 2)[valid]
            source = meta['inputs'][i]
            image = np.asarray(Image.open(verified(root / source['path'], source['sha256'])).convert('RGB'))
            xy, coefficients = footprint(uv, image.shape[1], image.shape[0])
            colors[valid, i] = (image[xy[..., 1], xy[..., 0]] / 255 * coefficients[..., None]).sum(axis=1)
            edge = np.minimum.reduce([uv[:, 0], uv[:, 1], image.shape[1]-1-uv[:, 0], image.shape[0]-1-uv[:, 1]])
            weights[valid, i] = np.clip(edge / meta['config'].get('fusion', {}).get('edge_width_px', 24), 0, 1)
            origin = np.linalg.inv(np.asarray(camera['T_camera_from_vehicle']))[:3, 3]
            near, facing = face_masks(origin, points[valid], box['min_m'], box['max_m'], box.get('support_tolerance_m', 1e-5))
            removed[valid, i] = near.any(axis=1) & ~facing.any(axis=1)
            near_bits[valid, i] = (near * (1 << np.arange(6))).sum(axis=1)
            facing_bits[valid, i] = (facing * (1 << np.arange(6))).sum(axis=1)
            references[valid, i] = blocked_by_faces(origin, points[valid], reference['min_m'], reference['max_m'], sensitivity['nominal_reference_bias_m'])
        if int(references.sum()) != original['nominal_blocked_retained_camera_pairs']:
            raise ValueError('primary reference count differs')
        reconstructed, supported, normalized = compose(colors, weights)
        error = np.abs(reconstructed[supported] - rgb[supported])
        reconstruction_valid = not error.size or bool(error.max() <= 2)
        after_weights = np.where(removed, 0, weights)
        counterfactual, after_supported, _ = compose(colors, after_weights)
        # Retain native pixels unchanged if their positive-weight sources were not removed.
        changed = (removed & (weights > 0)).any(axis=1)
        proposed = rgb.copy()
        proposed[changed & after_supported] = np.rint(np.clip(counterfactual[changed & after_supported], 0, 255)).astype(np.uint8)
        # This series uses rectangular carriers and color view. Its shader fallback is opaque.
        if meta['config']['surface']['type'] != 'rectangular_bowl_v1':
            raise ValueError('counterfactual fallback only defined for rectangular color view')
        proposed[changed & ~after_supported] = np.rint(np.array([.23, .24, .24]) * 255).astype(np.uint8)
        proxy = chroma_mask(proposed)
        retained = eligible & ~removed
        counterfactual_metrics = dict(visible_proxy_hits=int((visible & proxy).sum()),
            visible_without_source=int((visible & ~retained.any(axis=1)).sum()),
            outside_proxy=int((~target & proxy).sum()), reference_blocked_pairs=int((references & ~removed).sum()),
            road_linear_rgb_mae=float(np.abs(linearize(proposed[road] / 255) - linearize(direct.reshape(count, 3)[road] / 255)).mean()))
        baseline = next(r for r in primary['rows'] if r['frame'] == frame and r['kind'] == 'center'
                        and r['shift'] == 'nom' and r['policy'] == 'nominal')
        gates = dict(all_visible_hits=counterfactual_metrics['visible_proxy_hits'] == original['visible_target_pixels'],
            no_visible_without_source=counterfactual_metrics['visible_without_source'] == 0,
            outside_below_nominal_center=counterfactual_metrics['outside_proxy'] < baseline['outside_expected_pixels'],
            road_not_worse_than_nominal_center=counterfactual_metrics['road_linear_rgb_mae'] <= baseline['road_linear_rgb_mae'],
            no_reference_blocking_increase=counterfactual_metrics['reference_blocked_pairs'] <= baseline['nominal_blocked_retained_camera_pairs'])
        pairs = []
        for pixel, camera in np.argwhere(references):
            y, x = divmod(int(pixel), width)
            single_weights = weights[pixel:pixel+1].copy()
            single_weights[0, camera] = 0
            single_rgb, single_supported, _ = compose(colors[pixel:pixel+1], single_weights)
            if not single_supported[0]:
                single_rgb[0] = np.array([.23, .24, .24]) * 255
            pairs.append(dict(y=y, x=x, camera=int(camera), virtual_roi='target' if target[pixel] else 'road' if road[pixel] else 'other',
                visible_target=bool(visible[pixel]), target_visible_to_camera=bool(target[pixel] and vis.ravel()[pixel] & (1 << int(camera))),
                added_vs_nominal=bool(added[pixel, camera]), base_weight=float(weights[pixel, camera]),
                normalized_weight=float(normalized[pixel, camera]), near_face_bits=int(near_bits[pixel, camera]),
                facing_face_bits=int(facing_bits[pixel, camera]), rejected_by_facing=bool(removed[pixel, camera]),
                leave_one_out_max_delta_lsb=float(np.abs(single_rgb[0] - reconstructed[pixel]).max())))
        rows.append(dict(frame=frame, variant=original['variant'], shift=original['shift'], kind=original['kind'], policy=original['policy'],
            native_report_sha256=original['report_sha256'], output_sha256=original['output_sha256'],
            reference_blocked_pairs=len(pairs), added_reference_blocked_pairs=int((references & added).sum()),
            positive_weight_reference_blocked_pairs=int((references & (weights > 0)).sum()),
            face_removed_pairs=int(removed.sum()), face_removed_reference_pairs=int((references & removed).sum()),
            visible_pixels_losing_all_sources=int((visible & eligible.any(axis=1) & ~retained.any(axis=1)).sum()),
            visible_pixels_losing_all_positive_weights=int((visible & supported & ~after_supported).sum()),
            visible_support_before=int((visible & eligible.any(axis=1)).sum()), visible_support_after=int((visible & retained.any(axis=1)).sum()),
            reconstruction_error_lsb=describe(error.ravel()), reconstruction_valid=reconstruction_valid,
            hypothetical_metrics=counterfactual_metrics, hypothetical_gates=gates,
            hypothetical_all_gates=reconstruction_valid and all(gates.values()), reference_pairs=pairs))
    if len(rows) != 36:
        raise ValueError('incomplete candidate selection')
    return dict(schema_version=1, scope=plan['scope'], phase=plan['phase'], plan_sha256=sha(path),
        collector_sha256=sha(__file__), rows=rows, limitations=plan['limitations'])
