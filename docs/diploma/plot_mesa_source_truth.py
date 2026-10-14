"""Summarize frozen matched geometry and plot native/image counterexamples."""
import collections
import hashlib
import json
from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
RAW=ROOT/'artifacts/mesa-source-truth-v1/report.json'


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def verified(path,digest):
    if sha(path)!=digest: raise ValueError('plot input hash differs: '+str(path))
    return path


def paired(row): return row['native_pixel'][2] == 255


def good(row,tol='0.02'):
    return paired(row) and len(row['matched_eligible'][tol]) == len(row['eligible_cameras'])


def main():
    raw=json.loads(RAW.read_text());plan_path=ROOT/'configs/research/mesa-source-truth-plan.json';plan=json.loads(plan_path.read_text())
    if not raw['all_scene_anchors_passed'] or not raw['all_metric_oracles_passed'] or raw['plan_sha256']!=sha(plan_path):
        raise ValueError('matched audit or plan validation failed')
    metric_path=ROOT/plan['metric_invocations']
    verified(metric_path,plan['frozen_files'][plan['metric_invocations']])
    metrics=json.loads(metric_path.read_text())
    for capture in raw['captures']:
        metric=next(m for m in metrics if (m['frame'],m['variant'])==(capture['frame'],capture['variant']))
        if capture['device']!=metric['metadata']['device']: raise ValueError('actual backend device differs between repeated products')
    for row in raw['primary']+raw['inventory']:
        p=row['native_pixel']
        if (p[1]<2 and p[0]!=0) or (p[3]==0 and any(p)):
            raise ValueError('undefined/domain encoding is not exactly zero')
    profiles=plan['inventory_profiles'];grid=raw['inventory'];primary=raw['primary'];out=Path(__file__).parent/'figures/experiments'
    groups=[]
    for frame in [0,1]:
        for profile in profiles:
            rows=[r for r in grid if (r['frame'],r['variant'])==(frame,profile)]
            groups.append(dict(frame=frame,variant=profile,total=len(rows),outside_domain=sum(not r['native_pixel'][3] for r in rows),
                inside_without_source=sum(r['native_pixel'][3]!=0 and not r['eligible_cameras'] for r in rows),
                singleton=sum(len(r['eligible_cameras'])==1 for r in rows),paired=sum(paired(r) for r in rows),
                all_centres_matching_2mm=sum(good(r,'0.002') for r in rows),all_centres_matching_20mm=sum(good(r) for r in rows)))
    thresholds=[]
    for threshold in plan['thresholds_byte']:
        correct=[r for r in grid if good(r)]
        wrong=[r for r in grid if paired(r) and r['virtual_kind'] and not good(r)]
        old_wrong=[r for r in primary if paired(r) and r['virtual_kind'] and not good(r)]
        thresholds.append(dict(threshold_byte=threshold,correct_inventory_centres_flagged=sum(r['native_pixel'][0]>threshold for r in correct),
            correct_inventory_cells=len(correct),nonmatching_inventory_flagged=sum(r['native_pixel'][0]>threshold for r in wrong),
            nonmatching_inventory_cells=len(wrong),nonmatching_primary_flagged=sum(r['native_pixel'][0]>threshold for r in old_wrong),
            nonmatching_primary_cells=len(old_wrong)))
    profile=profiles[1]
    def selected(y,x):return next(r for r in grid if (r['frame'],r['variant'],r['y'],r['x'])==(0,profile,y,x))
    examples=[selected(232,312),selected(344,424),selected(296,24)]
    # Illustrations selected after seeing the inventory; primary selection/thresholds remain frozen.
    assert examples[0]['eligible_cameras']==examples[1]['eligible_cameras']==[0,3]
    assert not good(examples[0]) and good(examples[1],'0.002')
    summary=dict(schema_version=1,scope=raw['scope'],plan_sha256=raw['plan_sha256'],raw_report_sha256=sha(RAW),
        plot_sha256=sha(Path(__file__)),all_actual_metric_devices_equal=True,exact_undefined_domain_encoding=True,scene_anchor_checks=raw['scene_anchor_checks'],captures=raw['captures'],
        all_scene_anchors_passed=raw['all_scene_anchors_passed'],all_metric_oracles_passed=raw['all_metric_oracles_passed'],
        primary=primary,inventory_groups=groups,thresholds=thresholds,
        inventory_paired=[dict(frame=r['frame'],variant=r['variant'],y=r['y'],x=r['x'],virtual_kind=r['virtual_kind'],
            eligible_cameras=r['eligible_cameras'],native_pixel=r['native_pixel'],matched_eligible=r['matched_eligible'],
            source_kinds=[s['kind'] for s in r['sources']],source_errors_m=[s['distance_to_virtual_hit_m'] for s in r['sources']]) for r in grid if paired(r)],
        illustrative_posthoc_counterexamples=examples,limitations=raw['limitations'])
    fig,axes=plt.subplots(1,2,figsize=(12,4.5),layout='constrained')
    labels=[f"F{g['frame']} / {'nom' if g['variant']==profiles[0] else 'Z+5mm accept'}" for g in groups]
    bottom=np.zeros(len(groups))
    for key,label,color in [('outside_domain','Outside carrier domain','#b0b0b0'),('inside_without_source','Carrier without source','#d62728'),('singleton','Single source: undefined','#ffbb78'),('paired','Paired: comparison defined','#1f77b4')]:
        vals=[g[key] for g in groups];axes[0].bar(labels,vals,bottom=bottom,label=label,color=color);bottom+=vals
    axes[0].set(ylabel='Lattice cell memberships / profile',title='Deterministic 16px lattice; 880 cells each')
    axes[0].tick_params(axis='x',rotation=20);axes[0].legend(fontsize=8)
    x=range(len(thresholds))
    for key,label,color in [('correct_inventory_centres_flagged','Correct inventory centres flagged / 1101','#1f77b4'),('nonmatching_inventory_flagged','Nonmatching inventory flagged / 2','#d62728'),('nonmatching_primary_flagged','Nonmatching primary flagged / 6','#ff7f0e')]:
        axes[1].plot(x,[t[key] for t in thresholds],marker='o',label=label,color=color)
    axes[1].set_xticks(list(x),[str(t['threshold_byte']) for t in thresholds])
    axes[1].set(xlabel='Predeclared threshold (strict range > threshold)',ylabel='Correlated memberships flagged',title='Counts describe centre correspondence, not RGB quality')
    axes[1].legend(fontsize=8);axes[1].grid(alpha=.2)
    fig.suptitle('One explored coded scene; no holdout ranking or chosen rejection threshold')
    fig.savefig(out/'mesa_source_truth_inventory.png',dpi=160);plt.close(fig)
    archive=json.loads((ROOT/plan['paired_truth']).read_text());truth_path=ROOT/plan['paired_truth']
    road=next(v for k,v in archive['objects'].items() if k.startswith('SV road.'))
    target=next(v for k,v in archive['objects'].items() if k.startswith('SV coded object target.'))
    virtual_checks={};id_payloads={}
    for frame in [0,1]:
        name=archive['frames'][frame]['objects'];digest=archive['sha256'][name]
        ids=np.load(verified(truth_path.parent/name,digest),allow_pickle=False);id_payloads[name]=digest
        for row in primary+grid:
            if row['frame']!=frame or row['virtual_kind'] is None:continue
            value=int(ids[row['y'],row['x']])
            expected=2 if value==road else 1 if value==target else 3 if value in archive['ego_object_ids'] else 0 if value==0 else 4
            virtual_checks[(frame,row['y'],row['x'])]=expected==row['virtual_kind']
    if not all(virtual_checks.values()):raise ValueError('post-collection archived virtual object-ID crosscheck failed')
    summary['postcollection_virtual_id_crosscheck']=dict(unique_pixels=len(virtual_checks),all_passed=True,payload_sha256=id_payloads)
    rgb=np.asarray(Image.open(verified(truth_path.parent/archive['frames'][0]['rgb'],archive['sha256'][archive['frames'][0]['rgb']])).convert('RGB'))
    # Show the same camera pair/profile/frame for the geometric counterexample.
    fig,axes=plt.subplots(2,4,figsize=(12,6),layout='constrained')
    capture=next(c for c in raw['captures'] if (c['frame'],c['variant'])==(0,profile))
    directory=(ROOT/capture['report']).parent;metadata=json.loads(verified(directory/'report.json',capture['report_sha256']).read_text())
    cfg=metadata['config'];w,h=cfg['output']['width'],cfg['output']['height']
    native=np.fromfile(verified(directory/'output.rgba',next(p['sha256'] for p in metadata['products'] if p['file']=='output.rgba')),np.uint8).reshape(h,w,4)
    images={i['camera_id']:np.asarray(Image.open(verified(Path(i['path']),i['sha256'])).convert('RGB')) for i in metadata['inputs']}
    for row in examples:
        y,x=row['y'],row['x']
        row['illustrative_native_rgb']=native[y,x,:3].tolist()
        row['illustrative_independent_rgb']=rgb[y,x].tolist()
    summary['counterexample_scope']='Posthoc illustrations of centre correspondence and raw RGB; no final-image quality gate or chosen threshold.'
    for i,row in enumerate(examples[:2]):
        label='Nonmatching centres' if i==0 else 'Matching centres'
        y,x=row['y'],row['x']
        for j,image in enumerate([native,rgb]):
            axes[i,j].imshow(image);axes[i,j].set(xlim=(x-12,x+12),ylim=(min(h-1,y+12),y-12))
            axes[i,j].plot(x,y,'+',color='cyan',ms=8);axes[i,j].axis('off')
            axes[i,j].set_title(('Native final RGBA' if j==0 else 'Archived Blender RGB')+f'\nF0 ({y},{x})',fontsize=9)
        for j,s in enumerate(row['sources'],start=2):
            u,v=s['uv'];image=images[s['camera']]
            axes[i,j].imshow(image);axes[i,j].set(xlim=(u-10,u+10),ylim=(v+10,v-10));axes[i,j].plot(u,v,'+',color='cyan',ms=8);axes[i,j].axis('off')
            axes[i,j].set_title(f"{label}: C{s['camera']}\npoint error {s['distance_to_virtual_hit_m']*1000:.3f}mm; range {row['native_pixel'][0]}/255",fontsize=9)
    fig.suptitle('Post-hoc illustrations, same frame/profile/cameras0+3: low range can have wrong points\nCentre labels do not reconstruct antialias/filter footprints or establish final-image quality')
    fig.savefig(out/'mesa_source_truth_counterexamples.png',dpi=160);plt.close(fig)
    summary['figures_sha256']={p.name:sha(p) for p in [out/'mesa_source_truth_inventory.png',out/'mesa_source_truth_counterexamples.png']}
    (ROOT/'docs/validation/baselines/mesa_source_truth_v1.json').write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n')


if __name__=='__main__':main()
