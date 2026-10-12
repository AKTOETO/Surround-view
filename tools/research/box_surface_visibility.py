"""Independent finite-face support oracle for supplied-box footprint preservation."""
import json
import numpy as np
from source_correspondence import sha, verified
from source_visibility import blocked_by_faces
from local_support import collect as collect_geometry
from filter_visibility import products, policy_blocked as radial_blocked


def on_box_surface(points, box, epsilon=1e-5):
    """Minimum L-infinity distance to any of six finite rectangular faces."""
    points = np.asarray(points, float)
    lo, hi = np.asarray(box['min_m']), np.asarray(box['max_m'])
    distances = []
    for axis in range(3):
        for side in (lo[axis], hi[axis]):
            nearest = np.clip(points, lo, hi)
            nearest[..., axis] = side
            distances.append(np.max(np.abs(points - nearest), axis=-1))
    return np.min(distances, axis=0) <= epsilon


def policy_blocked(camera, points, pixels, box):
    if box.get('mode') != 'bilinear_box_surface_v1':
        return radial_blocked(camera, points, pixels, box)
    radial = radial_blocked(camera, points, pixels, dict(box, mode='bilinear_radial_v1'))
    origin = np.linalg.inv(np.asarray(camera['T_camera_from_vehicle']))[:3, 3]
    central = blocked_by_faces(origin, points, box['min_m'], box['max_m'], box['tolerance_m'])
    return central | (radial & ~on_box_surface(points, box))


def collect(root, plan_path):
    report = collect_geometry(root, plan_path)
    plan = json.loads(plan_path.read_text())
    truth_path = root / plan['truth_path'];truth = json.loads(truth_path.read_text())
    target_id = next(v for k, v in truth['objects'].items() if 'coded object target' in k)
    road_id = next(v for k, v in truth['objects'].items() if k.startswith('SV road.'))
    comparisons = []
    for row in report['rows']:
        frame, variant = row['frame'], row['variant'];d = root / row['directory']
        p = products(d)
        if variant in plan['historical_output_sha256']:
            verified(d / 'output.rgba', plan['historical_output_sha256'][variant][str(frame)])
        statuses = np.stack([p[f'camera{i}-status.u8'][..., 0] for i in range(4)], axis=-1)
        t = truth['frames'][frame]
        ids = np.load(verified(truth_path.parent / t['objects'], truth['sha256'][t['objects']]), allow_pickle=False)
        covered = (p['carrier-regions.u8'][..., 0] != 0) & (p['carrier-regions.u8'][..., 0] != 5)
        no_source = covered & ~np.any(statuses == 1, axis=-1)
        visible = np.load(verified(truth_path.parent / t['visibility'], truth['sha256'][t['visibility']]), allow_pickle=False) != 0
        row.update(rejected_camera_point_pairs=int((statuses == 7).sum()),
                   target_pixels_without_source=int((no_source & (ids == target_id)).sum()),
                   visible_target_pixels_without_source=int((no_source & (ids == target_id) & visible).sum()),
                   road_pixels_without_source=int((no_source & (ids == road_id)).sum()))
        if variant in ['plane', 'local_100']:
            continue
        baseline = products(root / plan['output_root'] / f"frame{frame}-{'plane' if variant.startswith('plane') else 'local_100'}")
        for key in ['carrier-positions.f32', 'carrier-regions.u8']:
            if not np.array_equal(p[key], baseline[key], equal_nan=True):
                raise ValueError('carrier changed')
        meta = json.loads((d / 'report.json').read_text());counts = []
        box = row['surface']['source_occlusion_box']
        for i, camera in enumerate(meta['config']['cameras']):
            old = baseline[f'camera{i}-status.u8'][..., 0];valid = old == 1
            positions = baseline['carrier-positions.f32'][valid]
            blocked = policy_blocked(camera, positions, baseline[f'camera{i}-pixels.f32'][valid], box)
            expected = old.copy();expected[valid] = np.where(blocked, 7, 1)
            mismatches = np.argwhere(expected != statuses[..., i])
            if len(mismatches):
                raise ValueError(f"oracle mismatch {frame}/{variant}/cam{i}: {mismatches[:8].tolist()}")
            retained = statuses[..., i] == 1
            if not np.array_equal(p[f'camera{i}-pixels.f32'][retained], baseline[f'camera{i}-pixels.f32'][retained]):
                raise ValueError('retained UV changed')
            counts.append(dict(camera=i, rejected_pairs=int(blocked.sum()), checked_pairs=int(valid.sum()),
                               retained_box_surface_pairs=int((~blocked & on_box_surface(positions, box)).sum()),
                               oracle_mismatches=0))
        comparisons.append(dict(frame=frame, variant=variant, geometry_unchanged=True,
                                retained_uv_unchanged=True, cameras=counts))
    gates = []
    for frame in plan['frames']:
        center = next(r for r in report['rows'] if r['frame'] == frame and r['variant'] == 'local_center')
        for variant in ['local_sf0', 'local_sf1']:
            row = next(r for r in report['rows'] if r['frame'] == frame and r['variant'] == variant)
            gates.append(dict(frame=frame, variant=variant,
                              visible_hits_preserved=row['visible_target_proxy_hits'] == center['visible_target_proxy_hits'],
                              visible_unsupported_not_increased=row['visible_target_pixels_without_source'] <= center['visible_target_pixels_without_source'],
                              outside_reduced=row['outside_expected_pixels'] < center['outside_expected_pixels'],
                              road_mae_not_increased=row['road_linear_rgb_mae'] <= center['road_linear_rgb_mae']))
    report['quality_gates'] = gates
    report.update(collector_sha256=sha(__file__), geometry_collector_sha256=sha(root / 'tools/research/local_support.py'),
                  policy_comparisons=comparisons)
    return report
