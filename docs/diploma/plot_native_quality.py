#!/usr/bin/env python3
"""Plot external direct RGB/ROI and native samples from a completed parity test."""
import argparse
import hashlib
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

def summarize_suite(root, plan_path, budget_path):
    plan = json.loads(plan_path.read_text())
    budget = json.loads(budget_path.read_text())
    plan_hash = hashlib.sha256(plan_path.read_bytes()).hexdigest()
    if json.loads((root / 'job.json').read_text())['plan_sha256'] != plan_hash:
        raise ValueError('capture plan hash changed')
    carriers = {json.dumps(row['surface'], sort_keys=True): row['id'] for row in budget['carriers']}
    conditions, pairs, cases, active, measured_resident, warm_resident = [], [], [], [], [], []
    models, identities, scenarios, projections, frame_deltas = [], [], [], [], []
    for case in plan['cases']:
        path = root / case['id'] / 'carrier-screen' / 'report.json'
        report = json.loads(path.read_text())
        oracle = json.loads(path.with_name('oracle-check.json').read_text())
        raw_path = root / case['id'] / 'raw' / 'capture.json'
        raw = json.loads(raw_path.read_text())
        identity = 'capture-' + hashlib.sha256(raw_path.read_bytes()).hexdigest()
        if (not report['success'] or not report['restored'] or
                oracle['max_absolute_metric_difference'] > 1e-10 or
                report['quality_reference']['dataset_id'] != identity or
                raw['environment_family'] != case['recipe']['world']['family']):
            raise ValueError('complete matching direct-reference reports required')
        identities.append(identity)
        scenarios.append(report['scenario_sha256'])
        projections.append(json.dumps(report['quality_reference']['view'], sort_keys=True))
        models.append([item['camera_model_sha256'] for item in report['quality_reference']['frames'][0]['inputs']])
        rows, repeats, frame_values = {}, {}, {}
        for sample in report['samples']:
            resources = sample['metadata']['mesh_resources']
            mesh = resources['active']
            if (not budget['triangles_min'] <= mesh['triangles'] <= budget['triangles_max'] or
                    mesh['vertices'] > budget['vertices_max'] or mesh['buffer_bytes'] > budget['buffer_bytes_max']):
                raise ValueError('active mesh budget violated')
            active.append(mesh)
            (warm_resident if sample['warmup'] else measured_resident).append(resources['resident']['buffer_bytes'])
            if not sample['warmup']:
                repeats.setdefault((sample['frame_index'], sample['variant']), set()).add(sample['rgba_sha256'])
                variant = report['scenario']['variants'][sample['variant']]
                carrier = carriers[json.dumps(variant['surface'], sort_keys=True)]
                frame_values[sample['frame_index'], carrier, variant['mode'], variant['pyramid_boundary']] = sample['quality']['mae_rgb8']
        if any(len(values) != 1 for values in repeats.values()):
            raise ValueError('paused RGB repeats changed')
        for (frame, carrier, mode, boundary), value in frame_values.items():
            if boundary == 'zero':
                frame_deltas.append(frame_values[frame, carrier, mode, 'normalized'] - value)
        for variant, quality in zip(report['scenario']['variants'], report['quality_summaries']):
            carrier = carriers[json.dumps(variant['surface'], sort_keys=True)]
            key = carrier, variant['mode'], variant['pyramid_boundary']
            if key in rows:
                raise ValueError('duplicate carrier condition')
            rows[key] = quality['pixel_weighted_mae_rgb8']
            conditions.append(dict(case=case['id'], carrier=carrier, mode=key[1], boundary=key[2], **quality))
        for carrier in carriers.values():
            for mode in ('multi_band', 'graph_cut_multi_band'):
                zero, normalized = rows[carrier, mode, 'zero'], rows[carrier, mode, 'normalized']
                pairs.append(dict(case=case['id'], carrier=carrier, mode=mode, zero=zero,
                                  normalized=normalized, delta=normalized-zero))
        cases.append(dict(case=case['id'], total_samples=len(report['samples']), measured=oracle['samples'],
            oracle_comparisons=oracle['comparisons'], max_metric_difference=oracle['max_absolute_metric_difference'],
            report_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), dataset_id=identity,
            reference=report['quality_reference'], capture_script_sha256=raw['script_sha256']))
    if (len(set(identities)) != len(identities) or any(model != models[0] for model in models)
            or len(set(scenarios)) != 1 or len(set(projections)) != 1):
        raise ValueError('different captures and equal controlled mount models required')
    return dict(schema_version=1, purpose='matched environment/carrier pilot; not confirmatory holdout',
        plan_sha256=plan_hash, scenario_sha256=scenarios[0], cases=cases, conditions=conditions, pairs=pairs,
        per_frame_boundary_contrasts=dict(count=len(frame_deltas), improved=sum(x < 0 for x in frame_deltas),
                                         delta_min=min(frame_deltas), delta_max=max(frame_deltas)),
        active_triangle_range=[min(x['triangles'] for x in active), max(x['triangles'] for x in active)],
        active_buffer_range=[min(x['buffer_bytes'] for x in active), max(x['buffer_bytes'] for x in active)],
        measured_resident_buffer_range=[min(measured_resident), max(measured_resident)],
        warmup_resident_buffer_range=[min(warm_resident), max(warm_resident)], paused_rgb_repeats_identical=True)


parser = argparse.ArgumentParser()
inputs = parser.add_mutually_exclusive_group(required=True)
inputs.add_argument('--run', type=Path)
inputs.add_argument('--summary', type=Path)
inputs.add_argument('--suite', type=Path)
repo = Path(__file__).resolve().parents[2]
parser.add_argument('--plan', type=Path, default=repo / 'assets/scenarios/quality-families-v1.json')
parser.add_argument('--budget-plan', type=Path, default=repo / 'configs/research/carrier-budget-plan.json')
parser.add_argument('--carrier')
parser.add_argument('--output', type=Path)
args = parser.parse_args()
if args.suite:
    summary = summarize_suite(args.suite, args.plan, args.budget_plan)
    args.summary = args.suite / 'carrier-summary.json'
    args.summary.write_text(json.dumps(summary, indent=2) + '\n')
if args.output is None:
    name = 'family_carrier_deltas.png' if args.summary else 'native_quality_parity.png'
    args.output = Path(__file__).parent / 'figures/experiments' / name
if args.summary:
    data = json.loads(args.summary.read_text())
    cases = [case['case'] for case in data['cases']]
    carriers = list(dict.fromkeys(row['carrier'] for row in data['pairs']))
    scale = max(abs(row['delta']) for row in data['pairs'])
    fig, axes = plt.subplots(1, 2, figsize=(12, 4), layout='constrained')
    for axis, mode in zip(axes, ('multi_band', 'graph_cut_multi_band')):
        rows = {(row['case'], row['carrier']): row['delta'] for row in data['pairs'] if row['mode'] == mode}
        matrix = np.array([[rows[case, carrier] for carrier in carriers] for case in cases])
        shown = axis.imshow(matrix, cmap='RdBu_r', vmin=-scale, vmax=scale)
        axis.set_xticks(range(len(carriers)), carriers, rotation=25, ha='right')
        axis.set_yticks(range(len(cases)), cases)
        axis.set_title(mode)
        for y, case in enumerate(cases):
            for x, carrier in enumerate(carriers):
                axis.text(x, y, f'{matrix[y,x]:.3f}', ha='center', va='center', fontsize=9,
                          color='white' if abs(matrix[y,x]) > scale * .5 else 'black')
    fig.colorbar(shown, ax=axes, label='MAE RGB8: normalized − zero')
    fig.suptitle('Парные изменения ошибки при одинаковом carrier и входах')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=160)
    plt.close(fig)
    print(args.output)
    raise SystemExit(0)
report_path = args.run / 'report.json'
report = json.loads(report_path.read_text())
check = json.loads((args.run / 'oracle-check.json').read_text())
if (not report['success'] or not report['restored'] or check['max_absolute_metric_difference'] > 1e-10
        or report['quality_reference']['kind'] != 'independent_ground_truth'):
    raise ValueError('completed direct-reference parity report required')
reference = report['quality_reference']['frames'][0]
fig, axes = plt.subplots(2, 3, figsize=(12, 5.7), layout='constrained')
axes[0, 0].imshow(Image.open(args.run / reference['rgb']))
axes[0, 0].set_title('Прямой Blender RGB, кадр 0', fontsize=11)
axes[1, 0].imshow(Image.open(args.run / reference['mask']))
axes[1, 0].set_title(f"Заданная ROI: {reference['selected_pixels']} пикселей", fontsize=11)
variants = list(enumerate(report['scenario']['variants']))
if args.carrier:
    variants = [(index, variant) for index, variant in variants
                if variant.get('surface', {}).get('type') == args.carrier]
if len(variants) != 4:
    raise ValueError('select exactly four fusion/boundary conditions; use --carrier for a carrier matrix')
for position, (index, variant) in enumerate(variants):
    sample = next(s for s in report['samples'] if not s['warmup'] and s['frame_index'] == 0 and s['variant'] == index)
    h, w = sample['metadata']['height'], sample['metadata']['width']
    rgba = np.frombuffer((args.run / sample['rgba_file']).read_bytes(), np.uint8).reshape(h, w, 4)
    row, column = position // 2, 1 + position % 2
    axes[row, column].imshow(rgba[..., :3])
    axes[row, column].set_title(f"{variant['mode']} / {variant['pyramid_boundary']}\n"
                               f"MAE RGB8 = {sample['quality']['mae_rgb8']:.3f}", fontsize=10)
for axis in axes.flat:
    axis.set_axis_off()
fig.suptitle('Проверка C++ оценщика на сохранённом Blender-наборе', fontsize=14)
args.output.parent.mkdir(parents=True, exist_ok=True)
fig.savefig(args.output, dpi=160, metadata={
    'Description': 'Validation fixture metric parity; not confirmatory holdout. Report SHA256: ' +
                   hashlib.sha256(report_path.read_bytes()).hexdigest()})
plt.close(fig)
print(args.output)
