"""Independent six-face audit of native supplied-box visibility (not RGB depth inference)."""
import json
import numpy as np
from source_correspondence import load_product, sha, verified
from local_support import collect as collect_geometry


def blocked_by_faces(origin, points, minimum, maximum, tolerance):
    """First surface intersection before the endpoint; independent of native slab code."""
    origin, points = np.asarray(origin, float), np.asarray(points, float)
    lo, hi = np.asarray(minimum, float), np.asarray(maximum, float)
    delta = points - origin
    length = np.linalg.norm(delta, axis=-1)
    limit = 1 - np.divide(tolerance, length, out=np.full_like(length, np.inf), where=length > 0)
    result = np.zeros(length.shape, bool)
    if np.all((origin >= lo) & (origin <= hi)):
        return length > tolerance
    for axis in range(3):
        for plane in [lo[axis], hi[axis]]:
            t = np.divide(plane - origin[axis], delta[..., axis],
                          out=np.full_like(length, np.inf), where=np.abs(delta[..., axis]) > 1e-12)
            inside = (t >= 0) & (t < limit)
            for other in range(3):
                if other == axis:
                    continue
                coordinate = origin[other] + np.where(np.isfinite(t), t, 0) * delta[..., other]
                inside &= (coordinate >= lo[other] - 1e-10) & (coordinate <= hi[other] + 1e-10)
            result |= inside
    return result


def products(directory):
    meta = json.loads((directory / 'report.json').read_text())
    return {p['file']: load_product(directory, p) for p in meta['products']}


def collect(root, plan_path):
    report = collect_geometry(root, plan_path)
    plan = json.loads(plan_path.read_text())
    truth_path = root / plan['truth_path']
    truth = json.loads(truth_path.read_text())
    target_id = next(v for k, v in truth['objects'].items() if 'coded object target' in k)
    road_id = next(v for k, v in truth['objects'].items() if k.startswith('SV road.'))
    comparisons = []
    for row in report['rows']:
        frame, variant = row['frame'], row['variant']
        directory = root / row['directory']
        p = products(directory)
        if variant == 'local_100':
            verified(directory / 'output.rgba', plan['local_output_sha256'][str(frame)])
        statuses = np.stack([p[f'camera{i}-status.u8'][..., 0] for i in range(4)], axis=-1)
        ids = np.load(verified(truth_path.parent / truth['frames'][frame]['objects'],
                              truth['sha256'][truth['frames'][frame]['objects']]), allow_pickle=False)
        covered = (p['carrier-regions.u8'][..., 0] != 0) & (p['carrier-regions.u8'][..., 0] != 5)
        no_source = covered & ~np.any(statuses == 1, axis=-1)
        row.update(rejected_camera_point_pairs=int((statuses == 7).sum()),
                   target_pixels_without_source=int((no_source & (ids == target_id)).sum()),
                   road_pixels_without_source=int((no_source & (ids == road_id)).sum()))
        if not variant.endswith('_occluded'):
            continue
        base_variant = variant.removesuffix('_occluded')
        base_row = next(r for r in report['rows'] if r['frame'] == frame and r['variant'] == base_variant)
        baseline = products(root / base_row['directory'])
        for key in ['carrier-positions.f32', 'carrier-regions.u8']:
            if not np.array_equal(p[key], baseline[key], equal_nan=True):
                raise ValueError('visibility changed carrier geometry')
        meta = json.loads((directory / 'report.json').read_text())
        box = row['surface']['source_occlusion_box']
        counts = []
        for camera, cfg in enumerate(meta['config']['cameras']):
            old = baseline[f'camera{camera}-status.u8'][..., 0]
            current = statuses[..., camera]
            valid = old == 1
            origin = np.linalg.inv(np.asarray(cfg['T_camera_from_vehicle']))[:3, 3]
            blocked = blocked_by_faces(origin, baseline['carrier-positions.f32'][valid],
                                       box['min_m'], box['max_m'], box['tolerance_m'])
            expected = old.copy()
            expected[valid] = np.where(blocked, 7, 1)
            mismatches = np.argwhere(current != expected)
            if len(mismatches):
                raise ValueError(f'face oracle mismatch frame{frame}/{variant}/camera{camera}: {mismatches[:8].tolist()}')
            retained = current == 1
            if not np.array_equal(p[f'camera{camera}-pixels.f32'][retained],
                                  baseline[f'camera{camera}-pixels.f32'][retained]):
                raise ValueError('retained UV changed')
            counts.append(dict(camera=camera, rejected_pairs=int(blocked.sum()),
                               retained_pairs=int(retained.sum()), oracle_mismatches=0))
        comparisons.append(dict(frame=frame, variant=variant, geometry_unchanged=True,
                                retained_uv_unchanged=True, cameras=counts))
    report.update(collector_sha256=sha(__file__), geometry_collector_sha256=sha(root / 'tools/research/local_support.py'),
                  visibility_comparisons=comparisons)
    return report
