"""Post-hoc exhaustive visible-loss partition on frozen native sensitivity outputs."""
import json
import numpy as np
from source_correspondence import sha, verified, load_product
from source_visibility import blocked_by_faces
from displaced_correspondence import chroma_mask

LABELS = ['baseline_unavailable', 'all_central_blocked', 'remaining_sources_footprint_rejected', 'source_present_proxy_missing']


def loss_partition(missing, before, central_blocked, after):
    """Disjoint output-pixel categories. Source validity does not imply positive fusion weight."""
    missing = np.asarray(missing, bool)
    before, central_blocked, after = map(lambda a: np.asarray(a, bool), (before, central_blocked, after))
    if before.shape != central_blocked.shape or before.shape != after.shape or before.shape[0] != len(missing):
        raise ValueError('loss partition dimensions differ')
    if np.any(after & ~before):
        raise ValueError('filter created a new geometrically eligible source')
    available = before.any(axis=1)
    central_available = (before & ~central_blocked).any(axis=1)
    retained = after.any(axis=1)
    labels = np.full(len(missing), -1, dtype=int)
    labels[missing & ~available] = 0
    labels[missing & available & ~retained & ~central_available] = 1
    labels[missing & available & ~retained & central_available] = 2
    labels[missing & retained] = 3
    if np.any(labels[missing] < 0):
        raise ValueError('unclassified visible loss')
    return labels


def collect(root, plan_path):
    plan = json.loads(plan_path.read_text())
    for name, digest in plan['frozen_files'].items():
        verified(root / name, digest)
    primary_path = verified(root / plan['primary_report'], plan['primary_report_sha256'])
    primary = json.loads(primary_path.read_text())
    sensitivity_path = verified(root / plan['sensitivity_plan'], primary['plan_sha256'])
    sensitivity = json.loads(sensitivity_path.read_text())
    truth_path = root / sensitivity['truth_path'];truth = json.loads(truth_path.read_text())
    target_id = next(v for k, v in truth['objects'].items() if 'coded object target' in k)
    rows = []
    for original in primary['rows']:
        frame, variant = original['frame'], original['variant']
        if variant == 'plane':
            continue
        directory = root / original['directory']
        meta = json.loads(verified(directory / 'report.json', original['report_sha256']).read_text())
        t = truth['frames'][frame]
        ids = np.load(verified(truth_path.parent / t['objects'], truth['sha256'][t['objects']]), allow_pickle=False)
        vis = np.load(verified(truth_path.parent / t['visibility'], truth['sha256'][t['visibility']]), allow_pickle=False)
        y, x = np.argwhere(ids == target_id).T
        def selected(name):
            product = next(p for p in meta['products'] if p['file'] == name)
            return load_product(directory, product)[y, x]
        rgb = selected('output.rgba')[:, :3]
        source_visible = vis[y, x] != 0
        proxy = chroma_mask(rgb, sensitivity['chroma_threshold'])
        missing = source_visible & ~proxy
        baseline_dir = root / sensitivity['output_root'] / f'frame{frame}-local_100'
        baseline_row = next(r for r in primary['rows'] if r['frame'] == frame and r['variant'] == 'local_100')
        baseline_meta = json.loads(verified(baseline_dir / 'report.json', baseline_row['report_sha256']).read_text())
        before, after, central = [], [], []
        points = selected('carrier-positions.f32')
        box = original['surface'].get('source_occlusion_box')
        for i, camera in enumerate(meta['config']['cameras']):
            product = next(p for p in baseline_meta['products'] if p['file'] == f'camera{i}-status.u8')
            before.append(load_product(baseline_dir, product)[y, x, 0] == 1)
            after.append(selected(f'camera{i}-status.u8')[:, 0] == 1)
            origin = np.linalg.inv(np.asarray(camera['T_camera_from_vehicle']))[:3, 3]
            central.append(blocked_by_faces(origin, points, box['min_m'], box['max_m'], box['tolerance_m']) if box else np.zeros(len(points), bool))
        labels = loss_partition(missing, np.array(before).T, np.array(central).T, np.array(after).T)
        partition = {label: int((labels == i).sum()) for i, label in enumerate(LABELS)}
        if sum(partition.values()) != original['visible_target_pixels'] - original['visible_target_proxy_hits']:
            raise ValueError('primary visible-loss count differs')
        if original.get('condition', {}).get('kind') == 'center' and partition['remaining_sources_footprint_rejected']:
            raise ValueError('center-only output incorrectly classified as footprint rejection')
        present = np.array(after).any(axis=0)
        rows.append(dict(frame=frame, variant=variant, condition=original['condition'],
                         native_report_sha256=original['report_sha256'], output_sha256=original['output_sha256'],
                         target_pixels=len(y), visible_target_pixels=int(source_visible.sum()),
                         visible_proxy_hits=int((source_visible & proxy).sum()), visible_loss_partition=partition,
                         hidden_target_pixels=int((~source_visible).sum()), hidden_proxy_hits=int((~source_visible & proxy).sum()),
                         hidden_target_source_present=int((~source_visible & present).sum()),
                         hidden_target_source_absent=int((~source_visible & ~present).sum()),
                         missing_visible_pixels=[dict(y=int(yy), x=int(xx), category=LABELS[int(label)]) for yy,xx,label in zip(y,x,labels) if label >= 0]))
    return dict(schema_version=1,scope=plan['scope'],phase='post_hoc_readonly_exploratory',plan_sha256=sha(plan_path),
                primary_report_sha256=sha(primary_path),collector_sha256=sha(__file__),rows=rows,limitations=plan['limitations'])
