"""Freeze the bounded overlap report and illustrate existing compiled captures."""
import json
from pathlib import Path
import sys
import numpy as np
from PIL import Image
from scipy.ndimage import binary_erosion
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/research'))
from source_correspondence import sha, verified, load_product
from textured_source import checked


def main():
    path = ROOT/'configs/research/target-overlap-retry-plan.json'
    plan, study = checked(ROOT, path)
    preflight_path = ROOT/plan['preflight_report']
    preflight = json.loads(preflight_path.read_text())
    raw_path = ROOT/plan['native_output_root']/'report.json'
    raw = json.loads(raw_path.read_text())
    if (preflight['plan_sha256'] != sha(path) or raw['plan_sha256'] != sha(path)
            or not preflight['all_passed'] or not raw['all_anchors_passed'] or not raw['all_metric_checks_passed']):
        raise ValueError('failed preflight/native gate')
    verified(ROOT/'tools/research/textured_source.py', raw['collector_sha256'])
    verified(ROOT/plan['output_root']/'capture-study.json', raw['scene_study_sha256'])
    calls = json.loads(verified(ROOT/plan['native_output_root']/'invocations.json',raw['invocation_sha256']).read_text())
    summaries, pairs = [], []
    fig, axes = plt.subplots(2,3,figsize=(12,7),constrained_layout=True)
    for i, case in enumerate(plan['cases']):
        gate = next(r for r in preflight['rows'] if r['id'] == case['id'])
        source = ROOT/plan['output_root']/case['id']/'capture'
        archive = json.loads((source/'paired_truth.json').read_text())
        labels = np.load(source/'objects_0000.npy',allow_pickle=False)
        target_id = next(v for k,v in archive['objects'].items() if 'coded object target' in k)
        interior = binary_erosion(labels==target_id,iterations=1)
        visibility = np.load(source/'visibility_0000.npy',allow_pickle=False)
        visible = np.array([int(v).bit_count()>=2 for v in visibility.ravel()]).reshape(visibility.shape)
        if int(interior.sum()) != gate['target_interior_pixels'] or int((interior & visible).sum()) != gate['target_two_camera_interior_pixels']:
            raise ValueError('capture differs from preflight target overlap')
        yy,xx = np.where(labels==target_id)
        bounds = (slice(max(0,yy.min()-12),min(labels.shape[0],yy.max()+13)),
                  slice(max(0,xx.min()-12),min(labels.shape[1],xx.max()+13)))
        images = [np.asarray(Image.open(source/'virtual_0000.png').convert('RGB'))]
        for profile in plan['profiles']:
            call = next(c for c in calls if (c['id'],c['profile'])==(case['id'],profile))
            directory = ROOT/call['directory']
            meta = json.loads(verified(directory/'report.json',call['correspondence_report_sha256']).read_text())
            product = next(p for p in meta['products'] if p['file']=='output.rgba')
            images.append(load_product(directory,product)[...,:3])
            q = next(q for q in raw['quality'] if (q['id'],q['profile'],q['kind'],q['roi'])==(case['id'],profile,1,'two_camera_visible_interior'))
            if q['pixels'] != gate['target_two_camera_interior_pixels']:
                raise ValueError('quality denominator differs')
            rows = [r for r in raw['rows'] if r['id']==case['id'] and r['profile']==profile and r['virtual_kind']==1]
            paired = [r for r in rows if len(r['sources'])>=2]
            summaries.append(dict(id=case['id'],profile=profile,target_lattice_cells=len(rows),
                paired=len(paired),all_matching_2mm=sum(all(s['matched']['0.002'] for s in r['sources']) for r in paired),
                none_matching_2mm=sum(not any(s['matched']['0.002'] for s in r['sources']) for r in paired),
                two_camera_target_pixels=q['pixels'],mae_byte=q['mae_byte']))
        by_profile = {p:{(r['y'],r['x']):r for r in raw['rows'] if r['id']==case['id'] and r['profile']==p and r['virtual_kind']==1} for p in plan['profiles']}
        for coordinate in sorted(by_profile['plane'].keys() & by_profile['supplied_local_patch'].keys()):
            a,b = [by_profile[p][coordinate] for p in plan['profiles']]
            # This inventory is reported as observed; no extra pixels/seeds are sought.
            paired = (len(a['sources'])>=2 and len(b['sources'])>=2
                and [s['camera'] for s in a['sources']]==[s['camera'] for s in b['sources']])
            contrast = paired and not any(s['matched']['0.002'] for s in a['sources']) and all(s['matched']['0.002'] for s in b['sources'])
            pairs.append(dict(id=case['id'],y=coordinate[0],x=coordinate[1],same_camera_pair=paired,
                none_to_all_matching_2mm=contrast,plane=a,patch=b))
        for j,image in enumerate(images):
            ax=axes[i,j]; ax.imshow(image[bounds],interpolation='nearest');ax.axis('off')
            label='Independent Blender RGB' if j==0 else plan['profiles'][j-1]
            if j:
                q=summaries[-2+j-1];label+=f"\nTarget MAE {q['mae_byte']:.2f}; n={q['two_camera_target_pixels']}"
            ax.set_title(f"Seed {case['scenario']['seed']}: {label}",fontsize=10)
    fig.suptitle('Confirmed two-camera target overlap; same input images and view\nCrops are fixed from independent target mask; no fusion-algorithm ranking')
    figure=ROOT/'docs/diploma/figures/experiments/target_overlap_comparison.png'
    fig.savefig(figure,dpi=160);plt.close(fig)
    compact={k:v for k,v in raw.items() if k!='rows'}
    compact.update(raw_report_sha256=sha(raw_path),preflight_sha256=sha(preflight_path),preflight=preflight,
        plot_sha256=sha(Path(__file__)),cells=len(raw['rows']),
        max_rgb_byte_error=max(r['byte_error'] for r in raw['rows']),summaries=summaries,
        target_same_coordinate_pairs=pairs,figure_sha256=sha(figure),
        conclusion='bounded control complete; next work is ConfigService/library/GUI; remaining research explicitly deferred')
    output=ROOT/'docs/validation/baselines/target_overlap_v1.json'
    output.write_text(json.dumps(compact,indent=2,allow_nan=False)+'\n')


if __name__=='__main__':
    main()
