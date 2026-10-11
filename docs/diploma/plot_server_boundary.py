"""Plot independently audited native-server boundary measurements and captured views."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from temporal_seam_stability import load_sequence


def plot_optimizer(path, output):
    data = json.loads(path.read_text())
    test = next(t for suite in data['testsuites'] for t in suite['testsuite']
                if t['name'] == 'ConvergedEnergyAndGlobalEnumeration')
    case = json.loads(test['worst_case'])
    positions = np.array([(np.cos(a), np.sin(a)) for a in np.linspace(0, 2*np.pi, 6, endpoint=False)])
    palette = ['#66c2a5','#fc8d62','#8da0cb','#e78ac3']
    fig, axes = plt.subplots(1, 2, figsize=(12, 6))
    for ax, labels, title in zip(axes, (case['labels'],case['global_labels']),
            (f"Expansion local minimum: E={case['energy']}", f"Exhaustive global minimum: E={case['global_energy']}")):
        for p, q, weight in case['edges']:
            xy = positions[[p,q]]
            ax.plot(xy[:,0],xy[:,1],color='gray',linewidth=1+weight/10,zorder=1)
            midpoint = xy.mean(axis=0)
            ax.text(*midpoint,str(weight),ha='center',va='center',
                    bbox=dict(facecolor='white',edgecolor='none',pad=1))
        ax.scatter(positions[:,0],positions[:,1],s=1700,c=[palette[c] for c in labels],zorder=2)
        for n, (xy, label) in enumerate(zip(positions,labels)):
            ax.text(*xy,f'p{n}\ncamera {label}',ha='center',va='center',zorder=3)
        ax.set_title(title)
        ax.set_xlim(-1.4,1.4);ax.set_ylim(-1.4,1.4)
        ax.set_aspect('equal');ax.set_axis_off()
    fig.suptitle('Known-answer six-node Potts graph: exact moves do not guarantee a global solution')
    fig.tight_layout()
    fig.savefig(output/'seam_optimizer_counterexample.png',dpi=150)
    plt.close(fig)


def plot_budget(data, args):
    carriers = list(data['cases'][0]['resources'])
    profiles = [(m,b) for m in ('multi_band','graph_cut_multi_band') for b in ('zero','normalized')]
    labels, errors, ious = [], [], []
    for case in data['cases']:
        for carrier in carriers:
            labels.append(f"seed{case['seed']} / {carrier.replace('_floor_v1','')}")
            groups = [[r for r in case['audit']['results'] if r['carrier'] == carrier
                       and (r['mode'],r['boundary']) == profile] for profile in profiles]
            errors.append([np.mean([r['quality']['linear_mae'] for r in group]) for group in groups])
            ious.append([np.mean([r['quality']['target']['iou'] for r in group]) for group in groups])
    fig, axes = plt.subplots(1,2,figsize=(14,10),sharey=True)
    for ax, values, title, cmap in zip(axes,(errors,ious),
            ('Linear RGB MAE (lower is better)','Coded-target IoU (higher is better)'),('magma_r','viridis')):
        im = ax.imshow(values,aspect='auto',cmap=cmap)
        ax.set_xticks(range(4),[m.replace('graph_cut_','GC ').replace('multi_band','MB')+' / '+b for m,b in profiles],rotation=25,ha='right')
        ax.set_yticks(range(len(labels)),labels)
        ax.set_title(title)
        for y,row in enumerate(values):
            for x,value in enumerate(row):
                ax.text(x,y,f'{value:.4f}',ha='center',va='center',color='white',
                        bbox=dict(facecolor='black',alpha=.35,edgecolor='none',pad=1))
        fig.colorbar(im,ax=ax,shrink=.6)
    fig.suptitle('Fixed-budget carrier screen: means of two adjacent frames per scene/profile')
    fig.tight_layout()
    args.output.mkdir(parents=True,exist_ok=True)
    fig.savefig(args.output/'carrier_budget_metrics.png',dpi=150);plt.close(fig)
    resources = data['cases'][0]['resources']
    fig, axes = plt.subplots(1,3,figsize=(13,4))
    for ax,field,ceiling in zip(axes,('triangles','vertices','buffer_bytes'),
            ('triangles_max','vertices_max','buffer_bytes_max')):
        ax.bar(range(len(carriers)),[resources[c][field] for c in carriers])
        ax.axhline(data['plan'][ceiling],color='red',linestyle='--',label='shared ceiling')
        ax.set_xticks(range(len(carriers)),[c.replace('_floor_v1','') for c in carriers],rotation=30)
        ax.set_title(field);ax.legend()
    fig.suptitle('Actual active carrier buffers; textures, driver overhead and ego mesh excluded')
    fig.tight_layout();fig.savefig(args.output/'carrier_budget_resources.png',dpi=150);plt.close(fig)
    # First seed and first profile are selected by the frozen plan, not image quality.
    case = data['cases'][0]
    report = case['audit']
    for name,digest in report['fixture_sha256'].items():
        if hashlib.sha256((args.fixture/name).read_bytes()).hexdigest() != digest:
            raise ValueError('budget view fixture hash mismatch')
    cfg,_,truths,_,stamps,_ = load_sequence(args.fixture,args.capture or args.fixture,'any')
    fig,axes = plt.subplots(len(stamps),6,figsize=(20,7),squeeze=False)
    carrier_ids = {'dome_floor_v1':'dome_floor','cylinder_floor_v1':'cylinder_floor','cube_floor_v1':'cube_floor'}
    for t,stamp in enumerate(stamps):
        axes[t,0].imshow(truths[t]);axes[t,0].set_title(f'Direct truth / frame {t}')
        for column,carrier in enumerate(carriers,1):
            native = report['native_reports'][carrier]
            frame = next(b['frame_index'] for b in native['frame_baselines'] if
                         b['metadata']['inputs'][0]['source_timestamp_ns'] == str(stamp))
            sample = next(s for s in native['samples'] if s['frame_index'] == frame and
                          s['variant'] == 0 and not s['warmup'])
            path = args.captures_root/f"seed{case['seed']}"/carrier_ids.get(carrier,carrier)/sample['rgba_file']
            payload = path.read_bytes()
            if hashlib.sha256(payload).hexdigest() != sample['rgba_sha256']:
                raise ValueError('budget native RGBA hash mismatch')
            axes[t,column].imshow(np.frombuffer(payload,np.uint8).reshape(cfg['output']['height'],cfg['output']['width'],4))
            axes[t,column].set_title(carrier)
    for ax in axes.flat:ax.set_axis_off()
    fig.suptitle(f"Seed{case['seed']}: actual server multi_band/zero, common mesh ceilings")
    fig.tight_layout();fig.savefig(args.output/'carrier_budget_views.png',dpi=150);plt.close(fig)


def plot_refinement(data, args):
    carriers = list(data['levels']['medium']['cases'][0]['resources'])
    fig,axes = plt.subplots(1,3,figsize=(16,5))
    for index,carrier in enumerate(carriers):
        rows = [r for r in data['comparisons'] if r['carrier'] == carrier]
        for level,offset,color in (('coarse',-.15,'#dc8039'),('medium',.15,'#2878b5')):
            values = [r[level+'_to_fine']['roi_linear_mae_to_fine'] for r in rows]
            axes[0].scatter(np.full(len(values),index+offset),values,c=color,alpha=.6,
                            label=level+' to fine' if index == 0 else None)
        for ax,field,title in zip(axes[1:],('linear_mae_delta','target_iou_delta'),
                                 ('Truth RGB MAE difference','Truth coded IoU difference')):
            for step,offset,color in (('medium_minus_coarse',-.15,'#dc8039'),('fine_minus_medium',.15,'#2878b5')):
                values = [r[step][field] for r in rows]
                ax.scatter(np.full(len(values),index+offset),values,c=color,alpha=.6,
                           label=step.replace('_',' ') if index == 0 else None)
            ax.set_title(title)
            ax.axhline(0,color='black',linewidth=.6)
    axes[0].set_title('Output linear RGB difference to finite fine mesh')
    for ax in axes:
        ax.set_xticks(range(len(carriers)),[c.replace('_floor_v1','') for c in carriers],rotation=25)
        ax.ticklabel_format(axis='y',style='sci',scilimits=(0,0))
        ax.grid(axis='y',alpha=.2);ax.legend(fontsize=8)
    fig.suptitle('Within-carrier refinement: 24 paired conditions per carrier, not independent clips')
    fig.tight_layout();args.output.mkdir(parents=True,exist_ok=True)
    fig.savefig(args.output/'carrier_refinement_metrics.png',dpi=150);plt.close(fig)
    # First seed/frame/profile, all five carriers; read the hash-verified native captures.
    seed = data['plan']['levels'][0]['plan']['seeds'][0]
    cfg,_,truths,_,_,_ = load_sequence(args.fixture,args.capture or args.fixture,'any')
    fig,axes = plt.subplots(5,5,figsize=(19,14),squeeze=False)
    ids = {'dome_floor_v1':'dome_floor','cylinder_floor_v1':'cylinder_floor','cube_floor_v1':'cube_floor'}
    for row,carrier in enumerate(carriers):
        images = []
        axes[row,0].imshow(truths[0]);axes[row,0].set_title(carrier+' / direct truth')
        for column,level in enumerate(data['plan']['levels'],1):
            name = level['id']
            root = ROOT/level['reused_root'] if 'reused_root' in level else args.captures_root/name
            case = next(c for c in data['levels'][name]['cases'] if c['seed'] == seed)
            for filename,digest in case['audit']['fixture_sha256'].items():
                if hashlib.sha256((args.fixture/filename).read_bytes()).hexdigest() != digest:
                    raise ValueError('refinement plot fixture hash mismatch')
            path = root/f'seed{seed}'/ids.get(carrier,carrier)/'report.json'
            if hashlib.sha256(path.read_bytes()).hexdigest() != case['audit']['native_report_sha256'][carrier]:
                raise ValueError('refinement plot native report hash mismatch')
            native = json.loads(path.read_text())
            sample = next(s for s in native['samples'] if s['frame_index'] == 0 and s['variant'] == 0 and not s['warmup'])
            payload = (path.parent/sample['rgba_file']).read_bytes()
            if hashlib.sha256(payload).hexdigest() != sample['rgba_sha256']:
                raise ValueError('refinement plot RGBA hash mismatch')
            image = np.frombuffer(payload,np.uint8).reshape(cfg['output']['height'],cfg['output']['width'],4)
            images.append(image[...,:3])
            axes[row,column].imshow(image)
            axes[row,column].set_title(name+f" / {case['resources'][carrier]['triangles']} triangles")
        delta = np.abs(images[1].astype(float)-images[2].astype(float))*8/255
        axes[row,4].imshow(np.clip(delta,0,1));axes[row,4].set_title('|medium - fine| RGB8 x8')
    for ax in axes.flat:ax.set_axis_off()
    fig.suptitle(f'Seed{seed}, first frame, multi_band/zero; last column is amplified difference, not a scene')
    fig.tight_layout();fig.savefig(args.output/'carrier_refinement_views.png',dpi=150);plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path,
                        default=ROOT/'docs/validation/baselines/server_boundary_v1.json')
    parser.add_argument('--captures-root', type=Path, default=ROOT/'artifacts/server-boundary-v2')
    parser.add_argument('--output', type=Path, default=Path(__file__).parent/'figures/experiments')
    parser.add_argument('--optimizer-report', type=Path)
    parser.add_argument('--fixture', type=Path, default=ROOT/'tests/data/object_stitch_v1')
    parser.add_argument('--capture', type=Path)
    args = parser.parse_args()
    report = json.loads(args.report.read_text())
    if report.get('experiment') == 'E-STITCH-carrier-refinement-01':
        plot_refinement(report,args)
        return
    if report.get('experiment') == 'E-STITCH-carrier-budget-01':
        plot_budget(report,args)
        return
    study = report.get('cases')
    if study:
        pairs = [dict(p,seed=c['seed']) for c in study for p in c['audit']['paired_differences']]
        report = study[0]['audit']
    else:
        pairs = report['paired_differences']
    seam = report.get('comparison') == 'seam_solver'
    prefix = 'seam_generalization' if study else 'server_seam' if seam else 'server_boundary'
    difference = report.get('difference','normalized minus zero')
    labels = [f"seed{p['seed']} / pose {p['truth_index']}" if study else
              f"{p['carrier']} / pose {p['truth_index']}" for p in pairs]
    colors = ['#2878b5' if p['mode'] in ('multi_band','graph_cut_seam') else '#dc8039' for p in pairs]
    fig, axes = plt.subplots(1, 3, figsize=(15, 9), sharey=True)
    for ax, field, title in zip(axes,
            ('linear_mae_delta', 'target_iou_delta', 'fusion_cpu_p50_delta_ms'),
            ('Linear RGB MAE difference', 'Coded-target IoU difference', 'Median fusion CPU difference (ms)')):
        ax.barh(np.arange(len(pairs)), [p[field] for p in pairs], color=colors)
        ax.axvline(0, color='black', linewidth=.8)
        ax.set_title(title, fontsize=10)
        ax.set_xlabel(difference)
        ax.grid(axis='x', alpha=.2)
        if field != 'fusion_cpu_p50_delta_ms':
            ax.ticklabel_format(axis='x', style='sci', scilimits=(0, 0))
        ax.locator_params(axis='x', nbins=5)
    axes[0].set_yticks(np.arange(len(pairs)), labels)
    axes[0].invert_yaxis()
    from matplotlib.patches import Patch
    axes[0].legend(handles=[Patch(color='#2878b5', label='graph_cut_seam' if seam else 'multi_band'),
                            Patch(color='#dc8039', label='graph_cut_multi_band')], loc='lower left')
    fig.suptitle(('Three new procedural instances: ' if study else 'Native-server reused fixture: ')+difference)
    fig.tight_layout()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.optimizer_report:
        plot_optimizer(args.optimizer_report,args.output)
    fig.savefig(args.output/(prefix+'_metrics.png'), dpi=150)
    plt.close(fig)

    # All poses of the declared dome baseline, not a result-selected best example.
    fixture = args.fixture
    for name, digest in report['fixture_sha256'].items():
        if hashlib.sha256((fixture/name).read_bytes()).hexdigest() != digest:
            raise ValueError('fixture hash mismatch')
    cfg, _, truths, _, stamps, _ = load_sequence(fixture, args.capture or fixture, 'any')
    native = report['native_reports']['dome_floor_v1']
    fig, axes = plt.subplots(len(stamps), 5, figsize=(17, 7), squeeze=False)
    for truth_index, stamp in enumerate(stamps):
        axes[truth_index, 0].imshow(truths[truth_index])
        axes[truth_index, 0].set_title(f'Direct Blender truth / pose {truth_index}')
        frame_index = next(b['frame_index'] for b in native['frame_baselines']
                           if b['metadata']['inputs'][0]['source_timestamp_ns'] == str(stamp))
        for variant, settings in enumerate(native['scenario']['variants']):
            sample = next(s for s in native['samples'] if s['frame_index'] == frame_index
                          and s['variant'] == variant and not s['warmup'])
            payload = (args.captures_root/'dome_floor'/sample['rgba_file']).read_bytes()
            if hashlib.sha256(payload).hexdigest() != sample['rgba_sha256']:
                raise ValueError('native capture hash mismatch')
            rgb = np.frombuffer(payload, np.uint8).reshape(cfg['output']['height'], cfg['output']['width'], 4)
            axes[truth_index, variant+1].imshow(rgb)
            policy = settings.get('seam_solver','binary_pairs') if seam else settings['pyramid_boundary']
            axes[truth_index, variant+1].set_title(settings['mode']+'\n'+policy)
    for ax in axes.flat:
        ax.set_axis_off()
    fig.suptitle(('Seed '+str(study[0]['seed'])+': ' if study else '')+
                 'Actual production-server RGBA: dome baseline, both predeclared poses')
    fig.tight_layout()
    fig.savefig(args.output/(prefix+'_views.png'), dpi=150)
    plt.close(fig)


if __name__ == '__main__':
    main()
