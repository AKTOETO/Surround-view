"""Read-only new-domain audit with actual RGB and independent local ray/texel support figures."""
import argparse,json,sys
from pathlib import Path
import numpy as np
import matplotlib.pyplot as plt
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/research'))
from hybrid_domain_audit import collect,read_native


def plots(report,plan,output):
    output.mkdir(parents=True,exist_ok=True)
    variants=[(r['mode'],r['boundary']) for r in report['rows'][:6]]
    labels=['seam EDT\nzero','cut\nzero','MB\nzero','cut+MB\nzero','MB\nnormalized','cut+MB\nnormalized']
    sums=[]
    for mode,boundary in variants:
        rows=[r for r in report['rows'] if (r['mode'],r['boundary'])==(mode,boundary)]
        sums.append({k:sum(r[k] for r in rows) for k in ['pixels','source_target_texel_pixels','no_target_center_or_texel_pixels','nonlocal_invalid_camera_pixels','zero_local_weight_contributing_pixels']})
    fig,axes=plt.subplots(1,2,figsize=(13,5),layout='constrained')
    a=np.array([r['source_target_texel_pixels'] for r in sums]);b=np.array([r['no_target_center_or_texel_pixels'] for r in sums])
    axes[0].bar(labels,a,label='Local source target texel center support')
    axes[0].bar(labels,b,bottom=a,label='No local source target center/texel')
    axes[0].set(ylabel='Repeated condition pixels',title='New domain: local central target-ray support is zero')
    positions=np.arange(6)
    axes[1].bar(positions-.2,[r['nonlocal_invalid_camera_pixels'] for r in sums],.4,label='Locally invalid camera contributes')
    axes[1].bar(positions+.2,[r['zero_local_weight_contributing_pixels'] for r in sums],.4,label='Zero local fusion weight camera contributes')
    axes[1].set(xticks=positions,xticklabels=labels,ylabel='Repeated condition pixels',title='Signed contribution magnitude > 1e-6 linear RGB')
    for ax in axes:ax.legend(fontsize=8);ax.grid(axis='y',alpha=.2)
    fig.suptitle('1227 repeated condition pixels / 547 frame-carrier coordinates; no quality ranking or causal color labels')
    fig.savefig(output/'hybrid_domain_counts.png',dpi=160);plt.close(fig)
    request=json.loads((ROOT/plan['requests']).read_text())
    group=next(g for g in request['groups'] if g['frame']==0 and g['carrier']=='plane')
    index_path=ROOT/group['index_path'];index=json.loads(index_path.read_text())
    d=ROOT/plan['output_root']/'frame0/plane'
    kinds=np.load(d/'texel-kinds.npy');weights=np.load(d/'texel-weights.npy')
    support=((kinds==1)&(weights>0)).any(axis=(1,2))
    coords=np.asarray(group['pixels_yx'])
    fig,axes=plt.subplots(2,3,figsize=(13,8),layout='constrained')
    for ax,selection,label in zip(axes.flat,group['cases'],labels):
        case=next(c for c in index['cases'] if c['directory']==selection['directory'])
        rgb=read_native(index_path.parent/case['directory'],case,index,'actual.rgba',4)
        ax.imshow(rgb,origin='upper');ids=np.asarray(selection['union_indices'],int)
        for mask,color,name in [(support,'lime','Target in local texel centers'),(~support,'cyan','No local center/texel target')]:
            chosen=ids[mask[ids]];xy=coords[chosen]
            ax.scatter(xy[:,1],xy[:,0],s=12,facecolors='none',edgecolors=color,linewidths=.7,label=name)
        ax.set(xlim=(180,550),ylim=(359.5,165),title=label.replace('\n',' / '),xlabel='Output x / px',ylabel='Output y / px')
    handles,legend=axes.flat[0].get_legend_handles_labels();fig.legend(handles,legend,loc='outside lower center',ncol=2)
    fig.suptitle('Actual native RGB / plane frame0; only new chroma coordinates marked. Virtual center rays: road.')
    fig.savefig(output/'hybrid_domain_rgb.png',dpi=160);plt.close(fig)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--plan',type=Path,default=ROOT/'configs/research/hybrid-domain-plan.json')
    p.add_argument('--output-report',type=Path,required=True)
    p.add_argument('--figures',type=Path,default=Path(__file__).parent/'figures/experiments')
    a=p.parse_args();r=collect(ROOT,a.plan);a.output_report.write_text(json.dumps(r,indent=2,allow_nan=False)+'\n')
    plots(r,json.loads(a.plan.read_text()),a.figures)
