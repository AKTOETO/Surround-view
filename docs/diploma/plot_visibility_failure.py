"""Frozen native visibility failures: central/footprint loss and unobserved target."""
import argparse
import json
import sys
from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT / 'tools/research'))
from visibility_failure import collect, LABELS


def find(report,frame,variant):
    return next(r for r in report['rows'] if r['frame']==frame and r['variant']==variant)


def plots(report,output):
    output.mkdir(parents=True,exist_ok=True)
    fig,axes=plt.subplots(2,2,figsize=(13,8),layout='constrained')
    colors=['gray','firebrick','darkorange','steelblue']
    titles=['Baseline unavailable','All central blocked','Remaining footprint rejected','Source present / proxy missing']
    for frame in [0,1]:
        groups=[([f's_nom_b{b:03d}_e1mm' for b in [0,2,20]],['0','2','20'],'Endpoint bias / mm'),
                ([f's_{s}_b020_e1mm' for s in ['nom','xp','xm','yp','ym','zp','zm']],['nom','xp','xm','yp','ym','zp','zm'],'Box translation ±5mm')]
        for ax,(variants,labels,xlabel) in zip(axes[frame],groups):
            bottom=np.zeros(len(variants))
            for key,title,color in zip(LABELS,titles,colors):
                values=[find(report,frame,v)['visible_loss_partition'][key] for v in variants]
                ax.bar(labels,values,bottom=bottom,label=title,color=color);bottom+=values
            ax.set(title=f'Frame {frame}; support1mm',xlabel=xlabel,ylabel='Missing independent visible target pixels')
            ax.grid(axis='y',alpha=.3)
    handles,labels=axes[0,0].get_legend_handles_labels();fig.legend(handles,labels,loc='outside lower center',ncol=2)
    fig.suptitle('Post-hoc exhaustive loss partition on actual frozen native outputs')
    fig.savefig(output / 'visibility_failure_causes.png',dpi=160);plt.close(fig)
    fig,axes=plt.subplots(1,2,figsize=(12,5),layout='constrained')
    for frame,ax in enumerate(axes):
        variants=['local_100','c_nom_b020','s_nom_b020_e1mm','s_nom_b020_e10mm']
        rows=[find(report,frame,v) for v in variants]
        values=[[r['visible_proxy_hits'] for r in rows],[r['hidden_proxy_hits'] for r in rows],
                [r['visible_target_pixels']-r['visible_proxy_hits'] for r in rows],
                [r['hidden_target_pixels']-r['hidden_proxy_hits'] for r in rows]]
        bottom=np.zeros(4)
        for v,label,color in zip(values,['Visible / proxy hit','Hidden / proxy hit','Visible / proxy missing','Hidden / proxy missing'],['forestgreen','magenta','orange','lightgray']):
            ax.bar(['Off','Center20mm','Support1mm','Support10mm'],v,bottom=bottom,label=label,color=color);bottom+=v
        ax.set(title=f'Frame {frame}',ylabel='Independent full-target pixels');ax.grid(axis='y',alpha=.3)
    handles,labels=axes[0].get_legend_handles_labels();fig.legend(handles,labels,loc='outside lower center',ncol=2)
    fig.suptitle('A full marker proxy can include target surfaces unseen by all source cameras')
    fig.savefig(output / 'visibility_failure_observability.png',dpi=160);plt.close(fig)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,default=ROOT / 'configs/research/visibility-failure-plan.json')
    parser.add_argument('--output-report',type=Path,required=True)
    parser.add_argument('--figures',type=Path,default=Path(__file__).parent / 'figures/experiments')
    args=parser.parse_args();report=collect(ROOT,args.plan)
    args.output_report.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n');plots(report,args.figures)
