"""Plot archived native finite-face-facing captures; no product rendering."""
import json
from pathlib import Path
import sys
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator
import numpy as np
from PIL import Image
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/research'))
from filter_visibility import products


def find(report, frame, shift, policy='accept'):
    return next(r for r in report['rows'] if r['frame'] == frame and r['shift'] == shift
                and r['kind'] == 'surface10mm' and r['policy'] == policy)


def main():
    new = json.loads((ROOT / 'docs/validation/baselines/native_facing_v1.json').read_text())
    old = json.loads((ROOT / 'docs/validation/baselines/native_uncertainty_v1.json').read_text())
    out = Path(__file__).parent / 'figures/experiments'
    out.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(2, 3, figsize=(14, 7), layout='constrained')
    for frame in [0, 1]:
        for col, (a, b, title) in enumerate([
            ('visible_proxy_hits', 'visible_target_proxy_hits', 'Visible target hits'),
            ('outside_proxy', 'outside_expected_pixels', 'Outside-target proxy pixels'),
            ('reference_blocked_pairs', 'nominal_blocked_retained_camera_pairs', 'Reference-blocked retained pairs')]):
            ax = axes[frame, col]
            for report, field, label, marker in [(old, b, 'Facing disabled', 'o'), (new, a, 'Facing enabled', 's')]:
                ax.plot(['nom', 'Z+5mm', 'Z-5mm'], [find(report, frame, s)[field] for s in ['nom', 'zp', 'zm']], marker=marker, label=label)
            ax.set(title=f'F{frame}: {title}', xlabel='Supplied box translation', ylabel='Count')
            ax.yaxis.set_major_locator(MaxNLocator(integer=True))
            ax.set_ylim(bottom=0)
            if frame == 1 and col == 1:
                ax.set_ylim(0, 1)
            ax.grid(alpha=.25)
    handles, labels = axes[0, 0].get_legend_handles_labels()
    fig.legend(handles, labels, loc='outside lower center', ncol=2)
    fig.suptitle('Native edge_feather; accept radius5mm, surface band10mm, endpoint bias20mm')
    fig.savefig(out / 'native_facing_metrics.png', dpi=160)
    plt.close(fig)
    parent = json.loads((ROOT / 'configs/research/native-uncertainty-nvidia-plan.json').read_text())
    truth_path = ROOT / parent['truth_path']
    truth = json.loads(truth_path.read_text())
    target = next(v for k, v in truth['objects'].items() if 'coded object target' in k)
    fig, axes = plt.subplots(2, 4, figsize=(14, 7), layout='constrained')
    for frame in [0, 1]:
        t = truth['frames'][frame]
        mask = np.load(truth_path.parent / t['objects'], allow_pickle=False) == target
        images = [np.asarray(Image.open(truth_path.parent / t['rgb']).convert('RGB'))]
        images += [products(ROOT / row['directory'])['output.rgba'] for row in [
            find(old, frame, 'zp'), find(new, frame, 'zp'), find(new, frame, 'zp', 'nominal')]]
        for ax, rgb, label in zip(axes[frame], images, ['Independent Blender', 'accept / facing off', 'accept / facing on', 'nominal / facing on']):
            ax.imshow(rgb)
            ax.contour(mask.astype(float), [.5], colors=['cyan'], linewidths=.8)
            ax.set(xlim=(290, 345), ylim=(230, 190), title=f'F{frame}: {label}', xlabel='Output x / px', ylabel='Output y / px')
    fig.suptitle('Z+5mm; actual native RGBA; cyan = full target, including hidden part')
    fig.savefig(out / 'native_facing_rgb.png', dpi=160)
    plt.close(fig)


if __name__ == '__main__':
    main()
