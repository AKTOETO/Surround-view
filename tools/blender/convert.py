#!/usr/bin/env python3
"""Convert Blender perspective cube captures into calibrated fisheye replay inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from simulator import camera_rays
from rig import cube_coordinates


def linear_rgb(image):
    value = image.astype(np.float64) / 255
    return np.where(value <= .04045, value / 12.92, ((value + .055) / 1.055) ** 2.4)


def srgb8(image):
    value = np.clip(image, 0, 1)
    encoded = np.where(value <= .0031308, value * 12.92, 1.055 * value ** (1/2.4) - .055)
    return np.rint(encoded * 255).astype(np.uint8)


def bilinear(image, uv):
    """Sample at top-left pixel centers, clamping both coordinates to the edge."""
    h, w = image.shape[:2]
    x = np.clip(uv[..., 0], 0, w-1)
    y = np.clip(uv[..., 1], 0, h-1)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    x1, y1 = np.minimum(x0+1, w-1), np.minimum(y0+1, h-1)
    tx, ty = (x-x0)[..., None], (y-y0)[..., None]
    return ((1-ty)*((1-tx)*image[y0,x0]+tx*image[y0,x1])
            + ty*((1-tx)*image[y1,x0]+tx*image[y1,x1]))


def convert_camera(cam, faces, size):
    """Sample in linear light. At cube edges, clamp to the last texel center.

    This is not seamless cross-face filtering; error decreases with face size.
    Pixels outside the calibrated theta bound are explicitly black.
    """
    rays, theta = camera_rays(cam)
    valid = theta <= cam['projection']['theta_max_rad']
    result = np.zeros((*theta.shape, 3), np.float64)
    for name, (mask, uv) in cube_coordinates(rays, size).items():
        mask &= valid
        if not np.any(mask):
            continue
        if name not in faces or faces[name].shape != (size, size, 3):
            raise ValueError(f'missing or incorrectly sized cube face {name}')
        values = bilinear(linear_rgb(faces[name]), uv)
        result[mask] = values[mask]
    return srgb8(result)


def convert(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    metadata = json.loads((source / 'capture.json').read_text())
    if metadata['schema_version'] != 1 or not metadata['frames']:
        raise ValueError('capture v1 with at least one frame required')
    # Validate all input files before creating output or publishing a manifest.
    for name, expected in metadata['sha256'].items():
        path = (source / name).resolve()
        if not path.is_relative_to(source) or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f'unsafe path or capture checksum mismatch: {name}')
    cfg = metadata['config']
    if [cam['id'] for cam in cfg['cameras']] != list(range(4)):
        raise ValueError('camera IDs must be ordered 0..3')
    output.mkdir(parents=True, exist_ok=False)
    rows, hashes = [], {}
    for index, row in enumerate(metadata['frames']):
        if [cam['id'] for cam in row['cameras']] != list(range(4)):
            raise ValueError('capture camera IDs must be ordered 0..3')
        paths = []
        for cam, capture in zip(cfg['cameras'], row['cameras']):
            faces = {}
            for face, filename in capture['faces'].items():
                if filename not in metadata['sha256']:
                    raise ValueError(f'unhashed face: {filename}')
                with Image.open(source / filename) as image:
                    faces[face] = np.array(image.convert('RGB'))
            image = convert_camera(cam, faces, metadata['face_size'])
            filename = f"camera{cam['id']}_{index:04d}.ppm"
            encoded = f'P6\n{image.shape[1]} {image.shape[0]}\n255\n'.encode() + image.tobytes()
            (output / filename).write_bytes(encoded)
            hashes[filename] = hashlib.sha256(encoded).hexdigest()
            paths.append(filename)
        rows.append({'scenario_timestamp_ns':row['scenario_timestamp_ns'],
                     'paths':paths, 'offset_ns':[0]*4})
    (output / 'config.json').write_text(json.dumps(cfg,indent=2)+'\n')
    truth = {key:metadata[key] for key in ('origin','fps','frames','blender_version','engine',
                                         'view_transform','script_sha256','limitations')}
    truth['capture_sha256'] = hashlib.sha256((source / 'capture.json').read_bytes()).hexdigest()
    truth['optics_check'] = metadata.get('optics_check')
    truth['converter_sha256'] = {
        path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in
        (Path(__file__),Path(__file__).with_name('rig.py'),
         Path(__file__).resolve().parents[1]/'simulator.py')}
    (output / 'ground_truth.json').write_text(json.dumps(truth,indent=2)+'\n')
    manifest = {'schema_version':1, 'origin':metadata['origin'],
                'calibration_ids':[c['calibration_id'] for c in cfg['cameras']],
                'frames':rows, 'sha256':hashes}
    (output / 'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return output / 'manifest.json'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    print(convert(args.capture,args.output))
