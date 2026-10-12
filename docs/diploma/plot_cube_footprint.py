"""Read-only cube footprint audit; actual source/cube RGB and measured geometric support."""
import argparse,json,sys
from pathlib import Path
import numpy as np
from PIL import Image
import matplotlib.pyplot as plt

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/research'))
from cube_footprint_audit import collect


def plots(report,plan,output):
    output.mkdir(parents=True,exist_ok=True)
    rows=report['rows'];labels=[f"{q['carrier']}\nf{q['frame']}" for q in rows]
    center=np.array([q['cube_center_target_support'] for q in rows]);grid=np.array([q['grid_only_target_support'] for q in rows])
    fig,ax=plt.subplots(figsize=(12,5),layout='constrained')
    ax.bar(labels,center,label='Positive-weight cube texel center target support')
    ax.bar(labels,grid,bottom=center,label='Only stratified pixel-area target support')
    for i,(a,b) in enumerate(zip(center,grid)):ax.text(i,a+b+.3,f'{a}+{b}',ha='center')
    ax.set(ylabel='Earlier source-texel-center residual pixels',ylim=(0,35),
           title='140 frozen residuals: 135 cube-center support + 5 pixel-area-only support')
    ax.legend();fig.savefig(output/'cube_footprint_counts.png',dpi=160);plt.close(fig)
    request_data=json.loads((ROOT/plan['requests']).read_text())
    requests=request_data['requests']
    scene=json.loads((ROOT/plan['output_root']/'scene-truth.json').read_text())
    # First frozen output pixel with only area support, then first colored source request.
    pixel=next(p for g in request_data['groups'] for p in g['residual_pixels']
               if not any(report['requests'][k]['center_support'] for k in p['request_ids']))
    key=next(k for k in pixel['request_ids'] if report['requests'][k]['source_chroma']
             and report['requests'][k]['grid_support'])
    q=requests[key];truth=scene['requests'][key]
    source=np.asarray(Image.open(ROOT/q['source_path']).convert('RGB'))
    cube=np.asarray(Image.open(ROOT/q['face_path']).convert('RGB'))
    fig,axes=plt.subplots(1,2,figsize=(12,5),layout='constrained')
    for ax,image,uv,label in [(axes[0],source,q['pixel'],'Actual fisheye texel (central ray: road)'),
                              (axes[1],cube,q['cube_uv'],'Actual cube RGB (all four central rays: road)')]:
        ax.imshow(image,origin='upper',interpolation='nearest');ax.scatter(*uv,c='yellow',marker='x',s=80)
        ax.set(xlim=(uv[0]-3,uv[0]+3),ylim=(uv[1]+3,uv[1]-3),xlabel='Source x / px',ylabel='Source y / px',title=label)
    for j,((u,v),weight,kind,hits) in enumerate(zip(q['texels'],q['weights'],truth['cube_center_kinds'],truth['cube_grid_target_hits'])):
        axes[1].scatter(u,v,facecolors='none',edgecolors='cyan',s=130)
        axes[1].annotate(f'{j}: w={weight:.3f}\nkind={kind}, grid={hits}/64',(u,v),
                         xytext=(-14 if j%2==0 else 14,18 if j<2 else -32),
                         ha='right' if j%2==0 else 'left',textcoords='offset points',fontsize=8,
                         bbox=dict(facecolor='white',alpha=.9,edgecolor='none'),arrowprops=dict(arrowstyle='-',color='black'))
    fig.suptitle(f"Frozen request {key}, face {q['face']}: area rays diagnose support, not Eevee filter weights")
    fig.savefig(output/'cube_footprint_samples.png',dpi=160);plt.close(fig)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--plan',type=Path,default=ROOT/'configs/research/cube-footprint-plan.json')
    p.add_argument('--output-report',type=Path,required=True)
    p.add_argument('--figures',type=Path,default=Path(__file__).parent/'figures/experiments')
    a=p.parse_args();report=collect(ROOT,a.plan)
    a.output_report.write_text(json.dumps(report,indent=2)+'\n')
    plots(report,json.loads(a.plan.read_text()),a.figures)
