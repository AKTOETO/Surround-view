"""Plot independently audited native-server boundary measurements and captured views."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from temporal_seam_stability import load_sequence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path,
                        default=ROOT/'docs/validation/baselines/server_boundary_v1.json')
    parser.add_argument('--captures-root', type=Path, default=ROOT/'artifacts/server-boundary-v2')
    parser.add_argument('--output', type=Path, default=Path(__file__).parent/'figures/experiments')
    args = parser.parse_args()
    report = json.loads(args.report.read_text())
    pairs = report['paired_differences']
    labels = [f"{p['carrier']} / pose {p['truth_index']}" for p in pairs]
    colors = ['#2878b5' if p['mode'] == 'multi_band' else '#dc8039' for p in pairs]
    fig, axes = plt.subplots(1, 3, figsize=(15, 9), sharey=True)
    for ax, field, title in zip(axes,
            ('linear_mae_delta', 'target_iou_delta', 'fusion_cpu_p50_delta_ms'),
            ('Linear RGB MAE difference', 'Coded-target IoU difference', 'Median fusion CPU difference (ms)')):
        ax.barh(np.arange(len(pairs)), [p[field] for p in pairs], color=colors)
        ax.axvline(0, color='black', linewidth=.8)
        ax.set_title(title, fontsize=10)
        ax.set_xlabel('normalized minus zero')
        ax.grid(axis='x', alpha=.2)
    axes[0].set_yticks(np.arange(len(pairs)), labels)
    axes[0].invert_yaxis()
    from matplotlib.patches import Patch
    axes[0].legend(handles=[Patch(color='#2878b5', label='multi_band'),
                            Patch(color='#dc8039', label='graph_cut_multi_band')], loc='lower left')
    fig.suptitle('Native-server paired screen: reused two-pose fixture; unequal carrier budgets')
    fig.tight_layout()
    args.output.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output/'server_boundary_metrics.png', dpi=150)
    plt.close(fig)

    # All poses of the declared dome baseline, not a result-selected best example.
    fixture = ROOT/'tests/data/object_stitch_v1'
    for name, digest in report['fixture_sha256'].items():
        if hashlib.sha256((fixture/name).read_bytes()).hexdigest() != digest:
            raise ValueError('fixture hash mismatch')
    cfg, _, truths, _, stamps, _ = load_sequence(fixture, fixture, 'any')
    native = report['native_reports']['dome_floor_v1']
    fig, axes = plt.subplots(len(stamps), 5, figsize=(17, 7), squeeze=False)
    for truth_index, stamp in enumerate(stamps):
        axes[truth_index, 0].imshow(truths[truth_index])
        axes[truth_index, 0].set_title(f'Direct Blender truth / pose {truth_index}')
        frame_index = next(b['frame_index'] for b in native['frame_baselines']
                           if b['metadata']['inputs'][0]['source_timestamp_ns'] == str(stamp))
        for variant, settings in enumerate(native['scenario']['variants']):
            sample = next(s for s in native['samples'] if s['frame_index'] == frame_index
                          and s['variant'] == variant and not s['warmup'])
            payload = (args.captures_root/'dome_floor'/sample['rgba_file']).read_bytes()
            if hashlib.sha256(payload).hexdigest() != sample['rgba_sha256']:
                raise ValueError('native capture hash mismatch')
            rgb = np.frombuffer(payload, np.uint8).reshape(cfg['output']['height'], cfg['output']['width'], 4)
            axes[truth_index, variant+1].imshow(rgb)
            axes[truth_index, variant+1].set_title(settings['mode']+'\n'+settings['pyramid_boundary'])
    for ax in axes.flat:
        ax.set_axis_off()
    fig.suptitle('Actual production-server RGBA: dome baseline, both predeclared poses')
    fig.tight_layout()
    fig.savefig(args.output/'server_boundary_views.png', dpi=150)
    plt.close(fig)


if __name__ == '__main__':
    main()
