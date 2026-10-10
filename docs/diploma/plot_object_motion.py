"""Plot per-frame coded-object IoU and source-ID support for E-STITCH-object-motion-01."""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--output', type=Path,
                        default=Path(__file__).parent/'figures/experiments/object_stitch_motion_metrics.png')
    args = parser.parse_args()
    summary = json.loads((args.results/'summary.json').read_text())
    truth = json.loads((args.capture/'paired_truth.json').read_text())
    positions = [row['diagnostic_target_position_m'][1] for row in truth['frames']]
    rows = summary['results']
    labels = sorted({(row['carrier'], row['mode']) for row in rows})
    matrix = np.asarray([[next(item['target']['iou'] for item in rows
                                if item['carrier'] == carrier and item['mode'] == mode
                                and item['frame'] == frame)
                         for frame in range(len(positions))]
                        for carrier, mode in labels])
    support = np.asarray([[next(item['source_identity_support']['outside_support_fraction']
                                for item in rows if item['carrier'] == carrier
                                and item['mode'] == mode and item['frame'] == frame)
                          for frame in range(len(positions))]
                         for carrier, mode in labels])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(2, 1, figsize=(13, 12), sharex=True,
                             gridspec_kw={'height_ratios': [1, 1]})
    y = np.arange(len(labels)+1)
    x = np.arange(len(positions)+1)
    image = axes[0].pcolormesh(x, y, matrix, vmin=0, vmax=1, cmap='viridis', shading='flat')
    axes[0].set_yticks(np.arange(len(labels))+.5,
                       [f'{carrier} / {mode}' for carrier, mode in labels], fontsize=7)
    axes[0].invert_yaxis()
    axes[0].set_title('RGB coded-target IoU by frame, carrier and fusion mode')
    fig.colorbar(image, ax=axes[0], label='IoU (higher means closer target-mask agreement)')
    image2 = axes[1].pcolormesh(x, y, support, vmin=0, vmax=1, cmap='magma', shading='flat')
    axes[1].set_yticks(np.arange(len(labels))+.5,
                       [f'{carrier} / {mode}' for carrier, mode in labels], fontsize=7)
    axes[1].invert_yaxis()
    axes[1].set_title('Fraction of projected source-ID support outside direct-view truth')
    fig.colorbar(image2, ax=axes[1], label='Outside-support fraction (lower means less unsupported mass)')
    axes[1].set_xticks(np.arange(len(positions))+.5,
                       [f'{i}\ny={pos:+.1f} m' for i, pos in enumerate(positions)])
    axes[1].set_xlabel('Synchronized sample index and target lateral position')
    fig.suptitle('E-STITCH-object-motion-01 · one scripted cuboid path; descriptive synthetic screen', y=.995)
    fig.tight_layout()
    fig.savefig(args.output, dpi=170)
    plt.close(fig)
    print(args.output)


if __name__ == '__main__':
    main()
