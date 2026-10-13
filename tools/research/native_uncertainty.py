"""Frozen production-renderer ambiguity policies: capture and independent audit."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import numpy as np
from PIL import Image
from source_correspondence import sha, verified, load_product
from source_visibility import blocked_by_faces
from visibility_uncertainty import certificates
from box_surface_visibility import on_box_surface
from displaced_correspondence import chroma_mask, linearize
from reference_height import mask_scores


def checked_plan(root, path):
    plan = json.loads(path.read_text())
    for name, digest in plan['frozen_files'].items():
        if name.startswith(('src/', 'include/')):
            # The experiment pins historical native source, not today's working tree.
            content = subprocess.run(['git', 'show', plan['source_revision'] + ':' + name],
                                     cwd=root, check=True, capture_output=True).stdout
            if hashlib.sha256(content).hexdigest() != digest:
                raise ValueError('historical native source checksum differs: ' + name)
        else:
            verified(root / name, digest)
    return plan


def capture(root, path, binary):
    plan = checked_plan(root, path)
    output = root / plan['output_root']
    if (output / 'invocations.json').exists():
        raise ValueError('capture already exists; preserve it and use a separately frozen output plan')
    verified(binary, plan['binary_sha256'])
    output.mkdir(parents=True, exist_ok=True)
    copied = output / 'sv-correspondence-probe'
    shutil.copy2(binary, copied)
    configs = root / plan['config_root']
    configs.mkdir(parents=True, exist_ok=True)
    calls = []
    for condition in plan['conditions']:
        name = f"frame{condition['frame']}-{condition['variant']}"
        cfg = json.loads((root / condition['template']).read_text())
        if condition['policy'] != 'nominal':
            cfg['surface']['source_occlusion_box']['uncertainty'] = dict(
                radius_m=condition['radius_m'], ambiguous_policy=condition['policy'])
        config_path = configs / (name + '.json')
        config_path.write_text(json.dumps(cfg, indent=2) + '\n')
        command = [str(copied), str(config_path), str(root / plan['manifest']),
                   str(output / name), str(condition['frame'])]
        environment = dict(os.environ, EGL_PLATFORM='surfaceless')
        environment.update(plan.get('backend_environment', {}))
        process = subprocess.run(command, capture_output=True, text=True, env=environment)
        calls.append(dict(frame=condition['frame'], variant=condition['variant'], name=name,
                          config_sha256=sha(config_path), argv=command, returncode=process.returncode,
                          stdout=process.stdout, stderr=process.stderr))
        (output / 'invocations.json').write_text(json.dumps(dict(plan_sha256=sha(path),
            binary_sha256=sha(copied), cases=calls), indent=2) + '\n')
        if process.returncode:
            raise RuntimeError(process.stderr)
        device = json.loads((output / name / 'report.json').read_text())['device']
        if plan.get('required_device_substring', '') not in device:
            raise ValueError('capture used an unexpected backend: ' + device)
        print(name, flush=True)


def footprint_blocked(camera, points, pixels, box):
    """Independent nominal neighbour-ray test, WITHOUT the central predicate."""
    result = np.zeros(len(points), bool)
    if box.get('mode', 'center') == 'center':
        return result
    inverse = np.linalg.inv(np.asarray(camera['T_camera_from_vehicle']))
    origin = inverse[:3, 3]
    optics = camera['projection']
    if any(optics['k']) or optics.get('alpha', 0):
        raise ValueError('footprint inverse requires zero-distortion equidistant optics')
    base = np.floor(pixels)
    fraction = pixels - base
    ranges = np.linalg.norm(points - origin, axis=-1)
    guard = box.get('guard_texels', 0)
    for y in range(-guard, 2 + guard):
        for x in range(-guard, 2 + guard):
            positive = np.ones(len(points), bool) if guard else (
                (1 - fraction[:, 0] if x == 0 else fraction[:, 0]) *
                (1 - fraction[:, 1] if y == 0 else fraction[:, 1]) > 0)
            uv = np.clip(base + [x, y], 0, [camera['resolution']['width'] - 1, camera['resolution']['height'] - 1])
            ab = (uv - [optics['cx'], optics['cy']]) / [optics['fx'], optics['fy']]
            theta = np.linalg.norm(ab, axis=-1)
            scale = np.divide(np.sin(theta), theta, out=np.ones_like(theta), where=theta > 1e-14)
            ray = np.column_stack([ab * scale[:, None], np.cos(theta)]) @ inverse[:3, :3].T
            result |= positive & blocked_by_faces(origin, origin + ray * ranges[:, None],
                np.asarray(box['min_m']) + 1e-5, np.asarray(box['max_m']) - 1e-5, box['tolerance_m'])
    if box['mode'] == 'bilinear_box_surface_v1':
        result &= ~on_box_surface(points, box, box.get('support_tolerance_m', 1e-5))
    return result


def expected_status(camera, points, pixels, box):
    origin = np.linalg.inv(np.asarray(camera['T_camera_from_vehicle']))[:3, 3]
    uncertainty = box.get('uncertainty', dict(radius_m=0, ambiguous_policy='reject'))
    blocked, clear = certificates(origin, points, box['min_m'], box['max_m'],
                                  box['tolerance_m'], uncertainty['radius_m'])
    ambiguous = ~blocked & ~clear
    reject_ambiguous = ambiguous & (uncertainty['ambiguous_policy'] == 'reject')
    footprint = footprint_blocked(camera, points, pixels, box)
    return np.where(blocked, 7, np.where(reject_ambiguous, 8, np.where(footprint, 7, 1))), ambiguous


def collect(root, path):
    plan = checked_plan(root, path)
    primary = json.loads((root / plan['primary_report']).read_text())
    sensitivity = json.loads((root / plan['sensitivity_plan']).read_text())
    truth_path = root / plan['truth_path']
    truth = json.loads(truth_path.read_text())
    target_id = next(v for k, v in truth['objects'].items() if 'coded object target' in k)
    road_id = next(v for k, v in truth['objects'].items() if k.startswith('SV road.'))
    output = root / plan['output_root']
    verified(output / 'sv-correspondence-probe', plan['binary_sha256'])
    inv_path = output / 'invocations.json'
    inv = json.loads(inv_path.read_text())
    if inv['plan_sha256'] != sha(path) or inv['binary_sha256'] != plan['binary_sha256']:
        raise ValueError('capture binding differs')
    expected = [(c['frame'], c['variant']) for c in plan['conditions']]
    if [(c['frame'], c['variant']) for c in inv['cases']] != expected:
        raise ValueError('incomplete or reordered captures')
    rows = []
    for condition, call in zip(plan['conditions'], inv['cases']):
        if call['returncode']:
            raise ValueError('failed capture')
        directory = output / call['name']
        meta = json.loads((directory / 'report.json').read_text())
        if plan.get('required_device_substring', '') not in meta['device']:
            raise ValueError('unexpected backend')
        config = root / plan['config_root'] / (call['name'] + '.json')
        if (meta['config_sha256'] != sha(config) or call['config_sha256'] != sha(config)
                or meta['source_revision'] != plan['source_revision']
                or meta['source_fingerprint'] != plan['source_fingerprint']
                or meta['frame_index'] != condition['frame'] or not meta['normal_output_unchanged']
                or meta['manifest_sha256'] != sensitivity['manifest_sha256']):
            raise ValueError('native capture provenance differs')
        expected_config = json.loads((root / condition['template']).read_text())
        if condition['policy'] != 'nominal':
            expected_config['surface']['source_occlusion_box']['uncertainty'] = dict(
                radius_m=condition['radius_m'], ambiguous_policy=condition['policy'])
        if json.loads(config.read_text()) != expected_config:
            raise ValueError('config differs from frozen condition')
        for source in meta['inputs']:
            verified(root / source['path'], source['sha256'])
        products = {p['file']: load_product(directory, p) for p in meta['products']}
        frame = condition['frame']
        baseline = next(r for r in primary['rows'] if r['frame'] == frame and r['variant'] == 'local_100')
        base_dir = root / baseline['directory']
        base_meta = json.loads(verified(base_dir / 'report.json', baseline['report_sha256']).read_text())
        base = {p['file']: load_product(base_dir, p) for p in base_meta['products']}
        for key in ['carrier-positions.f32', 'carrier-regions.u8']:
            if not np.array_equal(products[key], base[key], equal_nan=True):
                raise ValueError('carrier changed')
        if meta['config']['cameras'] != base_meta['config']['cameras']:
            raise ValueError('camera geometry changed')
        t = truth['frames'][frame]
        ids = np.load(verified(truth_path.parent / t['objects'], truth['sha256'][t['objects']]), allow_pickle=False)
        vis = np.load(verified(truth_path.parent / t['visibility'], truth['sha256'][t['visibility']]), allow_pickle=False)
        direct = np.asarray(Image.open(verified(truth_path.parent / t['rgb'], truth['sha256'][t['rgb']])).convert('RGB'))
        target, road = ids == target_id, ids == road_id
        visible = target & (vis != 0)
        rgb = products['output.rgba'][..., :3]
        proxy = chroma_mask(rgb, plan['chroma_threshold'])
        metrics = mask_scores(proxy, target)
        statuses = np.stack([products[f'camera{i}-status.u8'][..., 0] for i in range(4)], axis=-1)
        no_source = ~np.any(statuses == 1, axis=-1)
        metrics.update(visible_target_pixels=int(visible.sum()), visible_target_proxy_hits=int((visible & proxy).sum()),
            visible_target_pixels_without_source=int((visible & no_source).sum()),
            hidden_target_proxy_hits=int((target & ~visible & proxy).sum()),
            road_linear_rgb_mae=float(np.abs(linearize(rgb[road] / 255) - linearize(direct[road] / 255)).mean()),
            uncertain_rejected_pairs=int((statuses == 8).sum()))
        box = meta['config']['surface']['source_occlusion_box']
        comparisons = []
        nominal_retained = accepted_ambiguous = 0
        for i, camera in enumerate(meta['config']['cameras']):
            old = base[f'camera{i}-status.u8'][..., 0]
            valid = old == 1
            points = base['carrier-positions.f32'][valid]
            expected, ambiguous = expected_status(camera, points, base[f'camera{i}-pixels.f32'][valid], box)
            oracle = old.copy()
            oracle[valid] = expected
            mismatches = np.argwhere(oracle != statuses[..., i])
            comparisons.append(dict(camera=i, checked_pairs=int(valid.sum()), oracle_mismatches=len(mismatches),
                                    first_mismatch_yx=mismatches[:16].tolist()))
            retained = statuses[..., i] == 1
            if not np.array_equal(products[f'camera{i}-pixels.f32'][retained], base[f'camera{i}-pixels.f32'][retained]):
                raise ValueError('retained source UV changed')
            origin = np.linalg.inv(np.asarray(camera['T_camera_from_vehicle']))[:3, 3]
            reference = sensitivity['nominal_boxes_by_frame'][str(frame)]
            blocked = blocked_by_faces(origin, points, reference['min_m'], reference['max_m'], sensitivity['nominal_reference_bias_m'])
            nominal_retained += int((blocked & retained[valid]).sum())
            accepted_ambiguous += int((ambiguous & retained[valid]).sum())
        historical_match = sha(directory / 'output.rgba') == condition['historical_output_sha256']
        if condition['policy'] == 'nominal' and not historical_match:
            raise ValueError('unchanged nominal RGBA differs from historical capture')
        control = next(r for r in primary['rows'] if r['frame'] == frame and r['variant'] == 'c_nom_b020')
        metrics['nominal_blocked_retained_camera_pairs'] = nominal_retained
        metrics['accepted_ambiguous_pairs'] = accepted_ambiguous
        gates = dict(all_visible_hits=metrics['visible_target_proxy_hits'] == int(visible.sum()),
            no_visible_without_source=metrics['visible_target_pixels_without_source'] == 0,
            outside_below_nominal_center=metrics['outside_expected_pixels'] < control['outside_expected_pixels'],
            road_not_worse_than_nominal_center=metrics['road_linear_rgb_mae'] <= control['road_linear_rgb_mae'],
            no_reference_blocking_increase=nominal_retained <= control['nominal_blocked_retained_camera_pairs'])
        rows.append(dict(**condition, directory=str(directory.relative_to(root)), device=meta['device'],
            report_sha256=sha(directory / 'report.json'), output_sha256=sha(directory / 'output.rgba'),
            historical_rgba_match=historical_match, geometry_unchanged=True, retained_uv_unchanged=True,
            comparisons=comparisons, gates=gates, all_gates=all(gates.values()), **metrics))
    return dict(schema_version=1, scope=plan['scope'], phase=plan['phase'], plan_sha256=sha(path),
        collector_sha256=sha(__file__), binary_sha256=plan['binary_sha256'],
        invocations_sha256=sha(inv_path), rows=rows, limitations=plan['limitations'])


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, default=root / 'configs/research/native-uncertainty-nvidia-plan.json')
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--binary', type=Path, default=root / 'build/sv-correspondence-probe')
    parser.add_argument('--output-report', type=Path)
    args = parser.parse_args()
    if args.capture:
        capture(root, args.plan, args.binary)
    if args.output_report:
        report = collect(root, args.plan)
        args.output_report.parent.mkdir(parents=True, exist_ok=True)
        args.output_report.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
