"""Independent source-camera visibility for a directly ray-traced scene point.

Pure NumPy projection contract; ray_cast is supplied by the Blender caller.
Bit i denotes visibility in camera i. No projection carrier/fusion weights are used.
"""
import numpy as np


def projects_inside(camera, point_vehicle):
    transform = np.asarray(camera['T_camera_from_vehicle'], float)
    point = transform[:3, :3] @ point_vehicle + transform[:3, 3]
    if not np.isfinite(point).all():
        return False
    projection = camera['projection']
    rho = np.hypot(point[0], point[1])
    theta = np.arctan2(rho, point[2])
    if point[2] <= projection['z_epsilon_m'] or theta > projection['theta_max_rad']:
        return False
    distorted = theta + sum(k * theta**(2*i+3) for i, k in enumerate(projection['k']))
    scale = distorted/rho if rho > 1e-14 else 0.
    # OpenCV fisheye alpha couples normalized horizontal/vertical coordinates.
    u = projection['fx'] * scale * (point[0] + projection.get('alpha', 0.)*point[1]) + projection['cx']
    v = projection['fy'] * scale * point[1] + projection['cy']
    return 0 <= u <= camera['resolution']['width']-1 and 0 <= v <= camera['resolution']['height']-1


def visible_camera_bits(point_world, vehicle_pose, cameras, cast, tolerance_m=.02):
    """cast(origin, unit_direction, max_distance) returns first hit point or None.

    A view is accepted only if its nearest geometric hit agrees with the target
    within tolerance. This models opaque geometry, not transparency or pixel filters.
    """
    if tolerance_m <= 0 or not np.isfinite(tolerance_m):
        raise ValueError('positive finite ray-hit tolerance required')
    ids = [camera['id'] for camera in cameras]
    if sorted(ids) != list(range(4)):
        raise ValueError('four unique camera IDs 0..3 required')
    pose = np.asarray(vehicle_pose, float)
    point_world = np.asarray(point_world, float)
    local = (np.linalg.inv(pose) @ np.append(point_world, 1))[:3]
    result = 0
    for camera in cameras:
        if not projects_inside(camera, local):
            continue
        camera_pose = pose @ np.linalg.inv(camera['T_camera_from_vehicle'])
        origin = camera_pose[:3, 3]
        direction = point_world - origin
        distance = np.linalg.norm(direction)
        if distance <= tolerance_m:
            continue
        hit = cast(origin, direction/distance, distance+tolerance_m)
        if hit is not None and np.linalg.norm(np.asarray(hit)-point_world) <= tolerance_m:
            result |= 1 << camera['id']
    return result
