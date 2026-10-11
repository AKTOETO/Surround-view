"""Read-only figures for the frozen height/frame correspondence controls."""
import argparse
import json
from pathlib import Path
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/research'))
from source_correspondence_controls import collect
from source_correspondence import load_product, verified


def plot(report, plan, figures):
    figures.mkdir(parents=True, exist_ok=True)
    labels = ['plane', 'bowl', 'dome', 'cylinder', 'cube']
    fig, axes = plt.subplots(1, 2, figsize=(13, 5))
    x = np.arange(5)
    for case, color, offset in zip(report['cases'], ['#2874a6', '#85b9d9', '#bd3030', '#eb9797'], [-.15, -.05, .05, .15]):
        label = f"z={.2 if 'z020' in case['case'] else .8}m, frame {case['frame_index']}"
        axes[0].plot(x+offset, [row['visible_true_vs_native_uv_px']['p50'] for row in case['rows']],
                     'o-', color=color, label=label)
        valid = [row['rois']['source_visible'] for row in case['rows']]
        axes[1].plot(x+offset, [r['same_object_wrong_point']/(r['source_pairs']-r['invalid_projection'])
                              for r in valid], 'o-', color=color, label=label)
    for ax in axes:
        ax.set_xticks(x, labels); ax.grid(axis='y', alpha=.3)
    axes[0].set(ylabel='Median correct-visible to native UV gap / source pixels', title='Fixed 800x800 input grid')
    axes[1].set(ylabel='Wrong-point target hits / valid source pairs', title='Identity alone misses point correspondence errors')
    axes[0].legend(fontsize=8); axes[1].legend(fontsize=8)
    fig.suptitle('Two explored heights, two correlated frames; 0.02m match tolerance; no holdout ranking')
    fig.tight_layout(rect=(0,0,1,.92));fig.savefig(figures/'source_controls_metrics.png', dpi=150);plt.close(fig)
    fig, axes = plt.subplots(4,3,figsize=(14,15))
    for row, case in enumerate(report['cases']):
        entry = next(e for e in plan['cases'] if e['id']==case['case'])
        capture = ROOT/entry['input_root']/'seed101-capture'
        truth = json.loads((capture/'paired_truth.json').read_text())
        frame = truth['frames'][case['frame_index']]
        rgb = np.asarray(Image.open(verified(capture/frame['rgb'],truth['sha256'][frame['rgb']])))
        directory = ROOT/plan['output_root']/case['case']/f"frame{case['frame_index']}"
        mask = np.load(verified(directory/'direct-mask.npy',case['scene']['files']['direct-mask.npy']),allow_pickle=False)
        images=[rgb]
        for carrier in ['plane','bowl']:
            record = next(r for r in case['rows'] if r['carrier']==carrier)
            native_dir=directory/'native'/carrier
            native = json.loads(verified(native_dir/'report.json',record['report_sha256']).read_text())
            product = next(p for p in native['products'] if p['file']=='output.rgba')
            images.append(load_product(native_dir,product)[...,:3])
        for col, image in enumerate(images):
            ax=axes[row,col];ax.imshow(image)
            ax.contour(mask,levels=[.5],colors=['#00eaff'],linewidths=.8)
            detected=np.minimum(image[...,0],image[...,2]).astype(float)-image[...,1]>255*.15
            if detected.any():ax.contour(detected,levels=[.5],colors=['#ffb000'],linewidths=.8)
            ax.set_xlim(200,500);ax.set_ylim(359.5,165)
            ax.set_title(f"{case['case']}, frame {case['frame_index']} / {['Blender','plane','bowl'][col]}",fontsize=9)
            ax.set_xlabel('Output X / px');ax.set_ylabel('Output Y / px')
    fig.suptitle('Captured RGB only: cyan = independent target mask; orange = raw chroma proxy\n'
                 'Twenty native RGBA hashes match archived server frame baselines')
    fig.tight_layout(rect=(0,0,1,.96));fig.savefig(figures/'source_controls_views.png',dpi=150);plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,default=ROOT/'configs/research/source-correspondence-controls-plan.json')
    parser.add_argument('--output-report',type=Path,required=True)
    parser.add_argument('--figures',type=Path,default=Path(__file__).parent/'figures/experiments')
    args=parser.parse_args();plan=json.loads(args.plan.read_text())
    report=collect(ROOT,args.plan.resolve())
    args.output_report.write_text(json.dumps(report,indent=2)+'\n')
    plot(report,plan,args.figures)


if __name__=='__main__':main()
