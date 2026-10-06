#!/usr/bin/env python3
"""Plot measured calibration results and actual Blender/GLES images."""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image


def plot(study, capture, output):
    report = json.loads((study/'report.json').read_text())
    output.mkdir(parents=True, exist_ok=True)
    cases = [('nominal', 'Исходная номинальная калибровка'), ('truth', 'Истинные позы — reference'),
             ('iterative', 'ITERATIVE'), ('epnp', 'EPnP'), ('sqpnp', 'SQPnP'),
             ('ransac_epnp_lm', 'RANSAC + EPnP + LM')]
    fig, axes = plt.subplots(3, 2, figsize=(13, 12), constrained_layout=True)
    for axis, (method, title) in zip(axes.flat, cases):
        image = study/'images'/method/'render'/'preview.ppm'
        if image.exists():
            axis.imshow(Image.open(image))
        else:
            axis.set_facecolor('#eeeeee')
            axis.text(.5, .5, 'Нет экспортированного результата\nточки вышли из области валидности',
                      ha='center', va='center', transform=axis.transAxes, color='#aa2222')
        axis.set_title(title)
        axis.set_xticks([])
        axis.set_yticks([])
    fig.suptitle('Actual GLES: trial 0, σ = 0.5 px, 10% выбросов; один Blender-набор', fontsize=14)
    fig.savefig(output/'04_mount_images.png', dpi=130)
    plt.close(fig)
    fig, axes = plt.subplots(1, 2, figsize=(13, 5), constrained_layout=True)
    methods = ['iterative', 'epnp', 'sqpnp', 'ransac_epnp_lm']
    for axis, fraction in zip(axes, [0., .1]):
        for method in methods:
            rows = [r for r in report['summary'] if r['method'] == method and r['outlier_fraction'] == fraction]
            axis.plot([r['sigma_px'] for r in rows], [r['validation_rmse_px_median'] for r in rows],
                      marker='o', label=method)
            for r in rows:
                if r['successful_trials'] < r['trials']:
                    axis.annotate(f'{r["successful_trials"]}/{r["trials"]}',
                                  (r['sigma_px'], r['validation_rmse_px_median']))
        axis.set_title(f'Выбросы: {100*fraction:.0f}%')
        axis.set_xlabel('σ обучающего шума, px')
        axis.set_ylabel('Медиана held-out RMSE, px')
        axis.grid(alpha=.25)
        axis.legend(fontsize=8)
    fig.suptitle('5 mounting seeds × 4 камеры; только успешные fits, отказы указаны отдельно')
    fig.savefig(output/'04_mount_errors.png', dpi=140)
    plt.close(fig)
    with Image.open(capture/'overview.png') as image:
        image.save(output/'04_mount_world.png')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--study', required=True, type=Path)
    parser.add_argument('--capture', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=Path(__file__).parent/'figures/experiments')
    args = parser.parse_args()
    plot(args.study, args.capture, args.output)
