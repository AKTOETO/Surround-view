"""Reproduce measured detector overlays from E-CAL-raster-01 generated images."""
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
    parser.add_argument('--results',type=Path,default=ROOT/'artifacts/calibration-raster-ablation-v1')
    args = parser.parse_args()
    report = json.loads((args.results/'summary.json').read_text())
    fig,axes = plt.subplots(2,3,figsize=(12,7))
    for row,seed in enumerate(report['seed_units']):
        for col,condition in enumerate(('clean','blur_noise','heavy_blur')):
            case = next(r for r in report['results'] if r['seed']==seed and r['condition']==condition)
            # Use the first missed view if any, otherwise the first successful view.
            view = next((v for v in case['views'] if 'localization' not in v),case['views'][0])
            ax = axes[row,col]
            with Image.open(args.results/f'{seed}-{condition}'/f'board-{view["view"]:02}.png') as im:
                ax.imshow(im,cmap='gray',vmin=0,vmax=255)
            truth = np.asarray(view['exact_corners_px'])
            ax.scatter(truth[:,0],truth[:,1],s=12,facecolors='none',edgecolors='lime',linewidths=.6)
            if 'localization' in view:
                detected = np.asarray(view['detected_corners_px'])
                ax.scatter(detected[:,0],detected[:,1],s=4,c='magenta')
                status = f'RMSE {view["localization"]["rmse_px"]:.3f} px'
            else:
                status = 'DETECTOR REJECTED'
            ax.set_title(f'{seed} / {condition} / view {view["view"]}\n{status}',fontsize=9)
            ax.axis('off')
    fig.suptitle('Green: analytic corners; magenta: actual OpenCV detections',fontsize=11)
    fig.tight_layout()
    target = Path(__file__).parent/'figures/experiments/calibration_raster_detection.png'
    fig.savefig(target,dpi=140)
    plt.close(fig)


if __name__ == '__main__':
    main()
