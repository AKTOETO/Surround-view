"""Coordinate contract shared by Blender capture and the offline converter.

Vehicle: +X forward, +Y left, +Z up. Camera: +X right, +Y down,
+Z forward. Cube faces are expressed in the optical camera coordinates.
"""
import numpy as np


FACES = {
    "px": ((1, 0, 0), (0, -1, 0)),
    "nx": ((-1, 0, 0), (0, -1, 0)),
    "py": ((0, 1, 0), (0, 0, 1)),
    "ny": ((0, -1, 0), (0, 0, -1)),
    "pz": ((0, 0, 1), (0, -1, 0)),
    "nz": ((0, 0, -1), (0, -1, 0)),
}


def face_basis(name):
    forward, up = (np.array(v, dtype=float) for v in FACES[name])
    return np.array([np.cross(forward, up), -up, forward])


def configuration():
    cameras = []
    for i, (name, azimuth, center) in enumerate([
        ("front", 0, (2.36, 0, 0.85)),
        ("right", -np.pi / 2, (0.35, -1.10, 1.12)),
        ("rear", np.pi, (-2.36, 0, 0.85)),
        ("left", np.pi / 2, (0.35, 1.10, 1.12)),
    ]):
        tilt = np.deg2rad(12)
        forward = np.array([np.cos(azimuth) * np.cos(tilt),
                            np.sin(azimuth) * np.cos(tilt), -np.sin(tilt)])
        right = np.cross(forward, [0, 0, 1])
        right /= np.linalg.norm(right)
        rotation = np.array([right, np.cross(forward, right), forward])
        transform = np.eye(4)
        transform[:3, :3] = rotation
        transform[:3, 3] = -rotation @ center
        cameras.append({
            "id": i, "name": name, "calibration_id": f"blender-rig-{name}-v1",
            "resolution": {"width": 400, "height": 400},
            "projection": {"model": "opencv_fisheye", "fx": 128., "fy": 128.,
                           "cx": 199.5, "cy": 199.5, "alpha": 0., "k": [0.] * 4,
                           "theta_max_rad": 1.48, "z_epsilon_m": 1e-6},
            "T_camera_from_vehicle": transform.tolist(),
        })
    return {
        "schema_version": 1, "profile_id": "blender-street-v1",
        "units": {"length": "m", "angle": "rad", "time": "ns"},
        "vehicle": {"length_m": 4.6, "width_m": 1.8, "mask_margin_m": 0.1},
        "cameras": cameras,
        "surface": {"type": "dome_floor_v1", "dome_radius_m": 12.,
                    "dome_latitude_cells": 64, "dome_longitude_cells": 128,
                    "floor_radial_cells": 32},
        "virtual_camera": {"azimuth_rad": 0.8, "elevation_rad": 1.,
                           "distance_m": 8.5, "fov_y_rad": 1., "clip_m": [0.1, 100.]},
        "output": {"width": 960, "height": 540},
        "runtime": {"input_queue_per_camera": 3, "skew_window_ms": 10.,
                    "max_input_age_ms": 100.},
    }


def vehicle_pose(frame, fps=30):
    """Short straight drive at 2 m/s; pose is deterministic, not a physics model."""
    pose = np.eye(4)
    pose[0, 3] = 2. * frame / fps
    return pose


def cube_coordinates(rays, size):
    """Assign unit/nonunit CV rays to dominant-axis faces and pixel centers."""
    rays = np.asarray(rays, dtype=float)
    if size < 2 or not np.all(np.isfinite(rays)) or np.any(np.linalg.norm(rays, axis=-1) == 0):
        raise ValueError("finite nonzero rays and cube size >= 2 required")
    axis = np.argmax(np.abs(rays), axis=-1)
    result = {}
    for name in FACES:
        basis = face_basis(name)
        component = int(np.argmax(np.abs(basis[2])))
        local = rays @ basis.T
        mask = (axis == component) & (local[..., 2] > 0)
        xy = np.divide(local[..., :2], local[..., 2, None],
                       out=np.zeros_like(local[..., :2]), where=local[..., 2, None] != 0)
        # 90-degree perspective: focal length size/2, top-left pixel center (0,0).
        uv = (xy + 1) * size / 2 - 0.5
        result[name] = (mask, uv)
    return result
