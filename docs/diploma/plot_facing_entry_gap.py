"""Plot read-only central-filter counterfactuals; no product rendering."""
import json
from pathlib import Path
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator
ROOT = Path(__file__).resolve().parents[2]


def main():
    audit = json.loads((ROOT / 'docs/validation/baselines/facing_entry_gap_v1.json').read_text())
    primary = json.loads((ROOT / 'docs/validation/baselines/native_facing_v1.json').read_text())
    out = Path(__file__).parent / 'figures/experiments'
    rows = [r for r in audit['rows'] if r['kind'] == 'surface10mm' and r['policy'] == 'accept']
    fig, axes = plt.subplots(2, 3, figsize=(14, 7), layout='constrained')
    metrics = [('visible_proxy_hits','Visible target hits'), ('visible_without_source','Visible target without source'), ('reference_blocked_pairs','Reference-blocked retained pairs')]
    for frame in [0, 1]:
        ordered = [next(r for r in rows if r['frame']==frame and r['shift']==shift) for shift in ['nom','zp','zm']]
        for col, (metric,title) in enumerate(metrics):
            ax=axes[frame,col]
            native=[next(r for r in primary['rows'] if r['variant']==row['variant'] and r['frame']==frame)[metric] for row in ordered]
            ax.plot(['nom','Z+5mm','Z-5mm'], native, marker='o', label='Actual native facing')
            for name, label, marker in [('supplied_center_2mm','Hypothetical central 2mm','s'), ('supplied_center_0mm','Hypothetical central 0mm','^')]:
                values=[next(c for c in row['candidates'] if c['name']==name)['hypothetical_metrics'][metric] for row in ordered]
                ax.plot(['nom','Z+5mm','Z-5mm'], values, marker=marker, label=label)
            ax.set(title=f'F{frame}: {title}', xlabel='Supplied box translation', ylabel='Count')
            ax.yaxis.set_major_locator(MaxNLocator(integer=True)); ax.set_ylim(bottom=0); ax.grid(alpha=.25)
    handles,labels=axes[0,0].get_legend_handles_labels()
    fig.legend(handles,labels,loc='outside lower center',ncol=3)
    fig.suptitle('Fixed central-only filters on native eligible sources; accept radius5mm, band10mm')
    fig.savefig(out/'facing_entry_gap_metrics.png',dpi=160);plt.close(fig)
    fig,axes=plt.subplots(1,2,figsize=(12,5),layout='constrained')
    for frame,ax in enumerate(axes):
        ax.plot([0,180],[0,180],color='gray',alpha=.5,label='Same gap')
        for shift,marker,label in [('nom','o','nom'),('zp','s','Z+5mm'),('zm','^','Z-5mm')]:
            pairs=next(r for r in rows if r['frame']==frame and r['shift']==shift)['reference_pairs']
            ax.scatter([p['reference_gap_m']*1000 for p in pairs],[p['supplied_gap_m']*1000 if p['supplied_gap_m'] is not None else -15 for p in pairs],marker=marker,label=label,s=65,alpha=.8)
        ax.axhline(-15,color='gray',linestyle=':',alpha=.5)
        ax.set(title=f'F{frame}: remaining accept / band10mm pairs',xlabel='Nominal-reference entry-to-endpoint / mm',ylabel='Supplied entry-to-endpoint / mm',ylim=(-25,180),xlim=(0,180))
        ax.set_yticks([-15,0,50,100,150],['No hit','0','50','100','150']);ax.grid(alpha=.25);ax.legend()
    fig.suptitle('Fixed carrier rays, different supplied AABB; no hit is not a zero distance')
    fig.savefig(out/'facing_entry_gap_geometry.png',dpi=160);plt.close(fig)


if __name__=='__main__':
    main()
