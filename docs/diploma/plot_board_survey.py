#!/usr/bin/env python3
"""Plot held-out sensitivity to calibration-board pose survey error."""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = json.loads(args.report.read_text())
    rows = report['summary']
    translations = sorted({row['translation_sigma_mm'] for row in rows})
    rotations = sorted({row['rotation_sigma_deg'] for row in rows})
    values = {}
    for metric in ('median_validation_rmse_px', 'median_center_error_mm'):
        values[metric] = np.array([[next(row[metric] for row in rows
            if row['translation_sigma_mm'] == translation and row['rotation_sigma_deg'] == rotation)
            for rotation in rotations] for translation in translations])

    fig, axes = plt.subplots(1, 2, figsize=(12.4, 5.2), constrained_layout=True)
    panels = ((axes[0], values['median_validation_rmse_px'], 'Held-out reprojection RMSE (px)', 'magma'),
              (axes[1], values['median_center_error_mm'], 'Camera-center error (mm)', 'viridis'))
    for axis, matrix, title, palette in panels:
        image = axis.imshow(matrix, cmap=palette, aspect='auto')
        axis.set_title(title)
        axis.set_xlabel('Board-pose rotation measurement σ (degrees)')
        axis.set_ylabel('Board-pose translation measurement σ (mm)')
        axis.set_xticks(range(len(rotations)), [f'{value:g}' for value in rotations])
        axis.set_yticks(range(len(translations)), [f'{value:g}' for value in translations])
        cutoff = np.nanmin(matrix) + .62 * (np.nanmax(matrix) - np.nanmin(matrix))
        for y in range(matrix.shape[0]):
            for x in range(matrix.shape[1]):
                color = 'black' if matrix[y, x] > cutoff else 'white'
                axis.text(x, y, f'{matrix[y, x]:.2f}', ha='center', va='center', color=color, fontsize=9)
        fig.colorbar(image, ax=axis, shrink=.82)
    fig.suptitle(f"E-CAL-SURVEY-01 — {report['repeats']} seeded repeats per condition")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=180)
    plt.close(fig)


if __name__ == '__main__':
    main()
