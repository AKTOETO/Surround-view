#!/usr/bin/env python3
"""Plot saved dense analytic results; generates no world or ground truth."""
import argparse
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image


def plot(reference, output, view):
    names = [('plane', 'Плоскость'), ('bowl', 'Bowl'), ('dome', 'Купол + пол'),
             ('cylinder', 'Цилиндр + пол'), ('cube', 'Куб + пол')]
    figure, axes = plt.subplots(5, 2, figsize=(12, 15), constrained_layout=True)
    for row, (name, label) in enumerate(names):
        folder = reference/f'{name}-{view}-edge_feather'
        axes[row, 0].imshow(Image.open(folder/'reference.png'))
        axes[row, 0].set_title(label+' — аналитическая поверхность')
        with np.load(folder/'reference.npz') as data:
            coverage = np.ma.masked_where(~data['evaluation'], data['coverage'])
            picture = axes[row, 1].imshow(coverage, vmin=0, vmax=4, cmap='viridis', interpolation='nearest')
        axes[row, 1].set_title('Число допустимых проекций; серое — вне ROI')
        axes[row, 1].set_facecolor('#bbbbbb')
        for axis in axes[row]:
            axis.set_xticks([])
            axis.set_yticks([])
    figure.colorbar(picture, ax=axes[:, 1], ticks=range(5), shrink=.65, label='Камеры')
    figure.suptitle(f'CPU reference: {view}, кадр 0, edge-feather; без кузова и scene depth truth', fontsize=14)
    output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output, dpi=130)
    plt.close(figure)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path(__file__).parent/'figures/experiments/04_analytic_reference.png')
    parser.add_argument('--view', choices=['low', 'oblique'], default='low')
    args = parser.parse_args()
    plot(args.reference, args.output, args.view)
