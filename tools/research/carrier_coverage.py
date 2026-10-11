"""Reproduce/audit native carrier geometry masks; no image-fusion implementation."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from temporal_seam_stability import load_sequence
NAMES = ('background', 'floor', 'shell', 'vehicle_footprint', 'raised_bowl', 'vehicle_model')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def study(plan_path, root, probe=None):
    plan = json.loads(plan_path.read_text())
    rows, fingerprints = [], set()
    for source in plan['configs']:
        path = ROOT/source['path']
        if digest(path) != source['sha256']:
            raise ValueError('source config checksum mismatch')
        original = json.loads(path.read_text())
        for view in plan['views']:
            config = copy.deepcopy(original)
            config['virtual_camera'].update(view['override'])
            directory = root/source['id']/view['id']
            config_path = directory/'config.json'
            if probe:
                directory.mkdir(parents=True, exist_ok=True)
                config_path.write_text(json.dumps(config, indent=2)+'\n')
                subprocess.run([str(probe), str(config_path), str(directory)], check=True,
                               env=dict(os.environ, LIBGL_ALWAYS_SOFTWARE='1', EGL_PLATFORM='surfaceless'))
            if json.loads(config_path.read_text()) != config:
                raise ValueError('probe config differs from frozen plan')
            report_path = directory/'report.json'
            report = json.loads(report_path.read_text())
            mask_path = directory/'carrier-regions.u8'
            width, height = config['output']['width'], config['output']['height']
            if (report['config_sha256'] != digest(config_path)
                    or report['mask_sha256'] != digest(mask_path)
                    or (report['width'], report['height']) != (width, height)
                    or report['origin'] != 'top_left' or report['source_frames'] != 0
                    or report['scope'] != 'depth_tested_carrier_geometry'
                    or report['normal_output_unchanged'] is not True):
                raise ValueError('invalid native mask provenance/layout/parity')
            mask = np.fromfile(mask_path, dtype=np.uint8).reshape(height, width)
            if mask.max() >= len(NAMES):
                raise ValueError('unknown carrier region ID')
            counts = {name: int((mask == id).sum()) for id, name in enumerate(NAMES)}
            if counts != report['pixels'] or sum(counts.values()) != width*height:
                raise ValueError('mask counters do not partition the raster')
            fingerprints.add(report['source_fingerprint'])
            rows.append({'carrier': source['id'], 'view': view['id'], 'pixels': counts,
                         'fractions': {k: v/(width*height) for k, v in counts.items()},
                         'width': width, 'height': height,
                         'directory': directory.relative_to(root).as_posix(),
                         'mask_sha256': digest(mask_path), 'report_sha256': digest(report_path),
                         'config_sha256': digest(config_path),
                         'mesh_resources': report['mesh_resources']})
    intersections = []
    for seed in plan['roi_seeds']:
        dataset = ROOT/plan['paired_inputs_root']/f'seed{seed}-inputs'
        capture = ROOT/plan['paired_inputs_root']/f'seed{seed}-capture'
        cfg, _, _, rois, _, _ = load_sequence(dataset, capture, 'any')
        for row in (r for r in rows if r['view'] == 'historical'):
            directory = root/row['directory']
            config = json.loads((directory/'config.json').read_text())
            if any(config[k] != cfg[k] for k in ('virtual_camera', 'vehicle', 'output')):
                raise ValueError('historical ROI and geometry view/layout differ')
            mask = np.fromfile(directory/'carrier-regions.u8', dtype=np.uint8).reshape(row['height'], row['width'])
            for t, roi in enumerate(rois):
                if roi.shape != mask.shape:
                    raise ValueError('ROI layout mismatch')
                intersections.append({'seed': seed, 'truth_index': t, 'carrier': row['carrier'],
                    'roi_pixels': int(roi.sum()),
                    'roi_sha256': hashlib.sha256(roi.astype(np.uint8).tobytes()).hexdigest(),
                    'paired_truth_sha256': digest(capture/'paired_truth.json'),
                    'pixels': {name: int(((mask == id) & roi).sum()) for id, name in enumerate(NAMES)}})
    if len(fingerprints) != 1:
        raise ValueError('mixed source fingerprints')
    return {'schema_version': 1, 'experiment': plan['experiment'], 'plan_sha256': digest(plan_path),
            'source_fingerprint': fingerprints.pop(), 'scope': 'geometry_only_no_quality_ranking',
            'labels': list(NAMES), 'results': rows, 'historical_roi_intersections': intersections}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--probe', type=Path, help='run native probe; omit for read-only audit')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = study(args.plan.resolve(), args.root.resolve(), args.probe.resolve() if args.probe else None)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(f"Audited {len(result['results'])} native geometry masks")


if __name__ == '__main__':
    main()
