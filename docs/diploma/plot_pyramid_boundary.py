"""Visualize center-row outputs recorded by the native GTest boundary study."""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path,
                        default=root/'docs/validation/baselines/pyramid_boundary_v1.json')
    parser.add_argument('--output', type=Path,
                        default=Path(__file__).parent/'figures/experiments/pyramid_boundary_v1.png')
    args = parser.parse_args()
    data = json.loads(args.report.read_text())
    if data['failures'] or data['errors']:
        raise ValueError('native study did not pass')
    tests = {test['name']: test for suite in data['testsuites'] for test in suite['testsuite']}
    profiles = json.loads(tests['ZeroExtensionHasNonzeroConstantFieldError']['profiles'])
    fig, axes = plt.subplots(2, 2, figsize=(12, 7))
    for row, mode in enumerate(('multi_band', 'graph_cut_multi_band')):
        selected = [p for p in profiles if p['mode'] == mode]
        x = np.arange(len(selected[0]['center_row_red']))
        axes[row, 0].axhline(.6, color='black', linestyle=':', label='constant truth = 0.6')
        axes[row, 0].axvspan(11, 21, color='gray', alpha=.15, label='two-camera overlap')
        for profile in selected:
            axes[row, 0].plot(x, profile['center_row_red'], label=profile['boundary'])
        axes[row, 0].set_title(mode + ': native center-row profile')
        axes[row, 0].set_ylabel('Linear red channel')
        axes[row, 0].set_ylim(.48, .75)
        axes[row, 0].legend(fontsize=8)
        tiles = np.array([p['center_row_red'] for p in selected])
        axes[row, 1].imshow(tiles, vmin=0, vmax=1, cmap='gray', aspect='auto',
                           interpolation='nearest')
        axes[row, 1].set_yticks([0, 1], [p['boundary'] for p in selected])
        axes[row, 1].set_title('Measured red channel; shared linear grayscale')
    for ax in axes.ravel():
        ax.set_xlabel('Output x coordinate')
    fig.suptitle('Identical observed colors: zero extension vs normalized support')
    fig.tight_layout()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=160)
    plt.close(fig)


if __name__ == '__main__':
    main()
