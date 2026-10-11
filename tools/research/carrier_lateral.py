"""Run native research commands and audit common floor/shell quality support."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from object_metrics import spatial_errors
from temporal_seam_stability import load_sequence
from server_boundary import audit_budget, audited_rgba, linear, verify_capture_case


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, indent=2)+'\n')


def run(plan_path, root, inputs, binaries):
    plan = json.loads(plan_path.read_text())
    cases = json.loads((ROOT/plan['inputs_plan']).read_text())['cases']
    for seed in plan['seeds']:
        verify_capture_case(next(c for c in cases if c['scenario']['seed'] == seed), inputs)
        dataset = inputs/f'seed{seed}-inputs'
        if not dataset.exists():
            subprocess.run([sys.executable, str(ROOT/'tools/blender/convert.py'),
                            '--capture', str(inputs/f'seed{seed}-capture'), '--output', str(dataset),
                            '--image-format', 'png'], check=True)
    root.mkdir(parents=True, exist_ok=False)
    write(root/'provenance.json', {'plan_sha256': sha(plan_path),
          'inputs_plan_sha256': sha(ROOT/plan['inputs_plan']),
          'scenario_sha256': sha(ROOT/plan['scenario'])})
    env = dict(os.environ, SV_EGL_PLATFORM='surfaceless', EGL_PLATFORM='surfaceless',
               LIBGL_ALWAYS_SOFTWARE='1')
    for seed in plan['seeds']:
        dataset = inputs/f'seed{seed}-inputs'
        for carrier in plan['carriers']:
            directory = root/f'seed{seed}'/carrier['id']
            directory.mkdir(parents=True)
            cfg = json.loads((dataset/'config.json').read_text())
            cfg['surface'] = carrier['surface']
            config = directory/'config.json'
            write(config, cfg)
            subprocess.run([str(binaries/'sv-carrier-probe'), str(config), str(directory/'geometry')],
                           env=env, check=True, timeout=60)
            with tempfile.TemporaryDirectory(prefix='sv-lateral-') as ipc:
                command = [str(binaries/'sv-server'), '--config', str(config), '--manifest',
                           str(dataset/'manifest.json'), '--ipc-dir', ipc,
                           '--trace', str(directory/'trace.jsonl')]
                client = [str(binaries/'svctl'), '--unix', ipc, '--timeout-ms', '10000',
                          'research', str(ROOT/plan['scenario']), str(directory/'report.json')]
                write(directory/'invocation.json', {'server': command, 'client': client,
                      'environment': {k: env[k] for k in ('SV_EGL_PLATFORM', 'EGL_PLATFORM', 'LIBGL_ALWAYS_SOFTWARE')},
                      'binary_sha256': {name: sha(binaries/name) for name in ('sv-server', 'svctl', 'sv-carrier-probe')}})
                with (directory/'server.log').open('w') as log:
                    process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
                    try:
                        deadline = time.monotonic()+30
                        while not all((Path(ipc)/name).exists() for name in ('control.sock', 'data.sock')):
                            if process.poll() is not None or time.monotonic() > deadline:
                                raise RuntimeError(f'server not ready: {directory}')
                            time.sleep(.05)
                        with (directory/'client.log').open('w') as client_log:
                            subprocess.run(client, env=env, check=True, timeout=300,
                                           stdout=client_log, stderr=subprocess.STDOUT)
                    finally:
                        process.terminate()
                        try:
                            process.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
            print(f'Native sequence complete: seed{seed}/{carrier["id"]}', flush=True)


def audit(plan_path, root, inputs):
    result = audit_budget(plan_path, root, inputs, 'CARRIER_LATERAL_PROTOCOL.md')
    result['analyzer_sha256'] = sha(Path(__file__))
    input_provenance = json.loads((inputs/'provenance.json').read_text())
    for name,digest in input_provenance['generator_sha256'].items():
        if sha(ROOT/'tools/blender'/name) != digest:
            raise ValueError('capture driver/helper differs from recorded source')
    result['input_provenance_sha256'] = sha(inputs/'provenance.json')
    revisions = set()
    result['geometry'] = []
    result['common_roi_counts'] = []
    result['historical_input_equivalence'] = []
    for case in result['cases']:
        revisions.update(n['catalog']['source_revision'] for n in case['audit']['native_reports'].values())
        seed = case['seed']
        cfg, images, truths, rois, stamps, poses = load_sequence(inputs/f'seed{seed}-inputs', inputs/f'seed{seed}-capture', 'any')
        historical = ROOT/result['plan']['historical_inputs_root']
        manifest_path = historical/f'seed{seed}-inputs/manifest.json'
        if sha(manifest_path) != result['plan']['historical_manifests_sha256'][str(seed)]:
            raise ValueError('historical input manifest differs from frozen reference')
        old_cfg, old_images, _, _, old_stamps, old_poses = load_sequence(
            historical/f'seed{seed}-inputs', historical/f'seed{seed}-capture', 'any')
        without_view = lambda value: {k:v for k,v in value.items() if k != 'virtual_camera'}
        if (without_view(cfg) != without_view(old_cfg) or stamps != old_stamps
                or not np.array_equal(poses, old_poses)
                or not np.array_equal(images, old_images)):
            raise ValueError('historical/new camera input, rig or trajectory differs')
        result['historical_input_equivalence'].append({'seed': seed,
            'historical_manifest_sha256': sha(manifest_path),
            'new_manifest_sha256': sha(inputs/f'seed{seed}-inputs/manifest.json'),
            'decoded_rgb_sha256': hashlib.sha256(np.asarray(images).tobytes()).hexdigest(),
            'decoded_camera_rgb_equal': True, 'rig_poses_timestamps_equal': True})
        masks = {}
        for carrier in result['plan']['carriers']:
            directory = root/f'seed{seed}'/carrier['id']
            report_path = directory/'geometry/report.json'
            report = json.loads(report_path.read_text())
            mask_path = directory/'geometry/carrier-regions.u8'
            carrier_type = {'dome_floor':'dome_floor_v1', 'cylinder_floor':'cylinder_floor_v1',
                            'cube_floor':'cube_floor_v1'}.get(carrier['id'],carrier['id'])
            if (report['config_sha256'] != sha(directory/'config.json')
                    or report['mask_sha256'] != sha(mask_path)
                    or report['source_fingerprint'] != result['source_fingerprint']
                    or report['origin'] != 'top_left' or report['source_frames'] != 0
                    or report['scope'] != 'depth_tested_carrier_geometry'
                    or report['mesh_resources']['active'] != case['resources'][carrier_type]
                    or report['mesh_resources']['resident'] != report['mesh_resources']['active']
                    or report['normal_output_unchanged'] is not True
                    or [report['width'], report['height']] != result['plan']['output']):
                raise ValueError('geometry/native config, source or layout differs')
            mask = np.fromfile(mask_path, np.uint8).reshape(report['height'], report['width'])
            names = ('background', 'floor', 'shell', 'vehicle_footprint', 'raised_bowl', 'vehicle_model')
            if mask.max() >= len(names) or report['pixels'] != {n: int((mask == i).sum()) for i,n in enumerate(names)}:
                raise ValueError('invalid geometry IDs/counters')
            masks[carrier['id']] = mask
            result['geometry'].append({'seed': seed, 'carrier': carrier['id'], 'pixels': report['pixels'],
                                       'mask_sha256': sha(mask_path), 'report_sha256': sha(report_path)})
        enclosures = [masks[k] for k in ('dome_floor', 'cylinder_floor', 'cube_floor')]
        common_floor = np.logical_and.reduce([m == 1 for m in enclosures])
        common_shell = np.logical_and.reduce([m == 2 for m in enclosures])
        groups = []
        for t, roi in enumerate(rois):
            floor, shell = roi & common_floor, roi & common_shell
            group = dict(common_floor=floor, common_shell=shell, other=roi & ~(floor | shell))
            if not np.array_equal(np.logical_or.reduce(list(group.values())), roi):
                raise ValueError('common regions do not partition quality ROI')
            groups.append(group)
            result['common_roi_counts'].append({'seed': seed, 'truth_index': t,
                'groups': {k: {'pixels': int(v.sum()), 'sha256': hashlib.sha256(v.astype(np.uint8).tobytes()).hexdigest()}
                           for k,v in group.items()}})
        for row in case['audit']['results']:
            t = row['truth_index']
            rgba = audited_rgba(case, row, root)
            metrics = spatial_errors(np.abs(linear(rgba[...,:3]/255.)-linear(truths[t])), groups[t])
            weighted = sum(m['pixels']*(m['linear_mae'] or 0) for m in metrics.values())/int(rois[t].sum())
            if abs(weighted-row['quality']['linear_mae']) > 1e-12:
                raise ValueError('region metrics do not reproduce aggregate error')
            row['carrier_region_quality'] = metrics
        del case['audit']['native_reports']
    result['native_build_revisions'] = sorted(revisions)
    result['limitations'] += ['new view-specific truth; historical errors are not same-ray paired measurements',
                               'common masks are carrier geometry, not scene physical-height classes']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--run-binaries', type=Path, help='launch fresh native processes; omit for audit only')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    plan, root, inputs = (p.resolve() for p in (args.plan, args.root, args.inputs))
    if args.run_binaries:
        run(plan, root, inputs, args.run_binaries.resolve())
    result = audit(plan, root, inputs)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write(args.output, result)
    print('Audited', sum(len(c['audit']['results']) for c in result['cases']), 'quality conditions')


if __name__ == '__main__':
    main()
