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


def plot_optimizer(path, output):
    data = json.loads(path.read_text())
    test = next(t for suite in data['testsuites'] for t in suite['testsuite']
                if t['name'] == 'ConvergedEnergyAndGlobalEnumeration')
    case = json.loads(test['worst_case'])
    positions = np.array([(np.cos(a), np.sin(a)) for a in np.linspace(0, 2*np.pi, 6, endpoint=False)])
    palette = ['#66c2a5','#fc8d62','#8da0cb','#e78ac3']
    fig, axes = plt.subplots(1, 2, figsize=(12, 6))
    for ax, labels, title in zip(axes, (case['labels'],case['global_labels']),
            (f"Expansion local minimum: E={case['energy']}", f"Exhaustive global minimum: E={case['global_energy']}")):
        for p, q, weight in case['edges']:
            xy = positions[[p,q]]
            ax.plot(xy[:,0],xy[:,1],color='gray',linewidth=1+weight/10,zorder=1)
            midpoint = xy.mean(axis=0)
            ax.text(*midpoint,str(weight),ha='center',va='center',
                    bbox=dict(facecolor='white',edgecolor='none',pad=1))
        ax.scatter(positions[:,0],positions[:,1],s=1700,c=[palette[c] for c in labels],zorder=2)
        for n, (xy, label) in enumerate(zip(positions,labels)):
            ax.text(*xy,f'p{n}\ncamera {label}',ha='center',va='center',zorder=3)
        ax.set_title(title)
        ax.set_xlim(-1.4,1.4);ax.set_ylim(-1.4,1.4)
        ax.set_aspect('equal');ax.set_axis_off()
    fig.suptitle('Known-answer six-node Potts graph: exact moves do not guarantee a global solution')
    fig.tight_layout()
    fig.savefig(output/'seam_optimizer_counterexample.png',dpi=150)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path,
                        default=ROOT/'docs/validation/baselines/server_boundary_v1.json')
    parser.add_argument('--captures-root', type=Path, default=ROOT/'artifacts/server-boundary-v2')
    parser.add_argument('--output', type=Path, default=Path(__file__).parent/'figures/experiments')
    parser.add_argument('--optimizer-report', type=Path)
    parser.add_argument('--fixture', type=Path, default=ROOT/'tests/data/object_stitch_v1')
    parser.add_argument('--capture', type=Path)
    args = parser.parse_args()
    report = json.loads(args.report.read_text())
    study = report.get('cases')
    if study:
        pairs = [dict(p,seed=c['seed']) for c in study for p in c['audit']['paired_differences']]
        report = study[0]['audit']
    else:
        pairs = report['paired_differences']
    seam = report.get('comparison') == 'seam_solver'
    prefix = 'seam_generalization' if study else 'server_seam' if seam else 'server_boundary'
    difference = report.get('difference','normalized minus zero')
    labels = [f"seed{p['seed']} / pose {p['truth_index']}" if study else
              f"{p['carrier']} / pose {p['truth_index']}" for p in pairs]
    colors = ['#2878b5' if p['mode'] in ('multi_band','graph_cut_seam') else '#dc8039' for p in pairs]
    fig, axes = plt.subplots(1, 3, figsize=(15, 9), sharey=True)
    for ax, field, title in zip(axes,
            ('linear_mae_delta', 'target_iou_delta', 'fusion_cpu_p50_delta_ms'),
            ('Linear RGB MAE difference', 'Coded-target IoU difference', 'Median fusion CPU difference (ms)')):
        ax.barh(np.arange(len(pairs)), [p[field] for p in pairs], color=colors)
        ax.axvline(0, color='black', linewidth=.8)
        ax.set_title(title, fontsize=10)
        ax.set_xlabel(difference)
        ax.grid(axis='x', alpha=.2)
        if field != 'fusion_cpu_p50_delta_ms':
            ax.ticklabel_format(axis='x', style='sci', scilimits=(0, 0))
        ax.locator_params(axis='x', nbins=5)
    axes[0].set_yticks(np.arange(len(pairs)), labels)
    axes[0].invert_yaxis()
    from matplotlib.patches import Patch
    axes[0].legend(handles=[Patch(color='#2878b5', label='graph_cut_seam' if seam else 'multi_band'),
                            Patch(color='#dc8039', label='graph_cut_multi_band')], loc='lower left')
    fig.suptitle(('Three new procedural instances: ' if study else 'Native-server reused fixture: ')+difference)
    fig.tight_layout()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.optimizer_report:
        plot_optimizer(args.optimizer_report,args.output)
    fig.savefig(args.output/(prefix+'_metrics.png'), dpi=150)
    plt.close(fig)

    # All poses of the declared dome baseline, not a result-selected best example.
    fixture = args.fixture
    for name, digest in report['fixture_sha256'].items():
        if hashlib.sha256((fixture/name).read_bytes()).hexdigest() != digest:
            raise ValueError('fixture hash mismatch')
    cfg, _, truths, _, stamps, _ = load_sequence(fixture, args.capture or fixture, 'any')
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
            policy = settings.get('seam_solver','binary_pairs') if seam else settings['pyramid_boundary']
            axes[truth_index, variant+1].set_title(settings['mode']+'\n'+policy)
    for ax in axes.flat:
        ax.set_axis_off()
    fig.suptitle(('Seed '+str(study[0]['seed'])+': ' if study else '')+
                 'Actual production-server RGBA: dome baseline, both predeclared poses')
    fig.tight_layout()
    fig.savefig(args.output/(prefix+'_views.png'), dpi=150)
    plt.close(fig)


if __name__ == '__main__':
    main()
