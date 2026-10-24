"""Explicit pose handoff for E1->Blender fixtures; no sampling or calibration solver."""
import copy
import re

import numpy as np


def validate_camera_rig(bundle):
    """Only extrinsic matrices may differ; preserve every supplied scalar exactly."""
    if not isinstance(bundle, dict) or set(bundle) != {'nominal', 'actual', 'provenance'}:
        raise ValueError('camera rig requires nominal/actual/provenance')
    result = copy.deepcopy(bundle)
    provenance = result['provenance']
    if (not isinstance(provenance, dict) or set(provenance) !=
            {'policy', 'mount_seed', 'nominal_config_sha256', 'e1_truth_sha256', 'e1_report_sha256'} or
            provenance.get('policy') != 'thesis_e1_matrix_bridge_v1' or
            type(provenance.get('mount_seed')) is not int or not 0 <= provenance['mount_seed'] < 2**32):
        raise ValueError('invalid camera rig provenance')
    for key in ('nominal_config_sha256', 'e1_truth_sha256', 'e1_report_sha256'):
        if not isinstance(provenance[key], str) or not re.fullmatch('[0-9a-f]{64}', provenance[key]):
            raise ValueError('camera rig requires SHA256 identities')
    stripped = []
    for key in ('nominal', 'actual'):
        config = result[key]
        if not isinstance(config, dict) or not isinstance(config.get('cameras'), list) or len(config['cameras']) != 4:
            raise ValueError('camera rig requires four ordered cameras')
        config_without_poses = copy.deepcopy(config)
        for index, (camera, stripped_camera) in enumerate(zip(config['cameras'], config_without_poses['cameras'])):
            if type(camera.get('id')) is not int or camera['id'] != index:
                raise ValueError('camera rig IDs must be ordered 0..3')
            values = camera.get('T_camera_from_vehicle')
            if (not isinstance(values, list) or len(values) != 4 or
                    any(not isinstance(row, list) or len(row) != 4 for row in values) or
                    any(type(v) not in (int, float) for row in values for v in row)):
                raise ValueError('camera rig requires numeric 4x4 transforms')
            matrix = np.asarray(values, dtype=float)
            if (not np.isfinite(matrix).all() or not np.array_equal(matrix[3], [0, 0, 0, 1]) or
                    not np.allclose(matrix[:3, :3] @ matrix[:3, :3].T, np.eye(3), atol=1e-10, rtol=0) or
                    abs(np.linalg.det(matrix[:3, :3]) - 1) > 1e-10):
                raise ValueError('camera rig requires finite rigid right-handed transforms')
            del stripped_camera['T_camera_from_vehicle']
        stripped.append(config_without_poses)
    if stripped[0] != stripped[1]:
        raise ValueError('nominal/actual rig may differ only in extrinsic transforms')
    return result


def check_rig_recipe(bundle, recipe):
    if recipe is None:
        return
    mounts = recipe.get('mounts', {})
    if any(mounts.get(key, 0) != 0 for key in ('yaw_deg', 'pitch_deg', 'along_body_m')) or mounts.get('overrides'):
        raise ValueError('explicit camera rig forbids second mount perturbation')
    if recipe.get('mount_seed', bundle['provenance']['mount_seed']) != bundle['provenance']['mount_seed']:
        raise ValueError('recipe mount_seed differs from explicit camera rig')
