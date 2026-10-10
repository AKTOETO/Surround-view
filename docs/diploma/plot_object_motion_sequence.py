"""Draw the capture/analyze sequence for the moving coded-target experiment."""
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'diploma/figures/experiments/object_stitch_motion_sequence.png'


def main():
    fig, ax = plt.subplots(figsize=(12, 4.4))
    ax.set_xlim(0, 12)
    ax.set_ylim(0, 4.4)
    ax.axis('off')
    nodes = [
        (0.2, 2.65, 1.65, .9, 'Frozen plan', 'seed, target path,\nmounts, timestamps'),
        (2.25, 2.65, 1.8, .9, 'Blender scene', 'vehicle + moving target\nat each sample'),
        (4.6, 3.15, 1.85, .85, 'Camera inputs', '4 synchronous views\nper timestamp'),
        (4.6, 1.85, 1.85, .85, 'Direct truth', 'RGB + object IDs\n+ target position'),
        (7.15, 3.15, 1.75, .85, 'Converter', 'cube faces →\nfisheye frames'),
        (9.6, 2.4, 2.1, 1.05, 'Offline analysis', 'hash/trajectory checks\nRGB + support metrics'),
    ]
    for x, y, w, h, title, detail in nodes:
        ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle='round,pad=0.04,rounding_size=.08',
                                    facecolor='#eaf2f8', edgecolor='#355c7d', linewidth=1.5))
        ax.text(x+w/2, y+h*.69, title, ha='center', va='center', fontsize=10, weight='bold')
        ax.text(x+w/2, y+h*.31, detail, ha='center', va='center', fontsize=8.5)

    def arrow(start, end, label='', offset=(0, .12), color='#355c7d'):
        ax.annotate('', xy=end, xytext=start, arrowprops={'arrowstyle':'->','lw':1.5,'color':color})
        if label:
            ax.text((start[0]+end[0])/2+offset[0], (start[1]+end[1])/2+offset[1],
                    label, ha='center', va='center', fontsize=8, color=color)

    arrow((1.85,3.1),(2.25,3.1),'plan')
    arrow((4.05,3.22),(4.6,3.55),'same frame')
    arrow((4.05,2.95),(4.6,2.27),'same frame')
    arrow((6.45,3.58),(7.15,3.58),'camera faces')
    arrow((8.9,3.55),(9.6,3.05),'fisheye')
    arrow((6.45,2.25),(9.6,2.75),'truth + IDs', (0,.2), '#39826b')
    ax.text(6, .85, 'Repeat at 9 timestamps (10 Hz): 6 carriers × 7 fusion modes × 9 frames = 378 cases',
            ha='center', va='center', fontsize=10, color='#333333')
    ax.text(6, .38, 'Synchronous per-frame test; temporal lag/trails require a separate delayed-input experiment',
            ha='center', va='center', fontsize=9, color='#8b3a3a')
    fig.tight_layout()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT, dpi=160, bbox_inches='tight')
    plt.close(fig)


if __name__ == '__main__':
    main()
