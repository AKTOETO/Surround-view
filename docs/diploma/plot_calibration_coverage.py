"""Actual peripheral captures and paired known-geometry errors, no pooled score."""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path, default=ROOT / 'artifacts/calibration-coverage-v1')
    args = parser.parse_args()
    report = json.loads((args.results / 'summary.json').read_text())
    output = Path(__file__).parent / 'figures/experiments'
    output.mkdir(parents=True, exist_ok=True)
    entry = report['results'][2]  # kb_nonzero / first seed
    folder = args.results / f"{entry['family']}-{entry['seed']}"
    fig, axes = plt.subplots(2, 3, figsize=(12, 7))
    for ax, name in zip(axes[0], ('central_train-00.png', 'outer_train-04.png', 'outer_validation-04.png')):
        with Image.open(folder / name) as im:
            ax.imshow(im, cmap='gray', vmin=0, vmax=255)
        ax.set_title(name)
        ax.axis('off')
    for p, color in zip(entry['profiles'], ('tab:blue', 'tab:orange')):
        c = p['train_coverage']
        edges = np.array(c['bins_rad'])
        axes[1, 0].stairs(c['counts'], edges, label=p['profile'], color=color)
    axes[1, 0].set(xlabel='True corner angle theta, rad', ylabel='Training corners / bin')
    axes[1, 0].legend()
    for ax, group in zip(axes[1, 1:], ('central_validation', 'outer_validation')):
        for pose in entry['geometry'][group]:
            t = np.array(pose['translation'])
            theta = np.arctan2(np.linalg.norm(t[:2]), t[2])
            phi = np.arctan2(t[1], t[0])
            ax.scatter(theta * np.cos(phi), theta * np.sin(phi), s=45)
        ax.set(xlabel='theta cos(phi), rad', ylabel='theta sin(phi), rad', title=group, xlim=(-1.1, 1.1), ylim=(-1.1, 1.1))
        ax.set_aspect('equal')
        ax.grid(alpha=.3)
    fig.suptitle(f"Native-detector inputs and measured coverage: {entry['family']} / {entry['seed']}")
    fig.tight_layout()
    fig.savefig(output / 'calibration_coverage_views.png', dpi=145)
    plt.close(fig)

    fig, axes = plt.subplots(2, 2, figsize=(13, 8))
    labels = [f"{e['family']}\n{e['seed']}" for e in report['results']]
    x = np.arange(len(labels))
    for row, order in enumerate((2, 4)):
        for col, metric in enumerate(('outer', 'floor')):
            ax = axes[row, col]
            for profile, offset, color in (('central', -.16, 'tab:blue'), ('wide', .16, 'tab:orange')):
                values = []
                for entry in report['results']:
                    p = next(p for p in entry['profiles'] if p['profile'] == profile)
                    f = next(f for f in p['fits'] if f['order'] == order)
                    m = f.get('metric_validation')
                    values.append(np.nan if m is None else (m['ray_error_px']['outer']['p95'] if metric == 'outer' else m['floor_error_m']['p95']))
                ax.bar(x + offset, values, .3, label=profile, color=color)
            ax.set_xticks(x, labels, fontsize=7)
            ax.set_title(f'Order {order} / common true geometry')
            ax.set_ylabel('Outer ray p95, px' if metric == 'outer' else 'Known floor p95, m')
            ax.grid(axis='y', alpha=.3)
            ax.legend()
    fig.suptitle('Same 12-view budget; central-gate exported fits; all 32 models available')
    fig.tight_layout()
    fig.savefig(output / 'calibration_coverage_metrics.png', dpi=145)
    plt.close(fig)


if __name__ == '__main__':
    main()
