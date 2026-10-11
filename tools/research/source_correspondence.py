"""Read-only audit of native sample coordinates against independent Blender scene hits."""
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy.ndimage import binary_erosion


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verified(path, expected):
    if sha(path) != expected:
        raise ValueError('checksum mismatch: ' + str(path))
    return path


def load_product(directory, product):
    path = verified(directory / product['file'], product['sha256'])
    dtype = {'float32': '<f4', 'uint8': 'u1'}[product['dtype']]
    if product['byte_order'] != 'little_endian' or product['origin'] != 'top_left':
        raise ValueError('unsupported native layout')
    shape = (product['height'], product['width'], product['channels'])
    return np.fromfile(path, dtype=dtype).reshape(shape)


def project(camera, points):
    """Independent double equidistant model for this restricted zero-distortion fixture."""
    if any(camera['projection']['k']) or camera['projection'].get('alpha', 0):
        raise ValueError('equidistant zero-skew optics required')
    transform = np.asarray(camera['T_camera_from_vehicle'], float)
    local = points @ transform[:3, :3].T + transform[:3, 3]
    rho = np.hypot(local[..., 0], local[..., 1])
    theta = np.arctan2(rho, local[..., 2])
    scale = np.divide(theta, rho, out=np.zeros_like(theta), where=rho > 1e-14)
    optics, size = camera['projection'], camera['resolution']
    pixels = local[..., :2] * scale[..., None] * [optics['fx'], optics['fy']] + [optics['cx'], optics['cy']]
    valid = (local[..., 2] > optics['z_epsilon_m']) & (theta <= optics['theta_max_rad'])
    valid &= ((pixels >= 0) & (pixels <= [size['width']-1, size['height']-1])).all(axis=-1)
    return pixels, valid


def classify(status, kinds, hits, points, tolerance):
    """Keep invalid projection separate from a valid ray that misses or hits wrong geometry."""
    valid = status == 1
    distances = np.linalg.norm(hits - points[:, None, :], axis=-1)
    return {'invalid_projection': ~valid, 'no_scene_hit': valid & (kinds == 0),
            'wrong_object': valid & (kinds > 1),
            'same_object_wrong_point': valid & (kinds == 1) & (distances > tolerance),
            'matched_surface': valid & (kinds == 1) & (distances <= tolerance)}


def describe(values):
    values = np.asarray(values)
    values = values[np.isfinite(values)]
    return {key: float(value) for key, value in zip(('min', 'p50', 'max'),
            np.quantile(values, [0, .5, 1]))} if len(values) else None


def collect(root, plan_path):
    plan = json.loads(plan_path.read_text())
    for name, digest in plan['frozen_files'].items():
        verified(root / name, digest)
    output, inputs = root / plan['output_root'], root / plan['input_root']
    scene_path = output / 'scene-truth.json'
    scene = json.loads(scene_path.read_text())
    if (scene['plan_sha256'] != sha(plan_path) or not scene['direct_mask_bitwise_equal']
            or not scene['target_visibility_bitwise_equal']):
        raise ValueError('scene truth plan/reconstruction differs')
    verified(root / 'tools/blender/source_surface_truth.py', scene['helper_sha256'])
    arrays = {name: np.load(verified(output / name, digest), allow_pickle=False)
              for name, digest in scene['files'].items()}
    mask, coordinates = arrays['direct-mask.npy'], arrays['target-pixels.npy']
    if not np.array_equal(coordinates, np.argwhere(mask)):
        raise ValueError('target pixel ordering differs')
    yy, xx = coordinates.T
    points = arrays['direct-points.npy'][yy, xx]
    archived_path = inputs / 'seed101-capture/paired_truth.json'
    archived = json.loads(verified(archived_path, scene['archived_truth_sha256']).read_text())
    frame = archived['frames'][0]
    visibility = np.load(verified(inputs / 'seed101-capture' / frame['visibility'],
                                 archived['sha256'][frame['visibility']]), allow_pickle=False)[yy, xx]
    if not np.array_equal(arrays['target-visibility.npy'], visibility):
        raise ValueError('source visibility differs from archived truth')
    interior = binary_erosion(mask, iterations=2)[yy, xx]
    rois = {'all': np.ones(len(yy), bool), 'source_visible': visibility != 0,
            'interior': interior, 'visible_interior': interior & (visibility != 0)}
    invocations = json.loads((output / 'invocations.json').read_text())
    verified(output / 'sv-correspondence-probe', invocations['binary_sha256'])
    rows = []
    for carrier in plan['carriers']:
        directory = output / 'native' / carrier
        report_path = directory / 'report.json'
        report = json.loads(verified(report_path, scene['native_report_sha256'][carrier]).read_text())
        reference_dir = root / plan['native_reference_root'] / carrier
        reference_path = reference_dir / 'report.json'
        reference = json.loads(reference_path.read_text())
        if (report['config_sha256'] != sha(reference_dir / 'config.json')
                or report['manifest_sha256'] != sha(inputs / 'seed101-inputs/manifest.json')
                or not report['normal_output_unchanged'] or report['scenario_timestamp_ns'] != '0'):
            raise ValueError('native configuration/frame/capture parity differs')
        manifest = json.loads((inputs / 'seed101-inputs/manifest.json').read_text())
        for i, source in enumerate(report['inputs']):
            expected = (inputs / 'seed101-inputs' / manifest['frames'][0]['paths'][i]).resolve()
            if source['camera_id'] != i or Path(source['path']).resolve() != expected:
                raise ValueError('native source path/camera ID differs from frozen manifest')
            verified(expected, source['sha256'])
        products = {p['file']: load_product(directory, p) for p in report['products']}
        output_sha = sha(directory / 'output.rgba')
        if output_sha != reference['baseline_rgba_sha256'] or reference['baseline']['fusion_mode'] != 'edge_feather':
            raise ValueError('historical first-frame server RGBA hash differs')
        regions = products['carrier-regions.u8'][yy, xx, 0]
        carrier_points = products['carrier-positions.f32'][yy, xx]
        status = np.stack([products[f'camera{i}-status.u8'][yy, xx, 0] for i in range(4)], axis=-1)
        pixels = np.stack([products[f'camera{i}-pixels.f32'][yy, xx] for i in range(4)], axis=1)
        kinds, hits = arrays[carrier+'-source-kinds.npy'], arrays[carrier+'-source-hits.npy']
        classes = classify(status, kinds, hits, points, plan['surface_match_tolerance_m'])
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
                     'carrier_to_direct_surface_m': describe(np.linalg.norm(carrier_points-points, axis=-1)),
                     'native_vs_double_projection_px': describe(projection_error),
                     'visible_true_vs_native_uv_px': describe(correct_gap),
                     'valid_edge_weight': describe(edge),
                     'predicted_target_full_frame_pixels': int(predicted.sum()),
                     'statuses': {str(k): int((status == k).sum()) for k in np.unique(status)}})
    return {'schema_version': 1, 'scope': plan['scope'], 'plan_sha256': sha(plan_path),
            'scene_truth_sha256': sha(scene_path), 'binary_sha256': invocations['binary_sha256'],
            'collector_sha256': sha(__file__),
            'probe_source_sha256': sha(root / 'tests/correspondence_probe.cpp'),
            'capture_provenance_sha256': sha(inputs.parent / 'provenance.json'), 'scene': scene, 'rows': rows,
            'limitations': plan['limitations']}
