"""Recreate the paired-street experiment figures from its checked inputs and run output."""
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
    parser.add_argument('--results', type=Path, default=ROOT/'artifacts/paired-temporal-v3')
    parser.add_argument('--fixture', type=Path, default=ROOT/'tests/data/paired_street_v1')
    args = parser.parse_args()
    output = Path(__file__).parent/'figures'/'experiments'
    output.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(3, 3, figsize=(12, 7))
    modes = ['edge_feather', 'graph_cut_seam']
    for t in range(3):
        paths = [args.fixture/f'virtual_{t:04d}.png'] + [args.results/f'{m}_{t:04d}.png' for m in modes]
        for col, path in enumerate(paths):
            with Image.open(path) as image:
                axes[t, col].imshow(image)
            axes[t, col].axis('off')
            axes[t, col].set_title(f'{["Direct Blender view", *modes][col]}; x={t*.4:.1f} m')
    fig.tight_layout()
    fig.savefig(output/'paired_street_views.png', dpi=140)
    plt.close(fig)
    report = json.loads((args.results/'temporal_stability.json').read_text())
    fig, axes = plt.subplots(1, 2, figsize=(12, 4))
    for mode, result in report['results'].items():
        transitions = result['transitions']
        axes[0].plot([1, 2], [r['residual_change_mae'] for r in transitions], 'o-', label=mode)
        axes[1].plot([1, 2], [r['seam_motion']['mean_distance_px'] for r in transitions], 'o-', label=mode)
    axes[0].set_ylabel('Residual change MAE [sRGB 0..1]')
    axes[1].set_ylabel('Symmetric boundary distance [px]')
    for ax in axes:
        ax.set_xlabel('Frame transition (0→1, 1→2)')
        ax.set_xticks([1, 2])
        ax.grid(alpha=.3)
    axes[0].legend(fontsize=7)
    fig.tight_layout()
    fig.savefig(output/'paired_street_temporal.png', dpi=140)
    plt.close(fig)


if __name__ == '__main__':
    main()
