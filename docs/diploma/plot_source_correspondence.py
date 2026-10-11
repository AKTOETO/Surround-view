"""Audit recorded native/Blender correspondence and draw figures; never render product images."""
import argparse
import json
from pathlib import Path
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/research'))
from source_correspondence import collect, load_product, verified


def plot(report, output, figures):
    figures.mkdir(parents=True, exist_ok=True)
    scene = report['scene']
    mask = np.load(verified(output / 'direct-mask.npy', scene['files']['direct-mask.npy']), allow_pickle=False)
    direct = np.load(verified(output / 'direct-points.npy', scene['files']['direct-points.npy']), allow_pickle=False)
    plan = json.loads((ROOT / 'configs/research/source-correspondence-plan.json').read_text())
    inputs = ROOT / plan['input_root']
    truth = json.loads((inputs / 'seed101-capture/paired_truth.json').read_text())
    frame = truth['frames'][0]
    rgb = np.asarray(Image.open(verified(inputs / 'seed101-capture' / frame['rgb'], truth['sha256'][frame['rgb']])))
    fig, axes = plt.subplots(2, 3, figsize=(15, 9))
    panels = [(rgb, 'Independent Blender view')]
    products = {}
    for row in report['rows']:
        directory = output / 'native' / row['carrier']
        native = json.loads(verified(directory / 'report.json', row['report_sha256']).read_text())
        products[row['carrier']] = {p['file']: load_product(directory, p) for p in native['products']}
        panels.append((products[row['carrier']]['output.rgba'][..., :3], row['carrier']))
    for ax, (image, title) in zip(axes.flat, panels):
        ax.imshow(image)
        ax.contour(mask, levels=[.5], colors=['#00eaff'], linewidths=1)
        detected = np.minimum(image[..., 0], image[..., 2]).astype(float)-image[..., 1] > 255*.15
        if detected.any():
            ax.contour(detected, levels=[.5], colors=['#ffb000'], linewidths=1)
        ax.set_xlim(210, 500)
        ax.set_ylim(359.5, 165)
        ax.set_title(title)
        ax.set_xlabel('Output X / px'); ax.set_ylabel('Output Y / px')
    fig.suptitle('Historical first frame, 640x360: cyan = independent target; orange = RGB target proxy\n'
                 'Native RGBA hashes equal archived server baselines; zoom only, no image synthesis')
    fig.tight_layout(rect=(0, 0, 1, .92))
    fig.savefig(figures / 'source_correspondence_views.png', dpi=150)
    plt.close(fig)

    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    x = np.arange(len(report['rows']))
    for ax, field, label in zip(axes,
            ['carrier_to_direct_surface_m', 'visible_true_vs_native_uv_px'],
            ['Carrier to independent scene point / m', 'Camera 3 sample displacement / source px']):
        stats = [r[field] for r in report['rows']]
        median = np.array([s['p50'] for s in stats])
        ax.errorbar(x, median, yerr=[median-[s['min'] for s in stats], [s['max'] for s in stats]-median],
                    fmt='o', capsize=6, color='#17619a')
        ax.set_xticks(x, ['plane', 'bowl', 'dome', 'cylinder', 'cube'])
        ax.set_ylabel(label); ax.grid(axis='y', alpha=.3)
    axes[0].set_title('All 178 independent target pixels')
    axes[1].set_title('76 source-visible target pixels')
    fig.suptitle('Median and observed min/max; one scene/frame, not confidence intervals')
    fig.tight_layout(rect=(0, 0, 1, .92))
    fig.savefig(figures / 'source_correspondence_errors.png', dpi=150)
    plt.close(fig)

    coords = np.argwhere(mask)
    visibility = np.load(inputs / 'seed101-capture' / frame['visibility'], allow_pickle=False)
    visible = coords[(visibility[mask] & 8) != 0]
    index = np.argmin(np.linalg.norm(visible - coords.mean(axis=0), axis=1))
    y, x = visible[index]
    point = direct[y, x]
    carrier = products['plane']['carrier-positions.f32'][y, x]
    hits = np.load(output / 'plane-source-hits.npy', allow_pickle=False)
    selected = np.flatnonzero((coords == [y, x]).all(axis=1))[0]
    source_hit = hits[selected, 3]
    camera = np.linalg.inv(truth['config']['cameras'][3]['T_camera_from_vehicle'])[:3, 3]
    view = truth['config']['virtual_camera']
    az, el, distance = view['azimuth_rad'], view['elevation_rad'], view['distance_m']
    eye = distance*np.array([np.cos(el)*np.cos(az), np.cos(el)*np.sin(az), np.sin(el)])
    fig = plt.figure(figsize=(10, 7)); ax = fig.add_subplot(111, projection='3d')
    def line(a, b, color, label, style='-'):
        segment = np.stack([a, b]); ax.plot(*segment.T, color=color, label=label, linestyle=style)
    line(eye, carrier, '#e08000', 'Virtual ray: scene target lies before carrier')
    line(camera, point, '#188341', 'Camera 3 ray to correct scene point', '--')
    line(camera, source_hit, '#b3261e', 'Camera 3 ray at actual native UV')
    for p, label, color in [(eye, 'Virtual eye', '#e08000'), (camera, 'Camera 3', '#188341'),
                             (point, 'True target hit', '#188341'),
                             (carrier, 'Carrier / selected road hit', '#b3261e')]:
        ax.scatter(*p, color=color); ax.text(*p, ' '+label, fontsize=8)
    ax.set(xlabel='Vehicle X / m', ylabel='Vehicle Y / m', zlabel='Vehicle Z / m')
    ax.set_title(f'Actual rays at output pixel ({x}, {y}), plane carrier')
    ax.legend(loc='upper left', fontsize=8)
    fig.tight_layout(); fig.savefig(figures / 'source_correspondence_rays.png', dpi=150)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, default=ROOT / 'configs/research/source-correspondence-plan.json')
    parser.add_argument('--output-report', type=Path, required=True)
    parser.add_argument('--figures', type=Path, default=Path(__file__).parent / 'figures/experiments')
    args = parser.parse_args()
    report = collect(ROOT, args.plan.resolve())
    args.output_report.write_text(json.dumps(report, indent=2)+'\n')
    plan = json.loads(args.plan.read_text())
    plot(report, ROOT / plan['output_root'], args.figures)


if __name__ == '__main__':
    main()
