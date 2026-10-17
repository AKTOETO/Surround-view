"""Seeded scene/mount parameters; independent of Blender and calibration solvers."""
import copy
import json
from pathlib import Path

import numpy as np


TRAIN_SEEDS = tuple(range(0, 8))
HOLDOUT_SEEDS = tuple(range(8, 12))


def get_split(seed):
    """Return train or holdout split identifier based on seed."""
    return "train" if seed in TRAIN_SEEDS or seed < 8 else "holdout"


# Standardized scenario definitions (S0–S5) for surround-view calibration & stitching research
SCENARIO_PRESETS = {
    "S0": {
        "id": "S0_METRIC_GRID",
        "description": "Flat metric grid with ground markings, checker pattern and ground fiducials",
        "world": {"building_height_m": [4.0, 6.0], "building_spacing_m": 12.0},
        "mounts": {"yaw_deg": 1.0, "pitch_deg": 1.0, "along_body_m": 0.05},
    },
    "S1": {
        "id": "S1_NEAR_OVERLAP_OBSTACLE",
        "description": "Low bollards and metric blocks located directly inside camera overlap seams",
        "world": {"building_height_m": [5.0, 8.0], "building_spacing_m": 9.0},
        "mounts": {"yaw_deg": 3.0, "pitch_deg": 3.0, "along_body_m": 0.1},
    },
    "S2": {
        "id": "S2_VERTICAL_POLE_SEAM",
        "description": "Tall vertical streetlight and poles intersecting adjacent camera seams",
        "world": {"building_height_m": [6.0, 10.0], "building_spacing_m": 8.5},
        "mounts": {"yaw_deg": 4.0, "pitch_deg": 4.0, "along_body_m": 0.15},
    },
    "S3": {
        "id": "S3_FOREGROUND_OCCLUDER",
        "description": "Foreground parked vehicle and occluders with background walls at multiple depths",
        "world": {"building_height_m": [7.0, 12.0], "building_spacing_m": 8.0},
        "mounts": {"yaw_deg": 5.0, "pitch_deg": 5.0, "along_body_m": 0.2},
    },
    "S4": {
        "id": "S4_PHOTOMETRIC_STRESS",
        "description": "Illumination stress, dynamic shadows and per-camera mount bias",
        "world": {"building_height_m": [5.0, 9.0], "building_spacing_m": 9.0},
        "mounts": {"yaw_deg": 6.0, "pitch_deg": 6.0, "along_body_m": 0.25},
    },
    "S5": {
        "id": "S5_DYNAMIC_TRAJECTORY",
        "description": "Moving ego vehicle passing across crosswalks, traffic and obstacles",
        "world": {"building_height_m": [5.0, 9.0], "building_spacing_m": 9.0},
        "mounts": {"yaw_deg": 4.0, "pitch_deg": 4.0, "along_body_m": 0.15},
    },
}


def scenario_preset(preset_key, seed=0):
    """Build a complete scenario recipe from a preset key (S0..S5) and seed."""
    preset = SCENARIO_PRESETS.get(preset_key)
    if not preset:
        raise ValueError(f"unknown preset {preset_key}; choices: {list(SCENARIO_PRESETS)}")
    recipe = {
        "schema_version": 1,
        "preset_id": preset["id"],
        "seed": int(seed),
        "split": get_split(seed),
        "world": copy.deepcopy(preset["world"]),
        "mounts": copy.deepcopy(preset["mounts"]),
    }
    return validate(recipe)


def validate(recipe):
    result = copy.deepcopy(recipe)
    allowed_keys = {'schema_version', 'seed', 'world', 'mounts', 'preset_id', 'split', 'scene_seed', 'mount_seed'}
    if (set(result) - allowed_keys or type(result.get('schema_version')) is not int
            or result.get('schema_version') != 1):
        raise ValueError('scenario schema_version 1 and known keys required')
    seed = result.get('seed', 0)
    if isinstance(seed, bool) or not isinstance(seed, int) or not 0 <= seed < 2**32:
        raise ValueError('seed must be uint32')
    for key in ('scene_seed', 'mount_seed'):
        value = result.get(key, seed)
        if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value < 2**32:
            raise ValueError(key + ' must be uint32')
    split = result.get('split')
    if split is not None and split not in ('train', 'holdout', 'validation'):
        raise ValueError('split must be train, holdout or validation')
    world = result.setdefault('world', {})
    if set(world) - {'building_height_m', 'building_spacing_m', 'near_obstacle_jitter_m', 'family'}:
        raise ValueError('unknown world rule')
    if world.get('family', 'street') not in ('street', 'cargo_yard'):
        raise ValueError('world.family must be street or cargo_yard')
    heights = world.setdefault('building_height_m', [5., 9.])
    if len(heights) != 2 or not np.isfinite(heights).all() or not 3 <= heights[0] <= heights[1] <= 20:
        raise ValueError('building heights must satisfy 3 <= low <= high <= 20 m')
    spacing = world.setdefault('building_spacing_m', 9.)
    if not np.isfinite(spacing) or not 8 <= spacing <= 15:
        raise ValueError('building spacing must be 8..15 m')
    jitter = world.setdefault('near_obstacle_jitter_m', 0.)
    if not np.isfinite(jitter) or not 0 <= jitter <= .8:
        raise ValueError('near obstacle jitter must be 0..0.8 m')
    mounts = result.setdefault('mounts', {})
    if set(mounts) - {'yaw_deg', 'pitch_deg', 'along_body_m', 'overrides'}:
        raise ValueError('unknown mount rule')
    for key, maximum in [('yaw_deg', 20), ('pitch_deg', 20), ('along_body_m', .4)]:
        limit = mounts.setdefault(key, 0.)
        if not np.isfinite(limit) or not 0 <= limit <= maximum:
            raise ValueError('invalid mount bound: '+key)
    overrides = mounts.setdefault('overrides', {})
    if set(overrides) - {'0', '1', '2', '3'}:
        raise ValueError('override camera IDs must be 0..3')
    for values in overrides.values():
        if set(values) - {'yaw_deg', 'pitch_deg', 'along_body_m'}:
            raise ValueError('unknown camera override')
        for key, value in values.items():
            maximum = .4 if key == 'along_body_m' else 20
            if not np.isfinite(value) or abs(value) > maximum:
                raise ValueError('invalid override: '+key)
    return result


def near_obstacle_positions(recipe=None):
    """Separate deterministic random stream: changing building counts cannot move poles."""
    positions = np.array([[3.2, 2.3], [-3.8, -2.], [5., -1.2]])
    if recipe is None:
        return positions.tolist()
    recipe = validate(recipe)
    jitter = recipe['world']['near_obstacle_jitter_m']
    rng = np.random.default_rng(np.random.SeedSequence([recipe.get('scene_seed', recipe.get('seed', 0)), 1701]))
    return (positions+rng.uniform(-jitter, jitter, positions.shape)).tolist()


def load(path):
    return validate(json.loads(Path(path).read_text()))


def perturb(nominal, recipe):
    """Yaw in vehicle Z, pitch in optical X; front/rear slide Y, sides slide X."""
    recipe = validate(recipe)
    rng = np.random.default_rng(recipe.get('mount_seed', recipe.get('seed', 0)))
    result, sampled = copy.deepcopy(nominal), []
    for camera in result['cameras']:
        values = {key: float(rng.uniform(-recipe['mounts'][key], recipe['mounts'][key]))
                  for key in ('yaw_deg', 'pitch_deg', 'along_body_m')}
        values.update(recipe['mounts']['overrides'].get(str(camera['id']), {}))
        yaw, pitch = np.deg2rad([values['yaw_deg'], values['pitch_deg']])
        rz = np.array([[np.cos(yaw), -np.sin(yaw), 0], [np.sin(yaw), np.cos(yaw), 0], [0, 0, 1]])
        rx = np.array([[1, 0, 0], [0, np.cos(pitch), -np.sin(pitch)], [0, np.sin(pitch), np.cos(pitch)]])
        T = np.asarray(camera['T_camera_from_vehicle'])
        center = -T[:3, :3].T @ T[:3, 3]
        center[1 if camera['id'] in (0, 2) else 0] += values['along_body_m']
        rotation = (rz @ T[:3, :3].T @ rx).T
        T[:3, :3], T[:3, 3] = rotation, -rotation @ center
        camera['T_camera_from_vehicle'] = T.tolist()
        sampled.append(dict(camera_id=camera['id'], **values, center_vehicle_m=center.tolist()))
    return result, sampled


def validate_family_plan(plan):
    """Pure preflight for the bounded Blender fixture export queue (not native trials)."""
    import re
    result = copy.deepcopy(plan)
    if (not isinstance(result, dict) or set(result) - {'schema_version', 'purpose', 'capture', 'cases'}
            or type(result.get('schema_version')) is not int or result['schema_version'] != 1
            or not isinstance(result.get('cases'), list) or not 1 <= len(result['cases']) <= 16):
        raise ValueError('family plan schema1 with 1..16 cases required')
    if 'purpose' in result and (not isinstance(result['purpose'], str) or not 1 <= len(result['purpose']) <= 1024):
        raise ValueError('bounded purpose string required')
    bounds = {'frames': (2, 8), 'face_size': (32, 512), 'frame_step': (1, 30),
              'width': (16, 640), 'height': (16, 360)}
    options = result.get('capture')
    if not isinstance(options, dict) or set(options) != set(bounds):
        raise ValueError('known complete capture settings required')
    for key, (low, high) in bounds.items():
        value = options[key]
        if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
            raise ValueError('bounded integer capture setting required: ' + key)
    identifiers = set()
    for item in result['cases']:
        if (not isinstance(item, dict) or set(item) != {'id', 'recipe'}
                or not isinstance(item['id'], str) or not re.fullmatch(r'[a-zA-Z0-9_-]{1,64}', item['id'])):
            raise ValueError('safe unique case ID and recipe required')
        if item['id'] in identifiers:
            raise ValueError('duplicate family case ID')
        identifiers.add(item['id'])
        item['recipe'] = validate(item['recipe'])
    return result
