"""Reproduce the frozen post-hoc central-occlusion uncertainty certificates."""
import argparse
import json
from pathlib import Path
import sys
import matplotlib.pyplot as plt
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/research'))
from visibility_uncertainty import collect


def plots(report, output):
    output.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(2, 2, figsize=(12, 8), layout='constrained')
    for frame in (0, 1):
        for column, variant in enumerate(['c_nom_b020', 'c_zp_b020']):
            ax = axes[frame, column]
            rows = [r for r in report['rows'] if r['frame'] == frame and r['variant'] == variant]
            xs = [r['radius_m'] * 1000 for r in rows]
            guaranteed = [r['guaranteed_visible_eligible'] for r in rows]
            optimistic = [r['optimistic_visible_eligible_bound'] for r in rows]
            ax.fill_between(xs, guaranteed, optimistic, alpha=.2, color='darkorange', label='Ambiguous-only eligibility')
            ax.plot(xs, guaranteed, 'o-', color='forestgreen', label='Guaranteed clear source')
            ax.plot(xs, optimistic, 's-', color='darkorange', label='Optimistic eligibility bound')
            ax.axhline(rows[0]['visible_target_pixels'], ls=':', color='black', label='Independent visible target')
            ax.set(title=f'Frame {frame}; {"nominal box" if column == 0 else "box Z +5mm"}; bias20mm',
                   xlabel='Supplied face-coordinate uncertainty / mm', ylabel='Source-visible target pixels')
            ax.grid(alpha=.25)
    handles, labels = axes[0, 0].get_legend_handles_labels()
    fig.legend(handles, labels, loc='outside lower center', ncol=2)
    fig.suptitle('Central-ray certificates on frozen actual carrier points; not new RGB predictions')
    fig.savefig(output / 'visibility_uncertainty_bounds.png', dpi=160)
    plt.close(fig)

    fig, axes = plt.subplots(1, 2, figsize=(12, 5), layout='constrained')
    variants = ['c_nom_b000', 'c_nom_b002', 'c_nom_b020', 'c_zp_b020', 'c_zm_b020']
    labels = ['nom/0', 'nom/2', 'nom/20', 'Z+5/20', 'Z-5/20']
    for frame, ax in enumerate(axes):
        rows = [next(r for r in report['rows'] if r['frame'] == frame and r['variant'] == v and r['radius_m'] == .005) for v in variants]
        positions = list(range(len(rows)))
        ax.bar([p-.2 for p in positions], [r['central_visible_loss_potentially_recoverable'] for r in rows], width=.4,
               label='Potentially recoverable visible pixels', color='steelblue')
        ax.bar([p+.2 for p in positions], [r['newly_admitted_nominal_reference_blocked_pairs'] for r in rows], width=.4,
               label='New nominal-reference blocked pairs', color='firebrick')
        ax.set(xticks=positions, xticklabels=labels, title=f'Frame {frame}; uncertainty5mm',
               xlabel='Supplied box shift / endpoint bias mm', ylabel='Counts (distinct units, not a ratio)')
        ax.grid(axis='y', alpha=.25)
    handles, legend = axes[0].get_legend_handles_labels()
    fig.legend(handles, legend, loc='outside lower center', ncol=1)
    fig.suptitle('Accept-all-ambiguity relaxation: potential recovery and reference-risk diagnostic')
    fig.savefig(output / 'visibility_uncertainty_tradeoff.png', dpi=160)
    plt.close(fig)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, default=ROOT / 'configs/research/visibility-uncertainty-plan.json')
    parser.add_argument('--output-report', type=Path, required=True)
    parser.add_argument('--figures', type=Path, default=Path(__file__).parent / 'figures/experiments')
    args = parser.parse_args()
    report = collect(ROOT, args.plan)
    args.output_report.parent.mkdir(parents=True, exist_ok=True)
    args.output_report.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    plots(report, args.figures)
