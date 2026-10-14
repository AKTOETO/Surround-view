"""New texture/pose fixtures with independent Blender RGB, IDs and triangles.

Run capture(root, plan_path) in Blender. Restores the user's active scene.
"""
import hashlib
import json
from pathlib import Path
import bpy
import numpy as np
from diagnostic import build_diagnostic
from paired_truth import capture_paired, virtual_pose
from geometry_truth import opaque_hit
from textured_fixture import texture


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def textured_material(directory, seed, period):
    pixels = texture(seed)
    image = bpy.data.images.new('SV independent texture', width=128, height=128)
    rgba = np.concatenate([pixels.astype(float)/255, np.ones((128, 128, 1))], axis=-1)
    image.pixels.foreach_set(rgba.astype(np.float32).ravel())
    image.filepath_raw = str(directory/f'texture-{seed}.png')
    image.file_format = 'PNG'
    image.save()
    image.pack()
    mat = bpy.data.materials.new('SV world-space textured emission')
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    position = nodes.new('ShaderNodeNewGeometry')
    scale = nodes.new('ShaderNodeVectorMath'); scale.operation = 'SCALE'
    scale.inputs['Scale'].default_value = 1/period
    bitmap = nodes.new('ShaderNodeTexImage'); bitmap.image = image
    bitmap.projection = 'BOX'; bitmap.projection_blend = 0
    bitmap.interpolation = 'Linear'; bitmap.extension = 'REPEAT'
    emission = nodes.new('ShaderNodeEmission')
    output = nodes.new('ShaderNodeOutputMaterial')
    links.new(position.outputs['Position'], scale.inputs[0])
    links.new(scale.outputs['Vector'], bitmap.inputs['Vector'])
    links.new(bitmap.outputs['Color'], emission.inputs['Color'])
    links.new(emission.outputs[0], output.inputs['Surface'])
    return mat


def export_geometry(scene, directory, target, road, ego, config):
    deps = bpy.context.evaluated_depsgraph_get()
    triangles, kinds = [], []
    def kind(obj):
        return 1 if obj == target else 2 if obj == road else 3
    for obj in scene.objects:
        if obj.type != 'MESH' or obj.hide_render:
            continue
        evaluated = obj.evaluated_get(deps); mesh = evaluated.to_mesh()
        try:
            mesh.calc_loop_triangles()
            for triangle in mesh.loop_triangles:
                triangles.append([list(evaluated.matrix_world@mesh.vertices[v].co) for v in triangle.vertices])
                kinds.append(kind(obj))
        finally:
            evaluated.to_mesh_clear()
    np.save(directory/'triangles.npy', np.asarray(triangles), allow_pickle=False)
    np.save(directory/'kinds.npy', np.asarray(kinds, np.uint8), allow_pickle=False)
    pose = np.asarray(ego.matrix_world)@virtual_pose(config)
    h, w = config['output']['height'], config['output']['width']
    tangent = np.tan(config['virtual_camera']['fov_y_rad']/2)
    near, far = config['virtual_camera']['clip_m']; anchors = []
    for y in range(8, h, 32):
        for x in range(8, w, 32):
            ray = pose[:3, :3]@np.array([(2*(x+.5)/w-1)*tangent*w/h, (1-2*(y+.5)/h)*tangent, -1])
            length = np.linalg.norm(ray); direction = ray/length
            origin = pose[:3, 3]+near*ray
            hit = opaque_hit(scene, deps, origin, direction, (far-near)*length)
            anchors.append(dict(y=y, x=x, origin=origin.tolist(), direction=direction.tolist(),
                max_distance_m=float((far-near)*length), kind=kind(hit[4].original) if hit[0] else 0,
                point=list(hit[1]) if hit[0] else None))
    return anchors


def capture(root, plan_path):
    root, plan_path = Path(root), Path(plan_path)
    plan = json.loads(plan_path.read_text())
    for name, digest in plan['frozen_files'].items():
        if sha(root/name) != digest:
            raise ValueError('frozen file changed: '+name)
    output = root/plan['output_root']; output.mkdir(exist_ok=False)
    original = bpy.context.window.scene
    results = []
    try:
        for case in plan['cases']:
            directory = output/case['id']; directory.mkdir()
            scene = build_diagnostic(case); bpy.context.window.scene = scene
            scene.render.engine = 'CYCLES'
            scene.cycles.samples = plan['render_samples']; scene.cycles.use_denoising = False
            scene.cycles.seed = case['scenario']['seed']; scene.cycles.device = 'CPU'
            ego = scene.objects[scene['sv_ego']]
            target = scene.objects[json.loads(scene['sv_diagnostic_target'])['object_name']]
            road = next(o for o in scene.objects if o.name.split('.')[0] == 'SV road')
            keep = {ego, *ego.children_recursive, target, road}
            for obj in list(scene.objects):
                if obj.type == 'MESH' and obj not in keep:
                    bpy.data.objects.remove(obj, do_unlink=True)
            road.scale.y = 100/12
            target.rotation_euler.z = case['target_yaw_rad']
            for obj, seed, period in [(road, case['scenario']['seed'], 2.4), (target, case['scenario']['seed']+1, .6)]:
                obj.data.materials.clear(); obj.data.materials.append(textured_material(directory, seed, period))
            for key in ('sv_config', 'sv_nominal_config'):
                cfg = json.loads(scene[key]); cfg['virtual_camera'] = case['virtual_camera']
                scene[key] = json.dumps(cfg)
            capture_paired(scene, directory/'capture', **case['capture'])
            cfg = json.loads((directory/'capture/paired_truth.json').read_text())['config']
            bpy.context.view_layer.update()
            anchors = export_geometry(scene, directory, target, road, ego, cfg)
            results.append(dict(id=case['id'], anchors=anchors,
                sha256={str(p.relative_to(directory)): sha(p) for p in sorted(directory.rglob('*')) if p.is_file()}))
            (output/'progress.json').write_text(json.dumps(dict(completed=[r['id'] for r in results]))+'\n')
    finally:
        bpy.context.window.scene = original
    (output/'capture-study.json').write_text(json.dumps(dict(plan_sha256=sha(plan_path),
        blender_version=bpy.app.version_string, cases=results), indent=2)+'\n')
    return str(output)
