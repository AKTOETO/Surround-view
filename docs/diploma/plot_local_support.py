"""Supplied local support control: independent truth, native images and paired metrics."""
import argparse
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image
import matplotlib.pyplot as plt
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/research'))
from local_support import collect
from source_correspondence import load_product


def plots(report, plan, output):
    output.mkdir(parents=True, exist_ok=True)
    truth_path = ROOT / plan['truth_path']
    truth = json.loads(truth_path.read_text())
    labels = ['Plane z=0', 'Global z=0.8 m', 'Local h=0.8 m', 'Local h=1.0 m']
    fig, axes = plt.subplots(2, 5, figsize=(16, 7), layout='constrained')
    for frame in plan['frames']:
        target = np.load(ROOT / plan['reference_root'] / f'frame{frame}/direct-mask.npy')
        images = [np.asarray(Image.open(truth_path.parent / truth['frames'][frame]['rgb']).convert('RGB'))]
        for row in [r for r in report['rows'] if r['frame'] == frame]:
            d = ROOT / row['directory']
            meta = json.loads((d / 'report.json').read_text())
            images.append(load_product(d, next(p for p in meta['products'] if p['file'] == 'output.rgba')))
        for ax, image, label in zip(axes[frame], images, ['Independent Blender'] + labels):
            ax.imshow(image)
            ax.contour(target.astype(float), [.5], colors=['cyan'], linewidths=.8)
            ax.set(xlim=(180, 550), ylim=(359.5, 165), title=f'Frame {frame}: {label}', xlabel='Output x / px', ylabel='Output y / px')
    fig.suptitle('Supplied location and height; actual native RGB, cyan = independent target outline')
    fig.savefig(output / 'local_support_rgb.png', dpi=160)
    plt.close(fig)
    fig, axes = plt.subplots(2, 2, figsize=(12, 8), layout='constrained')
    metrics = [('raw_iou', 'Raw full-frame chroma proxy IoU'),
               ('outside_expected_pixels', 'Proxy pixels outside independent target'),
               ('road_linear_rgb_mae', 'Fixed-road linear RGB MAE'),
               ('carrier_to_virtual_target_distance_m', 'Median carrier / direct target distance, m')]
    for ax, (key, title) in zip(axes.flat, metrics):
        for frame in plan['frames']:
            rows = [r for r in report['rows'] if r['frame'] == frame]
            values = [r[key]['p50'] if isinstance(r[key], dict) else r[key] for r in rows]
            ax.plot(labels, values, marker='o', label=f'Frame {frame}')
        ax.set(ylabel=title)
        ax.tick_params(axis='x', rotation=15)
        ax.grid(alpha=.3)
        ax.legend()
    fig.suptitle('Joint target / ground evaluation; two correlated frames, no RGB geometry estimation')
    fig.savefig(output / 'local_support_metrics.png', dpi=160)
    plt.close(fig)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--plan', type=Path, default=ROOT / 'configs/research/local-support-plan.json')
    p.add_argument('--output-report', type=Path, required=True)
    p.add_argument('--figures', type=Path, default=Path(__file__).parent / 'figures/experiments')
    args = p.parse_args()
    report = collect(ROOT, args.plan)
    args.output_report.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    plots(report, json.loads(args.plan.read_text()), args.figures)
