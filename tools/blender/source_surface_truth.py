"""Independent opaque scene rays for a frozen native correspondence capture; no RGB renderer."""
import hashlib
import json
from pathlib import Path

import bpy
from mathutils import Matrix
import numpy as np

from diagnostic import build_diagnostic
from geometry_truth import opaque_hit
from paired_truth import virtual_pose


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def matrix(directory, product):
    path = directory / product['file']
    if sha(path) != product['sha256']:
        raise ValueError('native matrix checksum mismatch')
    dtype = '<f4' if product['dtype'] == 'float32' else 'u1'
    return np.fromfile(path, dtype=dtype).reshape(product['height'], product['width'], product['channels'])


def capture(root, plan_path):
    root, plan_path = Path(root), Path(plan_path)
    plan = json.loads(plan_path.read_text())
    for name, digest in plan['frozen_files'].items():
        if sha(root / name) != digest:
            raise ValueError('frozen input changed: ' + name)
    inputs, output = root / plan['input_root'], root / plan['output_root']
    case = json.loads((inputs / 'plan.json').read_text())['cases'][0]
    truth_path = inputs / 'seed101-capture/paired_truth.json'
    archived = json.loads(truth_path.read_text())
    frame = archived['frames'][0]
    legacy_path = inputs / 'seed101-capture' / frame['objects']
    if sha(legacy_path) != archived['sha256'][frame['objects']]:
        raise ValueError('archived virtual ID checksum mismatch')
    target_id = next(v for k, v in archived['objects'].items() if 'coded object target' in k)
    legacy_mask = np.load(legacy_path, allow_pickle=False) == target_id
    provenance = json.loads((inputs.parent / 'provenance.json').read_text())
    for name, digest in provenance['generator_sha256'].items():
        if sha(Path(__file__).with_name(name)) != digest:
            raise ValueError('historical scene helper changed: ' + name)
    original = bpy.context.window.scene
    try:
        scene = build_diagnostic(case)
        bpy.context.window.scene = scene
        ego = scene.objects[scene['sv_ego']]
        target = scene.objects[json.loads(scene['sv_diagnostic_target'])['object_name']]
        road = next(o for o in scene.objects if o.name.split('.')[0] == 'SV road')
        keep = {ego, *ego.children_recursive, target, road}
        for obj in list(scene.objects):
            if obj.type == 'MESH' and obj not in keep:
                bpy.data.objects.remove(obj, do_unlink=True)
        master = json.loads((root / 'assets/scenarios/parallax-sensor-v1.json').read_text())
        # Exactly the frozen parallax_sensor flat-road override.
        road.scale.y = master['ground_size_m'][1] / 12.
        road.scale.x = master['ground_size_m'][0] / 100.
        scene.frame_set(1)
        ego.matrix_world = Matrix(frame['T_world_from_vehicle'])
        bpy.context.view_layer.update()
        config = archived['config']
        generated = json.loads(scene['sv_config'])
        for old, new in zip(config['cameras'], generated['cameras']):
            if not np.allclose(old['T_camera_from_vehicle'], new['T_camera_from_vehicle'], atol=1e-10, rtol=0):
                raise ValueError('reconstructed rig differs from archived calibration')
            if any(old['projection']['k']) or old['projection'].get('alpha', 0):
                raise ValueError('truth inverse supports only equidistant zero-skew optics')
        if not np.allclose(target.location, frame['diagnostic_target_position_m'], atol=1e-5, rtol=0):
            raise ValueError('target pose changed')
        vehicle_pose = np.array(frame['T_world_from_vehicle'])
        virtual = vehicle_pose @ virtual_pose(config)
        h, w = legacy_mask.shape
        tangent = np.tan(config['virtual_camera']['fov_y_rad'] / 2)
        near, far = config['virtual_camera']['clip_m']
        dependency = bpy.context.evaluated_depsgraph_get()
        mask = np.zeros((h, w), bool)
        points = np.full((h, w, 3), np.nan, np.float32)
        for y in range(h):
            for x in range(w):
                local = np.array([(2*(x+.5)/w-1)*tangent*w/h,
                                  (1-2*(y+.5)/h)*tangent, -1.])
                direction = virtual[:3, :3] @ local
                length = np.linalg.norm(direction)
                direction /= length
                hit = opaque_hit(scene, dependency, virtual[:3, 3] + direction*near*length,
                                 direction, (far-near)*length)
                if hit[0] and hit[4].original == target:
                    mask[y, x] = True
                    points[y, x] = hit[1]
        if not np.array_equal(mask, legacy_mask):
            raise ValueError(f'reconstructed direct target mask differs in {np.count_nonzero(mask != legacy_mask)} pixels')
        coordinates = np.argwhere(mask)
        output.mkdir(parents=True, exist_ok=True)
        files = {}
        def save(name, value):
            path = output / name
            np.save(path, value, allow_pickle=False)
            files[name] = sha(path)
        save('direct-mask.npy', mask)
        save('direct-points.npy', points)
        save('target-pixels.npy', coordinates)
        for carrier in plan['carriers']:
            directory = output / 'native' / carrier
            report = json.loads((directory / 'report.json').read_text())
            if report['config']['cameras'] != config['cameras']:
                raise ValueError('native cameras differ from truth calibration')
            products = {p['file']: p for p in report['products']}
            hits = np.full((len(coordinates), 4, 3), np.nan, np.float32)
            kinds = np.zeros((len(coordinates), 4), np.uint8)
            for i, camera in enumerate(config['cameras']):
                pixels = matrix(directory, products[f'camera{i}-pixels.f32'])
                status = matrix(directory, products[f'camera{i}-status.u8'])[..., 0]
                pose = vehicle_pose @ np.linalg.inv(camera['T_camera_from_vehicle'])
                optics = camera['projection']
                for n, (y, x) in enumerate(coordinates):
                    if status[y, x] != 1:
                        continue
                    u, v = pixels[y, x]
                    a, b = (u-optics['cx'])/optics['fx'], (v-optics['cy'])/optics['fy']
                    theta = np.hypot(a, b)
                    scale = np.sin(theta)/theta if theta > 1e-14 else 1
                    direction = pose[:3, :3] @ np.array([a*scale, b*scale, np.cos(theta)])
                    hit = opaque_hit(scene, dependency, pose[:3, 3], direction, 200)
                    if hit[0]:
                        obj = hit[4].original
                        hits[n, i] = hit[1]
                        kinds[n, i] = 1 if obj == target else 2 if obj == road else 3 if obj in keep-{target,road} else 4
            save(carrier + '-source-hits.npy', hits)
            save(carrier + '-source-kinds.npy', kinds)
        record = {'schema_version': 1, 'scope': 'opaque_subpixel_source_rays_at_native_uv_target_roi',
                  'plan_sha256': sha(plan_path), 'blender_version': bpy.app.version_string,
                  'helper_sha256': sha(__file__), 'archived_truth_sha256': sha(truth_path),
                  'direct_mask_bitwise_equal': True, 'target_pixels': int(mask.sum()),
                  'source_kinds': {'0': 'no_hit_or_invalid_projection', '1': 'target', '2': 'road', '3': 'ego', '4': 'other'},
                  'files': files}
        (output / 'scene-truth.json').write_text(json.dumps(record, indent=2)+'\n')
        return record
    finally:
        bpy.context.window.scene = original
