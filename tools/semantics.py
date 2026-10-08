#!/usr/bin/env python3
"""Semantic classes, object-ID encoding, and discrete cube-to-fisheye ray conversion."""
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

from blender.rig import cube_coordinates, configuration
from simulator import camera_rays


# ---------------------------------------------------------------------------
# Semantic Taxonomy & Class Definition
# ---------------------------------------------------------------------------

SEMANTIC_CLASSES = {
    0: {"name": "background", "color": [40, 40, 60], "description": "Sky or infinite background"},
    1: {"name": "ground_drivable", "color": [70, 70, 70], "description": "Asphalt and drivable road surface"},
    2: {"name": "ground_marking", "color": [240, 240, 240], "description": "Road markings, lane dashes, parking bays"},
    3: {"name": "sidewalk_curb", "color": [180, 160, 140], "description": "Pedestrian sidewalk, paving and curbs"},
    4: {"name": "ego_vehicle", "color": [20, 100, 220], "description": "Ego vehicle body, glass, tires, fixtures"},
    5: {"name": "other_vehicle", "color": [220, 50, 40], "description": "Parked or dynamic non-ego vehicles"},
    6: {"name": "vertical_obstacle", "color": [240, 180, 20], "description": "Bollards, buildings, trees, streetlights"},
    7: {"name": "calibration_target", "color": [40, 220, 120], "description": "Checkerboards, ground fiducials, metric markers"},
}

SEMANTIC_NAME_TO_ID = {info["name"]: class_id for class_id, info in SEMANTIC_CLASSES.items()}


def colorize_semantics(label_map):
    """Convert a 2D integer semantic label array to an RGB image array."""
    h, w = label_map.shape
    rgb = np.zeros((h, w, 3), dtype=np.uint8)
    for class_id, info in SEMANTIC_CLASSES.items():
        mask = label_map == class_id
        rgb[mask] = info["color"]
    return rgb


# ---------------------------------------------------------------------------
# Discrete Cube-Face to Fisheye Sampling
# ---------------------------------------------------------------------------

def _sample_discrete_labels(label_grid, uv, invalid_label=0):
    """Nearest-neighbor texel sampling for discrete categorical semantic/object IDs."""
    h, w = label_grid.shape
    x = np.clip(np.rint(uv[..., 0]).astype(int), 0, w - 1)
    y = np.clip(np.rint(uv[..., 1]).astype(int), 0, h - 1)
    sampled = label_grid[y, x]
    return sampled.astype(np.int32)


def convert_camera_semantics(camera, faces, face_size, invalid_label=0):
    """Map cube-face discrete semantic integer grids onto calibrated fisheye rays.

    Uses nearest-neighbor discrete sampling to avoid categorical interpolation.
    Pixels outside the valid FOV are assigned `invalid_label` (0 / background).
    """
    rays, theta = camera_rays(camera)
    result = np.full(theta.shape, invalid_label, dtype=np.int32)
    valid_fov = theta <= camera["projection"]["theta_max_rad"]
    mappings = cube_coordinates(rays, face_size)

    for name, (mask, uv) in mappings.items():
        mask &= valid_fov
        if not np.any(mask):
            continue
        grid = faces.get(name)
        if grid is None or grid.shape != (face_size, face_size):
            raise ValueError(f"missing or incorrectly sized semantic face {name}")
        sampled = _sample_discrete_labels(grid, uv, invalid_label)
        result[mask] = sampled[mask]

    return result


# ---------------------------------------------------------------------------
# Analytic Semantic Oracle for Primitives
# ---------------------------------------------------------------------------

def analytic_primitive_semantic_faces(face_size, objects, default_class=0):
    """Build discrete semantic label grids for all 6 cube faces from analytic primitive oracle."""
    from validate_depth_plane import FACE_DIRECTIONS
    from validate_depth_primitives import ray_sphere_intersect, ray_box_intersect

    coord = (np.arange(face_size, dtype=float) + 0.5) * (2.0 / face_size) - 1.0
    a, b = np.meshgrid(coord, coord)
    faces = {}

    for name, components in FACE_DIRECTIONS.items():
        direction = np.stack(components(a, b), axis=-1)
        direction /= np.linalg.norm(direction, axis=-1, keepdims=True)

        t_min_all = np.full((face_size, face_size), np.inf, dtype=float)
        label_grid = np.full((face_size, face_size), default_class, dtype=np.int32)

        for obj in objects:
            kind = obj["kind"]
            class_id = obj["semantic_id"]
            if kind == "sphere":
                t = ray_sphere_intersect(direction, obj["center"], obj["radius"])
            elif kind == "box":
                t = ray_box_intersect(direction, obj["center"], obj["half_extents"], obj.get("rotation"))
            elif kind == "plane":
                n = np.asarray(obj["normal"], dtype=float)
                n /= np.linalg.norm(n)
                denom = direction @ n
                t = np.divide(obj["offset_m"], denom,
                              out=np.full_like(denom, np.nan),
                              where=denom > 1e-8)
            else:
                raise ValueError(f"unknown kind: {kind}")

            closer = np.isfinite(t) & (t > 0) & (t < t_min_all)
            t_min_all = np.where(closer, t, t_min_all)
            label_grid = np.where(closer, class_id, label_grid)

        faces[name] = label_grid

    return faces


def evaluate_semantic_oracle(face_size, objects, camera=None, default_class=0):
    """Evaluate discrete semantic conversion against analytic ray oracle."""
    from validate_depth_primitives import ray_sphere_intersect, ray_box_intersect

    camera = camera or configuration()["cameras"][0]
    faces = analytic_primitive_semantic_faces(face_size, objects, default_class)
    measured = convert_camera_semantics(camera, faces, face_size, default_class)

    rays, theta = camera_rays(camera)
    fov = theta <= camera["projection"]["theta_max_rad"]

    t_min_all = np.full(theta.shape, np.inf, dtype=float)
    expected = np.full(theta.shape, default_class, dtype=np.int32)

    for obj in objects:
        kind = obj["kind"]
        class_id = obj["semantic_id"]
        if kind == "sphere":
            t = ray_sphere_intersect(rays, obj["center"], obj["radius"])
        elif kind == "box":
            t = ray_box_intersect(rays, obj["center"], obj["half_extents"], obj.get("rotation"))
        elif kind == "plane":
            n = np.asarray(obj["normal"], dtype=float)
            n /= np.linalg.norm(n)
            denom = rays @ n
            t = np.divide(obj["offset_m"], denom,
                          out=np.full_like(denom, np.nan),
                          where=denom > 1e-8)
        closer = np.isfinite(t) & (t > 0) & (t < t_min_all)
        t_min_all = np.where(closer, t, t_min_all)
        expected = np.where(closer, class_id, expected)

    expected = np.where(fov, expected, default_class)
    measured = np.where(fov, measured, default_class)

    matching = (expected == measured) & fov
    total_fov = int(fov.sum())
    matches = int(matching.sum())

    per_class_metrics = {}
    for class_id, info in SEMANTIC_CLASSES.items():
        exp_mask = (expected == class_id) & fov
        meas_mask = (measured == class_id) & fov
        intersection = exp_mask & meas_mask
        union = exp_mask | meas_mask
        exp_count = int(exp_mask.sum())
        meas_count = int(meas_mask.sum())
        iou = float(intersection.sum() / union.sum()) if union.any() else None
        accuracy = float(intersection.sum() / exp_count) if exp_count else None
        if exp_count > 0 or meas_count > 0:
            per_class_metrics[info["name"]] = {
                "class_id": class_id,
                "expected_pixels": exp_count,
                "measured_pixels": meas_count,
                "accuracy": accuracy,
                "iou": iou,
            }

    return {
        "face_size": int(face_size),
        "total_fov_pixels": total_fov,
        "overall_pixel_accuracy": float(matches / max(1, total_fov)),
        "per_class": per_class_metrics,
    }
