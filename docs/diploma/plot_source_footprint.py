"""Reaudit frozen four-texel evidence and plot measured counts / actual source samples."""
import argparse
import json
from pathlib import Path
import sys
import numpy as np
from PIL import Image
import matplotlib.pyplot as plt

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/research'))
from source_footprint_audit import collect
from source_correspondence import load_product


def plots(report,plan,output):
    output.mkdir(parents=True,exist_ok=True)
    rows=report['rows'];labels=[f"{r['carrier']}\nf{r['frame_index']}" for r in rows]
    supported=np.array([r['mismatch']['positive_weight_target_texel_pixels'] for r in rows])
    residual=np.array([r['mismatch']['no_target_center_texel_pixels'] for r in rows])
    fig,ax=plt.subplots(figsize=(13,5),layout='constrained')
    ax.bar(labels,supported,label='Positive-weight target texel center')
    ax.bar(labels,residual,bottom=supported,label='No target center among four texels')
    for i,(a,b) in enumerate(zip(supported,residual)):ax.text(i,a+b+2,f'{a}+{b}',ha='center')
    ax.set(ylabel='Earlier point-ray / RGB mismatch pixels',ylim=(0,225),
           title='1164 frozen mismatches: 1024 have neighboring target-center support; 140 remain')
    ax.legend();fig.savefig(output/'source_footprint_counts.png',dpi=160);plt.close(fig)
    directory=ROOT/plan['output_root']/'frame0/plane'
    arrays={p.stem:np.load(p,allow_pickle=False) for p in directory.glob('*.npy')}
    native=ROOT/plan['native_root']/'frame0/native/plane'
    meta=json.loads((native/'report.json').read_text())
    products={p['file']:load_product(native,p) for p in meta['products']}
    has=((arrays['texel-kinds']==1)&(arrays['weights']>0)).any(axis=(1,2))
    fig,axes=plt.subplots(1,2,figsize=(11,5),layout='constrained')
    for ax,supported in zip(axes,[True,False]):
        candidates=np.flatnonzero(arrays['mismatch']&(has==supported))
        # Fixed row-major first representative, not a best-looking example.
        n=int(candidates[0]);y,x=arrays['pixels'][n];camera=3
        image=np.asarray(Image.open(ROOT/meta['inputs'][camera]['path']).convert('RGB'))
        uv=products[f'camera{camera}-pixels.f32'][y,x]
        xy=arrays['texels'][n,camera];weights=arrays['weights'][n,camera]
        kinds=arrays['texel-kinds'][n,camera]
        ax.imshow(image,origin='upper')
        for j,((u,v),weight,kind) in enumerate(zip(xy,weights,kinds)):
            ax.scatter(u,v,facecolors='none',edgecolors='lime' if kind==1 else 'cyan',s=160)
            ax.annotate(f'{j}: kind={kind}, w={weight:.3f}',(u,v),xytext=(-16 if j%2==0 else 16,18 if j<2 else -24),
                        ha='right' if j%2==0 else 'left',textcoords='offset points',fontsize=8,
                        bbox=dict(facecolor='white',alpha=.85,edgecolor='none'),arrowprops=dict(arrowstyle='-',color='black'))
        ax.scatter(*uv,c='yellow',marker='x',s=80,label='Actual native sample')
        ax.set(xlim=(uv[0]-3,uv[0]+3),ylim=(uv[1]+3,uv[1]-3),xlabel='Source x / px',ylabel='Source y / px',
               title=f"{'Neighbor target support' if supported else 'Residual filtered color'}\nplane f0 output ({x},{y}), camera3")
        ax.legend(loc='upper left',fontsize=8)
    fig.suptitle('Actual RGB texels; kind 1=target, 2=road. Center identity is not filtered color identity.')
    fig.savefig(output/'source_footprint_samples.png',dpi=160);plt.close(fig)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--plan',type=Path,default=ROOT/'configs/research/source-footprint-plan.json')
    p.add_argument('--output-report',type=Path,required=True)
    p.add_argument('--figures',type=Path,default=Path(__file__).parent/'figures/experiments')
    a=p.parse_args();report=collect(ROOT,a.plan)
    a.output_report.write_text(json.dumps(report,indent=2)+'\n')
    plots(report,json.loads(a.plan.read_text()),a.figures)
