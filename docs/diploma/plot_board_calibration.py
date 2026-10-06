#!/usr/bin/env python3
"""Plot detected and reprojected corners on held-out Blender camera images."""
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
sys.path.insert(0, str(ROOT/'tools'))
from simulator import project


def plot(dataset, study, output):
    dataset, study = Path(dataset), Path(study)
    detections = json.loads((study/'detected-validation/observations.json').read_text())
    nominal = json.loads((dataset/'nominal-config.json').read_text())
    candidate = json.loads((study/'candidate/config.json').read_text())
    fig, axes = plt.subplots(2, 2, figsize=(13, 12), constrained_layout=True)
    for camera_id, ax in enumerate(axes.flat):
        record = detections['cameras'][camera_id]
        points = np.asarray(record['points'], dtype=float)
        pixels = np.asarray(record['pixels'], dtype=float)
        observed = np.asarray(Image.open(dataset/f'camera{camera_id}_0003.ppm').convert('RGB'))
        ax.imshow(observed)
        before = project(nominal['cameras'][camera_id], points)
        after = project(candidate['cameras'][camera_id], points)
        ax.scatter(pixels[:, 0], pixels[:, 1], s=20, facecolors='none',
                   edgecolors='#00d8ff', linewidths=1, label='Углы OpenCV')
        ax.scatter(before[:, 0], before[:, 1], s=20, marker='x', color='#ff3344',
                   linewidths=1, label='Nominal')
        ax.scatter(after[:, 0], after[:, 1], s=13, marker='+', color='#39ed66',
                   linewidths=1.2, label='После калибровки')
        ax.set_title(f'Камера {camera_id}')
        ax.set_xticks([])
        ax.set_yticks([])
        if camera_id == 0:
            ax.legend(loc='lower right', fontsize=8)
    fig.suptitle('Отложенный Blender кадр: реальные пиксели и проекции модели', fontsize=14)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=150)
    plt.close(fig)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dataset', default='artifacts/blender-board-v9')
    parser.add_argument('--study', default='artifacts/image-calibration-v2')
    parser.add_argument('--output', default='docs/diploma/figures/experiments/04_image_calibration.png')
    args = parser.parse_args()
    plot(args.dataset, args.study, args.output)
