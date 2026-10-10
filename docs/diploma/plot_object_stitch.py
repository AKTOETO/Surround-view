"""Controlled-target RGB and mask comparison; no natural-object recognition implied."""
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
    parser.add_argument('--results', type=Path, default=ROOT/'artifacts/object-stitch-study-v2')
    parser.add_argument('--fixture', type=Path, default=ROOT/'tests/data/object_stitch_v1')
    args = parser.parse_args()
    meta = json.loads((args.fixture/'paired_truth.json').read_text())
    target_id = meta['objects'][meta['diagnostic_target']['object_name']]
    output = Path(__file__).parent/'figures/experiments'
    fig, axes = plt.subplots(2, 3, figsize=(12, 6))
    with Image.open(args.fixture/'virtual_0000.png') as image:
        direct = np.asarray(image).copy()
    ids = np.load(args.fixture/'objects_0000.npy')
    paths = [None, args.results/'dome_floor_edge_feather_0000.png',
             args.results/'dome_floor_graph_cut_seam_0000.png']
    masks = [ids == target_id]
    arrays = [direct]
    for path in paths[1:]:
        with Image.open(path) as image:
            arrays.append(np.asarray(image).copy())
        with Image.open(path.with_name(path.stem+'_mask.png')) as image:
            masks.append(np.asarray(image) > 0)
    titles = ['Direct RGB / exact target ID', 'dome+floor / edge feather', 'dome+floor / binary cut']
    for col in range(3):
        axes[0,col].imshow(arrays[col])
        axes[0,col].set_title(titles[col], fontsize=10)
        axes[1,col].imshow(masks[col], cmap='gray', vmin=0,vmax=1)
        axes[1,col].contour(ids == target_id, levels=[.5], colors=['lime'], linewidths=.7)
        axes[1,col].set_title('White: target support; green: exact truth contour', fontsize=8)
    for ax in axes.ravel():
        ax.axis('off')
    fig.tight_layout()
    fig.savefig(output/'object_stitch_masks.png', dpi=140)
    plt.close(fig)
    report = json.loads((args.results/'summary.json').read_text())
    fig, ax = plt.subplots(figsize=(10,4))
    modes = list(dict.fromkeys(row['mode'] for row in report['results']))
    for carrier in ('plane','bowl','dome_floor'):
        values = [np.mean([r['target']['iou'] for r in report['results']
                          if r['carrier']==carrier and r['mode']==mode]) for mode in modes]
        ax.plot(range(len(modes)), values, 'o-', label=carrier)
    ax.set_xticks(range(len(modes)), modes, rotation=45, ha='right', fontsize=8)
    ax.set_ylabel('Mean target-mask IoU (two frames)')
    ax.set_title('One coded object / one view; not a carrier/fusion ranking')
    ax.grid(alpha=.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(output/'object_stitch_iou.png', dpi=140)
    plt.close(fig)

    if all('source_identity_support' in row for row in report['results']):
        fig, ax = plt.subplots(figsize=(8, 5))
        for mode in modes:
            rows = [row for row in report['results'] if row['mode'] == mode]
            x = [row['source_identity_support']['outside_support_fraction'] for row in rows]
            y = [row['target']['iou'] for row in rows]
            ax.scatter(x, y, s=24, alpha=.75, label=mode)
        ax.set_xlabel('Weighted source-ID support outside direct-view truth')
        ax.set_ylabel('Final RGB coded-target IoU')
        ax.set_title('Provenance support and RGB appearance are different measurements')
        ax.grid(alpha=.25)
        ax.legend(fontsize=7, ncol=2)
        fig.tight_layout()
        fig.savefig(output/'object_stitch_source_support.png', dpi=150)
        plt.close(fig)


if __name__ == '__main__':
    main()
