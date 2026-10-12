"""Independent finite-face oracle for native radial source-footprint visibility controls."""
import json
import numpy as np
from source_correspondence import sha, verified, load_product
from source_visibility import blocked_by_faces
from local_support import collect as collect_geometry


def policy_blocked(camera, points, pixels, box):
    points, pixels = np.asarray(points, float), np.asarray(pixels, float)
    inverse = np.linalg.inv(np.asarray(camera['T_camera_from_vehicle']))
    origin = inverse[:3, 3]
    result = blocked_by_faces(origin, points, box['min_m'], box['max_m'], box['tolerance_m'])
    if box.get('mode', 'center') == 'center':
        return result
    if box['mode'] != 'bilinear_radial_v1':
        raise ValueError('unsupported policy')
    optics = camera['projection']
    if any(optics['k']) or optics.get('alpha', 0):
        raise ValueError('zero-distortion optics required')
    guard = box.get('guard_texels', 0)
    if guard not in (0, 1):
        raise ValueError('unsupported guard')
    base = np.floor(pixels);fraction = pixels - base
    ranges = np.linalg.norm(points - origin, axis=-1)
    for y in range(-guard, 2+guard):
        for x in range(-guard, 2+guard):
            positive = np.ones(len(points), bool) if guard else ((1-fraction[:,0] if x == 0 else fraction[:,0]) *
                         (1-fraction[:,1] if y == 0 else fraction[:,1]) > 0)
            uv = np.clip(base + [x,y], 0, [camera['resolution']['width']-1, camera['resolution']['height']-1])
            ab = (uv - [optics['cx'],optics['cy']]) / [optics['fx'],optics['fy']]
            theta = np.linalg.norm(ab, axis=-1)
            scale = np.divide(np.sin(theta), theta, out=np.ones_like(theta), where=theta > 1e-14)
            ray = np.column_stack([ab*scale[:,None], np.cos(theta)]) @ inverse[:3,:3].T
            endpoints = origin + ray*ranges[:,None]
            result |= positive & blocked_by_faces(origin, endpoints, np.asarray(box['min_m'])+1e-5, np.asarray(box['max_m'])-1e-5, box['tolerance_m'])
    return result


def products(directory):
    meta = json.loads((directory/'report.json').read_text())
    return {v['file']: load_product(directory,v) for v in meta['products']}


def collect(root, plan_path):
    report = collect_geometry(root,plan_path)
    plan = json.loads(plan_path.read_text())
    truth_path = root/plan['truth_path'];truth = json.loads(truth_path.read_text())
    target_id = next(v for k,v in truth['objects'].items() if 'coded object target' in k)
    road_id = next(v for k,v in truth['objects'].items() if k.startswith('SV road.'))
    comparisons = []
    for row in report['rows']:
        frame,variant = row['frame'],row['variant'];d=root/row['directory']
        p=products(d)
        if variant in plan['historical_output_sha256']:
            verified(d/'output.rgba',plan['historical_output_sha256'][variant][str(frame)])
        statuses=np.stack([p[f'camera{i}-status.u8'][...,0] for i in range(4)],axis=-1)
        t=truth['frames'][frame]
        ids=np.load(verified(truth_path.parent/t['objects'],truth['sha256'][t['objects']]),allow_pickle=False)
        covered=(p['carrier-regions.u8'][...,0]!=0)&(p['carrier-regions.u8'][...,0]!=5)
        no_source=covered&~np.any(statuses==1,axis=-1)
        row.update(rejected_camera_point_pairs=int((statuses==7).sum()),
                   target_pixels_without_source=int((no_source&(ids==target_id)).sum()),
                   road_pixels_without_source=int((no_source&(ids==road_id)).sum()))
        if variant in ['plane','local_100']:continue
        baseline=products(root/plan['output_root']/f"frame{frame}-{'plane' if variant.startswith('plane') else 'local_100'}")
        for key in ['carrier-positions.f32','carrier-regions.u8']:
            if not np.array_equal(p[key],baseline[key],equal_nan=True):raise ValueError('carrier changed')
        meta=json.loads((d/'report.json').read_text());counts=[]
        box=row['surface']['source_occlusion_box']
        for i,camera in enumerate(meta['config']['cameras']):
            old=baseline[f'camera{i}-status.u8'][...,0];valid=old==1
            blocked=policy_blocked(camera,baseline['carrier-positions.f32'][valid],baseline[f'camera{i}-pixels.f32'][valid],box)
            expected=old.copy();expected[valid]=np.where(blocked,7,1)
            mismatches=np.argwhere(expected!=statuses[...,i])
            if len(mismatches):raise ValueError(f"oracle mismatch {frame}/{variant}/cam{i}: {mismatches[:8].tolist()}")
            retained=statuses[...,i]==1
            if not np.array_equal(p[f'camera{i}-pixels.f32'][retained],baseline[f'camera{i}-pixels.f32'][retained]):
                raise ValueError('retained UV changed')
            counts.append(dict(camera=i,rejected_pairs=int(blocked.sum()),checked_pairs=int(valid.sum()),oracle_mismatches=0))
        comparisons.append(dict(frame=frame,variant=variant,geometry_unchanged=True,retained_uv_unchanged=True,cameras=counts))
    report.update(collector_sha256=sha(__file__),geometry_collector_sha256=sha(root/'tools/research/local_support.py'),policy_comparisons=comparisons)
    return report
