"""Offline independent oracle for compiled textured-scene renderer captures."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import numpy as np
from PIL import Image
from scipy.ndimage import binary_erosion
from source_correspondence import sha, verified, load_product
from residual_surface import inverse_ray, trace_triangles
from mesa_source_truth import virtual_segment, matching
from source_disagreement import bilinear_rgb
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'blender'))
from convert import convert


def calibration_delta(actual, physical):
    maximum = 0.
    for a, b in zip(actual, physical, strict=True):
        a, b = dict(a), dict(b)
        ta, tb = np.asarray(a.pop('T_camera_from_vehicle')), np.asarray(b.pop('T_camera_from_vehicle'))
        maximum = max(maximum, float(np.max(np.abs(ta-tb))))
        if a != b or not np.isfinite(ta).all() or not np.isfinite(tb).all() or maximum > 1e-12:
            raise ValueError('physical calibration differs')
    return maximum


def checked(root, plan_path):
    plan = json.loads(plan_path.read_text())
    for name, digest in plan['frozen_files'].items():
        verified(root/name, digest)
    directory = root/plan['output_root']
    study = json.loads((directory/'capture-study.json').read_text())
    if study['plan_sha256'] != sha(plan_path):
        raise ValueError('capture plan differs')
    for case in study['cases']:
        for name, digest in case['sha256'].items():
            verified(directory/case['id']/name, digest)
    return plan, study


def capture(root, plan_path):
    plan, _ = checked(root, plan_path)
    output = root/plan['native_output_root']; output.mkdir(exist_ok=False)
    calls = []
    env = dict(os.environ, **plan['backend_environment'])
    for case in plan['cases']:
        source = root/plan['output_root']/case['id']/'capture'
        inputs = output/(case['id']+'-inputs'); convert(source, inputs)
        base = json.loads((inputs/'config.json').read_text())
        for profile in plan['profiles']:
            cfg = copy.deepcopy(base)
            cfg['surface'] = dict(type='rectangular_bowl_v1', flat_half_length_m=2.6,
                flat_half_width_m=1.2, outer_half_length_m=14, outer_half_width_m=14,
                corner_height_m=0, uniform_cells=[32, 45])
            if profile == 'supplied_local_patch':
                x, y, z = case['target']['center_m']; a, b, c = case['target']['size_m']
                yaw = case['target_yaw_rad']
                cfg['surface']['local_support_patch'] = dict(center_x_m=x, center_y_m=y,
                    half_length_m=(abs(np.cos(yaw))*a+abs(np.sin(yaw))*b)/2,
                    half_width_m=(abs(np.sin(yaw))*a+abs(np.cos(yaw))*b)/2,
                    height_m=z+c/2, transition_m=.2)
            path = output/f"{case['id']}-{profile}.json"
            path.write_text(json.dumps(cfg, indent=2)+'\n')
            directory = output/f"{case['id']}-{profile}"
            correspondence = root/'artifacts/research-binaries/source-truth-mesa-v1/sv-correspondence-probe'
            args = [str(correspondence), str(path), str(inputs/'manifest.json'), str(directory), '0']
            result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=90)
            if result.returncode:
                raise RuntimeError(result.stderr)
            mask = directory/'disagreement.rgba'
            args = [str(root/'build/sv-support-wire-probe'), str(path), str(inputs/'manifest.json'), str(mask), 'source_disagreement_v1', '0']
            result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=90)
            if result.returncode:
                raise RuntimeError(result.stderr)
            calls.append(dict(id=case['id'], profile=profile, directory=str(directory.relative_to(root)),
                config=str(path.relative_to(root)), manifest=str((inputs/'manifest.json').relative_to(root)),
                correspondence_report_sha256=sha(directory/'report.json'), metric=json.loads(result.stdout)))
            (output/'invocations.json').write_text(json.dumps(calls, indent=2)+'\n')


def collect(root, plan_path):
    plan, study = checked(root, plan_path)
    calls_path = root/plan['native_output_root']/'invocations.json'
    calls = json.loads(calls_path.read_text())
    if [(c['id'], c['profile']) for c in calls] != [(c['id'], p) for c in plan['cases'] for p in plan['profiles']]:
        raise ValueError('capture selection differs')
    rows, anchors, quality, captures = [], [], [], []
    for call in calls:
        directory = root/call['directory']; source = root/plan['output_root']/call['id']
        case = next(c for c in study['cases'] if c['id'] == call['id'])
        m = json.loads(verified(directory/'report.json', call['correspondence_report_sha256']).read_text())
        metric = call['metric']; cfg = m['config']; h, w = cfg['output']['height'], cfg['output']['width']
        archive = json.loads((source/'capture/paired_truth.json').read_text())
        camera_delta = calibration_delta(cfg['cameras'], archive['config']['cameras'])
        if (not m['normal_output_unchanged'] or 'llvmpipe' not in m['device'] or m['device'] != metric['device']
                or m['source_fingerprint'] != metric['source_fingerprint'] or m['inputs'] != metric['inputs']
                or m['config_sha256'] != sha(root/call['config']) or m['config_sha256'] != metric['config_sha256']
                or m['manifest_sha256'] != sha(root/call['manifest']) or m['manifest_sha256'] != metric['manifest_sha256']
                or m['frame_index'] != 0 or metric['frame_index'] != 0 or metric['output_product'] != 'source_disagreement_v1'
                or cfg['virtual_camera'] != archive['config']['virtual_camera']
                or cfg['output'] != archive['config']['output']):
            raise ValueError('native identity differs')
        products = {v['file']: load_product(directory, v) for v in m['products']}
        mask = np.fromfile(verified(directory/'disagreement.rgba', metric['payload_sha256']), np.uint8).reshape(h,w,4)
        triangles = np.load(source/'triangles.npy', allow_pickle=False)
        kinds = np.load(source/'kinds.npy', allow_pickle=False)
        for anchor in case['anchors']:
            hit = trace_triangles(anchor['origin'], anchor['direction'], triangles, kinds, anchor['max_distance_m'])
            error, _ = matching(hit, anchor, [plan['anchor_tolerance_m']])
            passed = hit['kind'] == anchor['kind'] and ((hit['point'] is None and anchor['point'] is None)
                or (error is not None and error <= plan['anchor_tolerance_m']))
            anchors.append(dict(id=call['id'], profile=call['profile'], y=anchor['y'], x=anchor['x'], passed=passed, error_m=error))
        ids = np.load(source/'capture/objects_0000.npy', allow_pickle=False)
        visibility = np.load(source/'capture/visibility_0000.npy', allow_pickle=False)
        semantic = np.zeros_like(ids, dtype=np.uint8)
        for name, index in archive['objects'].items():
            semantic[ids == index] = 1 if 'coded object target' in name else 2 if name.split('.')[0] == 'SV road' else 3
        images = {i['camera_id']: np.asarray(Image.open(verified(Path(i['path']), i['sha256'])).convert('RGB')) for i in m['inputs']}
        pose = np.asarray(archive['frames'][0]['T_world_from_vehicle'])
        for y in range(plan['grid_offset'], h, plan['grid_step']):
            for x in range(plan['grid_offset'], w, plan['grid_step']):
                region = int(products['carrier-regions.u8'][y,x,0]); domain = region not in [0,3,5]
                cameras = [i for i in range(4) if products[f'camera{i}-status.u8'][y,x,0] == 1]
                sources = []; virtual = None
                if domain:
                    origin, ray, distance = virtual_segment(cfg, [y,x], pose)
                    virtual = trace_triangles(origin, ray, triangles, kinds, distance)
                    if virtual['kind'] != semantic[y,x]:
                        raise ValueError('virtual semantic kind differs from Blender ID')
                    for camera in cameras:
                        uv = products[f'camera{camera}-pixels.f32'][y,x]
                        origin, ray = inverse_ray(archive['config']['cameras'][camera], uv, pose)
                        hit = trace_triangles(origin, ray, triangles, kinds)
                        error, matched = matching(hit, virtual, plan['geometry_tolerances_m'])
                        sources.append(dict(camera=camera, uv=uv.tolist(), kind=hit['kind'], error_m=error,
                            matched=matched, rgb=bilinear_rgb(images[camera], uv).tolist()))
                elif cameras:
                    raise ValueError('eligible outside domain')
                expected = float(np.ptp([s['rgb'] for s in sources], axis=0).max()) if len(sources) >= 2 else None
                byte = int(np.floor(expected+.5)) if expected is not None else 0
                p = mask[y,x].tolist()
                passed = (p[1] == len(cameras) and p[2] == (255 if len(cameras)>=2 else 0)
                    and p[3] == (255 if domain else 0) and abs(p[0]-byte) <= plan['rgb_byte_tolerance']
                    and (len(cameras)>=2 or p[0] == 0))
                rows.append(dict(id=call['id'], profile=call['profile'], y=y, x=x, domain=domain,
                    virtual_kind=virtual['kind'] if virtual else None, sources=sources, native_pixel=p,
                    expected_range_byte=expected, metric_passed=passed, byte_error=abs(p[0]-byte)))
        rgb = products['output.rgba'][...,:3].astype(float)
        reference = np.asarray(Image.open(source/'capture/virtual_0000.png').convert('RGB'), float)
        available = np.array([int(v).bit_count() >= 2 for v in visibility.ravel()]).reshape(h,w)
        common_domain = np.ones((h,w), bool)
        for sibling in calls:
            if sibling['id'] != call['id']:
                continue
            other_directory = root/sibling['directory']
            other = json.loads(verified(other_directory/'report.json', sibling['correspondence_report_sha256']).read_text())
            region_product = next(p for p in other['products'] if p['file'] == 'carrier-regions.u8')
            regions = load_product(other_directory, region_product)[...,0]
            common_domain &= ~np.isin(regions, [0,3,5])
        for kind in [1,2]:
            interior = binary_erosion(semantic == kind, iterations=1)
            for label, roi in [('interior', interior), ('two_camera_visible_interior', interior & available),
                    ('posthoc_common_domain_interior', interior & common_domain),
                    ('posthoc_two_camera_visible_common_domain_interior', interior & available & common_domain)]:
                quality.append(dict(id=call['id'], profile=call['profile'], kind=kind, roi=label,
                    pixels=int(roi.sum()), mae_byte=float(np.abs(rgb-reference)[roi].mean()) if roi.any() else None))
        captures.append(dict(id=call['id'], profile=call['profile'], report_sha256=sha(directory/'report.json'),
            device=m['device'], source_fingerprint=m['source_fingerprint'], metric_payload_sha256=metric['payload_sha256'],
            camera_roundtrip_max_abs=camera_delta))
    return dict(schema_version=1, scope=plan['scope'], plan_sha256=sha(plan_path),
        scene_study_sha256=sha(root/plan['output_root']/'capture-study.json'), invocation_sha256=sha(calls_path),
        collector_sha256=sha(Path(__file__)), anchors=anchors, all_anchors_passed=all(a['passed'] for a in anchors),
        all_metric_checks_passed=all(r['metric_passed'] for r in rows), rows=rows, quality=quality,
        virtual_semantic_checks=sum(r['domain'] for r in rows),
        captures=captures, limitations=plan['limitations'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, default=Path('configs/research/textured-source-retry-plan.json'))
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--output-report', type=Path, required=True)
    args = parser.parse_args(); root = Path(__file__).resolve().parents[2]
    if args.capture:
        capture(root, args.plan.resolve())
    report = collect(root, args.plan.resolve())
    args.output_report.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    if not report['all_anchors_passed'] or not report['all_metric_checks_passed']:
        raise SystemExit('oracle failure retained in report')
