"""Frozen E3 fixture preparation and post-processing; all renders/metrics run natively."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys

import numpy as np
from PIL import Image
from scipy import ndimage as ndi

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/blender'))
from convert import convert


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text())


def write(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n')


def checked(root, name, hashes):
    path = (root / name).resolve()
    if not path.is_relative_to(root.resolve()) or name not in hashes or sha(path) != hashes[name]:
        raise ValueError('unverified fixture file: ' + name)
    return path


def evaluation_mask(labels, bits, ego_ids):
    if (labels.ndim != 2 or bits.shape != labels.shape or bits.dtype != np.uint8 or
            not np.issubdtype(labels.dtype, np.integer) or np.any(bits > 15)):
        raise ValueError('invalid object/visibility arrays')
    base = (labels != 0) & ~np.isin(labels, ego_ids) & (bits != 0)
    # Frozen before inspection of server outputs: Chebyshev-radius 2, outside=false.
    mask = ndi.binary_erosion(base, structure=np.ones((5, 5), bool), border_value=0)
    if not mask.any():
        raise ValueError('empty E3 evaluation mask')
    return mask


def input_guard(camera, image, stamp):
    h, w, channels = image.shape
    if channels != 3 or image.dtype != np.uint8 or camera['resolution'] != dict(width=w, height=h):
        raise ValueError('camera/image dimensions or encoding mismatch')
    p = camera['projection']
    values = [p[k] for k in ('fx', 'fy', 'cx', 'cy', 'theta_max_rad', 'z_epsilon_m')]
    values += p['k'] + sum(camera['T_camera_from_vehicle'], [])
    if len(values) != 26 or not np.isfinite(values).all():
        raise ValueError('finite numeric camera model required')
    values = [0.0 if v == 0 else v for v in values]
    return dict(camera_id=camera['id'], calibration_id=camera['calibration_id'],
        source_timestamp_ns=stamp,
        camera_model_sha256=hashlib.sha256(b'sv-camera-model-v1\0' +
            struct.pack('>III', camera['id'], w, h) + struct.pack('>26d', *values)).hexdigest(),
        image_identity=dict(algorithm='packed_rgb8_sha256_v1',
            sha256=hashlib.sha256(b'sv-packed-rgb8-v1\0' + struct.pack('>II', w, h) + image.tobytes()).hexdigest(),
            width=w, height=h, pixel_format='RGB8', row_origin='top_left'))


def prepare(prepared, capture_root, view, case_id, output):
    prepared, capture_root, output = map(Path, (prepared, capture_root, output))
    registry = read(prepared / 'registry.json')
    for name, digest in registry['files'].items():
        if sha(prepared / name) != digest:
            raise ValueError('prepared registry mismatch')
    plan = read(prepared / f'capture-{view}.json')
    selected = [c for c in plan['cases'] if c['id'] == case_id]
    if len(selected) != 1:
        raise ValueError('registered capture case required')
    case = selected[0]
    job = read(capture_root / 'job.json')
    raw = capture_root / case_id / 'raw'
    completed = [c for c in job['completed'] if c['id'] == case_id]
    if (job['plan_sha256'] != sha(prepared / f'capture-{view}.json') or len(completed) != 1 or
            completed[0]['capture_sha256'] != sha(raw / 'capture.json')):
        raise ValueError('completed capture bound to registered plan required')
    capture, paired = read(raw / 'capture.json'), read(raw / 'paired_truth.json')
    actual = case['camera_rig']['actual']
    if (capture['config'] != actual or paired['config'] != actual or
            capture['nominal_config'] != case['camera_rig']['nominal'] or
            capture.get('camera_rig_provenance') != case['camera_rig']['provenance'] or
            capture.get('scenario_recipe') != case['recipe'] or
            paired['capture_sha256'] != sha(raw / 'capture.json') or
            len(capture['frames']) != 1 or len(paired['frames']) != 1 or
            not 0 <= paired['virtual_projection_max_error_px'] <= .001):
        raise ValueError('capture rig/reference identity mismatch')
    for metadata in (capture, paired):
        for name in metadata['sha256']:
            checked(raw, name, metadata['sha256'])
    row, target = capture['frames'][0], paired['frames'][0]
    if (row['scenario_timestamp_ns'] != target['scenario_timestamp_ns'] or
            row['T_world_from_vehicle'] != target['T_world_from_vehicle'] or
            [c['id'] for c in row['cameras']] != list(range(4))):
        raise ValueError('capture timestamp/pose/order mismatch')
    for camera, captured in zip(actual['cameras'], row['cameras']):
        expected = np.array(row['T_world_from_vehicle']) @ np.linalg.inv(camera['T_camera_from_vehicle'])
        if not np.allclose(captured['T_world_from_camera'], expected, rtol=0, atol=1e-12):
            raise ValueError('actual optical pose differs from E1 matrix')
    labels = np.load(raw / target['objects'], allow_pickle=False)
    bits = np.load(raw / target['visibility'], allow_pickle=False)
    with Image.open(raw / target['rgb']) as image:
        rgb = np.array(image.convert('RGB'))
    shape = (actual['output']['height'], actual['output']['width'])
    if labels.shape != shape or rgb.shape != (*shape, 3):
        raise ValueError('reference resolution mismatch')
    if not np.isin(labels, [0, *paired['objects'].values()]).all():
        raise ValueError('unregistered object labels')
    mask = evaluation_mask(labels, bits, paired['ego_object_ids'])
    output.mkdir(parents=True, exist_ok=False)
    convert(raw, output / 'inputs')
    Image.fromarray(rgb).save(output / 'truth.ppm')
    Image.fromarray(np.repeat((mask * 255).astype(np.uint8)[..., None], 3, axis=2)).save(output / 'mask.ppm')
    manifest = read(output / 'inputs/manifest.json')
    images = [np.array(Image.open(output / 'inputs' / name).convert('RGB'))
              for name in manifest['frames'][0]['paths']]
    seed = case['recipe']['mount_seed']
    scenario = read(ROOT / 'configs/research/thesis-core.json')
    scenario['variants'] = [v for v in scenario['variants']
                            if v['mode'] == 'edge_feather' and v['surface']['type'] == 'dome_floor_v1']
    assert len(scenario['variants']) == 1
    scenario['quality_reference'] = 'reference.json'
    for state in ('nominal', 'estimated', 'true'):
        branch = output / state
        branch.mkdir()
        config_path = prepared / f'profiles/{view}/mount{seed}/{state}.json'
        config = read(config_path)
        shutil.copyfile(config_path, branch / 'config.json')
        branch_manifest = copy.deepcopy(manifest)
        branch_manifest['calibration_ids'] = [c['calibration_id'] for c in config['cameras']]
        for name in manifest['sha256']:
            shutil.copyfile(output / 'inputs' / name, branch / name)
        write(branch / 'manifest.json', branch_manifest)
        shutil.copyfile(output / 'truth.ppm', branch / 'truth.ppm')
        shutil.copyfile(output / 'mask.ppm', branch / 'mask.ppm')
        inputs = [input_guard(c, i, target['scenario_timestamp_ns']) for c, i in zip(config['cameras'], images)]
        write(branch / 'reference.json', dict(schema_version=2, kind='independent_ground_truth',
            dataset_id=f'thesis-e3-{view}-{case_id}', **config['output'],
            view=dict(config['virtual_camera'], target_m=[0., 0., 0.]),
            frames=[dict(rgb='truth.ppm', mask='mask.ppm', inputs=inputs)]))
        write(branch / 'scenario.json', scenario)
    audit = dict(schema_version=1, scope='E3_fixture_preparation_no_server_results',
        view=view, case_id=case_id, mount_seed=seed, scene_seed=case['recipe']['scene_seed'],
        capture_sha256=sha(raw / 'capture.json'), paired_truth_sha256=sha(raw / 'paired_truth.json'),
        plan_sha256=sha(prepared / f'capture-{view}.json'), registry_sha256=sha(prepared / 'registry.json'),
        roi_pixels=int(mask.sum()), total_pixels=int(mask.size),
        mask_policy=registry['mask_policy'], erosion='5x5 all-ones kernel; outside=false',
        shared_truth_sha256=sha(output / 'truth.ppm'), shared_mask_sha256=sha(output / 'mask.ppm'),
        files={p.relative_to(output).as_posix(): sha(p) for p in sorted(output.rglob('*')) if p.is_file()})
    write(output / 'fixture-audit.json', audit)
    print(json.dumps({k: audit[k] for k in ('view', 'case_id', 'roi_pixels')}))
    return audit


def audit_runs(runs, figure, output_audit):
    """Verify frozen native reports/captures and independently recompute RGB endpoints."""
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    runs, figure, output_audit = map(Path, (runs, figure, output_audit))
    rows, panels, identities, fixtures = [], {}, {}, []
    metric_comparisons = 0
    maximum_difference = 0.
    expected_cases = [f'{scene}-m{seed}' for scene in ('street-a', 'street-b', 'yard-a')
                      for seed in (1001, 1002, 1003)]
    for view in ('high', 'low'):
        for case_id in expected_cases:
            case = runs / view / case_id
            fixture = read(case / 'fixture-audit.json')
            if fixture['case_id'] != case_id or fixture['view'] != view:
                raise ValueError('case audit identity mismatch')
            for name in fixture['files']:
                checked(case, name, fixture['files'])
            fixtures.append(dict(view=view, case_id=case_id, sha256=sha(case / 'fixture-audit.json'),
                                 **{k: fixture[k] for k in ('capture_sha256', 'paired_truth_sha256',
                                    'roi_pixels', 'shared_truth_sha256', 'shared_mask_sha256')}))
            with Image.open(case / 'truth.ppm') as image:
                truth = np.array(image.convert('RGB'))
            with Image.open(case / 'mask.ppm') as image:
                mask = np.array(image.convert('RGB'))[..., 0] == 255
            if int(mask.sum()) != fixture['roi_pixels']:
                raise ValueError('mask pixel count mismatch')
            panels[(view, case_id, 'truth')] = truth
            for state in ('nominal', 'estimated', 'true'):
                branch = case / state
                report = read(branch / 'report.json')
                scenario, reference = read(branch / 'scenario.json'), read(branch / 'reference.json')
                if (not report['success'] or not report['restored'] or report['cursor_restored'] or
                        report['scenario_input_sha256'] != sha(branch / 'scenario.json') or
                        report['quality_reference']['manifest_sha256'] != sha(branch / 'reference.json') or
                        report['baseline_rgba_sha256'] != report['restored_rgba_sha256'] or
                        len(report['samples']) != 9 or len(report['scenario']['variants']) != 1 or
                        report['scenario']['variants'] != scenario['variants'] or
                        report['quality_acceptance']['status'] != 'not_requested' or
                        report['quality_reference']['schema_version'] != 2):
                    raise ValueError('incomplete or mismatched native E3 workflow')
                if (sha(branch / 'truth.ppm') != fixture['shared_truth_sha256'] or
                        sha(branch / 'mask.ppm') != fixture['shared_mask_sha256']):
                    raise ValueError('branch-specific reference/mask change')
                measured = [s for s in report['samples'] if not s['warmup']]
                if len(measured) != 7 or len({s['rgba_sha256'] for s in report['samples']}) != 1:
                    raise ValueError('repeat count or frozen RGB stability failure')
                expected_inputs = reference['frames'][0]['inputs']
                pixel_ids = [x['image_identity'] for x in expected_inputs]
                identity_key = (case_id, state)
                if identity_key in identities and identities[identity_key] != pixel_ids:
                    raise ValueError('high/low physical source images differ')
                identities[identity_key] = pixel_ids
                for sample in report['samples']:
                    meta = sample['metadata']
                    observed = [{k: item[k] for k in expected_inputs[i]}
                                for i, item in enumerate(meta['inputs'])]
                    if (observed != expected_inputs or len(observed) != 4 or
                            not all(x['used'] for x in meta['inputs']) or meta['view'] != reference['view'] or
                            meta['health'] != 'READY' or not meta['paused'] or
                            meta['width'] != 320 or meta['height'] != 180 or
                            meta['mesh_triangles'] != 3200 or meta['fusion_mode'] != 'edge_feather' or
                            meta['surface']['type'] != 'dome_floor_v1' or
                            meta['pixel_format'] != 'RGBA8' or meta['row_origin'] != 'top_left'):
                        raise ValueError('native frame guards mismatch')
                    if sample['warmup']:
                        continue
                    raw_path = branch / sample['rgba_file']
                    if sha(raw_path) != sample['rgba_sha256']:
                        raise ValueError('native RGBA capture hash mismatch')
                    rgba = np.frombuffer(raw_path.read_bytes(), np.uint8).reshape(180, 320, 4)
                    if not np.all(rgba[..., 3] == 255):
                        raise ValueError('unexpected transparent output')
                    delta = rgba[..., :3].astype(np.int32) - truth.astype(np.int32)
                    selected = delta[mask]
                    mse = float(np.square(selected).sum()) / selected.size
                    expected = dict(mae_rgb8=float(np.abs(selected).sum()) / selected.size,
                        rmse_rgb8=float(np.sqrt(mse)), max_absolute_rgb8=int(np.abs(selected).max()),
                        psnr_db=None if mse == 0 else float(10*np.log10(255.**2/mse)))
                    quality = sample['quality']
                    if quality['selected_pixels'] != int(mask.sum()) or quality['exact_match'] != (mse == 0):
                        raise ValueError('native ROI or exact-match mismatch')
                    for name, value in expected.items():
                        if value is None:
                            if quality[name] is not None:
                                raise ValueError('unexpected finite perfect-match PSNR')
                        else:
                            difference = abs(quality[name] - value)
                            maximum_difference = max(maximum_difference, difference)
                            if not np.isfinite(difference) or difference > 1e-10:
                                raise ValueError('native/offline quality mismatch')
                        metric_comparisons += 1
                panels[(view, case_id, state)] = rgba[..., :3].copy()
                summary = report['quality_summaries'][0]
                if (summary['frames'] != 1 or summary['selected_pixels'] != int(mask.sum()) or
                        abs(summary['pixel_weighted_mae_rgb8'] - measured[0]['quality']['mae_rgb8']) > 1e-10):
                    raise ValueError('quality summary mismatch')
                rows.append(dict(view=view, case_id=case_id, state=state,
                    report_sha256=sha(branch / 'report.json'), roi_pixels=int(mask.sum()),
                    rgba_sha256=measured[0]['rgba_sha256'], **measured[0]['quality'],
                    timings=report['summaries'][0]['timings'],
                    gl_renderer=report['catalog']['gl_renderer'],
                    source_fingerprint=report['catalog']['source_fingerprint'],
                    server_build_passport=report['catalog']['build_passport']['sha256'],
                    client_build_passport=report['client_build_passport']['sha256']))
            input_sets = [read(case / state / 'reference.json')['frames'][0]['inputs']
                          for state in ('nominal', 'estimated', 'true')]
            if not all([g['image_identity'] for g in guards] == pixel_ids for guards in input_sets):
                raise ValueError('physical inputs differ across calibration branches')
    paired = []
    for view in ('high', 'low'):
        for case_id in expected_cases:
            values = {r['state']: r for r in rows if r['view'] == view and r['case_id'] == case_id}
            paired.append(dict(view=view, case_id=case_id,
                nominal_mae=values['nominal']['mae_rgb8'], estimated_mae=values['estimated']['mae_rgb8'],
                true_mae=values['true']['mae_rgb8'],
                improvement=values['nominal']['mae_rgb8'] - values['estimated']['mae_rgb8'],
                estimated_minus_true=values['estimated']['mae_rgb8'] - values['true']['mae_rgb8']))
    fig, axes = plt.subplots(6, 4, figsize=(14, 12), constrained_layout=True)
    for row_index, (view, scene) in enumerate((v, s) for v in ('high', 'low')
                                            for s in ('street-a', 'street-b', 'yard-a')):
        case_id = scene + '-m1001'  # Frozen representative, never select by observed gain.
        for col, state in enumerate(('truth', 'nominal', 'estimated', 'true')):
            ax = axes[row_index, col]
            ax.imshow(panels[(view, case_id, state)], interpolation='nearest')
            title = f'{view}/{scene}: {state}'
            if state != 'truth':
                item = next(r for r in rows if (r['view'], r['case_id'], r['state']) == (view, case_id, state))
                title += f"; MAE {item['mae_rgb8']:.2f}"
            ax.set_title(title, fontsize=9)
            ax.axis('off')
    fig.suptitle('E3: independent direct view and native dome-floor / edge-feather; mount seed 1001')
    figure.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(figure, dpi=140)
    plt.close(fig)
    audit = dict(schema_version=1, scope='E3_static_synthetic_calibration_effect',
        conditions=len(rows), native_trials=486, measured_captures=378,
        metric_comparisons=metric_comparisons, max_metric_difference=maximum_difference,
        improved_pairs=sum(p['improvement'] > 0 for p in paired), paired=paired,
        fixtures=fixtures, rows=rows, figure_sha256=sha(figure),
        script_sha256=sha(__file__),
        limitations=['three procedural instances, shared mount seeds; not independent population samples',
                     'no physical cameras/Aurora acceptance; no photometric noise or moving scene',
                     'true calibration does not eliminate carrier parallax/occlusion error',
                     'quality repeats are identical; not additional independent scenes',
                     'quality timing run is descriptive, separate performance profile pending'])
    output_audit.parent.mkdir(parents=True, exist_ok=True)
    write(output_audit, audit)
    print(json.dumps({k: audit[k] for k in ('conditions', 'improved_pairs', 'metric_comparisons', 'max_metric_difference')}))
    return audit


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    fixture = commands.add_parser('prepare')
    fixture.add_argument('prepared', type=Path)
    fixture.add_argument('capture_root', type=Path)
    fixture.add_argument('view', choices=('high', 'low'))
    fixture.add_argument('case_id')
    fixture.add_argument('output', type=Path)
    audit = commands.add_parser('audit')
    audit.add_argument('runs', type=Path)
    audit.add_argument('figure', type=Path)
    audit.add_argument('output_audit', type=Path)
    args = parser.parse_args()
    if args.command == 'prepare':
        prepare(args.prepared, args.capture_root, args.view, args.case_id, args.output)
    else:
        audit_runs(args.runs, args.figure, args.output_audit)
