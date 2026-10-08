"""Vehicle body model and self-occlusion mask calculation.

Computes exact 3D ray-body intersections to determine if camera rays or carrier points
are occluded by the ego vehicle's own chassis, cabin, or side mirrors.
"""
import numpy as np


VEHICLE_DIMENSIONS = {
    "length_m": 4.6,
    "width_m": 1.8,
    "height_m": 1.5,
    "wheelbase_m": 2.7,
    "front_overhang_m": 0.95,
    "rear_overhang_m": 0.95,
    "ground_clearance_m": 0.15,
    "roof_start_x_m": -0.8,
    "roof_end_x_m": 1.2,
    "roof_height_m": 1.45,
    "mirror_extent_y_m": 1.08,
}


def vehicle_body_boxes():
    """Return oriented bounding boxes approximating the vehicle geometry in vehicle coordinates.
    
    Vehicle frame: X forward, Y left, Z up. Center (0,0,0) is center of vehicle base at ground level.
    """
    dim = VEHICLE_DIMENSIONS
    l_half = dim["length_m"] / 2.0
    w_half = dim["width_m"] / 2.0
    h_body = dim["height_m"]
    z_min = dim["ground_clearance_m"]
    
    boxes = [
        # Main chassis body (lower block)
        {
            "name": "chassis",
            "center": [0.0, 0.0, (h_body * 0.55 + z_min) / 2.0],
            "half_extents": [l_half, w_half, (h_body * 0.55 - z_min) / 2.0],
        },
        # Cabin / Greenhouse (upper block)
        {
            "name": "cabin",
            "center": [(dim["roof_start_x_m"] + dim["roof_end_x_m"]) / 2.0, 0.0, (dim["roof_height_m"] + h_body * 0.55) / 2.0],
            "half_extents": [
                (dim["roof_end_x_m"] - dim["roof_start_x_m"]) / 2.0,
                w_half * 0.85,
                (dim["roof_height_m"] - h_body * 0.55) / 2.0,
            ],
        },
        # Left mirror
        {
            "name": "mirror_left",
            "center": [0.35, dim["mirror_extent_y_m"] - 0.05, 1.0],
            "half_extents": [0.10, 0.08, 0.06],
        },
        # Right mirror
        {
            "name": "mirror_right",
            "center": [0.35, -dim["mirror_extent_y_m"] + 0.05, 1.0],
            "half_extents": [0.10, 0.08, 0.06],
        },
    ]
    return boxes


def ray_box_test(origins, directions, center, half_extents):
    """Test if rays intersect an axis-aligned bounding box.
    
    Args:
        origins: (..., 3) ray origins.
        directions: (..., 3) normalized ray directions.
        center: (3,) box center.
        half_extents: (3,) box half extents.
        
    Returns:
        hit: (...) boolean mask.
        t_near: (...) distance to entry.
    """
    c = np.asarray(center, dtype=float)
    h = np.asarray(half_extents, dtype=float)
    
    b_min = c - h
    b_max = c + h
    
    with np.errstate(divide="ignore", invalid="ignore"):
        inv_d = np.where(np.abs(directions) > 1e-12, 1.0 / directions, np.inf)
        t1 = (b_min - origins) * inv_d
        t2 = (b_max - origins) * inv_d
        
        t_min = np.minimum(t1, t2)
        t_max = np.maximum(t1, t2)
        
        t_enter = np.max(t_min, axis=-1)
        t_exit = np.min(t_max, axis=-1)
        
        hit = (t_enter <= t_exit) & (t_exit > 1e-4)
    return hit, t_enter


def compute_camera_self_occlusion(camera, points):
    """Check if the line of sight from camera to carrier points is occluded by the ego vehicle body.
    
    Args:
        camera: camera dict with T_camera_from_vehicle.
        points: (..., 3) 3D carrier points in vehicle coordinates.
        
    Returns:
        visible: (...) boolean mask (True if line of sight is NOT occluded by body).
    """
    T = np.asarray(camera["T_camera_from_vehicle"], dtype=float)
    # Camera center in vehicle coordinates: -R^T * t
    cam_pos = -T[:3, :3].T @ T[:3, 3]
    
    # Ray from camera optical center to carrier points
    diff = points - cam_pos
    dist = np.linalg.norm(diff, axis=-1)
    dirs = np.divide(diff, dist[..., None], out=np.zeros_like(diff), where=dist[..., None] > 1e-12)
    
    boxes = vehicle_body_boxes()
    occluded = np.zeros(points.shape[:-1], dtype=bool)
    
    for box in boxes:
        # Avoid self-intersecting the mirror on which the camera might be mounted
        hit, t_enter = ray_box_test(cam_pos, dirs, box["center"], box["half_extents"])
        # Occlusion occurs if intersection is strictly in front of camera and before the carrier point
        is_occluding = hit & (t_enter > 0.05) & (t_enter < dist - 0.02)
        occluded |= is_occluding
        
    return ~occluded


def compute_vehicle_footprint_mask(points, margin_m=0.1):
    """Compute boolean mask of points inside the 2D vehicle ground footprint.
    
    Args:
        points: (..., 3) points in vehicle coordinates.
        margin_m: safety margin around vehicle body.
        
    Returns:
        mask: (...) True inside vehicle footprint.
    """
    dim = VEHICLE_DIMENSIONS
    l_half = dim["length_m"] / 2.0 + margin_m
    w_half = dim["width_m"] / 2.0 + margin_m
    
    inside_xy = (np.abs(points[..., 0]) <= l_half) & (np.abs(points[..., 1]) <= w_half)
    return inside_xy & (points[..., 2] <= dim["height_m"] + margin_m)
