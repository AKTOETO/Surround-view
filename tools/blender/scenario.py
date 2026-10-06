"""Seeded scene/mount parameters; independent of Blender and calibration solvers."""
import copy
import json
from pathlib import Path

import numpy as np


def validate(recipe):
    result = copy.deepcopy(recipe)
    if set(result) - {'schema_version', 'seed', 'world', 'mounts'} or result.get('schema_version') != 1:
        raise ValueError('scenario schema_version 1 and known keys required')
    seed = result.get('seed', 0)
    if isinstance(seed, bool) or not isinstance(seed, int) or not 0 <= seed < 2**32:
        raise ValueError('seed must be uint32')
    world = result.setdefault('world', {})
    if set(world) - {'building_height_m', 'building_spacing_m'}:
        raise ValueError('unknown world rule')
    heights = world.setdefault('building_height_m', [5., 9.])
    if len(heights) != 2 or not np.isfinite(heights).all() or not 3 <= heights[0] <= heights[1] <= 20:
        raise ValueError('building heights must satisfy 3 <= low <= high <= 20 m')
    spacing = world.setdefault('building_spacing_m', 9.)
    if not np.isfinite(spacing) or not 8 <= spacing <= 15:
        raise ValueError('building spacing must be 8..15 m')
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


def load(path):
    return validate(json.loads(Path(path).read_text()))


def perturb(nominal, recipe):
    """Yaw in vehicle Z, pitch in optical X; front/rear slide Y, sides slide X."""
    recipe = validate(recipe)
    rng = np.random.default_rng(recipe.get('seed', 0))
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
