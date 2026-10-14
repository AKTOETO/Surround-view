"""Figures and compact baseline for the fixed independent texture screen."""
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle
from PIL import Image
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/research'))
from source_correspondence import sha, verified, load_product


def main():
    plan_path = ROOT/'configs/research/textured-source-retry-plan.json'
    plan = json.loads(plan_path.read_text())
    raw = ROOT/plan['native_output_root']/'report.json'
    report = json.loads(raw.read_text())
    if report['plan_sha256'] != sha(plan_path) or not report['all_anchors_passed'] or not report['all_metric_checks_passed']:
        raise ValueError('unverified audit')
    verified(ROOT/'tools/research/textured_source.py', report['collector_sha256'])
    calls_path = verified(ROOT/plan['native_output_root']/'invocations.json', report['invocation_sha256'])
    calls = json.loads(calls_path.read_text())
    study_path = verified(ROOT/plan['output_root']/'capture-study.json', report['scene_study_sha256'])
    study = json.loads(study_path.read_text())
    for case in study['cases']:
        for name, digest in case['sha256'].items():
            verified(ROOT/plan['output_root']/case['id']/name, digest)
    summaries, paired = [], []
    for capture in report['captures']:
        rows = [r for r in report['rows'] if (r['id'],r['profile']) == (capture['id'],capture['profile'])]
        counts = dict(all_matching=0, mixed=0, none_matching=0)
        thresholds = {str(t):dict(all_matching_flagged=0, mixed_flagged=0, none_matching_flagged=0) for t in plan['thresholds_byte']}
        for row in rows:
            if len(row['sources']) < 2:
                continue
            matched = [s['matched']['0.002'] for s in row['sources']]
            category = 'all_matching' if all(matched) else 'mixed' if any(matched) else 'none_matching'
            counts[category] += 1
            for t in plan['thresholds_byte']:
                thresholds[str(t)][category+'_flagged'] += row['native_pixel'][0] > t
            paired.append(dict(id=row['id'], profile=row['profile'], y=row['y'], x=row['x'],
                virtual_kind=row['virtual_kind'], native_pixel=row['native_pixel'], category_2mm=category,
                sources=[{k:s[k] for k in ['camera','uv','kind','error_m','matched']} for s in row['sources']]))
        summaries.append(dict(id=capture['id'], profile=capture['profile'], cells=len(rows),
            outside_domain=sum(not r['domain'] for r in rows), source_counts=[sum(len(r['sources'])==n for r in rows) for n in range(5)],
            paired_categories_2mm=counts, thresholds_byte=thresholds))
    figures = ROOT/'docs/diploma/figures/experiments'
    fig, axes = plt.subplots(2,3,figsize=(14,6.8), constrained_layout=True)
    for i, case in enumerate(plan['cases']):
        source = ROOT/plan['output_root']/case['id']/'capture'
        labels = np.load(source/'objects_0000.npy', allow_pickle=False)
        archive = json.loads((source/'paired_truth.json').read_text())
        target_id = next(v for k,v in archive['objects'].items() if 'coded object target' in k)
        yy,xx = np.where(labels == target_id)
        images = [np.asarray(Image.open(source/'virtual_0000.png').convert('RGB'))]
        for profile in plan['profiles']:
            call = next(c for c in calls if (c['id'],c['profile']) == (case['id'],profile))
            directory = ROOT/call['directory']
            m = json.loads(verified(directory/'report.json',call['correspondence_report_sha256']).read_text())
            product = next(p for p in m['products'] if p['file']=='output.rgba')
            images.append(load_product(directory,product)[...,:3])
        for j, image in enumerate(images):
            ax = axes[i,j]; ax.imshow(image); ax.axis('off')
            if len(xx):
                ax.add_patch(Rectangle((xx.min()-2,yy.min()-2),np.ptp(xx)+4,
                    np.ptp(yy)+4,fill=False,edgecolor='cyan',linewidth=1))
            title = 'Independent Blender RGB' if j==0 else plan['profiles'][j-1]
            if j:
                q = next(q for q in report['quality'] if (q['id'],q['profile'],q['kind'],q['roi']) == (case['id'],plan['profiles'][j-1],1,'interior'))
                title += f"\nTarget interior MAE {q['mae_byte']:.2f}; n={q['pixels']}"
            ax.set_title(f"Seed {case['scenario']['seed']}: {title}",fontsize=10)
    fig.suptitle('New texture/pose fixtures: same inputs, different supplied carriers\nCyan = direct target box; interior MAE is not a seam/ghost score')
    comparison = figures/'textured_source_comparison.png'; fig.savefig(comparison,dpi=160);plt.close(fig)
    fig,axes = plt.subplots(1,2,figsize=(12,4.5),constrained_layout=True)
    positions = np.arange(len(summaries)); bottom = np.zeros(len(summaries))
    for category,color in [('all_matching','#4b9d87'),('mixed','#e3ae45'),('none_matching','#bd5863')]:
        values = np.array([s['paired_categories_2mm'][category] for s in summaries])
        axes[0].bar(positions,values,bottom=bottom,label=category,color=color);bottom+=values
    axes[0].set_xticks(positions,[s['id'].replace('textured-','')+'\n'+('plane' if s['profile']=='plane' else 'patch') for s in summaries])
    axes[0].set_ylabel('Paired lattice memberships');axes[0].legend(fontsize=8)
    for category,color in [('all_matching','#4b9d87'),('mixed','#e3ae45'),('none_matching','#bd5863')]:
        values=[r['native_pixel'][0] for r in paired if r['category_2mm']==category]
        axes[1].hist(values,bins=np.arange(0,257,8),histtype='step',label=f'{category}: n={len(values)}',color=color)
    axes[1].set_xlabel('Native maximum RGB range (byte)');axes[1].set_ylabel('Correlated memberships')
    axes[1].set_yscale('log');axes[1].legend(fontsize=8)
    fig.suptitle('Centre geometry at 2 mm: no balanced sampling or threshold tuning')
    inventory = figures/'textured_source_inventory.png';fig.savefig(inventory,dpi=160);plt.close(fig)
    compact = {k:v for k,v in report.items() if k!='rows'}
    compact.update(raw_report_sha256=sha(raw),plot_sha256=sha(Path(__file__)),
        cells=len(report['rows']),max_rgb_byte_error=max(r['byte_error'] for r in report['rows']),
        summaries=summaries,paired=paired,figures_sha256={p.name:sha(p) for p in [comparison,inventory]})
    path=ROOT/'docs/validation/baselines/textured_source_v1.json'
    path.write_text(json.dumps(compact,indent=2,allow_nan=False)+'\n')


if __name__ == '__main__':
    main()
