"""Frozen residual audit plus explicitly post-hoc geometry details and figures."""
import argparse
import json
import sys
from pathlib import Path
import numpy as np
import matplotlib.pyplot as plt
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/research'))
from visibility_residual import collect
from source_correspondence import load_product, sha
from source_correspondence_controls import transform_points
from source_visibility import blocked_by_faces


def details(report, plan):
    truth = json.loads((ROOT / plan['truth_path']).read_text())
    for row in report['rows']:
        frame, variant = row['frame'], row['variant']
        name = f'frame{frame}-{variant}'
        d = ROOT / plan['output_root'] / name
        a = {n: np.load(d / n, allow_pickle=False) for n in row['scene_record']['files']}
        native = ROOT / plan['native_root'] / name
        meta = json.loads((native / 'report.json').read_text())
        p = {v['file']: load_product(native, v) for v in meta['products']}
        y, x = a['pixels.npy'].T
        vehicle = p['carrier-positions.f32'][y, x]
        pose = np.asarray(truth['frames'][frame]['T_world_from_vehicle'])
        world = transform_points(pose, vehicle)
        far = np.zeros(len(y), bool)
        target_centers = []
        for camera, cfg in enumerate(meta['config']['cameras']):
            road = a['residual.npy'] & (a['center-kinds.npy'][:, camera] == 2)
            distance = np.linalg.norm(world - a['center-hits.npy'][:, camera], axis=1)
            # Descriptive post-hoc threshold reuses the configured 2cm tolerance, not tuning.
            far |= road & (distance > .02)
            origin_vehicle = np.linalg.inv(cfg['T_camera_from_vehicle'])[:3, 3]
            origin_world = transform_points(pose, np.asarray([origin_vehicle]))[0]
            for n in np.flatnonzero(a['residual.npy'] & (a['center-kinds.npy'][:, camera] == 1)):
                box = meta['config']['surface']['source_occlusion_box']
                target_centers.append(dict(pixel_yx=[int(y[n]), int(x[n])], camera=camera,
                    carrier_vehicle_m=vehicle[n].tolist(), source_hit_world_m=a['center-hits.npy'][n, camera].tolist(),
                    carrier_minus_hit_range_m=float(np.linalg.norm(world[n]-origin_world)-np.linalg.norm(a['center-hits.npy'][n,camera]-origin_world)),
                    supplied_box_blocked_at_zero_tolerance=bool(blocked_by_faces(origin_vehicle, [vehicle[n]], box['min_m'], box['max_m'], 0)[0]),
                    supplied_box_blocked_at_configured_tolerance=bool(blocked_by_faces(origin_vehicle, [vehicle[n]], box['min_m'], box['max_m'], box['tolerance_m'])[0])))
        row['posthoc_geometry'] = dict(residual_pixels_with_source_road_hit_distance_over_2cm=int(far.sum()), target_center_pairs=target_centers)
    report['posthoc_scope'] = 'Descriptive geometry drill-down after primary frozen audit; no native counterfactual rendering or threshold optimization'
    report['figure_and_detail_script_sha256'] = sha(__file__)


def plots(report, plan, output):
    output.mkdir(parents=True, exist_ok=True)
    rows = report['rows']
    labels = [f"F{r['frame']} " + ('plane' if r['variant'].startswith('plane') else 'local') for r in rows]
    fig, ax = plt.subplots(figsize=(10, 5), layout='constrained')
    bottom = np.zeros(len(rows))
    categories = [('positive_weight_target_center_pixels', 'Target at source center', '#d95f02'),
                  ('neighbor', 'Target at neighboring texel center', '#7570b3'),
                  ('colored_texel_without_target_center_texel_pixels', 'Colored texel, no target-center texel', '#1b9e77')]
    for key, label, color in categories:
        values = np.array([r['residual']['positive_weight_target_texel_pixels'] - r['residual']['positive_weight_target_center_pixels'] if key == 'neighbor' else r['residual'][key] for r in rows])
        ax.bar(labels, values, bottom=bottom, label=label, color=color)
        for i, v in enumerate(values):
            if v >= 10: ax.text(i, bottom[i]+v/2, str(v), ha='center', va='center', color='white', fontsize=9)
        bottom += values
    ax.set(ylabel='Residual output pixels', title='All 497 residual pixels: source RGB footprint support is distinct from center-ray identity')
    ax.legend(loc='upper right')
    fig.savefig(output / 'visibility_residual_support.png', dpi=160)
    plt.close(fig)
    fig, axes = plt.subplots(2, 2, figsize=(12, 8), layout='constrained')
    truth = json.loads((ROOT / plan['truth_path']).read_text())
    cap = (ROOT / plan['truth_path']).parent
    target_id = next(v for k,v in truth['objects'].items() if 'coded object target' in k)
    for ax, row, label in zip(axes.flat, rows, labels):
        name = f"frame{row['frame']}-{row['variant']}"
        native = ROOT / plan['native_root'] / name
        meta = json.loads((native / 'report.json').read_text())
        rgb = load_product(native, next(p for p in meta['products'] if p['file'] == 'output.rgba'))
        a = ROOT / plan['output_root'] / name
        coords = np.load(a / 'pixels.npy');residual = np.load(a / 'residual.npy')
        mask = np.load(cap / truth['frames'][row['frame']]['objects']) == target_id
        ax.imshow(rgb)
        ax.contour(mask.astype(float), [.5], colors=['cyan'], linewidths=.8)
        ax.scatter(coords[residual,1], coords[residual,0], s=5, c='yellow', label='Residual proxy')
        for point in row['posthoc_geometry']['target_center_pairs']:
            y,x = point['pixel_yx'];ax.scatter([x], [y], c='red', marker='x', s=35)
        ax.set(xlim=(180,550), ylim=(359.5,165), title=f"{label}: {row['residual']['pixels']} residual pixels", xlabel='Output x / px', ylabel='Output y / px')
    fig.suptitle('Actual frozen native RGB; yellow=residual, cyan=independent target, red=two source-center target hits')
    fig.savefig(output / 'visibility_residual_pixels.png', dpi=160)
    plt.close(fig)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, default=ROOT / 'configs/research/visibility-residual-plan.json')
    parser.add_argument('--output-report', type=Path, required=True)
    parser.add_argument('--figures', type=Path, default=Path(__file__).parent / 'figures/experiments')
    args = parser.parse_args()
    report = collect(ROOT, args.plan)
    plan = json.loads(args.plan.read_text())
    details(report, plan)
    args.output_report.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    plots(report, plan, args.figures)
