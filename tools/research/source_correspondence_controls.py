"""Height/frame controls with explicit world/vehicle transforms; v1 audit stays frozen."""
import json
from pathlib import Path

import numpy as np
from scipy.ndimage import binary_erosion

from source_correspondence import sha, verified, load_product, project, classify, describe


def transform_points(pose, points):
    pose, points = np.asarray(pose, float), np.asarray(points, float)
    if pose.shape != (4, 4) or points.shape[-1] != 3 or not np.allclose(pose[3], [0, 0, 0, 1]):
        raise ValueError('homogeneous pose and 3D points required')
    return points @ pose[:3, :3].T + pose[:3, 3]


def collect_case(root, plan_path, case_entry, frame_index):
    plan = json.loads(plan_path.read_text())
    for name, digest in plan['frozen_files'].items():
        verified(root / name, digest)
    output = root / plan['output_root'] / case_entry['id'] / f'frame{frame_index}'
    inputs = root / case_entry['input_root']
    scene_path = output / 'scene-truth.json'
    scene = json.loads(scene_path.read_text())
    if (scene['plan_sha256'] != sha(plan_path) or not scene['direct_mask_bitwise_equal']
            or not scene['target_visibility_bitwise_equal']):
        raise ValueError('scene truth plan/reconstruction differs')
    verified(root / 'tools/blender/source_surface_controls.py', scene['helper_sha256'])
    arrays = {name: np.load(verified(output / name, digest), allow_pickle=False)
              for name, digest in scene['files'].items()}
    mask, coordinates = arrays['direct-mask.npy'], arrays['target-pixels.npy']
    if not np.array_equal(coordinates, np.argwhere(mask)):
        raise ValueError('target pixel ordering differs')
    yy, xx = coordinates.T
    points_world = arrays['direct-points.npy'][yy, xx]
    archived_path = inputs / 'seed101-capture/paired_truth.json'
    archived = json.loads(verified(archived_path, scene['archived_truth_sha256']).read_text())
    frame = archived['frames'][frame_index]
    visibility = np.load(verified(inputs / 'seed101-capture' / frame['visibility'],
                                 archived['sha256'][frame['visibility']]), allow_pickle=False)[yy, xx]
    if not np.array_equal(arrays['target-visibility.npy'], visibility):
        raise ValueError('source visibility differs from archived truth')
    vehicle_pose = np.asarray(frame['T_world_from_vehicle'], float)
    points = transform_points(np.linalg.inv(vehicle_pose), points_world)
    interior = binary_erosion(mask, iterations=2)[yy, xx]
    rois = {'all': np.ones(len(yy), bool), 'source_visible': visibility != 0,
            'interior': interior, 'visible_interior': interior & (visibility != 0)}
    session_root = root / plan['output_root']
    invocations = json.loads((session_root / 'invocations.json').read_text())
    verified(session_root / 'sv-correspondence-probe', invocations['binary_sha256'])
    rows = []
    for carrier in plan['carriers']:
        directory = output / 'native' / carrier
        report_path = directory / 'report.json'
        report = json.loads(verified(report_path, scene['native_report_sha256'][carrier]).read_text())
        reference_dir = root / case_entry['native_reference_root'] / carrier
        reference_path = reference_dir / 'report.json'
        reference = json.loads(reference_path.read_text())
        if (report['config_sha256'] != sha(reference_dir / 'config.json')
                or report['manifest_sha256'] != sha(inputs / 'seed101-inputs/manifest.json')
                or not report['normal_output_unchanged'] or report['scenario_timestamp_ns'] != frame['scenario_timestamp_ns']
                or report.get('frame_index') != frame_index):
            raise ValueError('native configuration/frame/capture parity differs')
        manifest = json.loads((inputs / 'seed101-inputs/manifest.json').read_text())
        for i, source in enumerate(report['inputs']):
            expected = (inputs / 'seed101-inputs' / manifest['frames'][frame_index]['paths'][i]).resolve()
            if source['camera_id'] != i or Path(source['path']).resolve() != expected:
                raise ValueError('native source path/camera ID differs from frozen manifest')
            verified(expected, source['sha256'])
        products = {p['file']: load_product(directory, p) for p in report['products']}
        output_sha = sha(directory / 'output.rgba')
        baseline = next(f for f in reference['frame_baselines'] if f['frame_index'] == frame_index)
        if output_sha != baseline['rgba_sha256'] or baseline['metadata']['fusion_mode'] != 'edge_feather':
            raise ValueError('historical first-frame server RGBA hash differs')
        regions = products['carrier-regions.u8'][yy, xx, 0]
        carrier_points = products['carrier-positions.f32'][yy, xx]
        status = np.stack([products[f'camera{i}-status.u8'][yy, xx, 0] for i in range(4)], axis=-1)
        pixels = np.stack([products[f'camera{i}-pixels.f32'][yy, xx] for i in range(4)], axis=1)
        kinds, hits = arrays[carrier+'-source-kinds.npy'], arrays[carrier+'-source-hits.npy']
        classes = classify(status, kinds, hits, points_world, plan['surface_match_tolerance_m'])
        if not np.all(sum(classes.values()) == 1):
            raise ValueError('classification missing/overlapping or nonfinite target hit')
        projection_error, correct_gap, edge = [], [], []
        for i, camera in enumerate(archived['config']['cameras']):
            expected, valid = project(camera, carrier_points)
            if not np.array_equal(valid, status[:, i] == 1):
                raise ValueError('independent/native source validity differs in target ROI')
            projection_error.extend(np.linalg.norm(expected[valid]-pixels[valid, i], axis=-1))
            correct, _ = project(camera, points)
            visible = valid & ((visibility & (1 << i)) != 0)
            correct_gap.extend(np.linalg.norm(correct[visible]-pixels[visible, i], axis=-1))
            p = pixels[valid, i]
            size = camera['resolution']
            distance = np.minimum(np.minimum(p[:, 0], p[:, 1]),
                                  np.minimum(size['width']-1-p[:, 0], size['height']-1-p[:, 1]))
            edge.extend(np.clip(distance / 24, 0, 1))
        rgb = products['output.rgba'][..., :3].astype(float) / 255
        predicted = np.minimum(rgb[..., 0], rgb[..., 2]) - rgb[..., 1] > .15
        table = {}
        for name, roi in rois.items():
            table[name] = {'pixels': int(roi.sum()),
                          'source_pairs': int(roi.sum()*4),
                          **{key: int(value[roi].sum()) for key, value in classes.items()},
                          'road_pairs': int(((status == 1) & (kinds == 2))[roi].sum()),
                          'predicted_target_pixels': int(predicted[yy[roi], xx[roi]].sum())}
        rows.append({'carrier': carrier, 'report_sha256': sha(report_path),
                     'reference_report_sha256': sha(reference_path), 'output_sha256': output_sha,
                     'historical_server_baseline_hash_equal': True,
                     'source_revision': report['source_revision'], 'source_fingerprint': report['source_fingerprint'],
                     'device': report['device'], 'rois': table,
                     'carrier_region_counts': {str(k): int((regions == k).sum()) for k in np.unique(regions)},
                     'carrier_z_m': describe(carrier_points[:, 2]),
                     'carrier_to_direct_surface_m': describe(np.linalg.norm(transform_points(vehicle_pose, carrier_points)-points_world, axis=-1)),
                     'native_vs_double_projection_px': describe(projection_error),
                     'visible_true_vs_native_uv_px': describe(correct_gap),
                     'valid_edge_weight': describe(edge),
                     'predicted_target_full_frame_pixels': int(predicted.sum()),
                     'statuses': {str(k): int((status == k).sum()) for k in np.unique(status)}})
    return {'schema_version': 1, 'case': case_entry['id'], 'frame_index': frame_index,
            'T_world_from_vehicle': vehicle_pose.tolist(), 'scope': plan['scope'], 'plan_sha256': sha(plan_path),
            'scene_truth_sha256': sha(scene_path), 'binary_sha256': invocations['binary_sha256'],
            'collector_sha256': sha(__file__),
            'probe_source_sha256': sha(root / 'tests/correspondence_probe.cpp'),
            'capture_provenance_sha256': sha(inputs.parent / 'provenance.json'), 'scene': scene, 'rows': rows,
            'limitations': plan['limitations']}


def collect(root, plan_path):
    plan = json.loads(plan_path.read_text())
    cases = [collect_case(root, plan_path, case, frame) for case in plan['cases'] for frame in plan['frames']]
    return {'schema_version': 1, 'scope': plan['scope'], 'plan_sha256': sha(plan_path),
            'collector_sha256': sha(__file__), 'cases': cases, 'limitations': plan['limitations']}
