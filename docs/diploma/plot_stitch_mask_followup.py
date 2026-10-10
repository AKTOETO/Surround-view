"""Plot measured RGB8 changes from the checked-in same-input regression report."""
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
    parser.add_argument('--report', type=Path, default=root / 'docs/validation/baselines/stitch_mask_followup_v2.json')
    parser.add_argument('--output', type=Path, default=Path(__file__).parent / 'figures/experiments/stitch_mask_followup_v2.png')
    args = parser.parse_args()
    data = json.loads(args.report.read_text())
    stats = data['rgb8_comparisons']
    labels = list(stats)
    modes = ('multi_band', 'graph_cut_multi_band')
    fig, axes = plt.subplots(1, 3, figsize=(14, 5))
    metrics = (('mean_abs_rgb8', 'Mean absolute RGB8 change'),
               ('max_rgb8', 'Maximum RGB8 channel change'),
               ('changed_pixel_percent', 'Changed pixels (%)'))
    x = np.arange(len(labels))
    for offset, mode in zip((-.18, .18), modes):
        for ax, (field, title) in zip(axes, metrics):
            ax.bar(x + offset, [stats[key][mode][field] for key in labels],
                   width=.35, label=mode)
            ax.set_title(title, fontsize=10)
    for ax in axes:
        ax.set_xticks(x, labels, rotation=40, ha='right', fontsize=8)
        ax.set_ylim(bottom=0)
        ax.grid(axis='y', alpha=.25)
    axes[0].legend(fontsize=8)
    fig.suptitle('Same-input mask-fix regression; RGB change is not truth error', fontsize=12)
    fig.tight_layout()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=160)
    plt.close(fig)


if __name__ == '__main__':
    main()
