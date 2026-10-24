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


def prepare(config_path, directory):
    config = json.loads(config_path.read_text())
    cases, truth = [], []
    mount_rng = np.random.default_rng(9001)  # Pilot seed; excluded from E1 holdout.
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
            a = .08 * (pose - 2.5)
            board_rotation = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0],
                                       [-np.sin(a), 0, np.cos(a)]])
            board = np.array([[(column - 2.5) * .04, (row - 1.5) * .04, 0]
                              for row in range(4) for column in range(6)])
            optical = board @ board_rotation.T + [.12 * (pose % 3 - 1), .08 * (pose % 2), 2.8 + .25 * pose]
            xyz = (optical - actual[:3, 3]) @ rotation
            uv = project(camera, actual, xyz)
            assert (uv[:, 0] >= 0).all() and (uv[:, 0] < camera['resolution']['width']).all()
            assert (uv[:, 1] >= 0).all() and (uv[:, 1] < camera['resolution']['height']).all()
            noise = np.random.default_rng(91000 + 100 * index + pose).normal(0, .5, uv.shape)
            groups.append((xyz, uv + noise))
        train = np.concatenate([g[0] for g in groups[:4]])
        train_uv = np.concatenate([g[1] for g in groups[:4]])
        validation = np.concatenate([g[0] for g in groups[4:]])
        validation_uv = np.concatenate([g[1] for g in groups[4:]])
        def provenance(poses):
            return dict(observation_ids=[f'cam{index}-pose{p}-corner{c}' for p in poses for c in range(24)],
                        frame_ids=[f'cam{index}-pose{p}' for p in poses for _ in range(24)])
        for profile in ('gaussian', 'outliers'):
            observed = train_uv.copy()
            if profile == 'outliers':
                rng = np.random.default_rng(92000 + index)
                selected = rng.choice(96, 10, replace=False)  # Rounded 10% of 96.
                observed[selected] += rng.uniform(-10, 10, (10, 2))
            for method in ('sqpnp', 'ransac_epnp_lm'):
                cases.append(dict(case_id=f'cam{index}/{profile}/{method}', parameters=dict(
                    camera_id=index, method=method, points=train.tolist(), pixels=observed.tolist(),
                    validation_points=validation.tolist(), validation_pixels=validation_uv.tolist(),
                    provenance=dict(dataset_id='thesis-e1-numerical-pilot-9001',
                                    training=provenance(range(4)), validation=provenance(range(4, 6))))))
        truth.append(dict(camera=copy.deepcopy(camera), true_transform=actual.tolist(),
                          yaw_rad=float(yaw), pitch_rad=float(pitch), along_body_m=float(shift),
                          validation_points=validation.tolist(), validation_pixels=validation_uv.tolist()))
    np.random.default_rng(93001).shuffle(cases)
    directory.mkdir(exist_ok=False)
    (directory / 'cases.json').write_text(json.dumps(dict(schema_version=1, cases=cases), indent=2) + '\n')
    (directory / 'truth.json').write_text(json.dumps(dict(schema_version=1,
        scope='numerical_preflight_not_blender_or_detector_or_holdout', config_sha256=sha(config_path),
        numpy_version=np.__version__, seed=9001, cameras=truth), indent=2) + '\n')


def evaluate(report_path, truth_path, figure, audit_path):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    report = json.loads(report_path.read_text())
    truth = json.loads(truth_path.read_text())
    assert report['success'] and report['planned'] == 16 and len(report['results']) == 16
    assert report['initial_state']['config_revision'] == report['final_state']['config_revision']
    assert report['input_sha256'] == sha(truth_path.parent / 'cases.json')
    rows = []
    for result in report['results']:
        camera_id, profile, method = result['case_id'].split('/')
        index = int(camera_id[3:])
        record = truth['cameras'][index]
        row = dict(camera_id=index, profile=profile, method=method, outcome=result['outcome'])
        if result['outcome'] == 'completed':
            status = result['status']
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
    fig.suptitle('E1 numerical preflight: four cameras, one mount seed; server fits only', fontsize=11)
    figure.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(figure, dpi=140)
    plt.close(fig)
    audit_path.parent.mkdir(parents=True, exist_ok=True)
    audit_path.write_text(json.dumps(dict(schema_version=1, scope=truth['scope'],
        report_sha256=sha(report_path), truth_sha256=sha(truth_path), cases_sha256=report['input_sha256'],
        catalog=report['catalog'], client_build_passport=report['client_build_passport'],
        configuration_unchanged=True, results=rows, figure_sha256=sha(figure)), indent=2) + '\n')
    print(json.dumps(dict(completed=sum(r['outcome'] == 'completed' for r in rows), planned=16)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    prepare_parser = sub.add_parser('prepare')
    prepare_parser.add_argument('config', type=Path)
    prepare_parser.add_argument('new_directory', type=Path)
    plot_parser = sub.add_parser('plot')
    for name in ('report', 'truth', 'figure', 'audit'):
        plot_parser.add_argument(name, type=Path)
    args = parser.parse_args()
    if args.mode == 'prepare':
        prepare(args.config, args.new_directory)
    else:
        evaluate(args.report, args.truth, args.figure, args.audit)


if __name__ == '__main__':
    main()
