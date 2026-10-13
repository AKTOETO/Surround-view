"""Visualize the frozen endpoint attribution; not a GPU raster simulator."""
import json
from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np

ROOT=Path(__file__).resolve().parents[2]


def main():
    report=json.loads((ROOT/'docs/validation/baselines/endpoint_precision_v1.json').read_text())
    if not report['all_replays_agree']: raise ValueError('replay/face attribution failed')
    out=Path(__file__).parent/'figures/experiments'
    fig,axes=plt.subplots(1,2,figsize=(11,4),layout='constrained')
    labels=[];gaps=[];colors=[]
    deviations=[]
    for row in report['rows']:
        prefix='nom/accept' if row['inset_m'] else 'Z-5mm/nominal'
        for r in row['records']:
            name='NVIDIA' if r['backend']=='nvidia' else 'Mesa'
            labels.append(prefix+' / '+name)
            gap=float(r['oracle']['gap_m']);gaps.append(gap*1000)
            colors.append('#d62728' if r['oracle']['blocked'] else '#1f77b4')
            deviations.append(max(abs(c['gap_m']-gap) for c in r['controls'])*1e6)
    axes[0].barh(labels,gaps,color=colors);axes[0].invert_yaxis()
    axes[0].axvline(20,color='black',linestyle='--',label='Configured bias 20mm')
    axes[0].set(xlim=(18.8,20.9),xlabel='High-precision entry-to-endpoint gap / mm',title='Same predicate, different\nrecorded endpoints')
    for i,v in enumerate(gaps): axes[0].text(v+.02,i,f'{v:.6f}',va='center',fontsize=8)
    axes[0].legend(fontsize=8)
    axes[1].barh(labels,deviations,color=colors);axes[1].invert_yaxis()
    axes[1].set(xlabel='Max deviation from 80-digit oracle\n(micrometre; eight replay models)',title='Eight host precision-factor replays\nper endpoint')
    fig.suptitle('Two configurations share F1 (207,324); not independent scene trials')
    for ax in axes: ax.title.set_fontsize(10)
    fig.savefig(out/'endpoint_precision_gaps.png',dpi=160);plt.close(fig)
    row=report['rows'][0]
    fig,axes=plt.subplots(1,3,figsize=(13,4),layout='constrained')
    mixes=row['axis_substitutions']
    axes[0].barh(['+'.join(m['mesa_axes']) or 'none (NVIDIA)' for m in mixes],
        [float(m['oracle']['gap_m'])*1000 for m in mixes],
        color=['#d62728' if m['oracle']['blocked'] else '#1f77b4' for m in mixes])
    axes[0].invert_yaxis();axes[0].axvline(20,color='black',linestyle='--')
    axes[0].set(xlim=(18.8,20.9),xlabel='Entry gap / mm',title='Substitute Mesa coordinates\ninto NVIDIA point')
    axes[1].bar(['X','Y','Z'],np.array(row['delta_xyz_m'])*1e6,color=['gray','gray','#9467bd'])
    axes[1].axhline(0,color='black',linewidth=.5)
    axes[1].set(ylabel='Mesa minus NVIDIA / micrometre',title='Recorded carrier-point difference')
    for r in row['records']:
        x,y=np.asarray(r['float_mvp_reprojection_error_px'])*1000
        label='NVIDIA' if r['backend']=='nvidia' else 'Mesa'
        axes[2].scatter(x,y,label=label)
        axes[2].annotate(label,(x,y),xytext=(4,4),textcoords='offset points')
    axes[2].scatter(0,0,marker='+',color='black')
    axes[2].set(xlabel='X reprojection error / milli-pixel',ylabel='Y reprojection error / milli-pixel',
        title='Rounded-MVP diagnostic,\nnot driver tracing',xlim=(-.15,1.35),ylim=(-.15,1.35))
    fig.suptitle('Counterfactual axis substitutions; vertex snapping not identified\nRed = blocked; blue = not blocked')
    for ax in axes: ax.title.set_fontsize(10)
    fig.savefig(out/'endpoint_precision_axes.png',dpi=160);plt.close(fig)


if __name__=='__main__':main()
