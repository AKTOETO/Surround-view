#!/usr/bin/env python3
"""Dense analytic carrier reference; no GL, C++ math, or triangle meshes."""
import argparse
import hashlib
import json
from pathlib import Path
import time

import numpy as np
from PIL import Image


def roots(a, b, c):
    """Two real quadratic roots, with a linear limit and inf for no hit."""
    a, b, c = np.broadcast_arrays(a, b, c)
    out = np.full((2,) + a.shape, np.inf)
    quadratic = np.abs(a) > 1e-14
    discriminant = b*b - 4*a*c
    real = quadratic & (discriminant >= 0)
    # Stable q formulation avoids cancellation of the small root.
    q = -.5 * (b + np.copysign(np.sqrt(np.maximum(discriminant, 0)), b))
    np.divide(q, a, out=out[0], where=real)
    np.divide(c, q, out=out[1], where=real & (q != 0))
    out[1] = np.where(real & (q == 0), out[0], out[1])
    np.divide(-c, b, out=out[0], where=~quadratic & (np.abs(b) > 1e-14))
    return out


def rays(config):
    """Top-left pixel centers; normalized world rays and camera-forward depth factor."""
    v = config['virtual_camera']
    target = np.asarray(v.get('target_m', [0, 0, 0]), dtype=float)
    az, el = v['azimuth_rad'], v['elevation_rad']
    eye = target + v['distance_m'] * np.array([np.cos(el)*np.cos(az), np.cos(el)*np.sin(az), np.sin(el)])
    forward = target - eye
    forward /= np.linalg.norm(forward)
    up = np.array([0., 0., 1.]) if abs(forward[2]) <= .999999 else np.array([0., 1., 0.])
    right = np.cross(forward, up)
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    w, h = config['output']['width'], config['output']['height']
    y, x = np.mgrid[:h, :w]
    tangent = np.tan(v['fov_y_rad']/2)
    directions = (forward + ((2*(x+.5)/w-1)*tangent*w/h)[..., None]*right
                  + ((1-2*(y+.5)/h)*tangent)[..., None]*up)
    directions /= np.linalg.norm(directions, axis=-1, keepdims=True)
    return eye, directions, directions @ forward


def intersect(surface, origin, directions, near=0., far=np.inf, depth_factor=1.):
    """Nearest carrier intersection; clipping uses view-axis depth, not ray length.

    Bowl patches are exact quadratics on the 3x3 flat/rising regions.
    Closed carriers include their floor and cap where applicable.
    """
    o, d = np.asarray(origin, float), np.asarray(directions, float)
    best = np.full(d.shape[:-1], np.inf)
    kind = surface['type']

    def consider(t, accept):
        nonlocal best
        with np.errstate(invalid='ignore'):
            p = o + t[..., None]*d
            valid = (np.isfinite(t) & (t > 1e-10) & (t*depth_factor >= near)
                     & (t*depth_factor <= far) & accept(p))
        best = np.where(valid & (t < best), t, best)

    def horizontal(z, accept):
        t = np.full(best.shape, np.inf)
        np.divide(z-o[2], d[..., 2], out=t, where=np.abs(d[..., 2]) > 1e-14)
        consider(t, accept)

    if kind == 'rectangular_bowl_v1':
        a, b = surface['flat_half_length_m'], surface['flat_half_width_m']
        A, B, H = surface['outer_half_length_m'], surface['outer_half_width_m'], surface['corner_height_m']
        for sx, lo_x, hi_x in [(-1, -A, -a), (0, -a, a), (1, a, A)]:
            for sy, lo_y, hi_y in [(-1, -B, -b), (0, -b, b), (1, b, B)]:
                ux, vx = (sx*o[0]-a, sx*d[..., 0]) if sx else (0., 0.)
                uy, vy = (sy*o[1]-b, sy*d[..., 1]) if sy else (0., 0.)
                qa = H/2*(vx*vx/(A-a)**2 + vy*vy/(B-b)**2)
                qb = H*(ux*vx/(A-a)**2 + uy*vy/(B-b)**2) - d[..., 2]
                qc = H/2*(ux*ux/(A-a)**2 + uy*uy/(B-b)**2) - o[2]
                for t in roots(qa, qb, qc):
                    consider(t, lambda p: ((p[..., 0] >= lo_x-1e-9) & (p[..., 0] <= hi_x+1e-9)
                                          & (p[..., 1] >= lo_y-1e-9) & (p[..., 1] <= hi_y+1e-9)))
    elif kind == 'dome_floor_v1':
        radius = surface['dome_radius_m']
        horizontal(0, lambda p: p[..., 0]**2+p[..., 1]**2 <= radius**2+1e-9)
        for t in roots(np.sum(d*d, axis=-1), 2*(d @ o), o @ o-radius**2):
            consider(t, lambda p: p[..., 2] >= -1e-9)
    elif kind == 'cylinder_floor_v1':
        radius, height = surface['radius_m'], surface['height_m']
        disk = lambda p: p[..., 0]**2+p[..., 1]**2 <= radius**2+1e-9
        horizontal(0, disk)
        horizontal(height, disk)
        for t in roots(np.sum(d[..., :2]**2, axis=-1), 2*(d[..., :2] @ o[:2]), o[:2] @ o[:2]-radius**2):
            consider(t, lambda p: (p[..., 2] >= -1e-9) & (p[..., 2] <= height+1e-9))
    elif kind == 'cube_floor_v1':
        radius, height = surface['half_extent_m'], surface['height_m']
        horizontal(0, lambda p: np.max(np.abs(p[..., :2]), axis=-1) <= radius+1e-9)
        horizontal(height, lambda p: np.max(np.abs(p[..., :2]), axis=-1) <= radius+1e-9)
        for axis in (0, 1):
            for side in (-radius, radius):
                t = np.full(best.shape, np.inf)
                np.divide(side-o[axis], d[..., axis], out=t, where=np.abs(d[..., axis]) > 1e-14)
                consider(t, lambda p: ((np.abs(p[..., 1-axis]) <= radius+1e-9)
                                      & (p[..., 2] >= -1e-9) & (p[..., 2] <= height+1e-9)))
    else:
        raise ValueError('unsupported analytic carrier: '+kind)
    points = o + np.where(np.isfinite(best), best, 0)[..., None]*d
    return points, np.isfinite(best), best


def sample(camera, points, image):
    """Independent atan2 fisheye projection and bilinear RGB8 texture sampling."""
    T, k = np.asarray(camera['T_camera_from_vehicle']), camera['projection']
    p = points @ T[:3, :3].T + T[:3, 3]
    rho = np.hypot(p[..., 0], p[..., 1])
    theta = np.arctan2(rho, p[..., 2])
    td = theta.copy()
    for j, coefficient in enumerate(k['k']):
        td += coefficient*theta**(2*j+3)
    scale = np.divide(td, rho, out=np.zeros_like(td), where=rho > 1e-14)
    u, v = k['fx']*p[..., 0]*scale+k['cx'], k['fy']*p[..., 1]*scale+k['cy']
    h, w = image.shape[:2]
    valid = ((p[..., 2] > k['z_epsilon_m']) & (theta <= k['theta_max_rad'])
             & (u >= 0) & (v >= 0) & (u <= w-1) & (v <= h-1))
    u, v = np.clip(u, 0, w-1), np.clip(v, 0, h-1)
    x, y = np.floor(u).astype(int), np.floor(v).astype(int)
    x1, y1 = np.minimum(x+1, w-1), np.minimum(y+1, h-1)
    fx, fy = (u-x)[..., None], (v-y)[..., None]
    rgb = ((1-fy)*((1-fx)*image[y, x]+fx*image[y, x1])
           + fy*((1-fx)*image[y1, x]+fx*image[y1, x1]))/255.
    edge = np.minimum.reduce([u, v, w-1-u, h-1-v])
    return rgb, valid, edge, theta


def render(config, images):
    eye, directions, depth = rays(config)
    near, far = config['virtual_camera']['clip_m']
    points, hit, distance = intersect(config['surface'], eye, directions, near, far, depth)
    fusion = dict(mode='edge_feather', edge_width_px=24., angle_power=2.)
    fusion.update(config.get('fusion', {}))
    weights, colors, validity = [], [], []
    for camera, image in zip(sorted(config['cameras'], key=lambda c: c['id']), images):
        rgb, valid, edge, theta = sample(camera, points, image)
        valid &= hit
        angle = np.maximum(np.cos(theta), 0)**fusion['angle_power']
        if fusion['mode'] == 'hard_best_angle':
            weight = np.where(valid, angle, -1.)
        else:
            weight = np.clip(edge/fusion['edge_width_px'], 0, 1)*valid
            if fusion['mode'] == 'angular_feather':
                weight *= angle
            elif fusion['mode'] != 'edge_feather':
                raise ValueError('unsupported fusion mode')
        weights.append(weight)
        colors.append(rgb)
        validity.append(valid)
    weights, colors = np.stack(weights, axis=-1), np.stack(colors, axis=-2)
    coverage = np.sum(validity, axis=0).astype(np.uint8)
    if fusion['mode'] == 'hard_best_angle':
        winner = np.argmax(weights, axis=-1)
        weights = np.eye(4)[winner]*(coverage > 0)[..., None]
    total = weights.sum(axis=-1)
    weights = np.divide(weights, total[..., None], out=np.zeros_like(weights), where=total[..., None] > 1e-6)
    linear = np.where(colors <= .04045, colors/12.92, ((colors+.055)/1.055)**2.4)
    blended = np.sum(linear*weights[..., None], axis=-2)
    rgb = np.where(blended <= .0031308, blended*12.92, 1.055*blended**(1/2.4)-.055)
    if config['surface']['type'] != 'rectangular_bowl_v1':
        radius = config['surface'].get('dome_radius_m', config['surface'].get('height_m'))
        elevation = np.clip(points[..., 2]/radius, 0, 1)
        t = np.clip((elevation-.35)/(.97-.35), 0, 1)
        t = (t*t*(3-2*t))[..., None]
        fallback = (1-t)*[.66, .73, .78]+t*[.17, .27, .39]
        fallback = np.where((points[..., 2] > 1e-9)[..., None], fallback, [.23, .24, .24])
    else:
        fallback = np.broadcast_to([.23, .24, .24], rgb.shape)
    rgb = np.where((total > 1e-6)[..., None], rgb, fallback)
    vehicle = config['vehicle']
    footprint = ((np.abs(points[..., 0]) <= vehicle['length_m']/2+vehicle['mask_margin_m'])
                 & (np.abs(points[..., 1]) <= vehicle['width_m']/2+vehicle['mask_margin_m']))
    # No vehicle overlay in this reference. A separate mask excludes its footprint.
    evaluation = hit & ~footprint
    rgb = np.where(hit[..., None], rgb, 0)
    return dict(rgb=np.uint8(np.rint(np.clip(rgb, 0, 1)*255)), world=points,
                hit=hit, distance=distance, coverage=coverage, weights=weights, evaluation=evaluation)


def run(config_path, manifest_path, output, frame=0):
    config_path, manifest_path, output = map(Path, (config_path, manifest_path, output))
    config, manifest = json.loads(config_path.read_text()), json.loads(manifest_path.read_text())
    if config.get('schema_version') != 1 or manifest.get('schema_version') != 1:
        raise ValueError('schema_version 1 required')
    if not 0 <= frame < len(manifest['frames']):
        raise ValueError('frame index outside manifest')
    if any(manifest['frames'][frame].get('offset_ns', [0]*4)):
        raise ValueError('reference requires a synchronous zero-offset row')
    cameras = sorted(config['cameras'], key=lambda c: c['id'])
    if [c['id'] for c in cameras] != list(range(4)) or len(manifest['frames'][frame]['paths']) != 4:
        raise ValueError('four canonical camera IDs/paths required')
    if manifest['calibration_ids'] != [c['calibration_id'] for c in cameras]:
        raise ValueError('manifest calibration mismatch')
    images, hashes = [], {}
    for camera, name in zip(cameras, manifest['frames'][frame]['paths']):
        path = (manifest_path.parent/name).resolve()
        if not path.is_relative_to(manifest_path.parent.resolve()):
            raise ValueError('image escapes dataset')
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if manifest['sha256'].get(name) != digest:
            raise ValueError('image checksum mismatch')
        image = np.asarray(Image.open(path).convert('RGB'), dtype=float)
        r = camera['resolution']
        if image.shape != (r['height'], r['width'], 3):
            raise ValueError('image dimensions mismatch')
        images.append(image)
        hashes[name] = digest
    start = time.perf_counter()
    result = render(config, images)
    elapsed = time.perf_counter()-start
    output.mkdir(parents=True, exist_ok=False)
    Image.fromarray(result['rgb']).save(output/'reference.png')
    Image.fromarray(result['coverage']*np.uint8(63)).save(output/'coverage.png')
    Image.fromarray(result['evaluation'].astype(np.uint8)*255).save(output/'evaluation-mask.png')
    np.savez_compressed(output/'reference.npz', **result)
    roi = result['evaluation']
    count = int(roi.sum())
    report = dict(schema_version=1, suite_id='surround-view-analytic-reference-v1',
                  config_sha256=hashlib.sha256(config_path.read_bytes()).hexdigest(),
                  manifest_sha256=hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
                  implementation_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  input_sha256=hashes, frame_index=frame,
                  scenario_timestamp_ns=manifest['frames'][frame]['scenario_timestamp_ns'],
                  surface=config['surface'], fusion=config.get('fusion', {'mode': 'edge_feather'}),
                  output=config['output'], render_seconds=elapsed, evaluation_pixels=count,
                  carrier_miss_pixels=int((~result['hit']).sum()),
                  coverage_histogram=[int(((result['coverage'] == i) & roi).sum()) for i in range(5)],
                  observed_fraction=float((result['coverage'][roi] > 0).mean()) if count else None,
                  positive_weight_fraction=float((result['weights'][roi].sum(axis=-1) > 0).mean()) if count else None,
                  limitations=['analytic carrier, not physical scene depth', 'no vehicle overlay or occlusion',
                               'footprint mask alone is not sufficient for GPU image comparison',
                               'single offline frame; no seam/ghosting/temporal quality conclusion'])
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    (output/'REPORT.md').write_text('# Аналитический CPU-эталон\n\n'
        'Первичный результат одного кадра; coverage не доказывает видимость реальных объектов.\n\n'
        '```json\n'+json.dumps(report, indent=2, ensure_ascii=False)+'\n```\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--frame', type=int, default=0)
    args = parser.parse_args()
    try:
        print(json.dumps(run(args.config, args.manifest, args.output, args.frame), indent=2))
    except (ValueError, KeyError, IndexError, OSError) as error:
        parser.exit(2, str(error)+'\n')
