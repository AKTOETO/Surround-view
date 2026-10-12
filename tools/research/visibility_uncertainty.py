"""Read-only sufficient certificates for bounded supplied-box uncertainty."""
import json
import numpy as np
from source_correspondence import sha, verified, load_product
from source_visibility import blocked_by_faces


def certificates(origin, points, minimum, maximum, bias, radius):
    """Return blocked/clear certificates; the remaining rays are ambiguous.

    Bounds enclose all nonempty AABBs whose face coordinates differ by <=radius.
    They also enclose bounded translations, but ambiguity is not a witness that
    a clear translated box exists. Endpoints and origin stay fixed.
    """
    origin, points, lo, hi = [np.asarray(v, dtype=float) for v in (origin, points, minimum, maximum)]
    if origin.shape != (3,) or points.ndim != 2 or points.shape[1] != 3 or lo.shape != (3,) or hi.shape != (3,):
        raise ValueError('expected origin/bounds (3,) and points (N,3)')
    if not all(np.isfinite(v).all() for v in (origin, points, lo, hi)) or np.any(lo >= hi):
        raise ValueError('nonfinite geometry or empty nominal box')
    if not np.isfinite(bias) or not np.isfinite(radius) or bias < 0 or radius < 0:
        raise ValueError('bias and radius must be finite and nonnegative')
    # A zero-width eroded intersection is retained as a closed, degenerate box;
    # a negative-width intersection is empty and provides no blocked certificate.
    blocked = (blocked_by_faces(origin, points, lo + radius, hi - radius, bias)
               if np.all(lo + radius <= hi - radius) else np.zeros(len(points), bool))
    clear = ~blocked_by_faces(origin, points, lo - radius, hi + radius, bias)
    if np.any(blocked & clear):
        raise ValueError('nested-box certificates contradict')
    return blocked, clear


def pixel_certificates(eligible, blocked, clear):
    eligible, blocked, clear = [np.asarray(v, bool) for v in (eligible, blocked, clear)]
    if eligible.ndim != 2 or blocked.shape != eligible.shape or clear.shape != eligible.shape:
        raise ValueError('expected matching (pixels,cameras) masks')
    if np.any(blocked & clear):
        raise ValueError('contradictory ray certificates')
    available = eligible.any(axis=1)
    guaranteed = (eligible & clear).any(axis=1)
    impossible = available & ~(eligible & ~blocked).any(axis=1)
    ambiguous = available & ~guaranteed & ~impossible
    return guaranteed, impossible, ambiguous


def collect(root, plan_path):
    plan = json.loads(plan_path.read_text())
    for name, digest in plan['frozen_files'].items():
        verified(root / name, digest)
    primary = json.loads((root / plan['primary_report']).read_text())
    sensitivity_path = verified(root / plan['sensitivity_plan'], primary['plan_sha256'])
    sensitivity = json.loads(sensitivity_path.read_text())
    # Historical source hashes describe the capture revision, not today's source.
    # Verify its pinned truth manifest and all consumed products instead.
    truth_path = verified(root / sensitivity['truth_path'], sensitivity['frozen_files'][sensitivity['truth_path']])
    truth = json.loads(truth_path.read_text())
    target_id = next(v for k, v in truth['objects'].items() if 'coded object target' in k)
    rows = []
    for original in primary['rows']:
        if original.get('condition', {}).get('kind') != 'center':
            continue
        frame = original['frame']
        directory = root / original['directory']
        meta = json.loads(verified(directory / 'report.json', original['report_sha256']).read_text())
        t = truth['frames'][frame]
        ids = np.load(verified(truth_path.parent / t['objects'], truth['sha256'][t['objects']]), allow_pickle=False)
        vis = np.load(verified(truth_path.parent / t['visibility'], truth['sha256'][t['visibility']]), allow_pickle=False)
        y, x = np.argwhere(ids == target_id).T
        visible = vis[y, x] != 0
        def selected(folder, metadata, name):
            descriptor = next(p for p in metadata['products'] if p['file'] == name)
            return load_product(folder, descriptor)[y, x]
        points = selected(directory, meta, 'carrier-positions.f32')
        # Verify the output even though this study never synthesizes RGB.
        selected(directory, meta, 'output.rgba')
        baseline = next(r for r in primary['rows'] if r['frame'] == frame and r['variant'] == 'local_100')
        base_dir = root / baseline['directory']
        base_meta = json.loads(verified(base_dir / 'report.json', baseline['report_sha256']).read_text())
        if meta['config']['cameras'] != base_meta['config']['cameras']:
            raise ValueError('camera geometry differs from matched baseline')
        np.testing.assert_array_equal(points, selected(base_dir, base_meta, 'carrier-positions.f32'))
        before = np.column_stack([selected(base_dir, base_meta, f'camera{i}-status.u8')[:, 0] == 1 for i in range(4)])
        after = np.column_stack([selected(directory, meta, f'camera{i}-status.u8')[:, 0] == 1 for i in range(4)])
        origins = [np.linalg.inv(np.asarray(c['T_camera_from_vehicle']))[:3, 3] for c in meta['config']['cameras']]
        box = original['surface']['source_occlusion_box']
        def blocked_at(bounds, bias):
            return np.column_stack([blocked_by_faces(o, points, bounds['min_m'], bounds['max_m'], bias) for o in origins])
        central = blocked_at(box, box['tolerance_m'])
        if not np.array_equal(after, before & ~central):
            raise ValueError('native center status disagrees with independent face oracle')
        nominal_available = after.any(axis=1)
        if int((visible & ~nominal_available).sum()) != original['visible_target_pixels'] - original['visible_target_proxy_hits']:
            raise ValueError('center eligibility loss differs from primary RGB proxy loss')
        reference = blocked_at(sensitivity['nominal_boxes_by_frame'][str(frame)], sensitivity['nominal_reference_bias_m'])
        for radius in plan['uncertainty_radii_m']:
            bounds = [certificates(o, points, box['min_m'], box['max_m'], box['tolerance_m'], radius) for o in origins]
            blocked, clear = [np.column_stack([b[i] for b in bounds]) for i in (0, 1)]
            if np.any(blocked & ~central) or np.any(clear & central):
                raise ValueError('nominal occlusion outside uncertainty bounds')
            guaranteed, impossible, ambiguous = pixel_certificates(before, blocked, clear)
            if not np.array_equal(guaranteed | impossible | ambiguous, before.any(axis=1)):
                raise ValueError('pixel certificates do not partition baseline availability')
            optimistic = (before & ~blocked).any(axis=1)
            newly_admitted = before & central & ~blocked
            rows.append(dict(frame=frame, variant=original['variant'], condition=original['condition'], radius_m=radius,
                             native_report_sha256=original['report_sha256'], output_sha256=original['output_sha256'],
                             target_pixels=len(y), visible_target_pixels=int(visible.sum()),
                             baseline_unavailable_visible=int((visible & ~before.any(axis=1)).sum()),
                             nominal_visible_eligible=int((visible & nominal_available).sum()),
                             guaranteed_visible_eligible=int((visible & guaranteed).sum()),
                             definitely_blocked_visible=int((visible & impossible).sum()),
                             ambiguous_only_visible=int((visible & ambiguous).sum()),
                             optimistic_visible_eligible_bound=int((visible & optimistic).sum()),
                             central_visible_loss_potentially_recoverable=int((visible & ~nominal_available & optimistic).sum()),
                             optimistic_hidden_eligible=int((~visible & optimistic).sum()),
                             nominal_hidden_eligible=int((~visible & nominal_available).sum()),
                             eligible_pairs=int(before.sum()), definitely_blocked_pairs=int((before & blocked).sum()),
                             definitely_clear_pairs=int((before & clear).sum()),
                             ambiguous_pairs=int((before & ~blocked & ~clear).sum()),
                             newly_admitted_pairs=int(newly_admitted.sum()),
                             newly_admitted_nominal_reference_blocked_pairs=int((newly_admitted & reference).sum())))
    if len(rows) != 18 * len(plan['uncertainty_radii_m']):
        raise ValueError('incomplete frozen center selection')
    return dict(schema_version=1, scope=plan['scope'], phase=plan['phase'], plan_sha256=sha(plan_path),
                collector_sha256=sha(__file__), primary_report_sha256=sha(root / plan['primary_report']),
                rows=rows, limitations=plan['limitations'])
