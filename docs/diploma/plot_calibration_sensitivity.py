"""Equal-RMS synthetic direction fields and measured finite output gains."""
import argparse
import json
from pathlib import Path
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from calibration.raster_study import digest
from calibration.sensitivity import DIRECTIONS, direction_field


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path, default=ROOT / 'artifacts/calibration-sensitivity-v1')
    parser.add_argument('--input', type=Path, default=ROOT / 'artifacts/calibration-diagnostic-v2')
    args = parser.parse_args()
    report = json.loads((args.results / 'summary.json').read_text())
    output = Path(__file__).parent / 'figures/experiments'
    output.mkdir(parents=True, exist_ok=True)
    name = 'kb_nonzero-7101/small_front-analytic_truth.json'
    path = args.input / name
    if digest(path) != report['exact_input_sha256'][name]:
        raise ValueError('exact observation hash mismatch')
    if digest(ROOT / 'tools/calibration/sensitivity.py') != report['code_sha256']['tools/calibration/sensitivity.py']:
        raise ValueError('direction generator version differs from recorded experiment')
    data = json.loads(path.read_text())
    truth = np.asarray([v['uv_px'] for v in data['train']])
    fig, axes = plt.subplots(2, 3, figsize=(12, 7))
    points = truth[4]
    for (direction, seed), ax in zip(DIRECTIONS, axes.ravel()):
        field = direction_field(truth, direction, seed)[4]
        ax.scatter(points[:, 0], points[:, 1], s=6, c='black')
        ax.quiver(points[:, 0], points[:, 1], field[:, 0]*8, field[:, 1]*8,
                  angles='xy', scale_units='xy', scale=1, width=.004)
        ax.set(xlim=(points[:, 0].min()-20, points[:, 0].max()+20),
               ylim=(points[:, 1].max()+20, points[:, 1].min()-20),
               title=direction + (f' / seed {seed}' if seed else ''), xlabel='u, px', ylabel='v, px')
        ax.set_aspect('equal')
    fig.suptitle('Constructed input directions, kb_nonzero/7101/small_front/view04\nGlobal RMS = 1; arrows scaled by 8 for visibility, not actual applied displacement')
    fig.tight_layout()
    fig.savefig(output / 'calibration_sensitivity_fields.png', dpi=140)
    plt.close(fig)

    fig, axes = plt.subplots(1, 2, figsize=(14, 8))
    labels = [f"{e['family']} / {e['seed']} / {e['profile']} / k{e['order']}" for e in report['results']]
    directions = [d + (f' {s}' if s else '') for d, s in DIRECTIONS]
    for ax, key, unit in zip(axes, ('outer_gain_px_per_px', 'floor_gain_m_per_px'), ('px / px', 'm / px')):
        values = np.full((len(labels), len(DIRECTIONS)), np.nan)
        for i, entry in enumerate(report['results']):
            for j, (name, seed) in enumerate(DIRECTIONS):
                pair = next(p for p in entry['pairs'] if (p['direction'], p['noise_seed'], p['amplitude_px']) == (name, seed, .05))
                if 'response' in pair:
                    values[i, j] = pair['response'][key]['p95']
        im = ax.imshow(values, aspect='auto', cmap='viridis')
        ax.set_xticks(np.arange(len(directions)), directions, rotation=25, ha='right', fontsize=8)
        ax.set_yticks(np.arange(len(labels)), labels, fontsize=7)
        for (i, j), value in np.ndenumerate(values):
            ax.text(j, i, 'missing' if not np.isfinite(value) else f'{value:.3f}', ha='center', va='center',
                    color='black' if value > np.nanmax(values)*.6 else 'white', fontsize=8)
        ax.set_title('Outer projection response' if key.startswith('outer') else 'Known floor reconstruction response')
        fig.colorbar(im, ax=ax, label=unit, fraction=.04)
    fig.suptitle('Directional p95 gain at h=0.05 px RMS; central difference of +h/-h fits\nSame true controls; not a condition number or a per-fit error')
    fig.tight_layout()
    fig.savefig(output / 'calibration_sensitivity_gains.png', dpi=140)
    plt.close(fig)


if __name__ == '__main__':
    main()
