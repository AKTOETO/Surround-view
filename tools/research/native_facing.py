"""Frozen native finite-face orientation candidate, with independent source audit."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image
from native_uncertainty import checked_plan, capture
from source_correspondence import sha, verified, load_product, describe
from source_visibility import blocked_by_faces
from source_footprint_audit import footprint
from displaced_correspondence import chroma_mask, linearize
from uncertainty_facing import face_masks, compose


def prepare(root, path):
    plan = json.loads(path.read_text())
    for c in plan['conditions']:
        template = root / c['template']
        baseline = verified(root / c['baseline_config'], plan['frozen_files'][c['baseline_config']])
        cfg = json.loads(baseline.read_text())
        if c['facing_enabled']:
            cfg['surface']['source_occlusion_box']['require_facing_surface'] = True
        content = json.dumps(cfg, indent=2) + '\n'
        if hashlib.sha256(content.encode()).hexdigest() != plan['frozen_files'][c['template']]:
            raise ValueError('derived template differs from frozen hash')
        if template.exists():
            verified(template, plan['frozen_files'][c['template']])
        else:
            template.parent.mkdir(parents=True, exist_ok=True)
            template.write_text(content)


def collect(root, path):
    plan = checked_plan(root, path)
    primary = json.loads((root / plan['primary_report']).read_text())
    parent = checked_plan(root, root / plan['parent_native_plan'])
    if primary['plan_sha256'] != sha(root / plan['parent_native_plan']):
        raise ValueError('parent report binding differs')
    sensitivity = json.loads((root / parent['sensitivity_plan']).read_text())
    truth_path = root / parent['truth_path']
    truth = json.loads(truth_path.read_text())
    target_id = next(v for k, v in truth['objects'].items() if 'coded object target' in k)
    road_id = next(v for k, v in truth['objects'].items() if k.startswith('SV road.'))
    output = root / plan['output_root']
    verified(output / 'sv-correspondence-probe', plan['binary_sha256'])
    inv_path = output / 'invocations.json'
    inv = json.loads(inv_path.read_text())
    if (inv['plan_sha256'] != sha(path) or inv['binary_sha256'] != plan['binary_sha256']
            or [(r['frame'], r['variant']) for r in inv['cases']] != [(c['frame'], c['variant']) for c in plan['conditions']]):
        raise ValueError('capture selection/binding differs')
    rows = []
    for condition, call in zip(plan['conditions'], inv['cases'], strict=True):
        if call['returncode']:
            raise ValueError('failed native capture')
        frame = condition['frame']
        original = next(r for r in primary['rows'] if r['frame'] == frame and r['variant'] == condition['baseline_variant'])
        base_dir = root / original['directory']
        before_meta = json.loads(verified(base_dir / 'report.json', original['report_sha256']).read_text())
        before = {p['file']: load_product(base_dir, p) for p in before_meta['products']}
        directory = output / call['name']
        meta = json.loads((directory / 'report.json').read_text())
        config_path = root / plan['config_root'] / (call['name'] + '.json')
        if (meta['source_revision'] != plan['source_revision'] or meta['source_fingerprint'] != plan['source_fingerprint']
                or meta['config_sha256'] != sha(config_path) or call['config_sha256'] != sha(config_path)
                or meta['frame_index'] != frame or not meta['normal_output_unchanged']
                or meta['manifest_sha256'] != before_meta['manifest_sha256'] or meta['inputs'] != before_meta['inputs']
                or plan['required_device_substring'] not in meta['device']):
            raise ValueError('native capture provenance differs')
        expected_config = copy.deepcopy(before_meta['config'])
        if condition['facing_enabled']:
            expected_config['surface']['source_occlusion_box']['require_facing_surface'] = True
        if meta['config'] != expected_config or json.loads(config_path.read_text()) != json.loads((root / condition['template']).read_text()):
            raise ValueError('configuration changed beyond declared facing flag')
        products = {p['file']: load_product(directory, p) for p in meta['products']}
        for key in ['carrier-positions.f32', 'carrier-regions.u8']:
            if not np.array_equal(products[key], before[key], equal_nan=True):
                raise ValueError('carrier changed')
        t = truth['frames'][frame]
        ids = np.load(verified(truth_path.parent / t['objects'], truth['sha256'][t['objects']]), allow_pickle=False)
        vis = np.load(verified(truth_path.parent / t['visibility'], truth['sha256'][t['visibility']]), allow_pickle=False)
        direct = np.asarray(Image.open(verified(truth_path.parent / t['rgb'], truth['sha256'][t['rgb']])).convert('RGB'))
        target, road = ids == target_id, ids == road_id
        visible = target & (vis != 0)
        rgb = products['output.rgba'][..., :3]
        statuses = np.stack([products[f'camera{i}-status.u8'][..., 0] for i in range(4)], axis=-1)
        count = ids.size
        colors = np.zeros((count, 4, 3)); weights = np.zeros((count, 4))
        comparisons = []; retained_reference = 0
        box = meta['config']['surface']['source_occlusion_box']
        for i, camera in enumerate(meta['config']['cameras']):
            old = before[f'camera{i}-status.u8'][..., 0]
            valid = old == 1
            origin = np.linalg.inv(np.asarray(camera['T_camera_from_vehicle']))[:3, 3]
            near, facing = face_masks(origin, before['carrier-positions.f32'][valid], box['min_m'], box['max_m'], box.get('support_tolerance_m', 1e-5))
            expected = old.copy()
            if condition['facing_enabled']:
                expected[valid] = np.where(near.any(axis=1) & ~facing.any(axis=1), 9, 1)
            mismatches = np.argwhere(expected != statuses[..., i])
            comparisons.append(dict(camera=i, checked_pairs=int(valid.sum()), oracle_mismatches=len(mismatches), first_mismatch_yx=mismatches[:16].tolist()))
            retained = statuses[..., i] == 1
            if np.any(retained & ~valid) or not np.array_equal(products[f'camera{i}-pixels.f32'][retained], before[f'camera{i}-pixels.f32'][retained]):
                raise ValueError('facing created a sample or changed retained UV')
            rejected_uv = products[f'camera{i}-pixels.f32'][statuses[..., i] == 9]
            if not np.isnan(rejected_uv).all():
                raise ValueError('rejected facing source has finite UV')
            reference = sensitivity['nominal_boxes_by_frame'][str(frame)]
            retained_reference += int(blocked_by_faces(origin, products['carrier-positions.f32'][retained], reference['min_m'], reference['max_m'], sensitivity['nominal_reference_bias_m']).sum())
            source = meta['inputs'][i]
            image = np.asarray(Image.open(verified(root / source['path'], source['sha256'])).convert('RGB'))
            uv = products[f'camera{i}-pixels.f32'][retained]
            xy, coefficients = footprint(uv, image.shape[1], image.shape[0])
            colors[retained.ravel(), i] = (image[xy[..., 1], xy[..., 0]] / 255 * coefficients[..., None]).sum(axis=1)
            edge = np.minimum.reduce([uv[:, 0], uv[:, 1], image.shape[1]-1-uv[:, 0], image.shape[0]-1-uv[:, 1]])
            weights[retained.ravel(), i] = np.clip(edge / meta['config'].get('fusion', {}).get('edge_width_px', 24), 0, 1)
        predicted, supported, _ = compose(colors, weights)
        error = np.abs(predicted[supported] - rgb.reshape(count, 3)[supported])
        reconstruction_valid = not error.size or bool(error.max() <= 2)
        proxy = chroma_mask(rgb)
        no_source = ~np.any(statuses == 1, axis=-1)
        metrics = dict(visible_proxy_hits=int((visible & proxy).sum()), visible_target_pixels=int(visible.sum()),
            visible_without_source=int((visible & no_source).sum()), outside_proxy=int((~target & proxy).sum()),
            reference_blocked_pairs=retained_reference, backfacing_rejected_pairs=int((statuses == 9).sum()),
            road_linear_rgb_mae=float(np.abs(linearize(rgb[road] / 255) - linearize(direct[road] / 255)).mean()))
        baseline = next(r for r in primary['rows'] if r['frame'] == frame and r['kind'] == 'center' and r['shift'] == 'nom' and r['policy'] == 'nominal')
        gates = dict(all_visible_hits=metrics['visible_proxy_hits'] == metrics['visible_target_pixels'],
            no_visible_without_source=metrics['visible_without_source'] == 0,
            outside_below_nominal_center=metrics['outside_proxy'] < baseline['outside_expected_pixels'],
            road_not_worse_than_nominal_center=metrics['road_linear_rgb_mae'] <= baseline['road_linear_rgb_mae'],
            no_reference_blocking_increase=retained_reference <= baseline['nominal_blocked_retained_camera_pairs'])
        archived_match = sha(directory / 'output.rgba') == original['output_sha256']
        if not condition['facing_enabled'] and not archived_match:
            raise ValueError('disabled facing changed historical RGBA')
        rows.append(dict(**condition, directory=str(directory.relative_to(root)), device=meta['device'],
            report_sha256=sha(directory / 'report.json'), output_sha256=sha(directory / 'output.rgba'),
            comparisons=comparisons, geometry_unchanged=True, retained_uv_unchanged=True,
            archived_rgba_match=archived_match, reconstruction_error_lsb=describe(error.ravel()),
            reconstruction_valid=reconstruction_valid, gates=gates, all_gates=all(gates.values()), **metrics))
    return dict(schema_version=1, scope=plan['scope'], phase=plan['phase'], plan_sha256=sha(path),
        collector_sha256=sha(__file__), binary_sha256=plan['binary_sha256'], invocations_sha256=sha(inv_path),
        rows=rows, limitations=plan['limitations'])


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, default=root / 'configs/research/native-facing-plan.json')
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--binary', type=Path, default=root / 'build/sv-correspondence-probe')
    parser.add_argument('--output-report', type=Path)
    args = parser.parse_args()
    if args.capture:
        prepare(root, args.plan)
        capture(root, args.plan, args.binary)
    if args.output_report:
        report = collect(root, args.plan)
        args.output_report.parent.mkdir(parents=True, exist_ok=True)
        args.output_report.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
