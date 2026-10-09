"""Render the full-joint calibration information figures from a study summary."""
import argparse
import json
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def plot(results, output):
    output.mkdir(parents=True, exist_ok=True)
    data = json.loads((results / 'summary.json').read_text())['results']
    plt.rcParams.update({'font.size': 10, 'axes.grid': True, 'grid.alpha': .25})

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.8), constrained_layout=True)
    for ax, order in zip(axes, (2, 4)):
        for origin, color, label in (('analytic_truth', '#2563a6', 'Exact synthetic corners'),
                                     ('detected_raster', '#d05a31', 'OpenCV detected corners')):
            curves = []
            for row in data:
                if row['distortion_order'] != order or row['observation_source'] != origin:
                    continue
                singular = np.asarray(row['singular_values'])
                curves.append(singular / singular[0])
            matrix = np.asarray(curves)
            x = np.arange(1, matrix.shape[1] + 1)
            median = np.median(matrix, axis=0)
            low, high = np.percentile(matrix, [25, 75], axis=0)
            ax.plot(x, median, color=color, label=label)
            ax.fill_between(x, low, high, color=color, alpha=.18)
        ax.set_yscale('log')
        ax.set_xlabel('Singular-value index, descending')
        ax.set_ylabel(r'Normalized singular value $\sigma_i/\sigma_1$')
        ax.set_title(f'KB polynomial order {order}')
        ax.legend(frameon=True)
    fig.suptitle('Joint intrinsic and per-view pose Jacobian spectrum (12 matched cases)')
    fig.savefig(output / 'joint_calibration_singular_spectrum.png', dpi=180)
    fig.savefig(output / 'joint_calibration_singular_spectrum.svg')
    plt.close(fig)

    categories = [('small_front', 2), ('large_front', 2),
                  ('small_front', 4), ('large_front', 4)]
    names = ['fx (px)', 'fy (px)', 'cx (px)', 'cy (px)']
    fig, axes = plt.subplots(1, 4, figsize=(15, 5.4), sharey=True, constrained_layout=True)
    y = np.arange(len(categories))
    for index, (ax, parameter) in enumerate(zip(axes, names)):
        for shift, key, color, label in ((-.14, 'detector_truth_rms_iid', '#2563a6', 'IID scaled by detector RMS'),
                                         (.14, 'cluster_sandwich_intrinsics', '#d05a31', 'View-cluster sandwich')):
            medians, lower, upper = [], [], []
            for profile, order in categories:
                values = []
                for row in data:
                    if (row['observation_source'] != 'detected_raster' or row['profile'] != profile
                            or row['distortion_order'] != order):
                        continue
                    block = (row['intrinsics_covariance_by_sigma_source'][key]
                             if key == 'detector_truth_rms_iid' else row[key])
                    values.append(block['standard_errors'][index])
                values = np.asarray(values)
                medians.append(np.median(values))
                lower.append(np.median(values) - np.percentile(values, 25))
                upper.append(np.percentile(values, 75) - np.median(values))
            ax.errorbar(medians, y + shift, xerr=np.asarray([lower, upper]), fmt='o',
                        capsize=3, color=color, label=label)
        ax.set_title(parameter)
        ax.set_xlabel('Local standard error (pixels)')
        if index == 0:
            ax.set_yticks(y, [f'{profile.replace("_", " ")}, order {order}'
                              for profile, order in categories])
            ax.legend(fontsize=8, loc='lower right')
        else:
            ax.tick_params(labelleft=False)
    fig.suptitle('Detected-corner intrinsic uncertainty: median and interquartile range')
    fig.savefig(output / 'joint_calibration_intrinsic_uncertainty.png', dpi=180)
    fig.savefig(output / 'joint_calibration_intrinsic_uncertainty.svg')
    plt.close(fig)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('docs/diploma/figures/experiments'))
    args = parser.parse_args()
    plot(args.results, args.output)
