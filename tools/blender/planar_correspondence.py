"""Isolated planar geometric control at archived native UVs; no RGB quality claim."""
import json
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector

from paired_truth import virtual_pose
from source_surface_controls import sha, matrix


def capture(root, plan_path):
    root, plan_path = Path(root), Path(plan_path)
    plan = json.loads(plan_path.read_text())
    for name, digest in plan['frozen_files'].items():
        if sha(root/name) != digest:
            raise ValueError('frozen input changed: '+name)
    original = bpy.context.window.scene
    scene = bpy.data.scenes.new('SV isolated planar correspondence')
    mesh = bpy.data.meshes.new('SV planar control mesh')
    extent = plan['plane_half_extent_m']
    mesh.from_pydata([(-extent,-extent,0),(extent,-extent,0),(extent,extent,0),(-extent,extent,0)], [], [(0,1,2,3)])
    obj = bpy.data.objects.new('SV planar control', mesh)
    scene.collection.objects.link(obj)
    output = root/plan['output_root']
    output.mkdir(exist_ok=False)
    records = []
    try:
        bpy.context.window.scene = scene
        bpy.context.view_layer.update()
        deps = bpy.context.evaluated_depsgraph_get()
        def hit(origin, direction):
            result = scene.ray_cast(deps, Vector(origin), Vector(direction), distance=200.)
            if not result[0] or result[4].original != obj:
                raise ValueError('ray misses isolated plane')
            return np.asarray(result[1], dtype=float)
        for frame in plan['frames']:
            source = root/plan['native_root']/f'frame{frame}'
            mask = np.load(source/'direct-mask.npy', allow_pickle=False)
            coordinates = np.argwhere(mask)
            archived = json.loads((root/plan['truth_path']).read_text())
            ego = np.asarray(archived['frames'][frame]['T_world_from_vehicle'], float)
            directory = output/f'frame{frame}'
            directory.mkdir()
            files = {}
            def save(name, array):
                np.save(directory/name, array, allow_pickle=False)
                files[name] = sha(directory/name)
            save('pixels.npy', coordinates)
            reference = json.loads((source/'native/plane/report.json').read_text())
            config = reference['config']
            pose = ego @ virtual_pose(config)
            w,h = config['output']['width'], config['output']['height']
            tangent = np.tan(config['virtual_camera']['fov_y_rad']/2)
            direct = []
            for y,x in coordinates:
                local = np.array([(2*(x+.5)/w-1)*tangent*w/h, (1-2*(y+.5)/h)*tangent, -1.])
                direction = pose[:3,:3] @ local
                direct.append(hit(pose[:3,3], direction/np.linalg.norm(direction)))
            save('plane-points.npy', np.asarray(direct))
            for carrier in plan['carriers']:
                native_dir = source/'native'/carrier
                report = json.loads((native_dir/'report.json').read_text())
                products = {p['file']: p for p in report['products']}
                hits = np.full((len(coordinates),4,3), np.nan)
                for i, camera in enumerate(config['cameras']):
                    optical = camera['projection']
                    if any(optical['k']) or optical.get('alpha',0):
                        raise ValueError('only zero-distortion equidistant control supported')
                    pixels = matrix(native_dir, products[f'camera{i}-pixels.f32'])
                    status = matrix(native_dir, products[f'camera{i}-status.u8'])[...,0]
                    camera_pose = ego @ np.linalg.inv(camera['T_camera_from_vehicle'])
                    for n,(y,x) in enumerate(coordinates):
                        if status[y,x] != 1:
                            continue
                        u,v = pixels[y,x]
                        a,b = (u-optical['cx'])/optical['fx'], (v-optical['cy'])/optical['fy']
                        theta = np.hypot(a,b)
                        scale = np.sin(theta)/theta if theta > 1e-14 else 1.
                        direction = camera_pose[:3,:3] @ np.array([a*scale,b*scale,np.cos(theta)])
                        hits[n,i] = hit(camera_pose[:3,3], direction)
                save(carrier+'-hits.npy', hits)
            records.append({'frame_index': frame, 'files': files})
        record = {'schema_version':1, 'scope':plan['scope'], 'plan_sha256':sha(plan_path),
                  'helper_sha256':sha(__file__), 'blender_version':bpy.app.version_string,
                  'scene_mesh_count':1, 'plane_z_m':0, 'records':records}
        (output/'scene-truth.json').write_text(json.dumps(record,indent=2)+'\n')
        return {'frames':len(records), 'pixels':[len(np.load(output/f"frame{r['frame_index']}"/'pixels.npy')) for r in records]}
    finally:
        bpy.context.window.scene = original
        bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.meshes.remove(mesh)
        bpy.data.scenes.remove(scene)
