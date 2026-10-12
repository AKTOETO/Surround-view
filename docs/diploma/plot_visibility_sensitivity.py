"""Native supplied-box tolerance sensitivity: frozen quality/geometry controls."""
import argparse
import json
import sys
from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/research'))
from visibility_sensitivity import collect
from filter_visibility import products


def find(report, frame, shift, bias, support):
    return next(r for r in report['rows'] if r['frame'] == frame and
                r.get('condition', {}).get('kind') == 'surface' and
                r['condition']['shift'] == shift and r['condition']['endpoint_bias_m'] == bias and
                r['condition']['support_tolerance_m'] == support)


def plots(report, plan, output):
    output.mkdir(parents=True, exist_ok=True)
    bands = plan['support_tolerances_m'];biases = plan['endpoint_biases_m']
    fig, axes = plt.subplots(2, 2, figsize=(11, 8), layout='constrained')
    for frame in plan['frames']:
        for col, (metric, title) in enumerate([('visible_target_proxy_hits','Visible hits'), ('outside_expected_pixels','Outside proxy')]):
            values = np.array([[find(report, frame, 'nom', bias, band)[metric] for band in bands] for bias in biases])
            ax = axes[frame, col];ax.imshow(values, cmap='viridis')
            for y, x in np.ndindex(values.shape):ax.text(x,y,str(values[y,x]),ha='center',va='center',color='white',bbox=dict(facecolor='black',alpha=.3,pad=1))
            ax.set(xticks=range(3), xticklabels=['10µm','1mm','10mm'], yticks=range(3), yticklabels=['0','2','20'], xlabel='Support band', ylabel='Endpoint bias / mm', title=f'Frame {frame}: {title}')
    fig.suptitle('Nominal supplied geometry; actual native outputs, no parameter fitting')
    fig.savefig(output / 'visibility_sensitivity_nominal.png', dpi=160);plt.close(fig)
    shifts = plan['geometry_shifts']
    fig, axes = plt.subplots(2, 2, figsize=(12, 8), layout='constrained')
    for frame in plan['frames']:
        for col, (metric, title) in enumerate([('visible_target_proxy_hits','Visible hits'), ('outside_expected_pixels','Outside proxy')]):
            ax = axes[frame,col]
            for band in bands:
                rows = [find(report,frame,s['name'],.02,band) for s in shifts]
                ax.plot([s['name'] for s in shifts],[r[metric] for r in rows],marker='o',label=f'{band*1000:g}mm support')
            ax.set(title=f'Frame {frame}: {title}',xlabel='Box translation (±5mm vehicle axis)',ylabel=title);ax.grid(alpha=.3);ax.legend()
    fig.suptitle('Geometry uncertainty: same RGB/truth/carrier, perturbed supplied occluder only')
    fig.savefig(output / 'visibility_sensitivity_geometry.png', dpi=160);plt.close(fig)
    truth_path = ROOT / plan['truth_path'];truth = json.loads(truth_path.read_text())
    fig, axes = plt.subplots(2, 4, figsize=(13, 7), layout='constrained')
    for frame in plan['frames']:
        mask = np.load(ROOT / plan['reference_root'] / f'frame{frame}/direct-mask.npy')
        images = [np.asarray(Image.open(truth_path.parent / truth['frames'][frame]['rgb']).convert('RGB'))]
        images += [products(ROOT / find(report,frame,'nom',.02,b)['directory'])['output.rgba'] for b in bands]
        for ax,rgb,label in zip(axes[frame],images,['Independent Blender','Support10µm','Support1mm','Support10mm']):
            ax.imshow(rgb);ax.contour(mask.astype(float),[.5],colors=['cyan'],linewidths=.8)
            ax.set(xlim=(290,345),ylim=(230,190),title=f'F{frame}: {label}',xlabel='Output x / px',ylabel='Output y / px')
    fig.suptitle('Native object detail: nominal box / endpoint bias20mm / guard1')
    fig.savefig(output / 'visibility_sensitivity_rgb.png',dpi=160);plt.close(fig)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,default=ROOT / 'configs/research/visibility-sensitivity-plan.json')
    parser.add_argument('--output-report',type=Path,required=True)
    parser.add_argument('--figures',type=Path,default=Path(__file__).parent / 'figures/experiments')
    args = parser.parse_args();report = collect(ROOT,args.plan)
    args.output_report.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    plots(report,json.loads(args.plan.read_text()),args.figures)
