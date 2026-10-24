"""Independent numerical fixture/plots for server E1 pilot. Does not fit any camera."""
import argparse
import copy
import hashlib
import json
from pathlib import Path

import numpy as np


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def project(camera, transform, points):
    optical = np.asarray(points) @ transform[:3, :3].T + transform[:3, 3]
    x, y, z = optical.T
    radius = np.hypot(x, y)
    theta = np.arctan2(radius, z)
    p = camera['projection']
    distorted = theta.copy()
    for i, k in enumerate(p['k']):
        distorted += k * theta ** (2 * i + 3)
    scale = np.divide(distorted, radius, out=np.zeros_like(radius), where=radius != 0)
    uv = np.column_stack((p['fx'] * (x + p.get('alpha', 0) * y) * scale + p['cx'],
                          p['fy'] * y * scale + p['cy']))
    assert np.all(z > 0) and np.all(theta < p['theta_max_rad'])
    return uv


def prepare(config_path, directory, plan_path=None):
    config = json.loads(config_path.read_text())
    plan = json.loads(plan_path.read_text()) if plan_path else None
    if plan:
        assert plan['policy'] == 'thesis_e1_spread_v2'
        assert (plan_path.parent / plan['rig_config']).resolve() == config_path.resolve()
        assert plan['training_poses'] == [0, 1, 2, 3] and plan['validation_poses'] == [4, 5]
        assert plan['board_inner_corners'] == [6, 4] and plan['board_spacing_m'] == .04
        assert plan['noise_sigma_px'] == .5 and plan['outliers'] == 10
        assert len(plan['optical_board_centres_m']) == len(plan['board_yaw_rad']) == 6
    seeds = plan['mount_seeds'] if plan else [9001]
    cases, truth = [], []
    for seed in seeds:
        mount_rng = np.random.default_rng(seed)  # Pilot seed; excluded from E1 holdout.
        for camera in config['cameras']:
            index = camera['id']
            nominal = np.asarray(camera['T_camera_from_vehicle'], dtype=float)
            yaw, pitch = mount_rng.uniform(-np.deg2rad(3), np.deg2rad(3), 2)
            shift = mount_rng.uniform(-.05, .05)
            cy, sy, cp, sp = np.cos(yaw), np.sin(yaw), np.cos(pitch), np.sin(pitch)
            rz = np.array([[cy, sy, 0], [-sy, cy, 0], [0, 0, 1]])
            rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
            rotation = rx @ nominal[:3, :3] @ rz
            center = -nominal[:3, :3].T @ nominal[:3, 3] + [shift, 0, 0]
            actual = np.eye(4)
            actual[:3, :3], actual[:3, 3] = rotation, -rotation @ center
            groups = []
            # Six fixed planar targets; their aggregate is noncoplanar. No output-based selection.
            for pose in range(6):
                a = plan['board_yaw_rad'][pose] if plan else .08 * (pose - 2.5)
                board_rotation = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0],
                                           [-np.sin(a), 0, np.cos(a)]])
                board = np.array([[(column - 2.5) * .04, (row - 1.5) * .04, 0]
                                  for row in range(4) for column in range(6)])
                centre = (plan['optical_board_centres_m'][pose] if plan else
                          [.12 * (pose % 3 - 1), .08 * (pose % 2), 2.8 + .25 * pose])
                optical = board @ board_rotation.T + centre
                xyz = (optical - actual[:3, 3]) @ rotation
                uv = project(camera, actual, xyz)
                assert (uv[:, 0] >= 0).all() and (uv[:, 0] < camera['resolution']['width']).all()
                assert (uv[:, 1] >= 0).all() and (uv[:, 1] < camera['resolution']['height']).all()
                noise_rng = np.random.default_rng([91000, seed, index, pose] if plan else 91000 + 100 * index + pose)
                noise = noise_rng.normal(0, .5, uv.shape)
                groups.append((xyz, uv + noise))
            train = np.concatenate([g[0] for g in groups[:4]])
            singular = np.linalg.svd(train - train.mean(axis=0), compute_uv=False)
            assert singular[-1] > 1e-8 * singular[0]
            train_uv = np.concatenate([g[1] for g in groups[:4]])
            validation = np.concatenate([g[0] for g in groups[4:]])
            validation_uv = np.concatenate([g[1] for g in groups[4:]])
            def provenance(poses):
                return dict(observation_ids=[f'cam{index}-pose{p}-corner{c}' for p in poses for c in range(24)],
                            frame_ids=[f'cam{index}-pose{p}' for p in poses for _ in range(24)])
            for profile in ('gaussian', 'outliers'):
                observed = train_uv.copy()
                if profile == 'outliers':
                    rng = np.random.default_rng([92000, seed, index] if plan else 92000 + index)
                    selected = rng.choice(96, 10, replace=False)  # Rounded 10% of 96.
                    observed[selected] += rng.uniform(-10, 10, (10, 2))
                for method in ('sqpnp', 'ransac_epnp_lm'):
                    prefix = f'mount{seed}/' if plan else ''
                    cases.append(dict(case_id=prefix + f'cam{index}/{profile}/{method}', parameters=dict(
                        camera_id=index, method=method, points=train.tolist(), pixels=observed.tolist(),
                        validation_points=validation.tolist(), validation_pixels=validation_uv.tolist(),
                        provenance=dict(dataset_id=(f'thesis-e1-spread-v2-mount{seed}' if plan else 'thesis-e1-numerical-pilot-9001'),
                                        training=provenance(range(4)), validation=provenance(range(4, 6))))))
            truth.append(dict(camera=copy.deepcopy(camera), mount_seed=seed, true_transform=actual.tolist(),
                              yaw_rad=float(yaw), pitch_rad=float(pitch), along_body_m=float(shift),
                              validation_points=validation.tolist(), validation_pixels=validation_uv.tolist()))
    np.random.default_rng(93001).shuffle(cases)
    directory.mkdir(exist_ok=False)
    (directory / 'cases.json').write_text(json.dumps(dict(schema_version=1, cases=cases), indent=2) + '\n')
    (directory / 'truth.json').write_text(json.dumps(dict(schema_version=1,
        scope=('registered_numerical_e1_v2_not_detector_or_scene_confirmation' if plan else
               'numerical_preflight_not_blender_or_detector_or_holdout'), config_sha256=sha(config_path),
        input_plan_sha256=sha(plan_path) if plan_path else None, input_plan=plan,
        numpy_version=np.__version__, generator_sha256=sha(Path(__file__)), mount_seeds=seeds, cameras=truth), indent=2) + '\n')


def evaluate(report_path, truth_path, figure, audit_path):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    report = json.loads(report_path.read_text())
    truth = json.loads(truth_path.read_text())
    planned = 4 * len(truth['cameras'])
    assert report['success'] and report['planned'] == planned and len(report['results']) == planned
    assert report['initial_state']['config_revision'] == report['final_state']['config_revision']
    assert report['input_sha256'] == sha(truth_path.parent / 'cases.json')
    rows = []
    for result in report['results']:
        parts = result['case_id'].split('/')
        camera_id, profile, method = parts[-3:]
        seed = int(parts[0][5:]) if len(parts) == 4 else 9001
        index = int(camera_id[3:])
        record = next(x for x in truth['cameras'] if x['camera']['id'] == index and x.get('mount_seed', 9001) == seed)
        row = dict(mount_seed=seed, camera_id=index, profile=profile, method=method, outcome=result['outcome'])
        if result['outcome'] == 'completed':
            status = result['status']
            if truth.get('input_plan'):
                assert status['execution_policy']['method'] == method
                assert status['execution_policy']['rng_policy'] == 'backend_owned_no_seed_parameter'
            row['execution_policy'] = status.get('execution_policy')
            fitted = np.asarray(status['T_camera_from_vehicle'])
            actual = np.asarray(record['true_transform'])
            nominal = np.asarray(record['camera']['T_camera_from_vehicle'])
            center = lambda t: -t[:3, :3].T @ t[:3, 3]
            rotation_error = np.rad2deg(np.arccos(np.clip((np.trace(fitted[:3, :3] @ actual[:3, :3].T) - 1) / 2, -1, 1)))
            uv = project(record['camera'], fitted, record['validation_points'])
            observed = np.asarray(record['validation_pixels'])
            rmse = np.sqrt(np.mean(np.sum((uv - observed) ** 2, axis=1)))
            assert abs(rmse - status['validation_rmse_px']) < 1e-7
            row.update(center_error_mm=float(np.linalg.norm(center(fitted) - center(actual)) * 1000),
                       nominal_center_error_mm=float(np.linalg.norm(center(nominal) - center(actual)) * 1000),
                       rotation_error_deg=float(rotation_error), validation_rmse_px=float(rmse),
                       training_rmse_px=status['training_rmse_px'], duration_ms=status['duration_ms'],
                       quality_accepted=status['quality_accepted'])
        rows.append(row)
    fig, axes = plt.subplots(1, 3, figsize=(12, 3.6), constrained_layout=True)
    labels = ['gaussian\nSQPnP', 'gaussian\nRANSAC+LM', 'outliers\nSQPnP', 'outliers\nRANSAC+LM']
    groups = [(p, m) for p in ('gaussian', 'outliers') for m in ('sqpnp', 'ransac_epnp_lm')]
    for ax, metric, title in zip(axes, ['center_error_mm', 'rotation_error_deg', 'validation_rmse_px'],
                                 ['Camera centre error, mm', 'Rotation error, deg', 'Held-out reprojection RMSE, px']):
        for column, (profile, method) in enumerate(groups):
            values = [r[metric] for r in rows if r['outcome'] == 'completed' and r['profile'] == profile and r['method'] == method]
            ax.scatter([column] * len(values), values, s=25)
        ax.set_xticks(range(4), labels, fontsize=8)
        ax.set_title(title, fontsize=10)
        ax.set_ylim(bottom=0)
        ax.grid(axis='y', alpha=.25)
    fig.suptitle(f'E1 numerical calibration: {len(truth["cameras"])} cameras; server fits only', fontsize=11)
    figure.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(figure, dpi=140)
    plt.close(fig)
    audit_path.parent.mkdir(parents=True, exist_ok=True)
    audit_path.write_text(json.dumps(dict(schema_version=1, scope=truth['scope'],
        report_sha256=sha(report_path), truth_sha256=sha(truth_path), cases_sha256=report['input_sha256'],
        catalog=report['catalog'], client_build_passport=report['client_build_passport'],
        input_plan=truth.get('input_plan'), input_plan_sha256=truth.get('input_plan_sha256'),
        generator_sha256=truth.get('generator_sha256'), numpy_version=truth.get('numpy_version'),
        configuration_unchanged=True, results=rows, figure_sha256=sha(figure)), indent=2) + '\n')
    print(json.dumps(dict(completed=sum(r['outcome'] == 'completed' for r in rows), planned=planned)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    prepare_parser = sub.add_parser('prepare')
    prepare_parser.add_argument('config', type=Path)
    prepare_parser.add_argument('new_directory', type=Path)
    prepare_parser.add_argument('--plan', type=Path)
    bridge_parser = sub.add_parser('prepare-e3')
    for name in ('scene_plan', 'truth', 'report', 'new_directory'):
        bridge_parser.add_argument(name, type=Path)
    audit_parser = sub.add_parser('audit-e3')
    for name in ('prepared', 'truth', 'report', 'project_binary', 'audit'):
        audit_parser.add_argument(name, type=Path)
    plot_parser = sub.add_parser('plot')
    for name in ('report', 'truth', 'figure', 'audit'):
        plot_parser.add_argument(name, type=Path)
    args = parser.parse_args()
    if args.mode == 'prepare':
        prepare(args.config, args.new_directory, args.plan)
    elif args.mode == 'audit-e3':
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/blender'))
        from thesis_bridge import audit_e3
        audit_e3(args.prepared, args.truth, args.report, args.project_binary, args.audit)
    elif args.mode == 'prepare-e3':
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/blender'))
        from thesis_bridge import export_e3
        export_e3(args.scene_plan, args.truth, args.report, args.new_directory)
    else:
        evaluate(args.report, args.truth, args.figure, args.audit)


if __name__ == '__main__':
    main()
