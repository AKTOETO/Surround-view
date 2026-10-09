"""Measured localization fields and exact/float32/detected calibration controls."""
import argparse
import json
from pathlib import Path
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from calibration.raster_study import digest

ORIGINS = ('analytic_truth', 'analytic_truth_float32', 'detected_raster')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path, default=ROOT / 'artifacts/calibration-diagnostic-v2')
    parser.add_argument('--input', type=Path, default=ROOT / 'artifacts/calibration-capture-v1')
    args = parser.parse_args()
    report = json.loads((args.results / 'summary.json').read_text())
    output = Path(__file__).parent / 'figures/experiments'
    output.mkdir(parents=True, exist_ok=True)
    entry = next(e for e in report['results'] if e['family'] == 'kb_nonzero' and e['seed'] == 7101)
    case = f"{entry['family']}-{entry['seed']}"
    fig, axes = plt.subplots(2, 2, figsize=(12, 8))
    for ax, name in zip(axes[0], ('small_front', 'large_front')):
        row = next(p for p in entry['profiles'] if p['profile'] == name)
        data = {}
        for origin in ('analytic_truth', 'detected_raster'):
            path = args.results / case / f'{name}-{origin}.json'
            if digest(path) != row['dataset_sha256'][origin]:
                raise ValueError('diagnostic point input hash mismatch')
            data[origin] = json.loads(path.read_text())
        filename = f'{name}-04.png'
        uv, truth = (np.array(next(v for v in data[o]['train'] if v['id'] == filename)['uv_px'])
                     for o in ('detected_raster', 'analytic_truth'))
        path = args.input / case / filename
        if digest(path) != report['input_sha256'][f'{case}/{filename}']:
            raise ValueError('parent image hash mismatch')
        with Image.open(path) as im:
            ax.imshow(im, cmap='gray', vmin=0, vmax=255)
        ax.scatter(truth[:, 0], truth[:, 1], s=5, c='cyan', label='true corner')
        ax.quiver(truth[:, 0], truth[:, 1], (uv-truth)[:, 0]*20, (uv-truth)[:, 1]*20,
                  angles='xy', scale_units='xy', scale=1, color='red', width=.004, label='error x20')
        ax.set(xlim=(truth[:, 0].min()-15, truth[:, 0].max()+15),
               ylim=(truth[:, 1].max()+15, truth[:, 1].min()-15), title=f'{name} / view04; arrows x20')
        ax.legend(fontsize=8)
    groups = ('central_train', 'small_front', 'small_tilt', 'large_front', 'large_tilt')
    for ax, key in zip(axes[1], ('rmse_px', 'mean_uv_px')):
        values = [[float(v[key]) if key == 'rmse_px' else float(np.linalg.norm(v[key]))
                   for e in report['results'] for v in e['detector_localization'] if v['group'] == g]
                  for g in groups]
        ax.boxplot(values, tick_labels=groups, showmeans=True)
        ax.tick_params(axis='x', labelsize=8)
        ax.set_ylabel('Per-view localization RMSE, px' if key == 'rmse_px' else 'Norm of per-view mean signed UV error, px')
        ax.grid(axis='y', alpha=.3)
    fig.suptitle('Raster/detector localization: actual recorded corners, no synthetic jitter')
    fig.tight_layout()
    fig.savefig(output / 'intrinsic_diagnostic_localization.png', dpi=140)
    plt.close(fig)

    families = list(dict.fromkeys(e['family'] for e in report['results']))
    fig, axes = plt.subplots(2, 2, figsize=(12, 8))
    x = np.arange(len(families))
    for row, order in enumerate((2, 4)):
        for col, key in enumerate(('outer', 'floor')):
            ax = axes[row, col]
            for i, origin in enumerate(ORIGINS):
                values = []
                for family in families:
                    fits = [f for e in report['results'] if e['family'] == family for p in e['profiles'] for f in p['fits']
                            if f['order'] == order and f['origin'] == origin and 'metric_validation' in f]
                    values.append(max((f['metric_validation']['ray_error_px']['outer']['p95'] if key == 'outer'
                                       else f['metric_validation']['floor_error_m']['p95'] for f in fits), default=np.nan))
                ax.bar(x + (i-1)*.24, values, .23, label=origin)
            ax.set_xticks(x, families, fontsize=8)
            ax.set_yscale('log')
            ax.set_ylabel('Max case outer-ray p95, px' if key == 'outer' else 'Max case floor p95, m')
            ax.set_title(f'Order {order}; max over 2 seeds x 4 profiles')
            ax.grid(axis='y', alpha=.3)
            ax.legend(fontsize=7)
    fig.suptitle('Observed-input error versus exact and UV float32 controls; not pooled quantiles')
    fig.tight_layout()
    fig.savefig(output / 'intrinsic_diagnostic_controls.png', dpi=140)
    plt.close(fig)


if __name__ == '__main__':
    main()
