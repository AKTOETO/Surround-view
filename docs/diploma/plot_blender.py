#!/usr/bin/env python3
"""Publish actual Blender inputs and GLES readbacks; never synthesizes image content."""
import argparse
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,required=True)
    parser.add_argument('--dataset',type=Path,required=True)
    parser.add_argument('--validation',type=Path,required=True)
    parser.add_argument('--low-view',type=Path,required=True)
    args = parser.parse_args()
    output = Path(__file__).parent/'figures'/'implementation'
    output.mkdir(parents=True,exist_ok=True)
    # PNG snapshots remain small ordinary Git assets; full frame sequences stay local.
    Image.open(args.capture/'overview.png').save(output/'03_blender_world.png')
    fig, axes = plt.subplots(2,2,figsize=(9,9))
    for index,(ax,name) in enumerate(zip(axes.flat,['Передняя','Правая','Задняя','Левая'])):
        ax.imshow(Image.open(args.dataset/f'camera{index}_0000.ppm'))
        ax.set_title(f'{name} камера · id={index}',fontsize=12)
        ax.axis('off')
    fig.tight_layout()
    fig.savefig(output/'03_blender_inputs.png',dpi=130,bbox_inches='tight')
    plt.close(fig)
    fig,axes = plt.subplots(2,1,figsize=(12,13))
    for ax,path,title in zip(axes,[args.validation/'bench'/'preview.ppm',args.low_view/'preview.ppm'],
                             ['Купол + пол · elevation = 1.00 rad',
                              'Купол + пол · elevation = 0.35 rad']):
        ax.imshow(Image.open(path))
        ax.set_title(title,fontsize=13)
        ax.axis('off')
    fig.tight_layout()
    fig.savefig(output/'03_blender_surround.png',dpi=110,bbox_inches='tight')
    plt.close(fig)


if __name__ == '__main__':
    main()
