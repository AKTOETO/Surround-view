"""Matched native hybrid RGB and signed-band figures; no renderer or quality ranking."""
import argparse
import json
from pathlib import Path
import sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/research'))
from hybrid_correspondence import collect
from source_correspondence import verified,load_product


def label(row):
    return row['mode'].replace('seam_distance_feather','seam EDT').replace('graph_cut_multi_band','cut+MB').replace('graph_cut_seam','cut').replace('multi_band','MB')+' / '+row['boundary']


def plot(report,plan,figures):
    figures.mkdir(parents=True,exist_ok=True)
    rows=[r for r in report['rows'] if r['frame_index']==0 and r['carrier']=='plane']
    reference=ROOT/plan['reference_root']/'frame0'
    metadata=json.loads((reference/'native/plane/report.json').read_text())
    product=next(p for p in metadata['products'] if p['file']=='output.rgba')
    edge=load_product(reference/'native/plane',product)
    archived=json.loads((ROOT/plan['truth_path']).read_text());frame=archived['frames'][0]
    truth=np.asarray(Image.open(verified(ROOT/plan['input_root']/'seed101-capture'/frame['rgb'],archived['sha256'][frame['rgb']])) )
    images=[truth,edge]+[np.fromfile(verified(ROOT/r['directory']/'actual.rgba',r['files']['actual.rgba']['sha256']),np.uint8).reshape(360,640,4) for r in rows]
    titles=['Independent Blender RGB','Edge feather (reference)']+[label(r) for r in rows]
    mask=np.load(reference/'direct-mask.npy')
    fig,axes=plt.subplots(2,4,figsize=(16,8))
    for ax,image,title in zip(axes.ravel(),images,titles):
        ax.imshow(image);ax.contour(mask,levels=[.5],colors=['cyan'],linewidths=.8)
        ax.set_xlim(180,550);ax.set_ylim(359.5,165);ax.set_title(title,fontsize=10)
        ax.set_xlabel('Output X / px');ax.set_ylabel('Output Y / px')
    fig.suptitle('Matched frame 0 / plane: actual RGB; cyan = independent expected target\nEvery hybrid variant still has zero raw target-color overlap with the expected mask')
    fig.tight_layout(rect=(0,0,1,.91));fig.savefig(figures/'hybrid_correspondence_rgb.png',dpi=150);plt.close(fig)
    fig,axes=plt.subplots(2,2,figsize=(13,8))
    for frame in [0,1]:
        for carrier,color in [('plane','#2874a6'),('bowl','#bd3030')]:
            values=[r for r in report['rows'] if r['frame_index']==frame and r['carrier']==carrier]
            roi=[r['rois']['source_visible_target'] for r in values];x=np.arange(len(values))
            axes[0,frame].plot(x,[v['nonlocal_invalid_max_abs_linear_rgb'] for v in roi],'o-',color=color,label=carrier)
            axes[1,frame].plot(x,[v['pixels_with_nonlocal_invalid_camera'] for v in roi],'o-',color=color,label=carrier)
        for ax in axes[:,frame]:
            ax.set_xticks(x,[label(r).replace(' / ','\n') for r in values],fontsize=8);ax.grid(axis='y',alpha=.3);ax.legend()
        axes[0,frame].set_title(f'Frame {frame}: max signed contribution magnitude')
        axes[0,frame].set_ylabel('Max abs linear RGB from locally invalid cameras')
        axes[1,frame].set_title(f'Frame {frame}: source-visible expected target ROI')
        axes[1,frame].set_ylabel('Pixels with locally invalid camera contribution > 1e-6')
    fig.suptitle('Nonlocal contributions are measured before clamp / composition; not object identity or opacity')
    fig.tight_layout(rect=(0,0,1,.94));fig.savefig(figures/'hybrid_correspondence_nonlocal.png',dpi=150);plt.close(fig)
    fig,axes=plt.subplots(1,2,figsize=(12,5))
    for frame,ax in enumerate(axes):
        values=[r for r in report['rows'] if r['frame_index']==frame and r['carrier']=='plane' and len(r['levels'])>1]
        for row in values:
            ax.plot([level['level'] for level in row['levels']],
                    [level['rois']['source_visible_target']['mean_abs_signed_linear_rgb'] for level in row['levels']],
                    'o-',label=label(row))
        ax.set_title(f'Plane, frame {frame}');ax.set_xticks([0,1,2,3]);ax.set_xlabel('Native level (fine → coarse)')
        ax.set_ylabel('Mean abs propagated signed band / linear RGB');ax.grid(alpha=.3);ax.legend(fontsize=8)
    fig.suptitle('Per-level signed bands propagated with native level sizes; magnitudes are not additive probabilities')
    fig.tight_layout(rect=(0,0,1,.93));fig.savefig(figures/'hybrid_correspondence_levels.png',dpi=150);plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,default=ROOT/'configs/research/hybrid-correspondence-plan.json')
    parser.add_argument('--output-report',type=Path,required=True)
    parser.add_argument('--figures',type=Path,default=Path(__file__).parent/'figures/experiments')
    args=parser.parse_args();plan_path=args.plan.resolve()
    report=collect(ROOT,plan_path);args.output_report.write_text(json.dumps(report,indent=2)+'\n')
    plot(report,json.loads(plan_path.read_text()),args.figures)


if __name__=='__main__':main()
