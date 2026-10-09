"""Recorded distance/tilt raster inputs and factorial known-geometry metrics."""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
PROFILES = ('small_front', 'small_tilt', 'large_front', 'large_tilt')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path, default=ROOT / 'artifacts/calibration-capture-v1')
    args = parser.parse_args()
    report = json.loads((args.results / 'summary.json').read_text())
    output = Path(__file__).parent / 'figures/experiments'
    output.mkdir(parents=True, exist_ok=True)
    entry = report['results'][2]
    folder = args.results / f"{entry['family']}-{entry['seed']}"
    fig, axes = plt.subplots(2, 2, figsize=(10, 8))
    for name, ax in zip(PROFILES, axes.ravel()):
        with Image.open(folder / f'{name}-04.png') as im:
            ax.imshow(im, cmap='gray', vmin=0, vmax=255)
        row = next(p for p in entry['profiles'] if p['profile'] == name)
        d = row['peripheral_diagnostics']
        ax.set_title(f"{name}: distance={row['parameters']['distance_m']} m\n"
                     f"median cell edge={d['cell_edge_px']['median']:.2f} px; normal tilt={d['normal_to_center_ray_rad']['max']:.3f} rad")
        ax.axis('off')
    fig.suptitle(f"Same board center direction: {entry['family']} / {entry['seed']} / view 04\nCell-edge median covers all eight peripheral training views")
    fig.tight_layout()
    fig.savefig(output / 'calibration_capture_views.png', dpi=140)
    plt.close(fig)

    fig, axes = plt.subplots(2, 2, figsize=(14, 8))
    labels = [f"{e['family']}\n{e['seed']}" for e in report['results']]
    x = np.arange(len(labels))
    for row, order in enumerate((2, 4)):
        for col, key in enumerate(('outer', 'floor')):
            ax = axes[row, col]
            for i, name in enumerate(PROFILES):
                values = []
                for entry in report['results']:
                    profile = next(p for p in entry['profiles'] if p['profile'] == name)
                    fit = next(f for f in profile['fits'] if f['order'] == order)
                    m = fit.get('metric_validation')
                    values.append(np.nan if m is None else (m['ray_error_px']['outer']['p95'] if key == 'outer' else m['floor_error_m']['p95']))
                ax.bar(x + (i - 1.5) * .19, values, .18, label=name)
            ax.set_xticks(x, labels, fontsize=7)
            ax.set_ylabel('Known outer ray p95, px' if key == 'outer' else 'Known floor p95, m')
            ax.set_title(f'Order {order}; common true controls')
            ax.grid(axis='y', alpha=.3)
            ax.legend(fontsize=7)
    missing = sum('metric_validation' not in f for e in report['results'] for p in e['profiles'] for f in p['fits'])
    fig.suptitle(f'Four paired capture profiles, 12 training views each; missing estimates: {missing} (not zero errors)')
    fig.tight_layout()
    fig.savefig(output / 'calibration_capture_metrics.png', dpi=140)
    plt.close(fig)


if __name__ == '__main__':
    main()
