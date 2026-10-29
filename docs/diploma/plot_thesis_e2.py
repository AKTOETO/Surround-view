"""Prepare frozen E2 scientific fixtures and audit native reports; no product dispatcher."""
import argparse
import copy
from pathlib import Path
import shutil

import numpy as np
from PIL import Image

from plot_thesis_e3 import ROOT, read, write, sha, checked

CASES = [f'{scene}-m{seed}' for scene in ('street-a', 'street-b', 'yard-a')
         for seed in (1001, 1002, 1003)]


def prepare(e3, output):
    e3, output = Path(e3), Path(output)
    plan = read(ROOT / 'configs/research/thesis-core.json')
    plan['quality_reference'] = 'reference.json'
    source_files = {}
    for view in ('high', 'low'):
        for identifier in CASES:
            source = e3 / view / identifier
            audit = read(source / 'fixture-audit.json')
            if (audit['view'], audit['case_id']) != (view, identifier):
                raise ValueError('E3 case identity mismatch')
            for name in audit['files']:
                checked(source, name, audit['files'])
            branch = source / 'true'
            manifest = read(branch / 'manifest.json')
            names = ['config.json', 'manifest.json', 'reference.json', 'truth.ppm', 'mask.ppm',
                     *manifest['sha256']]
            source_files[(view, identifier)] = (branch, names)
    output.mkdir(parents=True, exist_ok=False)
    registry = dict(schema_version=1, scope='E2_true_calibration_frozen_inputs',
                    plan_sha256=sha(ROOT / 'configs/research/thesis-core.json'), cases=[])
    for (view, identifier), (source, names) in source_files.items():
        destination = output / view / identifier
        destination.mkdir(parents=True)
        for name in names:
            shutil.copyfile(source / name, destination / name)
        write(destination / 'scenario.json', plan)
        registry['cases'].append(dict(view=view, case_id=identifier,
            e3_fixture_sha256=sha(e3 / view / identifier / 'fixture-audit.json'),
            files={name: sha(destination / name) for name in [*names, 'scenario.json']}))
    write(output / 'registry.json', registry)
    return registry


def verify_sample(branch, sample, expected_inputs, view, variant, truth, mask):
    meta = sample['metadata']
    observed = [{k: item[k] for k in expected_inputs[i]} for i, item in enumerate(meta['inputs'])]
    surface = variant['surface']
    triangles = 3196 if surface['type'] == 'rectangular_bowl_v1' else 3200
    resources = meta['mesh_resources']['active']
    if (observed != expected_inputs or len(observed) != 4 or not all(x['used'] for x in meta['inputs']) or
            meta['view'] != view or meta['health'] != 'READY' or not meta['paused'] or
            meta['source_type'] != 'replay' or meta['width'] != 320 or meta['height'] != 180 or
            meta['pixel_format'] != 'RGBA8' or meta['row_origin'] != 'top_left' or
            meta['mesh_triangles'] != triangles or resources['buffer_bytes'] > 60500 or
            resources['vertices'] > 1810 or
            meta['fusion_mode'] != variant['mode'] or meta['surface'] != surface):
        raise ValueError('E2 native frame/input/resource guards failed')
    if sample['warmup']:
        return None, 0.
    raw = (branch / sample['rgba_file']).resolve()
    if not raw.is_relative_to(branch.resolve()) or sha(raw) != sample['rgba_sha256']:
        raise ValueError('E2 RGBA capture mismatch')
    rgba = np.frombuffer(raw.read_bytes(), np.uint8).reshape(180, 320, 4)
    if not np.all(rgba[..., 3] == 255):
        raise ValueError('unexpected E2 alpha')
    delta = (rgba[..., :3].astype(np.int32) - truth.astype(np.int32))[mask]
    mse = float(np.square(delta).sum()) / delta.size
    expected = dict(mae_rgb8=float(np.abs(delta).sum()) / delta.size,
        rmse_rgb8=float(np.sqrt(mse)), max_absolute_rgb8=int(np.abs(delta).max()),
        psnr_db=None if mse == 0 else float(10*np.log10(255.**2/mse)))
    quality = sample['quality']
    if quality['selected_pixels'] != int(mask.sum()) or quality['exact_match'] != (mse == 0):
        raise ValueError('E2 selected pixels/exact-match mismatch')
    maximum = 0.
    for key, number in expected.items():
        if number is None:
            if quality[key] is not None:
                raise ValueError('E2 perfect-match PSNR mismatch')
        else:
            difference = abs(quality[key] - number)
            if not np.isfinite(difference) or difference > 1e-10:
                raise ValueError('E2 native/offline metrics differ')
            maximum = max(maximum, difference)
    return rgba[..., :3].copy(), maximum


def prepare_performance(e2, output):
    """Freeze paired observer profiles before timing; execution stays in native tools."""
    e2, output = Path(e2), Path(output)
    registry = read(e2 / 'registry.json')
    for case in registry['cases']:
        for name in case['files']:
            checked(e2 / case['view'] / case['case_id'], name, case['files'])
    output.mkdir(parents=True, exist_ok=False)
    result = dict(schema_version=1, scope='paired_short_pc_performance_not_target_acceptance',
        order_policy='case_alternating_off_on_v1', source_registry_sha256=sha(e2 / 'registry.json'), runs=[])
    for index, case in enumerate(registry['cases']):
        source = e2 / case['view'] / case['case_id']
        for mode in (('off', 'on') if index % 2 == 0 else ('on', 'off')):
            relative = Path(case['view']) / case['case_id'] / mode
            destination = output / relative
            destination.mkdir(parents=True)
            for name in case['files']:
                shutil.copyfile(source / name, destination / name)
            config, scenario = read(destination / 'config.json'), read(destination / 'scenario.json')
            config['telemetry'] = dict(enabled=mode == 'on', period_ms=50, history_capacity=256)
            scenario.update(warmup=5, repeats=30, capture_frames=False)
            write(destination / 'config.json', config)
            write(destination / 'scenario.json', scenario)
            result['runs'].append(dict(directory=relative.as_posix(), view=case['view'],
                case_id=case['case_id'], telemetry=mode,
                files={name: sha(destination / name) for name in case['files']}))
    write(output / 'registry.json', result)
    return result


def audit(runs, e3, figure, output):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    runs, e3, figure, output = map(Path, (runs, e3, figure, output))
    registry = read(runs / 'registry.json')
    expected_plan = read(ROOT / 'configs/research/thesis-core.json')
    expected_plan['quality_reference'] = 'reference.json'
    expected_keys = [(v, c) for v in ('high', 'low') for c in CASES]
    if [(c['view'], c['case_id']) for c in registry['cases']] != expected_keys:
        raise ValueError('E2 requires all 18 registered cases')
    rows, panels, source_reports = [], {}, []
    maximum = 0.
    cross_checks = 0
    for case in registry['cases']:
        view, identifier = case['view'], case['case_id']
        branch = runs / view / identifier
        for name in case['files']:
            checked(branch, name, case['files'])
        report, reference = read(branch / 'report.json'), read(branch / 'reference.json')
        if (read(branch / 'scenario.json') != expected_plan or not report['success'] or
                not report['restored'] or report['cursor_restored'] or len(report['samples']) != 54 or
                report['scenario_input_sha256'] != sha(branch / 'scenario.json') or
                report['scenario']['variants'] != expected_plan['variants'] or
                report['quality_reference']['manifest_sha256'] != sha(branch / 'reference.json') or
                report['quality_acceptance']['status'] != 'not_requested' or
                report['baseline_rgba_sha256'] != report['restored_rgba_sha256']):
            raise ValueError('E2 workflow/scenario/restore mismatch')
        original = e3 / view / identifier
        if sha(original / 'fixture-audit.json') != case['e3_fixture_sha256']:
            raise ValueError('E3 fixture changed')
        for name in ('reference.json', 'truth.ppm', 'mask.ppm', 'config.json', 'manifest.json'):
            if sha(branch / name) != sha(original / 'true' / name):
                raise ValueError('E2 changed frozen E3 true branch')
        with Image.open(branch / 'truth.ppm') as image:
            truth = np.array(image.convert('RGB'))
        with Image.open(branch / 'mask.ppm') as image:
            mask = np.array(image.convert('RGB'))[..., 0] == 255
        panels[(view, identifier, 'truth')] = truth
        source_reports.append(dict(view=view, case_id=identifier, report_sha256=sha(branch / 'report.json'),
            registry_files=case['files'], gl_renderer=report['catalog']['gl_renderer'],
            source_fingerprint=report['catalog']['source_fingerprint'],
            server_build_passport=report['catalog']['build_passport'],
            client_build_passport=report['client_build_passport']))
        for index, variant in enumerate(expected_plan['variants']):
            samples = [s for s in report['samples'] if s['variant'] == index]
            measured = [s for s in samples if not s['warmup']]
            if len(samples) != 9 or len(measured) != 7 or len({s['rgba_sha256'] for s in samples}) != 1:
                raise ValueError('E2 frozen repeats differ')
            for sample in samples:
                rgb, difference = verify_sample(branch, sample, reference['frames'][0]['inputs'],
                    reference['view'], variant, truth, mask)
                maximum = max(maximum, difference)
                if rgb is not None:
                    panels[(view, identifier, index)] = rgb
            if index == 4:
                previous = read(original / 'true/report.json')
                if measured[0]['rgba_sha256'] != next(s for s in previous['samples'] if not s['warmup'])['rgba_sha256']:
                    raise ValueError('E2/E3 matched dome-feather output differs')
                cross_checks += 1
            summary = report['quality_summaries'][index]
            if (summary['frames'] != 1 or summary['selected_pixels'] != int(mask.sum()) or
                    abs(summary['pixel_weighted_mae_rgb8'] - measured[0]['quality']['mae_rgb8']) > 1e-10):
                raise ValueError('E2 quality summary mismatch')
            rows.append(dict(view=view, case_id=identifier, variant=index, mode=variant['mode'],
                carrier=variant['surface']['type'], **measured[0]['quality'],
                rgba_sha256=measured[0]['rgba_sha256'], mesh_resources=measured[0]['metadata']['mesh_resources'],
                timings=report['summaries'][index]['timings']))
    fig, axes = plt.subplots(6, 7, figsize=(21, 11), constrained_layout=True)
    for row, (view, scene) in enumerate((v, s) for v in ('high', 'low')
                                       for s in ('street-a', 'street-b', 'yard-a')):
        identifier = scene + '-m1001'
        for col, index in enumerate(['truth', *range(6)]):
            axes[row, col].imshow(panels[(view, identifier, index)], interpolation='nearest')
            title = 'Direct truth' if index == 'truth' else (
                ('plane' if index < 3 else 'dome') + '/' + ['hard', 'feather', 'multiband'][index % 3])
            if index != 'truth':
                value = next(r for r in rows if (r['view'], r['case_id'], r['variant']) == (view, identifier, index))
                title += f"\nMAE {value['mae_rgb8']:.2f}"
            axes[row, col].set_title(title, fontsize=8)
            axes[row, col].axis('off')
        axes[row, 0].text(0., 1.15, f'{view}/{scene}', transform=axes[row, 0].transAxes, fontsize=10)
    fig.suptitle('E2: frozen true calibration; 3 fusion methods x 2 carriers; mount seed 1001')
    figure.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(figure, dpi=125)
    plt.close(fig)
    result = dict(schema_version=1, scope='E2_static_true_calibration_fusion_carrier',
        conditions=len(rows), native_trials=972, measured_captures=756, metric_comparisons=3024,
        max_metric_difference=maximum, e3_rgba_cross_checks=cross_checks,
        registry_sha256=sha(runs / 'registry.json'), script_sha256=sha(__file__),
        figure_sha256=sha(figure), reports=source_reports, rows=rows,
        limitations=['three procedural instances/two families; mounts reused across views/scenes',
                     'no population significance, real-driver or Aurora acceptance',
                     'RGB8 ROI endpoint is not ghost/seam safety or physical geometry accuracy',
                     'quality repeats are not independent images or performance acceptance'])
    write(output, result)
    print({k: result[k] for k in ('conditions', 'metric_comparisons', 'max_metric_difference', 'e3_rgba_cross_checks')})
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('prepare')
    p.add_argument('e3', type=Path)
    p.add_argument('output', type=Path)
    p = commands.add_parser('prepare-performance')
    p.add_argument('e2', type=Path)
    p.add_argument('output', type=Path)
    p = commands.add_parser('audit')
    for name in ('runs', 'e3', 'figure', 'output'):
        p.add_argument(name, type=Path)
    args = parser.parse_args()
    if args.command == 'prepare':
        prepare(args.e3, args.output)
    elif args.command == 'prepare-performance':
        prepare_performance(args.e2, args.output)
    else:
        audit(args.runs, args.e3, args.figure, args.output)
