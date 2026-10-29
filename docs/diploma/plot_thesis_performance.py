"""Audit compiled-runner timing/telemetry reports; never dispatch product operations."""
import argparse
from pathlib import Path

import numpy as np

from plot_thesis_e3 import read, write, sha, checked
from plot_thesis_e2 import verify_sample


def distribution(values):
    values = sorted(float(x) for x in values if x is not None)
    if not values or not np.isfinite(values).all() or values[0] < 0:
        raise ValueError('nonempty finite nonnegative sample series required')
    def rank(p):
        return values[max(0, int(np.ceil(p * len(values))) - 1)]
    return dict(count=len(values), p50=rank(.5), p95=rank(.95), p99=rank(.99), maximum=values[-1])


def read_telemetry(branch, report):
    catalog = read(branch / 'catalog.json')
    if not catalog['accepted']:
        raise ValueError('telemetry catalog rejected')
    catalog = catalog['telemetry_catalog']
    pages = sorted(branch.glob('telemetry-*.json'), key=lambda p: int(p.stem.split('-')[-1]))
    if not pages:
        raise ValueError('missing telemetry pages')
    samples, after, bound = [], 0, None
    hashes = {}
    for path in pages:
        ack = read(path)
        if not ack['accepted']:
            raise ValueError('telemetry read rejected')
        page = ack['telemetry']
        if (page['server_instance_id'] != catalog['server_instance_id'] or page['epoch'] != catalog['epoch'] or
                not page['enabled'] or int(page['after_cursor']) != after or int(page['lost_samples']) != 0):
            raise ValueError('telemetry instance/epoch/cursor/loss mismatch')
        if bound is None:
            bound = int(page['latest_cursor'])
        for sample in page['samples']:
            if int(sample['cursor']) != after + 1:
                raise ValueError('non-contiguous telemetry')
            after += 1
            if after <= bound:
                samples.append(sample)
        if int(page['next_cursor']) != after:
            raise ValueError('telemetry next cursor mismatch')
        hashes[path.name] = sha(path)
    if after < bound:
        raise ValueError('incomplete telemetry snapshot')
    measured = [s for s in report['samples'] if not s['warmup']]
    start = min(int(s['metadata']['render_complete_timestamp_ns']) for s in measured)
    end = max(int(s['metadata']['render_complete_timestamp_ns']) for s in measured)
    ids = {s['metadata']['frame_id'] for s in measured}
    selected = [s for s in samples if start <= int(s['timestamp_ns']) <= end and
                s['frame'] is not None and s['frame']['frame_id'] in ids and s['paused']]
    if not selected:
        raise ValueError('no telemetry samples inside measured frame interval')
    cpu = [s['process']['cpu_percent'] for s in selected if s['process']['cpu_percent'] is not None]
    rss = [int(s['process']['rss_bytes']) / 2**20 for s in selected if s['process']['rss_bytes'] is not None]
    return dict(samples=selected, cpu_one_core_percent=distribution(cpu), rss_mib=distribution(rss),
        collection_wall_ms=distribution([s['collection_wall_ms'] for s in selected]),
        received_samples=len(samples), selected_samples=len(selected), snapshot_latest_cursor=str(bound),
        catalog_sha256=sha(branch / 'catalog.json'), pages_sha256=hashes,
        instance=catalog['server_instance_id'], epoch=catalog['epoch'])


def audit(runs, e2, figure, output):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    runs, e2, figure, output = map(Path, (runs, e2, figure, output))
    registry = read(runs / 'registry.json')
    if registry['source_registry_sha256'] != sha(e2 / 'registry.json') or len(registry['runs']) != 36:
        raise ValueError('frozen paired performance registry required')
    rows, telemetry, reports = [], [], []
    for run in registry['runs']:
        branch = runs / run['directory']
        for name in run['files']:
            checked(branch, name, run['files'])
        report, scenario = read(branch / 'report.json'), read(branch / 'scenario.json')
        source = e2 / run['view'] / run['case_id']
        quality_report, reference = read(source / 'report.json'), read(branch / 'reference.json')
        expected = dict(read(source / 'scenario.json'), capture_frames=False, warmup=5, repeats=30)
        observer = report['observation_policy']
        if (scenario != expected or report['scenario_input_sha256'] != sha(branch / 'scenario.json') or
                report['quality_reference']['manifest_sha256'] != sha(branch / 'reference.json') or
                not report['success'] or not report['restored'] or len(report['samples']) != 210 or
                report['scenario']['variants'] != expected['variants'] or report['cursor_restored'] or
                report['baseline_rgba_sha256'] != report['restored_rgba_sha256'] or
                not observer['telemetry_stable'] or observer['capture_frames'] or
                observer['capture_consumer'] != 'none' or not observer['quality_evaluation'] or
                observer['progress_callback'] or observer['telemetry_before'] !=
                    dict(enabled=run['telemetry'] == 'on', period_ms=50, history_capacity=256)):
            raise ValueError('performance workflow/observer policy mismatch')
        for name in ('reference.json', 'manifest.json', 'truth.ppm', 'mask.ppm'):
            if sha(branch / name) != sha(source / name):
                raise ValueError('performance source/reference changed')
        config = read(branch / 'config.json')
        original = read(source / 'config.json')
        original['telemetry'] = config['telemetry']
        if config != original:
            raise ValueError('non-telemetry config change')
        for index, variant in enumerate(scenario['variants']):
            samples = [s for s in report['samples'] if s['variant'] == index]
            measured = [s for s in samples if not s['warmup']]
            prior = next(s for s in quality_report['samples'] if s['variant'] == index and not s['warmup'])
            if len(samples) != 35 or len(measured) != 30 or any(s['rgba_sha256'] != prior['rgba_sha256'] for s in samples):
                raise ValueError('performance changed quality/repeat count')
            for sample in samples:
                # No raw output files by policy; validate the frame guards without a fabricated capture.
                verify_sample(branch, dict(sample, warmup=True), reference['frames'][0]['inputs'],
                    reference['view'], variant, None, None)
                if 'rgba_file' in sample:
                    raise ValueError('unexpected performance capture')
                if not sample['warmup'] and sample['quality'] != prior['quality']:
                    raise ValueError('performance native metric parity mismatch')
            spans = {name: distribution([s['metadata']['pipeline_spans_ms'][name] for s in measured])
                     for name in measured[0]['metadata']['pipeline_spans_ms']
                     if any(s['metadata']['pipeline_spans_ms'][name] is not None for s in measured)}
            for name, native in report['summaries'][index]['timings'].items():
                if name in spans:
                    observed = spans[name]
                    if (native['count'] != observed['count'] or native['p50_ms'] != observed['p50'] or
                            native['p95_ms'] != observed['p95']):
                        raise ValueError('native percentile mismatch')
            rows.append(dict(view=run['view'], case_id=run['case_id'], telemetry=run['telemetry'],
                variant=index, mode=variant['mode'], carrier=variant['surface']['type'], stages_ms=spans,
                dropped_batches_max=max(int(s['metadata']['source_dropped_batches']) for s in measured),
                retained_input_age_ms=distribution([s['metadata']['server_receive_to_render_ms'] for s in measured])))
        reports.append(dict(directory=run['directory'], report_sha256=sha(branch / 'report.json'),
            gl_renderer=report['catalog']['gl_renderer'], source_fingerprint=report['catalog']['source_fingerprint'],
            observer=observer, client_build_passport=report['client_build_passport'],
            server_build_passport=report['catalog']['build_passport']))
        if run['telemetry'] == 'on':
            telemetry.append(dict(view=run['view'], case_id=run['case_id'], **read_telemetry(branch, report)))
    fig, axes = plt.subplots(1, 3, figsize=(15, 4), constrained_layout=True)
    for mode, offset in (('off', -.15), ('on', .15)):
        medians = [float(np.median([r['stages_ms']['render_wall']['p95'] for r in rows
                                   if r['telemetry'] == mode and r['variant'] == index])) for index in range(6)]
        axes[0].bar(np.arange(6) + offset, medians, width=.3, label=mode)
    axes[0].set_xticks(range(6), ['P/H', 'P/F', 'P/M', 'D/H', 'D/F', 'D/M'])
    axes[0].set_yscale('log')
    axes[0].set_ylabel('Median per-run render wall p95, ms')
    axes[0].legend(title='Telemetry')
    example = telemetry[0]['samples']
    time = [(int(s['timestamp_ns']) - int(example[0]['timestamp_ns'])) / 1e9 for s in example]
    axes[1].plot(time, [s['process']['cpu_percent'] for s in example])
    axes[1].set_ylabel('Server CPU, one logical core = 100%')
    axes[2].plot(time, [int(s['process']['rss_bytes']) / 2**20 for s in example])
    axes[2].set_ylabel('Server RSS, MiB (not VRAM)')
    for axis in axes[1:]:
        axis.set_xlabel('Server sample time, s')
    fig.suptitle('PC short performance profile: 30 measured samples; CPU/RSS example high/street-a/m1001')
    figure.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(figure, dpi=140)
    plt.close(fig)
    for series in telemetry:
        series['samples'] = [dict(cursor=s['cursor'], timestamp_ns=s['timestamp_ns'],
            frame_id=s['frame']['frame_id'], cpu_one_core_percent=s['process']['cpu_percent'],
            rss_bytes=s['process']['rss_bytes'], collection_wall_ms=s['collection_wall_ms'],
            source_dropped=s['source']['dropped']) for s in series['samples']]
    result = dict(schema_version=1, scope='short_paused_pc_pipeline_profile', conditions=len(rows),
        native_trials=7560, measured_trials=6480, timing_samples_per_condition=30,
        registry_sha256=sha(runs / 'registry.json'), script_sha256=sha(__file__),
        figure_sha256=sha(figure), reports=reports, rows=rows, telemetry=telemetry,
        limitations=['not live sensor throughput, capture-to-display or target thermal acceptance',
            'quality evaluator and per-trial SHA256 remain enabled; file capture disabled',
            'CPU samples average previous collection interval; no per-algorithm attribution',
            'CPU/RSS polling occurs after timing; server collection itself runs during timing',
            'one paired process run per input/profile, not independently replicated hardware runs',
            'empirical p99 at N=30 equals maximum; no stable tail-latency inference'])
    write(output, result)
    print({k: result[k] for k in ('conditions', 'native_trials', 'measured_trials')})
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runs', 'e2', 'figure', 'output'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    audit(args.runs, args.e2, args.figure, args.output)
