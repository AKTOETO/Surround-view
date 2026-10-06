"""Create measured chessboard targets for controlled Blender camera captures."""
import math

import numpy as np
import bpy
from mathutils import Matrix


INNER_CORNERS = (9, 6)
SQUARE_SIZE_M = 0.20


def _board_mesh(name):
    columns, rows = INNER_CORNERS[0] + 1, INNER_CORNERS[1] + 1
    vertices, faces, material_ids = [], [], []
    width, height = columns * SQUARE_SIZE_M, rows * SQUARE_SIZE_M
    # OpenCV's chessboard detector needs a contrasting white quiet zone around the grid.
    vertices.extend(((-SQUARE_SIZE_M/2, -SQUARE_SIZE_M/2, -.01),
                     (width+SQUARE_SIZE_M/2, -SQUARE_SIZE_M/2, -.01),
                     (width+SQUARE_SIZE_M/2, height+SQUARE_SIZE_M/2, -.01),
                     (-SQUARE_SIZE_M/2, height+SQUARE_SIZE_M/2, -.01)))
    faces.append((0, 1, 2, 3))
    material_ids.append(1)
    for y in range(rows):
        for x in range(columns):
            start = len(vertices)
            vertices.extend(((x*SQUARE_SIZE_M, y*SQUARE_SIZE_M, 0),
                             ((x+1)*SQUARE_SIZE_M, y*SQUARE_SIZE_M, 0),
                             ((x+1)*SQUARE_SIZE_M, (y+1)*SQUARE_SIZE_M, 0),
                             (x*SQUARE_SIZE_M, (y+1)*SQUARE_SIZE_M, 0)))
            faces.append((start, start+1, start+2, start+3))
            material_ids.append((x+y) % 2)
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    dark = bpy.data.materials.new(name + " black")
    dark.use_nodes = True
    next(node for node in dark.node_tree.nodes if node.type == "BSDF_PRINCIPLED").inputs[
        "Base Color"].default_value = (.012, .012, .012, 1)
    dark.diffuse_color = (.012, .012, .012, 1)
    light = bpy.data.materials.new(name + " white")
    light.use_nodes = True
    next(node for node in light.node_tree.nodes if node.type == "BSDF_PRINCIPLED").inputs[
        "Base Color"].default_value = (.95, .95, .95, 1)
    light.diffuse_color = (.95, .95, .95, 1)
    mesh.materials.append(dark)
    mesh.materials.append(light)
    for polygon, material_id in zip(mesh.polygons, material_ids):
        polygon.material_index = material_id
    return mesh


def create_targets(scene):
    mesh = _board_mesh("SV metric calibration board")
    targets = []
    for camera_id in range(4):
        obj = bpy.data.objects.new(f"SV calibration target {camera_id}", mesh)
        scene.collection.objects.link(obj)
        obj.hide_render = True
        targets.append(obj)
    return targets


def pose(camera, frame, vehicle_pose):
    """Return the lower-left internal-corner pose of a tilted board in vehicle space."""
    T_camera_vehicle = np.asarray(camera['T_camera_from_vehicle'])
    T_vehicle_camera = np.linalg.inv(T_camera_vehicle)
    camera_center = T_vehicle_camera[:3, 3]
    rotation = T_vehicle_camera[:3, :3]
    right, up, forward = rotation[:, 0], -rotation[:, 1], rotation[:, 2]
    normal = np.cross(right, up)
    base = np.column_stack((right, up, normal))
    phase = float(frame)
    rx = math.radians(5 * math.sin(phase * .8))
    ry = math.radians(7 * math.cos(phase * .6))
    local_x = np.array([[1, 0, 0], [0, math.cos(rx), -math.sin(rx)],
                        [0, math.sin(rx), math.cos(rx)]])
    local_y = np.array([[math.cos(ry), 0, math.sin(ry)], [0, 1, 0],
                        [-math.sin(ry), 0, math.cos(ry)]])
    board_rotation = base @ local_y @ local_x
    center = (camera_center + forward * 2.7 + right * (.12 * math.sin(phase + camera['id']))
              + np.array([0, 0, 1.0 + .12 * math.cos(phase * .7 + camera['id'])]))
    width, height = (INNER_CORNERS[0] + 1) * SQUARE_SIZE_M, (INNER_CORNERS[1] + 1) * SQUARE_SIZE_M
    mesh_origin = center - board_rotation @ np.array([width / 2, height / 2, 0])
    # Calibration object coordinates start at the first internal corner, one cell in.
    origin = mesh_origin + board_rotation @ np.array([SQUARE_SIZE_M, SQUARE_SIZE_M, 0])
    result = np.eye(4)
    result[:3, :3], result[:3, 3] = board_rotation, origin
    # World scene is static while the ego rig moves; metadata uses vehicle coordinates.
    return np.linalg.inv(vehicle_pose) @ result


def set_target(targets, camera_id, vehicle_pose, board_pose):
    for index, target in enumerate(targets):
        target.hide_render = index != camera_id
    target = targets[camera_id]
    mesh_from_board = np.eye(4)
    mesh_from_board[:3, 3] = [-SQUARE_SIZE_M, -SQUARE_SIZE_M, 0]
    target.matrix_world = Matrix((vehicle_pose @ board_pose @ mesh_from_board).tolist())
    bpy.context.view_layer.update()
