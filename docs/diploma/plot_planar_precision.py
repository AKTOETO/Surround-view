"""Read-only paired-backend precision figures, not RGB quality ranking."""
import argparse
import json
from pathlib import Path
import sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/research'))
from planar_precision import collect


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,default=ROOT/'configs/research/planar-precision-plan.json')
    parser.add_argument('--output-report',type=Path,required=True)
    parser.add_argument('--figures',type=Path,default=Path(__file__).parent/'figures/experiments')
    args=parser.parse_args();plan_path=args.plan.resolve()
    report=collect(ROOT,plan_path);args.output_report.write_text(json.dumps(report,indent=2)+'\n')
    rows=[r for r in report['rows'] if r['planar_geometry_required']]
    labels=[f"{r['carrier'].replace('_floor','')}\nf{r['frame_index']}" for r in rows]
    x=np.arange(len(rows));fig,axes=plt.subplots(1,3,figsize=(16,5))
    for index,label,color,offset in [(0,'NVIDIA','#2874a6',-.08),(1,'Mesa llvmpipe','#bd3030',.08)]:
        values=[r['backends'][index] for r in rows]
        for ax,key,scale in zip(axes,['source_to_ideal_plane_m','correct_vs_native_uv_px','native_output_reprojection_px'],[1000,1,1]):
            ax.plot(x+offset,[r[key]['max']*scale for r in values],'o-',color=color,label=label)
    axes[0].axhline(.1,color='black',linestyle='--',label='Frozen 0.1mm threshold')
    axes[1].axhline(.01,color='black',linestyle='--',label='Frozen 0.01px threshold')
    axes[1].plot(x,[r['backends'][0]['uniform_rounding_only_uv_px']['max'] for r in rows],'^:',color='#438632',label='Float32 MVP rounding only')
    for ax,label in zip(axes,['Max source-to-plane gap / mm','Max source UV gap / px','Max output reprojection gap / px']):
        ax.set_yscale('log');ax.set_xticks(x,labels,fontsize=8);ax.set_ylabel(label);ax.grid(axis='y',alpha=.3);ax.legend(fontsize=7)
    fig.suptitle('Same archived binary / inputs; exact floor ROI; double analytical plane oracle\n8/8 Mesa and 5/8 NVIDIA strict pass; no RGB quality or timing ranking')
    fig.tight_layout(rect=(0,0,1,.88));args.figures.mkdir(parents=True,exist_ok=True)
    fig.savefig(args.figures/'planar_precision_backends.png',dpi=150);plt.close(fig)


if __name__=='__main__':main()
