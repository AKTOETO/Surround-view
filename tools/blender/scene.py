"""Build an isolated metric street scene and export perspective cube captures.

In Blender's console or through MCP:
    import sys
    sys.path.insert(0, '/path/to/surround-view/tools/blender')
    import scene
    street = scene.build_scene()
    scene.capture(street, '/path/to/new/capture', frames=2)

Headless: blender --background --python tools/blender/scene.py -- --output DIR
No external assets, downloads, add-ons or current-scene deletion are used.
"""
import argparse
import hashlib
import importlib
import json
import math
from pathlib import Path
import sys

import bpy
import numpy as np
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rig import FACES, configuration, face_basis, vehicle_pose
from diagnostic_motion import position_for_capture
from scenario import load as load_scenario, validate as validate_scenario, perturb, near_obstacle_positions
import board_targets
import depth as depth_tools

# Blender MCP and the interactive console keep Python modules between captures.
importlib.reload(board_targets)


def enum_value(owner, field, value):
    """Check RNA before setting static enum values; fail on unsupported versions."""
    options = [item.identifier for item in owner.bl_rna.properties[field].enum_items]
    if value not in options:
        raise RuntimeError(f"{field}={value} unavailable; choices: {options}")
    setattr(owner, field, value)


def material(name, rgb, metallic=0., roughness=0.65):
    mat = bpy.data.materials.new("SV " + name)
    mat.use_nodes = True
    bsdf = next(node for node in mat.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
    bsdf.inputs['Base Color'].default_value = (*rgb, 1)
    bsdf.inputs['Metallic'].default_value = metallic
    bsdf.inputs['Roughness'].default_value = roughness
    mat.diffuse_color = (*rgb, 1)
    return mat


def mesh_object(scene, name, vertices, faces, mat, parent=None):
    mesh = bpy.data.meshes.new("SV " + name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new("SV " + name, mesh)
    scene.collection.objects.link(obj)
    obj.data.materials.append(mat)
    obj.parent = parent
    return obj


def box(scene, name, xyz, size, mat, parent=None, bevel=0.):
    vertices = [(x * size[0] / 2, y * size[1] / 2, z * size[2] / 2)
                for x, y, z in [(-1,-1,-1), (1,-1,-1), (1,1,-1), (-1,1,-1),
                                (-1,-1,1), (1,-1,1), (1,1,1), (-1,1,1)]]
    faces = [(0,3,2,1), (4,5,6,7), (0,1,5,4), (1,2,6,5), (2,3,7,6), (3,0,4,7)]
    obj = mesh_object(scene, name, vertices, faces, mat, parent)
    obj.location = xyz
    if bevel:
        modifier = obj.modifiers.new("Rounded edges", 'BEVEL')
        modifier.width = bevel
        modifier.segments = 3
    return obj


def cylinder(scene, name, xyz, radius, depth, mat, parent=None, wheel=False):
    n = 24
    vertices = [(radius * math.cos(2 * math.pi * i / n),
                 radius * math.sin(2 * math.pi * i / n), z)
                for z in (-depth/2, depth/2) for i in range(n)]
    faces = [tuple(reversed(range(n))), tuple(range(n, 2*n))]
    faces += [(i, (i+1)%n, (i+1)%n+n, i+n) for i in range(n)]
    obj = mesh_object(scene, name, vertices, faces, mat, parent)
    obj.location = xyz
    if wheel:
        obj.rotation_euler.x = math.pi / 2
    return obj


def car(scene, name, xyz, paint, mats):
    root = bpy.data.objects.new("SV " + name, None)
    scene.collection.objects.link(root)
    root.location = xyz
    box(scene, "chassis", (0,0,.63), (4.6,1.8,.68), paint, root, .17)
    # Trapezoid passenger compartment; +X is the front of every car.
    vertices = [(-1.45,-.82,.9), (1.15,-.82,.9), (1.15,.82,.9), (-1.45,.82,.9),
                (-.9,-.65,1.5), (.55,-.65,1.5), (.55,.65,1.5), (-.9,.65,1.5)]
    mesh_object(scene, "cabin glass", vertices,
                [(0,1,5,4), (1,2,6,5), (2,3,7,6), (3,0,4,7), (4,5,6,7)],
                mats['glass'], root)
    box(scene, "roof", (-.175,0,1.515), (1.55,1.37,.07), paint, root, .06)
    for y in (-.9,.9):
        box(scene, "window pillar", (-.25,y*.85,1.22), (.09,.08,.62), paint, root)
        box(scene, "mirror", (.35,y*1.09,1.10), (.24,.16,.13), paint, root, .04)
        for x in (-1.45,1.4):
            cylinder(scene, "tire", (x,y,.36), .36,.23,mats['rubber'],root,True)
            cylinder(scene, "rim", (x,y*1.14,.36), .22,.03,mats['metal'],root,True)
    for x, mat in ((2.285,mats['white']), (-2.285,mats['red'])):
        for y in (-.57,.57):
            box(scene, "lamp", (x,y,.76), (.04,.39,.14),mat,root,.035)
    box(scene, "front grille", (2.303,0,.48), (.025,.7,.18),mats['rubber'],root)
    return root


def build_scene(recipe=None):
    recipe = validate_scenario(recipe) if recipe is not None else None
    nominal = configuration()
    cfg, offsets = perturb(nominal, recipe) if recipe else (nominal, [])
    rng = np.random.default_rng(recipe.get('seed', 0)) if recipe else None
    scene = bpy.data.scenes.new("SV Research Street")
    # Render engine is a dynamic enum. Its current value is valid on this host.
    scene.render.engine = bpy.context.scene.render.engine
    scene.render.resolution_percentage = 100
    scene.render.pixel_aspect_x = scene.render.pixel_aspect_y = 1
    enum_value(scene.render.image_settings, 'file_format', 'PNG')
    enum_value(scene.render.image_settings, 'color_mode', 'RGB')
    scene.render.film_transparent = False
    # Dynamic OCIO values are checked by Blender's setter, not static RNA items.
    try:
        scene.view_settings.view_transform = 'Standard'
    except TypeError as error:
        raise RuntimeError("Standard sRGB view transform is required") from error
    scene.view_settings.exposure = 0
    scene.view_settings.gamma = 1
    scene.world = bpy.data.worlds.new("SV daylight")
    scene.world.use_nodes = True
    background = next(n for n in scene.world.node_tree.nodes if n.type == 'BACKGROUND')
    background.inputs['Color'].default_value = (.38,.58,.85,1)
    background.inputs['Strength'].default_value = .65
    sun = bpy.data.lights.new("SV sunlight", 'SUN')
    sun.energy = 2.2
    sun.angle = math.radians(12)
    light = bpy.data.objects.new("SV sunlight", sun)
    scene.collection.objects.link(light)
    light.rotation_euler = (.45,-.5,-.3)
    mats = {name: material(name, rgb) for name, rgb in {
        'asphalt':(.065,.075,.085), 'pavement':(.48,.47,.42),
        'white':(.92,.9,.8), 'red':(.65,.035,.025), 'yellow':(.95,.54,.015),
        'glass':(.035,.10,.15), 'rubber':(.015,.019,.025), 'metal':(.42,.46,.5),
        'blue':(.025,.22,.5), 'green':(.075,.22,.055), 'bark':(.2,.1,.04),
        'brick':(.43,.19,.11), 'cream':(.69,.59,.4), 'gray':(.32,.4,.45),
    }.items()}
    box(scene, "ground", (0,0,-.15), (100,65,.25),mats['green'])
    box(scene, "road", (0,0,-.025), (100,12,.05),mats['asphalt'])
    for y in (-7.5,7.5):
        box(scene, "sidewalk", (0,y,.09), (100,3,.18),mats['pavement'])
        box(scene, "curb", (0,math.copysign(6.08,y),.11), (100,.16,.22),mats['white'])
    for x in range(-45,46,6):
        box(scene, "center dash", (x,-2.5,.006), (3,.12,.012),mats['white'])
    for x in range(-4,7,3):
        for y in (-4.8,4.8):
            box(scene, "parking bay", (x,y,.007), (.1,1.9,.014),mats['white'])
    for x in range(11,16):
        box(scene, "crosswalk", (x,0,.008), (.55,11.5,.016),mats['white'])
    spacing = recipe['world']['building_spacing_m'] if recipe else 9
    for side in (-1,1):
        for i, x in enumerate(np.arange(-32,37,spacing)):
            height = float(rng.uniform(*recipe['world']['building_height_m'])) if recipe else 5 + (i % 3) * 1.4
            mat = mats[['brick','cream','gray'][i%3]]
            box(scene, "building", (x,side*12,height/2), (7.5,6,height),mat,bevel=.08)
            for floor in range(int(height/2)):
                for dx in (-2.3,0,2.3):
                    box(scene, "window", (x+dx,side*8.98,1.3+floor*2),
                        (1.25,.045,1.1),mats['glass'])
            box(scene, "shop awning", (x,side*8.5,2.6), (5.8,1.2,.12),mats['blue'])
        for x in (-18,-7,8,24):
            cylinder(scene, "tree trunk", (x,side*7.7,1.2), .17,2.4,mats['bark'])
            box(scene, "tree crown", (x,side*7.7,3.1), (2.8,2.5,3),mats['green'],bevel=1.)
            cylinder(scene, "streetlight", (x+2,side*6.7,2.5), .06,5,mats['metal'])
            box(scene, "streetlight head", (x+2,side*6.5,5), (.7,.6,.14),mats['white'])
    car(scene,"parked red",(6,4.4,0),mats['red'],mats)
    car(scene,"parked gray",(-9,-4.4,0),mats['gray'],mats)
    positions = near_obstacle_positions(recipe)
    for x,y in positions:
        box(scene,"bollard base",(x,y,.04),(.4,.4,.08),mats['rubber'])
        cylinder(scene,"yellow bollard",(x,y,.5),.11,1.,mats['yellow'])
    ego = car(scene,"Ego",(0,0,0),mats['blue'],mats)
    for cam in cfg['cameras']:
        transform = np.array(cam['T_camera_from_vehicle'])
        center = -transform[:3,:3].T @ transform[:3,3]
        marker = box(scene, "camera " + cam['name'], center, (.065,.065,.065),mats['rubber'],ego)
        marker.hide_render = True  # A locator must not occlude its own optical center.
    scene['sv_ego'] = ego.name
    scene['sv_config'] = json.dumps(cfg)
    scene['sv_nominal_config'] = json.dumps(nominal)
    scene['sv_recipe'] = json.dumps(recipe)
    scene['sv_mount_offsets'] = json.dumps(offsets)
    scene['sv_near_obstacles'] = json.dumps(positions)
    camera = bpy.data.cameras.new("SV capture optics")
    enum_value(camera,'type','PERSP')
    enum_value(camera,'sensor_fit','HORIZONTAL')
    camera.sensor_width = 36
    camera.lens = 18  # 90 degrees; square sensor image.
    camera.clip_start = .025
    camera.clip_end = 200
    obj = bpy.data.objects.new("SV capture camera",camera)
    scene.collection.objects.link(obj)
    scene.camera = obj
    return scene


def check_optics(scene):
    """Independently check pixel-center conventions with Blender's projector."""
    from bpy_extras.object_utils import world_to_camera_view

    camera = scene.camera
    saved = (camera.matrix_world.copy(), camera.data.lens,
             scene.render.resolution_x, scene.render.resolution_y)
    errors = []
    try:
        camera.data.lens = 18
        scene.render.resolution_x = scene.render.resolution_y = 256
        for cam in json.loads(scene.get('sv_config', json.dumps(configuration())))['cameras']:
            optical = np.linalg.inv(cam['T_camera_from_vehicle'])
            for face in FACES:
                pose = optical.copy()
                pose[:3,:3] = optical[:3,:3] @ face_basis(face).T @ np.diag([1,-1,-1])
                camera.matrix_world = Matrix(pose.tolist())
                for x,y in ((-.7,-.4),(.5,.6),(0.,0.)):
                    world = pose @ [x,-y,-1.,1.]
                    uv = world_to_camera_view(scene,camera,Vector(world[:3]))
                    pixel = np.array([uv.x*256-.5,(1-uv.y)*256-.5])
                    errors.append(float(np.linalg.norm(pixel-((np.array([x,y])+1)*128-.5))))
    finally:
        camera.matrix_world, camera.data.lens = saved[:2]
        scene.render.resolution_x, scene.render.resolution_y = saved[2:]
    if max(errors) > .001:
        raise RuntimeError(f'Blender optics convention mismatch: {max(errors)} px')
    return {'points':len(errors), 'max_error_px':max(errors), 'threshold_px':.001}


def capture(scene, output, frames=2, face_size=256, start_frame=0, calibration_boards=False,
            depth_truth=False, frame_step=1):
    """Render all optical centers at exactly the same scenario pose per row.

    Write completion metadata only after every requested image was saved.
    Existing output directories are rejected to avoid mixing datasets.
    """
    if (not 1 <= frames <= 300 or not 32 <= face_size <= 2048 or start_frame < 0
            or not isinstance(frame_step, int) or frame_step < 1):
        raise ValueError("frames=1..300, face_size=32..2048, start_frame>=0 required")
    output = Path(output).resolve()
    optics_check = check_optics(scene)
    output.mkdir(parents=True, exist_ok=False)
    cfg = json.loads(scene.get('sv_config', json.dumps(configuration())))
    nominal_cfg = json.loads(scene.get('sv_nominal_config', json.dumps(cfg)))
    if calibration_boards:
        # Give the metric-target dataset enough pixels for corner detection while preserving FOV.
        for candidate in (cfg, nominal_cfg):
            for cam in candidate['cameras']:
                cam['resolution']['width'] *= 2
                cam['resolution']['height'] *= 2
                projection = cam['projection']
                projection['fx'] *= 2
                projection['fy'] *= 2
                projection['cx'] = (projection['cx'] + .5) * 2 - .5
                projection['cy'] = (projection['cy'] + .5) * 2 - .5
                cam['calibration_id'] += '-board-800'
    ego = scene.objects[scene['sv_ego']]
    targets = board_targets.create_targets(scene) if calibration_boards else None
    scene.camera.data.lens = 18
    scene.render.resolution_x = scene.render.resolution_y = face_size
    depth_dir = output / 'depth'
    depth_state = depth_tools.attach_depth_output(scene, depth_dir) if depth_truth else None
    rows = []
    diagnostic_target = json.loads(scene.get('sv_diagnostic_target', 'null'))
    target_object = scene.objects.get(diagnostic_target['object_name']) if diagnostic_target else None
    for index in range(frames):
        frame = start_frame + index * frame_step
        pose = vehicle_pose(frame)
        ego.matrix_world = Matrix(pose.tolist())
        scene.frame_set(frame + 1)
        if target_object:
            target_position = position_for_capture(diagnostic_target, {'frames':frames, 'frame_step':frame_step}, index)
            if target_position is not None:
                target_object.location = target_position
        captures = []
        board_records = []
        for cam in cfg['cameras']:
            target_pose = board_targets.pose(cam, frame, pose) if targets else None
            if targets:
                board_targets.set_target(targets, cam['id'], pose, target_pose)
            transform = np.array(cam['T_camera_from_vehicle'])
            optical_pose = pose @ np.linalg.inv(transform)
            paths = {}
            depth_paths = {}
            for face in FACES:
                # Negative optical Z cannot occur inside the supported <180-degree FOV.
                if face == 'nz':
                    continue
                basis = face_basis(face)
                matrix = optical_pose.copy()
                matrix[:3,:3] = optical_pose[:3,:3] @ basis.T @ np.diag([1,-1,-1])
                scene.camera.matrix_world = Matrix(matrix.tolist())
                filename = f"frame{index:04d}_cam{cam['id']}_{face}.png"
                scene.render.filepath = str(output / filename)
                depth_filename = None
                if depth_state:
                    depth_output = depth_state[-1]
                    depth_stem = f"frame{index:04d}_cam{cam['id']}_{face}_depth_####"
                    depth_output.file_name = depth_stem
                    depth_filename = depth_stem.replace('####', f'{frame + 1:04d}') + '.exr'
                bpy.ops.render.render(write_still=True, scene=scene.name)
                paths[face] = filename
                if depth_state:
                    depth_paths[face] = str(Path('depth') / depth_filename)
            camera_capture = {'id':cam['id'], 'faces':paths,
                              'T_world_from_camera':optical_pose.tolist()}
            if depth_truth:
                camera_capture['depth_faces'] = depth_paths
            captures.append(camera_capture)
            if targets:
                board_records.append({'camera_id':cam['id'],
                                      'T_vehicle_from_board':target_pose.tolist(),
                                      'corner_order':'reverse_x'})
        row = {'scenario_timestamp_ns':str(round(frame * 1_000_000_000 / 30)),
               'T_world_from_vehicle':pose.tolist(), 'cameras':captures}
        if targets:
            row['calibration_boards'] = board_records
        rows.append(row)
    if target_object and diagnostic_target.get('motion_positions_m'):
        target_object.location = position_for_capture(
            diagnostic_target, {'frames':frames, 'frame_step':frame_step}, 0)
    depth_tools.restore_depth_output(scene, depth_state)
    # A true 3D overview, separate from sv-server's reconstructed surround view.
    scene.camera.location = (10,-12,10)
    direction = Vector((1,0,0.4)) - scene.camera.location
    scene.camera.rotation_euler = direction.to_track_quat('-Z','Y').to_euler()
    scene.camera.data.lens = 32
    scene.render.resolution_x, scene.render.resolution_y = 960,640
    scene.render.filepath = str(output / 'overview.png')
    bpy.ops.render.render(write_still=True, scene=scene.name)
    # Save just this scene and its dependencies, never overwrite the user's open .blend.
    bpy.data.libraries.write(str(output / 'street.blend'), {scene})
    metadata = {'schema_version':1, 'origin':'blender_metric_street_v1',
                'blender_version':bpy.app.version_string, 'engine':scene.render.engine,
                'view_transform':scene.view_settings.view_transform,
                'face_size':face_size, 'fps':30, 'config':cfg, 'frames':rows,
                'optics_check':optics_check,
                'scenario_recipe':json.loads(scene.get('sv_recipe', 'null')),
                'calibration_board':({'inner_corners':list(board_targets.INNER_CORNERS),
                                      'square_size_m':board_targets.SQUARE_SIZE_M}
                                     if calibration_boards else None),
                'mount_offsets':json.loads(scene.get('sv_mount_offsets', '[]')),
                'near_obstacles':json.loads(scene.get('sv_near_obstacles', 'null')),
                'nominal_config':nominal_cfg,
                'depth_truth':({'schema_version':1,
                                'encoding':'OpenEXR float32 camera-Z in metres; 1e10 means no hit',
                                'independent_of_surround_view_renderer':True}
                               if depth_truth else None),
                'limitations':['procedural geometry, no real vehicle CAD',
                               'scripted translation, no vehicle physics',
                               'offline render; no sensor noise, rolling shutter or exposure skew'] +
                              ([] if depth_truth else ['depth/semantic truth not exported']),
                'script_sha256':{name:hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                                 for name in ('scene.py','rig.py','scenario.py','board_targets.py','depth.py',
                                               'diagnostic_motion.py')},
                'sha256':{p.relative_to(output).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in sorted([*output.glob('*.png'), *depth_dir.glob('*.exr')])}}
    (output / 'capture.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(f"SV capture complete: {output}")
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True)
    parser.add_argument('--scenario',type=Path)
    parser.add_argument('--frames',type=int,default=2)
    parser.add_argument('--face-size',type=int,default=256)
    parser.add_argument('--start-frame',type=int,default=0)
    parser.add_argument('--calibration-boards',action='store_true')
    parser.add_argument('--depth-truth',action='store_true',
                        help='export float32 camera-Z OpenEXR faces from Blender Z pass')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    capture(build_scene(load_scenario(args.scenario) if args.scenario else None),args.output,
            args.frames,args.face_size,args.start_frame,args.calibration_boards,args.depth_truth)
