"""Geometry-only gate for the final bounded target-overlap control."""
import hashlib
import json
from pathlib import Path
import bpy
import numpy as np
from scipy.ndimage import binary_erosion
from diagnostic import build_diagnostic
from paired_truth import object_truth, virtual_pose


def check(root, plan_path):
    root, plan_path = Path(root), Path(plan_path)
    plan = json.loads(plan_path.read_text())
    for name, digest in plan['frozen_files'].items():
        if hashlib.sha256((root/name).read_bytes()).hexdigest() != digest:
            raise ValueError('frozen file changed: '+name)
    original = bpy.context.window.scene
    rows = []
    try:
        for case in plan['cases']:
            scene = build_diagnostic(case); bpy.context.window.scene = scene
            ego = scene.objects[scene['sv_ego']]
            target = scene.objects[json.loads(scene['sv_diagnostic_target'])['object_name']]
            road = next(o for o in scene.objects if o.name.split('.')[0] == 'SV road')
            keep = {ego, *ego.children_recursive, target, road}
            for obj in list(scene.objects):
                if obj.type == 'MESH' and obj not in keep:
                    bpy.data.objects.remove(obj, do_unlink=True)
            road.scale.y = 100/12
            target.rotation_euler.z = case['target_yaw_rad']
            cfg = json.loads(scene['sv_config']); cfg['virtual_camera'] = case['virtual_camera']
            cfg['output'] = dict(width=case['capture']['width'], height=case['capture']['height'])
            bpy.context.view_layer.update()
            names = sorted(o.name for o in scene.objects if o.type == 'MESH')
            ids = {name:i+1 for i,name in enumerate(names)}
            labels, visibility = object_truth(scene, virtual_pose(cfg), cfg, ids, np.eye(4))
            interior = binary_erosion(labels == ids[target.name], iterations=1)
            paired = np.array([int(v).bit_count()>=2 for v in visibility.ravel()]).reshape(visibility.shape)
            count = int((interior & paired).sum())
            rows.append(dict(id=case['id'], target_interior_pixels=int(interior.sum()),
                target_two_camera_interior_pixels=count,
                paired_visibility_bit_counts={str(int(v)):int(n) for v,n in zip(*np.unique(visibility[interior & paired],return_counts=True))},
                passed=count>=plan['minimum_target_overlap_pixels']))
    finally:
        bpy.context.window.scene = original
    report = dict(plan_sha256=hashlib.sha256(plan_path.read_bytes()).hexdigest(),
        blender_version=bpy.app.version_string, rows=rows, all_passed=all(r['passed'] for r in rows))
    path = root/plan['preflight_report']; path.parent.mkdir(parents=True,exist_ok=True)
    if path.exists():
        raise ValueError('existing preflight report')
    path.write_text(json.dumps(report,indent=2)+'\n')
    return report
