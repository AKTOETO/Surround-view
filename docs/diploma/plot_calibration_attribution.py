"""Plot full-Jacobian camera-versus-board-pose response attribution."""
import argparse
import json
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

DIRECTIONS = ('shift_x', 'radial', 'tangential', 'random')
LABELS = ('shift x', 'radial', 'tangential', 'random fields')


def plot(results, output):
    output.mkdir(parents=True, exist_ok=True)
    data = json.loads((results / 'summary.json').read_text())['results']
    plt.rcParams.update({'font.size': 10, 'axes.grid': True, 'grid.alpha': .25})
    fig, axes = plt.subplots(1, 2, figsize=(12, 5), constrained_layout=True)
    rng = np.random.default_rng(8675309)
    for ax, source, title in (
            (axes[0], 'linearized_prediction', 'Full-Jacobian linear prediction'),
            (axes[1], 'actual_symmetric_derivative', 'Nonlinear symmetric refit')):
        for x, (direction, label) in enumerate(zip(DIRECTIONS, LABELS)):
            values = []
            for case in data:
                for pair in case['directions']:
                    if pair['direction'] != direction:
                        continue
                    if source == 'actual_symmetric_derivative' and not (
                            pair['positive']['success'] and pair['negative']['success']):
                        continue
                    values.append(pair[source]['intrinsics_squared_share'])
            jitter = rng.uniform(-.12, .12, len(values))
            ax.scatter(x + jitter, values, s=24, alpha=.72,
                       color='#2563a6' if source == 'linearized_prediction' else '#d05a31')
            ax.plot([x - .18, x + .18], [np.median(values)] * 2, color='#222222', lw=2)
            ax.text(x, 1.035, f'n={len(values)}', ha='center', va='bottom', fontsize=8)
        ax.set_xticks(range(len(DIRECTIONS)), LABELS)
        ax.set_ylim(-.04, 1.12)
        ax.set_ylabel('Squared response share assigned to intrinsics')
        ax.set_title(title)
    fig.suptitle('Equal-RMS UV perturbations: camera parameters versus nuisance poses')
    fig.savefig(output / 'calibration_component_attribution.png', dpi=180)
    plt.close(fig)

    complete = [pair for case in data for pair in case['directions']
                if pair['positive']['success'] and pair['negative']['success']]
    fig, ax = plt.subplots(figsize=(10, 5), constrained_layout=True)
    for index, direction in enumerate(DIRECTIONS):
        values = [100 * pair['linear_vs_actual_relative_l2_error'] for pair in complete
                  if pair['direction'] == direction]
        if values:
            jitter = rng.uniform(-.13, .13, len(values))
            ax.scatter(index + jitter, values, color='#5a6e3a', alpha=.75, s=26)
            ax.plot([index - .2, index + .2], [np.median(values)] * 2, color='#222222', lw=2)
            ax.text(index, max(values) * 1.06, f'n={len(values)}', ha='center', fontsize=8)
    ax.set_xticks(range(len(DIRECTIONS)), LABELS)
    ax.set_ylabel('Relative L2 difference from linear prediction (%)')
    ax.set_title('Linearization error at h = 0.05 px (converged signed pairs only)')
    ax.set_ylim(bottom=0)
    fig.savefig(output / 'calibration_component_linearization_error.png', dpi=180)
    plt.close(fig)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('docs/diploma/figures/experiments'))
    args = parser.parse_args()
    plot(args.results, args.output)
