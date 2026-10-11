"""Figures for an isolated geometric control; no simulated RGB quality plots."""
import argparse
import json
from pathlib import Path
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import LogNorm
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/research'))
from planar_correspondence import collect
from source_correspondence import load_product,verified
from source_correspondence_controls import transform_points


def plot(report,plan,figures):
    figures.mkdir(parents=True,exist_ok=True)
    names=plan['carriers'];x=np.arange(len(names))
    fig,axes=plt.subplots(1,2,figsize=(13,5))
    for frame,offset,color in [(0,-.08,'#2874a6'),(1,.08,'#bc3030')]:
        rows=[r for r in report['rows'] if r['frame_index']==frame]
        axes[0].plot(x+offset,[r['source_to_direct_plane_m']['max']*1000 for r in rows],'o-',color=color,label=f'Frame {frame}')
        axes[1].plot(x+offset,[r['correct_vs_native_uv_px']['max'] for r in rows],'o-',color=color,label=f'Frame {frame}')
    axes[0].axhline(plan['planar_tolerance_m']*1000,color='black',linestyle='--',label='Frozen 0.1mm threshold')
    axes[1].axhline(plan['uv_tolerance_px'],color='black',linestyle='--',label='Frozen 0.01px threshold')
    for ax in axes:
        ax.set_xticks(x,['plane','bowl','dome','cylinder','cube']);ax.set_yscale('log');ax.grid(axis='y',alpha=.3);ax.legend(fontsize=8)
    axes[0].set_ylabel('Max source hit to direct plane point / mm')
    axes[1].set_ylabel('Max correct-plane to native UV gap / source pixels')
    fig.suptitle('Isolated Z=0 geometry; 5/8 exact-floor conditions pass both frozen thresholds\nBowl is curved, descriptive only; no planar RGB truth')
    fig.tight_layout(rect=(0,0,1,.9));fig.savefig(figures/'planar_control_metrics.png',dpi=150);plt.close(fig)
    archived=json.loads((ROOT/plan['truth_path']).read_text())
    scene=json.loads((ROOT/plan['output_root']/'scene-truth.json').read_text())
    fig,axes=plt.subplots(2,2,figsize=(10,7),layout='constrained')
    for row,frame in enumerate(plan['frames']):
        record=next(r for r in scene['records'] if r['frame_index']==frame)
        directory=ROOT/plan['output_root']/f'frame{frame}'
        coordinates=np.load(verified(directory/'pixels.npy',record['files']['pixels.npy']))
        truth=np.load(verified(directory/'plane-points.npy',record['files']['plane-points.npy']))
        for col,carrier in enumerate(['plane','bowl']):
            native=ROOT/plan['native_root']/f'frame{frame}'/'native'/carrier
            metadata=json.loads((native/'report.json').read_text())
            product=next(p for p in metadata['products'] if p['file']=='carrier-positions.f32')
            y,x=coordinates.T
            points=load_product(native,product)[y,x]
            world=transform_points(archived['frames'][frame]['T_world_from_vehicle'],points)
            error=np.linalg.norm(world-truth,axis=-1)*1000
            ax=axes[row,col]
            scatter=ax.scatter(x,y,c=error,s=10,norm=LogNorm(vmin=.001,vmax=20),cmap='viridis')
            ax.invert_yaxis();ax.set_aspect('equal');ax.set_title(f'{carrier}, frame {frame}')
            ax.set_xlabel('Preselected output X / px');ax.set_ylabel('Preselected output Y / px')
    fig.colorbar(scatter,ax=axes,label='Carrier point to isolated plane truth / mm',shrink=.75)
    fig.suptitle('Actual native carrier points on the preselected ROI; geometry, not RGB')
    fig.savefig(figures/'planar_control_points.png',dpi=150);plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,default=ROOT/'configs/research/planar-correspondence-plan.json')
    parser.add_argument('--output-report',type=Path,required=True)
    parser.add_argument('--figures',type=Path,default=Path(__file__).parent/'figures/experiments')
    args=parser.parse_args();plan_path=args.plan.resolve()
    report=collect(ROOT,plan_path)
    args.output_report.write_text(json.dumps(report,indent=2)+'\n')
    plot(report,json.loads(plan_path.read_text()),args.figures)


if __name__=='__main__':main()
