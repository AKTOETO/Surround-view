"""Visualize frozen residual geometry truth; does not render the product."""
import json
from pathlib import Path
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap,BoundaryNorm
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]


def main():
    report=json.loads((ROOT/'docs/validation/baselines/residual_surface_v1.json').read_text())
    if not report['oracle_all_passed']:raise ValueError('triangle oracle validation failed')
    out=Path(__file__).parent/'figures/experiments'
    labels=[];values=[];colors=[]
    for row in report['rows']:
        for s in row['sources']:
            if not s['projection_valid']:continue
            labels.append(f"F{row['frame']} ({row['y']},{row['x']}) C{s['camera']}"+(' [projected]' if s['uv_kind']!='native_retained' else ''))
            values.append(s['distance_to_virtual_hit_m'])
            colors.append('#d62728' if s['center_kind']!=row['virtual_kind'] else '#2ca02c' if s['matched']['0.002'] else '#ff7f0e' if s['matched']['0.02'] else '#9467bd')
    fig,ax=plt.subplots(figsize=(13,7),layout='constrained')
    for i,(v,color) in enumerate(zip(values,colors)):
        if v is None:ax.text(.04,i,'No scene hit (not zero error)',va='center',color='gray')
        else:
            ax.barh(i,v*1000,left=.001,color=color)
            ax.text(v*1000*1.08,i,f'{v*1000:.3f} mm',va='center',fontsize=9)
    ax.set_yticks(range(len(labels)),labels);ax.invert_yaxis();ax.set_xscale('log')
    ax.set(xlim=(.03,3000),xlabel='Distance between nearest source and virtual scene hit / mm (log)',title='All projection-valid alternatives at nine residual pixels\nGreen: same object <=2mm; orange: <=20mm; purple: same object farther; red: different object')
    ax.axvline(2,color='gray',linestyle='--');ax.axvline(20,color='gray',linestyle=':');ax.grid(axis='x',alpha=.25)
    fig.savefig(out/'residual_surface_errors.png',dpi=160);plt.close(fig)
    matrix=[];labels=[]
    for row in report['rows']:
        labels.append(f"F{row['frame']} ({row['y']},{row['x']})")
        matrix.append([2 if c['matched_eligible']['0.02'] else 1 if c['eligible_cameras'] else 0 for c in row['cases']])
    sample=report['rows'][0]['cases']
    assert all([c['variant'] for c in row['cases']]==[c['variant'] for c in sample] for row in report['rows']), 'condition order differs'
    columns=[]
    for c in sample:
        variant=c['variant'];shift='Z+5mm' if '_zp_' in variant else 'Z-5mm' if '_zm_' in variant else 'nom'
        band='10mm' if 'e10mm' in variant else '1mm'
        policy=variant.split('-')[-2]
        columns.append(f'{shift}/{band}/{policy}')
    fig,ax=plt.subplots(figsize=(15,6),layout='constrained')
    im=ax.imshow(matrix,cmap=ListedColormap(['#303030','#ff7f0e','#2ca02c']),norm=BoundaryNorm([-.5,.5,1.5,2.5],3),aspect='auto',interpolation='nearest')
    ax.set_yticks(range(len(labels)),labels);ax.set_xticks(range(18),columns,rotation=65,ha='right')
    ax.set(title='Historical native eligibility vs independently matched source (20mm point tolerance)\nSelected failure coordinates; no holdout or full-frame quality claim',xlabel='Historical supplied-box condition')
    bar=fig.colorbar(im,ax=ax,ticks=[0,1,2],fraction=.025);bar.ax.set_yticklabels(['No eligible source','Eligible, none matched','Matched eligible source'])
    fig.savefig(out/'residual_surface_support.png',dpi=160);plt.close(fig)
    truth=json.loads((ROOT/'artifacts/parallax-sensor-inputs-v1/rig101-near-z080/seed101-capture/paired_truth.json').read_text())
    base=ROOT/'artifacts/parallax-sensor-inputs-v1/rig101-near-z080/seed101-capture'
    fig,axes=plt.subplots(1,2,figsize=(12,5),layout='constrained')
    for frame,ax in enumerate(axes):
        ax.imshow(np.asarray(Image.open(base/truth['frames'][frame]['rgb']).convert('RGB')))
        for row in report['rows']:
            if row['frame']!=frame:continue
            ax.plot(row['x'],row['y'],'o',mfc='none',mec='cyan',ms=8)
            ax.annotate(f"{row['y']},{row['x']}",(row['x'],row['y']),xytext=(8,3),textcoords='offset points',color='cyan',fontsize=9)
        ax.set(xlim=(300,338),ylim=(223,193),title=f'Independent Blender virtual RGB F{frame}',xlabel='Output x / px',ylabel='Output y / px')
    fig.suptitle('All nine selected residual coordinates, including the road pixel; RGB is archived truth')
    fig.savefig(out/'residual_surface_pixels.png',dpi=160);plt.close(fig)


if __name__=='__main__':main()
