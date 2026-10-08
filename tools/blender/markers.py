"""Metric ground markers and raised 3D obstacle targets with known world ground truth."""
import json
import numpy as np


# Ground 2D fiducial markers (Z = 0 on road plane)
GROUND_MARKERS = [
    {"id": "gm_front_center", "world_xyz": [3.5, 0.0, 0.0], "size_m": [0.4, 0.4], "pattern": "crosshair_white_black"},
    {"id": "gm_front_left",   "world_xyz": [3.0, 2.0, 0.0], "size_m": [0.4, 0.4], "pattern": "crosshair_white_black"},
    {"id": "gm_front_right",  "world_xyz": [3.0, -2.0, 0.0], "size_m": [0.4, 0.4], "pattern": "crosshair_white_black"},
    {"id": "gm_rear_center",  "world_xyz": [-3.5, 0.0, 0.0], "size_m": [0.4, 0.4], "pattern": "crosshair_white_black"},
    {"id": "gm_rear_left",    "world_xyz": [-3.0, 2.0, 0.0], "size_m": [0.4, 0.4], "pattern": "crosshair_white_black"},
    {"id": "gm_rear_right",   "world_xyz": [-3.0, -2.0, 0.0], "size_m": [0.4, 0.4], "pattern": "crosshair_white_black"},
    {"id": "gm_left_center",  "world_xyz": [0.0, 2.5, 0.0], "size_m": [0.4, 0.4], "pattern": "crosshair_white_black"},
    {"id": "gm_right_center", "world_xyz": [0.0, -2.5, 0.0], "size_m": [0.4, 0.4], "pattern": "crosshair_white_black"},
]

# Raised 3D metric obstacles with known bounding extents
RAISED_OBSTACLES = [
    {
        "id": "obs_front_bollard_left",
        "kind": "cylinder",
        "center_xyz": [3.2, 2.3, 0.5],
        "radius_m": 0.11,
        "height_m": 1.0,
        "semantic_id": 6,
        "semantic_name": "vertical_obstacle",
    },
    {
        "id": "obs_rear_bollard_right",
        "kind": "cylinder",
        "center_xyz": [-3.8, -2.0, 0.5],
        "radius_m": 0.11,
        "height_m": 1.0,
        "semantic_id": 6,
        "semantic_name": "vertical_obstacle",
    },
    {
        "id": "obs_right_bollard",
        "kind": "cylinder",
        "center_xyz": [5.0, -1.2, 0.5],
        "radius_m": 0.11,
        "height_m": 1.0,
        "semantic_id": 6,
        "semantic_name": "vertical_obstacle",
    },
    {
        "id": "obs_parked_car_red",
        "kind": "box",
        "center_xyz": [6.0, 4.4, 0.75],
        "size_xyz": [4.6, 1.8, 1.5],
        "semantic_id": 5,
        "semantic_name": "other_vehicle",
    },
    {
        "id": "obs_parked_car_gray",
        "kind": "box",
        "center_xyz": [-9.0, -4.4, 0.75],
        "size_xyz": [4.6, 1.8, 1.5],
        "semantic_id": 5,
        "semantic_name": "other_vehicle",
    },
]


def markers_ground_truth(vehicle_pose_matrix=None):
    """Return ground truth positions of all markers in world and vehicle coordinates."""
    v_pose = np.eye(4) if vehicle_pose_matrix is None else np.asarray(vehicle_pose_matrix)
    inv_v = np.linalg.inv(v_pose)

    ground = []
    for m in GROUND_MARKERS:
        pw = np.array([*m["world_xyz"], 1.0])
        pv = inv_v @ pw
        ground.append({
            "id": m["id"],
            "world_xyz_m": m["world_xyz"],
            "vehicle_xyz_m": pv[:3].tolist(),
            "size_m": m["size_m"],
            "pattern": m["pattern"],
        })

    raised = []
    for obs in RAISED_OBSTACLES:
        cw = np.array([*obs["center_xyz"], 1.0])
        cv = inv_v @ cw
        entry = {
            "id": obs["id"],
            "kind": obs["kind"],
            "center_world_xyz_m": obs["center_xyz"],
            "center_vehicle_xyz_m": cv[:3].tolist(),
            "semantic_id": obs["semantic_id"],
            "semantic_name": obs["semantic_name"],
        }
        if "radius_m" in obs:
            entry["radius_m"] = obs["radius_m"]
            entry["height_m"] = obs["height_m"]
        if "size_xyz" in obs:
            entry["size_xyz_m"] = obs["size_xyz"]
        raised.append(entry)

    return {
        "ground_markers": ground,
        "raised_obstacles": raised,
    }
