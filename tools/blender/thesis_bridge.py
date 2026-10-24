"""Prepare immutable E3 fixture inputs from completed native E1 results. No rendering/fitting."""
import copy
import hashlib
import json
import math
from pathlib import Path
import re

from camera_rig import validate_camera_rig
from scenario import validate_family_plan


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def export_e3(plan_path, truth_path, report_path, output):
    plan_path, truth_path, report_path, output = map(Path, (plan_path, truth_path, report_path, output))
    plan = json.loads(plan_path.read_text())
    if (set(plan) != {'schema_version', 'purpose', 'rig_config', 'e1_input_plan_sha256',
                     'capture', 'scenes', 'views', 'estimated_profile', 'mask_policy'} or
            type(plan['schema_version']) is not int or plan['schema_version'] != 1 or
            plan['estimated_profile'] != 'gaussian/ransac_epnp_lm' or
            plan['mask_policy'] != 'non_ego_any_camera_visible_eroded_2px' or
            len(plan['scenes']) != 3 or len(plan['views']) != 2):
        raise ValueError('bounded thesis E3 plan schema required')
    base_path = (plan_path.parent / plan['rig_config']).resolve()
    if not base_path.is_relative_to(plan_path.parent.resolve()):
        raise ValueError('rig config must be inside plan directory')
    base = json.loads(base_path.read_text())
    truth = json.loads(truth_path.read_text())
    report = json.loads(report_path.read_text())
    if (truth['config_sha256'] != sha(base_path) or
            truth['input_plan_sha256'] != plan['e1_input_plan_sha256'] or
            report['input_sha256'] != sha(truth_path.parent / 'cases.json') or
            not report['success'] or report['planned'] != 48 or len(report['results']) != 48):
        raise ValueError('E1 source identities do not match frozen E3 plan')
    results = {r['case_id']: r for r in report['results']}
    if len(results) != 48:
        raise ValueError('duplicate E1 result case_id')
    records = {(r['mount_seed'], r['camera']['id']): r for r in truth['cameras']}
    if len(truth['cameras']) != 12 or len(records) != 12 or set(records) != {(seed, camera) for seed in (1001, 1002, 1003) for camera in range(4)}:
        raise ValueError('E3 requires the three registered E1 rigs')
    profiles = {}
    for seed in (1001, 1002, 1003):
        actual, estimated = copy.deepcopy(base), copy.deepcopy(base)
        for camera in range(4):
            record = records[(seed, camera)]
            if record['camera'] != base['cameras'][camera]:
                raise ValueError('E1 nominal camera differs from frozen rig')
            selected = results[f'mount{seed}/cam{camera}/gaussian/ransac_epnp_lm']
            status = selected.get('status', {})
            if (selected.get('outcome') != 'completed' or status.get('camera_id') != camera or
                    status.get('execution_policy', {}).get('method') != 'ransac_epnp_lm'):
                raise ValueError('required estimated E1 candidate is unavailable')
            actual['cameras'][camera]['T_camera_from_vehicle'] = copy.deepcopy(record['true_transform'])
            estimated['cameras'][camera]['T_camera_from_vehicle'] = copy.deepcopy(status['T_camera_from_vehicle'])
        provenance = dict(policy='thesis_e1_matrix_bridge_v1', mount_seed=seed,
                          nominal_config_sha256=sha(base_path), e1_truth_sha256=sha(truth_path),
                          e1_report_sha256=sha(report_path))
        validate_camera_rig(dict(nominal=base, actual=actual, provenance=provenance))
        validate_camera_rig(dict(nominal=base, actual=estimated, provenance=provenance))
        # Validate candidate matrices with original IDs above; export their server-issued identities below.
        for camera in range(4):
            estimated['cameras'][camera]['calibration_id'] = results[
                f'mount{seed}/cam{camera}/gaussian/ransac_epnp_lm']['status']['calibration_id']
        profiles[seed] = dict(nominal=copy.deepcopy(base), true=actual, estimated=estimated)
    files, captures, case_ids = {}, {}, set()
    for view in plan['views']:
        if (set(view) != {'id', 'virtual_camera'} or not isinstance(view['id'], str) or
                not re.fullmatch('[a-zA-Z0-9_-]{1,32}', view['id']) or view['id'] in captures):
            raise ValueError('safe unique E3 view IDs required')
        settings = view['virtual_camera']
        if (not isinstance(settings, dict) or set(settings) !=
                {'azimuth_rad', 'elevation_rad', 'distance_m', 'fov_y_rad', 'clip_m'}):
            raise ValueError('E3 startup view requires config fields, not FRAME target_m')
        scalars = [settings[k] for k in ('azimuth_rad', 'elevation_rad', 'distance_m', 'fov_y_rad')]
        clips = settings['clip_m']
        if (not isinstance(clips, list) or len(clips) != 2 or
                any(type(v) not in (int, float) or not math.isfinite(v) for v in scalars + clips) or
                not .35 <= settings['elevation_rad'] <= math.pi/2 or
                not 6 <= settings['distance_m'] <= 18 or not 0 < settings['fov_y_rad'] < math.pi or
                not 0 < clips[0] < clips[1]):
            raise ValueError('invalid E3 startup view values')
        for seed, states in profiles.items():
            for state, config in states.items():
                config = copy.deepcopy(config)
                config['virtual_camera'] = copy.deepcopy(view['virtual_camera'])
                files[f'profiles/{view["id"]}/mount{seed}/{state}.json'] = config
        capture = dict(schema_version=1, purpose=plan['purpose'], capture=copy.deepcopy(plan['capture']), cases=[])
        for scene in plan['scenes']:
            if (set(scene) != {'id', 'scene_seed', 'world'} or not isinstance(scene['id'], str) or
                    not re.fullmatch('[a-zA-Z0-9_-]{1,32}', scene['id'])):
                raise ValueError('safe E3 scene specification required')
            for seed in profiles:
                identifier = f'{scene["id"]}-m{seed}'
                if (view['id'], identifier) in case_ids:
                    raise ValueError('duplicate E3 scene IDs')
                case_ids.add((view['id'], identifier))
                nominal = files[f'profiles/{view["id"]}/mount{seed}/nominal.json']
                actual = files[f'profiles/{view["id"]}/mount{seed}/true.json']
                nominal_bytes = (json.dumps(nominal, indent=2) + '\n').encode()
                provenance = dict(policy='thesis_e1_matrix_bridge_v1', mount_seed=seed,
                                  nominal_config_sha256=hashlib.sha256(nominal_bytes).hexdigest(),
                                  e1_truth_sha256=sha(truth_path), e1_report_sha256=sha(report_path))
                capture['cases'].append(dict(id=identifier,
                    recipe=dict(schema_version=1, seed=0, scene_seed=scene['scene_seed'],
                                mount_seed=seed, split='holdout', world=copy.deepcopy(scene['world'])),
                    camera_rig=dict(nominal=copy.deepcopy(nominal), actual=copy.deepcopy(actual), provenance=provenance)))
        captures[view['id']] = validate_family_plan(capture)
    # Validate everything before creating output; a prior directory is never overwritten.
    output.mkdir(parents=True, exist_ok=False)
    registry = dict(schema_version=1, state='prepared_no_render', independent_rgb_available=False,
                    native_e3_trials_completed=False, mask_policy=plan['mask_policy'], source_plan_sha256=sha(plan_path),
                    e1_truth_sha256=sha(truth_path), e1_report_sha256=sha(report_path), files={})
    for name, value in {**files, **{f'capture-{key}.json': value for key, value in captures.items()}}.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, indent=2) + '\n')
        registry['files'][name] = sha(path)
    (output / 'registry.json').write_text(json.dumps(registry, indent=2) + '\n')
    return registry


def audit_e3(prepared, truth_path, report_path, project_binary, output_audit):
    """Offline matrix/native projection checks. Does not render or evaluate final RGB."""
    import subprocess
    import numpy as np
    from scenario import perturb
    prepared, truth_path, report_path, output_audit = map(Path, (prepared, truth_path, report_path, output_audit))
    truth, report = json.loads(truth_path.read_text()), json.loads(report_path.read_text())
    registry = json.loads((prepared / 'registry.json').read_text())
    if registry['e1_truth_sha256'] != sha(truth_path) or registry['e1_report_sha256'] != sha(report_path):
        raise ValueError('prepared registry source mismatch')
    if not all(sha(prepared/name) == digest for name, digest in registry['files'].items()):
        raise ValueError('prepared file checksum mismatch')
    records = {(x['mount_seed'], x['camera']['id']): x for x in truth['cameras']}
    results = {x['case_id']: x for x in report['results']}
    paths = sorted(prepared.glob('profiles/*/mount*/*.json'))
    if len(paths) != 18:
        raise ValueError('E3 preflight requires eighteen startup profiles')
    rmse_checks, models = 0, {}
    for path in paths:
        config = json.loads(path.read_text())
        seed, state = int(path.parent.name[5:]), path.stem
        if seed not in (1001, 1002, 1003) or state not in ('nominal', 'true', 'estimated'):
            raise ValueError('unexpected E3 startup profile')
        for camera in range(4):
            item = config['cameras'][camera]
            source = records[(seed, camera)]
            expected = (source['camera']['T_camera_from_vehicle'] if state == 'nominal' else
                        source['true_transform'] if state == 'true' else
                        results[f'mount{seed}/cam{camera}/gaussian/ransac_epnp_lm']['status']['T_camera_from_vehicle'])
            if item['T_camera_from_vehicle'] != expected:
                raise ValueError('profile matrix differs from E1 source')
        points = [[camera, *point] for camera in range(4) for point in records[(seed, camera)]['validation_points']]
        run = subprocess.run([str(project_binary), str(path)], input=json.dumps(points),
                             capture_output=True, text=True, check=True)
        pixels = json.loads(run.stdout)
        if len(pixels) != 192 or not all(p['valid'] for p in pixels):
            raise ValueError('native projection control failed')
        if state == 'estimated':
            for camera in range(4):
                observed = np.asarray(records[(seed, camera)]['validation_pixels'])
                projected = np.array([[p['u'], p['v']] for p in pixels[camera*48:(camera+1)*48]])
                rmse = float(np.sqrt(np.mean(np.sum((observed-projected)**2, axis=1))))
                expected = results[f'mount{seed}/cam{camera}/gaussian/ransac_epnp_lm']['status']['validation_rmse_px']
                if abs(rmse-expected) >= 1e-7:
                    raise ValueError('native profile validation differs from E1 result')
                rmse_checks += 1
        if path.parent.parent.name == 'high':
            models.setdefault(str(seed), {})[state] = config['cameras']
    matrix_checks = 0
    for view in ('high', 'low'):
        capture = validate_family_plan(json.loads((prepared/f'capture-{view}.json').read_text()))
        if len(capture['cases']) != 9:
            raise ValueError('E3 preflight requires nine capture cases per view')
        for case in capture['cases']:
            seed = case['recipe']['mount_seed']
            for camera in case['camera_rig']['actual']['cameras']:
                if camera['T_camera_from_vehicle'] != records[(seed, camera['id'])]['true_transform']:
                    raise ValueError('capture matrix differs from E1 truth')
                matrix_checks += 1
    base = json.loads((prepared/'profiles/high/mount1001/nominal.json').read_text())
    legacy, _ = perturb(base, dict(schema_version=1, seed=0, mount_seed=1001,
                                  mounts=dict(yaw_deg=3, pitch_deg=3, along_body_m=.05)))
    differences = []
    for camera in legacy['cameras']:
        a, b = np.array(camera['T_camera_from_vehicle']), np.array(records[(1001, camera['id'])]['true_transform'])
        differences.append(dict(camera_id=camera['id'],
            center_error_mm=float(np.linalg.norm(-a[:3, :3].T@a[:3, 3]+b[:3, :3].T@b[:3, 3])*1000),
            rotation_error_deg=float(np.rad2deg(np.arccos(np.clip((np.trace(a[:3, :3]@b[:3, :3].T)-1)/2, -1, 1))))))
    audit = dict(schema_version=1, scope='offline_e3_input_preflight_no_render',
        registry_sha256=sha(prepared/'registry.json'), source_plan_sha256=registry['source_plan_sha256'],
        e1_truth_sha256=sha(truth_path), e1_report_sha256=sha(report_path), independent_rgb_available=False,
        native_e3_trials_completed=False, blender_adapter_runtime_tested=False, mask_policy=registry['mask_policy'],
        native_configurations=18, native_points_projected=3456, estimated_validation_rmse_checks=rmse_checks,
        exact_capture_pose_checks=matrix_checks, camera_models=models,
        legacy_seed1001_disagreement=differences, registry_files=registry['files'])
    output_audit.parent.mkdir(parents=True, exist_ok=True)
    output_audit.write_text(json.dumps(audit, indent=2)+'\n')
    return audit
