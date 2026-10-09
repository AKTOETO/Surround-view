"""Figures for the paired cube-face resolution/source-visibility study."""
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
    parser.add_argument('--results', type=Path, default=ROOT/'artifacts/stitch-resolution-v1')
    parser.add_argument('--fixture', type=Path, default=ROOT/'tests/data/stitch_resolution_v1')
    args = parser.parse_args()
    output = Path(__file__).parent/'figures/experiments'
    summary = json.loads((args.results/'summary.json').read_text())
    frame = 0
    with Image.open(args.fixture/'256'/f'virtual_{frame:04d}.png') as im:
        truth = np.asarray(im).copy()
    rendered = []
    for size in (64, 256):
        with Image.open(args.results/f'{size}-any'/f'edge_feather_{frame:04d}.png') as im:
            rendered.append(np.asarray(im).copy())
    bits = np.load(args.fixture/'256'/f'visibility_{frame:04d}.npy')
    labels = np.load(args.fixture/'256'/f'objects_{frame:04d}.npy')
    fig, axes = plt.subplots(2, 3, figsize=(12, 7))
    titles = ['Direct Blender RGB', 'edge_feather; cube faces 64x64', 'edge_feather; cube faces 256x256']
    for col, image in enumerate([truth, *rendered]):
        axes[0, col].imshow(image)
        axes[0, col].set_title(titles[col], fontsize=10)
    visibility = np.array([int(x).bit_count() for x in bits.ravel()]).reshape(bits.shape)
    visibility = np.ma.masked_where(labels == 0, visibility)
    heat = axes[1, 0].imshow(visibility, cmap='viridis', vmin=0, vmax=4)
    axes[1, 0].set_title('Source visibility\n(number of cameras, opaque rays)', fontsize=10)
    fig.colorbar(heat, ax=axes[1, 0], ticks=[0, 1, 2, 3, 4], shrink=.65)
    for col, image in enumerate(rendered, 1):
        error = np.abs(image.astype(float)-truth).mean(axis=-1)/255
        heat = axes[1, col].imshow(error, vmin=0, vmax=.25, cmap='magma')
        axes[1, col].set_title('Raw RGB error\n(ego/ROI exclusions not applied)', fontsize=10)
        fig.colorbar(heat, ax=axes[1, col], shrink=.65)
    for ax in axes.ravel():
        ax.axis('off')
    fig.tight_layout()
    fig.savefig(output/'stitch_resolution_views.png', dpi=140)
    plt.close(fig)
    modes = list(summary['results']['64']['any']['results'])
    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    x = np.arange(len(modes))
    for size, offset in ((64, -.18), (256, .18)):
        result = summary['results'][str(size)]['any']['results']
        mae = [np.mean([f['mae'] for f in result[m]['frames']]) for m in modes]
        delta = [np.mean([t['residual_change_mae'] for t in result[m]['transitions']]) for m in modes]
        axes[0].bar(x+offset, mae, width=.36, label=f'{size}x{size}')
        axes[1].bar(x+offset, delta, width=.36, label=f'{size}x{size}')
    for ax, title in zip(axes, ('Mean frame RGB MAE', 'Mean transition residual change')):
        ax.set_xticks(x, modes, rotation=55, ha='right', fontsize=8)
        ax.set_title(title+'; any-camera visibility ROI')
        ax.set_ylabel('sRGB [0,1] error')
        ax.grid(axis='y', alpha=.3)
        ax.legend()
    fig.tight_layout()
    fig.savefig(output/'stitch_resolution_metrics.png', dpi=140)
    plt.close(fig)


if __name__ == '__main__':
    main()
