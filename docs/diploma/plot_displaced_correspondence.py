"""Actual RGB/point-ray attribution and a measured occlusion ray; no new renderer."""
import argparse
import json
from pathlib import Path
import sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/research'))
from displaced_correspondence import collect
from source_correspondence import load_product,verified
from source_correspondence_controls import transform_points


def data(plan,row):
    native=ROOT/plan['native_root']/f"frame{row['frame_index']}"/'native'/row['carrier']
    metadata=json.loads(verified(native/'report.json',row['native_report_sha256']).read_text())
    products={p['file']:load_product(native,p) for p in metadata['products']}
    directory=ROOT/plan['output_root']/f"frame{row['frame_index']}"/row['carrier']
    arrays={n:np.load(verified(directory/n,h),allow_pickle=False) for n,h in row['scene_record']['files'].items()}
    return metadata,products,arrays


def plot(report,plan,figures):
    figures.mkdir(parents=True,exist_ok=True)
    rows=[r for r in report['rows'] if r['carrier'] in ['plane','bowl']]
    fig,axes=plt.subplots(2,2,figsize=(12,8))
    for row,ax in zip(rows,axes.ravel()):
        metadata,products,arrays=data(plan,row)
        ax.imshow(products['output.rgba'])
        y,x=arrays['pixels.npy'].T
        support=(arrays['source-kinds.npy']==1).any(axis=1)
        overlay=np.zeros((*products['output.rgba'].shape[:2],4))
        overlay[y[support],x[support]]=[.1,1,.1,.45]
        overlay[y[~support],x[~support]]=[1,.6,0,.9]
        ax.imshow(overlay)
        expected=np.load(ROOT/plan['native_root']/f"frame{row['frame_index']}"/'direct-mask.npy')
        ax.contour(expected,levels=[.5],colors=['cyan'],linewidths=1)
        ax.set_xlim(150,580);ax.set_ylim(359.5,170)
        ax.set_title(f"{row['carrier']}, frame {row['frame_index']}: {support.sum()}/{len(support)} ray-supported")
        ax.set_xlabel('Output X / px');ax.set_ylabel('Output Y / px')
    fig.legend(handles=[Line2D([0],[0],color='lime',label='At least one native source ray hits target'),
                        Line2D([0],[0],color='orange',label='RGB proxy without target point-ray hit'),
                        Line2D([0],[0],color='cyan',label='Independent expected target mask')],loc='upper center',bbox_to_anchor=(.5,.96),ncol=1,fontsize=9)
    fig.suptitle('Actual edge-feather RGB with independent Blender point-ray classification')
    fig.tight_layout(rect=(0,0,1,.85));fig.savefig(figures/'displaced_source_support.png',dpi=150);plt.close(fig)
    row=next(r for r in report['rows'] if r['frame_index']==0 and r['carrier']=='plane')
    metadata,products,arrays=data(plan,row)
    candidates=np.flatnonzero((arrays['source-kinds.npy']==1).any(axis=1))
    n=candidates[len(candidates)//2];y,x=arrays['pixels.npy'][n]
    archived=json.loads((ROOT/plan['input_root']/'seed101-capture/paired_truth.json').read_text())
    ego=np.asarray(archived['frames'][0]['T_world_from_vehicle'])
    carrier=transform_points(ego,products['carrier-positions.f32'][y,x])
    camera_id=int(np.flatnonzero(arrays['source-kinds.npy'][n]==1)[0])
    pose=ego@np.linalg.inv(metadata['config']['cameras'][camera_id]['T_camera_from_vehicle'])
    origin=pose[:3,3];hit=arrays['source-hits.npy'][n,camera_id];direct=arrays['virtual-points.npy'][n]
    fig=plt.figure(figsize=(10,7));ax=fig.add_subplot(projection='3d')
    def line(points,color,label,style='-'):
        p=np.asarray(points);ax.plot(p[:,0],p[:,1],p[:,2],color=color,linestyle=style,label=label)
    line([origin,hit],'#bd3030',f'Camera {camera_id} → nearest target hit')
    line([hit,carrier],'#bd3030','Occluded segment to carrier','--')
    ax.scatter(*carrier,color='black',label='Native carrier point')
    ax.scatter(*direct,color='cyan',marker='x',s=70,label='Virtual nearest road point')
    ax.scatter(*origin,color='blue',label='Source camera origin')
    center=np.asarray(archived['frames'][0]['diagnostic_target_position_m'])
    case=json.loads((ROOT/plan['input_root']/'plan.json').read_text())['cases'][0]
    size=np.asarray(case['target']['size_m'])
    corners=np.array([center+size*np.array([i,j,k])/2 for i in [-1,1] for j in [-1,1] for k in [-1,1]])
    for i in range(8):
        for j in range(i+1,8):
            if bin(i^j).count('1')==1:line(corners[[i,j]],'#b040a0',None)
    low=np.minimum(np.minimum(origin,carrier),center-size/2);high=np.maximum(np.maximum(origin,carrier),center+size/2)
    ax.set_box_aspect(np.maximum(high-low,.3));ax.set(xlabel='World X / m',ylabel='World Y / m',zlabel='World Z / m')
    ax.set_title(f'Actual output pixel ({x},{y}); target blocks source ray before the road carrier\n'
                 'Native point and virtual road hit almost coincide in this floor view')
    ax.legend(fontsize=8);fig.tight_layout();fig.savefig(figures/'displaced_occlusion_ray.png',dpi=150);plt.close(fig)
    return {'frame_index':0,'carrier':'plane','output_pixel':[int(x),int(y)],'source_camera_id':camera_id}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,default=ROOT/'configs/research/displaced-correspondence-plan.json')
    parser.add_argument('--output-report',type=Path,required=True)
    parser.add_argument('--figures',type=Path,default=Path(__file__).parent/'figures/experiments')
    args=parser.parse_args();plan_path=args.plan.resolve();report=collect(ROOT,plan_path)
    args.output_report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(plot(report,json.loads(plan_path.read_text()),args.figures)))


if __name__=='__main__':main()
