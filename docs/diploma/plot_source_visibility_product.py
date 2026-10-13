"""Illustrate one native visibility product; no image synthesis or policy changes."""
import hashlib
import json
from pathlib import Path
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap, BoundaryNorm
import numpy as np
ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / 'artifacts/source-visibility-frame0.rgba'
CONFIG = ROOT / 'artifacts/native-facing-configs-v1/frame0-s_zp_b020_e10mm-accept-facing.json'
MANIFEST = ROOT / 'artifacts/parallax-sensor-inputs-v1/rig101-near-z080/seed101-inputs/manifest.json'
LABELS = ['No carrier/ego','Eligible unclassified','Unavailable','Behind camera','Outside angle','Outside image','Vehicle footprint','Central blocked','Ambiguous rejected','Backfacing support','Ambiguous accepted','Clear accepted','Footprint blocked']


def main():
    config = json.loads(CONFIG.read_text())
    w,h = config['output']['width'],config['output']['height']
    mask = np.frombuffer(RAW.read_bytes(),np.uint8).reshape(h,w,4)
    if mask.max()>12:
        raise ValueError('unknown native visibility status')
    report_path = ROOT / 'docs/validation/baselines/source_visibility_example_v1.json'
    hashes = {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [RAW,CONFIG,MANIFEST,Path(__file__)]}
    report = dict(schema_version=1, scope='illustrative_native_source_visibility_not_quality_trial',frame=0,width=w,height=h,
        requested_egl_vendor='10_nvidia.json',backend_device_not_recorded_by_fixture=True,
        sha256=hashes, camera_histograms=[dict(zip(LABELS,np.bincount(mask[...,camera].ravel(),minlength=13).tolist())) for camera in range(4)])
    # Preserve binding when regenerating figures from archived data.
    if report_path.exists() and json.loads(report_path.read_text()) != report:
        raise ValueError('illustration inputs or plot changed; archive a new version explicitly')
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    colors=['#151515','#bdbdbd','#555555','#9467bd','#8c564b','#e377c2','#333333','#d62728','#ff7f0e','#bcbd22','#17becf','#2ca02c','#1f77b4']
    cmap=ListedColormap(colors); norm=BoundaryNorm(np.arange(-.5,13.5),13)
    fig,axes=plt.subplots(2,2,figsize=(13,8),layout='constrained')
    for camera,ax in enumerate(axes.ravel()):
        im=ax.imshow(mask[...,camera],cmap=cmap,norm=norm,interpolation='nearest')
        ax.set(title=f'Camera{camera}',xlabel='Output x / px',ylabel='Output y / px')
    bar=fig.colorbar(im,ax=axes.ravel().tolist(),ticks=np.arange(13),fraction=.025)
    bar.ax.set_yticklabels(LABELS)
    fig.suptitle('Native source_visibility_v1; frame0, Z+5mm, accept radius5mm, surface band10mm\nSupplied-geometry policy statuses, not independent scene truth')
    fig.savefig(Path(__file__).parent/'figures/experiments/source_visibility_example.png',dpi=160)
    plt.close(fig)


if __name__=='__main__':
    main()
