"""Capture camera inputs and independent virtual RGB/object truth from one Blender scene.

Run inside Blender through MCP or its Python console. Files are generated artifacts.
Object IDs come from evaluated scene ray casts, never from a projection carrier.
"""
import hashlib
import json
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Vector
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rig import configuration, vehicle_pose
from scene import capture
from visibility import visible_camera_bits


def virtual_pose(config):
    view = config['virtual_camera']
    target = np.asarray(view.get('target_m', [0., 0., 0.]))
    az, el = view['azimuth_rad'], view['elevation_rad']
    eye = target + view['distance_m'] * np.array([
        np.cos(el)*np.cos(az), np.cos(el)*np.sin(az), np.sin(el)])
    forward = target - eye
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, [0., 0., 1.])
    if np.linalg.norm(right) < 1e-6:
        right = np.cross(forward, [0., 1., 0.])
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    matrix = np.eye(4)
    matrix[:3, :3] = np.column_stack([right, up, -forward])
    matrix[:3, 3] = eye
    return matrix


def object_truth(scene, pose, config, object_ids, vehicle_transform):
    """Nearest visible opaque mesh per pixel center, with perspective clipping.

    Unlike RGB, ray_cast has no antialiasing/transparency; boundary pixels therefore
    need an exclusion margin when using these IDs as an evaluation ROI.
    """
    w, h = config['output']['width'], config['output']['height']
    tangent = np.tan(config['virtual_camera']['fov_y_rad'] / 2)
    near, far = config['virtual_camera']['clip_m']
    dependency = bpy.context.evaluated_depsgraph_get()
    labels = np.zeros((h, w), np.uint16)
    visibility = np.zeros((h, w), np.uint8)
    origin = Vector(pose[:3, 3])
    def opaque_hit(start, direction, distance):
        # Scene.ray_cast includes viewport helpers that hide_render excludes from RGB.
        # Skip their entry/exit intersections instead of treating them as occluders.
        start = Vector(start)
        direction = Vector(direction)
        for _ in range(64):
            result = scene.ray_cast(dependency, start, direction, distance=float(distance))
            if not result[0] or not result[4].original.hide_render:
                return result
            advance = (result[1]-start).length + 1e-4
            start += direction * advance
            distance -= advance
            if distance <= 0:
                return (False, None, None, None, None, None)
        raise RuntimeError('too many hidden-helper intersections in visibility ray')

    def cast(start, direction, distance):
        found, location, *_ = opaque_hit(start, direction, distance)
        return np.asarray(location) if found else None

    for y in range(h):
        for x in range(w):
            local = np.array([(2*(x+.5)/w-1)*tangent*w/h,
                              (1-2*(y+.5)/h)*tangent, -1.])
            direction = pose[:3, :3] @ local
            length = np.linalg.norm(direction)
            direction /= length
            start = origin + Vector(direction * near * length)
            hit, location, _, _, obj, _ = opaque_hit(start, direction, (far-near)*length)
            if hit:
                labels[y, x] = object_ids[obj.original.name]
                visibility[y, x] = visible_camera_bits(
                    np.asarray(location), vehicle_transform, config['cameras'], cast)
    return labels, visibility


def capture_paired(scene, output, frames=3, face_size=64, frame_step=6, width=320, height=180):
    """A short straight drive, 2 m/s, with matched timestamps, poses and scene snapshot."""
    output = Path(output).resolve()
    if scene != bpy.context.scene or width < 16 or height < 16:
        raise ValueError('active context scene and output dimensions >=16 required')
    camera = scene.camera
    ego = scene.objects[scene['sv_ego']]
    render = scene.render
    saved_render = {key: getattr(render, key) for key in (
        'resolution_x', 'resolution_y', 'resolution_percentage', 'filepath',
        'pixel_aspect_x', 'pixel_aspect_y')}
    saved_camera = {key: getattr(camera.data, key) for key in (
        'lens', 'sensor_fit', 'sensor_width', 'sensor_height', 'clip_start', 'clip_end')}
    saved_image = {key: getattr(render.image_settings, key) for key in ('file_format', 'color_mode')}
    saved_matrix, saved_ego = camera.matrix_world.copy(), ego.matrix_world.copy()
    saved_frame = scene.frame_current
    saved_config = scene.get('sv_config')
    cfg = json.loads(saved_config) if saved_config else configuration()
    cfg['output'] = {'width': width, 'height': height}
    scene['sv_config'] = json.dumps(cfg)
    try:
        render.resolution_percentage = 100
        render.pixel_aspect_x = render.pixel_aspect_y = 1.
        camera.data.sensor_fit, camera.data.sensor_width = 'HORIZONTAL', 36.
        camera.data.clip_start, camera.data.clip_end = .025, 200.
        render.image_settings.file_format, render.image_settings.color_mode = 'PNG', 'RGB'
        capture(scene, output, frames=frames, face_size=face_size, frame_step=frame_step)
        names = sorted(obj.name for obj in scene.objects if obj.type == 'MESH')
        if len(names) >= 65535:
            raise ValueError('too many objects for uint16 truth')
        ids = {name: index+1 for index, name in enumerate(names)}
        ego_ids = []
        for name, index in ids.items():
            obj = scene.objects[name]
            if obj == ego or obj in ego.children_recursive:
                ego_ids.append(index)
        rows = []
        projection_errors = []
        camera.data.sensor_fit = 'VERTICAL'
        camera.data.lens = camera.data.sensor_height / (2*np.tan(cfg['virtual_camera']['fov_y_rad']/2))
        camera.data.clip_start, camera.data.clip_end = cfg['virtual_camera']['clip_m']
        render.resolution_x, render.resolution_y = width, height
        for index in range(frames):
            frame = index * frame_step
            scene.frame_set(frame+1)
            pose = vehicle_pose(frame)
            ego.matrix_world = Matrix(pose.tolist())
            view_pose = pose @ virtual_pose(cfg)
            camera.matrix_world = Matrix(view_pose.tolist())
            bpy.context.view_layer.update()
            from bpy_extras.object_utils import world_to_camera_view
            for x, y in ((0, 0), (width//2, height//2), (width-1, height-1)):
                tangent = np.tan(cfg['virtual_camera']['fov_y_rad']/2)
                local = [(2*(x+.5)/width-1)*tangent*width/height,
                         (1-2*(y+.5)/height)*tangent, -1., 1.]
                uv = world_to_camera_view(scene, camera, Vector((view_pose @ local)[:3]))
                error = np.linalg.norm([uv.x*width-.5-x, (1-uv.y)*height-.5-y])
                projection_errors.append(float(error))
            if max(projection_errors) > .001:
                raise RuntimeError('virtual reference pixel convention mismatch')
            rgb_name, labels_name = f'virtual_{index:04d}.png', f'objects_{index:04d}.npy'
            render.filepath = str(output / rgb_name)
            bpy.ops.render.render(write_still=True, scene=scene.name)
            labels, visibility = object_truth(scene, view_pose, cfg, ids, pose)
            np.save(output / labels_name, labels, allow_pickle=False)
            visibility_name = f'visibility_{index:04d}.npy'
            np.save(output / visibility_name, visibility, allow_pickle=False)
            rows.append({'scenario_timestamp_ns': str(round(frame*1e9/30)),
                         'T_world_from_vehicle': pose.tolist(), 'rgb': rgb_name,
                         'objects': labels_name, 'visibility': visibility_name})
        files = [row[key] for row in rows for key in ('rgb', 'objects', 'visibility')]
        metadata = {'schema_version': 2, 'frames': rows, 'objects': ids,
                    'source_visibility': {'encoding': 'uint8 bit i = visible from camera i',
                                          'tolerance_m': .02,
                                          'ignored_render_helpers': sorted(o.name for o in scene.objects if o.hide_render),
                                          'meaning': 'nearest opaque ray hit at direct-view scene point'},
                    'ego_object_ids': ego_ids, 'config': cfg,
                    'virtual_projection_max_error_px': max(projection_errors),
                    'capture_sha256': hashlib.sha256((output/'capture.json').read_bytes()).hexdigest(),
                    'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    'visibility_script_sha256': hashlib.sha256(Path(__file__).with_name('visibility.py').read_bytes()).hexdigest(),
                    'sha256': {name: hashlib.sha256((output/name).read_bytes()).hexdigest() for name in files},
                    'limitations': ['synthetic scene, scripted straight drive, no sensor noise',
                                    'object ray casts ignore alpha and antialiasing; exclude boundaries',
                                    'RGB reference includes geometry hidden from input cameras']}
        (output/'paired_truth.json').write_text(json.dumps(metadata, indent=2)+'\n')
        return str(output)
    finally:
        scene.frame_set(saved_frame)
        camera.matrix_world, ego.matrix_world = saved_matrix, saved_ego
        for key, value in saved_render.items():
            setattr(render, key, value)
        for key, value in saved_camera.items():
            setattr(camera.data, key, value)
        for key, value in saved_image.items():
            setattr(render.image_settings, key, value)
        if saved_config is None:
            del scene['sv_config']
        else:
            scene['sv_config'] = saved_config
        bpy.context.view_layer.update()


def capture_study(plan_path, output):
    """Build isolated scene variants from a saved plan; restore the active user scene."""
    from scene import build_scene
    from scenario import validate
    plan_path, output = Path(plan_path), Path(output)
    plan = json.loads(plan_path.read_text())
    recipes = [validate(recipe) for recipe in plan['scenarios']]
    if len({r['seed'] for r in recipes}) != len(recipes):
        raise ValueError('unique scene seeds required')
    output.mkdir(parents=True, exist_ok=False)
    original = bpy.context.window.scene
    captures = []
    try:
        for recipe in recipes:
            variant = build_scene(recipe)
            bpy.context.window.scene = variant
            directory = output/f"seed{recipe['seed']}-capture"
            capture_paired(variant, directory, **plan['capture'])
            captures.append({'seed': recipe['seed'], 'scene': variant.name,
                             'capture_sha256': hashlib.sha256((directory/'capture.json').read_bytes()).hexdigest()})
    finally:
        bpy.context.window.scene = original
    (output/'study_capture.json').write_text(json.dumps({
        'plan_sha256': hashlib.sha256(plan_path.read_bytes()).hexdigest(),
        'captures': captures}, indent=2)+'\n')
    return str(output)
