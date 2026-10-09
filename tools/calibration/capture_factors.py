"""Paired board distance/tilt factors with identical peripheral center directions."""
import numpy as np
from scipy.spatial.transform import Rotation

from calibration.coverage import board_points, peripheral_poses
from calibration.optical_models import project
from calibration.raster_board import K

PROFILES = {
    'small_front': {'distance_m': 3.2, 'tilt_rad': 0.},
    'small_tilt': {'distance_m': 3.2, 'tilt_rad': .35},
    'large_front': {'distance_m': 2., 'tilt_rad': 0.},
    'large_tilt': {'distance_m': 2., 'tilt_rad': .35},
}


def factor_poses(seed, distance_m, tilt_rad):
    if not np.isfinite(distance_m) or distance_m <= 0 or not np.isfinite(tilt_rad) or abs(tilt_rad) > .5:
        raise ValueError('positive distance and finite local tilt within +/-0.5 rad required')
    base = peripheral_poses(seed, angles=(.65,) * 4 + (.8,) * 4)
    result = []
    for i, (r, t) in enumerate(base):
        tilt = Rotation.from_euler('xy', [tilt_rad * (-1)**i, tilt_rad * (-1)**(i//2)]).as_matrix()
        result.append((r @ tilt, t / np.linalg.norm(t) * distance_m))
    return result


def diagnostics(geometry, family):
    cell_edges, tilts = [], []
    for r, t in geometry:
        uv = project(board_points(r, t), K, family).reshape(6, 9, 2)
        cell_edges.extend(np.linalg.norm(np.diff(uv, axis=0), axis=-1).ravel())
        cell_edges.extend(np.linalg.norm(np.diff(uv, axis=1), axis=-1).ravel())
        tilts.append(np.arccos(np.clip(r[:, 2] @ (t / np.linalg.norm(t)), -1, 1)))
    return {'cell_edge_px': {'min': float(np.min(cell_edges)), 'median': float(np.median(cell_edges)),
                             'max': float(np.max(cell_edges))},
            'normal_to_center_ray_rad': {'min': float(np.min(tilts)), 'max': float(np.max(tilts))}}
