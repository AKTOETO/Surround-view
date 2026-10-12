"""Independent scene rays for frozen visibility residuals and all source-visible controls."""
import json
from pathlib import Path
import bpy
from mathutils import Matrix
import numpy as np
from diagnostic import build_diagnostic
from geometry_truth import opaque_hit
from paired_truth import virtual_pose
from source_surface_controls import matrix, sha
from source_footprint_audit import footprint
from cube_footprint_audit import fisheye_ray
from visibility_residual import select_pixels


def capture(root, plan_path):
    root, plan_path = Path(root), Path(plan_path)
    plan = json.loads(plan_path.read_text())
    for name, digest in plan['frozen_files'].items():
        if sha(root / name) != digest:
            raise ValueError('frozen file changed: ' + name)
    inputs = root / plan['input_root']
    case = json.loads((inputs / 'plan.json').read_text())['cases'][0]
    archived = json.loads((root / plan['truth_path']).read_text())
    provenance = json.loads((inputs.parent / 'provenance.json').read_text())
    for name, digest in provenance['generator_sha256'].items():
        if sha(Path(__file__).with_name(name)) != digest:
            raise ValueError('historical scene generator differs')
    output = root / plan['output_root']
    output.mkdir(exist_ok=False)
    original = bpy.context.window.scene
    records = []
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
        road.scale.y = master['ground_size_m'][1] / 12.
        road.scale.x = master['ground_size_m'][0] / 100.
        for frame in plan['frames']:
            t = archived['frames'][frame]
            scene.frame_set(1 + frame * case['capture']['frame_step'])
            ego.matrix_world = Matrix(t['T_world_from_vehicle'])
            bpy.context.view_layer.update()
            if not np.allclose(target.location, t['diagnostic_target_position_m'], atol=1e-5, rtol=0):
                raise ValueError('target pose differs')
            generated = json.loads(scene['sv_config'])
            for a, b in zip(archived['config']['cameras'], generated['cameras'], strict=True):
                if not np.allclose(a['T_camera_from_vehicle'], b['T_camera_from_vehicle'], atol=1e-10, rtol=0):
                    raise ValueError('rig differs')
            pose = np.asarray(t['T_world_from_vehicle'])
            virtual = pose @ virtual_pose(archived['config'])
            deps = bpy.context.evaluated_depsgraph_get()
            def cast(origin, direction, distance=200.):
                hit = opaque_hit(scene, deps, origin, direction, distance)
                if not hit[0]:
                    return 0, np.full(3, np.nan)
                obj = hit[4].original
                return (1 if obj == target else 2 if obj == road else 3 if obj in keep else 4), np.asarray(hit[1])
            capture_dir = (root / plan['truth_path']).parent
            ids = np.load(capture_dir / t['objects'], allow_pickle=False)
            vis = np.load(capture_dir / t['visibility'], allow_pickle=False)
            target_id = next(v for k, v in archived['objects'].items() if 'coded object target' in k)
            for variant in plan['variants']:
                native = root / plan['native_root'] / f'frame{frame}-{variant}'
                meta = json.loads((native / 'report.json').read_text())
                if meta['frame_index'] != frame:
                    raise ValueError('native frame differs')
                p = {v['file']: matrix(native, v) for v in meta['products']}
                coords, residual = select_pixels(p['output.rgba'][..., :3], ids == target_id, vis != 0, plan['chroma_threshold'])
                y, x = coords.T
                virtual_kinds = np.zeros(len(coords), np.uint8)
                centers = np.zeros((len(coords), 4), np.uint8)
                hits = np.full((len(coords), 4, 3), np.nan)
                texels = np.full((len(coords), 4, 4, 2), -1, np.int64)
                weights = np.zeros((len(coords), 4, 4))
                kinds = np.zeros((len(coords), 4, 4), np.uint8)
                h, w = ids.shape
                near, far = archived['config']['virtual_camera']['clip_m']
                tangent = np.tan(archived['config']['virtual_camera']['fov_y_rad'] / 2)
                for n, (yy, xx) in enumerate(coords):
                    local = np.array([(2*(xx+.5)/w-1)*tangent*w/h, (1-2*(yy+.5)/h)*tangent, -1])
                    direction = virtual[:3, :3] @ local
                    length = np.linalg.norm(direction)
                    direction /= length
                    virtual_kinds[n] = cast(virtual[:3, 3] + direction*near*length, direction, (far-near)*length)[0]
                for i, camera in enumerate(meta['config']['cameras']):
                    uv = p[f'camera{i}-pixels.f32'][y, x]
                    valid = p[f'camera{i}-status.u8'][y, x, 0] == 1
                    camera_pose = pose @ np.linalg.inv(camera['T_camera_from_vehicle'])
                    def sample(pixel):
                        return cast(camera_pose[:3, 3], camera_pose[:3, :3] @ fisheye_ray(pixel, camera['projection']))
                    texels[valid, i], weights[valid, i] = footprint(uv[valid], camera['resolution']['width'], camera['resolution']['height'])
                    cache = {}
                    for n in np.flatnonzero(valid):
                        centers[n, i], hits[n, i] = sample(uv[n])
                        for j, xy in enumerate(texels[n, i]):
                            key = tuple(xy)
                            if key not in cache:
                                cache[key] = sample(xy)[0]
                            kinds[n, i, j] = cache[key]
                directory = output / f'frame{frame}-{variant}'
                directory.mkdir()
                arrays = {'pixels.npy': coords, 'residual.npy': residual, 'virtual-kinds.npy': virtual_kinds,
                          'center-kinds.npy': centers, 'center-hits.npy': hits, 'texels.npy': texels,
                          'weights.npy': weights, 'texel-kinds.npy': kinds}
                for name, array in arrays.items():
                    np.save(directory / name, array, allow_pickle=False)
                records.append(dict(frame=frame, variant=variant, native_report_sha256=sha(native / 'report.json'),
                                    files={n: sha(directory / n) for n in arrays}))
        result = dict(schema_version=1, scope=plan['scope'], plan_sha256=sha(plan_path), helper_sha256=sha(__file__),
                      blender_version=bpy.app.version_string, records=records)
        (output / 'scene-truth.json').write_text(json.dumps(result, indent=2) + '\n')
        return [dict(frame=r['frame'], variant=r['variant'], pixels=len(np.load(output / f"frame{r['frame']}-{r['variant']}" / 'pixels.npy'))) for r in records]
    finally:
        bpy.context.window.scene = original
