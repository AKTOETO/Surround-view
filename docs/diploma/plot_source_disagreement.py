"""Plot native diagnostic and bounded audit; no product-side computation in Python."""
import argparse
import json
from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np

ROOT=Path(__file__).resolve().parents[2]


def main(allow_pairing_failures=False):
    report=json.loads((ROOT/'docs/validation/baselines/source_disagreement_v1.json').read_text())
    if not report['all_oracles_passed'] and not allow_pairing_failures:
        raise ValueError('historical pairing failed; explicit --allow-pairing-failures needed to plot failures')
    out=Path(__file__).parent/'figures/experiments'
    points=sorted({(r['frame'],r['y'],r['x']) for r in report['rows']})
    variants=list(dict.fromkeys(r['variant'] for r in report['rows'] if r['frame']==0))
    counts=np.zeros((len(points),len(variants)))
    spread=np.full_like(counts,np.nan)
    for row in report['rows']:
        i=points.index((row['frame'],row['y'],row['x']));j=variants.index(row['variant'])
        counts[i,j]=row['native_pixel'][1]
        if row['native_pixel'][2]: spread[i,j]=row['native_pixel'][0]
    labels=[f'F{f} ({y},{x})' for f,y,x in points]
    columns=[]
    for variant in variants:
        shift='Z+5mm' if '_zp_' in variant else 'Z-5mm' if '_zm_' in variant else 'nom'
        band='10mm' if 'e10mm' in variant else '1mm'
        columns.append(f'{shift}/{band}/{variant.split("-")[-2]}')
    fig,axes=plt.subplots(2,1,figsize=(14,8),layout='constrained',sharex=True)
    cmap=plt.get_cmap('magma').copy();cmap.set_bad('#b0b0b0')
    for ax,data,title,colors,maximum in zip(axes,[counts,spread],
        ['Historical selected coordinates: native eligible source count',
         'Native encoded RGB range; gray = undefined (<2 cameras), not zero'],
        ['viridis',cmap],[4,255]):
        im=ax.imshow(data,cmap=colors,vmin=0,vmax=maximum,aspect='auto',interpolation='nearest')
        ax.set_yticks(range(len(labels)),labels);ax.set_title(title)
        fig.colorbar(im,ax=ax,fraction=.025)
    axes[-1].set_xticks(range(len(columns)),columns,rotation=65,ha='right')
    for row in report['rows']:
        if not row['oracle_passed']:
            i=points.index((row['frame'],row['y'],row['x']));j=variants.index(row['variant'])
            for ax in axes: ax.text(j,i,'X',color='red',ha='center',va='center',fontweight='bold')
    fig.suptitle('Native: 110 empty / 46 singleton / 6 paired; red X = 2 failed historical pairings (excluded)')
    fig.savefig(out/'source_disagreement_cells.png',dpi=150);plt.close(fig)
    calls=json.loads((ROOT/'artifacts/source-disagreement-v1/invocations.json').read_text())
    fig,axes=plt.subplots(2,2,figsize=(11,6),layout='constrained')
    for frame in [0,1]:
        call=next(c for c in calls if c['frame']==frame and c['variant']=='s_zp_b020_e10mm-accept-facing')
        m=call['metadata'];mask=np.fromfile(ROOT/call['file'],np.uint8).reshape(m['height'],m['width'],4)
        count=np.where(mask[...,3]!=0,mask[...,1].astype(float),np.nan)
        delta=np.where(mask[...,2]!=0,mask[...,0].astype(float),np.nan)
        for ax,data,title,maximum in zip(axes[frame],[count,delta],['Eligible sources','Defined RGB range / byte'],[4,255]):
            im=ax.imshow(data,cmap=cmap,vmin=0,vmax=maximum,interpolation='nearest')
            ax.set_title(f'F{frame} {title}');ax.axis('off');fig.colorbar(im,ax=ax,fraction=.025)
    fig.suptitle('Native diagnostic only; primary historical pairing failed for 2 selected cells\nZ+5mm, accept, surface10mm; gray = outside domain/undefined')
    fig.savefig(out/'source_disagreement_native.png',dpi=160);plt.close(fig)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--allow-pairing-failures',action='store_true')
    main(parser.parse_args().allow_pairing_failures)
