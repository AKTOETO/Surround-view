"""Measured optical-family rasters and fixed-pose errors; failures remain in report."""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results',type=Path,default=ROOT/'artifacts/calibration-optics-v1')
    args = parser.parse_args()
    report = json.loads((args.results/'summary.json').read_text())
    families = list(dict.fromkeys(r['family'] for r in report['results']))
    seed = report['seeds'][0]
    output = Path(__file__).parent/'figures/experiments'
    fig,axes = plt.subplots(1,4,figsize=(13,3.5))
    for family,ax in zip(families,axes):
        path = args.results/f'{family}-{seed}-clean'/'board-00.png'
        with Image.open(path) as im:
            ax.imshow(im,cmap='gray',vmin=0,vmax=255)
        ax.set_title(family)
        ax.axis('off')
    fig.suptitle(f'Same physical board pose / different optical projection / seed {seed}')
    fig.tight_layout()
    fig.savefig(output/'calibration_optics_rasters.png',dpi=140)
    plt.close(fig)
    fig,axes = plt.subplots(1,2,figsize=(11,4))
    for color,family in zip(('tab:blue','tab:orange','tab:green','tab:red'),families):
        for order,marker in ((2,'o'),(4,'x')):
            fits = [f for r in report['results'] if r['family']==family for f in r['fits']
                    if f['order']==order and 'metric_validation' in f]
            x = [f['report']['validation_error_px']['p95'] for f in fits]
            axes[0].scatter(x,[f['metric_validation']['ray_error_px']['outer']['p95'] for f in fits],
                            c=color,marker=marker,label=f'{family} / k{order}')
            axes[1].scatter(x,[f['metric_validation']['floor_error_m']['p95'] for f in fits],
                            c=color,marker=marker)
    axes[0].set_ylabel('Outer ray error p95, px (known rays, no pose fit)')
    axes[1].set_ylabel('Known floor reconstruction error p95, m')
    axes[0].set_yscale('log')
    for ax in axes:
        ax.set_xlabel('CLI validation p95, px (board pose fitted)')
        ax.grid(alpha=.3)
    axes[0].legend(fontsize=7,ncol=2)
    fig.suptitle('Accepted fits only; rejected cases are listed in the report')
    fig.tight_layout()
    fig.savefig(output/'calibration_optics_metric.png',dpi=140)
    plt.close(fig)


if __name__ == '__main__':
    main()
