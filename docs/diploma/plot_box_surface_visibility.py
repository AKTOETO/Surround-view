"""Supplied-box surface preservation: native RGB and joint quality controls."""
import argparse
import json
import sys
from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/research'))
from box_surface_visibility import collect, products


def plots(report, plan, output):
    output.mkdir(parents=True, exist_ok=True)
    truth_path = ROOT / plan['truth_path'];truth = json.loads(truth_path.read_text())
    fig, axes = plt.subplots(2, 4, figsize=(14, 7), layout='constrained')
    for frame in plan['frames']:
        mask = np.load(ROOT / plan['reference_root'] / f'frame{frame}/direct-mask.npy')
        images = [np.asarray(Image.open(truth_path.parent / truth['frames'][frame]['rgb']).convert('RGB'))]
        for mode in ['center', 'fp1', 'sf1']:
            row = next(r for r in report['rows'] if r['frame'] == frame and r['variant'] == 'local_' + mode)
            images.append(products(ROOT / row['directory'])['output.rgba'])
        for ax, rgb, label in zip(axes[frame], images, ['Independent Blender', 'Center', 'Radial guard1', 'Box surface guard1']):
            ax.imshow(rgb);ax.contour(mask.astype(float), [.5], colors=['cyan'], linewidths=.8)
            ax.set(xlim=(180,550), ylim=(359.5,165), title=f'Frame {frame}: {label}', xlabel='Output x / px', ylabel='Output y / px')
    fig.suptitle('Actual native RGB: preservation on supplied box surface; no depth estimation')
    fig.savefig(output / 'box_surface_visibility_rgb.png', dpi=160);plt.close(fig)
    fig, axes = plt.subplots(2, 2, figsize=(12, 8), layout='constrained')
    modes = ['center','fp0','fp1','sf0','sf1'];labels = ['Center','Radial 4','Radial 16','Box 4','Box 16']
    metrics = [('outside_expected_pixels','Outside chroma proxy pixels'), ('raw_iou','Raw full-frame proxy IoU'),
               ('visible_target_proxy_hits','Source-visible target proxy hits'), ('road_linear_rgb_mae','Fixed-road linear RGB MAE')]
    for ax, (key, label) in zip(axes.flat, metrics):
        for frame in plan['frames']:
            for carrier in ['plane', 'local']:
                rows = [next(r for r in report['rows'] if r['frame'] == frame and r['variant'] == carrier+'_'+m) for m in modes]
                ax.plot(labels, [r[key] for r in rows], marker='o', linestyle='-' if carrier == 'local' else '--', label=f'F{frame} {carrier}')
        ax.set(ylabel=label);ax.grid(alpha=.3);ax.legend()
    fig.suptitle('Joint quality controls: object preservation and residual reduction')
    fig.savefig(output / 'box_surface_visibility_metrics.png', dpi=160);plt.close(fig)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, default=ROOT / 'configs/research/box-surface-visibility-plan.json')
    parser.add_argument('--output-report', type=Path, required=True)
    parser.add_argument('--figures', type=Path, default=Path(__file__).parent / 'figures/experiments')
    args = parser.parse_args()
    report = collect(ROOT, args.plan)
    args.output_report.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    plots(report, json.loads(args.plan.read_text()), args.figures)
