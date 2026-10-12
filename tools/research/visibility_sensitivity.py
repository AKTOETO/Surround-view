"""Frozen paired support-band, endpoint-bias and supplied-box translation audit."""
import json
import numpy as np
from source_correspondence import sha, verified
from source_visibility import blocked_by_faces
from local_support import collect as collect_geometry
from filter_visibility import products, policy_blocked as radial_blocked
from box_surface_visibility import on_box_surface


def policy_blocked(camera, points, pixels, box):
    if box.get('mode') != 'bilinear_box_surface_v1':
        return radial_blocked(camera, points, pixels, box)
    radial = radial_blocked(camera, points, pixels, dict(box, mode='bilinear_radial_v1'))
    origin = np.linalg.inv(np.asarray(camera['T_camera_from_vehicle']))[:3, 3]
    central = blocked_by_faces(origin, points, box['min_m'], box['max_m'], box['tolerance_m'])
    return central | (radial & ~on_box_surface(points, box, box.get('support_tolerance_m', 1e-5)))


def collect(root, plan_path):
    report = collect_geometry(root, plan_path)
    plan = json.loads(plan_path.read_text())
    truth_path = root / plan['truth_path'];truth = json.loads(truth_path.read_text())
    conditions = {c['variant']: c for c in plan['conditions']}
    target_id = next(v for k, v in truth['objects'].items() if 'coded object target' in k)
    road_id = next(v for k, v in truth['objects'].items() if k.startswith('SV road.'))
    comparisons = []
    for row in report['rows']:
        frame, variant = row['frame'], row['variant'];d = root / row['directory']
        p = products(d)
        if variant in plan['historical_output_sha256']:
            verified(d / 'output.rgba', plan['historical_output_sha256'][variant][str(frame)])
        if variant == 'plane':
            continue
        row['condition'] = conditions[variant]
        statuses = np.stack([p[f'camera{i}-status.u8'][..., 0] for i in range(4)], axis=-1)
        t = truth['frames'][frame]
        ids = np.load(verified(truth_path.parent / t['objects'], truth['sha256'][t['objects']]), allow_pickle=False)
        visibility = np.load(verified(truth_path.parent / t['visibility'], truth['sha256'][t['visibility']]), allow_pickle=False)
        target, road = ids == target_id, ids == road_id
        covered = (p['carrier-regions.u8'][..., 0] != 0) & (p['carrier-regions.u8'][..., 0] != 5)
        no_source = covered & ~np.any(statuses == 1, axis=-1)
        row.update(target_pixels_without_source=int((no_source & target).sum()),
                   visible_target_pixels_without_source=int((no_source & target & (visibility != 0)).sum()),
                   road_pixels_without_source=int((no_source & road).sum()))
        baseline = products(root / plan['output_root'] / f'frame{frame}-local_100')
        for key in ['carrier-positions.f32', 'carrier-regions.u8']:
            if not np.array_equal(p[key], baseline[key], equal_nan=True):
                raise ValueError('box-only perturbation changed carrier geometry')
        meta = json.loads((d / 'report.json').read_text());counts = []
        box = row['surface'].get('source_occlusion_box')
        nominal = plan['nominal_boxes_by_frame'][str(frame)]
        nominal_blocked_retained = unsupported_target_pairs = supported_road_pairs = 0
        for i, camera in enumerate(meta['config']['cameras']):
            old = baseline[f'camera{i}-status.u8'][..., 0];valid = old == 1
            positions = baseline['carrier-positions.f32'][valid]
            blocked = policy_blocked(camera, positions, baseline[f'camera{i}-pixels.f32'][valid], box) if box else np.zeros(len(positions), bool)
            expected = old.copy();expected[valid] = np.where(blocked, 7, 1)
            mismatches = np.argwhere(expected != statuses[..., i])
            # Endpoint bias0 is an intentional numerical stress condition. Keep ALL mismatch
            # counts; never omit those cases or silently widen comparison tolerances.
            counts.append(dict(camera=i, checked_pairs=int(valid.sum()), oracle_mismatches=len(mismatches),
                               first_mismatch_yx=mismatches[:16].tolist()))
            retained = statuses[..., i] == 1
            if not np.array_equal(p[f'camera{i}-pixels.f32'][retained], baseline[f'camera{i}-pixels.f32'][retained]):
                raise ValueError('retained UV changed')
            origin = np.linalg.inv(np.asarray(camera['T_camera_from_vehicle']))[:3, 3]
            nominal_blocked = blocked_by_faces(origin, positions, nominal['min_m'], nominal['max_m'], plan['nominal_reference_bias_m'])
            nominal_blocked_retained += int((nominal_blocked & retained[valid]).sum())
            unsupported_target_pairs += int((retained & target & ((visibility & (1 << i)) == 0)).sum())
            if box:
                support = on_box_surface(positions, box, box.get('support_tolerance_m', 1e-5))
                supported_road_pairs += int((support & retained[valid] & road[valid]).sum())
        row.update(nominal_blocked_retained_camera_pairs=nominal_blocked_retained,
                   accepted_target_camera_pairs_without_truth_visibility=unsupported_target_pairs,
                   retained_supported_road_camera_pairs=supported_road_pairs)
        comparisons.append(dict(frame=frame, variant=variant, geometry_unchanged=True, retained_uv_unchanged=True, cameras=counts))
    gates = []
    for row in report['rows']:
        if row.get('condition', {}).get('kind') != 'surface':
            continue
        frame = row['frame']
        center = next(r for r in report['rows'] if r['frame'] == frame and r['variant'] == row['condition']['center_variant'])
        nominal_center = next(r for r in report['rows'] if r['frame'] == frame and r['variant'] == plan['nominal_center_variant'])
        gates.append(dict(frame=frame, variant=row['variant'],
                          matched_center_visible_hits=center['visible_target_proxy_hits'],
                          all_visible_hits_preserved=row['visible_target_proxy_hits'] == row['visible_target_pixels'],
                          no_visible_unsupported=row['visible_target_pixels_without_source'] == 0,
                          outside_below_nominal_center=row['outside_expected_pixels'] < nominal_center['outside_expected_pixels'],
                          road_mae_not_above_nominal_center=row['road_linear_rgb_mae'] <= nominal_center['road_linear_rgb_mae'],
                          nominal_blocked_retained_not_increased=row['nominal_blocked_retained_camera_pairs'] <= nominal_center['nominal_blocked_retained_camera_pairs']))
    report.update(collector_sha256=sha(__file__), geometry_collector_sha256=sha(root / 'tools/research/local_support.py'),
                  policy_comparisons=comparisons, quality_gates=gates)
    return report
