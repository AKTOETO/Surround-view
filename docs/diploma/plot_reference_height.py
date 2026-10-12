"""Native supplied-height ablation: actual RGB and independent target/road metrics."""
import argparse,json,sys
from pathlib import Path
import numpy as np
from PIL import Image
import matplotlib.pyplot as plt
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/research'))
from reference_height import collect
from source_correspondence import load_product


def plots(report,plan,output):
    output.mkdir(parents=True,exist_ok=True)
    truth=json.loads((ROOT/plan['truth_path']).read_text());capture=(ROOT/plan['truth_path']).parent
    fig,axes=plt.subplots(2,5,figsize=(15,7),layout='constrained')
    for frame in plan['frames']:
        mask=np.load(ROOT/plan['reference_root']/f'frame{frame}/direct-mask.npy')
        images=[np.asarray(Image.open(capture/truth['frames'][frame]['rgb']).convert('RGB'))]
        for row in [r for r in report['rows'] if r['frame']==frame]:
            d=ROOT/row['directory'];m=json.loads((d/'report.json').read_text())
            images.append(load_product(d,next(p for p in m['products'] if p['file']=='output.rgba')))
        for ax,image,title in zip(axes[frame],images,['Independent Blender']+[f'Assumed h={h:.1f} m' for h in plan['heights_m']]):
            ax.imshow(image,origin='upper');ax.contour(mask.astype(float),levels=[.5],colors=['cyan'],linewidths=.8)
            ax.set(xlim=(180,550),ylim=(359.5,165),title=f'Frame {frame}: {title}',xlabel='Output x / px',ylabel='Output y / px')
    fig.suptitle('Actual native RGB: supplied global height may move color into expected ROI and distort the road')
    fig.savefig(output/'reference_height_rgb.png',dpi=160);plt.close(fig)
    fig,axes=plt.subplots(2,2,figsize=(10,7),layout='constrained')
    metrics=[('raw_recall','Raw expected-target proxy recall'),('raw_iou','Raw full-frame proxy IoU'),
             ('carrier_to_virtual_target_distance_m','Carrier / virtual target distance, median / m'),
             ('road_linear_rgb_mae','Fixed independent road ROI linear RGB MAE')]
    for ax,(key,label) in zip(axes.flat,metrics):
        for frame in plan['frames']:
            rows=[r for r in report['rows'] if r['frame']==frame]
            y=[r[key]['p50'] if isinstance(r[key],dict) else r[key] for r in rows]
            ax.plot([r['height_m'] for r in rows],y,marker='o',label=f'Frame {frame}')
        ax.set(xlabel='Supplied reference height / m',ylabel=label);ax.grid(alpha=.25);ax.legend()
    fig.suptitle('Known-height sensitivity control; not RGB depth estimation or a general mitigation')
    fig.savefig(output/'reference_height_metrics.png',dpi=160);plt.close(fig)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--plan',type=Path,default=ROOT/'configs/research/reference-height-plan.json')
    p.add_argument('--output-report',type=Path,required=True)
    p.add_argument('--figures',type=Path,default=Path(__file__).parent/'figures/experiments')
    a=p.parse_args();r=collect(ROOT,a.plan);a.output_report.write_text(json.dumps(r,indent=2,allow_nan=False)+'\n');plots(r,json.loads(a.plan.read_text()),a.figures)
