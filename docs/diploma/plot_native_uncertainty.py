"""Native ambiguity policy quality, reference-risk and actual RGBA crops."""
import argparse
import json
from pathlib import Path
import sys
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator
import numpy as np
from PIL import Image
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/research'))
from native_uncertainty import collect
from filter_visibility import products


def find(report, frame, shift, kind, policy):
    return next(r for r in report['rows'] if r['frame'] == frame and r['shift'] == shift
                and r['kind'] == kind and r['policy'] == policy)


def plots(report, plan, output):
    output.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(2, 3, figsize=(15, 8), layout='constrained')
    for frame in [0, 1]:
        for column, (metric, title) in enumerate([
                ('visible_target_proxy_hits', 'Independent visible target hits'),
                ('outside_expected_pixels', 'Outside-target proxy pixels'),
                ('nominal_blocked_retained_camera_pairs', 'Nominal-reference blocked retained pairs')]):
            ax = axes[frame, column]
            for policy, marker in [('nominal', 'o'), ('accept', 's'), ('reject', '^')]:
                rows = [find(report, frame, shift, 'surface10mm', policy) for shift in ['nom', 'zp', 'zm']]
                ax.plot(['nom', 'Z+5mm', 'Z-5mm'], [r[metric] for r in rows], marker=marker, label=policy)
            ax.set(title=f'Frame {frame}: {title}', xlabel='Supplied box translation', ylabel='Count')
            ax.yaxis.set_major_locator(MaxNLocator(integer=True))
            ax.set_ylim(bottom=0)
            if not any(r[metric] for r in report['rows'] if r['frame'] == frame and r['kind'] == 'surface10mm'):
                ax.set_ylim(0, 1)
            ax.grid(alpha=.25)
    handles, labels = axes[0, 0].get_legend_handles_labels()
    fig.legend(handles, labels, loc='outside lower center', ncol=3)
    fig.suptitle('Actual native RGBA; surface band10mm, endpoint bias20mm; central uncertainty5mm')
    fig.savefig(output / 'native_uncertainty_metrics.png', dpi=160)
    plt.close(fig)

    truth_path = ROOT / plan['truth_path']
    truth = json.loads(truth_path.read_text())
    fig, axes = plt.subplots(2, 6, figsize=(18, 7), layout='constrained')
    cases = [('surface10mm', 'nominal'), ('surface1mm', 'accept'),
             ('surface10mm', 'accept'), ('surface10mm', 'reject'), ('center', 'accept')]
    for frame in [0, 1]:
        t = truth['frames'][frame]
        ids = np.load(truth_path.parent / t['objects'], allow_pickle=False)
        target_id = next(v for k, v in truth['objects'].items() if 'coded object target' in k)
        mask = ids == target_id
        images = [np.asarray(Image.open(truth_path.parent / t['rgb']).convert('RGB'))]
        images += [products(ROOT / find(report, frame, 'zp', kind, policy)['directory'])['output.rgba']
                   for kind, policy in cases]
        for ax, rgb, label in zip(axes[frame], images, ['Independent Blender'] + [f'{kind}\n{policy}' for kind, policy in cases]):
            ax.imshow(rgb)
            ax.contour(mask.astype(float), [.5], colors=['cyan'], linewidths=.8)
            ax.set(xlim=(290, 345), ylim=(230, 190), title=f'F{frame}: {label}', xlabel='Output x / px', ylabel='Output y / px')
    fig.suptitle('Z+5mm box; actual independent truth and native outputs; cyan = full target outline')
    fig.savefig(output / 'native_uncertainty_rgb.png', dpi=160)
    plt.close(fig)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, default=ROOT / 'configs/research/native-uncertainty-nvidia-plan.json')
    parser.add_argument('--output-report', type=Path, required=True)
    parser.add_argument('--figures', type=Path, default=Path(__file__).parent / 'figures/experiments')
    args = parser.parse_args()
    report = collect(ROOT, args.plan)
    args.output_report.parent.mkdir(parents=True, exist_ok=True)
    args.output_report.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    plots(report, json.loads(args.plan.read_text()), args.figures)
