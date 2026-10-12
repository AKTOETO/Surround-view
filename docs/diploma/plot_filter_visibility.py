"""Native radial-footprint visibility: joint residual, supported-target and road evaluation."""
import argparse,json,sys
from pathlib import Path
import numpy as np
from PIL import Image
import matplotlib.pyplot as plt
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/research'))
from filter_visibility import collect
from source_correspondence import load_product


def plots(report,plan,output):
    output.mkdir(parents=True,exist_ok=True)
    truth_path=ROOT/plan['truth_path'];truth=json.loads(truth_path.read_text())
    modes=['center','fp0','fp1'];labels=['Center ray','4 bilinear rays','16 guard rays']
    fig,axes=plt.subplots(2,4,figsize=(14,7),layout='constrained')
    for frame in plan['frames']:
        mask=np.load(ROOT/plan['reference_root']/f'frame{frame}/direct-mask.npy')
        images=[np.asarray(Image.open(truth_path.parent/truth['frames'][frame]['rgb']).convert('RGB'))]
        for mode in modes:
            row=next(r for r in report['rows'] if r['frame']==frame and r['variant']=='local_'+mode)
            d=ROOT/row['directory'];m=json.loads((d/'report.json').read_text())
            images.append(load_product(d,next(p for p in m['products'] if p['file']=='output.rgba')))
        for ax,rgb,label in zip(axes[frame],images,['Independent Blender']+labels):
            ax.imshow(rgb);ax.contour(mask.astype(float),[.5],colors=['cyan'],linewidths=.8)
            ax.set(xlim=(180,550),ylim=(359.5,165),title=f'Frame {frame}: {label}',xlabel='Output x / px',ylabel='Output y / px')
    fig.suptitle('Actual native local-carrier RGB; supplied geometry, candidate radial footprint visibility')
    fig.savefig(output/'filter_visibility_rgb.png',dpi=160);plt.close(fig)
    fig,axes=plt.subplots(2,2,figsize=(12,8),layout='constrained')
    metrics=[('outside_expected_pixels','Outside chroma proxy pixels'),('raw_iou','Raw full-frame proxy IoU'),
             ('visible_target_proxy_hits','Source-visible target proxy hits'),('road_linear_rgb_mae','Fixed-road linear RGB MAE')]
    for ax,(key,label) in zip(axes.flat,metrics):
        for frame in plan['frames']:
            for carrier in ['plane','local']:
                rows=[next(r for r in report['rows'] if r['frame']==frame and r['variant']==carrier+'_'+m) for m in modes]
                ax.plot(labels,[r[key] for r in rows],marker='o',linestyle='-' if carrier=='local' else '--',label=f'F{frame} {carrier}')
        ax.set(ylabel=label);ax.tick_params(axis='x',rotation=10);ax.grid(alpha=.3);ax.legend()
    fig.suptitle('Preservation controls: fewer residuals may also remove supported target pixels')
    fig.savefig(output/'filter_visibility_metrics.png',dpi=160);plt.close(fig)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--plan',type=Path,default=ROOT/'configs/research/filter-visibility-plan.json')
    p.add_argument('--output-report',type=Path,required=True)
    p.add_argument('--figures',type=Path,default=Path(__file__).parent/'figures/experiments')
    a=p.parse_args();report=collect(ROOT,a.plan);a.output_report.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n');plots(report,json.loads(a.plan.read_text()),a.figures)
