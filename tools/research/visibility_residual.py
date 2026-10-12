"""Read-only attribution of residual color after frozen native source-visibility control."""
import json
import numpy as np
from source_footprint_audit import footprint


def select_pixels(rgb, target, visible, threshold):
    color = np.asarray(rgb, float) / 255
    proxy = np.minimum(color[..., 0], color[..., 2]) - color[..., 1] > threshold
    residual = proxy & ~target
    control = target & visible
    coordinates = np.argwhere(residual | control)
    return coordinates, residual[tuple(coordinates.T)]


def collect(root, plan_path):
    from source_correspondence import sha, verified, load_product, describe
    from source_correspondence_controls import transform_points
    from PIL import Image
    from displaced_correspondence import linearize, encode, chroma_mask
    plan = json.loads(plan_path.read_text())
    for name, digest in plan['frozen_files'].items():
        verified(root / name, digest)
    out = root / plan['output_root']
    scene_path = out / 'scene-truth.json'
    scene = json.loads(scene_path.read_text())
    if scene['plan_sha256'] != sha(plan_path):
        raise ValueError('scene plan binding differs')
    verified(root / 'tools/blender/visibility_residual.py', scene['helper_sha256'])
    truth_path = root / plan['truth_path']
    truth = json.loads(truth_path.read_text())
    target_id = next(v for k, v in truth['objects'].items() if 'coded object target' in k)
    rows = []
    expected = [(f, v) for f in plan['frames'] for v in plan['variants']]
    if [(r['frame'], r['variant']) for r in scene['records']] != expected:
        raise ValueError('scene cases differ')
    for record in scene['records']:
        frame, variant = record['frame'], record['variant']
        native = root / plan['native_root'] / f'frame{frame}-{variant}'
        meta = json.loads(verified(native / 'report.json', record['native_report_sha256']).read_text())
        if meta['config'].get('fusion', {}).get('mode', 'edge_feather') != 'edge_feather':
            raise ValueError('edge_feather fixture required')
        p = {v['file']: load_product(native, v) for v in meta['products']}
        t = truth['frames'][frame]
        ids = np.load(verified(truth_path.parent / t['objects'], truth['sha256'][t['objects']]), allow_pickle=False)
        vis = np.load(verified(truth_path.parent / t['visibility'], truth['sha256'][t['visibility']]), allow_pickle=False)
        coords, residual = select_pixels(p['output.rgba'][..., :3], ids == target_id, vis != 0, plan['chroma_threshold'])
        d = out / f'frame{frame}-{variant}'
        a = {n: np.load(verified(d / n, h), allow_pickle=False) for n, h in record['files'].items()}
        if not np.array_equal(coords, a['pixels.npy']) or not np.array_equal(residual, a['residual.npy']):
            raise ValueError('selection differs')
        y, x = coords.T
        weights = np.zeros((len(coords), 4));colors = np.zeros((len(coords), 4, 3))
        target_center = np.zeros(len(coords), bool);target_texel = target_center.copy();colored_texel = target_center.copy()
        road_distances = []
        for i, camera in enumerate(meta['config']['cameras']):
            valid = p[f'camera{i}-status.u8'][y, x, 0] == 1
            uv = p[f'camera{i}-pixels.f32'][y, x][valid]
            source = meta['inputs'][i]
            image = np.asarray(Image.open(verified(root / source['path'], source['sha256'])).convert('RGB'))
            xy, w = footprint(uv, image.shape[1], image.shape[0])
            if not np.array_equal(xy, a['texels.npy'][valid, i]) or not np.array_equal(w, a['weights.npy'][valid, i]):
                raise ValueError('bilinear footprint differs')
            texel_rgb = image[xy[..., 1], xy[..., 0]]
            colors[valid, i] = (texel_rgb / 255 * w[..., None]).sum(axis=1)
            edge = np.minimum.reduce([uv[:, 0], uv[:, 1], image.shape[1]-1-uv[:, 0], image.shape[0]-1-uv[:, 1]])
            weights[valid, i] = np.clip(edge / meta['config'].get('fusion', {}).get('edge_width_px', 24), 0, 1)
            road = residual & (a['center-kinds.npy'][:, i] == 2) & valid
            carrier_world = transform_points(np.asarray(t['T_world_from_vehicle']), p['carrier-positions.f32'][y[road], x[road]])
            road_distances.extend(np.linalg.norm(carrier_world - a['center-hits.npy'][road, i], axis=1))
            positive = weights[valid, i] > 0
            target_center[valid] |= (a['center-kinds.npy'][valid, i] == 1) & positive
            target_texel[valid] |= ((a['texel-kinds.npy'][valid, i] == 1) & (w > 0)).any(axis=1) & positive
            colored_texel[valid] |= (chroma_mask(texel_rgb) & (w > 0)).any(axis=1) & positive
        total = weights.sum(axis=1)
        supported = total > 1e-6
        weights = np.divide(weights, total[:, None], out=np.zeros_like(weights), where=supported[:, None])
        predicted = encode((linearize(colors) * weights[..., None]).sum(axis=1)) * 255
        error = np.abs(predicted[supported] - p['output.rgba'][y, x, :3][supported])
        if error.size and error.max() > plan['rgba_error_limit_lsb']:
            raise ValueError('independent fusion reconstruction exceeds predeclared bound')
        def counts(mask):
            return dict(pixels=int(mask.sum()), virtual_target_pixels=int((mask & (a['virtual-kinds.npy'] == 1)).sum()),
                        virtual_road_pixels=int((mask & (a['virtual-kinds.npy'] == 2)).sum()),
                        positive_weight_target_center_pixels=int((mask & target_center).sum()),
                        positive_weight_target_texel_pixels=int((mask & target_texel).sum()),
                        colored_texel_without_target_center_texel_pixels=int((mask & colored_texel & ~target_texel).sum()),
                        positive_weight_chroma_texel_pixels=int((mask & colored_texel).sum()),
                        no_weighted_source_pixels=int((mask & ~supported).sum()))
        rows.append(dict(frame=frame, variant=variant, native_report_sha256=sha(native / 'report.json'),
                         scene_record=record, residual=counts(residual), control=counts(~residual),
                         rgba_reconstruction_error_lsb=describe(error.ravel()),
                         residual_carrier_to_source_road_hit_distance_m=describe(road_distances),
                         residual_source_road_pairs=len(road_distances)))
    return dict(schema_version=1, scope=plan['scope'], plan_sha256=sha(plan_path), collector_sha256=sha(__file__),
                scene_truth_sha256=sha(scene_path), blender_version=scene['blender_version'], rows=rows, limitations=plan['limitations'])
