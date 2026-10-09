"""Recreate scene/exposure study figures; ranges describe three layout variants, not CI."""
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
    parser.add_argument('--results', type=Path, default=ROOT/'artifacts/stitch-robustness-v2')
    parser.add_argument('--inputs', type=Path, default=ROOT/'artifacts/stitch-validation-v1')
    args = parser.parse_args()
    output = Path(__file__).parent/'figures/experiments'
    report = json.loads((args.results/'summary.json').read_text())
    seeds = list(report['results'])
    fig, axes = plt.subplots(len(seeds), 3, figsize=(12, 7), squeeze=False)
    for row, seed in enumerate(seeds):
        paths = [args.inputs/f'seed{seed}-capture/virtual_0001.png',
                 args.results/f'seed{seed}-nominal/edge_feather_0001.png',
                 args.results/f'seed{seed}-front_jump/edge_feather_0001.png']
        titles = ['Direct Blender RGB', 'edge_feather nominal', 'edge_feather front -0.5 EV']
        for col, (path, title) in enumerate(zip(paths, titles)):
            with Image.open(path) as image:
                axes[row, col].imshow(image)
            axes[row, col].set_title(f'seed {seed}: {title}', fontsize=9)
            axes[row, col].axis('off')
    fig.tight_layout()
    fig.savefig(output/'stitch_robustness_views.png', dpi=140)
    plt.close(fig)
    modes = list(report['results'][seeds[0]]['nominal']['results'])
    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    for condition, offset in zip(('nominal', 'static_bias', 'front_jump'), (-.25, 0, .25)):
        mae, change = [], []
        for seed in seeds:
            results = report['results'][seed][condition]['results']
            mae.append([np.mean([f['mae'] for f in results[m]['frames']]) for m in modes])
            change.append([np.mean([t['residual_change_mae'] for t in results[m]['transitions']]) for m in modes])
        for ax, values in zip(axes, (mae, change)):
            values = np.asarray(values)
            mean = values.mean(axis=0)
            spread = [mean-values.min(axis=0), values.max(axis=0)-mean]
            ax.bar(np.arange(len(modes))+offset, mean, width=.24, yerr=spread, capsize=2, label=condition)
    for ax, title in zip(axes, ('Mean RGB MAE', 'Mean residual change')):
        ax.set_xticks(np.arange(len(modes)), modes, rotation=55, ha='right', fontsize=8)
        ax.set_title(title+'; bars: mean, whiskers: min/max layouts', fontsize=10)
        ax.set_ylabel('sRGB [0,1] error')
        ax.legend(fontsize=8)
        ax.grid(axis='y', alpha=.3)
    fig.tight_layout()
    fig.savefig(output/'stitch_robustness_metrics.png', dpi=140)
    plt.close(fig)


if __name__ == '__main__':
    main()
