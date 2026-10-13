"""Illustrate compiled fallback display; Python prepares fixture and plots native RGBA only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from simulator import generate
OUT = ROOT / 'artifacts/fallback-display-example-v1'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture():
    OUT.mkdir(exist_ok=False)
    config = json.loads((ROOT / 'configs/synthetic.json').read_text())
    config['output'].update(width=320, height=180)
    # Narrow input FoV creates visible fallback alongside ordinary sampled ground.
    for camera in config['cameras']:
        camera['projection']['theta_max_rad'] = .9
    manifest = generate(OUT / 'inputs', config, 1)
    env = dict(os.environ, __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json',
               LIBGL_ALWAYS_SOFTWARE='1', EGL_PLATFORM='surfaceless')
    for carrier in ['bowl', 'dome']:
        if carrier == 'dome':
            config['surface'] = dict(type='dome_floor_v1', dome_radius_m=12,
                dome_latitude_cells=32, dome_longitude_cells=64, floor_radial_cells=16)
        for mode in ['legacy', 'hatch']:
            config['fusion'] = dict(mode='seam_distance_feather')
            if mode == 'hatch':
                config['fusion']['fallback_display'] = dict(mode=mode)
            path = OUT / f'{carrier}-{mode}.json'
            path.write_text(json.dumps(config, indent=2)+'\n')
            subprocess.run([str(ROOT / 'build/sv-render-fusion-probe'), str(path), str(manifest),
                str(OUT / f'{carrier}-{mode}'), '--color-only'], check=True, env=env)


def plot():
    fig, axes = plt.subplots(2, 2, figsize=(11, 6), layout='constrained')
    records = []
    for y, carrier in enumerate(['bowl', 'dome']):
        for x, mode in enumerate(['legacy', 'hatch']):
            folder = OUT / f'{carrier}-{mode}'
            index_path = folder / 'index.json'
            index = json.loads(index_path.read_text())
            cfg = OUT / f'{carrier}-{mode}.json'
            if sha(cfg) != index['config_sha256']:
                raise ValueError('config changed after native capture')
            raw = folder / 'seam_distance_feather-color/actual.rgba'
            rgba = np.fromfile(raw, np.uint8).reshape(index['height'], index['width'], 4)
            axes[y, x].imshow(rgba)
            axes[y, x].set_title(f'{carrier} / {mode}')
            axes[y, x].axis('off')
            records.append(dict(carrier=carrier, mode=mode, device=index['device'],
                source_fingerprint=index['source_fingerprint'],
                sha256={str(p.relative_to(ROOT)): sha(p) for p in [cfg, index_path, raw]},
                manifest_sha256=index['manifest_sha256'], inputs=index['inputs']))
    fig.suptitle('Native fallback display; synthetic fixture, not a scene-correspondence test')
    picture = ROOT / 'docs/diploma/figures/experiments/fallback_display_example.png'
    fig.savefig(picture, dpi=160)
    plt.close(fig)
    report = dict(schema_version=1, scope='illustrative_native_fallback_display_not_quality_trial',
        script_sha256=sha(Path(__file__)), figure_sha256=sha(picture), records=records)
    (ROOT / 'docs/validation/baselines/fallback_display_example_v1.json').write_text(
        json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', action='store_true')
    if parser.parse_args().capture:
        capture()
    plot()
