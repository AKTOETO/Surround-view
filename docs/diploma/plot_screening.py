#!/usr/bin/env python3
"""Captioned actual surface/fusion screening images from compare_surfaces.py."""
import argparse
import json
import os
from pathlib import Path

os.environ.setdefault('MPLCONFIGDIR','/tmp/sv-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--screening',type=Path,required=True)
    args = parser.parse_args()
    report = json.loads((args.screening/'report.json').read_text())
    rows = {(row['carrier'],row['mode'],row['diagnostic']):row for row in report['rows']
            if row['view']=='low'}
    carriers = ['plane','bowl','dome','cylinder','cube']
    labels = dict(plane='Плоскость',bowl='Bowl',dome='Купол + пол',
                  cylinder='Цилиндр + пол + крышка',cube='Куб: стены + пол + крышка')
    output = Path(__file__).parent/'figures'/'experiments'
    output.mkdir(parents=True,exist_ok=True)
    modes = ['edge_feather','hard_best_angle','angular_feather']
    fig,axes = plt.subplots(5,3,figsize=(14,14))
    for r,carrier in enumerate(carriers):
        for c,mode in enumerate(modes):
            row = rows[carrier,mode,'color']
            axes[r,c].imshow(Image.open(args.screening/row['directory']/'preview.png'))
            axes[r,c].set_title(labels[carrier]+'\n'+mode,fontsize=10)
            axes[r,c].axis('off')
    fig.suptitle('Один набор камер · одинаковый ракурс · плотности сеток различаются',fontsize=13)
    fig.tight_layout()
    fig.savefig(output/'04_surface_fusion_screen.png',dpi=95,bbox_inches='tight')
    plt.close(fig)
    fig,axes = plt.subplots(5,2,figsize=(10,14))
    for r,carrier in enumerate(carriers):
        for c,diagnostic in enumerate(['coverage','weights']):
            row = rows[carrier,'edge_feather',diagnostic]
            axes[r,c].imshow(Image.open(args.screening/row['directory']/'preview.png'))
            axes[r,c].set_title(labels[carrier]+' · '+diagnostic,fontsize=10)
            axes[r,c].axis('off')
    fig.suptitle('Coverage: серый = число камер, чёрный = 0, magenta = mask\n'
                 'Weights: front=red, right=green, rear=blue, left=yellow',fontsize=11)
    fig.tight_layout()
    fig.savefig(output/'04_surface_diagnostics.png',dpi=95,bbox_inches='tight')
    plt.close(fig)


if __name__ == '__main__':
    main()
