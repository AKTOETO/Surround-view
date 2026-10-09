"""Blender ray truth shared by virtual-view and input-camera object-ID exports."""
import numpy as np
from mathutils import Vector


def opaque_hit(scene, dependency, start, direction, distance):
    start, direction = Vector(start), Vector(direction)
    for _ in range(64):
        result = scene.ray_cast(dependency, start, direction, distance=float(distance))
        if not result[0] or not result[4].original.hide_render:
            return result
        advance = (result[1]-start).length + 1e-4
        start += direction*advance
        distance -= advance
        if distance <= 0:
            return (False, None, None, None, None, None)
    raise RuntimeError('too many hidden-helper intersections in truth ray')


def source_object_ids(scene, config, vehicle_pose, object_ids):
    """Exact pixel-center casts for the current equidistant, zero-skew synthetic rig.

    Restricted deliberately: other optical models need an independently validated inverse.
    IDs ignore transparency and pixel filtering; RGB may mix objects at boundaries.
    """
    dependency = __import__('bpy').context.evaluated_depsgraph_get()
    result = []
    for camera in config['cameras']:
        k = camera['projection']
        if any(k['k']) or k.get('alpha', 0) != 0:
            raise ValueError('source ID exporter supports equidistant zero-skew optics only')
        h, w = camera['resolution']['height'], camera['resolution']['width']
        yy, xx = np.mgrid[:h, :w]
        a, b = (xx-k['cx'])/k['fx'], (yy-k['cy'])/k['fy']
        theta = np.hypot(a, b)
        scale = np.divide(np.sin(theta), theta, out=np.ones_like(theta), where=theta > 1e-14)
        rays = np.stack([a*scale, b*scale, np.cos(theta)], axis=-1)
        pose = vehicle_pose @ np.linalg.inv(camera['T_camera_from_vehicle'])
        rays = rays @ pose[:3, :3].T
        ids = np.zeros((h, w), np.uint16)
        for y, x in np.argwhere(theta <= k['theta_max_rad']):
            hit, _, _, _, obj, _ = opaque_hit(scene, dependency, pose[:3, 3], rays[y,x], 200.)
            if hit:
                ids[y,x] = object_ids[obj.original.name]
        result.append(ids)
    return result
